// Coverage tests for the full network library:
// redis_protocol, redis_client (against an in-process mock Redis server),
// tcp/websocket sessions, servers, clients, message_router.
#include <gtest/gtest.h>

#include <asio.hpp>

#include <algorithm>
#include <array>
#include <atomic>
#include <chrono>
#include <condition_variable>
#include <cstdint>
#include <cstdlib>
#include <deque>
#include <future>
#include <memory>
#include <mutex>
#include <optional>
#include <string>
#include <string_view>
#include <thread>
#include <unordered_map>
#include <vector>

#include "network/length_prefixed_framer.h"
#include "network/message_router.h"
#include "network/protobuf_framing.h"
#include "network/redis_client.h"
#include "network/redis_protocol.h"
#include "network/session.h"
#include "network/tcp_client.h"
#include "network/tcp_server.h"
#include "network/tcp_session.h"
#include "network/websocket_client.h"
#include "network/websocket_frame.h"
#include "network/websocket_server.h"
#include "network/websocket_session.h"
#include "network/websocket_util.h"
#include "network/websocket_utils.h"

#include "proto/chat.pb.h"

namespace chirp::network {
namespace {

using Clock = std::chrono::steady_clock;

template <typename Pred>
bool WaitFor(Pred&& pred, int timeout_ms = 3000) {
  const auto deadline = Clock::now() + std::chrono::milliseconds(timeout_ms);
  while (Clock::now() < deadline) {
    if (pred()) {
      return true;
    }
    std::this_thread::sleep_for(std::chrono::milliseconds(5));
  }
  return pred();
}

uint16_t FreePort() {
  asio::io_context io;
  asio::ip::tcp::acceptor a(io, asio::ip::tcp::endpoint(asio::ip::tcp::v4(), 0));
  return static_cast<uint16_t>(a.local_endpoint().port());
}

std::string LpFrame(std::string_view payload) {
  const uint32_t n = static_cast<uint32_t>(payload.size());
  std::string out;
  out.push_back(static_cast<char>((n >> 24) & 0xFF));
  out.push_back(static_cast<char>((n >> 16) & 0xFF));
  out.push_back(static_cast<char>((n >> 8) & 0xFF));
  out.push_back(static_cast<char>(n & 0xFF));
  out.append(payload);
  return out;
}

// ---------------------------------------------------------------------------
// In-process mock Redis server: accepts many connections, parses RESP
// commands, records them and replies with canned responses keyed by the
// command verb (e.g. "GET" -> "$5\r\nhello\r\n").
// ---------------------------------------------------------------------------
class MockRedisServer {
public:
  using Replies = std::unordered_map<std::string, std::string>;

  MockRedisServer() : acceptor_(io_, asio::ip::tcp::endpoint(asio::ip::tcp::v4(), 0)) {}

  ~MockRedisServer() { Stop(); }

  MockRedisServer(const MockRedisServer&) = delete;
  MockRedisServer& operator=(const MockRedisServer&) = delete;

  uint16_t port() const {
    std::error_code ec;
    return static_cast<uint16_t>(acceptor_.local_endpoint(ec).port());
  }

  void Start(Replies replies) {
    replies_ = std::move(replies);
    DoAccept();
    th_ = std::thread([this] { io_.run(); });
  }

  void Stop() {
    if (stopped_.exchange(true)) {
      return;
    }
    io_.stop();
    if (th_.joinable()) {
      th_.join();
    }
  }

  std::vector<std::vector<std::string>> Commands() const {
    std::lock_guard<std::mutex> lock(mu_);
    return cmds_;
  }

  size_t ConnectionCount() const {
    std::lock_guard<std::mutex> lock(mu_);
    return conns_.size();
  }

  // Writes raw bytes to every open connection (used to feed the subscriber).
  void PushRaw(const std::string& data) {
    asio::post(io_, [this, data] {
      std::lock_guard<std::mutex> lock(mu_);
      for (auto& c : conns_) {
        if (c->sock.is_open()) {
          std::error_code ec;
          asio::write(c->sock, asio::buffer(data), ec);
        }
      }
    });
  }

  // Abruptly resets every open connection (SO_LINGER 0 -> RST to the peer).
  void ResetConnections() {
    asio::post(io_, [this] {
      std::lock_guard<std::mutex> lock(mu_);
      for (auto& c : conns_) {
        if (c->sock.is_open()) {
          std::error_code ec;
          c->sock.set_option(asio::socket_base::linger(true, 0), ec);
          c->sock.close(ec);
        }
      }
    });
  }

  // Shuts down the read side only: any subsequent bytes written by the peer
  // are answered with a TCP reset (deterministic ECONNRESET at the peer).
  void ShutdownReceive() {
    asio::post(io_, [this] {
      std::lock_guard<std::mutex> lock(mu_);
      for (auto& c : conns_) {
        if (c->sock.is_open()) {
          std::error_code ec;
          c->sock.shutdown(asio::ip::tcp::socket::shutdown_receive, ec);
        }
      }
    });
  }

  // Gracefully closes every open connection (peer sees EOF on read).
  void CloseConnections() {
    asio::post(io_, [this] {
      std::lock_guard<std::mutex> lock(mu_);
      for (auto& c : conns_) {
        if (c->sock.is_open()) {
          std::error_code ec;
          c->sock.close(ec);
        }
      }
    });
  }

  // Bytes written to the socket immediately after accept (e.g. a fake HTTP
  // response for WebSocket handshake tests).
  void SetPushOnConnect(std::string data) { push_on_connect_ = std::move(data); }

private:
  struct Conn {
    explicit Conn(asio::ip::tcp::socket s) : sock(std::move(s)) {}
    asio::ip::tcp::socket sock;
    std::string rbuf;
    std::array<char, 4096> buf{};
  };

  void DoAccept() {
    acceptor_.async_accept([this](std::error_code ec, asio::ip::tcp::socket sock) {
      if (stopped_.load()) {
        return;
      }
      if (!ec) {
        auto conn = std::make_shared<Conn>(std::move(sock));
        {
          std::lock_guard<std::mutex> lock(mu_);
          conns_.push_back(conn);
          if (!push_on_connect_.empty()) {
            std::error_code wec;
            asio::write(conn->sock, asio::buffer(push_on_connect_), wec);
          }
        }
        DoRead(conn);
      }
      if (acceptor_.is_open() && !stopped_.load()) {
        DoAccept();
      }
    });
  }

  void DoRead(std::shared_ptr<Conn> conn) {
    conn->sock.async_read_some(asio::buffer(conn->buf),
                               [this, conn](std::error_code ec, std::size_t n) {
                                 if (ec) {
                                   return;
                                 }
                                 conn->rbuf.append(conn->buf.data(), n);
                                 HandleBytes(conn);
                                 DoRead(conn);
                               });
  }

  void HandleBytes(std::shared_ptr<Conn> conn) {
    auto parsed = ParseRespCommands(conn->rbuf);
    if (parsed.empty()) {
      return;
    }
    std::error_code ec;
    for (const auto& cmd : parsed) {
      {
        std::lock_guard<std::mutex> lock(mu_);
        cmds_.push_back(cmd);
      }
      auto it = replies_.find(cmd.at(0));
      if (it != replies_.end() && conn->sock.is_open()) {
        asio::write(conn->sock, asio::buffer(it->second), ec);
      }
    }
  }

  // Parses complete RESP arrays ("<*n> <$len> <arg>...") from the buffer,
  // removing consumed bytes; stops at the first incomplete command.
  static std::vector<std::vector<std::string>> ParseRespCommands(std::string& buf) {
    std::vector<std::vector<std::string>> out;
    size_t pos = 0;
    while (pos < buf.size() && buf[pos] == '*') {
      auto line_end = buf.find("\r\n", pos);
      if (line_end == std::string::npos) {
        break;
      }
      const long n = std::strtol(buf.c_str() + pos + 1, nullptr, 10);
      if (n < 0 || n > 32) {
        pos = line_end + 2;
        continue;
      }
      size_t cur = line_end + 2;
      std::vector<std::string> args;
      bool complete = true;
      for (long i = 0; i < n; ++i) {
        if (cur >= buf.size() || buf[cur] != '$') {
          complete = false;
          break;
        }
        auto le = buf.find("\r\n", cur);
        if (le == std::string::npos) {
          complete = false;
          break;
        }
        const long len = std::strtol(buf.c_str() + cur + 1, nullptr, 10);
        if (len < 0 || buf.size() < le + 2 + static_cast<size_t>(len) + 2) {
          complete = false;
          break;
        }
        args.emplace_back(buf.substr(le + 2, static_cast<size_t>(len)));
        cur = le + 2 + static_cast<size_t>(len) + 2;
      }
      if (!complete) {
        break;
      }
      out.push_back(std::move(args));
      pos = cur;
    }
    buf.erase(0, pos);
    return out;
  }

  asio::io_context io_;
  asio::ip::tcp::acceptor acceptor_;
  std::thread th_;
  std::atomic<bool> stopped_{false};
  Replies replies_;
  std::string push_on_connect_;

  mutable std::mutex mu_;
  std::vector<std::shared_ptr<Conn>> conns_;
  std::vector<std::vector<std::string>> cmds_;
};

// ---------------------------------------------------------------------------
// RedisRespParser / BuildRedisCommand (pure logic)
// ---------------------------------------------------------------------------

TEST(RedisProtocolTest, PopsSimpleStringErrorAndInteger) {
  RedisRespParser p;
  p.Append(reinterpret_cast<const uint8_t*>("+OK\r\n"), 5);
  auto r = p.Pop();
  ASSERT_TRUE(r.has_value());
  EXPECT_EQ(r->type, RedisResp::Type::kSimpleString);
  EXPECT_EQ(r->str, "OK");

  p.Append(reinterpret_cast<const uint8_t*>("-ERR bad\r\n"), 10);
  r = p.Pop();
  ASSERT_TRUE(r.has_value());
  EXPECT_EQ(r->type, RedisResp::Type::kError);
  EXPECT_EQ(r->str, "ERR bad");

  p.Append(reinterpret_cast<const uint8_t*>(":42\r\n"), 6);
  r = p.Pop();
  ASSERT_TRUE(r.has_value());
  EXPECT_EQ(r->type, RedisResp::Type::kInteger);
  EXPECT_EQ(r->integer, 42);
}

TEST(RedisProtocolTest, BulkStringAndNullBulk) {
  RedisRespParser p;
  p.Append(reinterpret_cast<const uint8_t*>("$5\r\nhello\r\n"), 11);
  auto r = p.Pop();
  ASSERT_TRUE(r.has_value());
  EXPECT_EQ(r->type, RedisResp::Type::kBulkString);
  EXPECT_EQ(r->str, "hello");

  p.Append(reinterpret_cast<const uint8_t*>("$-1\r\n"), 5);
  r = p.Pop();
  ASSERT_TRUE(r.has_value());
  EXPECT_EQ(r->type, RedisResp::Type::kNull);
}

TEST(RedisProtocolTest, IncompleteBulkStringWaitsForData) {
  RedisRespParser p;
  const std::string full = "$5\r\nhello\r\n";
  p.Append(reinterpret_cast<const uint8_t*>(full.data()), 4);
  EXPECT_FALSE(p.Pop().has_value());
  p.Append(reinterpret_cast<const uint8_t*>(full.data() + 4), 3);
  EXPECT_FALSE(p.Pop().has_value());
  p.Append(reinterpret_cast<const uint8_t*>(full.data() + 7), 4);
  auto r = p.Pop();
  ASSERT_TRUE(r.has_value());
  EXPECT_EQ(r->str, "hello");
}

TEST(RedisProtocolTest, ArrayHeaderWithoutLinesYieldsNothing) {
  RedisRespParser p;
  const std::string hdr = "*3\r"; // no trailing newline: line read fails
  p.Append(reinterpret_cast<const uint8_t*>(hdr.data()), hdr.size());
  EXPECT_FALSE(p.Pop().has_value());
}

TEST(RedisProtocolTest, ArrayFlatAndNested) {
  RedisRespParser p;
  const std::string data = "*2\r\n$3\r\nabc\r\n$3\r\ndef\r\n";
  p.Append(reinterpret_cast<const uint8_t*>(data.data()), data.size());
  auto r = p.Pop();
  ASSERT_TRUE(r.has_value());
  ASSERT_EQ(r->type, RedisResp::Type::kArray);
  ASSERT_EQ(r->array.size(), 2u);
  EXPECT_EQ(r->array[0].str, "abc");
  EXPECT_EQ(r->array[1].str, "def");

  const std::string nested = "*2\r\n:7\r\n*1\r\n$2\r\nhi\r\n";
  p.Append(reinterpret_cast<const uint8_t*>(nested.data()), nested.size());
  r = p.Pop();
  ASSERT_TRUE(r.has_value());
  ASSERT_EQ(r->array.size(), 2u);
  EXPECT_EQ(r->array[0].type, RedisResp::Type::kInteger);
  EXPECT_EQ(r->array[0].integer, 7);
  ASSERT_EQ(r->array[1].array.size(), 1u);
  EXPECT_EQ(r->array[1].array[0].str, "hi");
}

TEST(RedisProtocolTest, NullAndIncompleteArrays) {
  RedisRespParser p;
  p.Append(reinterpret_cast<const uint8_t*>("*-1\r\n"), 5);
  auto r = p.Pop();
  ASSERT_TRUE(r.has_value());
  EXPECT_EQ(r->type, RedisResp::Type::kNull);

  // Only the header line has arrived so far.
  p.Append(reinterpret_cast<const uint8_t*>("*2\r\n$1\r\na"), 9);
  EXPECT_FALSE(p.Pop().has_value());

  // Empty array pops fine.
  p.Clear();
  p.Append(reinterpret_cast<const uint8_t*>("*0\r\n"), 4);
  r = p.Pop();
  ASSERT_TRUE(r.has_value());
  EXPECT_TRUE(r->array.empty());
}

TEST(RedisProtocolTest, IncompleteLineAndUnknownType) {
  RedisRespParser p;
  EXPECT_FALSE(p.Pop().has_value());            // empty buffer
  p.Append(reinterpret_cast<const uint8_t*>("+"), 1);
  EXPECT_FALSE(p.Pop().has_value());            // no CRLF yet
  p.Clear();
  p.Append(reinterpret_cast<const uint8_t*>("X!"), 2);
  EXPECT_FALSE(p.Pop().has_value());            // unknown type marker
  p.Clear();
  p.Append(reinterpret_cast<const uint8_t*>("$"), 1);
  EXPECT_FALSE(p.Pop().has_value());            // '$' without line
}

TEST(RedisProtocolTest, QueuedMessagesPopInOrder) {
  RedisRespParser p;
  const std::string data = "+a\r\n+b\r\n:3\r\n";
  p.Append(reinterpret_cast<const uint8_t*>(data.data()), data.size());
  auto r1 = p.Pop();
  auto r2 = p.Pop();
  auto r3 = p.Pop();
  ASSERT_TRUE(r1 && r2 && r3);
  EXPECT_EQ(r1->str, "a");
  EXPECT_EQ(r2->str, "b");
  EXPECT_EQ(r3->integer, 3);
  EXPECT_FALSE(p.Pop().has_value());
}

TEST(RedisProtocolTest, BuildRedisCommandFormat) {
  const std::string cmd = BuildRedisCommand({"GET", "key"});
  EXPECT_EQ(cmd, "*2\r\n$3\r\nGET\r\n$3\r\nkey\r\n");

  const std::string empty = BuildRedisCommand({});
  EXPECT_EQ(empty, "*0\r\n");
}

// ---------------------------------------------------------------------------
// websocket_util / websocket_utils
// ---------------------------------------------------------------------------

TEST(WebSocketUtilTest, AcceptKeyRfc6455Vector) {
  // RFC 6455 section 1.3 example nonce/accept pair.
  EXPECT_EQ(ComputeWebSocketAccept("dGhlIHNhbXBsZSBub25jZQ=="), "s3pPLMBiTxaQ9kYGzzhZRbK+xOo=");
}

TEST(WebSocketUtilTest, IStartsWithCases) {
  EXPECT_TRUE(IStartsWith("Sec-WebSocket-Key: x", "sec-websocket-key:"));
  EXPECT_TRUE(IStartsWith("UPGRADE: websocket", "Upgrade:"));
  EXPECT_TRUE(IStartsWith("anything", ""));
  EXPECT_FALSE(IStartsWith("short", "shorter-than-s"));
  EXPECT_FALSE(IStartsWith("Connection: close", "Connect:"));
}

TEST(WebSocketUtilTest, TrimAsciiWhitespaceCases) {
  EXPECT_EQ(TrimAsciiWhitespace("  ab \t\r\n"), "ab");
  EXPECT_EQ(TrimAsciiWhitespace("\t\t"), "");
  EXPECT_EQ(TrimAsciiWhitespace("x"), "x");
  EXPECT_EQ(TrimAsciiWhitespace(""), "");
  EXPECT_EQ(TrimAsciiWhitespace(" a b "), "a b");
}

TEST(WebSocketUtilsTest, HandshakeAndUpgradeCheck) {
  const std::string hs = BuildWebSocketHandshake("example.com", 8080, "/ws");
  EXPECT_NE(hs.find("GET /ws HTTP/1.1\r\n"), std::string::npos);
  EXPECT_NE(hs.find("Host: example.com:8080\r\n"), std::string::npos);
  EXPECT_NE(hs.find("Sec-WebSocket-Key: dGhlIHNhbXBsZSBub25jZQ==\r\n"), std::string::npos);
  EXPECT_NE(hs.find("Sec-WebSocket-Version: 13\r\n"), std::string::npos);

  EXPECT_TRUE(IsWebSocketUpgradeSuccessful("HTTP/1.1 101 Switching Protocols\r\n"));
  EXPECT_FALSE(IsWebSocketUpgradeSuccessful("HTTP/1.1 400 Bad Request\r\n"));
  EXPECT_FALSE(IsWebSocketUpgradeSuccessful(""));
}

// ---------------------------------------------------------------------------
// WebSocketFrameParser / BuildWebSocketFrame
// ---------------------------------------------------------------------------

TEST(WebSocketFrameTest, RoundTripAllLengthClasses) {
  const std::string small = "abc";
  const std::string medium(300, 'm');
  const std::string large(70000, 'L');

  for (const std::string& payload : {small, medium, large}) {
    WebSocketFrameParser p;
    const std::string unmasked = BuildWebSocketFrame(0x2, payload, false);
    p.Append(reinterpret_cast<const uint8_t*>(unmasked.data()), unmasked.size());
    auto f = p.PopFrame();
    ASSERT_TRUE(f.has_value()) << payload.size();
    EXPECT_TRUE(f->fin);
    EXPECT_EQ(f->opcode, 0x2);
    EXPECT_EQ(f->payload, payload);
    EXPECT_FALSE(p.PopFrame().has_value());

    const std::string masked = BuildWebSocketFrame(0x2, payload, true);
    p.Append(reinterpret_cast<const uint8_t*>(masked.data()), masked.size());
    f = p.PopFrame();
    ASSERT_TRUE(f.has_value()) << payload.size();
    EXPECT_EQ(f->payload, payload);
  }
}

TEST(WebSocketFrameTest, EmptyPayloadFrame) {
  WebSocketFrameParser p;
  const std::string frame = BuildWebSocketFrame(0x8, "", false);
  p.Append(reinterpret_cast<const uint8_t*>(frame.data()), frame.size());
  auto f = p.PopFrame();
  ASSERT_TRUE(f.has_value());
  EXPECT_EQ(f->opcode, 0x8);
  EXPECT_TRUE(f->payload.empty());
}

TEST(WebSocketFrameTest, IncompleteHeaders) {
  WebSocketFrameParser p;
  // One byte only.
  p.Append(reinterpret_cast<const uint8_t*>("\x82"), 1);
  EXPECT_FALSE(p.PopFrame().has_value());

  // 16-bit extended length header split.
  p.Clear();
  p.Append(reinterpret_cast<const uint8_t*>("\x82\xfe\x01"), 3);
  EXPECT_FALSE(p.PopFrame().has_value());

  // 64-bit extended length missing.
  p.Clear();
  p.Append(reinterpret_cast<const uint8_t*>("\x82\xff\x00"), 3);
  EXPECT_FALSE(p.PopFrame().has_value());

  // Mask key incomplete.
  p.Clear();
  p.Append(reinterpret_cast<const uint8_t*>("\x82\x83\x01\x02"), 4);
  EXPECT_FALSE(p.PopFrame().has_value());

  // Payload incomplete.
  p.Clear();
  p.Append(reinterpret_cast<const uint8_t*>("\x82\x05ab"), 4);
  EXPECT_FALSE(p.PopFrame().has_value());
}

TEST(WebSocketFrameTest, OversizePayloadClearsBuffer) {
  WebSocketFrameParser p;
  // Header claims 16MB + 1 bytes (above the 16MiB safety limit).
  std::string frame;
  frame += static_cast<char>(0x82);
  frame += static_cast<char>(0xFF); // 64-bit length follows
  for (int i = 0; i < 8; ++i) {
    frame += static_cast<char>((i == 0) ? 0x01 : 0x00); // 0x0100000000... > limit
  }
  p.Append(reinterpret_cast<const uint8_t*>(frame.data()), frame.size());
  EXPECT_FALSE(p.PopFrame().has_value());

  // The buffer was dropped; a fresh valid frame parses afterwards.
  const std::string ok = BuildWebSocketFrame(0x2, "fine", false);
  p.Append(reinterpret_cast<const uint8_t*>(ok.data()), ok.size());
  auto f = p.PopFrame();
  ASSERT_TRUE(f.has_value());
  EXPECT_EQ(f->payload, "fine");
}

TEST(WebSocketFrameTest, FragmentedFrameFlagAndMultipleFrames) {
  WebSocketFrameParser p;
  // Hand-rolled unmasked frame with FIN=0, opcode=2, len=2.
  const std::string frag = std::string("\x02\x02hi", 4);
  p.Append(reinterpret_cast<const uint8_t*>(frag.data()), frag.size());
  auto f = p.PopFrame();
  ASSERT_TRUE(f.has_value());
  EXPECT_FALSE(f->fin);
  EXPECT_EQ(f->opcode, 0x2);

  // Two complete frames in a single buffer pop in order.
  p.Clear();
  const std::string two =
      BuildWebSocketFrame(0x2, "first", false) + BuildWebSocketFrame(0x9, "second", false);
  p.Append(reinterpret_cast<const uint8_t*>(two.data()), two.size());
  auto a = p.PopFrame();
  auto b = p.PopFrame();
  ASSERT_TRUE(a && b);
  EXPECT_EQ(a->payload, "first");
  EXPECT_EQ(b->opcode, 0x9);
  EXPECT_EQ(b->payload, "second");
}

// ---------------------------------------------------------------------------
// LengthPrefixedFramer edge cases
// ---------------------------------------------------------------------------

TEST(LengthPrefixedFramerTest, SplitFramesAndClear) {
  LengthPrefixedFramer f;
  const std::string frame = LpFrame("payload");
  f.Append(reinterpret_cast<const uint8_t*>(frame.data()), 3);
  EXPECT_FALSE(f.PopFrame().has_value());
  EXPECT_EQ(f.BufferedBytes(), 3u);
  f.Append(reinterpret_cast<const uint8_t*>(frame.data() + 3), frame.size() - 3);
  auto out = f.PopFrame();
  ASSERT_TRUE(out.has_value());
  EXPECT_EQ(*out, "payload");
  EXPECT_EQ(f.BufferedBytes(), 0u);

  f.Append(reinterpret_cast<const uint8_t*>(frame.data()), frame.size());
  f.Clear();
  EXPECT_EQ(f.BufferedBytes(), 0u);
  EXPECT_FALSE(f.PopFrame().has_value());
}

TEST(ProtobufFramingTest, EncodeDecodeRoundTripAndNullOut) {
  chirp::chat::SendMessageRequest req;
  req.set_content("hello framing");
  const auto bytes = ProtobufFraming::Encode(req);
  ASSERT_EQ(bytes.size(), 4 + req.ByteSizeLong());
  // Length prefix is u32 BE.
  const uint32_t len = (static_cast<uint32_t>(bytes[0]) << 24) | (static_cast<uint32_t>(bytes[1]) << 16) |
                        (static_cast<uint32_t>(bytes[2]) << 8) | static_cast<uint32_t>(bytes[3]);
  EXPECT_EQ(len, req.ByteSizeLong());

  chirp::chat::SendMessageRequest parsed;
  ASSERT_TRUE(ProtobufFraming::Decode(std::string(bytes.begin() + 4, bytes.end()), &parsed));
  EXPECT_EQ(parsed.content(), "hello framing");

  EXPECT_FALSE(ProtobufFraming::Decode("", nullptr));
  EXPECT_FALSE(ProtobufFraming::Decode("\xff\xff\xff\xff", &parsed)); // invalid wire data
}

// ---------------------------------------------------------------------------
// RedisClient against the mock server
// ---------------------------------------------------------------------------

class RedisClientTest : public ::testing::Test {
protected:
  void SetUp() override {
    server_.Start({
        {"GET", "$5\r\nhello\r\n"},
        {"SET", "+OK\r\n"},
        {"DEL", ":2\r\n"},
        {"PUBLISH", ":1\r\n"},
        {"RPUSH", ":3\r\n"},
        {"EXPIRE", ":1\r\n"},
        {"LRANGE", "*3\r\n$3\r\nabc\r\n$3\r\ndef\r\n:9\r\n"},
        {"KEYS", "*2\r\n$2\r\nk1\r\n+ok2\r\n"},
    });
  }

  MockRedisServer server_;
  RedisClient client{"127.0.0.1", 0};
};

TEST_F(RedisClientTest, GetReturnsBulkStringValue) {
  client = RedisClient("127.0.0.1", server_.port());
  auto v = client.Get("greeting");
  ASSERT_TRUE(v.has_value());
  EXPECT_EQ(*v, "hello");
  auto cmds = server_.Commands();
  ASSERT_FALSE(cmds.empty());
  EXPECT_EQ(cmds.back(), (std::vector<std::string>{"GET", "greeting"}));
}

TEST_F(RedisClientTest, GetHandlesNullAndErrorReplies) {
  MockRedisServer null_server;
  null_server.Start({{"GET", "$-1\r\n"}});
  RedisClient c("127.0.0.1", null_server.port());
  EXPECT_FALSE(c.Get("missing").has_value());

  MockRedisServer err_server;
  err_server.Start({{"GET", "-ERR boom\r\n"}});
  RedisClient e("127.0.0.1", err_server.port());
  EXPECT_FALSE(e.Get("k").has_value());
}

TEST_F(RedisClientTest, BooleanCommands) {
  client = RedisClient("127.0.0.1", server_.port());
  EXPECT_TRUE(client.SetEx("k", "v", 60));
  EXPECT_TRUE(client.Del("k"));
  EXPECT_TRUE(client.Publish("chan", "msg"));
  EXPECT_TRUE(client.RPush("list", "item"));
  EXPECT_TRUE(client.Expire("k", 30));

  MockRedisServer wrong_types;
  wrong_types.Start({
      {"SET", "$-1\r\n"},   // not +OK
      {"DEL", "+OK\r\n"},   // not integer
      {"EXPIRE", ":0\r\n"}, // zero
  });
  RedisClient w("127.0.0.1", wrong_types.port());
  EXPECT_FALSE(w.SetEx("k", "v", 1));
  EXPECT_FALSE(w.Del("k"));
  EXPECT_FALSE(w.Expire("k", 1));
}

TEST_F(RedisClientTest, ListAndKeysCommands) {
  client = RedisClient("127.0.0.1", server_.port());
  // LRANGE mixes bulk and integer elements; only strings are returned.
  EXPECT_EQ(client.LRange("list", 0, -1), (std::vector<std::string>{"abc", "def"}));
  // KEYS mixes bulk and simple strings; both are returned.
  EXPECT_EQ(client.Keys("k*"), (std::vector<std::string>{"k1", "ok2"}));

  MockRedisServer not_array;
  not_array.Start({
      {"LRANGE", "+OK\r\n"},
      {"KEYS", "$-1\r\n"},
  });
  RedisClient n("127.0.0.1", not_array.port());
  EXPECT_TRUE(n.LRange("l", 0, -1).empty());
  EXPECT_TRUE(n.Keys("*").empty());
}

TEST_F(RedisClientTest, ConnectionFailureYieldsDefaults) {
  const uint16_t dead = FreePort(); // nothing listens here
  RedisClient c("127.0.0.1", dead);
  EXPECT_FALSE(c.Get("k").has_value());
  EXPECT_FALSE(c.SetEx("k", "v", 1));
  EXPECT_FALSE(c.Del("k"));
  EXPECT_FALSE(c.Publish("ch", "m"));
  EXPECT_FALSE(c.RPush("k", "v"));
  EXPECT_FALSE(c.Expire("k", 1));
  EXPECT_TRUE(c.LRange("k", 0, -1).empty());
  EXPECT_TRUE(c.Keys("*").empty());
}

// ---------------------------------------------------------------------------
// RedisSubscriber against the mock server
// ---------------------------------------------------------------------------

TEST(RedisSubscriberTest, SubscribeBeforeStartFails) {
  RedisSubscriber sub("127.0.0.1", FreePort());
  EXPECT_FALSE(sub.Subscribe("chan"));
  EXPECT_FALSE(sub.Unsubscribe("chan"));
  EXPECT_FALSE(sub.IsConnected());
  sub.Stop();
}

TEST(RedisSubscriberTest, LifecycleDeliversMessages) {
  MockRedisServer server;
  server.Start({});

  RedisSubscriber sub("127.0.0.1", server.port());

  std::mutex mu;
  std::condition_variable cv;
  int connects = 0;
  std::vector<std::pair<std::string, std::string>> messages;
  std::vector<std::string> errors;

  sub.SetConnectCallback([&] {
    std::lock_guard<std::mutex> l(mu);
    ++connects;
    cv.notify_all();
  });
  sub.SetErrorCallback([&](const std::string& e) {
    std::lock_guard<std::mutex> l(mu);
    errors.push_back(e);
    cv.notify_all();
  });
  sub.SetMessageCallback([&](const std::string& ch, const std::string& payload) {
    std::lock_guard<std::mutex> l(mu);
    messages.emplace_back(ch, payload);
    cv.notify_all();
  });

  sub.Start();
  {
    std::unique_lock<std::mutex> l(mu);
    cv.wait_for(l, std::chrono::seconds(3), [&] { return connects == 1; });
  }
  ASSERT_EQ(connects, 1);
  EXPECT_TRUE(sub.IsConnected());

  EXPECT_TRUE(sub.Subscribe("room1"));
  EXPECT_TRUE(WaitFor([&] {
    auto cmds = server.Commands();
    return !cmds.empty() && cmds.back() == (std::vector<std::string>{"SUBSCRIBE", "room1"});
  }));

  // Push a real "message" event (array of 3 bulk strings).
  server.PushRaw("*3\r\n$7\r\nmessage\r\n$5\r\nroom1\r\n$8\r\nhi-there\r\n");
  EXPECT_TRUE(WaitFor([&] {
    std::lock_guard<std::mutex> l(mu);
    return messages.size() == 1;
  }));
  {
    std::lock_guard<std::mutex> l(mu);
    EXPECT_EQ(messages[0].first, "room1");
    EXPECT_EQ(messages[0].second, "hi-there");
  }

  // Non-"message" arrays (subscribe confirmations) are ignored.
  server.PushRaw("*3\r\n$9\r\nsubscribe\r\n$5\r\nroom1\r\n:1\r\n");
  std::this_thread::sleep_for(std::chrono::milliseconds(150));
  {
    std::lock_guard<std::mutex> l(mu);
    EXPECT_EQ(messages.size(), 1u);
  }

  EXPECT_TRUE(sub.Unsubscribe("room1"));
  sub.Stop();
  EXPECT_FALSE(sub.IsConnected());

  // Stop is idempotent; Subscribe after stop fails again.
  sub.Stop();
  EXPECT_FALSE(sub.Subscribe("room1"));
}

TEST(RedisSubscriberTest, RestartReconnects) {
  MockRedisServer server;
  server.Start({});

  RedisSubscriber sub("127.0.0.1", server.port());
  std::atomic<int> connects{0};
  sub.SetConnectCallback([&] { connects.fetch_add(1); });

  sub.Start();
  EXPECT_TRUE(WaitFor([&] { return connects.load() == 1; }));
  sub.Stop();

  sub.Start();
  EXPECT_TRUE(WaitFor([&] { return connects.load() == 2; }));
  EXPECT_TRUE(sub.IsConnected());
  sub.Stop();
}

TEST(RedisSubscriberTest, ConnectionRefusedReportsError) {
  std::mutex mu;
  std::condition_variable cv;
  std::vector<std::string> errors;
  bool connected = false;

  RedisSubscriber sub("127.0.0.1", FreePort());
  sub.SetErrorCallback([&](const std::string& e) {
    std::lock_guard<std::mutex> l(mu);
    errors.push_back(e);
    cv.notify_all();
  });
  sub.SetConnectCallback([&] {
    std::lock_guard<std::mutex> l(mu);
    connected = true;
    cv.notify_all();
  });

  sub.Start();
  {
    std::unique_lock<std::mutex> l(mu);
    cv.wait_for(l, std::chrono::seconds(3), [&] { return !errors.empty() || connected; });
  }
  EXPECT_TRUE(!errors.empty() || connected);
  EXPECT_FALSE(sub.IsConnected());
  sub.Stop();
}

// ---------------------------------------------------------------------------
// TcpSession over a real loopback socket pair
// ---------------------------------------------------------------------------

class TcpHarness {
public:
  TcpHarness()
      : work_(asio::make_work_guard(io_)),
        acceptor_(io_, asio::ip::tcp::endpoint(asio::ip::tcp::v4(), 0)),
        client_(io_) {
    runner_ = std::thread([this] { io_.run(); });
  }

  ~TcpHarness() {
    io_.stop();
    if (runner_.joinable()) {
      runner_.join();
    }
  }

  uint16_t port() const {
    std::error_code ec;
    return static_cast<uint16_t>(acceptor_.local_endpoint(ec).port());
  }

  asio::ip::tcp::socket& client() { return client_; }

  void ConnectClient() {
    std::error_code ec;
    client_.connect(asio::ip::tcp::endpoint(asio::ip::make_address("127.0.0.1"), port()), ec);
    ASSERT_FALSE(ec) << ec.message();
  }

  void WriteAll(asio::ip::tcp::socket& s, const std::string& data) {
    std::error_code ec;
    asio::write(s, asio::buffer(data), ec);
    ASSERT_FALSE(ec) << ec.message();
  }

  std::string ReadN(asio::ip::tcp::socket& s, size_t n) {
    std::string out;
    out.resize(n);
    size_t got = 0;
    std::error_code ec;
    while (got < n) {
      const size_t r = s.read_some(asio::buffer(&out[got], n - got), ec);
      if (ec) {
        return out.substr(0, got);
      }
      got += r;
    }
    return out;
  }

  asio::io_context& io() { return io_; }

protected:
  asio::io_context io_;
  asio::executor_work_guard<asio::io_context::executor_type> work_;
  asio::ip::tcp::acceptor acceptor_;
  asio::ip::tcp::socket client_;
  std::thread runner_;
};

class TcpSessionTest : public TcpHarness, public ::testing::Test {
protected:
  std::shared_ptr<TcpSession> MakeSession(TcpSession::FrameCallback on_frame,
                                          TcpSession::CloseCallback on_close = nullptr) {
    std::promise<std::shared_ptr<TcpSession>> p;
    acceptor_.async_accept([this, &p, on_frame, on_close](std::error_code ec,
                                                          asio::ip::tcp::socket sock) {
      if (ec) {
        p.set_exception(std::make_exception_ptr(std::runtime_error(ec.message())));
        return;
      }
      auto s = std::make_shared<TcpSession>(std::move(sock), on_frame, std::move(on_close));
      s->Start();
      p.set_value(s);
    });
    ConnectClient();
    return p.get_future().get();
  }
};

TEST_F(TcpSessionTest, ReceivesLengthPrefixedFrames) {
  std::mutex mu;
  std::vector<std::string> frames;
  auto session = MakeSession([&](std::shared_ptr<Session>, std::string&& payload) {
    std::lock_guard<std::mutex> l(mu);
    frames.push_back(std::move(payload));
  });

  EXPECT_GT(session->RemoteEndpoint().port(), 0u);

  const std::string frame = LpFrame("hello tcp");
  WriteAll(client(), frame.substr(0, 3));
  std::this_thread::sleep_for(std::chrono::milliseconds(50)); // partial frame waits
  {
    std::lock_guard<std::mutex> l(mu);
    EXPECT_TRUE(frames.empty());
  }
  WriteAll(client(), frame.substr(3));
  EXPECT_TRUE(WaitFor([&] {
    std::lock_guard<std::mutex> l(mu);
    return frames.size() == 1;
  }));
  EXPECT_EQ(frames[0], "hello tcp");
  session->Close();
}

TEST_F(TcpSessionTest, SendQueuesInOrderAndSendAndClose) {
  std::atomic<int> closes{0};
  auto session = MakeSession(nullptr, [&](std::shared_ptr<Session>) { closes.fetch_add(1); });

  session->Send("one");
  session->Send("two");
  session->SendAndClose("three");

  EXPECT_EQ(ReadN(client(), 11), "onetwothree");
  // After the flush the server closes; the client sees EOF.
  std::array<char, 16> buf{};
  std::error_code ec;
  while (true) {
    const size_t n = client().read_some(asio::buffer(buf), ec);
    if (ec || n == 0) {
      break;
    }
  }
  EXPECT_TRUE(ec);
  EXPECT_TRUE(WaitFor([&] { return closes.load() == 1; }));
}

TEST_F(TcpSessionTest, ClientDisconnectTriggersCloseCallbackOnce) {
  std::atomic<int> closes{0};
  auto session = MakeSession(nullptr, [&](std::shared_ptr<Session>) { closes.fetch_add(1); });

  client().close();
  EXPECT_TRUE(WaitFor([&] { return closes.load() == 1 && session->IsClosed(); }));

  session->Close(); // idempotent
  session->Close();
  EXPECT_TRUE(WaitFor([&] { return closes.load() == 1; }));
  std::this_thread::sleep_for(std::chrono::milliseconds(100));
  EXPECT_EQ(closes.load(), 1);
}

TEST_F(TcpSessionTest, IncompleteFrameHeldUntilMoreData) {
  std::mutex mu;
  std::vector<std::string> frames;
  auto session = MakeSession([&](std::shared_ptr<Session>, std::string&& payload) {
    std::lock_guard<std::mutex> l(mu);
    frames.push_back(std::move(payload));
  });

  // Length prefix says 4 bytes but only 2 arrive.
  WriteAll(client(), std::string("\x00\x00\x00\x04"
                                 "ab",
                                 6));
  std::this_thread::sleep_for(std::chrono::milliseconds(100));
  {
    std::lock_guard<std::mutex> l(mu);
    EXPECT_TRUE(frames.empty());
  }
  WriteAll(client(), "cd");
  EXPECT_TRUE(WaitFor([&] {
    std::lock_guard<std::mutex> l(mu);
    return frames.size() == 1;
  }));
  EXPECT_EQ(frames[0], "abcd");
  session->Close();
}

// ---------------------------------------------------------------------------
// WebSocketSession over a loopback socket pair
// ---------------------------------------------------------------------------

class WsSessionTest : public TcpHarness, public ::testing::Test {
protected:
  std::shared_ptr<WebSocketSession> MakeSession(WebSocketSession::FrameCallback on_frame,
                                                WebSocketSession::CloseCallback on_close = nullptr) {
    std::promise<std::shared_ptr<WebSocketSession>> p;
    acceptor_.async_accept([this, &p, on_frame, on_close](std::error_code ec,
                                                          asio::ip::tcp::socket sock) {
      if (ec) {
        p.set_exception(std::make_exception_ptr(std::runtime_error(ec.message())));
        return;
      }
      auto s = std::make_shared<WebSocketSession>(std::move(sock), on_frame, std::move(on_close));
      s->Start();
      p.set_value(s);
    });
    ConnectClient();
    return p.get_future().get();
  }

  std::string Handshake() {
    const std::string hs =
        "GET /ws HTTP/1.1\r\n"
        "Host: localhost\r\n"
        "Upgrade: websocket\r\n"
        "Connection: Upgrade\r\n"
        "Sec-WebSocket-Key: dGhlIHNhbXBsZSBub25jZQ==\r\n"
        "Sec-WebSocket-Version: 13\r\n\r\n";
    WriteAll(client(), hs);
    std::string resp;
    while (resp.find("\r\n\r\n") == std::string::npos) {
      std::array<char, 512> buf{};
      std::error_code ec;
      const size_t n = client().read_some(asio::buffer(buf), ec);
      if (ec) {
        break;
      }
      resp.append(buf.data(), n);
    }
    return resp;
  }

  void SendWs(uint8_t opcode, const std::string& payload, bool fin = true) {
    std::string frame = BuildWebSocketFrame(opcode, payload, true);
    if (!fin) {
      frame[0] = static_cast<char>(opcode & 0x0F); // clear FIN bit
    }
    WriteAll(client(), frame);
  }
};

TEST_F(WsSessionTest, HandshakeRepliesWithRfcAccept) {
  auto session = MakeSession(nullptr);
  const std::string resp = Handshake();
  EXPECT_NE(resp.find("HTTP/1.1 101 Switching Protocols"), std::string::npos);
  EXPECT_NE(resp.find("Sec-WebSocket-Accept: s3pPLMBiTxaQ9kYGzzhZRbK+xOo="), std::string::npos);
  session->Close();
}

TEST_F(WsSessionTest, BinaryFrameDeliversLpPayload) {
  std::mutex mu;
  std::vector<std::string> frames;
  auto session = MakeSession([&](std::shared_ptr<Session>, std::string&& payload) {
    std::lock_guard<std::mutex> l(mu);
    frames.push_back(std::move(payload));
  });

  ASSERT_NO_FATAL_FAILURE(Handshake());
  SendWs(0x2, LpFrame("ws-payload"));
  EXPECT_TRUE(WaitFor([&] {
    std::lock_guard<std::mutex> l(mu);
    return frames.size() == 1;
  }));
  EXPECT_EQ(frames[0], "ws-payload");
  session->Close();
}

TEST_F(WsSessionTest, PingAnsweredWithPong) {
  auto session = MakeSession(nullptr);
  ASSERT_NO_FATAL_FAILURE(Handshake());

  SendWs(0x9, "ping-data");
  const std::string pong = ReadN(client(), 2 + 9); // header + unmasked payload
  ASSERT_GE(pong.size(), 2u);
  EXPECT_EQ(static_cast<uint8_t>(pong[0]) & 0x0F, 0xA);
  EXPECT_EQ(pong.substr(2), "ping-data");
  session->Close();
}

TEST_F(WsSessionTest, CloseFrameEchoesAndCloses) {
  std::atomic<int> closes{0};
  auto session = MakeSession(nullptr, [&](std::shared_ptr<Session>) { closes.fetch_add(1); });
  ASSERT_NO_FATAL_FAILURE(Handshake());

  SendWs(0x8, "");
  const std::string echo = ReadN(client(), 2);
  ASSERT_GE(echo.size(), 2u);
  EXPECT_EQ(static_cast<uint8_t>(echo[0]) & 0x0F, 0x8);
  EXPECT_TRUE(WaitFor([&] { return closes.load() == 1 && session->IsClosed(); }));

  // Sends after close are dropped without crashing.
  session->Send("late");
  session->SendAndClose("late2");
  std::this_thread::sleep_for(std::chrono::milliseconds(100));
}

TEST_F(WsSessionTest, WriteErrorAfterPeerCloseTriggersCloseCallback) {
  std::atomic<int> closes{0};
  auto session = MakeSession(nullptr, [&](std::shared_ptr<Session>) { closes.fetch_add(1); });
  ASSERT_NO_FATAL_FAILURE(Handshake());

  // Drop the peer abruptly (RST), then send: the queued async write fails
  // and the session must run its close path.
  std::error_code ec;
  client().set_option(asio::socket_base::linger(true, 0), ec);
  client().close(ec);

  session->Send(LpFrame("after-close"));
  EXPECT_TRUE(WaitFor([&] { return closes.load() >= 1 && session->IsClosed(); }));
}

TEST_F(WsSessionTest, FragmentedBinaryFrameCloses) {
  std::atomic<int> closes{0};
  auto session = MakeSession(nullptr, [&](std::shared_ptr<Session>) { closes.fetch_add(1); });
  ASSERT_NO_FATAL_FAILURE(Handshake());

  SendWs(0x2, "part", /*fin=*/false);
  EXPECT_TRUE(WaitFor([&] { return closes.load() == 1 && session->IsClosed(); }));
}

TEST_F(WsSessionTest, UnknownOpcodeIgnoredAndHandshakeLeftover) {
  std::mutex mu;
  std::vector<std::string> frames;
  auto session = MakeSession([&](std::shared_ptr<Session>, std::string&& payload) {
    std::lock_guard<std::mutex> l(mu);
    frames.push_back(std::move(payload));
  });

  // Send handshake WITH a binary frame piggybacked on the same write.
  std::string hs =
      "GET /ws HTTP/1.1\r\n"
      "Host: localhost\r\n"
      "Upgrade: websocket\r\n"
      "Connection: Upgrade\r\n"
      "Sec-WebSocket-Key: dGhlIHNhbXBsZSBub25jZQ==\r\n"
      "Sec-WebSocket-Version: 13\r\n\r\n";
  hs += BuildWebSocketFrame(0x1, "text-ignored", true); // text frame: ignored
  hs += BuildWebSocketFrame(0x2, LpFrame("after-text"), true);
  WriteAll(client(), hs);

  // Read the 101 response first.
  std::string resp;
  while (resp.find("\r\n\r\n") == std::string::npos) {
    std::array<char, 512> buf{};
    std::error_code ec;
    const size_t n = client().read_some(asio::buffer(buf), ec);
    if (ec) {
      break;
    }
    resp.append(buf.data(), n);
  }
  EXPECT_NE(resp.find("101"), std::string::npos);

  EXPECT_TRUE(WaitFor([&] {
    std::lock_guard<std::mutex> l(mu);
    return frames.size() == 1;
  }));
  EXPECT_EQ(frames[0], "after-text");
  EXPECT_GT(session->RemoteEndpoint().port(), 0u);
  session->Close();
}

TEST_F(WsSessionTest, SplitHandshakeStillCompletes) {
  auto session = MakeSession(nullptr);
  const std::string hs =
      "GET /ws HTTP/1.1\r\n"
      "Host: localhost\r\n"
      "Sec-WebSocket-Key: dGhlIHNhbXBsZSBub25jZQ==\r\n";
  WriteAll(client(), hs.substr(0, 20));
  std::this_thread::sleep_for(std::chrono::milliseconds(50));
  WriteAll(client(), hs.substr(20));
  std::this_thread::sleep_for(std::chrono::milliseconds(50));
  WriteAll(client(), "\r\n"); // terminator

  std::string resp;
  while (resp.find("\r\n\r\n") == std::string::npos) {
    std::array<char, 512> buf{};
    std::error_code ec;
    const size_t n = client().read_some(asio::buffer(buf), ec);
    if (ec) {
      break;
    }
    resp.append(buf.data(), n);
  }
  EXPECT_NE(resp.find("101"), std::string::npos);
  session->Close();
}

// ---------------------------------------------------------------------------
// TcpServer / TcpClient
// ---------------------------------------------------------------------------

TEST(TcpClientTest, ConnectFailsToDeadPortAndBadHost) {
  asio::io_context io;
  TcpClient client(io);
  client.SetCallbacks(nullptr, nullptr);
  EXPECT_FALSE(client.Connect("127.0.0.1", FreePort()));
  EXPECT_FALSE(client.Connect("not-an-ip-address", 80)); // make_address throws
  EXPECT_FALSE(client.IsConnected());
}

TEST(TcpServerClientTest, EndToEndFrameEcho) {
  asio::io_context io;
  auto work = asio::make_work_guard(io);
  std::thread runner([&] { io.run(); });

  const uint16_t port = FreePort();
  TcpServer server(io, port,
                   [&](std::shared_ptr<Session> s, std::string&& payload) {
                     s->Send(LpFrame("echo:" + payload)); // note: raw Send, prefix already applied
                   },
                   nullptr);
  server.Start();

  TcpClient client(io);
  std::mutex mu;
  std::vector<std::string> frames;
  client.SetCallbacks([&](std::shared_ptr<Session>, std::string&& payload) {
    std::lock_guard<std::mutex> l(mu);
    frames.push_back(std::move(payload));
  });
  ASSERT_TRUE(client.Connect("127.0.0.1", port));
  EXPECT_TRUE(client.IsConnected());

  client.GetSession()->Send(LpFrame("ping"));
  EXPECT_TRUE(WaitFor([&] {
    std::lock_guard<std::mutex> l(mu);
    return frames.size() == 1;
  }));
  EXPECT_EQ(frames[0], "echo:ping");

  client.Disconnect();
  client.Disconnect(); // idempotent
  server.Stop();
  io.stop();
  runner.join();
}

// ---------------------------------------------------------------------------
// WebSocketServer / WebSocketClient
// ---------------------------------------------------------------------------

TEST(WebSocketClientTest, ConnectFailsToDeadPortAndBadHost) {
  asio::io_context io;
  WebSocketClient client(io);
  client.SetCallbacks(nullptr, nullptr);
  EXPECT_FALSE(client.Connect("127.0.0.1", FreePort()));
  EXPECT_FALSE(client.Connect("not-an-ip-address", 80, "/ws"));
  EXPECT_FALSE(client.IsConnected());
  EXPECT_FALSE(client.GetSession() != nullptr);
}

TEST(WebSocketServerClientTest, HandshakeAndFrameRoundTrip) {
  asio::io_context io;
  auto work = asio::make_work_guard(io);
  std::thread runner([&] { io.run(); });

  const uint16_t port = FreePort();
  WebSocketServer server(io, port,
                         [](std::shared_ptr<Session> s, std::string&& payload) {
                           s->Send(LpFrame(payload)); // must re-frame: LP prefix already stripped
                         },
                         nullptr);
  server.Start();

  WebSocketClient client(io);
  std::mutex mu;
  std::vector<std::string> frames;
  std::atomic<int> closes{0};
  client.SetCallbacks(
      [&](std::shared_ptr<Session>, std::string&& payload) {
        std::lock_guard<std::mutex> l(mu);
        frames.push_back(std::move(payload));
      },
      [&](std::shared_ptr<Session>) { closes.fetch_add(1); });

  ASSERT_TRUE(client.Connect("127.0.0.1", port, "/ws"));
  EXPECT_TRUE(client.IsConnected());

  client.GetSession()->Send(LpFrame("hello-ws"));
  ASSERT_TRUE(WaitFor([&] {
    std::lock_guard<std::mutex> l(mu);
    return frames.size() == 1;
  }));
  EXPECT_EQ(frames[0], "hello-ws");

  client.Disconnect();
  server.Stop();
  io.stop();
  runner.join();
}

// ---------------------------------------------------------------------------
// MessageRouter (redis pub/sub routing)
// ---------------------------------------------------------------------------

TEST(MessageRouterTest, ChannelHelpers) {
  EXPECT_EQ(RouterChannels::UserChat("u1"), "chirp:chat:user:u1");
  EXPECT_EQ(RouterChannels::GroupChat("g1"), "chirp:chat:group:g1");
  EXPECT_EQ(RouterChannels::UserSocial("u1"), "chirp:social:user:u1");
  EXPECT_EQ(RouterChannels::UserPresence("u1"), "chirp:presence:user:u1");
  EXPECT_EQ(RouterChannels::KickNotification("i1"), "chirp:kick:instance:i1");
  EXPECT_EQ(RouterChannels::ServiceRegister("chat", "i1"), "chirp:service:chat:i1");
}

TEST(MessageRouterTest, SubscriptionsPublishAndRouting) {
  MockRedisServer server;
  server.Start({{"PUBLISH", ":1\r\n"}});

  asio::io_context io;
  auto work = asio::make_work_guard(io);
  std::thread runner([&] { io.run(); });

  MessageRouter router(io, "127.0.0.1", server.port());
  EXPECT_EQ(router.RedisHost(), "127.0.0.1");
  EXPECT_EQ(router.RedisPort(), server.port());

  // Subscribing before the subscriber is connected fails but still registers.
  EXPECT_FALSE(router.SubscribeUserChat("alice", nullptr));

  ASSERT_TRUE(router.Start());

  // Once connected, re-subscription from the connect callback runs and new
  // subscriptions succeed.
  EXPECT_TRUE(WaitFor([&] {
    for (const auto& c : server.Commands()) {
      if (c.size() == 2 && c[0] == "SUBSCRIBE" && c[1] == "chirp:chat:user:alice") {
        return true;
      }
    }
    return false;
  }));

  std::atomic<int> got_msg{0};
  EXPECT_TRUE(router.SubscribeUserChat("alice", [&](const std::string& m) {
    if (m == "routed") {
      got_msg.fetch_add(1);
    }
  }));
  EXPECT_TRUE(router.SubscribeGroupChat("g1", [](const std::string&) {}));
  EXPECT_TRUE(router.SubscribeUserSocial("alice", [](const std::string&) {}));
  EXPECT_TRUE(router.SubscribeKickNotification("inst1", [](const std::string&) {}));

  // Routing: a pub/sub "message" event for a subscribed channel reaches the cb
  // (dispatched through the router's io_context).
  server.PushRaw("*3\r\n$7\r\nmessage\r\n$21\r\nchirp:chat:user:alice\r\n$6\r\nrouted\r\n");
  EXPECT_TRUE(WaitFor([&] { return got_msg.load() == 1; }));

  // Publish goes through RedisClient and reports the integer reply.
  EXPECT_TRUE(router.Publish("some:channel", "payload"));
  EXPECT_TRUE(WaitFor([&] {
    for (const auto& c : server.Commands()) {
      if (c.size() == 3 && c[0] == "PUBLISH" && c[1] == "some:channel") {
        return true;
      }
    }
    return false;
  }));

  // Local-first routing: local hit short-circuits; local miss falls back to
  // redis publish (which the mock answers with :1 -> true).
  EXPECT_TRUE(router.SendChatMessage("bob", "m", [](const std::string& uid) { return uid == "bob"; }));
  EXPECT_TRUE(router.SendChatMessage("bob", "m", [](const std::string&) { return false; }));
  EXPECT_TRUE(router.BroadcastToGroup("g1", "group-msg"));

  // Unsubscribe sends UNSUBSCRIBE to redis.
  router.Unsubscribe("chirp:chat:user:alice");
  EXPECT_TRUE(WaitFor([&] {
    for (const auto& c : server.Commands()) {
      if (c.size() == 2 && c[0] == "UNSUBSCRIBE" && c[1] == "chirp:chat:user:alice") {
        return true;
      }
    }
    return false;
  }));

  router.Stop();
  router.Stop(); // idempotent

  io.stop();
  runner.join();
}

TEST(MessageRouterTest, PublishWithoutRedisFails) {
  asio::io_context io;
  const uint16_t dead = FreePort();
  MessageRouter router(io, "127.0.0.1", dead);
  ASSERT_TRUE(router.Start());
  EXPECT_FALSE(router.Publish("chan", "m"));
  EXPECT_FALSE(router.BroadcastToGroup("g", "m"));
  EXPECT_FALSE(router.SendChatMessage("u", "m", nullptr));
  router.Stop();
}

// ---------------------------------------------------------------------------
// Edge-path coverage: abrupt server drops and malformed subscriber input.
// ---------------------------------------------------------------------------

TEST_F(RedisClientTest, ServerCloseMidCommandYieldsNothing) {
  MockRedisServer server;
  server.Start({}); // no replies: client blocks reading the response

  RedisClient c("127.0.0.1", server.port());
  std::atomic<bool> done{false};
  std::thread reader([&] {
    EXPECT_FALSE(c.Get("k").has_value());
    done.store(true);
  });
  // Wait until the command is in flight, then drop the connection so the
  // blocking read fails with an error.
  for (int i = 0; i < 500 && !done.load(); ++i) {
    if (!server.Commands().empty()) {
      break;
    }
    std::this_thread::sleep_for(std::chrono::milliseconds(2));
  }
  server.CloseConnections();
  reader.join();
  EXPECT_TRUE(done.load());
}

TEST(RedisSubscriberTest, MalformedPushesAreIgnoredAndRecoveryWorks) {
  MockRedisServer server;
  server.Start({});

  RedisSubscriber sub("127.0.0.1", server.port());
  std::atomic<int> connects{0};
  std::mutex mu;
  std::vector<std::pair<std::string, std::string>> messages;
  sub.SetConnectCallback([&] { connects.fetch_add(1); });
  sub.SetMessageCallback([&](const std::string& ch, const std::string& payload) {
    std::lock_guard<std::mutex> l(mu);
    messages.emplace_back(ch, payload);
  });

  sub.Start();
  EXPECT_TRUE(WaitFor([&] { return connects.load() == 1; }));
  ASSERT_TRUE(sub.Subscribe("room1"));

  // Empty line, bad bulk length and null bulk string: none of these may
  // crash or misalign the parser.
  server.PushRaw("\r\n");
  server.PushRaw("*1\r\n$x\r\n");
  server.PushRaw("*1\r\n$-1\r\n");
  std::this_thread::sleep_for(std::chrono::milliseconds(200));
  {
    std::lock_guard<std::mutex> l(mu);
    EXPECT_TRUE(messages.empty());
  }

  // A well-formed push after the garbage is still delivered.
  server.PushRaw("*3\r\n$7\r\nmessage\r\n$5\r\nroom1\r\n$2\r\nok\r\n");
  EXPECT_TRUE(WaitFor([&] {
    std::lock_guard<std::mutex> l(mu);
    return messages.size() == 1;
  }));
  {
    std::lock_guard<std::mutex> l(mu);
    EXPECT_EQ(messages[0].first, "room1");
    EXPECT_EQ(messages[0].second, "ok");
  }

  // Truncated array and a dangling line without CRLF leave the parser
  // waiting for more bytes (no callback, no crash). The declared bulk
  // length is longer than the delivered data: the read stays incomplete.
  server.PushRaw("*2\r\n$7\r\nmessage\r\n$50\r\nshort");
  server.PushRaw("partial-without-crln");
  std::this_thread::sleep_for(std::chrono::milliseconds(200));
  sub.Stop();
}

TEST(RedisSubscriberTest, ResetConnectionReportsReadError) {
  // Accepts connections but never reads: closing a socket whose receive
  // buffer still holds unread bytes makes the kernel send a reset (not a
  // plain EOF), which must surface as a read error at the subscriber.
  class NoReadServer {
   public:
    NoReadServer()
        : acceptor_(io_, asio::ip::tcp::endpoint(asio::ip::tcp::v4(), 0)) {
      port_ = static_cast<uint16_t>(acceptor_.local_endpoint().port());
      DoAccept();
      thread_ = std::thread([this] { io_.run(); });
    }
    ~NoReadServer() {
      io_.stop();
      if (thread_.joinable()) {
        thread_.join();
      }
    }
    uint16_t port() const { return port_; }
    void CloseAll() {
      asio::post(io_, [this] {
        std::lock_guard<std::mutex> lock(mu_);
        for (auto& s : sockets_) {
          asio::error_code ec;
          s->close(ec);
        }
      });
    }

   private:
    void DoAccept() {
      auto sock = std::make_shared<asio::ip::tcp::socket>(io_);
      acceptor_.async_accept(*sock, [this, sock](const std::error_code& ec) {
        if (!ec) {
          std::lock_guard<std::mutex> lock(mu_);
          sockets_.push_back(sock);
        }
        DoAccept();
      });
    }
    asio::io_context io_;
    asio::ip::tcp::acceptor acceptor_;
    uint16_t port_{0};
    std::mutex mu_;
    std::vector<std::shared_ptr<asio::ip::tcp::socket>> sockets_;
    std::thread thread_;
  };

  NoReadServer server;

  RedisSubscriber sub("127.0.0.1", server.port());
  std::atomic<int> connects{0};
  std::mutex mu;
  std::vector<std::string> errors;
  sub.SetConnectCallback([&] { connects.fetch_add(1); });
  sub.SetErrorCallback([&](const std::string& e) {
    std::lock_guard<std::mutex> l(mu);
    errors.push_back(e);
  });

  sub.Start();
  EXPECT_TRUE(WaitFor([&] { return connects.load() == 1; }));

  // The SUBSCRIBE command lands in the server's receive buffer; closing
  // the socket with pending unread data triggers a TCP reset.
  ASSERT_TRUE(sub.Subscribe("room1"));
  std::this_thread::sleep_for(std::chrono::milliseconds(300));
  server.CloseAll();

  EXPECT_TRUE(WaitFor([&] {
    std::lock_guard<std::mutex> l(mu);
    return !errors.empty();
  }));
  sub.Stop();
}

TEST(WebSocketClientTest, HandshakeRejectedByNonUpgradeResponse) {
  MockRedisServer server; // plain TCP acceptor is enough
  server.SetPushOnConnect("HTTP/1.1 200 OK\r\nContent-Length: 0\r\n\r\n");
  server.Start({});

  asio::io_context io;
  WebSocketClient client(io);
  EXPECT_FALSE(client.Connect("127.0.0.1", server.port(), "/ws"));
  EXPECT_FALSE(client.IsConnected());
}

TEST(WebSocketClientTest, HandshakeAgainstClosedConnectionFails) {
  MockRedisServer server;
  server.Start({});

  asio::io_context io;
  WebSocketClient client(io);
  // Connect then drop the server side immediately: either the handshake
  // write or the response read fails, both must return false.
  std::thread connector([&] {
    for (int i = 0; i < 50; ++i) {
      if (server.ConnectionCount() > 0) {
        break;
      }
      std::this_thread::sleep_for(std::chrono::milliseconds(2));
    }
    server.ResetConnections();
  });
  EXPECT_FALSE(client.Connect("127.0.0.1", server.port(), "/ws"));
  connector.join();
  EXPECT_FALSE(client.IsConnected());
}

} // namespace
} // namespace chirp::network
