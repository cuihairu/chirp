package chirp

import (
	"context"
	"encoding/binary"
	"errors"
	"io"
	"net"
	"sync"
	"testing"
	"time"

	"google.golang.org/protobuf/proto"

	pbcommon "github.com/cui/chirp/proto/go/common"
	pbgw "github.com/cui/chirp/proto/go/gateway"
	pbsg "github.com/cui/chirp/proto/go/server_gateway"
)

// syncConn serializes frame writes from multiple handler goroutines.
type syncConn struct {
	mu   sync.Mutex
	conn net.Conn
}

func (s *syncConn) write(pkt *pbgw.Packet) error {
	body, err := proto.Marshal(pkt)
	if err != nil {
		return err
	}
	frame := make([]byte, 4+len(body))
	binary.BigEndian.PutUint32(frame, uint32(len(body)))
	copy(frame[4:], body)
	s.mu.Lock()
	defer s.mu.Unlock()
	_, err = s.conn.Write(frame)
	return err
}

// fakeHub accepts loopback connections and hands every inbound frame to
// handler on a per-connection goroutine. Handlers that answer requests out
// of order spawn their own goroutines and write through the *syncConn.
type fakeHub struct {
	t       *testing.T
	ln      net.Listener
	conns   chan *syncConn
	closers sync.WaitGroup
}

func startFakeHub(t *testing.T, handler func(sc *syncConn, pkt *pbgw.Packet)) *fakeHub {
	t.Helper()
	ln, err := net.Listen("tcp", "127.0.0.1:0")
	if err != nil {
		t.Fatalf("fake hub listen: %v", err)
	}
	h := &fakeHub{t: t, ln: ln, conns: make(chan *syncConn, 8)}
	h.closers.Add(1)
	go func() {
		defer h.closers.Done()
		for {
			conn, err := ln.Accept()
			if err != nil {
				return
			}
			sc := &syncConn{conn: conn}
			select {
			case h.conns <- sc:
			default:
			}
			h.closers.Add(1)
			go func() {
				defer h.closers.Done()
				for {
					pkt, err := readFrame(conn)
					if err != nil {
						return
					}
					handler(sc, pkt)
				}
			}()
		}
	}()
	t.Cleanup(func() {
		ln.Close()
		h.closers.Wait()
	})
	return h
}

func readFrame(conn net.Conn) (*pbgw.Packet, error) {
	var hdr [4]byte
	if _, err := io.ReadFull(conn, hdr[:]); err != nil {
		return nil, err
	}
	n := binary.BigEndian.Uint32(hdr[:])
	if n == 0 || n > 4*1024*1024 {
		return nil, ErrBadFrame
	}
	buf := make([]byte, n)
	if _, err := io.ReadFull(conn, buf); err != nil {
		return nil, err
	}
	pkt := &pbgw.Packet{}
	if err := proto.Unmarshal(buf, pkt); err != nil {
		return nil, err
	}
	return pkt, nil
}

func testConfig(h *fakeHub) Config {
	return Config{
		Host:              "127.0.0.1",
		Port:              h.ln.Addr().(*net.TCPAddr).Port,
		ServiceID:         "game-backend",
		Secret:            "sekrit",
		ReconnectDelay:    20 * time.Millisecond,
		HeartbeatInterval: 30 * time.Millisecond,
		DialTimeout:       2 * time.Second,
	}
}

// authOK answers every auth attempt with OK (and no server-assigned
// interval, exercising the client's fallback cadence).
func authOK(sc *syncConn, pkt *pbgw.Packet) {
	if pkt.GetMsgId() != pbgw.MsgID_SERVER_AUTH_REQ {
		return
	}
	sc.write(&pbgw.Packet{
		MsgId: pbgw.MsgID_SERVER_AUTH_RESP,
		Body: mustMarshal(&pbsg.ServerAuthResponse{
			Code:         pbcommon.ErrorCode_OK,
			ServerTimeMs: nowMs(),
		}),
	})
}

func waitFor(t *testing.T, timeout time.Duration, what string, cond func() bool) {
	t.Helper()
	deadline := time.Now().Add(timeout)
	for time.Now().Before(deadline) {
		if cond() {
			return
		}
		time.Sleep(5 * time.Millisecond)
	}
	t.Fatalf("timed out waiting for %s", what)
}

func TestAuthHandshakeAndHeartbeatPings(t *testing.T) {
	pingSeqs := make(chan int64, 64)
	h := startFakeHub(t, func(sc *syncConn, pkt *pbgw.Packet) {
		switch pkt.GetMsgId() {
		case pbgw.MsgID_SERVER_AUTH_REQ:
			req := &pbsg.ServerAuthRequest{}
			if err := proto.Unmarshal(pkt.GetBody(), req); err != nil {
				t.Errorf("bad auth body: %v", err)
				return
			}
			if req.GetServiceId() != "game-backend" || req.GetSecret() != "sekrit" {
				t.Errorf("unexpected credentials: %+v", req)
			}
			if pkt.GetSequence() != 0 {
				t.Errorf("auth sequence = %d, want 0", pkt.GetSequence())
			}
			authOK(sc, pkt)
		case pbgw.MsgID_SERVER_HEARTBEAT_PING:
			ping := &pbsg.ServerHeartbeatPing{}
			if err := proto.Unmarshal(pkt.GetBody(), ping); err != nil {
				t.Errorf("bad ping body: %v", err)
				return
			}
			if ping.GetClientTimeMs() <= 0 {
				t.Errorf("ping client_time_ms = %d, want > 0", ping.GetClientTimeMs())
			}
			if pkt.GetSequence() == 0 {
				t.Errorf("ping sequence must be non-zero")
			}
			pingSeqs <- pkt.GetSequence()
			sc.write(&pbgw.Packet{MsgId: pbgw.MsgID_SERVER_HEARTBEAT_PONG, Body: mustMarshal(
				&pbsg.ServerHeartbeatPong{ServerTimeMs: nowMs()})})
		}
	})

	c := NewClient(testConfig(h))
	c.Start()
	defer c.Stop()

	for i := 0; i < 3; i++ {
		select {
		case <-pingSeqs:
		case <-time.After(3 * time.Second):
			t.Fatalf("only %d heartbeats observed", i)
		}
	}
	if !c.Connected() {
		t.Fatal("client should be connected after auth")
	}
}

func TestAuthRejectedThenRecovers(t *testing.T) {
	attempts := 0
	var mu sync.Mutex
	h := startFakeHub(t, func(sc *syncConn, pkt *pbgw.Packet) {
		if pkt.GetMsgId() != pbgw.MsgID_SERVER_AUTH_REQ {
			return
		}
		mu.Lock()
		attempts++
		n := attempts
		mu.Unlock()
		code := pbcommon.ErrorCode_OK
		if n == 1 {
			code = pbcommon.ErrorCode_AUTH_FAILED
		}
		sc.write(&pbgw.Packet{
			MsgId: pbgw.MsgID_SERVER_AUTH_RESP,
			Body:  mustMarshal(&pbsg.ServerAuthResponse{Code: code}),
		})
	})

	c := NewClient(testConfig(h))
	c.Start()
	defer c.Stop()

	waitFor(t, 3*time.Second, "recovery after auth rejection", c.Connected)
	mu.Lock()
	defer mu.Unlock()
	if attempts < 2 {
		t.Fatalf("hub saw %d auth attempts, want >= 2", attempts)
	}
}

func TestInjectMessageCorrelatesBySequence(t *testing.T) {
	h := startFakeHub(t, func(sc *syncConn, pkt *pbgw.Packet) {
		switch pkt.GetMsgId() {
		case pbgw.MsgID_SERVER_AUTH_REQ:
			authOK(sc, pkt)
		case pbgw.MsgID_INJECT_MESSAGE_REQ:
			req := &pbsg.MessageInjectRequest{}
			if err := proto.Unmarshal(pkt.GetBody(), req); err != nil {
				t.Errorf("bad inject body: %v", err)
				return
			}
			injectID := req.GetInjectId()
			code := pbcommon.ErrorCode_OK
			if injectID == "dup" {
				code = pbcommon.ErrorCode_INVALID_PARAM
			}
			go func() {
				if injectID == "a" {
					// Answer the first request late, so the response for
					// "b" overtakes it: only sequence correlation keeps
					// the callers straight.
					time.Sleep(150 * time.Millisecond)
				}
				sc.write(&pbgw.Packet{
					MsgId:    pbgw.MsgID_INJECT_MESSAGE_RESP,
					Sequence: pkt.GetSequence(),
					Body: mustMarshal(&pbsg.MessageInjectResponse{
						Code: code, InjectId: injectID,
					}),
				})
			}()
		}
	})

	c := NewClient(testConfig(h))
	c.Start()
	defer c.Stop()
	waitFor(t, 3*time.Second, "initial connect", c.Connected)

	ctx := context.Background()
	type result struct {
		id  string
		err error
	}
	results := make(chan result, 3)
	do := func(id string) {
		resp, err := c.InjectMessage(ctx, &pbsg.MessageInjectRequest{
			InjectId: id, SenderKind: pbsg.SenderKind_SENDER_NPC,
			SenderId: "npc:blacksmith_01", ReceiverId: "player-7",
			Content: []byte("welcome"),
		})
		got := result{id: id, err: err}
		if resp != nil {
			if resp.GetInjectId() != id {
				t.Errorf("response inject_id = %q, want %q (correlation broken)",
					resp.GetInjectId(), id)
			}
		}
		results <- got
	}
	go do("a")
	go do("b")
	for i := 0; i < 2; i++ {
		r := <-results
		if r.err != nil {
			t.Errorf("inject %q: %v", r.id, r.err)
		}
	}

	// A non-OK code surfaces as *ServerError, not as a transport error.
	_, err := c.InjectMessage(ctx, &pbsg.MessageInjectRequest{InjectId: "dup"})
	var serr *ServerError
	if !errors.As(err, &serr) || serr.Code != pbcommon.ErrorCode_INVALID_PARAM {
		t.Fatalf("dup inject err = %v, want ServerError(INVALID_PARAM)", err)
	}
}

func TestEventDeliverNotifyAndAck(t *testing.T) {
	acked := make(chan []string, 1)
	var mu sync.Mutex
	var notified *pbsg.EventDeliverNotify
	h := startFakeHub(t, func(sc *syncConn, pkt *pbgw.Packet) {
		switch pkt.GetMsgId() {
		case pbgw.MsgID_SERVER_AUTH_REQ:
			authOK(sc, pkt)
			// The hub pushes a queued event right after auth.
			sc.write(&pbgw.Packet{
				MsgId: pbgw.MsgID_EVENT_DELIVER_NOTIFY,
				Body: mustMarshal(&pbsg.EventDeliverNotify{
					EventId: "evt-1", EventType: "quest.trigger",
					Payload: []byte(`{"quest":42}`), Attempt: 1,
				}),
			})
		case pbgw.MsgID_EVENT_ACK_REQ:
			req := &pbsg.EventAckRequest{}
			if err := proto.Unmarshal(pkt.GetBody(), req); err != nil {
				t.Errorf("bad ack body: %v", err)
				return
			}
			acked <- req.GetEventIds()
			sc.write(&pbgw.Packet{
				MsgId:    pbgw.MsgID_EVENT_ACK_RESP,
				Sequence: pkt.GetSequence(),
				Body:     mustMarshal(&pbsg.EventAckResponse{Code: pbcommon.ErrorCode_OK}),
			})
		}
	})

	c := NewClient(testConfig(h))
	c.SetEventHandler(func(n *pbsg.EventDeliverNotify) {
		mu.Lock()
		notified = n
		mu.Unlock()
	})
	c.Start()
	defer c.Stop()

	waitFor(t, 3*time.Second, "event delivery", func() bool {
		mu.Lock()
		defer mu.Unlock()
		return notified != nil
	})
	mu.Lock()
	n := notified
	mu.Unlock()
	if n.GetEventId() != "evt-1" || n.GetAttempt() != 1 || string(n.GetPayload()) != `{"quest":42}` {
		t.Errorf("delivered event = %+v", n)
	}

	resp, err := c.AckEvents(context.Background(), "evt-1")
	if err != nil {
		t.Fatalf("AckEvents: %v", err)
	}
	if resp.GetCode() != pbcommon.ErrorCode_OK {
		t.Fatalf("ack code = %d", resp.GetCode())
	}
	select {
	case ids := <-acked:
		if len(ids) != 1 || ids[0] != "evt-1" {
			t.Fatalf("hub saw ack ids %v", ids)
		}
	case <-time.After(3 * time.Second):
		t.Fatal("hub never received EVENT_ACK_REQ")
	}
}

func TestPendingCallsFailWhenConnectionDrops(t *testing.T) {
	var mu sync.Mutex
	conns := 0
	h := startFakeHub(t, func(sc *syncConn, pkt *pbgw.Packet) {
		switch pkt.GetMsgId() {
		case pbgw.MsgID_SERVER_AUTH_REQ:
			authOK(sc, pkt)
			mu.Lock()
			conns++
			mu.Unlock()
		case pbgw.MsgID_INJECT_MESSAGE_REQ:
			// Drop the connection without answering: the in-flight call
			// must fail, and the client must come back on its own.
			sc.conn.Close()
		}
	})

	c := NewClient(testConfig(h))
	c.Start()
	defer c.Stop()
	waitFor(t, 3*time.Second, "initial connect", c.Connected)

	_, err := c.InjectMessage(context.Background(), &pbsg.MessageInjectRequest{InjectId: "lost"})
	if !errors.Is(err, ErrConnectionLost) {
		t.Fatalf("in-flight inject err = %v, want ErrConnectionLost", err)
	}
	waitFor(t, 3*time.Second, "reconnect after drop", c.Connected)
	mu.Lock()
	defer mu.Unlock()
	if conns < 2 {
		t.Fatalf("hub saw %d connections, want >= 2", conns)
	}
}

func TestInjectNotifyDispatchedToHandler(t *testing.T) {
	received := make(chan *pbsg.InjectMessageNotify, 1)
	h := startFakeHub(t, func(sc *syncConn, pkt *pbgw.Packet) {
		if pkt.GetMsgId() != pbgw.MsgID_SERVER_AUTH_REQ {
			return
		}
		authOK(sc, pkt)
		sc.write(&pbgw.Packet{
			MsgId: pbgw.MsgID_INJECT_MESSAGE_NOTIFY,
			Body: mustMarshal(&pbsg.InjectMessageNotify{
				Message: &pbsg.MessageInjectRequest{
					InjectId: "inj-9", SenderKind: pbsg.SenderKind_SENDER_SYSTEM,
					SenderId: "system", ReceiverId: "player-7",
					Content: []byte("maintenance in 10 minutes"),
				},
			}),
		})
	})

	c := NewClient(testConfig(h))
	c.SetInjectHandler(func(n *pbsg.InjectMessageNotify) { received <- n })
	c.Start()
	defer c.Stop()

	select {
	case n := <-received:
		m := n.GetMessage()
		if m.GetInjectId() != "inj-9" || m.GetSenderKind() != pbsg.SenderKind_SENDER_SYSTEM ||
			m.GetSenderId() != "system" || m.GetReceiverId() != "player-7" ||
			string(m.GetContent()) != "maintenance in 10 minutes" {
			t.Errorf("notify mismatch: %+v", m)
		}
	case <-time.After(3 * time.Second):
		t.Fatal("inject notify never dispatched")
	}
}

func TestOversizedFrameDropsConnection(t *testing.T) {
	var mu sync.Mutex
	conns := 0
	h := startFakeHub(t, func(sc *syncConn, pkt *pbgw.Packet) {
		if pkt.GetMsgId() != pbgw.MsgID_SERVER_AUTH_REQ {
			return
		}
		authOK(sc, pkt)
		mu.Lock()
		conns++
		n := conns
		mu.Unlock()
		if n == 1 {
			// Framing violation: a header claiming ~4GB with no payload.
			sc.mu.Lock()
			sc.conn.Write([]byte{0xFF, 0xFF, 0xFF, 0xFF})
			sc.mu.Unlock()
		}
	})

	c := NewClient(testConfig(h))
	c.Start()
	defer c.Stop()
	waitFor(t, 3*time.Second, "reconnect after bad frame", c.Connected)
	mu.Lock()
	defer mu.Unlock()
	if conns < 2 {
		t.Fatalf("hub saw %d connections, want >= 2", conns)
	}
}

func TestLifecycleFailFastAndStop(t *testing.T) {
	h := startFakeHub(t, func(sc *syncConn, pkt *pbgw.Packet) {
		if pkt.GetMsgId() == pbgw.MsgID_SERVER_AUTH_REQ {
			authOK(sc, pkt)
		}
	})

	c := NewClient(testConfig(h))
	// Before Start: not connected yet.
	if _, err := c.InjectMessage(context.Background(), &pbsg.MessageInjectRequest{}); !errors.Is(err, ErrNotConnected) {
		t.Fatalf("pre-start inject err = %v, want ErrNotConnected", err)
	}

	c.Start()
	waitFor(t, 3*time.Second, "connect", c.Connected)

	c.Stop()
	c.Stop() // idempotent
	if c.Connected() {
		t.Fatal("Connected() must be false after Stop")
	}
	if _, err := c.InjectMessage(context.Background(), &pbsg.MessageInjectRequest{}); !errors.Is(err, ErrClosed) {
		t.Fatalf("post-stop inject err = %v, want ErrClosed", err)
	}
	if _, err := c.AckEvents(context.Background(), "x"); !errors.Is(err, ErrClosed) {
		t.Fatalf("post-stop ack err = %v, want ErrClosed", err)
	}
}

func TestRPCTimeoutViaContext(t *testing.T) {
	h := startFakeHub(t, func(sc *syncConn, pkt *pbgw.Packet) {
		if pkt.GetMsgId() != pbgw.MsgID_SERVER_AUTH_REQ {
			return
		}
		authOK(sc, pkt)
		// Never answers the RPC: only the caller's ctx can end the wait.
	})

	c := NewClient(testConfig(h))
	c.Start()
	defer c.Stop()
	waitFor(t, 3*time.Second, "connect", c.Connected)

	ctx, cancel := context.WithTimeout(context.Background(), 80*time.Millisecond)
	defer cancel()
	_, err := c.InjectMessage(ctx, &pbsg.MessageInjectRequest{InjectId: "slow"})
	if !errors.Is(err, context.DeadlineExceeded) {
		t.Fatalf("unanswered rpc err = %v, want context.DeadlineExceeded", err)
	}
	// The connection stays usable: a subsequent answered call works (the
	// hub here never answers, so just assert the client is still up).
	if !c.Connected() {
		t.Fatal("ctx timeout must not tear down the connection")
	}
}
