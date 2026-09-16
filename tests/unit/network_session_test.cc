#include <gtest/gtest.h>

#include <fcntl.h>
#include <sys/socket.h>
#include <unistd.h>

#include <atomic>
#include <chrono>
#include <cstring>
#include <memory>
#include <string>
#include <vector>

#include "network/protobuf_framing.h"
#include "network/tcp_client.h"
#include "network/tcp_session.h"
#include "network/websocket_client.h"
#include "network/websocket_frame.h"
#include "network/websocket_session.h"
#include "network/websocket_util.h"

using chirp::network::BuildWebSocketFrame;
using chirp::network::ProtobufFraming;
using chirp::network::Session;
using chirp::network::TcpClient;
using chirp::network::TcpSession;
using chirp::network::WebSocketClient;
using chirp::network::WebSocketFrameParser;
using chirp::network::WebSocketSession;

namespace {

// ---------------------------------------------------------------------------
// Harness: an in-process connected socket pair (no network stack involved).
// Side A is owned by the test; side B is moved into the session under test.
// The io_context is driven manually with Pump() on the test thread, so all
// session handlers run on this thread - no cross-thread races.
// ---------------------------------------------------------------------------

std::string FrameBytes(const std::string& payload) {
  std::string out;
  const uint32_t n = static_cast<uint32_t>(payload.size());
  out.push_back(static_cast<char>((n >> 24) & 0xFF));
  out.push_back(static_cast<char>((n >> 16) & 0xFF));
  out.push_back(static_cast<char>((n >> 8) & 0xFF));
  out.push_back(static_cast<char>(n & 0xFF));
  out += payload;
  return out;
}

class SocketPairPipe {
 public:
  asio::io_context io;
  std::unique_ptr<asio::ip::tcp::socket> test_side;

  SocketPairPipe() {
    int sv[2];
    if (::socketpair(AF_UNIX, SOCK_STREAM, 0, sv) != 0) {
      throw std::runtime_error("socketpair failed");
    }
    test_side = std::make_unique<asio::ip::tcp::socket>(io);
    session_fd = sv[1];
    test_side->assign(asio::ip::tcp::v4(), sv[0]);
    test_side->non_blocking(true);
  }

  ~SocketPairPipe() {
    if (session_fd >= 0) {
      ::close(session_fd);
    }
  }

  asio::ip::tcp::socket TakeSessionSocket() {
    asio::ip::tcp::socket s(io);
    s.assign(asio::ip::tcp::v4(), session_fd);
    session_fd = -1;  // ownership transferred
    return s;
  }

  void Pump(int rounds = 6) {
    for (int i = 0; i < rounds; ++i) {
      io.poll();
      io.restart();
      std::this_thread::sleep_for(std::chrono::milliseconds(1));
    }
  }

  bool WriteAll(const std::string& data, int timeout_ms = 2000) {
    size_t written = 0;
    auto deadline = std::chrono::steady_clock::now() +
                    std::chrono::milliseconds(timeout_ms);
    while (written < data.size()) {
      asio::error_code ec;
      size_t n = test_side->write_some(
          asio::buffer(data.data() + written, data.size() - written), ec);
      written += n;
      if (ec == asio::error::would_block) {
        if (std::chrono::steady_clock::now() > deadline) {
          return false;
        }
        std::this_thread::sleep_for(std::chrono::milliseconds(1));
      } else if (ec) {
        return false;
      }
    }
    return true;
  }

  std::string ReadExactly(size_t n, int timeout_ms = 2000) {
    std::string out(n, '\0');
    size_t got = 0;
    auto deadline = std::chrono::steady_clock::now() +
                    std::chrono::milliseconds(timeout_ms);
    while (got < n) {
      asio::error_code ec;
      size_t r = test_side->read_some(
          asio::buffer(out.data() + got, n - got), ec);
      got += r;
      if (ec == asio::error::would_block) {
        if (std::chrono::steady_clock::now() > deadline) {
          return out.substr(0, got);
        }
        std::this_thread::sleep_for(std::chrono::milliseconds(1));
      } else if (ec) {
        return out.substr(0, got);
      }
    }
    return out;
  }

  std::string ReadUntil(const std::string& delim, int timeout_ms = 2000) {
    std::string acc;
    auto deadline = std::chrono::steady_clock::now() +
                    std::chrono::milliseconds(timeout_ms);
    while (acc.find(delim) == std::string::npos) {
      char buf[512];
      asio::error_code ec;
      size_t r = test_side->read_some(asio::buffer(buf), ec);
      if (r > 0) {
        acc.append(buf, r);
      }
      if (ec == asio::error::would_block) {
        if (std::chrono::steady_clock::now() > deadline) {
          return acc;
        }
        std::this_thread::sleep_for(std::chrono::milliseconds(1));
      } else if (ec) {
        return acc;
      }
    }
    return acc;
  }

  // Returns true when the peer reached EOF (session closed its side).
  bool WaitEof(int timeout_ms = 2000) {
    auto deadline = std::chrono::steady_clock::now() +
                    std::chrono::milliseconds(timeout_ms);
    while (std::chrono::steady_clock::now() < deadline) {
      char buf[64];
      asio::error_code ec;
      size_t r = test_side->read_some(asio::buffer(buf), ec);
      if (ec == asio::error::eof) {
        return true;
      }
      if (ec == asio::error::would_block) {
        std::this_thread::sleep_for(std::chrono::milliseconds(2));
        Pump(2);
      } else if (ec) {
        return ec == asio::error::eof;
      } else if (r == 0) {
        return true;
      }
    }
    return false;
  }

 private:
  int session_fd = -1;
};

std::string WsPayload(const std::string& lp_framed_bytes) {
  return lp_framed_bytes;
}

// ---------------------------------------------------------------------------
// TcpSession
// ---------------------------------------------------------------------------

class TcpSessionTest : public ::testing::Test {
 protected:
  void MakeSession(
      TcpSession::FrameCallback on_frame = nullptr,
      TcpSession::CloseCallback on_close = nullptr) {
    session_ = std::make_shared<TcpSession>(pipe_.TakeSessionSocket(),
                                            std::move(on_frame),
                                            std::move(on_close));
  }

  SocketPairPipe pipe_;
  std::shared_ptr<TcpSession> session_;
};

TEST_F(TcpSessionTest, ReceivesLengthPrefixedFrames) {
  std::vector<std::string> frames;
  MakeSession([&](std::shared_ptr<Session>, std::string&& payload) {
    frames.push_back(std::move(payload));
  });
  session_->Start();

  ASSERT_TRUE(pipe_.WriteAll(FrameBytes("hello")));
  pipe_.Pump();
  ASSERT_EQ(frames.size(), 1u);
  EXPECT_EQ(frames[0], "hello");

  // A second frame on the same connection
  ASSERT_TRUE(pipe_.WriteAll(FrameBytes("second")));
  pipe_.Pump();
  ASSERT_EQ(frames.size(), 2u);
  EXPECT_EQ(frames[1], "second");
}

TEST_F(TcpSessionTest, FragmentedFrameIsReassembled) {
  std::vector<std::string> frames;
  MakeSession([&](std::shared_ptr<Session>, std::string&& payload) {
    frames.push_back(std::move(payload));
  });
  session_->Start();

  const std::string wire = FrameBytes("assembled");
  ASSERT_TRUE(pipe_.WriteAll(wire.substr(0, 3)));
  pipe_.Pump();
  EXPECT_TRUE(frames.empty());  // not complete yet

  ASSERT_TRUE(pipe_.WriteAll(wire.substr(3)));
  pipe_.Pump();
  ASSERT_EQ(frames.size(), 1u);
  EXPECT_EQ(frames[0], "assembled");
}

TEST_F(TcpSessionTest, MultipleFramesInOneWrite) {
  std::vector<std::string> frames;
  MakeSession([&](std::shared_ptr<Session>, std::string&& payload) {
    frames.push_back(std::move(payload));
  });
  session_->Start();

  ASSERT_TRUE(pipe_.WriteAll(FrameBytes("one") + FrameBytes("two")));
  pipe_.Pump();
  ASSERT_EQ(frames.size(), 2u);
  EXPECT_EQ(frames[0], "one");
  EXPECT_EQ(frames[1], "two");
}

TEST_F(TcpSessionTest, SendWritesRawBytesToPeer) {
  MakeSession();
  session_->Start();

  session_->Send("raw-bytes");
  pipe_.Pump();

  EXPECT_EQ(pipe_.ReadExactly(9), "raw-bytes");
}

TEST_F(TcpSessionTest, QueuedSendsAreFlushedInOrder) {
  MakeSession();
  session_->Start();

  session_->Send("first");
  session_->Send("second");
  session_->Send("third");
  pipe_.Pump();

  EXPECT_EQ(pipe_.ReadExactly(17), "firstsecondthird");
}

TEST_F(TcpSessionTest, SendAndCloseFlushesThenCloses) {
  bool closed = false;
  MakeSession(nullptr, [&](std::shared_ptr<Session>) { closed = true; });
  session_->Start();

  session_->SendAndClose("bye");
  pipe_.Pump();

  EXPECT_EQ(pipe_.ReadExactly(3), "bye");
  EXPECT_TRUE(pipe_.WaitEof());
  EXPECT_TRUE(closed);
}

TEST_F(TcpSessionTest, CloseTriggersCallbackOnce) {
  int close_count = 0;
  MakeSession(nullptr, [&](std::shared_ptr<Session>) { close_count++; });
  session_->Start();

  session_->Close();
  pipe_.Pump();
  session_->Close();  // second close: no extra callback
  pipe_.Pump();

  EXPECT_EQ(close_count, 1);
  EXPECT_TRUE(session_->IsClosed());
}

TEST_F(TcpSessionTest, PeerCloseTriggersSessionClose) {
  bool closed = false;
  MakeSession(nullptr, [&](std::shared_ptr<Session>) { closed = true; });
  session_->Start();

  pipe_.test_side->close();
  pipe_.Pump();

  EXPECT_TRUE(closed);
}

TEST_F(TcpSessionTest, RemoteEndpointIsCallable) {
  MakeSession();
  session_->Start();
  // On a socketpair fd remote_endpoint fails and yields the default
  // endpoint; the call itself must not throw.
  auto ep = session_->RemoteEndpoint();
  (void)ep;
  auto addr = session_->RemoteAddress();
  (void)addr;
  SUCCEED();
}

TEST_F(TcpSessionTest, SendAfterCloseIsDropped) {
  MakeSession();
  session_->Start();
  session_->Close();
  pipe_.Pump();

  session_->Send("dropped");  // socket closed -> async write fails path
  pipe_.Pump();
  // Drain; nothing must arrive (EOF seen instead).
  EXPECT_TRUE(pipe_.WaitEof());
}

TEST_F(TcpSessionTest, PeerHalfClosedTracksPeerFinAndOwnClose) {
  // No Start(): the read loop would consume the peer's FIN and close the
  // session before the peek could observe it.
  MakeSession();

  // Nothing pending and no FIN: still alive.
  EXPECT_FALSE(session_->PeerHalfClosed());

  // After our own Close() the answer is unconditionally yes.
  session_->Close();
  pipe_.Pump();
  EXPECT_TRUE(session_->PeerHalfClosed());

  // Pending unread data reads as alive (second pipe: the first session is
  // closed now).
  SocketPairPipe data_pipe;
  auto data_session = std::make_shared<TcpSession>(data_pipe.TakeSessionSocket(),
                                                   nullptr, nullptr);
  ASSERT_TRUE(data_pipe.WriteAll("payload"));
  EXPECT_FALSE(data_session->PeerHalfClosed());

  // A FIN with an empty receive buffer is the half-closed state: writes to
  // this session would vanish. Fresh pipe - the peek above never consumed
  // the pending payload, and data must not mask the FIN here.
  SocketPairPipe fin_pipe;
  auto fin_session = std::make_shared<TcpSession>(fin_pipe.TakeSessionSocket(),
                                                  nullptr, nullptr);
  fin_pipe.test_side->shutdown(asio::ip::tcp::socket::shutdown_send);
  EXPECT_TRUE(fin_session->PeerHalfClosed());
}

// ---------------------------------------------------------------------------
// WebSocketSession
// ---------------------------------------------------------------------------

class WebSocketSessionTest : public ::testing::Test {
 protected:
  static constexpr const char* kKey = "dGhlIHNhbXBsZSBub25jZQ==";
  // Cross-checked against python hashlib + openssl
  static constexpr const char* kAccept = "s3pPLMBiTxaQ9kYGzzhZRbK+xOo=";

  std::string HandshakeRequest(const std::string& key = kKey) {
    return "GET /ws HTTP/1.1\r\n"
           "Host: test\r\n"
           "Upgrade: websocket\r\n"
           "Connection: Upgrade\r\n" +
           (key.empty() ? std::string("")
                        : "Sec-WebSocket-Key: " + key + "\r\n") +
           "Sec-WebSocket-Version: 13\r\n"
           "\r\n";
  }

  void MakeSession(
      TcpSession::FrameCallback on_frame = nullptr,
      TcpSession::CloseCallback on_close = nullptr) {
    session_ = std::make_shared<WebSocketSession>(pipe_.TakeSessionSocket(),
                                                  std::move(on_frame),
                                                  std::move(on_close));
  }

  SocketPairPipe pipe_;
  std::shared_ptr<WebSocketSession> session_;
};

TEST_F(WebSocketSessionTest, HandshakeReplies101WithCorrectAccept) {
  MakeSession();
  session_->Start();

  ASSERT_TRUE(pipe_.WriteAll(HandshakeRequest()));
  pipe_.Pump();

  const std::string resp = pipe_.ReadUntil("\r\n\r\n");
  EXPECT_NE(resp.find("HTTP/1.1 101 Switching Protocols"), std::string::npos);
  EXPECT_NE(resp.find("Upgrade: websocket"), std::string::npos);
  EXPECT_NE(resp.find("Connection: Upgrade"), std::string::npos);
  EXPECT_NE(resp.find(std::string("Sec-WebSocket-Accept: ") + kAccept),
            std::string::npos);
}

TEST_F(WebSocketSessionTest, HandshakeWithoutKeyHeaderStillResponds) {
  MakeSession();
  session_->Start();

  ASSERT_TRUE(pipe_.WriteAll(HandshakeRequest(/*key=*/"")));
  pipe_.Pump();

  const std::string resp = pipe_.ReadUntil("\r\n\r\n");
  EXPECT_NE(resp.find("101 Switching Protocols"), std::string::npos);
  // Accept computed from an empty key (implementation must not crash)
  EXPECT_NE(resp.find("Sec-WebSocket-Accept: "), std::string::npos);
}

TEST_F(WebSocketSessionTest, HandshakeDeliveredInFragments) {
  MakeSession();
  session_->Start();

  const std::string req = HandshakeRequest();
  ASSERT_TRUE(pipe_.WriteAll(req.substr(0, 20)));
  pipe_.Pump();
  ASSERT_TRUE(pipe_.WriteAll(req.substr(20, 25)));
  pipe_.Pump();
  ASSERT_TRUE(pipe_.WriteAll(req.substr(45)));
  pipe_.Pump();

  EXPECT_NE(pipe_.ReadUntil("\r\n\r\n").find("101"), std::string::npos);
}

TEST_F(WebSocketSessionTest, BinaryFrameAfterHandshakeDeliversPayload) {
  std::vector<std::string> frames;
  MakeSession([&](std::shared_ptr<Session>, std::string&& payload) {
    frames.push_back(std::move(payload));
  });
  session_->Start();

  ASSERT_TRUE(pipe_.WriteAll(HandshakeRequest()));
  pipe_.Pump();
  (void)pipe_.ReadUntil("\r\n\r\n");

  ASSERT_TRUE(pipe_.WriteAll(BuildWebSocketFrame(0x2, FrameBytes("pay"), true)));
  pipe_.Pump();

  ASSERT_EQ(frames.size(), 1u);
  EXPECT_EQ(frames[0], "pay");
}

TEST_F(WebSocketSessionTest, FramesPiggybackedOnHandshake) {
  std::vector<std::string> frames;
  MakeSession([&](std::shared_ptr<Session>, std::string&& payload) {
    frames.push_back(std::move(payload));
  });
  session_->Start();

  // Handshake and a complete ws frame in a single write (leftover path)
  ASSERT_TRUE(pipe_.WriteAll(HandshakeRequest() +
                             BuildWebSocketFrame(0x2, FrameBytes("left"), true)));
  pipe_.Pump();
  (void)pipe_.ReadUntil("\r\n\r\n");

  ASSERT_EQ(frames.size(), 1u);
  EXPECT_EQ(frames[0], "left");
}

TEST_F(WebSocketSessionTest, UnmaskedClientFramesAreAccepted) {
  std::vector<std::string> frames;
  MakeSession([&](std::shared_ptr<Session>, std::string&& payload) {
    frames.push_back(std::move(payload));
  });
  session_->Start();

  ASSERT_TRUE(pipe_.WriteAll(HandshakeRequest()));
  pipe_.Pump();
  (void)pipe_.ReadUntil("\r\n\r\n");

  ASSERT_TRUE(pipe_.WriteAll(BuildWebSocketFrame(0x2, FrameBytes("plain"), false)));
  pipe_.Pump();
  ASSERT_EQ(frames.size(), 1u);
  EXPECT_EQ(frames[0], "plain");
}

TEST_F(WebSocketSessionTest, PingIsAnsweredWithPong) {
  MakeSession();
  session_->Start();

  ASSERT_TRUE(pipe_.WriteAll(HandshakeRequest()));
  pipe_.Pump();
  (void)pipe_.ReadUntil("\r\n\r\n");

  ASSERT_TRUE(pipe_.WriteAll(BuildWebSocketFrame(0x9, "ping-data", true)));
  pipe_.Pump();

  WebSocketFrameParser parser;
  parser.Append(reinterpret_cast<const uint8_t*>("\x8a"), 1);  // placeholder
  // Read the pong frame bytes (header 2 + payload 9)
  const std::string pong = pipe_.ReadExactly(2 + 9);
  parser.Clear();
  parser.Append(reinterpret_cast<const uint8_t*>(pong.data()), pong.size());
  auto f = parser.PopFrame();
  ASSERT_TRUE(f.has_value());
  EXPECT_EQ(f->opcode, 0xA);
  EXPECT_EQ(f->payload, "ping-data");
}

TEST_F(WebSocketSessionTest, CloseFrameEchoesAndCloses) {
  bool closed = false;
  MakeSession(nullptr, [&](std::shared_ptr<Session>) { closed = true; });
  session_->Start();

  ASSERT_TRUE(pipe_.WriteAll(HandshakeRequest()));
  pipe_.Pump();
  (void)pipe_.ReadUntil("\r\n\r\n");

  ASSERT_TRUE(pipe_.WriteAll(BuildWebSocketFrame(0x8, "", true)));
  pipe_.Pump();

  // Expect an echoed close frame: FIN + opcode 0x8, length 0
  const std::string echo = pipe_.ReadExactly(2);
  ASSERT_EQ(echo.size(), 2u);
  EXPECT_EQ(static_cast<uint8_t>(echo[0]), 0x88);
  EXPECT_TRUE(closed);
}

TEST_F(WebSocketSessionTest, FragmentedWebSocketFrameClosesSession) {
  bool closed = false;
  MakeSession(nullptr, [&](std::shared_ptr<Session>) { closed = true; });
  session_->Start();

  ASSERT_TRUE(pipe_.WriteAll(HandshakeRequest()));
  pipe_.Pump();
  (void)pipe_.ReadUntil("\r\n\r\n");

  // FIN=0 fragment: server does not support continuation -> close
  std::string frag;
  frag.push_back(static_cast<char>(0x02));  // FIN=0, opcode 0x2
  frag.push_back(static_cast<char>(0x83));  // masked, len 3
  frag += std::string("\x01\x02\x03\x04", 4);  // mask key
  frag += std::string("\xAA\xBB\xCC", 3);      // masked payload
  ASSERT_TRUE(pipe_.WriteAll(frag));
  pipe_.Pump();

  EXPECT_TRUE(closed);
}

TEST_F(WebSocketSessionTest, TextFramesAreIgnored) {
  std::vector<std::string> frames;
  bool closed = false;
  MakeSession([&](std::shared_ptr<Session>, std::string&& payload) {
    frames.push_back(std::move(payload));
  },
              [&](std::shared_ptr<Session>) { closed = true; });
  session_->Start();

  ASSERT_TRUE(pipe_.WriteAll(HandshakeRequest()));
  pipe_.Pump();
  (void)pipe_.ReadUntil("\r\n\r\n");

  ASSERT_TRUE(pipe_.WriteAll(BuildWebSocketFrame(0x1, "text", true)));
  pipe_.Pump();

  EXPECT_TRUE(frames.empty());
  EXPECT_FALSE(closed);
}

TEST_F(WebSocketSessionTest, ServerSendWrapsPayloadInBinaryFrame) {
  MakeSession();
  session_->Start();

  ASSERT_TRUE(pipe_.WriteAll(HandshakeRequest()));
  pipe_.Pump();
  (void)pipe_.ReadUntil("\r\n\r\n");

  session_->Send("payload");
  pipe_.Pump();

  const std::string wire = pipe_.ReadExactly(2 + 7);
  WebSocketFrameParser parser;
  parser.Append(reinterpret_cast<const uint8_t*>(wire.data()), wire.size());
  auto f = parser.PopFrame();
  ASSERT_TRUE(f.has_value());
  EXPECT_EQ(f->opcode, 0x2);
  EXPECT_TRUE(f->fin);
  EXPECT_EQ(f->payload, "payload");
}

TEST_F(WebSocketSessionTest, SendAndCloseFlushesThenCloses) {
  bool closed = false;
  MakeSession(nullptr, [&](std::shared_ptr<Session>) { closed = true; });
  session_->Start();

  ASSERT_TRUE(pipe_.WriteAll(HandshakeRequest()));
  pipe_.Pump();
  (void)pipe_.ReadUntil("\r\n\r\n");

  session_->SendAndClose("last");
  pipe_.Pump();

  const std::string wire = pipe_.ReadExactly(2 + 4);
  WebSocketFrameParser parser;
  parser.Append(reinterpret_cast<const uint8_t*>(wire.data()), wire.size());
  auto f = parser.PopFrame();
  ASSERT_TRUE(f.has_value());
  EXPECT_EQ(f->payload, "last");
  EXPECT_TRUE(pipe_.WaitEof());
  EXPECT_TRUE(closed);
}

TEST_F(WebSocketSessionTest, SendAfterCloseIsDropped) {
  bool closed = false;
  MakeSession(nullptr, [&](std::shared_ptr<Session>) { closed = true; });
  session_->Start();

  session_->Close();
  pipe_.Pump();
  ASSERT_TRUE(closed);

  session_->Send("dropped");
  session_->SendAndClose("dropped");
  pipe_.Pump();
  SUCCEED();  // no crash, no bytes
}

TEST_F(WebSocketSessionTest, PeerCloseTriggersCloseCallback) {
  bool closed = false;
  MakeSession(nullptr, [&](std::shared_ptr<Session>) { closed = true; });
  session_->Start();

  ASSERT_TRUE(pipe_.WriteAll(HandshakeRequest()));
  pipe_.Pump();
  (void)pipe_.ReadUntil("\r\n\r\n");

  pipe_.test_side->close();
  pipe_.Pump();
  EXPECT_TRUE(closed);
}

TEST_F(WebSocketSessionTest, RemoteEndpointIsCallable) {
  MakeSession();
  session_->Start();
  auto ep = session_->RemoteEndpoint();
  (void)ep;
  auto addr = session_->RemoteAddress();
  (void)addr;
  SUCCEED();
}

// ---------------------------------------------------------------------------
// TcpClient / WebSocketClient failure paths (no listeners anywhere)
// ---------------------------------------------------------------------------

class ClientFailureTest : public ::testing::Test {};

TEST_F(ClientFailureTest, TcpConnectRefusedReturnsFalse) {
  asio::io_context io;
  TcpClient client(io);
  client.SetCallbacks([](std::shared_ptr<Session>, std::string&&) {},
                      [](std::shared_ptr<Session>) {});
  EXPECT_FALSE(client.Connect("127.0.0.1", 1));
}

TEST_F(ClientFailureTest, TcpConnectInvalidAddressReturnsFalse) {
  asio::io_context io;
  TcpClient client(io);
  EXPECT_FALSE(client.Connect("not-an-ip-address", 1234));
}

TEST_F(ClientFailureTest, TcpDisconnectWithoutConnectIsSafe) {
  asio::io_context io;
  {
    TcpClient client(io);
    client.Disconnect();
  }
  SUCCEED();
}

TEST_F(ClientFailureTest, WebSocketConnectRefusedReturnsFalse) {
  asio::io_context io;
  WebSocketClient client(io);
  client.SetCallbacks([](std::shared_ptr<Session>, std::string&&) {},
                      [](std::shared_ptr<Session>) {});
  EXPECT_FALSE(client.Connect("127.0.0.1", 1, "/ws"));
}

TEST_F(ClientFailureTest, WebSocketConnectInvalidAddressReturnsFalse) {
  asio::io_context io;
  WebSocketClient client(io);
  EXPECT_FALSE(client.Connect("no-such-host.invalid", 1234, "/"));
}

TEST_F(ClientFailureTest, WebSocketDisconnectWithoutConnectIsSafe) {
  asio::io_context io;
  {
    WebSocketClient client(io);
    client.Disconnect();
  }
  SUCCEED();
}

// ---------------------------------------------------------------------------
// websocket_util.h inline helpers
// ---------------------------------------------------------------------------

TEST(WebSocketUtilsTest, HandshakeRequestFormat) {
  const std::string req = chirp::network::BuildWebSocketHandshake("localhost", 8080);
  EXPECT_NE(req.find("GET / HTTP/1.1\r\n"), std::string::npos);
  EXPECT_NE(req.find("Host: localhost:8080\r\n"), std::string::npos);
  EXPECT_NE(req.find("Upgrade: websocket\r\n"), std::string::npos);
  EXPECT_NE(req.find("Sec-WebSocket-Key: dGhlIHNhbXBsZSBub25jZQ==\r\n"),
            std::string::npos);
  EXPECT_NE(req.find("Sec-WebSocket-Version: 13\r\n"), std::string::npos);
  EXPECT_EQ(req.substr(req.size() - 4), "\r\n\r\n");

  const std::string custom = chirp::network::BuildWebSocketHandshake("h", 1, "/chat");
  EXPECT_NE(custom.find("GET /chat HTTP/1.1\r\n"), std::string::npos);
}

TEST(WebSocketUtilsTest, UpgradeSuccessDetection) {
  EXPECT_TRUE(chirp::network::IsWebSocketUpgradeSuccessful(
      "HTTP/1.1 101 Switching Protocols\r\n\r\n"));
  EXPECT_FALSE(chirp::network::IsWebSocketUpgradeSuccessful(
      "HTTP/1.1 400 Bad Request\r\n\r\n"));
  EXPECT_FALSE(chirp::network::IsWebSocketUpgradeSuccessful(""));
}

}  // namespace

// ---------------------------------------------------------------------------
// Loopback link tests: real 127.0.0.1 listeners drive server DoAccept paths
// and client success paths (connect + handshake). All sockets stay on the
// host loopback interface; random ephemeral ports are used.
// ---------------------------------------------------------------------------

#include "network/tcp_server.h"
#include "network/websocket_server.h"

namespace {

// Finds a free ephemeral port by binding an acceptor and closing it again.
uint16_t FreePort() {
  asio::io_context io;
  asio::ip::tcp::acceptor acc(io, asio::ip::tcp::endpoint(asio::ip::tcp::v4(), 0));
  return static_cast<uint16_t>(acc.local_endpoint().port());
}

class LoopbackLinkTest : public ::testing::Test {};

TEST_F(LoopbackLinkTest, TcpServerAcceptsClientAndDeliversFrames) {
  asio::io_context io;
  std::vector<std::string> got;
  bool closed = false;

  uint16_t port = FreePort();
  chirp::network::TcpServer server(
      io, port,
      [&](std::shared_ptr<Session> s, std::string&& payload) {
        got.push_back(std::move(payload));
        s->Send(FrameBytes("pong"));
      },
      [&](std::shared_ptr<Session>) { closed = true; });
  server.Start();

  // Run the server io on a helper thread while the client connects
  // synchronously from the test thread.
  std::thread server_thread([&io] { io.run(); });

  chirp::network::TcpClient client(io);
  std::vector<std::string> echoed;
  client.SetCallbacks(
      [&](std::shared_ptr<Session>, std::string&& payload) {
        echoed.push_back(std::move(payload));
      },
      [](std::shared_ptr<Session>) {});
  ASSERT_TRUE(client.Connect("127.0.0.1", port));

  client.GetSession()->Send(FrameBytes("ping"));
  // Wait for the frame to reach the server
  for (int i = 0; i < 300 && got.empty(); ++i) {
    std::this_thread::sleep_for(std::chrono::milliseconds(2));
  }
  EXPECT_EQ(got.size(), 1u);
  EXPECT_EQ(got[0], "ping");

  // Server echo reaches the client again
  for (int i = 0; i < 300 && echoed.empty(); ++i) {
    std::this_thread::sleep_for(std::chrono::milliseconds(2));
  }
  EXPECT_EQ(echoed.size(), 1u);
  EXPECT_EQ(echoed[0], "pong");

  client.Disconnect();
  for (int i = 0; i < 300 && !closed; ++i) {
    std::this_thread::sleep_for(std::chrono::milliseconds(2));
  }
  EXPECT_TRUE(closed);

  server.Stop();
  io.stop();
  server_thread.join();
}

TEST_F(LoopbackLinkTest, WebSocketServerCompletesClientHandshake) {
  asio::io_context io;
  std::vector<std::string> got;
  bool closed = false;

  uint16_t port = FreePort();
  chirp::network::WebSocketServer server(
      io, port,
      [&](std::shared_ptr<Session> s, std::string&& payload) {
        got.push_back(std::move(payload));
        s->Send(FrameBytes("pong"));
      },
      [&](std::shared_ptr<Session>) { closed = true; });
  server.Start();

  std::thread server_thread([&io] { io.run(); });

  chirp::network::WebSocketClient client(io);
  std::vector<std::string> echoed;
  client.SetCallbacks(
      [&](std::shared_ptr<Session>, std::string&& payload) {
        echoed.push_back(std::move(payload));
      },
      [](std::shared_ptr<Session>) {});

  // Connect performs the full HTTP upgrade handshake synchronously.
  EXPECT_TRUE(client.Connect("127.0.0.1", port, "/ws"));

  client.GetSession()->Send(FrameBytes("ping"));
  for (int i = 0; i < 300 && got.empty(); ++i) {
    std::this_thread::sleep_for(std::chrono::milliseconds(2));
  }
  EXPECT_EQ(got.size(), 1u);
  if (!got.empty()) EXPECT_EQ(got[0], "ping");

  for (int i = 0; i < 300 && echoed.empty(); ++i) {
    std::this_thread::sleep_for(std::chrono::milliseconds(2));
  }
  EXPECT_EQ(echoed.size(), 1u);
  if (!echoed.empty()) EXPECT_EQ(echoed[0], "pong");

  client.Disconnect();
  server.Stop();
  io.stop();
  server_thread.join();
}

TEST_F(LoopbackLinkTest, TcpServerAcceptsMultipleSequentialClients) {
  asio::io_context io;
  std::atomic<int> connections{0};

  uint16_t port = FreePort();
  chirp::network::TcpServer server(
      io, port,
      [&](std::shared_ptr<Session>, std::string&&) {},
      [&](std::shared_ptr<Session>) {});
  server.Start();
  std::thread server_thread([&io] { io.run(); });

  for (int i = 0; i < 3; ++i) {
    chirp::network::TcpClient client(io);
    client.SetCallbacks([](std::shared_ptr<Session>, std::string&&) {},
                        [](std::shared_ptr<Session>) {});
    if (!client.Connect("127.0.0.1", port)) {
      ADD_FAILURE() << "client connect failed";
    }
    client.GetSession()->Send(FrameBytes("x"));
    std::this_thread::sleep_for(std::chrono::milliseconds(20));
    client.Disconnect();
  }

  server.Stop();
  io.stop();
  server_thread.join();
  SUCCEED();
}

}  // namespace
