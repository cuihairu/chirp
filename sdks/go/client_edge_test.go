// Edge-arm coverage for the client: the frame codec, config defaults, error
// types, and the dispatch/completePending/heartbeatLoop/rpc arms that the
// round-trip tests against fakeHub never reach (marshal failures, mismatched
// responses, stop races, write-deadline failures). These call the internal
// seams directly where a full connect cycle cannot force the arm
// deterministically.
package chirp

import (
	"bytes"
	"context"
	"encoding/binary"
	"errors"
	"fmt"
	"net"
	"testing"
	"time"

	"google.golang.org/protobuf/proto"

	pbcommon "github.com/cui/chirp/proto/go/common"
	pbsg "github.com/cui/chirp/proto/go/game_server_gateway"
	pbgw "github.com/cui/chirp/proto/go/gateway"
)

func TestErrorStrings(t *testing.T) {
	auth := (&AuthError{Code: pbcommon.ErrorCode_AUTH_FAILED}).Error()
	if want := fmt.Sprintf("chirp: auth rejected (code=%d)", pbcommon.ErrorCode_AUTH_FAILED); auth != want {
		t.Errorf("AuthError.Error() = %q, want %q", auth, want)
	}
	srv := (&ServerError{Code: pbcommon.ErrorCode_INVALID_PARAM}).Error()
	wantSrv := fmt.Sprintf("chirp: server returned code=%d", pbcommon.ErrorCode_INVALID_PARAM)
	if srv != wantSrv {
		t.Errorf("ServerError.Error() = %q, want %q", srv, wantSrv)
	}
}

func TestConfigApplyDefaults(t *testing.T) {
	cfg := Config{}
	cfg.applyDefaults()
	if cfg.HeartbeatInterval != defaultHeartbeat || cfg.ReconnectDelay != defaultReconnect ||
		cfg.DialTimeout != defaultDial || cfg.MaxFrameBytes != defaultMaxFrame {
		t.Errorf("zero config defaults = %+v", cfg)
	}
	if cfg.Logger == nil {
		t.Error("nil Logger must default to slog.Default()")
	}

	// Explicit values survive (only <= 0 / nil are replaced).
	cfg = Config{HeartbeatInterval: time.Second, ReconnectDelay: 2 * time.Second,
		DialTimeout: 3 * time.Second, MaxFrameBytes: 512}
	cfg.applyDefaults()
	if cfg.HeartbeatInterval != time.Second || cfg.ReconnectDelay != 2*time.Second ||
		cfg.DialTimeout != 3*time.Second || cfg.MaxFrameBytes != 512 {
		t.Errorf("explicit config clobbered: %+v", cfg)
	}

	// NewClient applies the defaults before the first dial.
	c := NewClient(Config{})
	if c.cfg.MaxFrameBytes != defaultMaxFrame {
		t.Errorf("NewClient left MaxFrameBytes = %d", c.cfg.MaxFrameBytes)
	}
}

func TestReadPacketRejectsBadFrames(t *testing.T) {
	frame := func(body []byte) []byte {
		out := make([]byte, 4+len(body))
		binary.BigEndian.PutUint32(out, uint32(len(body)))
		copy(out[4:], body)
		return out
	}
	valid := frame(mustMarshal(&pbgw.Packet{MsgId: pbgw.MsgID_SERVER_HEARTBEAT_PONG}))

	if _, err := readPacket(bytes.NewReader(nil), defaultMaxFrame); err == nil {
		t.Error("EOF header must error")
	}
	if _, err := readPacket(bytes.NewReader([]byte{0, 0, 0, 0}), defaultMaxFrame); !errors.Is(err, ErrBadFrame) {
		t.Errorf("zero-length frame err = %v, want ErrBadFrame", err)
	}
	if _, err := readPacket(bytes.NewReader([]byte{0x7f, 0xff, 0xff, 0xff}), defaultMaxFrame); !errors.Is(err, ErrBadFrame) {
		t.Errorf("oversized header err = %v, want ErrBadFrame", err)
	}
	truncated := []byte{0, 0, 0, 4, 0x1, 0x2} // header claims 4, body has 2
	if _, err := readPacket(bytes.NewReader(truncated), defaultMaxFrame); err == nil {
		t.Error("truncated body must error")
	}
	// 0xFF is an invalid wire tag for every server-plane message.
	if _, err := readPacket(bytes.NewReader(frame([]byte{0xFF})), defaultMaxFrame); err == nil {
		t.Error("unparseable body must error")
	}
	pkt, err := readPacket(bytes.NewReader(valid), defaultMaxFrame)
	if err != nil || pkt.GetMsgId() != pbgw.MsgID_SERVER_HEARTBEAT_PONG {
		t.Errorf("valid frame: pkt=%v err=%v", pkt, err)
	}
}

// deadlineFailConn makes every write fail at SetWriteDeadline, isolating
// writePacket's deadline arm from the actual Write.
type deadlineFailConn struct{ net.Conn }

func (deadlineFailConn) SetWriteDeadline(time.Time) error {
	return errors.New("deadline unsupported")
}

func TestWritePacketRejectsBadBodiesAndDeadConns(t *testing.T) {
	c := NewClient(Config{MaxFrameBytes: 8})
	conn, peer := net.Pipe()
	defer conn.Close()

	// An all-default Packet marshals to zero bytes: the framing contract
	// forbids empty frames.
	if err := c.writePacket(conn, &pbgw.Packet{}); !errors.Is(err, ErrBadFrame) {
		t.Errorf("empty body err = %v, want ErrBadFrame", err)
	}
	// A body larger than MaxFrameBytes never hits the wire.
	big := &pbgw.Packet{Body: bytes.Repeat([]byte("x"), 16)}
	if err := c.writePacket(conn, big); !errors.Is(err, ErrBadFrame) {
		t.Errorf("oversized body err = %v, want ErrBadFrame", err)
	}
	if err := c.writePacket(deadlineFailConn{conn}, &pbgw.Packet{MsgId: 1, Body: []byte("y")}); err == nil {
		t.Error("SetWriteDeadline failure must surface")
	}
	// A closed peer fails the Write itself.
	peer.Close()
	if err := c.writePacket(conn, &pbgw.Packet{MsgId: 1, Body: []byte("z")}); err == nil {
		t.Error("write on closed peer must fail")
	}
}

func TestMustMarshalPanicsOnUnmarshalableMessage(t *testing.T) {
	defer func() {
		if recover() == nil {
			t.Error("mustMarshal must panic when proto.Marshal fails")
		}
	}()
	// proto.Marshal rejects invalid UTF-8 in string fields.
	mustMarshal(&pbsg.MessageInjectRequest{SenderId: "\xff"})
}

func TestRPCMarshalFailureSurfacesAsError(t *testing.T) {
	c := NewClient(Config{})
	// The request body cannot be marshaled; rpc must refuse the call before
	// touching connection state (a fresh client is not connected anyway, so
	// reaching ErrNotConnected here would mean the check order regressed).
	_, err := c.InjectMessage(context.Background(),
		&pbsg.MessageInjectRequest{SenderId: "\xff"})
	if err == nil || errors.Is(err, ErrNotConnected) {
		t.Errorf("unmarshalable request err = %v, want a marshal error", err)
	}
}

func TestDispatchEdgeFrames(t *testing.T) {
	c := NewClient(Config{})
	garbage := []byte{0xFF}

	// A second auth response with no waiter: logged and ignored, the read
	// loop keeps going.
	if !c.dispatch(&pbgw.Packet{MsgId: pbgw.MsgID_SERVER_AUTH_RESP, Body: garbage}) {
		t.Error("duplicate auth response must not end the read loop")
	}

	// A garbage auth body reports the unmarshal error to the auth waiter.
	ch := make(chan error, 1)
	c.mu.Lock()
	c.authCh = ch
	c.mu.Unlock()
	if c.dispatch(&pbgw.Packet{MsgId: pbgw.MsgID_SERVER_AUTH_RESP, Body: garbage}) {
		t.Error("garbage auth response must end the attempt")
	}
	select {
	case err := <-ch:
		if err == nil {
			t.Error("garbage auth body must deliver an error")
		}
	default:
		t.Fatal("auth waiter never notified")
	}

	// The server-assigned cadence overrides the configured fallback.
	ch2 := make(chan error, 1)
	c.mu.Lock()
	c.authCh = ch2
	c.mu.Unlock()
	resp := mustMarshal(&pbsg.ServerAuthResponse{
		Code: pbcommon.ErrorCode_OK, HeartbeatIntervalSeconds: 7,
	})
	if !c.dispatch(&pbgw.Packet{MsgId: pbgw.MsgID_SERVER_AUTH_RESP, Body: resp}) {
		t.Fatal("ok auth response must keep the read loop")
	}
	select {
	case err := <-ch2:
		if err != nil {
			t.Fatalf("ok auth delivered err %v", err)
		}
	default:
		t.Fatal("auth waiter never notified on ok")
	}
	if c.cfg.HeartbeatInterval != 7*time.Second {
		t.Errorf("server cadence ignored: %v", c.cfg.HeartbeatInterval)
	}

	// Garbage notify bodies are logged and skipped, never fatal.
	for _, id := range []pbgw.MsgID{
		pbgw.MsgID_INJECT_MESSAGE_NOTIFY,
		pbgw.MsgID_EVENT_DELIVER_NOTIFY,
		pbgw.MsgID_DEVICES_PRESENCE_NOTIFY,
	} {
		if !c.dispatch(&pbgw.Packet{MsgId: id, Body: garbage}) {
			t.Errorf("garbage %v must not end the read loop", id)
		}
	}

	// A delivered event with no handler registered stays non-fatal (the hub
	// would otherwise redeliver it forever — the warn is the only trace).
	evt := mustMarshal(&pbsg.EventDeliverNotify{EventId: "e-1", EventType: "t"})
	if !c.dispatch(&pbgw.Packet{MsgId: pbgw.MsgID_EVENT_DELIVER_NOTIFY, Body: evt}) {
		t.Error("unhandled event must not end the read loop")
	}

	// Liveness is hub-enforced: the client does nothing with PONG.
	if !c.dispatch(&pbgw.Packet{MsgId: pbgw.MsgID_SERVER_HEARTBEAT_PONG}) {
		t.Error("PONG must not end the read loop")
	}
	// Unknown ids are ignored, like the C++ peer.
	if !c.dispatch(&pbgw.Packet{MsgId: pbgw.MsgID(9999)}) {
		t.Error("unknown msg id must not end the read loop")
	}
}

func TestCompletePendingMismatchStrayAndGarbage(t *testing.T) {
	c := NewClient(Config{})

	// A response whose msg id does not match the pending call is dropped,
	// along with the stale entry.
	mismatched := &pendingCall{
		respID: pbgw.MsgID_INJECT_MESSAGE_RESP,
		resp:   &pbsg.MessageInjectResponse{},
		ch:     make(chan error, 1),
	}
	c.mu.Lock()
	c.pending[5] = mismatched
	c.mu.Unlock()
	c.completePending(&pbgw.Packet{Sequence: 5, MsgId: pbgw.MsgID_EVENT_PUBLISH_RESP})
	select {
	case <-mismatched.ch:
		t.Error("mismatched response must not complete the call")
	default:
	}
	c.mu.Lock()
	_, still := c.pending[5]
	c.mu.Unlock()
	if still {
		t.Error("mismatched response must drop the stale entry")
	}

	// A response with no pending call at all is a stray: ignored.
	c.completePending(&pbgw.Packet{Sequence: 9, MsgId: pbgw.MsgID_INJECT_MESSAGE_RESP})

	// A matching id with an unparseable body delivers the unmarshal error.
	broken := &pendingCall{
		respID: pbgw.MsgID_INJECT_MESSAGE_RESP,
		resp:   &pbsg.MessageInjectResponse{},
		ch:     make(chan error, 1),
	}
	c.mu.Lock()
	c.pending[7] = broken
	c.mu.Unlock()
	c.completePending(&pbgw.Packet{Sequence: 7, MsgId: pbgw.MsgID_INJECT_MESSAGE_RESP,
		Body: []byte{0xFF}})
	select {
	case err := <-broken.ch:
		if err == nil {
			t.Error("garbage response body must deliver an error")
		}
	default:
		t.Fatal("broken call never completed")
	}
}

func TestHeartbeatLoopExitArms(t *testing.T) {
	run := func(interval time.Duration, closeStopCh, closeStop, closePeer bool) bool {
		c := NewClient(Config{HeartbeatInterval: interval})
		conn, peer := net.Pipe()
		defer conn.Close()
		stop, connDone := make(chan struct{}), make(chan struct{})
		switch {
		case closeStopCh:
			close(c.stopCh)
		case closeStop:
			close(stop)
		}
		if closePeer {
			peer.Close()
		}
		finished := make(chan struct{})
		go func() {
			c.heartbeatLoop(conn, stop, connDone)
			close(finished)
		}()
		select {
		case <-finished:
			return true
		case <-time.After(2 * time.Second):
			return false
		}
	}
	if !run(time.Hour, false, true, false) {
		t.Error("heartbeatLoop must exit when the attempt ends (stop)")
	}
	if !run(time.Hour, true, false, false) {
		t.Error("heartbeatLoop must exit on client Stop")
	}
	if !run(5*time.Millisecond, false, false, true) {
		t.Error("heartbeatLoop must exit when the ping write fails")
	}
}

func TestAttemptDialFailureKeepsRetrying(t *testing.T) {
	// Port 1 on loopback has no listener: every dial is refused.
	c := NewClient(Config{Host: "127.0.0.1", Port: 1,
		ReconnectDelay: 5 * time.Millisecond, DialTimeout: 100 * time.Millisecond})
	c.Start()
	defer c.Stop()
	time.Sleep(80 * time.Millisecond)
	if c.Connected() {
		t.Fatal("client must not report connected while every dial fails")
	}
}

func TestAttemptConnDropDuringAuthWait(t *testing.T) {
	conns := make(chan struct{}, 16)
	h := startFakeHub(t, func(sc *syncConn, pkt *pbgw.Packet) {
		conns <- struct{}{}
		// Accept, take the auth frame, then drop without answering: the
		// auth waiter must fail the attempt and the client must come back.
		sc.conn.Close()
	})

	c := NewClient(testConfig(h))
	c.Start()
	defer c.Stop()
	waitFor(t, 3*time.Second, "reconnect after drop during auth", func() bool {
		return len(conns) >= 2
	})
}

func TestStopDuringAuthWaitEndsAttempt(t *testing.T) {
	accepted := make(chan struct{}, 1)
	h := startFakeHub(t, func(sc *syncConn, pkt *pbgw.Packet) {
		select {
		case accepted <- struct{}{}:
		default:
		}
		// Accept and stay silent forever.
	})

	c := NewClient(testConfig(h))
	c.Start()
	<-accepted
	// Close the stop channel directly (not Stop, which would also kill the
	// conn and open a race with the read loop's done): the auth waiter sees
	// only stopCh ready and the loop exits deterministically.
	close(c.stopCh)
	waitFor(t, 3*time.Second, "run loop exit after stop during auth", func() bool {
		c.mu.Lock()
		defer c.mu.Unlock()
		return c.conn == nil // clearConn ran: the attempt gave up cleanly
	})
	// Release the hub handler: close the conn the attempt left behind.
	c.mu.Lock()
	conn := c.conn
	c.mu.Unlock()
	if conn != nil {
		conn.Close()
	}
}

func TestDisconnectHandlerFiresOnDrop(t *testing.T) {
	dropped := make(chan error, 4)
	h := startFakeHub(t, func(sc *syncConn, pkt *pbgw.Packet) {
		if pkt.GetMsgId() != pbgw.MsgID_SERVER_AUTH_REQ {
			return
		}
		authOK(sc, pkt)
		sc.conn.Close() // drop right after auth: wasAuthed, handler must fire
	})

	c := NewClient(testConfig(h))
	c.SetDisconnectHandler(func(err error) { dropped <- err })
	c.Start()
	defer c.Stop()

	select {
	case err := <-dropped:
		if !errors.Is(err, ErrConnectionLost) {
			t.Fatalf("disconnect handler err = %v, want ErrConnectionLost", err)
		}
	case <-time.After(3 * time.Second):
		t.Fatal("disconnect handler never fired")
	}
}

func TestPublishUnbindResolveRoundTrip(t *testing.T) {
	h := startFakeHub(t, func(sc *syncConn, pkt *pbgw.Packet) {
		switch pkt.GetMsgId() {
		case pbgw.MsgID_SERVER_AUTH_REQ:
			authOK(sc, pkt)
		case pbgw.MsgID_EVENT_PUBLISH_REQ:
			req := &pbsg.EventPublishRequest{}
			if err := proto.Unmarshal(pkt.GetBody(), req); err != nil {
				t.Errorf("bad publish body: %v", err)
				return
			}
			sc.write(&pbgw.Packet{
				MsgId: pbgw.MsgID_EVENT_PUBLISH_RESP, Sequence: pkt.GetSequence(),
				Body: mustMarshal(&pbsg.EventPublishResponse{
					Code: pbcommon.ErrorCode_OK, EventId: req.GetEventId()}),
			})
		case pbgw.MsgID_UNBIND_PLAYER_IDENTITY_REQ:
			sc.write(&pbgw.Packet{
				MsgId: pbgw.MsgID_UNBIND_PLAYER_IDENTITY_RESP, Sequence: pkt.GetSequence(),
				Body: mustMarshal(&pbsg.UnbindPlayerIdentityResponse{Code: pbcommon.ErrorCode_OK}),
			})
		case pbgw.MsgID_RESOLVE_GAME_USER_REQ:
			req := &pbsg.ResolveGameUserRequest{}
			if err := proto.Unmarshal(pkt.GetBody(), req); err != nil {
				t.Errorf("bad resolve body: %v", err)
				return
			}
			sc.write(&pbgw.Packet{
				MsgId: pbgw.MsgID_RESOLVE_GAME_USER_RESP, Sequence: pkt.GetSequence(),
				Body: mustMarshal(&pbsg.ResolveGameUserResponse{
					Code: pbcommon.ErrorCode_OK, PlayerId: "player-1"}),
			})
		}
	})

	c := NewClient(testConfig(h))
	c.Start()
	defer c.Stop()
	waitFor(t, 3*time.Second, "connect", c.Connected)

	ctx, cancel := context.WithTimeout(context.Background(), 5*time.Second)
	defer cancel()
	pub, err := c.PublishEvent(ctx, &pbsg.EventPublishRequest{
		EventId: "evt-9", EventType: "loot.drop", Payload: []byte(`{}`),
		TargetServiceId: "trade",
	})
	if err != nil || pub.GetEventId() != "evt-9" {
		t.Fatalf("PublishEvent: resp=%v err=%v", pub, err)
	}
	if _, err := c.UnbindPlayerIdentity(ctx, &pbsg.UnbindPlayerIdentityRequest{
		GameId: "game-a", GameUserId: "u-1",
	}); err != nil {
		t.Fatalf("UnbindPlayerIdentity: %v", err)
	}
	resolved, err := c.ResolveGameUser(ctx, &pbsg.ResolveGameUserRequest{
		GameId: "game-a", GameUserId: "u-1",
	})
	if err != nil || resolved.GetPlayerId() != "player-1" {
		t.Fatalf("ResolveGameUser: resp=%v err=%v", resolved, err)
	}
}

// Every RPC wrapper must fail fast (and uniformly) before the connection is
// authenticated, exercising each wrapper's error arm.
func TestAllRPCWrappersFailFastBeforeConnect(t *testing.T) {
	c := NewClient(Config{})
	ctx := context.Background()
	wrap := func(name string, call func() error) {
		t.Helper()
		if err := call(); !errors.Is(err, ErrNotConnected) {
			t.Errorf("%s err = %v, want ErrNotConnected", name, err)
		}
	}
	wrap("InjectMessage", func() error {
		_, err := c.InjectMessage(ctx, &pbsg.MessageInjectRequest{})
		return err
	})
	wrap("PublishEvent", func() error {
		_, err := c.PublishEvent(ctx, &pbsg.EventPublishRequest{})
		return err
	})
	wrap("AckEvents", func() error {
		_, err := c.AckEvents(ctx, "e")
		return err
	})
	wrap("BindPlayerIdentity", func() error {
		_, err := c.BindPlayerIdentity(ctx, &pbsg.BindPlayerIdentityRequest{})
		return err
	})
	wrap("UnbindPlayerIdentity", func() error {
		_, err := c.UnbindPlayerIdentity(ctx, &pbsg.UnbindPlayerIdentityRequest{})
		return err
	})
	wrap("GetPlayerIdentities", func() error {
		_, err := c.GetPlayerIdentities(ctx, &pbsg.GetPlayerIdentitiesRequest{})
		return err
	})
	wrap("ResolveGameUser", func() error {
		_, err := c.ResolveGameUser(ctx, &pbsg.ResolveGameUserRequest{})
		return err
	})
	wrap("SubscribePlayerChannel", func() error {
		_, err := c.SubscribePlayerChannel(ctx, &pbsg.SubscribePlayerChannelRequest{})
		return err
	})
	wrap("UnsubscribePlayerChannel", func() error {
		_, err := c.UnsubscribePlayerChannel(ctx, &pbsg.UnsubscribePlayerChannelRequest{})
		return err
	})
	wrap("GetPlayerSubscriptions", func() error {
		_, err := c.GetPlayerSubscriptions(ctx, &pbsg.GetPlayerSubscriptionsRequest{})
		return err
	})
	wrap("MarkChannelsRead", func() error {
		_, err := c.MarkChannelsRead(ctx, &pbsg.MarkChannelsReadRequest{})
		return err
	})
	wrap("GetUnreadSummary", func() error {
		_, err := c.GetUnreadSummary(ctx, &pbsg.GetUnreadSummaryRequest{})
		return err
	})
	wrap("SetGamePresenceEnabled", func() error {
		_, err := c.SetGamePresenceEnabled(ctx, &pbsg.SetGamePresenceEnabledRequest{})
		return err
	})
	wrap("GetGamePresence", func() error {
		_, err := c.GetGamePresence(ctx, &pbsg.GetGamePresenceRequest{})
		return err
	})
}

// A write that fails after the call was registered must remove the pending
// entry and surface the transport error (not leave a stale waiter).
func TestRPCWriteFailureFailsFast(t *testing.T) {
	c := NewClient(Config{})
	conn, peer := net.Pipe()
	peer.Close() // every Write on conn fails from here on
	c.mu.Lock()
	c.conn = conn
	c.authed = true
	c.mu.Unlock()
	defer func() {
		c.mu.Lock()
		c.conn = nil
		c.mu.Unlock()
		conn.Close()
	}()

	_, err := c.InjectMessage(context.Background(), &pbsg.MessageInjectRequest{InjectId: "w"})
	if err == nil || errors.Is(err, ErrNotConnected) {
		t.Fatalf("dead-conn inject err = %v, want a write error", err)
	}
	c.mu.Lock()
	pending := len(c.pending)
	c.mu.Unlock()
	if pending != 0 {
		t.Fatalf("failed write left %d pending calls", pending)
	}
}

// Stop while an RPC is in flight (and the hub stays silent): the call must
// return ErrClosed promptly via the stop-drain path instead of hanging.
func TestRPCStopDrainsUnansweredCall(t *testing.T) {
	h := startFakeHub(t, func(sc *syncConn, pkt *pbgw.Packet) {
		if pkt.GetMsgId() == pbgw.MsgID_SERVER_AUTH_REQ {
			authOK(sc, pkt)
		}
		// Requests are read but never answered.
	})

	c := NewClient(testConfig(h))
	c.Start()
	defer c.Stop()
	waitFor(t, 3*time.Second, "connect", c.Connected)

	done := make(chan error, 1)
	go func() {
		_, err := c.InjectMessage(context.Background(), &pbsg.MessageInjectRequest{InjectId: "s"})
		done <- err
	}()
	// Wait until the call is registered, then stop: the drain select must
	// fall through to ErrClosed (no response can arrive).
	waitFor(t, 3*time.Second, "pending registration", func() bool {
		c.mu.Lock()
		defer c.mu.Unlock()
		return len(c.pending) == 1
	})
	c.Stop()
	select {
	case err := <-done:
		if err == nil {
			t.Fatal("stopped call must fail, got nil")
		}
	case <-time.After(3 * time.Second):
		t.Fatal("stop must fail in-flight calls promptly")
	}
}
