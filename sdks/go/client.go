// Package chirp implements the game-backend client for chirp's server
// gateway plane: a trusted service (game server, trade service, NPC engine)
// dials out to chirp_server_gateway, authenticates with a service_id +
// shared secret, and then injects messages into the chat plane and
// publishes reliable events toward other services.
//
// The wire contract mirrors the C++ reference peer
// (services/chat/src/server_gateway_peer.cc): length-prefixed Packet frames
// ([u32 BE len][Packet protobuf], 4MB cap), SERVER_AUTH_REQ as the first
// frame, a server-assigned heartbeat cadence, sequence-correlated
// request/response RPCs, and at-least-once event delivery that the consumer
// acknowledges by event id. A dropped connection fails every in-flight call
// and the client reconnects (fixed delay, like the C++ peer); events not
// acked before the drop are redelivered by the hub with an incremented
// attempt counter.
//
// Callbacks (inject/event/disconnect handlers) run on the client's internal
// read goroutine and must not block. All other methods are safe for
// concurrent use.
package chirp

import (
	"context"
	"encoding/binary"
	"errors"
	"fmt"
	"io"
	"log/slog"
	"net"
	"sync"
	"time"

	"google.golang.org/protobuf/proto"

	pbcommon "github.com/cui/chirp/proto/go/common"
	pbgw "github.com/cui/chirp/proto/go/gateway"
	pbsg "github.com/cui/chirp/proto/go/server_gateway"
)

// Errors returned by the client. Use errors.Is to test for them.
var (
	// ErrClosed is returned once Stop has been called.
	ErrClosed = errors.New("chirp: client stopped")
	// ErrNotConnected is returned by RPCs issued before the connection is
	// authenticated (the C++ peer answers those with SERVER_UNAVAILABLE).
	ErrNotConnected = errors.New("chirp: not connected")
	// ErrConnectionLost is returned by RPCs that were in flight when the
	// connection dropped; the client reconnects on its own.
	ErrConnectionLost = errors.New("chirp: connection lost")
	// ErrBadFrame is an internal signal: the peer violated the framing
	// contract (zero or oversized frame) and the connection must be dropped.
	ErrBadFrame = errors.New("chirp: invalid frame from server")
)

// AuthError reports that the hub rejected the service credentials; the
// client keeps retrying with its reconnect delay, like the C++ peer.
type AuthError struct {
	Code pbcommon.ErrorCode
}

func (e *AuthError) Error() string {
	return fmt.Sprintf("chirp: auth rejected (code=%d)", e.Code)
}

// ServerError reports an RPC that completed with a non-OK ErrorCode from the
// hub (e.g. a duplicate inject_id routed as an error, or an unknown target
// service for an event).
type ServerError struct {
	Code pbcommon.ErrorCode
}

func (e *ServerError) Error() string {
	return fmt.Sprintf("chirp: server returned code=%d", e.Code)
}

// Config configures a Client. Zero values fall back to the defaults below.
type Config struct {
	Host string
	Port int

	// ServiceID and Secret are the trust-gate credentials checked by the
	// hub's SERVER_AUTH_REQ handler.
	ServiceID string
	Secret    string

	// HeartbeatInterval is the fallback cadence used when the hub does not
	// assign one in ServerAuthResponse. Default 30s.
	HeartbeatInterval time.Duration
	// ReconnectDelay is the fixed wait between reconnect attempts (the C++
	// peer's reconnect_delay_seconds). Default 5s.
	ReconnectDelay time.Duration
	// DialTimeout bounds a single TCP dial attempt. Default 10s.
	DialTimeout time.Duration

	// MaxFrameBytes is the inbound frame cap (mirrors the hub's 4MB).
	// Default 4MB.
	MaxFrameBytes int

	// Logger receives lifecycle warnings (connect loss, auth rejection,
	// dropped stray responses). Default slog.Default().
	Logger *slog.Logger
}

const (
	defaultHeartbeat  = 30 * time.Second
	defaultReconnect  = 5 * time.Second
	defaultDial       = 10 * time.Second
	defaultMaxFrame   = 4 * 1024 * 1024
	frameWriteTimeout = 10 * time.Second
)

func (c *Config) applyDefaults() {
	if c.HeartbeatInterval <= 0 {
		c.HeartbeatInterval = defaultHeartbeat
	}
	if c.ReconnectDelay <= 0 {
		c.ReconnectDelay = defaultReconnect
	}
	if c.DialTimeout <= 0 {
		c.DialTimeout = defaultDial
	}
	if c.MaxFrameBytes <= 0 {
		c.MaxFrameBytes = defaultMaxFrame
	}
	if c.Logger == nil {
		c.Logger = slog.Default()
	}
}

type pendingCall struct {
	respID   pbgw.MsgID
	resp     proto.Message
	validate func() error
	ch       chan error
}

// Client is a dial-out connection from a trusted service to
// chirp_server_gateway. Create one with NewClient, register handlers, and
// call Start; Stop tears the client down for good.
type Client struct {
	cfg Config
	log *slog.Logger

	stopOnce sync.Once
	stopCh   chan struct{}

	// handlerMu guards the three user callbacks, which may be registered
	// before or after Start.
	handlerMu    sync.RWMutex
	onInject     func(*pbsg.InjectMessageNotify)
	onEvent      func(*pbsg.EventDeliverNotify)
	onDisconnect func(err error)

	// mu guards the connection-scoped state below.
	mu      sync.Mutex
	conn    net.Conn
	authed  bool
	closed  bool
	seq     uint64
	pending map[uint64]*pendingCall
	authCh  chan error // one-shot SERVER_AUTH_RESP waiter per attempt

	writeMu sync.Mutex // serializes frame writes on the current conn
}

// NewClient returns a client for the given endpoint and credentials. It does
// not dial until Start.
func NewClient(cfg Config) *Client {
	cfg.applyDefaults()
	return &Client{
		cfg:     cfg,
		log:     cfg.Logger,
		stopCh:  make(chan struct{}),
		pending: make(map[uint64]*pendingCall),
	}
}

// SetInjectHandler registers the callback for INJECT_MESSAGE_NOTIFY frames —
// injections the chat plane routed back to this service. Called on the read
// goroutine; must not block.
func (c *Client) SetInjectHandler(h func(*pbsg.InjectMessageNotify)) {
	c.handlerMu.Lock()
	c.onInject = h
	c.handlerMu.Unlock()
}

// SetEventHandler registers the callback for EVENT_DELIVER_NOTIFY frames —
// reliable events addressed to this service (at-least-once: acknowledge
// processed events with AckEvents, or the hub redelivers them). Called on
// the read goroutine; must not block.
func (c *Client) SetEventHandler(h func(*pbsg.EventDeliverNotify)) {
	c.handlerMu.Lock()
	c.onEvent = h
	c.handlerMu.Unlock()
}

// SetDisconnectHandler registers a callback fired when an authenticated
// connection drops. Auth rejections and failed dials only go to the log.
// Called on the client's loop goroutine; must not block.
func (c *Client) SetDisconnectHandler(h func(err error)) {
	c.handlerMu.Lock()
	c.onDisconnect = h
	c.handlerMu.Unlock()
}

// Start launches the connect/auth/reconnect loop. Idempotent.
func (c *Client) Start() {
	go c.run()
}

// Stop tears the client down: every in-flight call fails with ErrClosed and
// the loop exits without reconnecting. Idempotent.
func (c *Client) Stop() {
	c.stopOnce.Do(func() {
		close(c.stopCh)
		c.mu.Lock()
		c.closed = true
		if c.conn != nil {
			c.conn.Close()
		}
		c.mu.Unlock()
	})
}

// Connected reports whether the client currently holds an authenticated
// connection.
func (c *Client) Connected() bool {
	c.mu.Lock()
	defer c.mu.Unlock()
	return !c.closed && c.authed && c.conn != nil
}

// nextSeq reserves the next wire sequence under the state lock.
func (c *Client) nextSeq() uint64 {
	c.mu.Lock()
	defer c.mu.Unlock()
	c.seq++
	return c.seq
}

func (c *Client) run() {
	for {
		wasAuthed := false
		err := c.attempt(&wasAuthed)
		if wasAuthed {
			c.handlerMu.RLock()
			h := c.onDisconnect
			c.handlerMu.RUnlock()
			if h != nil {
				h(err)
			}
		} else if err != nil {
			c.log.Warn("chirp server-gateway attempt failed", "err", err, "service", c.cfg.ServiceID)
		}
		select {
		case <-c.stopCh:
			return
		case <-time.After(c.cfg.ReconnectDelay):
		}
	}
}

// attempt dials, runs the auth handshake, pumps heartbeats until the
// connection drops, and reports why it ended. *wasAuthed is set when the
// hub accepted the credentials at least once on this attempt.
func (c *Client) attempt(wasAuthed *bool) error {
	conn, err := net.DialTimeout("tcp", c.address(), c.cfg.DialTimeout)
	if err != nil {
		return err
	}

	authCh := make(chan error, 1)
	c.mu.Lock()
	c.conn = conn
	c.authed = false
	c.authCh = authCh
	c.mu.Unlock()

	done := make(chan struct{})
	go c.readLoop(conn, done)

	authReq := &pbsg.ServerAuthRequest{
		ServiceId:       c.cfg.ServiceID,
		Secret:          c.cfg.Secret,
		ProtocolVersion: 1,
	}
	err = c.writePacket(conn, &pbgw.Packet{
		MsgId:    pbgw.MsgID_SERVER_AUTH_REQ,
		Sequence: 0, // the C++ peer sends auth untracked, sequence 0
		Body:     mustMarshal(authReq),
	})
	if err == nil {
		select {
		case err = <-authCh:
		case <-done:
			err = ErrConnectionLost
		case <-c.stopCh:
			err = ErrClosed
		}
	}

	if err != nil {
		conn.Close()
		<-done
		c.clearConn(nil)
		return err
	}

	*wasAuthed = true
	c.mu.Lock()
	c.authed = true
	c.mu.Unlock()
	c.log.Info("connected to chirp server-gateway",
		"service", c.cfg.ServiceID, "heartbeat", c.cfg.HeartbeatInterval.String())

	hbStop := make(chan struct{})
	go c.heartbeatLoop(conn, hbStop, done)

	<-done
	close(hbStop)
	c.clearConn(ErrConnectionLost)
	return ErrConnectionLost
}

// heartbeatLoop sends SERVER_HEARTBEAT_PING at the server-assigned cadence.
// The hub closes silent connections after two intervals, so a ping that
// cannot be written is the client's early-warning: close the conn and let
// the read loop funnel the drop.
func (c *Client) heartbeatLoop(conn net.Conn, stop, connDone chan struct{}) {
	ticker := time.NewTicker(c.cfg.HeartbeatInterval)
	defer ticker.Stop()
	for {
		select {
		case <-stop:
			return
		case <-connDone:
			return
		case <-c.stopCh:
			return
		case <-ticker.C:
			ping := &pbsg.ServerHeartbeatPing{ClientTimeMs: nowMs()}
			if err := c.writePacket(conn, &pbgw.Packet{
				MsgId:    pbgw.MsgID_SERVER_HEARTBEAT_PING,
				Sequence: int64(c.nextSeq()),
				Body:     mustMarshal(ping),
			}); err != nil {
				conn.Close()
				return
			}
		}
	}
}

func (c *Client) readLoop(conn net.Conn, done chan struct{}) {
	defer close(done)
	for {
		pkt, err := readPacket(conn, c.cfg.MaxFrameBytes)
		if err != nil {
			return
		}
		if !c.dispatch(pkt) {
			return
		}
	}
}

// dispatch routes one frame and reports whether the read loop should keep
// going (an auth rejection ends the attempt).
func (c *Client) dispatch(pkt *pbgw.Packet) bool {
	switch pkt.GetMsgId() {
	case pbgw.MsgID_SERVER_AUTH_RESP:
		c.mu.Lock()
		ch := c.authCh
		c.authCh = nil
		c.mu.Unlock()
		if ch == nil {
			c.log.Warn("chirp server-gateway sent an unexpected auth response")
			return true
		}
		resp := &pbsg.ServerAuthResponse{}
		if err := proto.Unmarshal(pkt.GetBody(), resp); err != nil {
			ch <- err
			return false
		}
		if resp.GetCode() != pbcommon.ErrorCode_OK {
			ch <- &AuthError{Code: resp.GetCode()}
			return false
		}
		if secs := resp.GetHeartbeatIntervalSeconds(); secs > 0 {
			c.cfg.HeartbeatInterval = time.Duration(secs) * time.Second
		}
		ch <- nil
	case pbgw.MsgID_INJECT_MESSAGE_RESP,
		pbgw.MsgID_EVENT_PUBLISH_RESP,
		pbgw.MsgID_EVENT_ACK_RESP:
		c.completePending(pkt)
	case pbgw.MsgID_INJECT_MESSAGE_NOTIFY:
		notify := &pbsg.InjectMessageNotify{}
		if err := proto.Unmarshal(pkt.GetBody(), notify); err != nil {
			c.log.Warn("chirp: failed to parse InjectMessageNotify", "err", err)
			return true
		}
		c.handlerMu.RLock()
		h := c.onInject
		c.handlerMu.RUnlock()
		if h != nil {
			h(notify)
		}
	case pbgw.MsgID_EVENT_DELIVER_NOTIFY:
		notify := &pbsg.EventDeliverNotify{}
		if err := proto.Unmarshal(pkt.GetBody(), notify); err != nil {
			c.log.Warn("chirp: failed to parse EventDeliverNotify", "err", err)
			return true
		}
		c.handlerMu.RLock()
		h := c.onEvent
		c.handlerMu.RUnlock()
		if h == nil {
			// Without this warn a misrouted event would vanish without a
			// trace (and redeliver forever, since nobody acks it).
			c.log.Warn("chirp: event delivered but no handler registered",
				"type", notify.GetEventType(), "id", notify.GetEventId())
			return true
		}
		h(notify)
	case pbgw.MsgID_SERVER_HEARTBEAT_PONG:
		// Liveness is enforced by the hub, as on the C++ peer: nothing to do.
	default:
		// Unknown frames are ignored, as on the C++ peer.
	}
	return true
}

func (c *Client) completePending(pkt *pbgw.Packet) {
	c.mu.Lock()
	call, ok := c.pending[uint64(pkt.GetSequence())]
	if ok && call.respID != pkt.GetMsgId() {
		// Nobody can be waiting on a (sequence, msg id) pair that does not
		// match: a duplicate or a misrouted response. Drop it, like the
		// C++ peer does.
		c.log.Warn("chirp: response does not match pending rpc",
			"seq", pkt.GetSequence(), "msg_id", pkt.GetMsgId())
		ok = false
	}
	delete(c.pending, uint64(pkt.GetSequence()))
	c.mu.Unlock()
	if !ok {
		return
	}
	if err := proto.Unmarshal(pkt.GetBody(), call.resp); err != nil {
		call.ch <- err
	} else {
		call.ch <- call.validate()
	}
}

// clearConn drops the connection-scoped state. dropErr fails every pending
// call; nil leaves them (no call can be pending before auth completes).
func (c *Client) clearConn(dropErr error) {
	c.mu.Lock()
	if c.conn != nil {
		c.conn.Close()
		c.conn = nil
	}
	c.authed = false
	c.authCh = nil
	stale := c.pending
	c.pending = make(map[uint64]*pendingCall)
	c.mu.Unlock()
	for _, call := range stale {
		call.ch <- dropErr
	}
}

func (c *Client) rpc(ctx context.Context, reqID, respID pbgw.MsgID,
	req proto.Message, resp proto.Message, validate func() error) error {
	body, err := proto.Marshal(req)
	if err != nil {
		return err
	}

	c.mu.Lock()
	if c.closed {
		c.mu.Unlock()
		return ErrClosed
	}
	if c.conn == nil || !c.authed {
		c.mu.Unlock()
		return ErrNotConnected
	}
	conn := c.conn
	c.seq++
	seq := c.seq
	call := &pendingCall{respID: respID, resp: resp, validate: validate, ch: make(chan error, 1)}
	c.pending[seq] = call
	c.mu.Unlock()

	err = c.writePacket(conn, &pbgw.Packet{MsgId: reqID, Sequence: int64(seq), Body: body})
	if err != nil {
		c.mu.Lock()
		delete(c.pending, seq)
		c.mu.Unlock()
		return err
	}

	select {
	case err := <-call.ch:
		return err
	case <-ctx.Done():
		c.mu.Lock()
		delete(c.pending, seq)
		c.mu.Unlock()
		return ctx.Err()
	case <-c.stopCh:
		// Stop already failed every pending call; this one may have raced
		// the sweep, so drain without blocking on its buffered result.
		select {
		case err := <-call.ch:
			return err
		default:
			return ErrClosed
		}
	}
}

// InjectMessage injects a message whose sender is not a user account
// (system announcement, NPC line, game-service notification) into the chat
// plane. inject_id is the caller's idempotency key.
func (c *Client) InjectMessage(ctx context.Context, req *pbsg.MessageInjectRequest) (*pbsg.MessageInjectResponse, error) {
	resp := &pbsg.MessageInjectResponse{}
	err := c.rpc(ctx, pbgw.MsgID_INJECT_MESSAGE_REQ, pbgw.MsgID_INJECT_MESSAGE_RESP, req, resp,
		func() error { return serverErr(resp.GetCode()) })
	if err != nil {
		return nil, err
	}
	return resp, nil
}

// PublishEvent publishes an event that must reach target_service_id
// reliably: the hub queues it while the target is offline and redelivers
// until acknowledged. event_id is an optional caller idempotency key.
func (c *Client) PublishEvent(ctx context.Context, req *pbsg.EventPublishRequest) (*pbsg.EventPublishResponse, error) {
	resp := &pbsg.EventPublishResponse{}
	err := c.rpc(ctx, pbgw.MsgID_EVENT_PUBLISH_REQ, pbgw.MsgID_EVENT_PUBLISH_RESP, req, resp,
		func() error { return serverErr(resp.GetCode()) })
	if err != nil {
		return nil, err
	}
	return resp, nil
}

// AckEvents acknowledges processed EventDeliverNotify deliveries by id;
// unacked events are redelivered on reconnect (at-least-once).
func (c *Client) AckEvents(ctx context.Context, eventIDs ...string) (*pbsg.EventAckResponse, error) {
	req := &pbsg.EventAckRequest{EventIds: eventIDs}
	resp := &pbsg.EventAckResponse{}
	err := c.rpc(ctx, pbgw.MsgID_EVENT_ACK_REQ, pbgw.MsgID_EVENT_ACK_RESP, req, resp,
		func() error { return serverErr(resp.GetCode()) })
	if err != nil {
		return nil, err
	}
	return resp, nil
}

func (c *Client) address() string {
	return net.JoinHostPort(c.cfg.Host, fmt.Sprintf("%d", c.cfg.Port))
}

func (c *Client) writePacket(conn net.Conn, pkt *pbgw.Packet) error {
	body, err := proto.Marshal(pkt)
	if err != nil {
		return err
	}
	if len(body) == 0 || uint32(len(body)) > uint32(c.cfg.MaxFrameBytes) {
		return ErrBadFrame
	}
	frame := make([]byte, 4+len(body))
	binary.BigEndian.PutUint32(frame, uint32(len(body)))
	copy(frame[4:], body)

	c.writeMu.Lock()
	defer c.writeMu.Unlock()
	if err := conn.SetWriteDeadline(time.Now().Add(frameWriteTimeout)); err != nil {
		return err
	}
	_, err = conn.Write(frame)
	return err
}

func readPacket(r io.Reader, maxFrameBytes int) (*pbgw.Packet, error) {
	var hdr [4]byte
	if _, err := io.ReadFull(r, hdr[:]); err != nil {
		return nil, err
	}
	n := binary.BigEndian.Uint32(hdr[:])
	if n == 0 || n > uint32(maxFrameBytes) {
		return nil, ErrBadFrame
	}
	buf := make([]byte, n)
	if _, err := io.ReadFull(r, buf); err != nil {
		return nil, err
	}
	pkt := &pbgw.Packet{}
	if err := proto.Unmarshal(buf, pkt); err != nil {
		return nil, err
	}
	return pkt, nil
}

func serverErr(code pbcommon.ErrorCode) error {
	if code == pbcommon.ErrorCode_OK {
		return nil
	}
	return &ServerError{Code: code}
}

func mustMarshal(m proto.Message) []byte {
	body, err := proto.Marshal(m)
	if err != nil {
		// The server-plane messages have no marshal failure modes (no
		// required fields in proto3, no extensions); reaching this means a
		// programming error.
		panic(fmt.Sprintf("chirp: marshal %T: %v", m, err))
	}
	return body
}

func nowMs() int64 {
	return time.Now().UnixMilli()
}
