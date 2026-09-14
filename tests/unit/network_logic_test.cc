#include <gtest/gtest.h>

#include <atomic>
#include <chrono>
#include <cstdint>
#include <cstring>
#include <limits>
#include <future>
#include <stdexcept>
#include <string>
#include <vector>

#include "message_router.h"
#include "network/protobuf_framing.h"
#include "network/redis_client.h"
#include "network/redis_protocol.h"
#include "network/tcp_server.h"
#include "network/websocket_frame.h"
#include "network/websocket_server.h"
#include "network/websocket_util.h"

#include "fake_servers.h"
#include "proto/gateway.pb.h"

using chirp::network::BuildRedisCommand;
using chirp::network::BuildWebSocketFrame;
using chirp::network::ComputeWebSocketAccept;
using chirp::network::IStartsWith;
using chirp::network::MessageRouter;
using chirp::network::ProtobufFraming;
using chirp::network::RedisClient;
using chirp::network::RedisResp;
using chirp::network::RedisRespParser;
using chirp::network::RedisSubscriber;
using chirp::network::RouterChannels;
using chirp_test::Bulk;
using chirp_test::Int;
using chirp_test::Simple;
using chirp_test::Array;
using chirp::network::TcpServer;
using chirp::network::TrimAsciiWhitespace;
using chirp::network::WebSocketFrameParser;
using chirp::network::WebSocketServer;

namespace {

// A port that is virtually guaranteed to refuse connections instantly
// (127.0.0.1 resolves locally, port 1 has no listener -> ECONNREFUSED).
constexpr const char* kRefusedHost = "127.0.0.1";
constexpr uint16_t kRefusedPort = 1;

void AppendStr(WebSocketFrameParser* parser, const std::string& s) {
  parser->Append(reinterpret_cast<const uint8_t*>(s.data()), s.size());
}

void AppendStr(RedisRespParser* parser, const std::string& s) {
  parser->Append(reinterpret_cast<const uint8_t*>(s.data()), s.size());
}

// ---------------------------------------------------------------------------
// websocket_frame.cc
// ---------------------------------------------------------------------------

class WebSocketFrameTest : public ::testing::Test {};

TEST_F(WebSocketFrameTest, RoundTripSmallPayloadUnmasked) {
  std::string frame = BuildWebSocketFrame(0x1, "hello", /*mask=*/false);
  WebSocketFrameParser parser;
  AppendStr(&parser, frame);

  auto f = parser.PopFrame();
  ASSERT_TRUE(f.has_value());
  EXPECT_TRUE(f->fin);
  EXPECT_EQ(f->opcode, 0x1);
  EXPECT_EQ(f->payload, "hello");
  EXPECT_FALSE(parser.PopFrame().has_value());  // fully consumed
}

TEST_F(WebSocketFrameTest, RoundTripMasked) {
  std::string frame = BuildWebSocketFrame(0x2, "masked payload", /*mask=*/true);
  WebSocketFrameParser parser;
  AppendStr(&parser, frame);

  auto f = parser.PopFrame();
  ASSERT_TRUE(f.has_value());
  EXPECT_EQ(f->opcode, 0x2);
  EXPECT_EQ(f->payload, "masked payload");  // mask removed by parser
}

TEST_F(WebSocketFrameTest, LengthFormatsChosenCorrectly) {
  // <=125 -> 7-bit inline length
  EXPECT_EQ(BuildWebSocketFrame(0x1, std::string(125, 'a'), false).size(), 2u + 125u);
  // 126..65535 -> 16-bit extended length
  EXPECT_EQ(BuildWebSocketFrame(0x1, std::string(126, 'a'), false).size(), 4u + 126u);
  EXPECT_EQ(BuildWebSocketFrame(0x1, std::string(300, 'a'), false).size(), 4u + 300u);
  EXPECT_EQ(BuildWebSocketFrame(0x1, std::string(65535, 'a'), false).size(), 4u + 65535u);
  // >65535 -> 64-bit extended length
  EXPECT_EQ(BuildWebSocketFrame(0x1, std::string(65536, 'a'), false).size(), 10u + 65536u);
}

TEST_F(WebSocketFrameTest, ParsesLargeFrame) {
  const std::string payload(70000, 'x');
  WebSocketFrameParser parser;
  AppendStr(&parser, BuildWebSocketFrame(0x2, payload, false));

  auto f = parser.PopFrame();
  ASSERT_TRUE(f.has_value());
  EXPECT_EQ(f->payload.size(), payload.size());
  EXPECT_EQ(f->payload, payload);
}

TEST_F(WebSocketFrameTest, FragmentedInputIsBuffered) {
  const std::string frame = BuildWebSocketFrame(0x1, "fragment me", true);

  WebSocketFrameParser parser;
  AppendStr(&parser, frame.substr(0, 3));
  EXPECT_FALSE(parser.PopFrame().has_value());
  AppendStr(&parser, frame.substr(3, 5));
  EXPECT_FALSE(parser.PopFrame().has_value());
  AppendStr(&parser, frame.substr(8));

  auto f = parser.PopFrame();
  ASSERT_TRUE(f.has_value());
  EXPECT_EQ(f->payload, "fragment me");
}

TEST_F(WebSocketFrameTest, IncompleteVariantsReturnNullopt) {
  WebSocketFrameParser parser;

  // Fewer than 2 header bytes
  AppendStr(&parser, std::string("\x81", 1));
  EXPECT_FALSE(parser.PopFrame().has_value());

  WebSocketFrameParser p2;
  // 16-bit length declared but missing
  AppendStr(&p2, std::string("\x81\xfe\x00\x05", 4));
  EXPECT_FALSE(p2.PopFrame().has_value());

  WebSocketFrameParser p3;
  // 64-bit length header declared but missing
  AppendStr(&p3, std::string("\x81\xff\x00\x00\x00\x00", 6));
  EXPECT_FALSE(p3.PopFrame().has_value());

  WebSocketFrameParser p4;
  // Mask bit set but mask key missing
  AppendStr(&p4, std::string("\x81\x81", 2));  // len 1 + mask
  EXPECT_FALSE(p4.PopFrame().has_value());

  WebSocketFrameParser p5;
  // Payload partially delivered
  AppendStr(&p5, std::string("\x81\x83ab", 3));  // len 3, got 2
  EXPECT_FALSE(p5.PopFrame().has_value());
}

TEST_F(WebSocketFrameTest, OversizedDeclaredPayloadDropsBuffer) {
  WebSocketFrameParser parser;
  // Header declaring a 17MB payload (over the 16MB safety limit)
  std::string header;
  header.push_back(static_cast<char>(0x82));
  header.push_back(static_cast<char>(0xFF));
  for (int i = 0; i < 8; ++i) {
    header.push_back(static_cast<char>((i == 2) ? 0x01 : 0x00));  // 0x1000000
  }
  header += "garbage";
  AppendStr(&parser, header);

  EXPECT_FALSE(parser.PopFrame().has_value());  // dropped
  // Buffer was cleared, parser is usable again
  AppendStr(&parser, BuildWebSocketFrame(0x1, "ok", false));
  auto f = parser.PopFrame();
  ASSERT_TRUE(f.has_value());
  EXPECT_EQ(f->payload, "ok");
}

TEST_F(WebSocketFrameTest, MultipleFramesInOneBuffer) {
  WebSocketFrameParser parser;
  AppendStr(&parser, BuildWebSocketFrame(0x1, "one", false));
  AppendStr(&parser, BuildWebSocketFrame(0x2, "two", true));

  auto f1 = parser.PopFrame();
  ASSERT_TRUE(f1.has_value());
  EXPECT_EQ(f1->payload, "one");
  auto f2 = parser.PopFrame();
  ASSERT_TRUE(f2.has_value());
  EXPECT_EQ(f2->payload, "two");
  EXPECT_EQ(f2->opcode, 0x2);
}

TEST_F(WebSocketFrameTest, ClearResetsState) {
  WebSocketFrameParser parser;
  AppendStr(&parser, "\x81\x83xyz");  // incomplete
  parser.Clear();
  EXPECT_FALSE(parser.PopFrame().has_value());
}

// ---------------------------------------------------------------------------
// websocket_util.cc
// ---------------------------------------------------------------------------

class WebSocketUtilTest : public ::testing::Test {};

TEST_F(WebSocketUtilTest, AcceptKeyMatchesReference) {
  // Expected values cross-checked with Python hashlib and OpenSSL:
  // b64(sha1(key + "258EAFA5-E914-47DA-95CA-C5AB0DC85B11"))
  EXPECT_EQ(ComputeWebSocketAccept("dGhlIHNhbXBsZSBub25jZQ=="),
            "s3pPLMBiTxaQ9kYGzzhZRbK+xOo=");
  EXPECT_EQ(ComputeWebSocketAccept("x3JJHMbDL1EzLkh9GBhXDw=="),
            "HSmrc0sMlYUkAGmm5OPpG2HaGWk=");
  // Same key -> same accept (deterministic)
  EXPECT_EQ(ComputeWebSocketAccept("abc"), ComputeWebSocketAccept("abc"));
  EXPECT_NE(ComputeWebSocketAccept("abc"), ComputeWebSocketAccept("abd"));
}

TEST_F(WebSocketUtilTest, IStartsWithIsCaseInsensitive) {
  EXPECT_TRUE(IStartsWith("Upgrade: websocket", "upgrade"));
  EXPECT_TRUE(IStartsWith("UPGRADE", "upgrade"));
  EXPECT_TRUE(IStartsWith("upgrade", "UPGRADE"));
  EXPECT_TRUE(IStartsWith("anything", ""));
  EXPECT_FALSE(IStartsWith("up", "upgrade"));
  EXPECT_FALSE(IStartsWith("downgrade", "upgrade"));
  EXPECT_TRUE(IStartsWith("Sec-WebSocket-Key: abc", "sec-websocket-key"));
}

TEST_F(WebSocketUtilTest, TrimAsciiWhitespace) {
  EXPECT_EQ(TrimAsciiWhitespace("  hello  "), "hello");
  EXPECT_EQ(TrimAsciiWhitespace("\t\r\nvalue \t"), "value");
  EXPECT_EQ(TrimAsciiWhitespace("no-trim"), "no-trim");
  EXPECT_EQ(TrimAsciiWhitespace("   "), "");
  EXPECT_EQ(TrimAsciiWhitespace(""), "");
  EXPECT_EQ(TrimAsciiWhitespace("\r\n"), "");
}

// ---------------------------------------------------------------------------
// redis_protocol.cc
// ---------------------------------------------------------------------------

class RedisProtocolTest : public ::testing::Test {};

TEST_F(RedisProtocolTest, BuildRedisCommandFormat) {
  EXPECT_EQ(BuildRedisCommand({"GET", "key"}),
            "*2\r\n$3\r\nGET\r\n$3\r\nkey\r\n");
  EXPECT_EQ(BuildRedisCommand({}),
            "*0\r\n");
  EXPECT_EQ(BuildRedisCommand({"PING"}),
            "*1\r\n$4\r\nPING\r\n");
}

TEST_F(RedisProtocolTest, ParseSimpleString) {
  RedisRespParser p;
  AppendStr(&p, "+OK\r\n");
  auto r = p.Pop();
  ASSERT_TRUE(r.has_value());
  EXPECT_EQ(r->type, RedisResp::Type::kSimpleString);
  EXPECT_EQ(r->str, "OK");
  EXPECT_FALSE(p.Pop().has_value());
}

TEST_F(RedisProtocolTest, ParseError) {
  RedisRespParser p;
  AppendStr(&p, "-ERR unknown command\r\n");
  auto r = p.Pop();
  ASSERT_TRUE(r.has_value());
  EXPECT_EQ(r->type, RedisResp::Type::kError);
  EXPECT_EQ(r->str, "ERR unknown command");
}

TEST_F(RedisProtocolTest, ParseInteger) {
  RedisRespParser p;
  AppendStr(&p, ":1234\r\n: -7\r\n");
  auto r1 = p.Pop();
  ASSERT_TRUE(r1.has_value());
  EXPECT_EQ(r1->type, RedisResp::Type::kInteger);
  EXPECT_EQ(r1->integer, 1234);

  auto r2 = p.Pop();
  ASSERT_TRUE(r2.has_value());
  EXPECT_EQ(r2->integer, -7);
}

TEST_F(RedisProtocolTest, ParseBulkString) {
  RedisRespParser p;
  AppendStr(&p, "$5\r\nhello\r\n");
  auto r = p.Pop();
  ASSERT_TRUE(r.has_value());
  EXPECT_EQ(r->type, RedisResp::Type::kBulkString);
  EXPECT_EQ(r->str, "hello");
}

TEST_F(RedisProtocolTest, ParseNullBulkString) {
  RedisRespParser p;
  AppendStr(&p, "$-1\r\n");
  auto r = p.Pop();
  ASSERT_TRUE(r.has_value());
  EXPECT_EQ(r->type, RedisResp::Type::kNull);
}

TEST_F(RedisProtocolTest, ParseEmptyBulkString) {
  RedisRespParser p;
  AppendStr(&p, "$0\r\n\r\n");
  auto r = p.Pop();
  ASSERT_TRUE(r.has_value());
  EXPECT_EQ(r->type, RedisResp::Type::kBulkString);
  EXPECT_EQ(r->str, "");
}

TEST_F(RedisProtocolTest, ParseNullArray) {
  RedisRespParser p;
  AppendStr(&p, "*-1\r\n");
  auto r = p.Pop();
  ASSERT_TRUE(r.has_value());
  EXPECT_EQ(r->type, RedisResp::Type::kNull);
}

TEST_F(RedisProtocolTest, ParseArray) {
  RedisRespParser p;
  // A pub/sub message array: ["message", "chan", "payload"]
  AppendStr(&p, "*3\r\n$7\r\nmessage\r\n$4\r\nchan\r\n$7\r\npayload\r\n");
  auto r = p.Pop();
  ASSERT_TRUE(r.has_value());
  ASSERT_EQ(r->type, RedisResp::Type::kArray);
  ASSERT_EQ(r->array.size(), 3u);
  EXPECT_EQ(r->array[0].str, "message");
  EXPECT_EQ(r->array[1].str, "chan");
  EXPECT_EQ(r->array[2].str, "payload");
}

TEST_F(RedisProtocolTest, ParseNestedArray) {
  RedisRespParser p;
  AppendStr(&p, "*2\r\n*2\r\n$1\r\na\r\n$1\r\nb\r\n:42\r\n");
  auto r = p.Pop();
  ASSERT_TRUE(r.has_value());
  ASSERT_EQ(r->array.size(), 2u);
  ASSERT_EQ(r->array[0].type, RedisResp::Type::kArray);
  EXPECT_EQ(r->array[0].array[0].str, "a");
  EXPECT_EQ(r->array[0].array[1].str, "b");
  EXPECT_EQ(r->array[1].integer, 42);
}

TEST_F(RedisProtocolTest, FragmentedAndIncompleteInputs) {
  RedisRespParser p;
  const std::string full = "$5\r\nhello\r\n";
  AppendStr(&p, full.substr(0, 4));
  EXPECT_FALSE(p.Pop().has_value());
  AppendStr(&p, full.substr(4));
  auto r = p.Pop();
  ASSERT_TRUE(r.has_value());
  EXPECT_EQ(r->str, "hello");

  // Missing trailing \r\n of bulk payload
  RedisRespParser p2;
  AppendStr(&p2, "$5\r\nhell");
  EXPECT_FALSE(p2.Pop().has_value());

  // Incomplete line (no \r\n at all)
  RedisRespParser p3;
  AppendStr(&p3, "+OK");
  EXPECT_FALSE(p3.Pop().has_value());

  // Empty buffer
  RedisRespParser p4;
  EXPECT_FALSE(p4.Pop().has_value());
}

TEST_F(RedisProtocolTest, UnknownTypeByteReturnsNullopt) {
  RedisRespParser p;
  AppendStr(&p, "?wat\r\n");
  EXPECT_FALSE(p.Pop().has_value());
}

TEST_F(RedisProtocolTest, RoundTripWithBuildCommand) {
  RedisRespParser p;
  AppendStr(&p, BuildRedisCommand({"SUBSCRIBE", "chan"}));

  // SUBSCRIBE ack from server is an array; the request itself is an array of
  // bulk strings which the parser handles as an array.
  auto r = p.Pop();
  ASSERT_TRUE(r.has_value());
  ASSERT_EQ(r->type, RedisResp::Type::kArray);
  ASSERT_EQ(r->array.size(), 2u);
  EXPECT_EQ(r->array[0].str, "SUBSCRIBE");
  EXPECT_EQ(r->array[1].str, "chan");
}

// ---------------------------------------------------------------------------
// protobuf_framing.cc remaining branches
// ---------------------------------------------------------------------------

TEST(ProtobufFramingTest, DecodeNullOutReturnsFalse) {
  chirp::gateway::Packet pkt;
  pkt.set_sequence(5);
  std::string bytes(reinterpret_cast<const char*>(
                        ProtobufFraming::Encode(pkt).data()),
                    ProtobufFraming::Encode(pkt).size());
  EXPECT_FALSE(ProtobufFraming::Decode(bytes, nullptr));
}

TEST(ProtobufFramingTest, EncodeDecodeRoundTrip) {
  chirp::gateway::Packet pkt;
  pkt.set_msg_id(chirp::gateway::LOGIN_REQ);
  pkt.set_sequence(17);
  pkt.set_body("abc");

  const auto framed = ProtobufFraming::Encode(pkt);
  const std::string bytes(reinterpret_cast<const char*>(framed.data()), framed.size());

  // Decode expects the bare message: strip the 4-byte length prefix
  chirp::gateway::Packet out;
  ASSERT_GE(bytes.size(), 4u);
  const uint32_t len = (static_cast<uint32_t>(static_cast<uint8_t>(bytes[0])) << 24) |
                       (static_cast<uint32_t>(static_cast<uint8_t>(bytes[1])) << 16) |
                       (static_cast<uint32_t>(static_cast<uint8_t>(bytes[2])) << 8) |
                       static_cast<uint32_t>(static_cast<uint8_t>(bytes[3]));
  ASSERT_EQ(bytes.size(), 4u + len);
  ASSERT_TRUE(out.ParseFromString(bytes.substr(4)));
  EXPECT_EQ(out.msg_id(), chirp::gateway::LOGIN_REQ);
  EXPECT_EQ(out.sequence(), 17);
  EXPECT_EQ(out.body(), "abc");
}

// ---------------------------------------------------------------------------
// redis_client.cc / message_router.cc failure paths (connection refused)
// ---------------------------------------------------------------------------

class RedisClientFailureTest : public ::testing::Test {
protected:
  RedisClient client_{kRefusedHost, kRefusedPort};
};

TEST_F(RedisClientFailureTest, GetFailsFast) {
  auto v = client_.Get("key");
  EXPECT_FALSE(v.has_value());
}

TEST_F(RedisClientFailureTest, SetExFails) {
  EXPECT_FALSE(client_.SetEx("k", "v", 60));
}

TEST_F(RedisClientFailureTest, DelFails) {
  EXPECT_FALSE(client_.Del("k"));
}

TEST_F(RedisClientFailureTest, PublishFails) {
  EXPECT_FALSE(client_.Publish("chan", "msg"));
}

TEST_F(RedisClientFailureTest, RPushFails) {
  EXPECT_FALSE(client_.RPush("list", "v"));
}

TEST_F(RedisClientFailureTest, ExpireFails) {
  EXPECT_FALSE(client_.Expire("k", 30));
}

TEST_F(RedisClientFailureTest, LRangeFailsEmpty) {
  EXPECT_TRUE(client_.LRange("k", 0, -1).empty());
}

TEST_F(RedisClientFailureTest, KeysFailsEmpty) {
  EXPECT_TRUE(client_.Keys("*").empty());
}

class RedisSubscriberTest : public ::testing::Test {};

TEST_F(RedisSubscriberTest, SubscribeWithoutConnectionFails) {
  RedisSubscriber sub(kRefusedHost, kRefusedPort);
  EXPECT_FALSE(sub.Subscribe("chan"));
  EXPECT_FALSE(sub.Unsubscribe("chan"));
  EXPECT_FALSE(sub.IsConnected());
}

TEST_F(RedisSubscriberTest, StartConnectFailureReportsErrorAndStops) {
  RedisSubscriber sub(kRefusedHost, kRefusedPort);

  std::promise<std::string> promise;
  auto future = promise.get_future();
  sub.SetErrorCallback([&promise](const std::string& err) {
    promise.set_value(err);
  });
  bool connect_cb_called = false;
  sub.SetConnectCallback([&connect_cb_called] { connect_cb_called = true; });

  sub.Start();
  EXPECT_EQ(future.wait_for(std::chrono::milliseconds(5000)),
            std::future_status::ready);  // refused -> error callback fires
  EXPECT_FALSE(future.get().empty());
  EXPECT_FALSE(connect_cb_called);
  EXPECT_FALSE(sub.IsConnected());

  sub.Stop();  // must join the finished thread cleanly
  sub.Stop();  // idempotent
}

TEST_F(RedisSubscriberTest, StartStopWithoutCallbacksIsSafe) {
  RedisSubscriber sub(kRefusedHost, kRefusedPort);
  sub.Start();
  // Give the connection attempt a moment, then stop; no callbacks installed.
  std::this_thread::sleep_for(std::chrono::milliseconds(200));
  sub.Stop();
}

class MessageRouterTest : public ::testing::Test {
protected:
  asio::io_context io_;
  MessageRouter router_{io_, kRefusedHost, kRefusedPort};
};

TEST_F(MessageRouterTest, AccessorsExposeRedisEndpoint) {
  EXPECT_EQ(router_.RedisHost(), kRefusedHost);
  EXPECT_EQ(router_.RedisPort(), kRefusedPort);
}

TEST_F(MessageRouterTest, StartStopLifecycle) {
  EXPECT_TRUE(router_.Start());
  router_.Stop();
  router_.Stop();  // idempotent
}

TEST_F(MessageRouterTest, SubscribeRecordsChannelsAndSurvivesStop) {
  EXPECT_TRUE(router_.Start());  // subscriber thread fails fast on connect

  bool called = false;
  // Returns false because the connection is down, but the subscription is
  // registered internally.
  router_.SubscribeUserChat("alice", [&called](const std::string&) { called = true; });
  router_.SubscribeGroupChat("g1", [](const std::string&) {});
  router_.SubscribeUserSocial("bob", [](const std::string&) {});
  router_.SubscribeKickNotification("inst1", [](const std::string&) {});

  router_.Unsubscribe(RouterChannels::UserChat("alice"));
  router_.Stop();

  EXPECT_FALSE(called);
}

TEST_F(MessageRouterTest, PublishFailsWithoutRedis) {
  EXPECT_FALSE(router_.Publish("chan", "msg"));
}

TEST_F(MessageRouterTest, SendChatMessagePrefersLocalDelivery) {
  bool local_called = false;
  EXPECT_TRUE(router_.SendChatMessage(
      "alice", "hi", [&](const std::string& user) {
        local_called = true;
        EXPECT_EQ(user, "alice");
        return true;
      }));
  EXPECT_TRUE(local_called);
}

TEST_F(MessageRouterTest, SendChatMessageFallsBackToRedis) {
  // Local delivery declines -> tries Redis publish -> fails (no Redis)
  EXPECT_FALSE(router_.SendChatMessage(
      "alice", "hi", [](const std::string&) { return false; }));
}

TEST_F(MessageRouterTest, SendChatMessageWithoutLocalCallback) {
  EXPECT_FALSE(router_.SendChatMessage("alice", "hi", nullptr));
}

class CountingPublisher : public RedisClient {
 public:
  CountingPublisher() : RedisClient("127.0.0.1", 1) {}
  int64_t PublishCount(const std::string&, const std::string&) override {
    return next_count;
  }
  int64_t next_count = 0;
};

TEST_F(MessageRouterTest, SendChatMessageCountWithoutRedisIsNegative) {
  // No backend at all: neither local nor remote delivery happened.
  EXPECT_EQ(router_.SendChatMessageCount("alice", "hi",
                                         [](const std::string&) { return false; }),
            -1);
  EXPECT_EQ(router_.SendChatMessageCount("alice", "hi", nullptr), -1);
  // But a local delivery still reports exactly one receiver.
  EXPECT_EQ(router_.SendChatMessageCount("alice", "hi",
                                         [](const std::string&) { return true; }),
            1);
}

TEST_F(MessageRouterTest, PublishCountReportsReceiverCount) {
  CountingPublisher publisher;
  publisher.next_count = 2;
  EXPECT_EQ(publisher.PublishCount("chan", "msg"), 2);
  EXPECT_TRUE(publisher.Publish("chan", "msg"));  // published = count >= 0

  publisher.next_count = 0;  // empty channel: published, nobody listening
  EXPECT_EQ(publisher.PublishCount("chan", "msg"), 0);
  EXPECT_TRUE(publisher.Publish("chan", "msg"));
}

TEST_F(MessageRouterTest, BroadcastToGroupFailsWithoutRedis) {
  EXPECT_FALSE(router_.BroadcastToGroup("g1", "msg"));
}

TEST(RouterChannelsTest, ChannelNaming) {
  EXPECT_EQ(RouterChannels::UserChat("u1"), "chirp:chat:user:u1");
  EXPECT_EQ(RouterChannels::GroupChat("g1"), "chirp:chat:group:g1");
  EXPECT_EQ(RouterChannels::UserSocial("u1"), "chirp:social:user:u1");
  EXPECT_EQ(RouterChannels::UserPresence("u1"), "chirp:presence:user:u1");
  EXPECT_EQ(RouterChannels::KickNotification("i1"), "chirp:kick:instance:i1");
  EXPECT_EQ(RouterChannels::ServiceRegister("chat", "i1"),
            "chirp:service:chat:i1");
}

// ---------------------------------------------------------------------------
// tcp_server.cc / websocket_server.cc construction (bind only, no accept)
// ---------------------------------------------------------------------------

TEST(ServerConstructionTest, TcpServerBindAndStop) {
  asio::io_context io;
  TcpServer server(io, /*port=*/0, [](std::shared_ptr<chirp::network::Session>,
                                      std::string&&) {});
  server.Stop();
}

TEST(ServerConstructionTest, TcpServerWithCloseCallback) {
  asio::io_context io;
  TcpServer server(io, /*port=*/0,
                   [](std::shared_ptr<chirp::network::Session>, std::string&&) {},
                   [](std::shared_ptr<chirp::network::Session>) {});
  server.Stop();
}

TEST(ServerConstructionTest, WebSocketServerBindAndStop) {
  asio::io_context io;
  WebSocketServer server(io, /*port=*/0,
                         [](std::shared_ptr<chirp::network::Session>,
                            std::string&&) {},
                         [](std::shared_ptr<chirp::network::Session>) {});
  server.Stop();
}

TEST(ServerConstructionTest, ServersOnEphemeralPortsDoNotConflict) {
  asio::io_context io;
  TcpServer s1(io, 0, [](std::shared_ptr<chirp::network::Session>, std::string&&) {});
  TcpServer s2(io, 0, [](std::shared_ptr<chirp::network::Session>, std::string&&) {});
  WebSocketServer s3(io, 0, [](std::shared_ptr<chirp::network::Session>, std::string&&) {});
  s1.Stop();
  s2.Stop();
  s3.Stop();
}

}  // namespace

namespace {

std::string Bulk(const std::string& s) {
  return "$" + std::to_string(s.size()) + "\r\n" + s + "\r\n";
}
std::string Int(int64_t v) { return ":" + std::to_string(v) + "\r\n"; }
std::string Simple(const std::string& s) { return "+" + s + "\r\n"; }
std::string Array(const std::vector<std::string>& items) {
  std::string out = "*" + std::to_string(items.size()) + "\r\n";
  for (const auto& i : items) {
    out += Bulk(i);
  }
  return out;
}

class RedisLoopbackTest : public ::testing::Test {
 protected:
  // A typical RESP command router for the fake server.
  static std::string DefaultRouter(std::atomic<int>& publish_count,
                                   std::vector<std::string>& seen_cmds,
                                   std::mutex& mu,
                                   const std::vector<std::string>& args) {
    if (args.empty()) {
      return Simple("OK");
    }
    const std::string& cmd = args[0];
    if (cmd == "GET") {
      return Bulk("stored-value");
    }
    if (cmd == "SET") {
      return Simple("OK");
    }
    if (cmd == "DEL" || cmd == "RPUSH" || cmd == "EXPIRE") {
      return Int(1);
    }
    if (cmd == "PUBLISH") {
      ++publish_count;
      return Int(2);
    }
    if (cmd == "LRANGE") {
      return Array({"foo", "bar"});
    }
    if (cmd == "KEYS") {
      return Array({"key1", "key2"});
    }
    if (cmd == "SUBSCRIBE" || cmd == "UNSUBSCRIBE") {
      std::lock_guard<std::mutex> lock(mu);
      seen_cmds.push_back(args.size() > 1 ? args[1] : "");
      return Array({"ok"});
    }
    return Simple("OK");
  }
};

TEST_F(RedisLoopbackTest, ClientCommandsSucceedAgainstFake) {
  std::atomic<int> publishes{0};
  std::vector<std::string> subscribed;
  std::mutex mu;
  chirp_test::FakeRedisServer fake([&](const std::vector<std::string>& args) {
    return DefaultRouter(publishes, subscribed, mu, args);
  });

  RedisClient client("127.0.0.1", fake.port());

  auto got = client.Get("k");
  ASSERT_TRUE(got.has_value());
  EXPECT_EQ(*got, "stored-value");

  EXPECT_TRUE(client.SetEx("k", "v", 60));
  EXPECT_TRUE(client.Del("k"));
  EXPECT_TRUE(client.RPush("list", "item"));
  EXPECT_TRUE(client.Expire("k", 30));

  auto range = client.LRange("list", 0, -1);
  ASSERT_EQ(range.size(), 2u);
  EXPECT_EQ(range[0], "foo");
  EXPECT_EQ(range[1], "bar");

  auto keys = client.Keys("*");
  ASSERT_EQ(keys.size(), 2u);
  EXPECT_EQ(keys[0], "key1");

  EXPECT_TRUE(client.Publish("chan", "msg"));
  EXPECT_EQ(publishes.load(), 1);
}

TEST_F(RedisLoopbackTest, ClientHandlesNullAndTypeMismatch) {
  chirp_test::FakeRedisServer fake([](const std::vector<std::string>& args) {
    if (!args.empty() && args[0] == "GET") {
      return std::string("$-1\r\n");  // nil
    }
    return std::string("+OK\r\n");    // wrong type for most commands
  });

  RedisClient client("127.0.0.1", fake.port());
  EXPECT_FALSE(client.Get("k").has_value());      // nil bulk
  EXPECT_FALSE(client.Del("k"));                  // integer required
  EXPECT_FALSE(client.Expire("k", 5));            // integer required
  EXPECT_TRUE(client.LRange("k", 0, -1).empty()); // array required
  EXPECT_TRUE(client.Keys("*").empty());
}

TEST_F(RedisLoopbackTest, SetExNonOkSimpleStringFails) {
  chirp_test::FakeRedisServer fake([](const std::vector<std::string>&) {
    return std::string("+NOPE\r\n");
  });
  RedisClient client("127.0.0.1", fake.port());
  EXPECT_FALSE(client.SetEx("k", "v", 60));
  EXPECT_FALSE(client.Expire("k", 5));  // +NOPE is not an integer
}

TEST_F(RedisLoopbackTest, SubscriberReceivesPushedMessages) {
  std::atomic<int> publishes{0};
  std::vector<std::string> subscribed;
  std::mutex mu;
  chirp_test::FakeRedisServer fake([&](const std::vector<std::string>& args) {
    return DefaultRouter(publishes, subscribed, mu, args);
  });

  RedisSubscriber sub("127.0.0.1", fake.port());
  std::promise<std::string> connected_promise;
  auto connected = connected_promise.get_future();
  sub.SetConnectCallback([&connected_promise] { connected_promise.set_value("up"); });

  std::promise<std::pair<std::string, std::string>> msg_promise;
  auto msg_future = msg_promise.get_future();
  sub.SetMessageCallback([&msg_promise](const std::string& ch, const std::string& payload) {
    msg_promise.set_value({ch, payload});
  });

  sub.Start();
  ASSERT_EQ(connected.wait_for(std::chrono::milliseconds(5000)),
            std::future_status::ready);
  EXPECT_TRUE(sub.IsConnected());

  EXPECT_TRUE(sub.Subscribe("chirp:chat:user:alice"));

  // Wait until the fake actually processed the SUBSCRIBE (the connection is
  // accepted) before pushing; otherwise the push may race with accept.
  {
    const auto deadline = std::chrono::steady_clock::now() +
                          std::chrono::milliseconds(3000);
    while (std::chrono::steady_clock::now() < deadline) {
      {
        std::lock_guard<std::mutex> lock(mu);
        if (!subscribed.empty()) break;
      }
      std::this_thread::sleep_for(std::chrono::milliseconds(2));
    }
  }
  fake.Publish("chirp:chat:user:alice", "hello-through-redis");
  if (msg_future.wait_for(std::chrono::milliseconds(5000)) !=
      std::future_status::ready) {
    std::lock_guard<std::mutex> lock(mu);
    fprintf(stderr, "DIAG connected=%d subscribed=%zu\n",
            (int)sub.IsConnected(), subscribed.size());
    for (const auto& ch : subscribed) {
      fprintf(stderr, "DIAG sub-cmd: %s\n", ch.c_str());
    }
  }
  ASSERT_EQ(msg_future.wait_for(std::chrono::milliseconds(1000)),
            std::future_status::ready);
  auto received = msg_future.get();
  EXPECT_EQ(received.first, "chirp:chat:user:alice");
  EXPECT_EQ(received.second, "hello-through-redis");

  EXPECT_TRUE(sub.Unsubscribe("chirp:chat:user:alice"));
  sub.Stop();
  EXPECT_FALSE(sub.IsConnected());
}

TEST_F(RedisLoopbackTest, MessageRouterDeliversSubscribedMessages) {
  std::atomic<int> publishes{0};
  std::vector<std::string> subscribed;
  std::mutex mu;
  chirp_test::FakeRedisServer fake([&](const std::vector<std::string>& args) {
    return DefaultRouter(publishes, subscribed, mu, args);
  });

  asio::io_context io;
  MessageRouter router(io, "127.0.0.1", fake.port());
  ASSERT_TRUE(router.Start());

  std::promise<std::string> got_msg;
  auto msg_future = got_msg.get_future();
  router.SubscribeUserChat("alice", [&got_msg](const std::string& msg) {
    got_msg.set_value(msg);
  });

  // Router only knows how to deliver after the subscription callback fired.
  {
    const auto deadline = std::chrono::steady_clock::now() +
                          std::chrono::milliseconds(3000);
    while (std::chrono::steady_clock::now() < deadline) {
      {
        std::lock_guard<std::mutex> lock(mu);
        if (!subscribed.empty()) break;
      }
      std::this_thread::sleep_for(std::chrono::milliseconds(2));
    }
  }
  fake.Publish("chirp:chat:user:alice", "routed-payload");

  // The router posts the callback onto its io_context.
  const auto deadline = std::chrono::steady_clock::now() + std::chrono::milliseconds(3000);
  while (std::chrono::steady_clock::now() < deadline &&
         msg_future.wait_for(std::chrono::milliseconds(0)) != std::future_status::ready) {
    io.poll();
    io.restart();
    std::this_thread::sleep_for(std::chrono::milliseconds(2));
  }
  ASSERT_EQ(msg_future.wait_for(std::chrono::milliseconds(1000)),
            std::future_status::ready);
  EXPECT_EQ(msg_future.get(), "routed-payload");

  // Publishing goes through the fake server
  EXPECT_TRUE(router.Publish("some-channel", "data"));
  EXPECT_TRUE(router.BroadcastToGroup("g1", "group-data"));
  EXPECT_TRUE(router.SendChatMessage("bob", "m", [](const std::string&) { return false; }));

  router.Stop();
}

TEST_F(WebSocketUtilTest, EncodeBase64AllLengthClasses) {
  using chirp::network::EncodeBase64;
  // len % 3 == 0
  EXPECT_EQ(EncodeBase64(reinterpret_cast<const uint8_t*>("abc"), 3), "YWJj");
  // len % 3 == 1 -> "==" padding
  EXPECT_EQ(EncodeBase64(reinterpret_cast<const uint8_t*>("a"), 1), "YQ==");
  EXPECT_EQ(EncodeBase64(reinterpret_cast<const uint8_t*>("abcd"), 4), "YWJjZA==");
  // len % 3 == 2 -> "=" padding
  EXPECT_EQ(EncodeBase64(reinterpret_cast<const uint8_t*>("ab"), 2), "YWI=");
  EXPECT_EQ(EncodeBase64(reinterpret_cast<const uint8_t*>("abcde"), 5), "YWJjZGU=");
  EXPECT_TRUE(EncodeBase64(nullptr, 0).empty());
}

// ---------------------------------------------------------------------------
// MessageRouter with injected misbehaving Redis doubles.
// ---------------------------------------------------------------------------

namespace {

class ThrowingPublisher : public RedisClient {
 public:
  ThrowingPublisher() : RedisClient("127.0.0.1", 1) {}
  bool Publish(const std::string&, const std::string&) override {
    throw std::runtime_error("redis publish exploded");
  }
  int64_t PublishCount(const std::string&, const std::string&) override {
    throw std::runtime_error("redis publish exploded");
  }
};

class ThrowingSubscriber : public RedisSubscriber {
 public:
  ThrowingSubscriber() : RedisSubscriber("127.0.0.1", 1) {}
  void Start() override { throw std::runtime_error("subscriber start exploded"); }
};

}  // namespace

TEST(MessageRouterInjectionTest, StartPropagatesSubscriberFailure) {
  asio::io_context io;
  MessageRouter router(io, "127.0.0.1", 1, nullptr, [] {
    return std::unique_ptr<RedisSubscriber>(std::make_unique<ThrowingSubscriber>());
  });
  EXPECT_FALSE(router.Start());
  router.Stop();
}

TEST(MessageRouterInjectionTest, PublishReportsPublisherException) {
  asio::io_context io;
  MessageRouter router(io, "127.0.0.1", 1,
                       [] {
                         return std::unique_ptr<RedisClient>(std::make_unique<ThrowingPublisher>());
                       },
                       nullptr);
  ASSERT_TRUE(router.Start());
  EXPECT_FALSE(router.Publish("chan", "m"));
  EXPECT_EQ(router.PublishCount("chan", "m"), -1);
  EXPECT_EQ(router.SendChatMessageCount("bob", "m", nullptr), -1);
  router.Stop();
}

TEST(MessageRouterLocalModeTest, EmptyHostRegistersSubscriptionsLocally) {
  asio::io_context io;
  MessageRouter router(io, "", 6379);

  // Without a Redis backend: publish fails...
  EXPECT_FALSE(router.Publish("chan", "m"));
  // ...but subscriptions are still recorded and report success.
  EXPECT_TRUE(router.SubscribeUserChat("alice", [](const std::string&) {}));
  EXPECT_TRUE(router.SubscribeGroupChat("g1", [](const std::string&) {}));
  EXPECT_TRUE(router.SubscribeUserSocial("alice", [](const std::string&) {}));
  EXPECT_TRUE(router.SubscribeKickNotification("inst-1", [](const std::string&) {}));
  // Start/Stop without a subscriber backend succeed trivially.
  EXPECT_TRUE(router.Start());
  router.Unsubscribe(RouterChannels::UserChat("alice"));
  router.Stop();
}

}  // namespace
