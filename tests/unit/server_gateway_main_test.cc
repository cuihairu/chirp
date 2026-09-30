// Coverage tests for the server gateway entry point
// (services/game/server_gateway/src/main.cc): the peer table, the framed
// packet dispatcher and the deadline/disconnect handlers. main.cc is
// included with main() renamed; GatewayRuntime - the frame graph hoisted out
// of main()'s lambda captures (batch 15, behavior-preserving) - is driven
// directly with in-memory sessions, so every branch of the packet flow runs
// without sockets. The argv/signal/io.run assembly inside main() itself
// stays with the process-level smoke legs (batch 9-12 convention); its
// GetArg/ParseU16Arg helpers are unit-tested directly below.

#include <gtest/gtest.h>

#include <chrono>
#include <cstdint>
#include <memory>
#include <string>
#include <thread>
#include <vector>

// Relative path, same convention as the party service test: unambiguous even
// if another main.cc ever lands on the include path.
#define main chirp_game_server_gateway_main
#include "../../services/game/server_gateway/src/main.cc"
#undef main

namespace {

// Pure in-memory Session mock: records everything sent through it.
class MockSession : public Session {
 public:
  void Send(std::string bytes) override { sent.push_back(std::move(bytes)); }
  void SendAndClose(std::string bytes) override {
    sent.push_back(std::move(bytes));
    close_after_send = true;
  }
  void Close() override { closed = true; }
  bool IsClosed() const override { return closed; }
  std::string RemoteAddress() const override { return "127.0.0.1"; }

  std::vector<std::string> sent;
  bool closed = false;
  bool close_after_send = false;
};

// Strips the u32-BE length prefix produced by ProtobufFraming::Encode and
// parses the payload (ProtobufFraming::Decode itself expects a bare message).
template <typename T>
bool DecodeFramed(const std::string& framed, T* out) {
  if (framed.size() < 4u) {
    return false;
  }
  const uint32_t len =
      (static_cast<uint32_t>(static_cast<uint8_t>(framed[0])) << 24) |
      (static_cast<uint32_t>(static_cast<uint8_t>(framed[1])) << 16) |
      (static_cast<uint32_t>(static_cast<uint8_t>(framed[2])) << 8) |
      static_cast<uint32_t>(static_cast<uint8_t>(framed[3]));
  if (framed.size() != 4u + static_cast<size_t>(len)) {
    return false;
  }
  return out->ParseFromString(framed.substr(4, len));
}

std::vector<chirp::gateway::Packet> ReceivedPackets(const MockSession& s) {
  std::vector<chirp::gateway::Packet> out;
  for (const auto& framed : s.sent) {
    chirp::gateway::Packet pkt;
    if (DecodeFramed(framed, &pkt)) {
      out.push_back(std::move(pkt));
    }
  }
  return out;
}

template <typename T>
std::vector<T> Received(const MockSession& s, chirp::gateway::MsgID id) {
  std::vector<T> out;
  for (const auto& pkt : ReceivedPackets(s)) {
    if (pkt.msg_id() != id) {
      continue;
    }
    T msg;
    if (msg.ParseFromString(pkt.body())) {
      out.push_back(std::move(msg));
    }
  }
  return out;
}

// The wire payload OnFrame expects: a serialized Packet (the length prefix
// is the session layer's concern; MockSession bypasses it).
std::string PacketPayload(chirp::gateway::MsgID id, int64_t seq,
                          const std::string& body) {
  chirp::gateway::Packet pkt;
  pkt.set_msg_id(id);
  pkt.set_sequence(seq);
  pkt.set_body(body);
  return pkt.SerializeAsString();
}

class ServerGatewayMainTest : public ::testing::Test {
 protected:
  void SetUp() override {
    config_.service_secrets = {{"chat", "chat-secret"}, {"game", "game-secret"}};
    config_.chat_service_id = "chat";
    config_.heartbeat_interval_seconds = 7;
    handlers_ = std::make_unique<sg::ServerGatewayHandlers>(config_, registry_, queue_);
    rt_ = std::make_unique<GatewayRuntime>(io_, config_, *handlers_, auth_timeout_);
  }

  std::shared_ptr<MockSession> NewSession() { return std::make_shared<MockSession>(); }

  void Deliver(const std::shared_ptr<MockSession>& s, chirp::gateway::MsgID id, int64_t seq,
               const google::protobuf::Message& body) {
    rt_->OnFrame(s, PacketPayload(id, seq, body.SerializeAsString()));
  }

  void DeliverRaw(const std::shared_ptr<MockSession>& s, std::string payload) {
    rt_->OnFrame(s, std::move(payload));
  }

  // Auths the session as `service` and asserts the happy handshake.
  void AuthAs(const std::shared_ptr<MockSession>& s, const std::string& service) {
    sg::ServerAuthRequest req;
    req.set_service_id(service);
    req.set_secret(service + "-secret");
    req.set_protocol_version(1);
    Deliver(s, chirp::gateway::SERVER_AUTH_REQ, 1, req);
    const auto resps = Received<sg::ServerAuthResponse>(*s, chirp::gateway::SERVER_AUTH_RESP);
    ASSERT_EQ(resps.size(), 1u);
    EXPECT_EQ(resps[0].code(), chirp::common::OK);
    EXPECT_EQ(resps[0].heartbeat_interval_seconds(), config_.heartbeat_interval_seconds);
  }

  // Inserts an entry straight into the peer table (no frame), so the
  // deadline/disconnect contracts can be driven without a full handshake.
  void InsertUnauthenticated(const std::shared_ptr<MockSession>& s) {
    PeerContext ctx;
    ctx.session = s;
    ctx.sender = std::make_shared<SessionPeerSender>(s);
    rt_->peers.emplace(s.get(), std::move(ctx));
  }

  sg::ServerGatewayConfig config_;
  sg::ServiceRegistry registry_;
  sg::EventQueue queue_{1000};
  std::unique_ptr<sg::ServerGatewayHandlers> handlers_;
  asio::io_context io_;
  // Fast auth-timeout TTL so the expiry test sleeps ~1s, not 10s.
  int auth_timeout_ = 1;
  std::unique_ptr<GatewayRuntime> rt_;
};

// ---------------------------------------------------------------------------
// main()'s argv helpers (called by the scaffolding, driven directly)
// ---------------------------------------------------------------------------

TEST_F(ServerGatewayMainTest, GetArgHitsPairSkipsDanglingAndFallsBack) {
  char bin[] = "bin";
  char port[] = "--port";
  char value[] = "9000";
  char dangling[] = "--dangling";
  char* argv[] = {bin, port, value, dangling};
  EXPECT_EQ(GetArg(4, argv, "--port", "8100"), "9000");
  EXPECT_EQ(GetArg(4, argv, "--missing", "8100"), "8100");
  // A key in the last slot has no value to take; the default wins.
  EXPECT_EQ(GetArg(4, argv, "--dangling", "8100"), "8100");
}

TEST_F(ServerGatewayMainTest, ParseU16ArgConvertsFallsBackAndTruncatesGarbage) {
  char bin[] = "bin";
  char port[] = "--port";
  char value[] = "8100";
  char* argv[] = {bin, port, value};
  EXPECT_EQ(ParseU16Arg(3, argv, "--port", 123), 8100);
  EXPECT_EQ(ParseU16Arg(3, argv, "--gone", 123), 123);
  char garbage[] = "not-a-number";
  char* argv2[] = {bin, port, garbage};
  EXPECT_EQ(ParseU16Arg(3, argv2, "--port", 123), 0);  // std::atoi -> 0
}

TEST_F(ServerGatewayMainTest, NowMsTracksWallClock) {
  const auto before = std::chrono::duration_cast<std::chrono::milliseconds>(
                          std::chrono::system_clock::now().time_since_epoch())
                          .count();
  const auto now = NowMs();
  const auto after = std::chrono::duration_cast<std::chrono::milliseconds>(
                         std::chrono::system_clock::now().time_since_epoch())
                         .count();
  EXPECT_GE(now, before - 5000);
  EXPECT_LE(now, after + 5000);
}

// ---------------------------------------------------------------------------
// Framing helpers
// ---------------------------------------------------------------------------

TEST_F(ServerGatewayMainTest, SendPacketAndParseBodyRoundTrip) {
  auto s = NewSession();
  sg::ServerHeartbeatPing ping;
  ping.set_client_time_ms(1234);
  SendPacket(s, chirp::gateway::SERVER_HEARTBEAT_PING, 77, ping);
  ASSERT_EQ(s->sent.size(), 1u);
  chirp::gateway::Packet pkt;
  ASSERT_TRUE(DecodeFramed(s->sent[0], &pkt));
  EXPECT_EQ(pkt.msg_id(), chirp::gateway::SERVER_HEARTBEAT_PING);
  EXPECT_EQ(pkt.sequence(), 77);
  sg::ServerHeartbeatPing back;
  ASSERT_TRUE(ParseBody(pkt, &back));
  EXPECT_EQ(back.client_time_ms(), 1234);

  chirp::gateway::Packet junk;
  junk.set_body("\xff\xfe not a message");
  sg::ServerHeartbeatPing out;
  EXPECT_FALSE(ParseBody(junk, &out));
}

TEST_F(ServerGatewayMainTest, SessionPeerSenderRequiresLiveOpenSession) {
  std::weak_ptr<Session> expired;
  {
    auto s = NewSession();
    expired = s;
  }
  SessionPeerSender dead(expired);
  sg::EventDeliverNotify notify;
  notify.set_event_id("e1");
  EXPECT_FALSE(dead.Send(chirp::gateway::EVENT_DELIVER_NOTIFY, notify));

  auto s = NewSession();
  SessionPeerSender sender(s);
  s->Close();
  EXPECT_FALSE(sender.Send(chirp::gateway::EVENT_DELIVER_NOTIFY, notify));
  s->closed = false;
  ASSERT_TRUE(sender.Send(chirp::gateway::EVENT_DELIVER_NOTIFY, notify));
  ASSERT_EQ(s->sent.size(), 1u);
  chirp::gateway::Packet pkt;
  ASSERT_TRUE(DecodeFramed(s->sent[0], &pkt));
  EXPECT_EQ(pkt.msg_id(), chirp::gateway::EVENT_DELIVER_NOTIFY);
  EXPECT_EQ(pkt.sequence(), 0);  // server-initiated frames carry no sequence
  sg::EventDeliverNotify back;
  ASSERT_TRUE(back.ParseFromString(pkt.body()));
  EXPECT_EQ(back.event_id(), "e1");
}

// ---------------------------------------------------------------------------
// Frame dispatch: authentication flow
// ---------------------------------------------------------------------------

TEST_F(ServerGatewayMainTest, UnparseablePayloadIsIgnoredBeforePeerTable) {
  auto s = NewSession();
  DeliverRaw(s, std::string("\xff\xfe garbage"));
  EXPECT_TRUE(ReceivedPackets(*s).empty());
  EXPECT_TRUE(rt_->peers.empty());
  EXPECT_FALSE(s->closed);
}

TEST_F(ServerGatewayMainTest, FirstFrameMustBeServerAuthRequest) {
  auto s = NewSession();
  sg::ServerHeartbeatPing ping;
  ping.set_client_time_ms(1);
  Deliver(s, chirp::gateway::SERVER_HEARTBEAT_PING, 1, ping);
  EXPECT_TRUE(s->closed);
  EXPECT_TRUE(rt_->peers.empty());
  EXPECT_TRUE(ReceivedPackets(*s).empty());
}

TEST_F(ServerGatewayMainTest, AuthGarbageBodyEchoesInvalidParamAndCloses) {
  auto s = NewSession();
  DeliverRaw(s, PacketPayload(chirp::gateway::SERVER_AUTH_REQ, 9, "\xff\xfe junk"));
  ASSERT_EQ(s->sent.size(), 1u);
  chirp::gateway::Packet pkt;
  ASSERT_TRUE(DecodeFramed(s->sent[0], &pkt));
  EXPECT_EQ(pkt.msg_id(), chirp::gateway::SERVER_AUTH_RESP);
  EXPECT_EQ(pkt.sequence(), 9);  // sequence echoed on the failure path too
  sg::ServerAuthResponse resp;
  ASSERT_TRUE(resp.ParseFromString(pkt.body()));
  EXPECT_EQ(resp.code(), chirp::common::INVALID_PARAM);
  EXPECT_TRUE(s->closed);
  EXPECT_TRUE(rt_->peers.empty());
}

TEST_F(ServerGatewayMainTest, AuthWrongSecretRejectsAndCloses) {
  auto s = NewSession();
  sg::ServerAuthRequest req;
  req.set_service_id("game");
  req.set_secret("wrong");
  req.set_protocol_version(1);
  Deliver(s, chirp::gateway::SERVER_AUTH_REQ, 5, req);
  const auto resps = Received<sg::ServerAuthResponse>(*s, chirp::gateway::SERVER_AUTH_RESP);
  ASSERT_EQ(resps.size(), 1u);
  EXPECT_EQ(resps[0].code(), chirp::common::AUTH_FAILED);
  EXPECT_GT(resps[0].server_time_ms(), 0);  // NowMs() wired into the response
  // HandleAuth leaves heartbeat_interval_seconds unset on failure: the
  // interval is advertised to peers that actually get in.
  EXPECT_EQ(resps[0].heartbeat_interval_seconds(), 0);
  EXPECT_TRUE(s->closed);
  EXPECT_TRUE(rt_->peers.empty());
  EXPECT_FALSE(registry_.IsOnline("game"));
}

TEST_F(ServerGatewayMainTest, AuthOkMarksPeerAndArmsAuthenticatedDeadline) {
  auto s = NewSession();
  AuthAs(s, "game");
  ASSERT_EQ(rt_->peers.size(), 1u);
  auto& ctx = rt_->peers.at(s.get());
  EXPECT_TRUE(ctx.authenticated);
  EXPECT_EQ(ctx.service_id, "game");
  ASSERT_TRUE(ctx.deadline != nullptr);
  // authenticated TTL = max(2, 2 * heartbeat) = 14s with heartbeat = 7.
  const auto exp = ctx.deadline->expiry();
  const auto now = std::chrono::steady_clock::now();
  EXPECT_GE(exp, now + std::chrono::seconds(13));
  EXPECT_LE(exp, now + std::chrono::seconds(15));
}

TEST_F(ServerGatewayMainTest, ReconnectDisplacesPreviousConnectionForSameService) {
  auto first = NewSession();
  AuthAs(first, "game");
  auto second = NewSession();
  AuthAs(second, "game");

  EXPECT_TRUE(first->closed);
  EXPECT_FALSE(second->closed);
  EXPECT_EQ(rt_->peers.size(), 1u);
  EXPECT_EQ(rt_->peers.count(first.get()), 0u);
  ASSERT_EQ(rt_->peers.count(second.get()), 1u);
  EXPECT_TRUE(registry_.IsOnline("game"));
  EXPECT_EQ(registry_.Get("game").get(), rt_->peers.at(second.get()).sender.get());

  // The displaced connection's late close callback is a no-op: it must not
  // unregister the live connection or touch the peer table.
  rt_->OnClose(first);
  EXPECT_TRUE(registry_.IsOnline("game"));
  EXPECT_EQ(rt_->peers.size(), 1u);
}

// ---------------------------------------------------------------------------
// Frame dispatch: authenticated traffic
// ---------------------------------------------------------------------------

TEST_F(ServerGatewayMainTest, HeartbeatDispatchAnswersPongAndToleratesGarbage) {
  auto s = NewSession();
  AuthAs(s, "game");
  sg::ServerHeartbeatPing ping;
  ping.set_client_time_ms(1234);
  Deliver(s, chirp::gateway::SERVER_HEARTBEAT_PING, 3, ping);
  const auto pongs = Received<sg::ServerHeartbeatPong>(*s, chirp::gateway::SERVER_HEARTBEAT_PONG);
  ASSERT_EQ(pongs.size(), 1u);
  EXPECT_GT(pongs[0].server_time_ms(), 0);

  // Garbage ping body: no pong, the connection survives.
  DeliverRaw(s, PacketPayload(chirp::gateway::SERVER_HEARTBEAT_PING, 4, "\xff junk"));
  EXPECT_EQ(ReceivedPackets(*s).size(), 2u);  // AUTH_RESP + PONG only
  EXPECT_FALSE(s->closed);
}

TEST_F(ServerGatewayMainTest, InjectDispatchRoutesToChatAndRejectsGarbage) {
  auto game = NewSession();
  AuthAs(game, "game");

  sg::MessageInjectRequest req;
  req.set_inject_id("inj-9");
  req.set_sender_kind(chirp::game_server_gateway::SENDER_NPC);
  req.set_sender_id("npc:blacksmith_01");
  req.set_channel_type(3);  // WORLD
  req.set_channel_id("world");
  req.set_content("hello travelers");

  // Chat is offline yet: the dispatch reports SERVER_UNAVAILABLE.
  Deliver(game, chirp::gateway::INJECT_MESSAGE_REQ, 5, req);
  auto resps = Received<sg::MessageInjectResponse>(*game, chirp::gateway::INJECT_MESSAGE_RESP);
  ASSERT_EQ(resps.size(), 1u);
  EXPECT_EQ(resps[0].code(), chirp::common::SERVER_UNAVAILABLE);

  // Bring chat online; the same injection now lands on its connection.
  auto chat = NewSession();
  AuthAs(chat, "chat");
  Deliver(game, chirp::gateway::INJECT_MESSAGE_REQ, 6, req);
  resps = Received<sg::MessageInjectResponse>(*game, chirp::gateway::INJECT_MESSAGE_RESP);
  ASSERT_EQ(resps.size(), 2u);
  EXPECT_EQ(resps[1].code(), chirp::common::OK);
  EXPECT_EQ(resps[1].inject_id(), "inj-9");
  const auto notifies =
      Received<sg::InjectMessageNotify>(*chat, chirp::gateway::INJECT_MESSAGE_NOTIFY);
  ASSERT_EQ(notifies.size(), 1u);
  EXPECT_EQ(notifies[0].message().inject_id(), "inj-9");

  // Garbage body: INVALID_PARAM, no second notify.
  DeliverRaw(game, PacketPayload(chirp::gateway::INJECT_MESSAGE_REQ, 7, "\xff junk"));
  resps = Received<sg::MessageInjectResponse>(*game, chirp::gateway::INJECT_MESSAGE_RESP);
  ASSERT_EQ(resps.size(), 3u);
  EXPECT_EQ(resps[2].code(), chirp::common::INVALID_PARAM);
  EXPECT_EQ(Received<sg::InjectMessageNotify>(*chat, chirp::gateway::INJECT_MESSAGE_NOTIFY).size(),
            1u);
}

TEST_F(ServerGatewayMainTest, PublishDispatchDeliversQueuesAndRejectsGarbage) {
  auto game = NewSession();
  AuthAs(game, "game");

  // Target online: delivered immediately on the same connection.
  sg::EventPublishRequest pub;
  pub.set_target_service_id("game");
  pub.set_event_type("quest.trigger");
  pub.set_payload("payload");
  Deliver(game, chirp::gateway::EVENT_PUBLISH_REQ, 7, pub);
  auto resps = Received<sg::EventPublishResponse>(*game, chirp::gateway::EVENT_PUBLISH_RESP);
  ASSERT_EQ(resps.size(), 1u);
  EXPECT_EQ(resps[0].code(), chirp::common::OK);
  EXPECT_FALSE(resps[0].queued());
  const auto delivers =
      Received<sg::EventDeliverNotify>(*game, chirp::gateway::EVENT_DELIVER_NOTIFY);
  ASSERT_EQ(delivers.size(), 1u);
  EXPECT_EQ(delivers[0].event_id(), resps[0].event_id());

  // Target offline: queued for redelivery, nothing delivered here.
  sg::EventPublishRequest queued = pub;
  queued.set_target_service_id("trade");
  Deliver(game, chirp::gateway::EVENT_PUBLISH_REQ, 8, queued);
  resps = Received<sg::EventPublishResponse>(*game, chirp::gateway::EVENT_PUBLISH_RESP);
  ASSERT_EQ(resps.size(), 2u);
  EXPECT_EQ(resps[1].code(), chirp::common::OK);
  EXPECT_TRUE(resps[1].queued());
  EXPECT_EQ(queue_.UnackedCount("trade"), 1u);
  EXPECT_EQ(Received<sg::EventDeliverNotify>(*game, chirp::gateway::EVENT_DELIVER_NOTIFY).size(),
            1u);

  // Garbage publish body: INVALID_PARAM without touching the queue.
  DeliverRaw(game, PacketPayload(chirp::gateway::EVENT_PUBLISH_REQ, 9, "\xff junk"));
  resps = Received<sg::EventPublishResponse>(*game, chirp::gateway::EVENT_PUBLISH_RESP);
  ASSERT_EQ(resps.size(), 3u);
  EXPECT_EQ(resps[2].code(), chirp::common::INVALID_PARAM);
}

TEST_F(ServerGatewayMainTest, AckDispatchAcksOwnEventsAndRejectsGarbage) {
  auto game = NewSession();
  AuthAs(game, "game");

  sg::EventPublishRequest pub;
  pub.set_target_service_id("game");
  pub.set_event_type("quest.trigger");
  pub.set_payload("payload");
  Deliver(game, chirp::gateway::EVENT_PUBLISH_REQ, 7, pub);
  const auto delivers =
      Received<sg::EventDeliverNotify>(*game, chirp::gateway::EVENT_DELIVER_NOTIFY);
  ASSERT_EQ(delivers.size(), 1u);
  EXPECT_EQ(queue_.UnackedCount("game"), 1u);  // in flight until acked

  // The ack is attributed to the frame's authenticated service_id.
  sg::EventAckRequest ack;
  ack.add_event_ids(delivers[0].event_id());
  Deliver(game, chirp::gateway::EVENT_ACK_REQ, 8, ack);
  auto resps = Received<sg::EventAckResponse>(*game, chirp::gateway::EVENT_ACK_RESP);
  ASSERT_EQ(resps.size(), 1u);
  EXPECT_EQ(resps[0].code(), chirp::common::OK);
  EXPECT_EQ(queue_.UnackedCount("game"), 0u);

  // Garbage ack body: INVALID_PARAM.
  DeliverRaw(game, PacketPayload(chirp::gateway::EVENT_ACK_REQ, 9, "\xff junk"));
  resps = Received<sg::EventAckResponse>(*game, chirp::gateway::EVENT_ACK_RESP);
  ASSERT_EQ(resps.size(), 2u);
  EXPECT_EQ(resps[1].code(), chirp::common::INVALID_PARAM);
}

TEST_F(ServerGatewayMainTest, UnknownMsgIdIsIgnoredAfterAuth) {
  auto s = NewSession();
  AuthAs(s, "game");
  const auto before = ReceivedPackets(*s).size();
  // The WP-8 block (5013) belongs to app_chat now; this hub ignores it, and
  // any other unimplemented server-plane id with it.
  sg::ServerAuthRequest dummy;
  dummy.set_service_id("ignored");
  Deliver(s, static_cast<chirp::gateway::MsgID>(5013), 11, dummy);
  Deliver(s, static_cast<chirp::gateway::MsgID>(9999), 12, dummy);
  EXPECT_EQ(ReceivedPackets(*s).size(), before);
  EXPECT_FALSE(s->closed);
  EXPECT_EQ(rt_->peers.size(), 1u);
}

// ---------------------------------------------------------------------------
// Deadline / disconnect contracts
// ---------------------------------------------------------------------------

TEST_F(ServerGatewayMainTest, ArmDeadlineIgnoresUnknownPeer) {
  auto s = NewSession();
  rt_->ArmDeadline(s.get(), true);
  EXPECT_TRUE(rt_->peers.empty());
}

TEST_F(ServerGatewayMainTest, ReArmingDeadlineReusesTimerAndSurvivesCancellation) {
  auto s = NewSession();
  AuthAs(s, "game");
  auto* first_timer = rt_->peers.at(s.get()).deadline.get();
  ASSERT_TRUE(first_timer != nullptr);

  // Any subsequent frame re-arms: the previous wait is cancelled in place
  // (same timer object) and its aborted completion must not close the peer.
  sg::ServerHeartbeatPing ping;
  ping.set_client_time_ms(1);
  Deliver(s, chirp::gateway::SERVER_HEARTBEAT_PING, 2, ping);
  EXPECT_EQ(rt_->peers.at(s.get()).deadline.get(), first_timer);

  io_.restart();
  while (io_.poll() > 0) {
  }
  EXPECT_FALSE(s->closed);
  EXPECT_EQ(rt_->peers.size(), 1u);
}

TEST_F(ServerGatewayMainTest, UnauthenticatedDeadlineTtlUsesAuthTimeout) {
  // auth_timeout = 10 > 1: the max() keeps the configured timeout.
  GatewayRuntime wide(io_, config_, *handlers_, 10);
  auto s = NewSession();
  PeerContext ctx;
  ctx.session = s;
  ctx.sender = std::make_shared<SessionPeerSender>(s);
  wide.peers.emplace(s.get(), std::move(ctx));
  wide.ArmDeadline(s.get(), false);
  ASSERT_EQ(wide.peers.count(s.get()), 1u);
  const auto exp = wide.peers.at(s.get()).deadline->expiry();
  const auto now = std::chrono::steady_clock::now();
  EXPECT_GE(exp, now + std::chrono::seconds(9));
  EXPECT_LE(exp, now + std::chrono::seconds(11));
}

TEST_F(ServerGatewayMainTest, UnauthenticatedDeadlineFiresAndClosesIdlePeer) {
  auto s = NewSession();
  InsertUnauthenticated(s);
  rt_->ArmDeadline(s.get(), false);  // ttl = max(1, auth_timeout_) = 1s
  ASSERT_EQ(rt_->peers.count(s.get()), 1u);
  const auto exp = rt_->peers.at(s.get()).deadline->expiry();
  const auto now = std::chrono::steady_clock::now();
  EXPECT_GE(exp, now + std::chrono::milliseconds(500));
  EXPECT_LE(exp, now + std::chrono::seconds(2));

  std::this_thread::sleep_for(std::chrono::milliseconds(1100));
  io_.restart();
  while (io_.poll() > 0) {
  }
  EXPECT_TRUE(s->closed);
  EXPECT_TRUE(rt_->peers.empty());
  // The entry never authenticated, so the registry was never touched.
  EXPECT_FALSE(registry_.IsOnline("game"));
}

TEST_F(ServerGatewayMainTest, CloseCallbackCleansPeerRegistryAndTimer) {
  auto s = NewSession();
  AuthAs(s, "game");
  EXPECT_TRUE(registry_.IsOnline("game"));
  rt_->OnClose(s);
  EXPECT_TRUE(rt_->peers.empty());
  EXPECT_FALSE(registry_.IsOnline("game"));
  // A repeat close for the already-removed session is a no-op.
  rt_->OnClose(s);
  EXPECT_TRUE(rt_->peers.empty());
}

TEST_F(ServerGatewayMainTest, DisconnectUnauthenticatedEntrySkipsRegistryCleanup) {
  auto s = NewSession();
  InsertUnauthenticated(s);
  rt_->OnDisconnect(s.get());
  EXPECT_TRUE(rt_->peers.empty());
  // Empty service_id: OnPeerDisconnected was skipped, registry stays clean.
  EXPECT_FALSE(registry_.IsOnline("game"));
}

}  // namespace
