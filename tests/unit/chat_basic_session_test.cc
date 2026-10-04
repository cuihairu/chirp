// Basic chat build (chirp_chat) packet dispatch: main.cc keeps its handler
// surface (MessageStore, HandlePacket's 38-case dispatch, HandleDisconnect)
// in an anonymous namespace; pull the file in with main() renamed so the
// tests below can drive the production dispatch directly - the same pattern
// chat_managers_test.cc (main_distributed) and chat_enhanced_session_test.cc
// (main_enhanced) use. Coverage batch 10: this file sits outside the gate's
// INCLUDE_PREFIXES universe (is_excluded drops main*), so the numbers come
// from the measurer over gcov JSON, not from the gate.

#include <gtest/gtest.h>

#include <fcntl.h>
#include <unistd.h>

#include <atomic>
#include <chrono>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <functional>
#include <memory>
#include <mutex>
#include <string>
#include <thread>
#include <utility>
#include <vector>

#include <asio.hpp>

#include "network/chat_peer_hub.h"
#include "network/chat_peer_link.h"
#include "network/redis_client.h"
#include "network/server_gateway_peer.h"
#include "proto/auth.pb.h"
#include "proto/chat.pb.h"
#include "proto/common.pb.h"
#include "proto/game_server_gateway.pb.h"
#include "proto/gateway.pb.h"

#include "fake_servers.h"
#include "in_memory_redis.h"
#include "jwt.h"
#include "network/tcp_client.h"
#include "network/websocket_client.h"

// The basic main keeps its internals (MessageStore, FeatureHandlers,
// HandlePacket, HandleDisconnect, ...) in an anonymous namespace; include
// the file with main() renamed so the tests can drive them directly.
#define main chirp_chat_basic_main
#include "main.cc"
#undef main

namespace {

// Deadline-bounded predicate poll (fixed sleeps flake on this machine's
// load): 5s budget, 2ms ticks.
bool WaitFor(const std::function<bool()>& pred, int64_t budget_ms = 5000) {
  const auto deadline =
      std::chrono::steady_clock::now() + std::chrono::milliseconds(budget_ms);
  while (std::chrono::steady_clock::now() < deadline) {
    if (pred()) {
      return true;
    }
    std::this_thread::sleep_for(std::chrono::milliseconds(2));
  }
  return pred();
}

// Pure in-memory Session mock: records every frame sent through it and can
// fake a half-closed peer (the delivery paths treat that as offline).
class MockSession : public chirp::network::Session {
 public:
  void Send(std::string bytes) override { sent.push_back(std::move(bytes)); }
  void SendAndClose(std::string bytes) override {
    sent.push_back(std::move(bytes));
    close_after_send = true;
  }
  void Close() override { closed = true; }
  bool IsClosed() const override { return closed; }
  bool PeerHalfClosed() override { return half_closed; }
  std::string RemoteAddress() const override { return "127.0.0.1"; }

  std::vector<std::string> sent;
  bool closed = false;
  bool close_after_send = false;
  bool half_closed = false;
};

// Strips the u32-BE length prefix produced by ProtobufFraming::Encode and
// parses the payload.
bool DecodeFramed(const std::string& framed, chirp::gateway::Packet* out) {
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

std::vector<chirp::gateway::Packet> FramesOf(const MockSession& session) {
  std::vector<chirp::gateway::Packet> frames;
  for (const auto& framed : session.sent) {
    chirp::gateway::Packet pkt;
    if (DecodeFramed(framed, &pkt)) {
      frames.push_back(std::move(pkt));
    }
  }
  return frames;
}

std::vector<chirp::gateway::Packet> FramesOf(const MockSession& session,
                                             chirp::gateway::MsgID msg_id) {
  std::vector<chirp::gateway::Packet> matches;
  for (const auto& pkt : FramesOf(session)) {
    if (pkt.msg_id() == msg_id) {
      matches.push_back(pkt);
    }
  }
  return matches;
}

// The fixture mirrors main()'s wiring 1:1 (same manager/resolver/notifier
// graph) so the dispatch under test is the production one; only the io loop,
// listeners and argv scaffolding are absent.
class BasicChatTest : public ::testing::Test {
 protected:
  void SetUp() override {
    chirp::common::Logger::Instance().SetLevel(chirp::common::Logger::Level::kError);
    store_ = std::make_shared<MessageStore>(nullptr, /*ttl=*/0);
    state_ = std::make_shared<chirp::network::SessionRegistry>();
    trusted_ = std::make_shared<std::unordered_set<const chirp::network::Session*>>();
    acks_ = std::make_shared<chirp::chat::DeliveryAckManager>(
        io_, ack_config_,
        [this](const std::string& receiver, const std::string& payload) {
          store_->AddOfflineBytes(receiver, payload);
        },
        [this](const std::string& receiver, const std::string& payload) {
          store_->RemoveOffline(receiver, payload);
        });
    RebuildFeatures();
  }

  void TearDown() override { io_.stop(); }

  // Assembles the production FeatureHandlers graph. Tests swap pointer
  // members (rate_limiter / token_verifier / word_filter / hub_peer /
  // directory / hub / spoke / gateway_secret) after SetUp; the reference
  // members are fixed here.
  void RebuildFeatures(chirp::chat::MentionHandlers* mentions_override = nullptr) {
    notify_member_ = [this](const std::string& user_id, chirp::gateway::MsgID msg_id,
                            const google::protobuf::Message& body) -> bool {
      if (msg_id == chirp::gateway::CHAT_MESSAGE_NOTIFY) {
        const auto& chat_msg = static_cast<const chirp::chat::ChatMessage&>(body);
        if (delivery_prefs_.IsChannelMuted(user_id, chat_msg.channel_type()) ||
            delivery_prefs_.IsUserBlocked(user_id, chat_msg.sender_id())) {
          return true;
        }
      }
      const auto recvs = chirp::network::GetUserSessions(state_, user_id);
      if (recvs.empty()) {
        return false;
      }
      const std::string payload = body.SerializeAsString();
      for (const auto& recv : recvs) {
        chirp::chat::runtime::SendPacket(recv, msg_id, 0, payload);
      }
      return true;
    };
    resolve_members_ = [this](chirp::chat::ChannelType type, const std::string& channel_id,
                              const std::string& exclude) -> std::vector<std::string> {
      if (type == chirp::chat::PRIVATE) {
        const size_t sep = channel_id.find('|');
        if (sep == std::string::npos || sep == 0 || sep + 1 >= channel_id.size()) {
          return {};
        }
        const std::string left = channel_id.substr(0, sep);
        const std::string right = channel_id.substr(sep + 1);
        const std::string& other = (left == exclude) ? right : left;
        if (other.empty() || other == exclude) {
          return {};
        }
        return {other};
      }
      std::vector<std::string> out;
      for (const auto& member : groups_.GetMembers(channel_id)) {
        if (member.user_id() != exclude) {
          out.push_back(member.user_id());
        }
      }
      return out;
    };
    is_moderator_ = [this](chirp::chat::ChannelType type, const std::string& channel_id,
                           const std::string& user_id) -> bool {
      if (type != chirp::chat::GUILD) {
        return false;
      }
      for (const auto& member : groups_.GetMembers(channel_id)) {
        if (member.user_id() == user_id) {
          return member.role() >= chirp::chat::MODERATOR;
        }
      }
      return false;
    };
    group_handlers_ =
        std::make_unique<chirp::chat::GroupHandlers>(groups_, notify_member_);
    receipt_handlers_ = std::make_unique<chirp::chat::ReadReceiptHandlers>(
        receipts_, resolve_members_, notify_member_);
    typing_handlers_ =
        std::make_unique<chirp::chat::TypingHandlers>(typing_, resolve_members_, notify_member_);
    reaction_handlers_ = std::make_unique<chirp::chat::ReactionHandlers>(
        reactions_, resolve_members_, notify_member_);
    purge_offline_ = [this](const std::string& message_id, const std::string& receiver) {
      store_->PurgeOfflineByMessageId(receiver, message_id);
    };
    mark_recalled_ = [this](chirp::chat::ChannelType type, const std::string& channel_id,
                            const std::string& message_id) {
      store_->MarkRecalled(type, channel_id, message_id);
    };
    edit_handlers_ = std::make_unique<chirp::chat::MessageEditHandlers>(
        edits_, resolve_members_, is_moderator_, notify_member_, purge_offline_,
        mark_recalled_);
    mention_handlers_ =
        std::make_unique<chirp::chat::MentionHandlers>(mentions_, is_moderator_);

    features_ = std::make_unique<FeatureHandlers>(FeatureHandlers{
        .groups = *group_handlers_,
        .receipts = *receipt_handlers_,
        .typing = *typing_handlers_,
        .reactions = *reaction_handlers_,
        .edits = *edit_handlers_,
        .mentions = mentions_override ? *mentions_override : *mention_handlers_,
        .push = push_,
        .npc_service_id = {},
        .gateway_secret = {},
        .trusted_conns = trusted_,
    });
    features_->rate_limiter = &limiter_;
    // channel_pacer 刻意留 null（生产 main 恒装一个，但 1-5s 的窗口会咬死本
    // 文件里一切快速连发用例；节奏拒绝臂由 SendGuardChain 自装真 pacer 覆盖，
    // 与 rate_limiter/word_filter 的可换装缝同款做法）。
    features_->repeat_guard = &repeat_;
    features_->delivery_prefs = &delivery_prefs_;
    features_->word_filter = &word_filter_;
    features_->token_verifier = &verifier_;
    features_->acks = acks_.get();
  }

  // --- drive helpers -------------------------------------------------------

  // Runs one framed packet through the production dispatch.
  void Dispatch(chirp::gateway::MsgID msg_id, const std::string& body,
                const std::shared_ptr<MockSession>& session, int64_t seq = 1,
                const std::shared_ptr<MessageStore>& store = nullptr) {
    chirp::gateway::Packet pkt;
    pkt.set_msg_id(msg_id);
    pkt.set_sequence(seq);
    pkt.set_body(body);
    HandlePacket(store ? store : store_, state_, *features_, session,
                 pkt.SerializeAsString());
  }

  template <typename Req>
  void DispatchReq(chirp::gateway::MsgID msg_id, const Req& req,
                   const std::shared_ptr<MockSession>& session, int64_t seq = 1) {
    Dispatch(msg_id, req.SerializeAsString(), session, seq);
  }

  // LOGIN_REQ through the dispatch; returns the parsed response.
  chirp::auth::LoginResponse Login(const std::shared_ptr<MockSession>& session,
                                   const std::string& token,
                                   const std::string& device_id = "d1",
                                   const std::string& platform = "web",
                                   bool supports_ack = false, int64_t seq = 1) {
    chirp::auth::LoginRequest req;
    req.set_token(token);
    req.set_device_id(device_id);
    req.set_platform(platform);
    req.set_supports_message_ack(supports_ack);
    DispatchReq(chirp::gateway::LOGIN_REQ, req, session, seq);
    chirp::auth::LoginResponse resp;
    const auto frames = FramesOf(*session, chirp::gateway::LOGIN_RESP);
    if (!frames.empty()) {
      resp.ParseFromString(frames.back().body());
    } else {
      resp.set_code(chirp::common::SERVER_UNAVAILABLE);  // 哨兵：没回包不许读成 OK
    }
    return resp;
  }

  // SEND_MESSAGE_REQ through the dispatch (sender must be logged in and
  // equal the authenticated user, like ValidateSendMessageRequest demands).
  chirp::chat::SendMessageResponse Send(
      const std::shared_ptr<MockSession>& session, const std::string& sender,
      const std::string& receiver, const std::string& content,
      chirp::chat::ChannelType type = chirp::chat::PRIVATE,
      const std::string& channel_id = "", const std::string& reply_to = "",
      int64_t seq = 1) {
    chirp::chat::SendMessageRequest req;
    req.set_sender_id(sender);
    req.set_receiver_id(receiver);
    req.set_channel_type(type);
    req.set_channel_id(channel_id);
    req.set_content(content);
    req.set_reply_to_message_id(reply_to);
    DispatchReq(chirp::gateway::SEND_MESSAGE_REQ, req, session, seq);
    chirp::chat::SendMessageResponse resp;
    const auto frames = FramesOf(*session, chirp::gateway::SEND_MESSAGE_RESP);
    if (!frames.empty()) {
      resp.ParseFromString(frames.back().body());
    } else {
      resp.set_code(chirp::common::SERVER_UNAVAILABLE);
    }
    return resp;
  }

  asio::io_context io_;
  chirp::chat::DeliveryAckManager::Config ack_config_{.timeout_ms = 10000};
  std::shared_ptr<chirp::chat::DeliveryAckManager> acks_;
  std::shared_ptr<MessageStore> store_;
  std::shared_ptr<chirp::network::SessionRegistry> state_;
  std::shared_ptr<std::unordered_set<const chirp::network::Session*>> trusted_;

  // managers (声明序即构造序；handlers 在 SetUp 里建，引用才有效)
  chirp::chat::GroupManager groups_;
  chirp::chat::ReadReceiptManager receipts_;
  chirp::chat::TypingManager typing_{chirp::chat::TypingConfig{}};
  chirp::chat::ReactionManager reactions_;
  chirp::chat::EditConfig edit_config_;
  chirp::chat::MessageEditManager edits_{edit_config_};
  chirp::chat::MentionManager mentions_;

  chirp::chat::PushBridge push_{nullptr};
  chirp::chat::DeliveryPrefs delivery_prefs_;
  chirp::chat::ChannelPacer pacer_;
  chirp::chat::RepeatGuard repeat_;
  chirp::chat::WordFilter word_filter_{chirp::chat::WordFilterOptions{}};
  chirp::common::LoginTokenVerifier verifier_{""};
  chirp::chat::ChatRateLimiter::Config limiter_config_;
  chirp::chat::ChatRateLimiter limiter_{nullptr, limiter_config_};

  chirp::chat::GroupMemberNotifier notify_member_;
  chirp::chat::ChannelMemberResolver resolve_members_;
  chirp::chat::ChannelModeratorChecker is_moderator_;
  chirp::chat::OfflineMessagePurger purge_offline_;
  chirp::chat::RecallTombstoneMarker mark_recalled_;
  std::unique_ptr<chirp::chat::GroupHandlers> group_handlers_;
  std::unique_ptr<chirp::chat::ReadReceiptHandlers> receipt_handlers_;
  std::unique_ptr<chirp::chat::TypingHandlers> typing_handlers_;
  std::unique_ptr<chirp::chat::ReactionHandlers> reaction_handlers_;
  std::unique_ptr<chirp::chat::MessageEditHandlers> edit_handlers_;
  std::unique_ptr<chirp::chat::MentionHandlers> mention_handlers_;
  std::unique_ptr<FeatureHandlers> features_;
};

// --- 心跳 / 垃圾帧 ----------------------------------------------------------

TEST_F(BasicChatTest, HeartbeatPongEchoesTimestampAndDropsGarbage) {
  auto session = std::make_shared<MockSession>();

  chirp::gateway::HeartbeatPing ping;
  ping.set_timestamp(123456);
  DispatchReq(chirp::gateway::HEARTBEAT_PING, ping, session, 7);
  const auto pongs = FramesOf(*session, chirp::gateway::HEARTBEAT_PONG);
  ASSERT_EQ(pongs.size(), 1u);
  chirp::gateway::HeartbeatPong pong;
  ASSERT_TRUE(pong.ParseFromString(pongs[0].body()));
  EXPECT_EQ(pong.timestamp(), 123456);
  EXPECT_GT(pong.server_time(), 0);
  EXPECT_EQ(pongs[0].sequence(), 7);

  // 非 Packet 字节：静默丢弃，不崩不回。
  auto stranger = std::make_shared<MockSession>();
  HandlePacket(store_, state_, *features_, stranger, std::string("\xde\xad\xbe\xef", 4));
  EXPECT_TRUE(stranger->sent.empty());

  // Ping body 解析失败：Warn 后同样无回帧。
  Dispatch(chirp::gateway::HEARTBEAT_PING, std::string("\xde\xad", 2), session);
  EXPECT_EQ(FramesOf(*session, chirp::gateway::HEARTBEAT_PONG).size(), 1u);

  // 未知 msg_id：default 分支静默。
  Dispatch(static_cast<chirp::gateway::MsgID>(9999), "", session);
  EXPECT_EQ(FramesOf(*session).size(), 1u);
}

// --- 内部面信任门（SERVER_AUTH_REQ）------------------------------------------

TEST_F(BasicChatTest, ServerAuthGateIgnoresWithoutSecretAndRejectsBad) {
  auto session = std::make_shared<MockSession>();
  chirp::game_server_gateway::ServerAuthRequest auth;
  auth.set_secret("shh");

  // 无 secret 配置：帧被忽略（直连模式历史行为），不回不关。
  DispatchReq(chirp::gateway::SERVER_AUTH_REQ, auth, session, 11);
  EXPECT_TRUE(session->sent.empty());
  EXPECT_TRUE(trusted_->empty());

  features_->gateway_secret = "shh";
  DispatchReq(chirp::gateway::SERVER_AUTH_REQ, auth, session, 12);
  EXPECT_EQ(trusted_->count(session.get()), 1u);
  const auto oks = FramesOf(*session, chirp::gateway::SERVER_AUTH_RESP);
  ASSERT_EQ(oks.size(), 1u);
  chirp::game_server_gateway::ServerAuthResponse ok;
  ASSERT_TRUE(ok.ParseFromString(oks[0].body()));
  EXPECT_EQ(ok.code(), chirp::common::OK);
  EXPECT_GT(ok.server_time_ms(), 0);
  EXPECT_EQ(oks[0].sequence(), 12);
  EXPECT_FALSE(session->close_after_send);

  // 错误 secret：AUTH_FAILED 回完即关，不进信任集。
  auth.set_secret("nope");
  auto stranger = std::make_shared<MockSession>();
  DispatchReq(chirp::gateway::SERVER_AUTH_REQ, auth, stranger, 13);
  EXPECT_EQ(trusted_->count(stranger.get()), 0u);
  const auto denies = FramesOf(*stranger, chirp::gateway::SERVER_AUTH_RESP);
  ASSERT_EQ(denies.size(), 1u);
  chirp::game_server_gateway::ServerAuthResponse deny;
  ASSERT_TRUE(deny.ParseFromString(denies[0].body()));
  EXPECT_EQ(deny.code(), chirp::common::AUTH_FAILED);
  EXPECT_TRUE(stranger->close_after_send);

  // 垃圾 body：同款 AUTH_FAILED。
  auto garbage = std::make_shared<MockSession>();
  Dispatch(chirp::gateway::SERVER_AUTH_REQ, std::string("\xde\xad\xbe\xef", 4), garbage);
  const auto bads = FramesOf(*garbage, chirp::gateway::SERVER_AUTH_RESP);
  ASSERT_EQ(bads.size(), 1u);
  ASSERT_TRUE(deny.ParseFromString(bads[0].body()));
  EXPECT_EQ(deny.code(), chirp::common::AUTH_FAILED);
}

// --- 登录：脚手架身份 + 顶号 + 清单 + 离线补投 + ack-capable ------------------

TEST_F(BasicChatTest, LoginKicksSamePlatformAndAnnouncesOthers) {
  auto web = std::make_shared<MockSession>();
  const auto first = Login(web, "alice", "tab-1", "web", /*supports_ack=*/true);
  ASSERT_EQ(first.code(), chirp::common::OK);
  EXPECT_EQ(first.user_id(), "alice");
  EXPECT_FALSE(first.session_id().empty());
  EXPECT_TRUE(first.kick_previous());
  EXPECT_EQ(first.online_devices_size(), 0);

  // 跨 platform 共存：web 收到 ios 上线清单事件。
  auto phone = std::make_shared<MockSession>();
  ASSERT_EQ(Login(phone, "alice", "p1", "ios").code(), chirp::common::OK);
  const auto announces = FramesOf(*web, chirp::gateway::DEVICES_PRESENCE_NOTIFY);
  ASSERT_EQ(announces.size(), 1u);
  chirp::auth::DevicesPresenceNotify notify;
  ASSERT_TRUE(notify.ParseFromString(announces[0].body()));
  ASSERT_EQ(notify.devices_size(), 1);
  EXPECT_EQ(notify.devices(0).platform(), "ios");
  EXPECT_TRUE(notify.devices(0).online());

  // 同 platform 重登：旧连接收 KICK_NOTIFY 并被服务端关闭。
  auto web2 = std::make_shared<MockSession>();
  const auto second = Login(web2, "alice", "tab-2", "web");
  ASSERT_EQ(second.code(), chirp::common::OK);
  const auto kicks = FramesOf(*web, chirp::gateway::KICK_NOTIFY);
  ASSERT_EQ(kicks.size(), 1u);
  chirp::auth::KickNotify kick;
  ASSERT_TRUE(kick.ParseFromString(kicks[0].body()));
  EXPECT_EQ(kick.reason(), "logged in on another web");
  EXPECT_TRUE(web->close_after_send);

  // 登录即 ack-capable：补投的离线消息挂 Track，未 ack 前在册。
  chirp::chat::ChatMessage pending;
  pending.set_message_id("m-off");
  pending.set_content("offline refill");
  store_->AddOffline("alice", pending);
  auto relogin = std::make_shared<MockSession>();
  chirp::chat::ChatMessage gone;  // 被顶掉的槽位消息作废重发
  ASSERT_EQ(Login(relogin, "alice", "tab-3", "web", /*supports_ack=*/true).code(),
            chirp::common::OK);
  const auto refills = FramesOf(*relogin, chirp::gateway::CHAT_MESSAGE_NOTIFY);
  ASSERT_EQ(refills.size(), 1u);
  // P1-5:补投副本自带投递主语——delivery_id 非空且不与 message_id 混同;
  // Track 的在册副本序列化含同一 delivery_id(回队重投保原值的前提)。
  chirp::chat::ChatMessage refill_msg;
  ASSERT_TRUE(refill_msg.ParseFromString(refills[0].body()));
  EXPECT_FALSE(refill_msg.delivery_id().empty());
  EXPECT_NE(refill_msg.delivery_id(), "m-off");
  EXPECT_EQ(acks_->pending_count(), 1u);
  EXPECT_TRUE(store_->PopOffline("alice").empty());  // 已弹空，不会二次补投
}

// --- 登录拒绝：限流 / 解析 / JWT / 空 token ------------------------------------

TEST_F(BasicChatTest, OfflineRefillKeepsExistingDeliveryId) {
  // P1-5:已带 delivery_id 的副本(ack 回队重投场景)保留原值——同一次
  // 投递的重投同 id,消费端据此去重;首次补投铸新值见上一用例。
  chirp::chat::ChatMessage kept;
  kept.set_message_id("m-kept");
  kept.set_delivery_id("dlv_kept");
  kept.set_content("kept id");
  store_->AddOffline("alice", kept);
  auto session = std::make_shared<MockSession>();
  ASSERT_EQ(Login(session, "alice", "tab-1", "web").code(), chirp::common::OK);
  const auto refills = FramesOf(*session, chirp::gateway::CHAT_MESSAGE_NOTIFY);
  ASSERT_EQ(refills.size(), 1u);
  chirp::chat::ChatMessage got;
  ASSERT_TRUE(got.ParseFromString(refills[0].body()));
  EXPECT_EQ(got.delivery_id(), "dlv_kept");
}

TEST_F(BasicChatTest, RefillAckWithDeliveryIdClearsPendingExactly) {
  // P1-5 余项：补投副本的 MESSAGE_ACK 显式携带 delivery_id → 精确命中补投
  // 挂起（handler 忘传 delivery_id 时 key 还挂在首投主语上，本用例失败）。
  // 旧客户端空 delivery_id 的兼容匹配见 PrivateSendFansOutTracksAckAnd...。
  chirp::chat::ChatMessage kept;
  kept.set_message_id("m-refill");
  kept.set_delivery_id("dlv_refill");
  kept.set_content("refill ack");
  store_->AddOffline("alice", kept);
  auto session = std::make_shared<MockSession>();
  ASSERT_EQ(Login(session, "alice", "tab-2", "web", /*supports_ack=*/true)
                .code(),
            chirp::common::OK);
  ASSERT_EQ(FramesOf(*session, chirp::gateway::CHAT_MESSAGE_NOTIFY).size(), 1u);
  // 补投已挂起，主语 = dlv_refill。
  EXPECT_EQ(acks_->pending_count(), 1u);

  chirp::chat::MessageAck wrong;
  wrong.set_message_id("m-refill");
  wrong.set_delivery_id("dlv_other");  // 另一笔投递的主语：不命中
  wrong.set_user_id("alice");
  DispatchReq(chirp::gateway::MESSAGE_ACK, wrong, session);
  EXPECT_EQ(acks_->pending_count(), 1u);

  chirp::chat::MessageAck ack;
  ack.set_message_id("m-refill");
  ack.set_delivery_id("dlv_refill");
  ack.set_user_id("alice");
  DispatchReq(chirp::gateway::MESSAGE_ACK, ack, session);
  EXPECT_EQ(acks_->pending_count(), 0u);
}

TEST_F(BasicChatTest, LoginRejectionsCoverLimiterParseJwtAndEmpty) {
  chirp_test::InMemoryRedis redis;
  auto fake = std::make_unique<chirp_test::FakeRedisServer>(
      [&redis](const std::vector<std::string>& args) { return redis.Handle(args); });
  auto redis_client = std::make_shared<chirp::network::RedisClient>("127.0.0.1", fake->port());
  chirp::chat::ChatRateLimiter::Config cfg;
  cfg.max_logins_per_minute_per_ip = 1;
  cfg.window_seconds = 60;
  chirp::chat::ChatRateLimiter limiter(redis_client, cfg);
  features_->rate_limiter = &limiter;
  features_->gateway_secret = "shh";

  auto first = std::make_shared<MockSession>();
  ASSERT_EQ(Login(first, "alice").code(), chirp::common::OK);

  // 同 IP 第二次登录超预算 → RATE_LIMITED。
  auto second = std::make_shared<MockSession>();
  EXPECT_EQ(Login(second, "bob").code(), chirp::common::RATE_LIMITED);

  // SERVER_AUTH 信任过的连接豁免 per-IP 限流（网关扇出语义）。
  chirp::game_server_gateway::ServerAuthRequest auth;
  auth.set_secret("shh");
  auto pipe = std::make_shared<MockSession>();
  DispatchReq(chirp::gateway::SERVER_AUTH_REQ, auth, pipe);
  EXPECT_EQ(Login(pipe, "carol").code(), chirp::common::OK);

  // 后续登录断言与限流无关：切回 fail-open limiter，别让 per-IP 计数吞掉
  // 解析/验签的回码。
  features_->rate_limiter = &limiter_;

  // body 解析失败 → INVALID_PARAM。
  auto garbage = std::make_shared<MockSession>();
  Dispatch(chirp::gateway::LOGIN_REQ, std::string("\xde\xad\xbe\xef", 4), garbage);
  chirp::auth::LoginResponse resp;
  const auto frames = FramesOf(*garbage, chirp::gateway::LOGIN_RESP);
  ASSERT_EQ(frames.size(), 1u);
  ASSERT_TRUE(resp.ParseFromString(frames[0].body()));
  EXPECT_EQ(resp.code(), chirp::common::INVALID_PARAM);

  // 真验签：坏 token → AUTH_FAILED；空 token → AUTH_FAILED（验签失败，非
  // 脚手架的 INVALID_PARAM——verifier enabled 时 Verify("") 必败）。
  chirp::common::LoginTokenVerifier verifier("jwt-s3cret");
  features_->token_verifier = &verifier;
  auto bad = std::make_shared<MockSession>();
  EXPECT_EQ(Login(bad, "not-a-jwt").code(), chirp::common::AUTH_FAILED);
  auto empty = std::make_shared<MockSession>();
  EXPECT_EQ(Login(empty, "").code(), chirp::common::AUTH_FAILED);

  // 脚手架模式（verifier 空 secret）空 token → INVALID_PARAM（user_id 空）。
  features_->token_verifier = &verifier_;
  auto scaffold = std::make_shared<MockSession>();
  EXPECT_EQ(Login(scaffold, "").code(), chirp::common::INVALID_PARAM);
}

// --- 私聊主链：多端扇出 / ack 挂起 / 离线入队 ----------------------------------

TEST_F(BasicChatTest, PrivateSendFansOutTracksAckAndQueuesOffline) {
  auto alice = std::make_shared<MockSession>();
  ASSERT_EQ(Login(alice, "alice").code(), chirp::common::OK);

  // bob 双端在线（跨 platform 共存），其中一端 ack-capable。
  auto bob_phone = std::make_shared<MockSession>();
  auto bob_tablet = std::make_shared<MockSession>();
  ASSERT_EQ(Login(bob_phone, "bob", "p1", "ios", /*supports_ack=*/true).code(),
            chirp::common::OK);
  ASSERT_EQ(Login(bob_tablet, "bob", "p2", "android").code(), chirp::common::OK);

  const auto resp = Send(alice, "alice", "bob", "hi bob");
  ASSERT_EQ(resp.code(), chirp::common::OK);
  EXPECT_FALSE(resp.message_id().empty());

  for (const auto& device : {bob_phone, bob_tablet}) {
    const auto notifies = FramesOf(*device, chirp::gateway::CHAT_MESSAGE_NOTIFY);
    ASSERT_EQ(notifies.size(), 1u);
    chirp::chat::ChatMessage msg;
    ASSERT_TRUE(msg.ParseFromString(notifies[0].body()));
    EXPECT_EQ(msg.content(), "hi bob");
    EXPECT_EQ(msg.channel_id(), "alice|bob");
  }
  // 任一 ack-capable 端即挂起：等 MESSAGE_ACK，超时才回队。
  EXPECT_EQ(acks_->pending_count(), 1u);

  // ack 的空 id / 未认证 / 他人身份三守卫 + 命中清除。
  chirp::chat::MessageAck ack;
  ack.set_message_id(resp.message_id());
  ack.set_user_id("bob");
  auto stranger = std::make_shared<MockSession>();  // 未认证：忽略
  DispatchReq(chirp::gateway::MESSAGE_ACK, ack, stranger);
  EXPECT_EQ(acks_->pending_count(), 1u);

  chirp::chat::MessageAck foreign;
  foreign.set_message_id(resp.message_id());
  foreign.set_user_id("mallory");
  DispatchReq(chirp::gateway::MESSAGE_ACK, foreign, bob_phone);
  EXPECT_EQ(acks_->pending_count(), 1u);

  DispatchReq(chirp::gateway::MESSAGE_ACK, ack, bob_phone);
  EXPECT_EQ(acks_->pending_count(), 0u);

  // bob 全端半关：按离线处理（TARGET_OFFLINE + 入队 + push 通知钩子）。
  bob_phone->half_closed = true;
  bob_tablet->half_closed = true;
  const auto offline_resp = Send(alice, "alice", "bob", "while away");
  EXPECT_EQ(offline_resp.code(), chirp::common::TARGET_OFFLINE);
  const auto queued = store_->PopOffline("bob");
  ASSERT_EQ(queued.size(), 1u);
  EXPECT_EQ(queued[0].content(), "while away");

  // 发送校验：未认证 / sender 与认证身份不符。
  auto anon = std::make_shared<MockSession>();
  EXPECT_EQ(Send(anon, "alice", "bob", "x").code(), chirp::common::AUTH_FAILED);
  EXPECT_EQ(Send(alice, "mallory", "bob", "x").code(), chirp::common::AUTH_FAILED);
  // 私聊缺接收方 / 发给自己：INVALID_PARAM（basic 校验拒 self-DM）。
  EXPECT_EQ(Send(alice, "alice", "", "x").code(), chirp::common::INVALID_PARAM);
  EXPECT_EQ(Send(alice, "alice", "alice", "x").code(), chirp::common::INVALID_PARAM);

  // ack 管理器缺席的守卫臂（生产装配恒有，Track 直接跳过）：bob 双端恢复
  // 在线，投递照常 OK，不产生挂起。
  features_->acks = nullptr;
  bob_phone->half_closed = false;
  bob_tablet->half_closed = false;
  EXPECT_EQ(Send(alice, "alice", "bob", "no acks wired").code(), chirp::common::OK);
  EXPECT_EQ(acks_->pending_count(), 0u);
  features_->acks = acks_.get();
}

// --- 发送守门链：限流 / 节奏 / 重复禁言 / @everyone 冷却 ------------------------

TEST_F(BasicChatTest, SendGuardChainRejectsInOrder) {
  chirp_test::InMemoryRedis redis;
  auto fake = std::make_unique<chirp_test::FakeRedisServer>(
      [&redis](const std::vector<std::string>& args) { return redis.Handle(args); });
  auto redis_client = std::make_shared<chirp::network::RedisClient>("127.0.0.1", fake->port());
  chirp::chat::ChatRateLimiter::Config cfg;
  cfg.max_sends_per_minute_per_user = 1;
  cfg.window_seconds = 60;
  chirp::chat::ChatRateLimiter limiter(redis_client, cfg);
  features_->rate_limiter = &limiter;

  auto alice = std::make_shared<MockSession>();
  ASSERT_EQ(Login(alice, "alice").code(), chirp::common::OK);

  // 第 1 条消耗掉唯一发送预算（WORLD 非群成员 → AUTH_FAILED，但预算已耗）。
  EXPECT_EQ(Send(alice, "alice", "", "world one", chirp::chat::WORLD, "world").code(),
            chirp::common::AUTH_FAILED);
  // 第 2 条撞每用户模糊闸 → RATE_LIMITED。
  EXPECT_EQ(Send(alice, "alice", "bob", "budget gone").code(),
            chirp::common::RATE_LIMITED);

  // 节奏限流：换无 Redis 预算的默认 limiter（fail-open）+ 自装真 pacer，
  // WORLD 连发两条，第二条撞 5s 窗 → RATE_LIMITED（auth/param 失败不耗节奏，
  // 第一条已过校验、pacer 锚定在最后放行的发送）。
  features_->rate_limiter = &limiter_;
  features_->channel_pacer = &pacer_;
  EXPECT_EQ(Send(alice, "alice", "", "world one", chirp::chat::WORLD, "world").code(),
            chirp::common::AUTH_FAILED);
  EXPECT_EQ(Send(alice, "alice", "", "world two", chirp::chat::WORLD, "world").code(),
            chirp::common::RATE_LIMITED);

  // @everyone 冷却：群成员的非私聊发送（TEAM 不设节奏，两条连发不撞 pacer），
  // 第一条记录 everyone 使用，紧接的第二条在 60s 冷却内 → AUTH_FAILED
  // （mention 守卫在广播之前）。必须在重复禁言段之前跑：禁言是用户级 5 分钟
  // 封印，会吞掉后续一切发送。
  chirp::chat::CreateGroupRequest create;
  create.set_creator_id("alice");
  create.set_group_name("g");
  create.add_initial_members("alice");
  DispatchReq(chirp::gateway::CREATE_GROUP_REQ, create, alice);
  chirp::chat::CreateGroupResponse create_resp;
  ASSERT_TRUE(create_resp.ParseFromString(
      FramesOf(*alice, chirp::gateway::CREATE_GROUP_RESP).back().body()));
  ASSERT_EQ(create_resp.code(), chirp::common::OK);
  EXPECT_EQ(Send(alice, "alice", "", "@everyone rally", chirp::chat::TEAM,
                create_resp.group_id()).code(),
            chirp::common::OK);
  EXPECT_EQ(Send(alice, "alice", "", "@everyone again", chirp::chat::TEAM,
                create_resp.group_id()).code(),
            chirp::common::AUTH_FAILED);

  // 重复禁言：TEAM 不设节奏，连续三条相同内容，第三条起拒（触发条也拒）。
  for (int i = 0; i < 3; ++i) {
    const auto code =
        Send(alice, "alice", "", "spam", chirp::chat::TEAM, "team-1").code();
    if (i < 2) {
      EXPECT_EQ(code, chirp::common::AUTH_FAILED);  // 非群成员，但守门链已过
    } else {
      EXPECT_EQ(code, chirp::common::RATE_LIMITED);  // 第 3 条撞禁言
    }
  }
}

// --- 敏感词：replace 改写投递 / reject 专码拒收 --------------------------------

TEST_F(BasicChatTest, WordFilterReplacesOrRejectsContent) {
  const auto lexicon = std::filesystem::temp_directory_path() /
                       ("chirp_wf_" + std::to_string(::getpid()) + "_b10.txt");
  {
    std::ofstream out(lexicon);
    out << "badword\n";
  }

  auto alice = std::make_shared<MockSession>();
  auto bob = std::make_shared<MockSession>();
  ASSERT_EQ(Login(alice, "alice").code(), chirp::common::OK);
  ASSERT_EQ(Login(bob, "bob").code(), chirp::common::OK);

  // REJECT：专码 WORD_FILTERED，消息不落库不投递。
  chirp::chat::WordFilterOptions reject_opts;
  reject_opts.lexicon_path = lexicon.string();
  reject_opts.policy = chirp::chat::WordFilterPolicy::kReject;
  chirp::chat::WordFilter rejector(reject_opts);
  features_->word_filter = &rejector;
  EXPECT_EQ(Send(alice, "alice", "bob", "has badword inside").code(),
            chirp::common::WORD_FILTERED);
  EXPECT_EQ(FramesOf(*bob, chirp::gateway::CHAT_MESSAGE_NOTIFY).size(), 0u);

  // REPLACE：命中区段折叠为掩码后照常投递。
  chirp::chat::WordFilterOptions replace_opts;
  replace_opts.lexicon_path = lexicon.string();
  replace_opts.policy = chirp::chat::WordFilterPolicy::kReplace;
  chirp::chat::WordFilter replacer(replace_opts);
  features_->word_filter = &replacer;
  ASSERT_EQ(Send(alice, "alice", "bob", "has badword inside").code(),
            chirp::common::OK);
  const auto notifies = FramesOf(*bob, chirp::gateway::CHAT_MESSAGE_NOTIFY);
  ASSERT_EQ(notifies.size(), 1u);
  chirp::chat::ChatMessage msg;
  ASSERT_TRUE(msg.ParseFromString(notifies[0].body()));
  EXPECT_EQ(msg.content().find("badword"), std::string::npos);
  EXPECT_NE(msg.content().find("has"), std::string::npos);

  std::filesystem::remove(lexicon);
  features_->word_filter = &word_filter_;
}

// --- 词库下发：fetch 守卫 / 条件 GET / 热更新广播 ---------------------------------

TEST_F(BasicChatTest, WordFilterFetchGuardsAndEmptyServerAnswer) {
  auto alice = std::make_shared<MockSession>();
  auto anon = std::make_shared<MockSession>();
  ASSERT_EQ(Login(alice, "alice").code(), chirp::common::OK);

  // 垃圾 body → INVALID_PARAM；未认证 → AUTH_FAILED（对齐 2239-2244 守卫惯例）。
  Dispatch(chirp::gateway::WORD_FILTER_FETCH_REQ, std::string("\xde\xad", 2), alice);
  DispatchReq(chirp::gateway::WORD_FILTER_FETCH_REQ,
              chirp::chat::WordFilterFetchRequest{}, anon);
  {
    const auto frames = FramesOf(*alice, chirp::gateway::WORD_FILTER_FETCH_RESP);
    ASSERT_EQ(frames.size(), 1u);
    chirp::chat::WordFilterFetchResponse resp;
    ASSERT_TRUE(resp.ParseFromString(frames[0].body()));
    EXPECT_EQ(resp.code(), chirp::common::INVALID_PARAM);
  }
  {
    const auto frames = FramesOf(*anon, chirp::gateway::WORD_FILTER_FETCH_RESP);
    ASSERT_EQ(frames.size(), 1u);
    chirp::chat::WordFilterFetchResponse resp;
    ASSERT_TRUE(resp.ParseFromString(frames[0].body()));
    EXPECT_EQ(resp.code(), chirp::common::AUTH_FAILED);
  }

  // 未装配词库：version 0 / enabled false / 空文本（客户端语义=清空本地词库）。
  DispatchReq(chirp::gateway::WORD_FILTER_FETCH_REQ,
              chirp::chat::WordFilterFetchRequest{}, alice);
  {
    const auto frames = FramesOf(*alice, chirp::gateway::WORD_FILTER_FETCH_RESP);
    ASSERT_EQ(frames.size(), 2u);
    chirp::chat::WordFilterFetchResponse resp;
    ASSERT_TRUE(resp.ParseFromString(frames[1].body()));
    EXPECT_EQ(resp.code(), chirp::common::OK);
    EXPECT_EQ(resp.lexicon().version(), 0);
    EXPECT_FALSE(resp.lexicon().enabled());
    EXPECT_EQ(resp.lexicon().lexicon(), "");
  }
}

TEST_F(BasicChatTest, WordFilterFetchFullLexiconThenConditionalGet) {
  auto alice = std::make_shared<MockSession>();
  ASSERT_EQ(Login(alice, "alice").code(), chirp::common::OK);

  const auto lexicon = std::filesystem::temp_directory_path() /
                       ("chirp_wf_fetch_" + std::to_string(::getpid()) + ".txt");
  {
    std::ofstream out(lexicon);
    out << "Second\nbadword\n";
  }

  chirp::chat::WordFilterOptions opts;
  opts.lexicon_path = lexicon.string();
  opts.reload_check_interval_ms = 60'000;  // 测试期间文件不变，不触发重载
  chirp::chat::WordFilter loaded(opts);
  features_->word_filter = &loaded;

  // 首拉（known_version 0）→ 全量文本，规范化（小写、去重排序）。
  chirp::chat::WordFilterFetchRequest req;
  req.set_known_version(0);
  DispatchReq(chirp::gateway::WORD_FILTER_FETCH_REQ, req, alice, /*seq=*/7);
  {
    const auto frames = FramesOf(*alice, chirp::gateway::WORD_FILTER_FETCH_RESP);
    ASSERT_EQ(frames.size(), 1u);
    EXPECT_EQ(frames[0].sequence(), 7);
    chirp::chat::WordFilterFetchResponse resp;
    ASSERT_TRUE(resp.ParseFromString(frames[0].body()));
    EXPECT_EQ(resp.code(), chirp::common::OK);
    EXPECT_EQ(resp.lexicon().version(), 1);
    EXPECT_TRUE(resp.lexicon().enabled());
    EXPECT_EQ(resp.lexicon().policy(), chirp::chat::WORD_FILTER_POLICY_REPLACE);
    EXPECT_EQ(resp.lexicon().replacement(), "**");
    EXPECT_EQ(resp.lexicon().lexicon(), "badword\nsecond\n");
  }

  // 条件 GET 命中（known_version == version）→ 文本省略，其余元数据照常。
  req.set_known_version(1);
  DispatchReq(chirp::gateway::WORD_FILTER_FETCH_REQ, req, alice, /*seq=*/8);
  {
    const auto frames = FramesOf(*alice, chirp::gateway::WORD_FILTER_FETCH_RESP);
    ASSERT_EQ(frames.size(), 2u);
    chirp::chat::WordFilterFetchResponse resp;
    ASSERT_TRUE(resp.ParseFromString(frames[1].body()));
    EXPECT_EQ(frames[1].sequence(), 8);
    EXPECT_EQ(resp.code(), chirp::common::OK);
    EXPECT_EQ(resp.lexicon().version(), 1);
    EXPECT_EQ(resp.lexicon().lexicon(), "");
  }

  std::filesystem::remove(lexicon);
  features_->word_filter = &word_filter_;
}

TEST_F(BasicChatTest, WordFilterHotReloadBroadcastsToLiveSessions) {
  auto alice = std::make_shared<MockSession>();
  auto bob = std::make_shared<MockSession>();
  auto anon = std::make_shared<MockSession>();  // 从不登录：不在 registry
  ASSERT_EQ(Login(alice, "alice").code(), chirp::common::OK);
  ASSERT_EQ(Login(bob, "bob").code(), chirp::common::OK);

  const auto lexicon = std::filesystem::temp_directory_path() /
                       ("chirp_wf_reload_" + std::to_string(::getpid()) + ".txt");
  {
    std::ofstream out(lexicon);
    out << "first\n";
  }

  chirp::chat::WordFilterOptions opts;
  opts.lexicon_path = lexicon.string();
  opts.reload_check_interval_ms = 0;  // 每次发送都检查 mtime
  chirp::chat::WordFilter loaded(opts);
  features_->word_filter = &loaded;
  // 与生产 main 同款接线：每次装载广播全量词库给全部活会话。
  loaded.set_on_reload([this, &loaded](int64_t) {
    chirp::chat::BroadcastWordFilterUpdate(state_, loaded);
  });

  // 改写词库文件；下一次发送（Filter→ReloadIfStale→LoadLexicon→on_reload）
  // 触发广播。mtime 显式设到远处，摆脱文件系统时间戳粒度（与
  // word_filter_test 的 TouchNewMtime 同款手法）。
  {
    std::ofstream out(lexicon);
    out << "first\nsecond\n";
  }
  struct timespec times[2];
  times[0].tv_sec = 0;
  times[0].tv_nsec = UTIME_NOW;
  times[1].tv_sec = 1'700'000'000;
  times[1].tv_nsec = 0;
  ASSERT_EQ(::utimensat(AT_FDCWD, lexicon.c_str(), times, 0), 0);
  EXPECT_EQ(Send(alice, "alice", "bob", "clean words only").code(),
            chirp::common::OK);
  EXPECT_EQ(loaded.version(), 2);
  EXPECT_EQ(loaded.word_count(), 2u);

  for (const auto* session : {alice.get(), bob.get()}) {
    const auto frames = FramesOf(*session, chirp::gateway::WORD_FILTER_UPDATE_NOTIFY);
    ASSERT_EQ(frames.size(), 1u);
    EXPECT_EQ(frames[0].sequence(), 0);  // 推送 seq 恒 0
    chirp::chat::WordFilterUpdateNotify notify;
    ASSERT_TRUE(notify.ParseFromString(frames[0].body()));
    EXPECT_EQ(notify.lexicon().version(), 2);
    EXPECT_TRUE(notify.lexicon().enabled());
    EXPECT_EQ(notify.lexicon().lexicon(), "first\nsecond\n");
  }
  // 未登录连接不在 registry，收不到推送。
  EXPECT_TRUE(FramesOf(*anon, chirp::gateway::WORD_FILTER_UPDATE_NOTIFY).empty());

  std::filesystem::remove(lexicon);
  features_->word_filter = &word_filter_;
}

// --- 私聊边界：拉黑静默 / NPC 改道 / 引用校验 -----------------------------------

TEST_F(BasicChatTest, PrivateSendEdgesBlockedNpcDanglingReplyValidReply) {
  auto alice = std::make_shared<MockSession>();
  auto bob = std::make_shared<MockSession>();
  ASSERT_EQ(Login(alice, "alice").code(), chirp::common::OK);
  ASSERT_EQ(Login(bob, "bob").code(), chirp::common::OK);

  // 拉黑：bob 拉黑 alice，发送方照拿 OK，零投递零离线。
  ASSERT_TRUE(delivery_prefs_.BlockUser("bob", "alice"));
  EXPECT_EQ(Send(alice, "alice", "bob", "shadowed").code(), chirp::common::OK);
  EXPECT_EQ(FramesOf(*bob, chirp::gateway::CHAT_MESSAGE_NOTIFY).size(), 0u);
  EXPECT_TRUE(store_->PopOffline("bob").empty());
  delivery_prefs_.UnblockUser("bob", "alice");

  // NPC 接收方：改道上行为事件（hub peer 未 Start → 发布回调 fail-fast 只留
  // Warn），玩家投递/离线整体跳过，受理即 OK。
  auto npc_peer = chirp::network::ServerGatewayPeer::Create(
      io_, chirp::network::ServerGatewayPeer::Options{}, nullptr);
  features_->hub_peer = npc_peer.get();
  features_->npc_service_id = "npc-dialog";
  EXPECT_EQ(Send(alice, "alice", "npc:merchant", "hello smith").code(),
            chirp::common::OK);
  io_.poll();  // strand 上排队的发布回调
  EXPECT_TRUE(store_->PopOffline("npc:merchant").empty());
  features_->hub_peer = nullptr;
  features_->npc_service_id.clear();

  // 悬空引用：reply_to 不在本会话历史 → INVALID_PARAM。
  EXPECT_EQ(Send(alice, "alice", "bob", "reply", chirp::chat::PRIVATE, "",
                 "no-such-message").code(),
            chirp::common::INVALID_PARAM);

  // 引用存在的消息：放行。
  const auto first = Send(alice, "alice", "bob", "first");
  ASSERT_EQ(first.code(), chirp::common::OK);
  EXPECT_EQ(Send(alice, "alice", "bob", "second", chirp::chat::PRIVATE, "",
                 first.message_id()).code(),
            chirp::common::OK);

  // body 解析失败 → INVALID_PARAM。
  Dispatch(chirp::gateway::SEND_MESSAGE_REQ, std::string("\xde\xad", 2), alice);
  const auto frames = FramesOf(*alice, chirp::gateway::SEND_MESSAGE_RESP);
  ASSERT_FALSE(frames.empty());
  chirp::chat::SendMessageResponse resp;
  ASSERT_TRUE(resp.ParseFromString(frames.back().body()));
  EXPECT_EQ(resp.code(), chirp::common::INVALID_PARAM);
}

// --- 群组频道：成员广播 + 离线成员入队 + 非成员拒收 ------------------------------

TEST_F(BasicChatTest, GroupSendBroadcastsToMembersAndRefusesNonMember) {
  auto alice = std::make_shared<MockSession>();
  auto carol = std::make_shared<MockSession>();
  ASSERT_EQ(Login(alice, "alice").code(), chirp::common::OK);
  ASSERT_EQ(Login(carol, "carol").code(), chirp::common::OK);

  // bob 从不登录（离线成员）；建群带 alice + bob。发送者本人被广播排除
  // （成员解析 exclude=sender），alice 不该收到自己的消息。
  chirp::chat::CreateGroupRequest create;
  create.set_creator_id("alice");
  create.set_group_name("room");
  create.add_initial_members("alice");
  create.add_initial_members("bob");
  DispatchReq(chirp::gateway::CREATE_GROUP_REQ, create, alice);
  const auto created = FramesOf(*alice, chirp::gateway::CREATE_GROUP_RESP);
  ASSERT_EQ(created.size(), 1u);
  chirp::chat::CreateGroupResponse create_resp;
  ASSERT_TRUE(create_resp.ParseFromString(created[0].body()));
  ASSERT_EQ(create_resp.code(), chirp::common::OK);
  const std::string group_id = create_resp.group_id();

  // 成员发送：发送者无自回环 notify，离线成员进离线队列。
  const auto resp = Send(alice, "alice", "", "group hello", chirp::chat::TEAM, group_id);
  EXPECT_EQ(resp.code(), chirp::common::OK);
  EXPECT_EQ(FramesOf(*alice, chirp::gateway::CHAT_MESSAGE_NOTIFY).size(), 0u);
  const auto queued = store_->PopOffline("bob");
  ASSERT_EQ(queued.size(), 1u);
  EXPECT_EQ(queued[0].content(), "group hello");

  // 非成员发送：AUTH_FAILED。
  EXPECT_EQ(Send(carol, "carol", "", "intruder", chirp::chat::TEAM, group_id).code(),
            chirp::common::AUTH_FAILED);
}

// --- 跨平面：游戏前缀回复 + 好友私聊镜像 + spoke 上行（真 hub/link） ------------

TEST_F(BasicChatTest, CrossPlaneReplyFriendRelayAndSpokeUplink) {
  // hub+link 独占本地 io（与 fixture 的 io 隔离）。
  asio::io_context io;
  chirp::network::ChatPeerHub::Options hub_opts;
  hub_opts.allowed_peers["game_chat"] = "peer-s3cret";
  hub_opts.heartbeat_interval_seconds = 30;
  std::mutex plane_mu;
  std::vector<chirp::gateway::ChannelMessageNotify> uplinks;
  auto hub = chirp::network::ChatPeerHub::Create(
      io, hub_opts,
      [](const std::string&, const std::string&, int32_t,
         const std::vector<chirp::gateway::PeerCapability>&) {},
      [](const std::string&, const std::string&) {},
      [&plane_mu, &uplinks](const std::string& /*service_id*/,
                            const chirp::gateway::ChannelMessageNotify& notify) {
        std::lock_guard<std::mutex> lock(plane_mu);
        uplinks.push_back(notify);
      });
  hub->Start();
  auto watchdog = std::make_shared<asio::steady_timer>(io);
  watchdog->expires_after(std::chrono::seconds(30));
  watchdog->async_wait([&io](const std::error_code&) { io.stop(); });
  std::thread runner([&] { io.run(); });

  chirp::network::ChatPeerLink::Options link_opts;
  link_opts.host = "127.0.0.1";
  link_opts.port = hub->port();
  link_opts.service_id = "game_chat";
  link_opts.secret = "peer-s3cret";
  link_opts.game_id = "game42";
  link_opts.reconnect_delay_seconds = 1;
  std::mutex inject_mu;
  std::vector<chirp::gateway::PeerInjectMessageNotify> injects;
  auto link = chirp::network::ChatPeerLink::Create(
      io, link_opts, [](int32_t, const std::vector<chirp::gateway::PeerCapability>&) {},
      [](const chirp::gateway::ChannelMessageNotify&) {},
      [&inject_mu, &injects](const chirp::gateway::PeerInjectMessageNotify& notify) {
        std::lock_guard<std::mutex> lock(inject_mu);
        injects.push_back(notify);
      });
  link->Start();
  auto registered_on_io = [&] {
    auto task = std::make_shared<std::packaged_task<bool()>>([&] { return link->registered(); });
    asio::post(io, [task] { (*task)(); });
    return task->get_future().get();
  };
  ASSERT_TRUE(WaitFor(registered_on_io));

  chirp::chat::PlayerDirectory directory(chirp::chat::PlayerDirectory::Options{});
  chirp::game_server_gateway::BindPlayerIdentityRequest bind;
  bind.set_binding_id("b1");
  bind.set_player_id("alice");
  bind.set_game_id("game42");
  bind.set_game_user_id("u-1");
  ASSERT_EQ(directory.HandleBindPlayerIdentity(bind).code(), chirp::common::OK);
  features_->directory = &directory;
  features_->hub = hub.get();

  auto alice = std::make_shared<MockSession>();
  ASSERT_EQ(Login(alice, "alice").code(), chirp::common::OK);

  // 游戏前缀回复：绑定的发送者 → kSent → OK，注入到注册的 spoke。
  EXPECT_EQ(Send(alice, "alice", "", "in game reply", chirp::chat::GUILD,
                "game42:guild_1").code(),
            chirp::common::OK);
  ASSERT_TRUE(WaitFor([&] {
    std::lock_guard<std::mutex> lock(inject_mu);
    return injects.size() >= 1u;
  }));
  {
    std::lock_guard<std::mutex> lock(inject_mu);
    EXPECT_EQ(injects[0].channel_id(), "guild_1");
    EXPECT_EQ(injects[0].content(), "in game reply");
  }

  // 未绑定发送者 → INVALID_PARAM；无在线 spoke 的游戏 → SERVER_UNAVAILABLE。
  auto carol = std::make_shared<MockSession>();
  ASSERT_EQ(Login(carol, "carol").code(), chirp::common::OK);
  EXPECT_EQ(Send(carol, "carol", "", "x", chirp::chat::GUILD, "game42:guild_1").code(),
            chirp::common::INVALID_PARAM);
  EXPECT_EQ(Send(alice, "alice", "", "x", chirp::chat::GUILD, "game99:guild_1").code(),
            chirp::common::SERVER_UNAVAILABLE);

  // 好友私聊镜像：bob 绑定 game42，alice→bob 私聊副本注入 spoke（channel=
  // bob 的游戏身份）。bob 同时本地在线，收正常 notify。
  chirp::game_server_gateway::BindPlayerIdentityRequest bind_bob;
  bind_bob.set_binding_id("b2");
  bind_bob.set_player_id("bob");
  bind_bob.set_game_id("game42");
  bind_bob.set_game_user_id("u-2");
  ASSERT_EQ(directory.HandleBindPlayerIdentity(bind_bob).code(), chirp::common::OK);
  auto bob = std::make_shared<MockSession>();
  ASSERT_EQ(Login(bob, "bob").code(), chirp::common::OK);
  ASSERT_EQ(Send(alice, "alice", "bob", "hi in game too").code(), chirp::common::OK);
  ASSERT_TRUE(WaitFor([&] {
    std::lock_guard<std::mutex> lock(inject_mu);
    return injects.size() >= 2u;
  }));
  {
    std::lock_guard<std::mutex> lock(inject_mu);
    EXPECT_EQ(injects[1].channel_id(), "u-2");
    EXPECT_EQ(injects[1].content(), "hi in game too");
  }
  EXPECT_EQ(FramesOf(*bob, chirp::gateway::CHAT_MESSAGE_NOTIFY).size(), 1u);

  // spoke 上行：注册的 link + 非私聊 OK 发送 → CHANNEL_MESSAGE_NOTIFY 上 hub。
  features_->spoke = link;
  features_->spoke_game_id = "game42";
  chirp::chat::CreateGroupRequest create;
  create.set_creator_id("alice");
  create.set_group_name("g");
  create.add_initial_members("alice");
  DispatchReq(chirp::gateway::CREATE_GROUP_REQ, create, alice);
  chirp::chat::CreateGroupResponse create_resp;
  const auto created = FramesOf(*alice, chirp::gateway::CREATE_GROUP_RESP);
  ASSERT_TRUE(create_resp.ParseFromString(created.back().body()));
  ASSERT_EQ(Send(alice, "alice", "", "guild up", chirp::chat::GUILD,
                create_resp.group_id()).code(),
            chirp::common::OK);
  ASSERT_TRUE(WaitFor([&] {
    std::lock_guard<std::mutex> lock(plane_mu);
    return uplinks.size() == 1u;
  }));
  {
    std::lock_guard<std::mutex> lock(plane_mu);
    EXPECT_EQ(uplinks[0].game_id(), "game42");
    ASSERT_TRUE(uplinks[0].has_message());
    EXPECT_EQ(uplinks[0].message().content(), "guild up");
  }

  features_->spoke = nullptr;
  features_->directory = nullptr;
  features_->hub = nullptr;
  link->Stop();
  hub->Stop();
  watchdog->cancel();
  io.stop();
  runner.join();
}

// --- 频道屏蔽 / 黑名单 RPC 面 ---------------------------------------------------

TEST_F(BasicChatTest, MuteAndBlockRpcContract) {
  auto alice = std::make_shared<MockSession>();
  ASSERT_EQ(Login(alice, "alice").code(), chirp::common::OK);
  auto anon = std::make_shared<MockSession>();

  // SET_CHANNEL_MUTE：垃圾 body / 未认证 / 不可屏蔽频道（PRIVATE）/ 正常。
  chirp::chat::SetChannelMuteRequest mute;
  mute.set_channel_type(chirp::chat::WORLD);
  mute.set_muted(true);
  Dispatch(chirp::gateway::SET_CHANNEL_MUTE_REQ, std::string("\xde\xad", 2), anon);
  DispatchReq(chirp::gateway::SET_CHANNEL_MUTE_REQ, mute, anon);
  {
    const auto frames = FramesOf(*anon, chirp::gateway::SET_CHANNEL_MUTE_RESP);
    ASSERT_EQ(frames.size(), 2u);
    chirp::chat::SetChannelMuteResponse resp;
    ASSERT_TRUE(resp.ParseFromString(frames[0].body()));
    EXPECT_EQ(resp.code(), chirp::common::INVALID_PARAM);
    ASSERT_TRUE(resp.ParseFromString(frames[1].body()));
    EXPECT_EQ(resp.code(), chirp::common::AUTH_FAILED);
  }
  chirp::chat::SetChannelMuteRequest private_mute;
  private_mute.set_channel_type(chirp::chat::PRIVATE);
  DispatchReq(chirp::gateway::SET_CHANNEL_MUTE_REQ, private_mute, alice);
  DispatchReq(chirp::gateway::SET_CHANNEL_MUTE_REQ, mute, alice);
  {
    const auto frames = FramesOf(*alice, chirp::gateway::SET_CHANNEL_MUTE_RESP);
    ASSERT_EQ(frames.size(), 2u);
    chirp::chat::SetChannelMuteResponse bad, ok;
    ASSERT_TRUE(bad.ParseFromString(frames[0].body()));
    EXPECT_EQ(bad.code(), chirp::common::INVALID_PARAM);
    ASSERT_TRUE(ok.ParseFromString(frames[1].body()));
    EXPECT_EQ(ok.code(), chirp::common::OK);
    EXPECT_TRUE(ok.muted());
  }

  // GET_CHANNEL_MUTES：未认证 / 三频道全量上报（WORLD 已置 muted）。
  DispatchReq(chirp::gateway::GET_CHANNEL_MUTES_REQ,
              chirp::chat::GetChannelMutesRequest{}, anon);
  DispatchReq(chirp::gateway::GET_CHANNEL_MUTES_REQ,
              chirp::chat::GetChannelMutesRequest{}, alice);
  {
    const auto frames = FramesOf(*alice, chirp::gateway::GET_CHANNEL_MUTES_RESP);
    ASSERT_EQ(frames.size(), 1u);
    chirp::chat::GetChannelMutesResponse resp;
    ASSERT_TRUE(resp.ParseFromString(frames[0].body()));
    EXPECT_EQ(resp.code(), chirp::common::OK);
    EXPECT_EQ(resp.states_size(), 3);
    EXPECT_EQ(resp.states(0).muted(), true);  // WORLD 按序第一
  }

  // BLOCK：空目标 INVALID_PARAM / 自拉黑 INVALID_PARAM / 正常 OK。
  chirp::chat::BlockMessageSenderRequest block;
  block.set_target_user_id("");
  DispatchReq(chirp::gateway::BLOCK_MESSAGE_SENDER_REQ, block, alice);
  block.set_target_user_id("alice");
  DispatchReq(chirp::gateway::BLOCK_MESSAGE_SENDER_REQ, block, alice);
  block.set_target_user_id("eve");
  DispatchReq(chirp::gateway::BLOCK_MESSAGE_SENDER_REQ, block, alice);
  {
    const auto frames = FramesOf(*alice, chirp::gateway::BLOCK_MESSAGE_SENDER_RESP);
    ASSERT_EQ(frames.size(), 3u);
    chirp::chat::BlockMessageSenderResponse resp;
    ASSERT_TRUE(resp.ParseFromString(frames[0].body()));
    EXPECT_EQ(resp.code(), chirp::common::INVALID_PARAM);
    ASSERT_TRUE(resp.ParseFromString(frames[1].body()));
    EXPECT_EQ(resp.code(), chirp::common::INVALID_PARAM);
    ASSERT_TRUE(resp.ParseFromString(frames[2].body()));
    EXPECT_EQ(resp.code(), chirp::common::OK);
  }

  // BLOCK 的垃圾 body（parse-first 回 INVALID_PARAM）与未认证臂。
  Dispatch(chirp::gateway::BLOCK_MESSAGE_SENDER_REQ, std::string("\xde\xad", 2), anon);
  chirp::chat::BlockMessageSenderRequest anon_block;
  anon_block.set_target_user_id("eve");
  DispatchReq(chirp::gateway::BLOCK_MESSAGE_SENDER_REQ, anon_block, anon);
  {
    const auto frames = FramesOf(*anon, chirp::gateway::BLOCK_MESSAGE_SENDER_RESP);
    ASSERT_EQ(frames.size(), 2u);
    chirp::chat::BlockMessageSenderResponse resp;
    ASSERT_TRUE(resp.ParseFromString(frames[0].body()));
    EXPECT_EQ(resp.code(), chirp::common::INVALID_PARAM);
    ASSERT_TRUE(resp.ParseFromString(frames[1].body()));
    EXPECT_EQ(resp.code(), chirp::common::AUTH_FAILED);
  }

  // UNBLOCK 幂等（解未拉黑者亦 OK）+ GET_BLOCKED 列表。
  chirp::chat::UnblockMessageSenderRequest unblock;
  unblock.set_target_user_id("nobody");
  DispatchReq(chirp::gateway::UNBLOCK_MESSAGE_SENDER_REQ, unblock, alice);
  Dispatch(chirp::gateway::UNBLOCK_MESSAGE_SENDER_REQ, std::string("\xde\xad", 2), anon);
  DispatchReq(chirp::gateway::UNBLOCK_MESSAGE_SENDER_REQ, unblock, anon);
  DispatchReq(chirp::gateway::GET_BLOCKED_SENDERS_REQ,
              chirp::chat::GetBlockedSendersRequest{}, anon);
  DispatchReq(chirp::gateway::GET_BLOCKED_SENDERS_REQ,
              chirp::chat::GetBlockedSendersRequest{}, alice);
  {
    const auto frames = FramesOf(*alice, chirp::gateway::GET_BLOCKED_SENDERS_RESP);
    ASSERT_EQ(frames.size(), 1u);
    chirp::chat::GetBlockedSendersResponse resp;
    ASSERT_TRUE(resp.ParseFromString(frames[0].body()));
    EXPECT_EQ(resp.code(), chirp::common::OK);
    ASSERT_EQ(resp.target_user_ids_size(), 1);
    EXPECT_EQ(resp.target_user_ids(0), "eve");
  }
  {
    const auto frames = FramesOf(*anon, chirp::gateway::UNBLOCK_MESSAGE_SENDER_RESP);
    ASSERT_EQ(frames.size(), 2u);
    chirp::chat::UnblockMessageSenderResponse resp;
    ASSERT_TRUE(resp.ParseFromString(frames[0].body()));
    EXPECT_EQ(resp.code(), chirp::common::INVALID_PARAM);  // 垃圾 body（parse-first）
    ASSERT_TRUE(resp.ParseFromString(frames[1].body()));
    EXPECT_EQ(resp.code(), chirp::common::AUTH_FAILED);  // 未认证
  }

  // UNBLOCK 空目标：UnblockUser 拒空串 → INVALID_PARAM（幂等 OK 之外唯一
  // 的失败臂）。
  chirp::chat::UnblockMessageSenderRequest empty_target;
  empty_target.set_target_user_id("");
  DispatchReq(chirp::gateway::UNBLOCK_MESSAGE_SENDER_REQ, empty_target, alice);
  {
    const auto frames = FramesOf(*alice, chirp::gateway::UNBLOCK_MESSAGE_SENDER_RESP);
    ASSERT_EQ(frames.size(), 2u);  // [0] 幂等 OK（"nobody"），[1] 本次
    chirp::chat::UnblockMessageSenderResponse resp;
    ASSERT_TRUE(resp.ParseFromString(frames[1].body()));
    EXPECT_EQ(resp.code(), chirp::common::INVALID_PARAM);
  }
}

// --- 历史回读：分页 + has_more + before 过滤 + 校验 ------------------------------

TEST_F(BasicChatTest, GetHistoryPaginatesAndFiltersByTimestamp) {
  auto alice = std::make_shared<MockSession>();
  auto bob = std::make_shared<MockSession>();
  ASSERT_EQ(Login(alice, "alice").code(), chirp::common::OK);
  // bob 在线：私聊走「已投递」路径回 OK（离线路径回 TARGET_OFFLINE，另测）。
  ASSERT_EQ(Login(bob, "bob").code(), chirp::common::OK);

  for (const auto& text : {"one", "two", "three"}) {
    const auto resp = Send(alice, "alice", "bob", text);
    ASSERT_EQ(resp.code(), chirp::common::OK);
    EXPECT_GT(resp.server_timestamp(), 0);
  }

  auto history = [&](const std::string& channel, int32_t limit, int64_t before) {
    chirp::chat::GetHistoryRequest req;
    req.set_user_id("alice");
    req.set_channel_type(chirp::chat::PRIVATE);
    req.set_channel_id(channel);
    req.set_limit(limit);
    req.set_before_timestamp(before);
    DispatchReq(chirp::gateway::GET_HISTORY_REQ, req, alice);
    chirp::chat::GetHistoryResponse resp;
    const auto frames = FramesOf(*alice, chirp::gateway::GET_HISTORY_RESP);
    resp.ParseFromString(frames.back().body());
    return resp;
  };

  const auto page = history("alice|bob", 2, 0);
  ASSERT_EQ(page.code(), chirp::common::OK);
  EXPECT_EQ(page.messages_size(), 2);
  EXPECT_TRUE(page.has_more());

  // before 边界（真实发送的时间戳同毫秒不可分辨，这里只测确定性的两端；
  // 精确的中界过滤在 MessageStore 用显式时间戳锁）：before=1 全滤掉、
  // before=INT64_MAX 全放行。
  const auto ancient = history("alice|bob", 10, 1);
  EXPECT_EQ(ancient.messages_size(), 0);
  const auto everything = history("alice|bob", 10, INT64_MAX);
  EXPECT_EQ(everything.messages_size(), 3);

  // 未认证 / 非本会话成员 → AUTH_FAILED；垃圾 body → INVALID_PARAM。
  auto anon = std::make_shared<MockSession>();
  chirp::chat::GetHistoryRequest req;
  req.set_user_id("mallory");
  req.set_channel_type(chirp::chat::PRIVATE);
  req.set_channel_id("alice|bob");
  DispatchReq(chirp::gateway::GET_HISTORY_REQ, req, anon);
  Dispatch(chirp::gateway::GET_HISTORY_REQ, std::string("\xde\xad", 2), anon);
  const auto frames = FramesOf(*anon, chirp::gateway::GET_HISTORY_RESP);
  ASSERT_EQ(frames.size(), 2u);
  chirp::chat::GetHistoryResponse resp;
  ASSERT_TRUE(resp.ParseFromString(frames[0].body()));
  EXPECT_EQ(resp.code(), chirp::common::AUTH_FAILED);
  ASSERT_TRUE(resp.ParseFromString(frames[1].body()));
  EXPECT_EQ(resp.code(), chirp::common::INVALID_PARAM);
}

// --- 群组 RPC 委托：垃圾 body 全臂 + happy path -------------------------------

TEST_F(BasicChatTest, GroupRpcDelegationHappyAndGarbage) {
  auto alice = std::make_shared<MockSession>();
  auto bob = std::make_shared<MockSession>();
  ASSERT_EQ(Login(alice, "alice").code(), chirp::common::OK);
  ASSERT_EQ(Login(bob, "bob").code(), chirp::common::OK);

  // 建群（happy）。
  chirp::chat::CreateGroupRequest create;
  create.set_creator_id("alice");
  create.set_group_name("room");
  DispatchReq(chirp::gateway::CREATE_GROUP_REQ, create, alice);
  chirp::chat::CreateGroupResponse create_resp;
  ASSERT_TRUE(create_resp.ParseFromString(
      FramesOf(*alice, chirp::gateway::CREATE_GROUP_RESP).back().body()));
  ASSERT_EQ(create_resp.code(), chirp::common::OK);
  const std::string gid = create_resp.group_id();

  // join / members / user-groups / info / invite / leave（happy）。
  chirp::chat::JoinGroupRequest join;
  join.set_user_id("bob");
  join.set_group_id(gid);
  DispatchReq(chirp::gateway::JOIN_GROUP_REQ, join, bob);
  chirp::chat::JoinGroupResponse join_resp;
  ASSERT_TRUE(join_resp.ParseFromString(
      FramesOf(*bob, chirp::gateway::JOIN_GROUP_RESP).back().body()));
  EXPECT_EQ(join_resp.code(), chirp::common::OK);

  chirp::chat::GetGroupMembersRequest members;
  members.set_group_id(gid);
  DispatchReq(chirp::gateway::GET_GROUP_MEMBERS_REQ, members, alice);
  chirp::chat::GetGroupMembersResponse members_resp;
  ASSERT_TRUE(members_resp.ParseFromString(
      FramesOf(*alice, chirp::gateway::GET_GROUP_MEMBERS_RESP).back().body()));
  EXPECT_EQ(members_resp.code(), chirp::common::OK);
  EXPECT_GE(members_resp.members_size(), 2);

  chirp::chat::GetUserGroupsRequest user_groups;
  user_groups.set_user_id("bob");
  DispatchReq(chirp::gateway::GET_USER_GROUPS_REQ, user_groups, bob);
  chirp::chat::GetUserGroupsResponse ug_resp;
  ASSERT_TRUE(ug_resp.ParseFromString(
      FramesOf(*bob, chirp::gateway::GET_USER_GROUPS_RESP).back().body()));
  EXPECT_EQ(ug_resp.code(), chirp::common::OK);
  EXPECT_EQ(ug_resp.groups_size(), 1);

  chirp::chat::GetGroupInfoRequest info;
  info.set_group_id(gid);
  DispatchReq(chirp::gateway::GET_GROUP_INFO_REQ, info, alice);
  chirp::chat::GetGroupInfoResponse info_resp;
  ASSERT_TRUE(info_resp.ParseFromString(
      FramesOf(*alice, chirp::gateway::GET_GROUP_INFO_RESP).back().body()));
  EXPECT_EQ(info_resp.code(), chirp::common::OK);

  chirp::chat::InviteToGroupRequest invite;
  invite.set_inviter_id("alice");
  invite.set_group_id(gid);
  invite.set_target_user_id("carol");
  DispatchReq(chirp::gateway::INVITE_TO_GROUP_REQ, invite, alice);
  chirp::chat::InviteToGroupResponse invite_resp;
  ASSERT_TRUE(invite_resp.ParseFromString(
      FramesOf(*alice, chirp::gateway::INVITE_TO_GROUP_RESP).back().body()));
  EXPECT_EQ(invite_resp.code(), chirp::common::OK);

  // kick + leave（happy）。
  chirp::chat::KickMemberRequest kick;
  kick.set_group_id(gid);
  kick.set_target_user_id("bob");
  kick.set_requester_id("alice");
  DispatchReq(chirp::gateway::KICK_MEMBER_REQ, kick, alice);
  chirp::chat::KickMemberResponse kick_resp;
  ASSERT_TRUE(kick_resp.ParseFromString(
      FramesOf(*alice, chirp::gateway::KICK_MEMBER_RESP).back().body()));
  EXPECT_EQ(kick_resp.code(), chirp::common::OK);

  chirp::chat::LeaveGroupRequest leave;
  leave.set_group_id(gid);
  leave.set_user_id("alice");
  DispatchReq(chirp::gateway::LEAVE_GROUP_REQ, leave, alice);
  chirp::chat::LeaveGroupResponse leave_resp;
  ASSERT_TRUE(leave_resp.ParseFromString(
      FramesOf(*alice, chirp::gateway::LEAVE_GROUP_RESP).back().body()));
  EXPECT_EQ(leave_resp.code(), chirp::common::OK);

  // 八个群 RPC 的垃圾 body → 各自 INVALID_PARAM（全部委托面走过）。
  const std::vector<std::pair<chirp::gateway::MsgID, chirp::gateway::MsgID>> pairs = {
      {chirp::gateway::CREATE_GROUP_REQ, chirp::gateway::CREATE_GROUP_RESP},
      {chirp::gateway::JOIN_GROUP_REQ, chirp::gateway::JOIN_GROUP_RESP},
      {chirp::gateway::LEAVE_GROUP_REQ, chirp::gateway::LEAVE_GROUP_RESP},
      {chirp::gateway::KICK_MEMBER_REQ, chirp::gateway::KICK_MEMBER_RESP},
      {chirp::gateway::GET_GROUP_INFO_REQ, chirp::gateway::GET_GROUP_INFO_RESP},
      {chirp::gateway::GET_GROUP_MEMBERS_REQ, chirp::gateway::GET_GROUP_MEMBERS_RESP},
      {chirp::gateway::GET_USER_GROUPS_REQ, chirp::gateway::GET_USER_GROUPS_RESP},
      {chirp::gateway::INVITE_TO_GROUP_REQ, chirp::gateway::INVITE_TO_GROUP_RESP}};
  for (const auto& [req_id, resp_id] : pairs) {
    Dispatch(req_id, std::string("\xde\xad", 2), alice);
    // 最新一帧就是本次回包。
    chirp::gateway::Packet latest;
    ASSERT_TRUE(DecodeFramed(alice->sent.back(), &latest)) << "req_id=" << req_id;
    ASSERT_EQ(latest.msg_id(), resp_id);
    // 所有 *Response 的 field 1 都是 code。
    chirp::chat::CreateGroupResponse probe;
    ASSERT_TRUE(probe.ParseFromString(latest.body()));
    EXPECT_EQ(probe.code(), chirp::common::INVALID_PARAM) << "req_id=" << req_id;
  }
}

// --- 回执 / 输入状态 / 表情 / 编辑撤回 / @提及 委托 ------------------------------

TEST_F(BasicChatTest, ReceiptTypingReactionEditMentionDelegation) {
  auto alice = std::make_shared<MockSession>();
  auto bob = std::make_shared<MockSession>();
  ASSERT_EQ(Login(alice, "alice").code(), chirp::common::OK);
  ASSERT_EQ(Login(bob, "bob").code(), chirp::common::OK);

  const auto sent = Send(alice, "alice", "bob", "target");
  ASSERT_EQ(sent.code(), chirp::common::OK);

  // MARK_READ happy（bob 在 alice|bob 里标读）+ GET_READ_RECEIPTS。
  chirp::chat::MarkReadRequest mark;
  mark.set_user_id("bob");
  mark.set_channel_id("alice|bob");
  mark.set_message_id(sent.message_id());
  DispatchReq(chirp::gateway::MARK_READ_REQ, mark, bob);
  chirp::chat::MarkReadResponse mark_resp;
  ASSERT_TRUE(mark_resp.ParseFromString(
      FramesOf(*bob, chirp::gateway::MARK_READ_RESP).back().body()));
  EXPECT_EQ(mark_resp.code(), chirp::common::OK);

  chirp::chat::GetReadReceiptsRequest receipts_req;
  receipts_req.set_message_id(sent.message_id());
  DispatchReq(chirp::gateway::GET_READ_RECEIPTS_REQ, receipts_req, alice);
  chirp::chat::GetReadReceiptsResponse receipts_resp;
  ASSERT_TRUE(receipts_resp.ParseFromString(
      FramesOf(*alice, chirp::gateway::GET_READ_RECEIPTS_RESP).back().body()));
  EXPECT_EQ(receipts_resp.code(), chirp::common::OK);

  chirp::chat::GetUnreadCountRequest unread;
  unread.set_user_id("alice");
  DispatchReq(chirp::gateway::GET_UNREAD_COUNT_REQ, unread, alice);
  chirp::chat::GetUnreadCountResponse unread_resp;
  ASSERT_TRUE(unread_resp.ParseFromString(
      FramesOf(*alice, chirp::gateway::GET_UNREAD_COUNT_RESP).back().body()));
  EXPECT_EQ(unread_resp.code(), chirp::common::OK);

  // 表情：加 / 查 / 删。
  chirp::chat::AddReactionRequest add;
  add.set_message_id(sent.message_id());
  add.set_user_id("alice");
  add.set_emoji("+1");
  DispatchReq(chirp::gateway::ADD_REACTION_REQ, add, alice);
  chirp::chat::AddReactionResponse add_resp;
  ASSERT_TRUE(add_resp.ParseFromString(
      FramesOf(*alice, chirp::gateway::ADD_REACTION_RESP).back().body()));
  EXPECT_EQ(add_resp.code(), chirp::common::OK);

  chirp::chat::GetReactionsRequest get_re;
  get_re.set_message_id(sent.message_id());
  DispatchReq(chirp::gateway::GET_REACTIONS_REQ, get_re, alice);
  chirp::chat::GetReactionsResponse get_re_resp;
  ASSERT_TRUE(get_re_resp.ParseFromString(
      FramesOf(*alice, chirp::gateway::GET_REACTIONS_RESP).back().body()));
  EXPECT_EQ(get_re_resp.code(), chirp::common::OK);

  chirp::chat::RemoveReactionRequest remove;
  remove.set_message_id(sent.message_id());
  remove.set_user_id("alice");
  remove.set_emoji("+1");
  DispatchReq(chirp::gateway::REMOVE_REACTION_REQ, remove, alice);
  chirp::chat::RemoveReactionResponse remove_resp;
  ASSERT_TRUE(remove_resp.ParseFromString(
      FramesOf(*alice, chirp::gateway::REMOVE_REACTION_RESP).back().body()));
  EXPECT_EQ(remove_resp.code(), chirp::common::OK);

  // 编辑（作者本人）+ 撤回（作者本人，private 在默认可撤回名单）。
  chirp::chat::EditMessageRequest edit;
  edit.set_message_id(sent.message_id());
  edit.set_user_id("alice");
  edit.set_new_content("edited");
  DispatchReq(chirp::gateway::EDIT_MESSAGE_REQ, edit, alice);
  chirp::chat::EditMessageResponse edit_resp;
  ASSERT_TRUE(edit_resp.ParseFromString(
      FramesOf(*alice, chirp::gateway::EDIT_MESSAGE_RESP).back().body()));
  EXPECT_EQ(edit_resp.code(), chirp::common::OK);

  chirp::chat::DeleteMessageRequest del;
  del.set_message_id(sent.message_id());
  del.set_user_id("alice");
  DispatchReq(chirp::gateway::DELETE_MESSAGE_REQ, del, alice);
  chirp::chat::DeleteMessageResponse del_resp;
  ASSERT_TRUE(del_resp.ParseFromString(
      FramesOf(*alice, chirp::gateway::DELETE_MESSAGE_RESP).back().body()));
  EXPECT_EQ(del_resp.code(), chirp::common::OK);
  // 墓碑经 fixture 的 mark_recalled 钩子落到 store：历史只剩置位+空正文。
  const auto page = store_->GetHistory(chirp::chat::PRIVATE, "alice|bob", 0, 10, nullptr);
  ASSERT_EQ(page.size(), 1u);
  EXPECT_TRUE(page[0].is_recalled());
  EXPECT_TRUE(page[0].content().empty());

  // 批量软删（作者 + private 频道）。
  const auto second = Send(alice, "alice", "bob", "bulk target");
  ASSERT_EQ(second.code(), chirp::common::OK);
  chirp::chat::BulkDeleteRequest bulk;
  bulk.add_message_ids(second.message_id());
  bulk.set_requester_id("alice");
  bulk.set_channel_id("alice|bob");
  DispatchReq(chirp::gateway::BULK_DELETE_REQ, bulk, alice);
  chirp::chat::BulkDeleteResponse bulk_resp;
  ASSERT_TRUE(bulk_resp.ParseFromString(
      FramesOf(*alice, chirp::gateway::BULK_DELETE_RESP).back().body()));
  EXPECT_EQ(bulk_resp.code(), chirp::common::OK);
  EXPECT_EQ(bulk_resp.deleted_count(), 1);

  // @提及建议 + 输入状态（notify 无回帧；对端收 TYPING 广播）。
  chirp::chat::GetMentionSuggestionsRequest suggest;
  suggest.set_user_id("alice");
  suggest.set_channel_id("alice|bob");
  suggest.set_query("bo");
  DispatchReq(chirp::gateway::GET_MENTION_SUGGESTIONS_REQ, suggest, alice);
  chirp::chat::GetMentionSuggestionsResponse suggest_resp;
  ASSERT_TRUE(suggest_resp.ParseFromString(
      FramesOf(*alice, chirp::gateway::GET_MENTION_SUGGESTIONS_RESP).back().body()));
  EXPECT_EQ(suggest_resp.code(), chirp::common::OK);

  chirp::chat::TypingIndicator typing;
  typing.set_channel_id("alice|bob");
  typing.set_user_id("alice");
  typing.set_is_typing(true);
  DispatchReq(chirp::gateway::TYPING_INDICATOR_NOTIFY, typing, alice);
  EXPECT_GT(FramesOf(*bob, chirp::gateway::TYPING_INDICATOR_NOTIFY).size(), 0u);

  chirp::chat::GetTypingUsersRequest typing_req;
  typing_req.set_channel_id("alice|bob");
  DispatchReq(chirp::gateway::GET_TYPING_USERS_REQ, typing_req, bob);
  chirp::chat::GetTypingUsersResponse typing_resp;
  ASSERT_TRUE(typing_resp.ParseFromString(
      FramesOf(*bob, chirp::gateway::GET_TYPING_USERS_RESP).back().body()));
  EXPECT_EQ(typing_resp.code(), chirp::common::OK);

  // 垃圾 body 全臂：12 个委托 RPC 各回 INVALID_PARAM（typing/heartbeat 类
  // notify 无回帧，不在此列）。
  const std::vector<std::pair<chirp::gateway::MsgID, chirp::gateway::MsgID>> pairs = {
      {chirp::gateway::MARK_READ_REQ, chirp::gateway::MARK_READ_RESP},
      {chirp::gateway::GET_READ_RECEIPTS_REQ, chirp::gateway::GET_READ_RECEIPTS_RESP},
      {chirp::gateway::GET_UNREAD_COUNT_REQ, chirp::gateway::GET_UNREAD_COUNT_RESP},
      {chirp::gateway::GET_TYPING_USERS_REQ, chirp::gateway::GET_TYPING_USERS_RESP},
      {chirp::gateway::ADD_REACTION_REQ, chirp::gateway::ADD_REACTION_RESP},
      {chirp::gateway::REMOVE_REACTION_REQ, chirp::gateway::REMOVE_REACTION_RESP},
      {chirp::gateway::GET_REACTIONS_REQ, chirp::gateway::GET_REACTIONS_RESP},
      {chirp::gateway::EDIT_MESSAGE_REQ, chirp::gateway::EDIT_MESSAGE_RESP},
      {chirp::gateway::DELETE_MESSAGE_REQ, chirp::gateway::DELETE_MESSAGE_RESP},
      {chirp::gateway::BULK_DELETE_REQ, chirp::gateway::BULK_DELETE_RESP},
      {chirp::gateway::GET_MENTION_SUGGESTIONS_REQ,
       chirp::gateway::GET_MENTION_SUGGESTIONS_RESP}};
  for (const auto& [req_id, resp_id] : pairs) {
    Dispatch(req_id, std::string("\xde\xad", 2), alice);
    chirp::gateway::Packet latest;
    ASSERT_TRUE(DecodeFramed(alice->sent.back(), &latest)) << "req_id=" << req_id;
    ASSERT_EQ(latest.msg_id(), resp_id);
    chirp::chat::CreateGroupResponse probe;  // 所有 *Response 的 field 1 都是 code
    ASSERT_TRUE(probe.ParseFromString(latest.body()));
    EXPECT_EQ(probe.code(), chirp::common::INVALID_PARAM) << "req_id=" << req_id;
  }

  // TYPING 垃圾 body：Warn 后无回帧，也不崩。
  Dispatch(chirp::gateway::TYPING_INDICATOR_NOTIFY, std::string("\xde\xad", 2), alice);
  chirp::gateway::Packet tail;
  EXPECT_TRUE(alice->sent.empty() || DecodeFramed(alice->sent.back(), &tail));
}

// --- 登出：确认槽位释放 + 清单下线广播 + ack/信任清理 ---------------------------

TEST_F(BasicChatTest, LogoutDisconnectsAndPurgesPresence) {
  auto web = std::make_shared<MockSession>();
  auto phone = std::make_shared<MockSession>();
  ASSERT_EQ(Login(web, "alice", "tab-1", "web", /*supports_ack=*/true).code(),
            chirp::common::OK);
  ASSERT_EQ(Login(phone, "alice", "p1", "ios").code(), chirp::common::OK);

  // 预置：信任集里有该连接，ack 在册——登出要两者都清。
  trusted_->insert(web.get());
  chirp::chat::ChatMessage pending;
  pending.set_message_id("m-x");
  store_->AddOffline("alice", pending);
  acks_->MarkCapable(web);

  const size_t announces_before =
      FramesOf(*phone, chirp::gateway::DEVICES_PRESENCE_NOTIFY).size();
  chirp::auth::LogoutRequest req;
  req.set_user_id("alice");  // ValidateLogoutRequest 要求 user_id 与认证身份一致
  DispatchReq(chirp::gateway::LOGOUT_REQ, req, web);
  const auto frames = FramesOf(*web, chirp::gateway::LOGOUT_RESP);
  ASSERT_EQ(frames.size(), 1u);
  chirp::auth::LogoutResponse resp;
  ASSERT_TRUE(resp.ParseFromString(frames[0].body()));
  EXPECT_EQ(resp.code(), chirp::common::OK);
  EXPECT_TRUE(web->close_after_send);           // 登出即断（SendPacketAndClose）
  EXPECT_EQ(trusted_->count(web.get()), 0u);    // 信任撤销
  EXPECT_EQ(acks_->pending_count(), 0u);        // 忘记该连接的挂起
  // 剩余端收到 web 下线清单事件。
  EXPECT_EQ(FramesOf(*phone, chirp::gateway::DEVICES_PRESENCE_NOTIFY).size(),
            announces_before + 1u);

  // 未认证登出：回错误码（ValidateLogoutRequest 拒未登录）。
  auto anon = std::make_shared<MockSession>();
  DispatchReq(chirp::gateway::LOGOUT_REQ, chirp::auth::LogoutRequest{}, anon);
  const auto anon_frames = FramesOf(*anon, chirp::gateway::LOGOUT_RESP);
  ASSERT_EQ(anon_frames.size(), 1u);
  ASSERT_TRUE(resp.ParseFromString(anon_frames[0].body()));
  EXPECT_NE(resp.code(), chirp::common::OK);

  // 垃圾 body → INVALID_PARAM。
  Dispatch(chirp::gateway::LOGOUT_REQ, std::string("\xde\xad", 2), anon);
  ASSERT_TRUE(resp.ParseFromString(
      FramesOf(*anon, chirp::gateway::LOGOUT_RESP).back().body()));
  EXPECT_EQ(resp.code(), chirp::common::INVALID_PARAM);

  // 直接断开（HandleDisconnect）：未认证会话不广播。
  const size_t before = FramesOf(*phone, chirp::gateway::DEVICES_PRESENCE_NOTIFY).size();
  HandleDisconnect(state_, acks_.get(), trusted_, anon);
  EXPECT_EQ(FramesOf(*phone, chirp::gateway::DEVICES_PRESENCE_NOTIFY).size(), before);
}

// --- MessageStore：Redis 镜像路径（真 RedisClient + InMemoryRedis） --------------

TEST_F(BasicChatTest, MessageStoreRedisMirrorsHistoryAndQueues) {
  chirp_test::InMemoryRedis redis;
  auto fake = std::make_unique<chirp_test::FakeRedisServer>(
      [&redis](const std::vector<std::string>& args) { return redis.Handle(args); });
  auto client = std::make_shared<chirp::network::RedisClient>("127.0.0.1", fake->port());
  MessageStore redis_store(client, /*ttl=*/60);

  // 活性探针：下面几乎所有断言都被内存回退路径双保险覆盖——若 Redis 连接
  // 静默失败（RPush/LRange 的失败返回被调用方忽略），测试照样绿。先在
  // scratch 键上钉死客户端对 fake 服务端的读写确实成功，redis 分支才是
  // 真被测到。
  ASSERT_TRUE(client->RPush("probe:list", "v1"));
  ASSERT_EQ(client->LRange("probe:list", 0, -1), (std::vector<std::string>{"v1"}));
  ASSERT_TRUE(client->Del("probe:list"));

  chirp::chat::ChatMessage m1, m2, m3;
  m1.set_message_id("m1");
  m1.set_channel_type(chirp::chat::PRIVATE);
  m1.set_channel_id("alice|bob");
  m1.set_content("one");
  m1.set_timestamp(1000);  // 显式错开：before 过滤在同毫秒时间戳下不可分辨
  m2 = m1;
  m2.set_message_id("m2");
  m2.set_content("two");
  m2.set_timestamp(2000);
  m3 = m1;
  m3.set_message_id("m3");
  m3.set_content("three");
  m3.set_timestamp(3000);
  redis_store.AddMessage(m1);
  redis_store.AddMessage(m2);
  redis_store.AddMessage(m3);

  // 历史镜像确实落进了 Redis 列表（不是只有内存台账在应答）：键里恰好
  // 3 条，后续 GetHistory 的 redis 分支才有真实输入。
  ASSERT_EQ(
      client->LRange(redis_store.HistoryKey(chirp::chat::PRIVATE, "alice|bob"), 0, -1).size(),
      3u);

  // 历史走 Redis 路径：倒序扫描 + limit + has_more。
  bool has_more = false;
  const auto page = redis_store.GetHistory(chirp::chat::PRIVATE, "alice|bob", 0, 2,
                                           &has_more);
  ASSERT_EQ(page.size(), 2u);
  EXPECT_EQ(page[0].message_id(), "m2");  // 最近两条，时间正序返回
  EXPECT_EQ(page[1].message_id(), "m3");
  EXPECT_TRUE(has_more);
  // before 过滤：m2 之后（ts≥2001）的消息被滤掉，剩 m1 + m2。
  const auto older =
      redis_store.GetHistory(chirp::chat::PRIVATE, "alice|bob", m2.timestamp() + 1, 10, nullptr);
  EXPECT_EQ(older.size(), 2u);

  // 坏条目跳过：直接往历史键塞垃圾，读回只剩可解析条目。
  client->RPush(redis_store.HistoryKey(chirp::chat::PRIVATE, "alice|bob"),
                std::string("\xde\xad\xbe\xef", 4));
  const auto filtered = redis_store.GetHistory(chirp::chat::PRIVATE, "alice|bob", 0, 10,
                                               nullptr);
  EXPECT_EQ(filtered.size(), 3u);

  // 离线队列：入队（带 TTL）→ 弹空（读 + DEL）。
  redis_store.AddOffline("bob", m1);
  ASSERT_EQ(redis_store.PopOffline("bob").size(), 1u);
  EXPECT_TRUE(redis_store.PopOffline("bob").empty());  // DEL 生效

  // 字节级回队 + 迟到 ack 的 LRem 清除。
  redis_store.AddOfflineBytes("bob", m1.SerializeAsString());
  EXPECT_TRUE(redis_store.RemoveOffline("bob", m1.SerializeAsString()));
  EXPECT_FALSE(redis_store.RemoveOffline("bob", m1.SerializeAsString()));  // 已无副本

  // 按 id 回收（撤回路径）：入队两条同 id 副本 → 全部回收。
  redis_store.AddOffline("carol", m1);
  redis_store.AddOfflineBytes("carol", m1.SerializeAsString());
  EXPECT_EQ(redis_store.PurgeOfflineByMessageId("carol", "m1"), 2u);

  // 撤回墓碑：内存 + Redis 镜像双写。
  redis_store.AddMessage(m2);
  EXPECT_TRUE(redis_store.MarkRecalled(chirp::chat::PRIVATE, "alice|bob", "m2"));
  const auto tombstones =
      redis_store.GetHistory(chirp::chat::PRIVATE, "alice|bob", 0, 10, nullptr);
  for (const auto& msg : tombstones) {
    if (msg.message_id() == "m2") {
      EXPECT_TRUE(msg.is_recalled());
      EXPECT_TRUE(msg.content().empty());
    }
  }

  // HasMessage 在 Redis 形态下仍以内存台账为准（写入即记）。
  EXPECT_TRUE(redis_store.HasMessage(chirp::chat::PRIVATE, "alice|bob", "m3"));
  EXPECT_FALSE(redis_store.HasMessage(chirp::chat::PRIVATE, "alice|bob", "m999"));
}

// --- MessageStore：裁剪上限与回退边界 ------------------------------------------

TEST_F(BasicChatTest, MessageStoreTrimsCapsAndFallbackEdges) {
  // 历史裁剪：kMaxHistory=100，第 101 条挤掉最旧。
  chirp::chat::ChatMessage msg;
  msg.set_channel_type(chirp::chat::WORLD);
  msg.set_channel_id("world");
  for (int i = 0; i <= static_cast<int>(MessageStore::kMaxHistory); ++i) {
    msg.set_message_id("m" + std::to_string(i));
    msg.set_content(std::to_string(i));
    store_->AddMessage(msg);
  }
  const auto all = store_->GetHistory(chirp::chat::WORLD, "world", 0, 1000, nullptr);
  ASSERT_EQ(all.size(), MessageStore::kMaxHistory);
  EXPECT_EQ(all.front().message_id(), "m1");  // m0 被挤掉

  // before 中界过滤（显式时间戳，确定性）：ts ∈ [1100, 2000) 只有 m1 一条。
  {
    chirp::chat::ChatMessage stamped;
    stamped.set_channel_type(chirp::chat::GUILD);
    stamped.set_channel_id("g");
    for (int i = 1; i <= 3; ++i) {
      stamped.set_message_id("s" + std::to_string(i));
      stamped.set_timestamp(1000 * i);
      store_->AddMessage(stamped);
    }
    const auto middle = store_->GetHistory(chirp::chat::GUILD, "g", 2000, 10, nullptr);
    ASSERT_EQ(middle.size(), 1u);
    EXPECT_EQ(middle[0].message_id(), "s1");
  }

  // 离线回退队列裁剪：kMaxOfflineInMemory=200。
  for (int i = 0; i <= static_cast<int>(MessageStore::kMaxOfflineInMemory) + 1; ++i) {
    store_->AddOffline("piled", msg);
  }
  auto pending = store_->PopOffline("piled");
  EXPECT_EQ(pending.size(), MessageStore::kMaxOfflineInMemory);

  // 空身份守卫。
  EXPECT_TRUE(store_->PopOffline("").empty());
  chirp::chat::ChatMessage any;
  any.set_message_id("z");
  store_->AddOffline("", any);  // no-op，不产生键
  EXPECT_FALSE(store_->RemoveOffline("", any.SerializeAsString()));
  EXPECT_EQ(store_->PurgeOfflineByMessageId("", "z"), 0u);
  EXPECT_EQ(store_->PurgeOfflineByMessageId("piled", ""), 0u);

  // 回退队列的字节级入队：坏字节跳过，好字节按消息入队。
  store_->AddOfflineBytes("bytes", std::string("\xde\xad\xbe\xef", 4));
  store_->AddOfflineBytes("bytes", any.SerializeAsString());
  const auto byte_queue = store_->PopOffline("bytes");
  ASSERT_EQ(byte_queue.size(), 1u);
  EXPECT_EQ(byte_queue[0].message_id(), "z");

  // 字节级入队同样受 kMaxOfflineInMemory=200 上限裁剪：第 201 条挤掉最旧。
  chirp::chat::ChatMessage trim_msg;
  trim_msg.set_message_id("t0");
  for (int i = 0; i <= static_cast<int>(MessageStore::kMaxOfflineInMemory); ++i) {
    store_->AddOfflineBytes("trim", trim_msg.SerializeAsString());
  }
  EXPECT_EQ(store_->PopOffline("trim").size(), MessageStore::kMaxOfflineInMemory);

  // 内存路径的迟到 ack 清除：按 message_id 匹配命中即删，重复清除落空。
  chirp::chat::ChatMessage late;
  late.set_message_id("late-1");
  store_->AddOffline("gone", late);
  EXPECT_TRUE(store_->RemoveOffline("gone", late.SerializeAsString()));
  EXPECT_FALSE(store_->RemoveOffline("gone", late.SerializeAsString()));

  // 内存路径的撤回回收：消息式 + 字节式两条同 id 副本一次清空。
  store_->AddOffline("carol-mem", late);
  store_->AddOfflineBytes("carol-mem", late.SerializeAsString());
  EXPECT_EQ(store_->PurgeOfflineByMessageId("carol-mem", "late-1"), 2u);

  // limit≤0 取默认 50：WORLD 台账 100 条只要最近 50 条。
  const auto default_page = store_->GetHistory(chirp::chat::WORLD, "world", 0, 0, nullptr);
  ASSERT_EQ(default_page.size(), 50u);

  // 未知频道边界：内存历史空回 / 台账查无 / 撤回 no-op 成功（redis 缺席时
  // 无镜像可改，ok 恒真）。
  EXPECT_TRUE(store_->GetHistory(chirp::chat::GUILD, "no-such", 0, 10, nullptr).empty());
  EXPECT_FALSE(store_->HasMessage(chirp::chat::GUILD, "no-such", "s1"));
  EXPECT_TRUE(store_->MarkRecalled(chirp::chat::GUILD, "no-such", "s1"));

  // 私聊键归一（对称序）+ 会话 id 唯一性。
  EXPECT_EQ(store_->PrivateChannelId("bob", "alice"), "alice|bob");
  EXPECT_EQ(store_->PrivateChannelId("alice", "bob"), "alice|bob");
  const auto s1 = GenerateSessionId();
  const auto s2 = GenerateSessionId();
  EXPECT_NE(s1, s2);
  EXPECT_NE(s1.find("chat_session_"), std::string::npos);
}

// --- WP-8 自服务 RPC 挂在同一端口的信任门后面 -----------------------------------

TEST_F(BasicChatTest, PlayerDirectoryRpcRidesTrustGate) {
  chirp::chat::PlayerDirectory directory(chirp::chat::PlayerDirectory::Options{});
  features_->directory = &directory;
  features_->gateway_secret = "shh";

  // 未信任连接：BIND 被目录信任门拒（AUTH_FAILED），不进 switch。
  auto stranger = std::make_shared<MockSession>();
  chirp::game_server_gateway::BindPlayerIdentityRequest bind;
  bind.set_binding_id("b1");
  bind.set_player_id("alice");
  bind.set_game_id("game42");
  bind.set_game_user_id("u-1");
  DispatchReq(chirp::gateway::BIND_PLAYER_IDENTITY_REQ, bind, stranger);
  const auto denied = FramesOf(*stranger, chirp::gateway::BIND_PLAYER_IDENTITY_RESP);
  ASSERT_EQ(denied.size(), 1u);
  chirp::game_server_gateway::BindPlayerIdentityResponse bind_resp;
  ASSERT_TRUE(bind_resp.ParseFromString(denied[0].body()));
  EXPECT_EQ(bind_resp.code(), chirp::common::AUTH_FAILED);

  // 信任后同一条 RPC 落地。
  chirp::game_server_gateway::ServerAuthRequest auth;
  auth.set_secret("shh");
  DispatchReq(chirp::gateway::SERVER_AUTH_REQ, auth, stranger);
  DispatchReq(chirp::gateway::BIND_PLAYER_IDENTITY_REQ, bind, stranger);
  const auto handled = FramesOf(*stranger, chirp::gateway::BIND_PLAYER_IDENTITY_RESP);
  ASSERT_EQ(handled.size(), 2u);
  ASSERT_TRUE(bind_resp.ParseFromString(handled[1].body()));
  EXPECT_EQ(bind_resp.code(), chirp::common::OK);

  // 目录未装（null）时 WP-8 id 落到 switch default：静默。
  features_->directory = nullptr;
  auto plain = std::make_shared<MockSession>();
  DispatchReq(chirp::gateway::BIND_PLAYER_IDENTITY_REQ, bind, plain);
  EXPECT_TRUE(plain->sent.empty());
}

// --- main() 进程级 E2E（覆盖率批次 23）：直驱改名后的 chirp_chat_basic_main ------
//
// 与 DistributedMainTest（chat_managers_test.cc）同款形态：runner 线程跑真实的
// main()，客户端走真 TCP/WS 回环。basic main() 的 TcpServer/WebSocketServer
// 构造没有 distributed 形态的 try/catch 兜底——丢端口竞态时 system_error 会逸出
// main，runner 线程在边界接住并记 rc=1，让重试循环换端口重来而不是 terminate
// 整个测试进程。端口用 bind+close 各自独立探测：内核顺序分配临时端口，连续取
// 值正是下一条出站连接（如死 Redis 重连）会拿到的端口。

uint16_t BasicMainProbePort() {
  asio::io_context io;
  asio::ip::tcp::acceptor probe(io, asio::ip::tcp::endpoint(asio::ip::tcp::v4(), 0));
  return static_cast<uint16_t>(probe.local_endpoint().port());
}

std::vector<char*> BasicMainArgv(const std::vector<std::string>& args,
                                 std::vector<std::unique_ptr<char[]>>& holds) {
  std::vector<char*> argv;
  for (const auto& a : args) {
    auto buf = std::make_unique<char[]>(a.size() + 1);
    std::memcpy(buf.get(), a.c_str(), a.size() + 1);
    argv.push_back(buf.get());
    holds.push_back(std::move(buf));
  }
  return argv;
}

// u32-BE 长度前缀帧（ProtobufFraming::Encode 同款字节布局）。
std::string BasicMainFrame(chirp::gateway::MsgID id, int64_t seq,
                           const google::protobuf::Message& body) {
  chirp::gateway::Packet pkt;
  pkt.set_msg_id(id);
  pkt.set_sequence(seq);
  pkt.set_body(body.SerializeAsString());
  std::string framed(4 + pkt.ByteSizeLong(), '\0');
  const uint32_t len = static_cast<uint32_t>(pkt.ByteSizeLong());
  framed[0] = static_cast<char>((len >> 24) & 0xFF);
  framed[1] = static_cast<char>((len >> 16) & 0xFF);
  framed[2] = static_cast<char>((len >> 8) & 0xFF);
  framed[3] = static_cast<char>(len & 0xFF);
  pkt.SerializeToArray(framed.data() + 4, static_cast<int>(len));
  return framed;
}

// 一个直连客户端：自有 io_context，等待原语内联泵排空（TcpClient 的
// Connect/Send/Disconnect 都是同步直调、非 strand 投递——泵只发生在等待
// 循环里，与发送/断开在同一个测试线程上交替，无并发踩踏）。
class BasicMainClient {
 public:
  BasicMainClient() : tcp_(io_) {
    tcp_.SetCallbacks(
        [this](std::shared_ptr<chirp::network::Session>, std::string&& payload) {
          chirp::gateway::Packet pkt;
          if (pkt.ParseFromArray(payload.data(), static_cast<int>(payload.size()))) {
            std::lock_guard<std::mutex> lock(rx_mu_);
            received_.push_back(pkt);
          }
        },
        [](std::shared_ptr<chirp::network::Session>) {});
  }

  // 连到服务直到成功或服务退出（丢端口竞态）。rc 是 runner 的哨兵原子量。
  bool ConnectOrDied(uint16_t port, const std::atomic<int>& rc) {
    for (int i = 0; i < 400; ++i) {
      if (tcp_.Connect("127.0.0.1", port)) {
        return true;
      }
      if (rc.load() != 12345) {
        return false;
      }
      std::this_thread::sleep_for(std::chrono::milliseconds(5));
    }
    return false;
  }

  // 泵着自己的 io 等谓词为真（回帧都落在本客户端的 io 上）。
  bool WaitPumped(const std::function<bool()>& pred, int64_t budget_ms = 5000) {
    const auto deadline =
        std::chrono::steady_clock::now() + std::chrono::milliseconds(budget_ms);
    while (std::chrono::steady_clock::now() < deadline) {
      io_.poll();
      io_.restart();
      if (pred()) {
        return true;
      }
      std::this_thread::sleep_for(std::chrono::milliseconds(2));
    }
    io_.poll();
    io_.restart();
    return pred();
  }

  // 发一帧并等到累计收帧数不少于 want_total（含异步 notify 帧）。
  bool SendAndWait(chirp::gateway::MsgID id, const google::protobuf::Message& body,
                   size_t want_total) {
    tcp_.GetSession()->Send(BasicMainFrame(id, next_seq_++, body));
    return WaitPumped([&] {
      std::lock_guard<std::mutex> lock(rx_mu_);
      return received_.size() >= want_total;
    });
  }

  // 发一帧不等回包（MESSAGE_ACK 等无应答面）。
  void SendFireAndForget(chirp::gateway::MsgID id, const google::protobuf::Message& body) {
    tcp_.GetSession()->Send(BasicMainFrame(id, next_seq_++, body));
  }

  void Pump(int64_t budget_ms = 400) {
    const auto deadline =
        std::chrono::steady_clock::now() + std::chrono::milliseconds(budget_ms);
    while (std::chrono::steady_clock::now() < deadline) {
      io_.poll();
      io_.restart();
      std::this_thread::sleep_for(std::chrono::milliseconds(2));
    }
  }

  size_t CountOf(chirp::gateway::MsgID id) {
    std::lock_guard<std::mutex> lock(rx_mu_);
    return static_cast<size_t>(std::count_if(received_.begin(), received_.end(),
                                             [id](const chirp::gateway::Packet& p) {
                                               return p.msg_id() == id;
                                             }));
  }

  std::vector<chirp::gateway::Packet> FramesOf(chirp::gateway::MsgID id) {
    std::lock_guard<std::mutex> lock(rx_mu_);
    std::vector<chirp::gateway::Packet> out;
    for (const auto& pkt : received_) {
      if (pkt.msg_id() == id) {
        out.push_back(pkt);
      }
    }
    return out;
  }

  // Close() 只是 post 到会话 strand：断开后要泵一次 io，FIN 才真正发出去，
  // 服务端才会把会话摘掉（否则按“健康端”向僵尸连接投递，离线臂永远走不到）。
  void Disconnect() {
    tcp_.Disconnect();
    Pump(200);
  }

 private:
  asio::io_context io_;
  chirp::network::TcpClient tcp_;
  int64_t next_seq_ = 1;
  std::mutex rx_mu_;
  std::vector<chirp::gateway::Packet> received_;
};

// runner 线程体：接住 basic TcpServer 构造逸出的 system_error（见节首注释）。
std::thread BasicMainRun(std::atomic<int>& rc, int argc, char** argv) {
  return std::thread([&rc, argc, argv] {
    try {
      rc = chirp_chat_basic_main(argc, argv);
    } catch (const std::system_error&) {
      rc = 1;
    }
  });
}

TEST(BasicMainTest, EndToEndTcpSessionWithLiveDeliveryAndAck) {
  chirp::common::Logger::Instance().SetLevel(chirp::common::Logger::Level::kError);
  constexpr int kMaxBindAttempts = 5;
  for (int attempt = 0; attempt < kMaxBindAttempts; ++attempt) {
    const uint16_t port = BasicMainProbePort();
    const uint16_t ws_port = BasicMainProbePort();
    const std::vector<std::string> args = {
        "chat", "--port", std::to_string(port), "--ws_port", std::to_string(ws_port),
        "--redis_host", "127.0.0.1", "--redis_port", "1"};
    std::vector<std::unique_ptr<char[]>> holds;
    std::vector<char*> argv = BasicMainArgv(args, holds);
    std::atomic<int> rc{12345};
    std::thread runner = BasicMainRun(rc, static_cast<int>(argv.size()), argv.data());

    BasicMainClient alice, bob;
    if (!alice.ConnectOrDied(port, rc) || !bob.ConnectOrDied(port, rc)) {
      alice.Disconnect();
      bob.Disconnect();
      runner.join();
      ASSERT_LT(attempt, kMaxBindAttempts - 1) << "service kept losing the port race";
      continue;
    }

    chirp::auth::LoginRequest alice_login;
    alice_login.set_token("alice");
    alice_login.set_supports_message_ack(true);
    ASSERT_TRUE(alice.SendAndWait(chirp::gateway::LOGIN_REQ, alice_login, 1));
    {
      const auto frames = alice.FramesOf(chirp::gateway::LOGIN_RESP);
      ASSERT_EQ(frames.size(), 1u);
      chirp::auth::LoginResponse resp;
      ASSERT_TRUE(resp.ParseFromString(frames[0].body()));
      EXPECT_EQ(resp.code(), chirp::common::OK);
      EXPECT_EQ(resp.user_id(), "alice");
    }

    chirp::auth::LoginRequest bob_login;
    bob_login.set_token("bob");
    ASSERT_TRUE(bob.SendAndWait(chirp::gateway::LOGIN_REQ, bob_login, 1));
    EXPECT_EQ(bob.CountOf(chirp::gateway::LOGIN_RESP), 1u);

    // bob → alice 私聊：在线投递直达 alice 的真实 TCP 会话。SendAndWait 的
    // 期望数是本会话累计收包数：bob 已收 1 帧 LOGIN_RESP，发送后应为 2。
    chirp::chat::SendMessageRequest send;
    send.set_sender_id("bob");
    send.set_receiver_id("alice");
    send.set_channel_type(chirp::chat::PRIVATE);
    send.set_content("hello over tcp");
    ASSERT_TRUE(bob.SendAndWait(chirp::gateway::SEND_MESSAGE_REQ, send, 2));
    {
      const auto frames = bob.FramesOf(chirp::gateway::SEND_MESSAGE_RESP);
      ASSERT_EQ(frames.size(), 1u);
      chirp::chat::SendMessageResponse resp;
      ASSERT_TRUE(resp.ParseFromString(frames[0].body()));
      EXPECT_EQ(resp.code(), chirp::common::OK);
    }
    ASSERT_TRUE(alice.WaitPumped([&] {
      return alice.CountOf(chirp::gateway::CHAT_MESSAGE_NOTIFY) >= 1;
    }));
    std::string delivered_id;
    {
      const auto frames = alice.FramesOf(chirp::gateway::CHAT_MESSAGE_NOTIFY);
      ASSERT_EQ(frames.size(), 1u);
      chirp::chat::ChatMessage msg;
      ASSERT_TRUE(msg.ParseFromString(frames[0].body()));
      EXPECT_EQ(msg.content(), "hello over tcp");
      delivered_id = msg.message_id();
      ASSERT_FALSE(delivered_id.empty());
    }

    // ack-capable 投递被客户端确认（MESSAGE_ACK 无应答面）。
    chirp::chat::MessageAck ack;
    ack.set_message_id(delivered_id);
    alice.SendFireAndForget(chirp::gateway::MESSAGE_ACK, ack);

    chirp::chat::GetHistoryRequest hist;
    // 校验器要求 user_id 与登录身份一致（否则 AUTH_FAILED）。
    hist.set_user_id("alice");
    hist.set_channel_id("alice|bob");
    hist.set_limit(10);
    ASSERT_TRUE(alice.SendAndWait(chirp::gateway::GET_HISTORY_REQ, hist, 3));
    {
      const auto frames = alice.FramesOf(chirp::gateway::GET_HISTORY_RESP);
      ASSERT_EQ(frames.size(), 1u);
      chirp::chat::GetHistoryResponse resp;
      ASSERT_TRUE(resp.ParseFromString(frames[0].body()));
      ASSERT_EQ(resp.messages_size(), 1);
      EXPECT_EQ(resp.messages(0).content(), "hello over tcp");
    }

    chirp::auth::LogoutRequest logout;
    logout.set_user_id("alice");
    ASSERT_TRUE(alice.SendAndWait(chirp::gateway::LOGOUT_REQ, logout, 4));
    {
      const auto frames = alice.FramesOf(chirp::gateway::LOGOUT_RESP);
      ASSERT_EQ(frames.size(), 1u);
      chirp::auth::LogoutResponse resp;
      ASSERT_TRUE(resp.ParseFromString(frames[0].body()));
      EXPECT_EQ(resp.code(), chirp::common::OK);
    }

    alice.Disconnect();
    bob.Disconnect();
    // 让服务侧处理 EOF（TCP 断开 lambda）后再停机。
    std::this_thread::sleep_for(std::chrono::milliseconds(200));

    raise(SIGTERM);
    EXPECT_TRUE(WaitFor([&] { return rc.load() != 12345; }, 3000));
    EXPECT_EQ(rc.load(), 0);
    runner.join();
    return;
  }
}

// ack 超时重排队：未被确认的在线投递超时后回离线队列，下次登录补投——这是
// DeliveryAckManager 挂在 main() 装配上的回调（AddOfflineBytes）在真实服务
// 进程里的闭环。--ack_timeout_ms 300 + 扫描 500ms 节拍，1.5s 等待足够翻篇。
TEST(BasicMainTest, AckTimeoutRequeuesAndRefillsOnNextLogin) {
  chirp::common::Logger::Instance().SetLevel(chirp::common::Logger::Level::kError);
  constexpr int kMaxBindAttempts = 5;
  for (int attempt = 0; attempt < kMaxBindAttempts; ++attempt) {
    const uint16_t port = BasicMainProbePort();
    const uint16_t ws_port = BasicMainProbePort();
    const std::vector<std::string> args = {
        "chat", "--port", std::to_string(port), "--ws_port", std::to_string(ws_port),
        "--redis_host", "127.0.0.1", "--redis_port", "1", "--ack_timeout_ms", "300"};
    std::vector<std::unique_ptr<char[]>> holds;
    std::vector<char*> argv = BasicMainArgv(args, holds);
    std::atomic<int> rc{12345};
    std::thread runner = BasicMainRun(rc, static_cast<int>(argv.size()), argv.data());

    BasicMainClient alice, bob;
    if (!alice.ConnectOrDied(port, rc) || !bob.ConnectOrDied(port, rc)) {
      alice.Disconnect();
      bob.Disconnect();
      runner.join();
      ASSERT_LT(attempt, kMaxBindAttempts - 1) << "service kept losing the port race";
      continue;
    }

    chirp::auth::LoginRequest alice_login;
    alice_login.set_token("alice");
    alice_login.set_supports_message_ack(true);
    ASSERT_TRUE(alice.SendAndWait(chirp::gateway::LOGIN_REQ, alice_login, 1));

    chirp::auth::LoginRequest bob_login;
    bob_login.set_token("bob");
    ASSERT_TRUE(bob.SendAndWait(chirp::gateway::LOGIN_REQ, bob_login, 1));

    chirp::chat::SendMessageRequest send;
    send.set_sender_id("bob");
    send.set_receiver_id("alice");
    send.set_channel_type(chirp::chat::PRIVATE);
    send.set_content("please ack me");
    ASSERT_TRUE(bob.SendAndWait(chirp::gateway::SEND_MESSAGE_REQ, send, 1));
    ASSERT_TRUE(alice.WaitPumped([&] {
      return alice.CountOf(chirp::gateway::CHAT_MESSAGE_NOTIFY) >= 1;
    }));
    std::string delivered_id;
    {
      const auto frames = alice.FramesOf(chirp::gateway::CHAT_MESSAGE_NOTIFY);
      chirp::chat::ChatMessage msg;
      ASSERT_TRUE(msg.ParseFromString(frames.back().body()));
      delivered_id = msg.message_id();
      ASSERT_FALSE(delivered_id.empty());
    }
    // 故意不 ack：等超时重排队落进离线队列（内存回退——redis 是死端口）。
    std::this_thread::sleep_for(std::chrono::milliseconds(1500));
    alice.Disconnect();
    std::this_thread::sleep_for(std::chrono::milliseconds(250));

    // 重登：LOGIN_RESP + 离线补投（字节原样回队的同一 message_id）。
    BasicMainClient alice_again;
    ASSERT_TRUE(alice_again.ConnectOrDied(port, rc));
    ASSERT_TRUE(alice_again.SendAndWait(chirp::gateway::LOGIN_REQ, alice_login, 2));
    {
      const auto frames =
          alice_again.FramesOf(chirp::gateway::CHAT_MESSAGE_NOTIFY);
      ASSERT_EQ(frames.size(), 1u);
      chirp::chat::ChatMessage msg;
      ASSERT_TRUE(msg.ParseFromString(frames[0].body()));
      EXPECT_EQ(msg.message_id(), delivered_id);
      EXPECT_EQ(msg.content(), "please ack me");
    }

    alice_again.Disconnect();
    bob.Disconnect();
    raise(SIGTERM);
    EXPECT_TRUE(WaitFor([&] { return rc.load() != 12345; }, 3000));
    EXPECT_EQ(rc.load(), 0);
    runner.join();
    return;
  }
}

// 全旗标扫描：一次 main() 驱动把每个可选块都装配起来——hub 模式（含
// allowed_peers 逗号/冒号解析双臂）、spoke（拨死端口走重连告警臂）、
// server-plane peer + NPC uplink 旗标、四个目录 redis 工厂（死端口）、词库
// reject 策略、JWT 登录（token_secret 面）。全部走回环死端口，无外部依赖。
TEST(BasicMainTest, FlagSweepStartsEveryOptionalBlock) {
  chirp::common::Logger::Instance().SetLevel(chirp::common::Logger::Level::kError);
  const auto lexicon = std::filesystem::temp_directory_path() / "chirp_basic_main_wf.txt";
  {
    std::ofstream out(lexicon);
    out << "# comment line\nbadword\n";
  }
  struct LexiconCleanup {
    std::filesystem::path path;
    ~LexiconCleanup() {
      std::error_code ec;
      std::filesystem::remove(path, ec);
    }
  } lexicon_cleanup{lexicon};

  constexpr int kMaxBindAttempts = 5;
  for (int attempt = 0; attempt < kMaxBindAttempts; ++attempt) {
    const uint16_t port = BasicMainProbePort();
    const uint16_t ws_port = BasicMainProbePort();
    const uint16_t hub_peer_port = BasicMainProbePort();
    const std::vector<std::string> args = {
        "chat",
        "--port", std::to_string(port),
        "--ws_port", std::to_string(ws_port),
        "--redis_host", "127.0.0.1", "--redis_port", "1",
        "--offline_ttl", "300",
        "--notification_host", "127.0.0.1", "--notification_port", "1",
        "--login_rate_limit_per_min", "100",
        "--send_rate_limit_per_min", "100",
        "--word_filter_file", lexicon.string(),
        "--word_filter_policy", "reject",
        "--recall_window_sec", "60",
        "--recall_channels", "private,guild,world",
        "--token_secret", "jwt-s3cret",
        "--gateway_service_secret", "trust",
        "--hub_mode", "1",
        "--hub_peer_port", std::to_string(hub_peer_port),
        "--allowed_peers", "id1:sec1,id2:sec2",
        "--min_peer_version", "1",
        "--allow_unknown_peers", "1",
        "--app_chat_host", "127.0.0.1", "--app_chat_port", "1",
        "--game_service_id", "game_chat",
        "--game_service_secret", "sec",
        "--game_id", "game1",
        "--server_gateway_host", "127.0.0.1", "--server_gateway_port", "1",
        "--server_gateway_service", "chat",
        "--server_gateway_secret", "sgsec",
        "--server_gateway_reconnect", "1",
        "--npc_service_id", "npc-svc",
        "--npc_prefix", "npc:",
        "--binding_redis_host", "127.0.0.1", "--binding_redis_port", "1",
        "--subscription_redis_host", "127.0.0.1", "--subscription_redis_port", "1",
        "--unread_redis_host", "127.0.0.1", "--unread_redis_port", "1",
        "--game_presence_redis_host", "127.0.0.1", "--game_presence_redis_port", "1",
        "--max_fanout_per_message", "5",
        "--ack_timeout_ms", "60000"};
    std::vector<std::unique_ptr<char[]>> holds;
    std::vector<char*> argv = BasicMainArgv(args, holds);
    std::atomic<int> rc{12345};
    std::thread runner = BasicMainRun(rc, static_cast<int>(argv.size()), argv.data());

    BasicMainClient user;
    if (!user.ConnectOrDied(port, rc)) {
      user.Disconnect();
      runner.join();
      ASSERT_LT(attempt, kMaxBindAttempts - 1) << "service kept losing the port race";
      continue;
    }

    // token_secret 面：真 HS256 JWT 按 sub 登录（批 10 已直测验签器，这里证明
    // main() 装配出的链路）。
    const int64_t now_sec = chirp::chat::runtime::NowMs() / 1000;
    chirp::auth::LoginRequest login;
    login.set_token(chirp::common::JwtSignHS256("flag-user", now_sec, "jwt-s3cret",
                                                now_sec + 3600));
    ASSERT_TRUE(user.SendAndWait(chirp::gateway::LOGIN_REQ, login, 1));
    {
      const auto frames = user.FramesOf(chirp::gateway::LOGIN_RESP);
      ASSERT_EQ(frames.size(), 1u);
      chirp::auth::LoginResponse resp;
      ASSERT_TRUE(resp.ParseFromString(frames[0].body()));
      EXPECT_EQ(resp.code(), chirp::common::OK);
      EXPECT_EQ(resp.user_id(), "flag-user");
    }

    // 词库 reject：命中词被拒（INVALID_PARAM），词库是本测试写入的临时文件。
    chirp::chat::SendMessageRequest curse;
    curse.set_sender_id("flag-user");
    curse.set_receiver_id("offline-guy");
    curse.set_channel_type(chirp::chat::PRIVATE);
    curse.set_content("curse badword here");
    ASSERT_TRUE(user.SendAndWait(chirp::gateway::SEND_MESSAGE_REQ, curse, 2));
    {
      const auto frames = user.FramesOf(chirp::gateway::SEND_MESSAGE_RESP);
      ASSERT_EQ(frames.size(), 1u);
      chirp::chat::SendMessageResponse resp;
      ASSERT_TRUE(resp.ParseFromString(frames[0].body()));
      // 词库命中 + reject 策略 → 专码 WORD_FILTERED（10），不是 INVALID_PARAM。
      EXPECT_EQ(resp.code(), chirp::common::WORD_FILTERED);
    }

    // 私聊节奏 1s——filter 拒绝也耗节奏槽，隔开再发；离线接收方回
    // TARGET_OFFLINE（basic 形态已入队语义）。
    std::this_thread::sleep_for(std::chrono::milliseconds(1100));
    chirp::chat::SendMessageRequest clean = curse;
    clean.set_content("clean text");
    ASSERT_TRUE(user.SendAndWait(chirp::gateway::SEND_MESSAGE_REQ, clean, 3));
    {
      const auto frames = user.FramesOf(chirp::gateway::SEND_MESSAGE_RESP);
      ASSERT_EQ(frames.size(), 2u);
      chirp::chat::SendMessageResponse resp;
      ASSERT_TRUE(resp.ParseFromString(frames[1].body()));
      EXPECT_EQ(resp.code(), chirp::common::TARGET_OFFLINE);
    }

    user.Disconnect();
    raise(SIGTERM);
    EXPECT_TRUE(WaitFor([&] { return rc.load() != 12345; }, 3000));
    EXPECT_EQ(rc.load(), 0);
    runner.join();
    return;
  }
}

// WS 平面：同一套 HandlePacket/HandleDisconnect 挂在 WebSocketServer 上的
// 两条委托 lambda。
TEST(BasicMainTest, WebSocketPlaneSession) {
  chirp::common::Logger::Instance().SetLevel(chirp::common::Logger::Level::kError);
  constexpr int kMaxBindAttempts = 5;
  for (int attempt = 0; attempt < kMaxBindAttempts; ++attempt) {
    const uint16_t port = BasicMainProbePort();
    const uint16_t ws_port = BasicMainProbePort();
    const std::vector<std::string> args = {
        "chat", "--port", std::to_string(port), "--ws_port", std::to_string(ws_port),
        "--redis_host", "127.0.0.1", "--redis_port", "1", "--ack_timeout_ms", "60000"};
    std::vector<std::unique_ptr<char[]>> holds;
    std::vector<char*> argv = BasicMainArgv(args, holds);
    std::atomic<int> rc{12345};
    std::thread runner = BasicMainRun(rc, static_cast<int>(argv.size()), argv.data());

    asio::io_context io;
    chirp::network::WebSocketClient ws(io);
    std::vector<chirp::gateway::Packet> received;
    std::mutex rx_mu;
    ws.SetCallbacks(
        [&](std::shared_ptr<chirp::network::Session>, std::string&& payload) {
          chirp::gateway::Packet pkt;
          // 服务端 WS 帧可能带 u32-BE 长度前缀，两种形态都要接受
          // （managers 套件同款处理）。
          if (!pkt.ParseFromArray(payload.data(), static_cast<int>(payload.size())) &&
              payload.size() > 4) {
            pkt.ParseFromArray(payload.data() + 4,
                               static_cast<int>(payload.size()) - 4);
          }
          std::lock_guard<std::mutex> lock(rx_mu);
          received.push_back(pkt);
        },
        [](std::shared_ptr<chirp::network::Session>) {});

    bool connected = false;
    bool died = false;
    for (int i = 0; i < 400 && !connected; ++i) {
      connected = ws.Connect("127.0.0.1", ws_port, "/ws");
      if (!connected) {
        if (rc.load() != 12345) {
          died = true;
          break;
        }
        std::this_thread::sleep_for(std::chrono::milliseconds(5));
      }
    }
    if (!connected || died) {
      runner.join();
      ASSERT_LT(attempt, kMaxBindAttempts - 1) << "service kept losing the port race";
      continue;
    }

    chirp::auth::LoginRequest login;
    login.set_token("ws-user");
    ws.GetSession()->Send(
        BasicMainFrame(chirp::gateway::LOGIN_REQ, 1, login));
    // WS 客户端的收发都跑在本测试的 io 上：等待谓词里必须泵 io
    // （Session::Send 只是 post 到 strand）。
    ASSERT_TRUE(WaitFor([&] {
      io.poll();
      io.restart();
      std::lock_guard<std::mutex> lock(rx_mu);
      return !received.empty();
    }));
    {
      std::lock_guard<std::mutex> lock(rx_mu);
      ASSERT_EQ(received.size(), 1u);
      EXPECT_EQ(received[0].msg_id(), chirp::gateway::LOGIN_RESP);
      chirp::auth::LoginResponse resp;
      ASSERT_TRUE(resp.ParseFromString(received[0].body()));
      EXPECT_EQ(resp.code(), chirp::common::OK);
      EXPECT_EQ(resp.user_id(), "ws-user");
    }

    ws.Disconnect();
    // 让服务侧处理 WS 断开（disconnect lambda）后再停机。
    std::this_thread::sleep_for(std::chrono::milliseconds(200));
    raise(SIGTERM);
    EXPECT_TRUE(WaitFor([&] { return rc.load() != 12345; }, 3000));
    EXPECT_EQ(rc.load(), 0);
    runner.join();
    return;
  }
}

// 跨平面全链路：同进程两个 main() 实例——hub 模式实例 A + spoke 实例 B。
// B 的群消息上行 CHANNEL_MESSAGE_NOTIFY 到 A 的 hub，A 的 PlayerDirectory 按
// 订阅 fan-out 出私信副本（deliver_copy），p1 掉线时落离线队列重登补投；
// 反向 p1 以 <game_id>:<频道> 前缀发送，hub 解析绑定注入 spoke，spoke 把
// 频道名当目标用户投给 B 本地会话（g2 以该名登录收信）。
TEST(BasicMainTest, CrossPlaneHubSpokeFanoutReplyAndInject) {
  chirp::common::Logger::Instance().SetLevel(chirp::common::Logger::Level::kError);
  constexpr int kMaxBindAttempts = 5;
  for (int attempt = 0; attempt < kMaxBindAttempts; ++attempt) {
    const uint16_t a_port = BasicMainProbePort();
    const uint16_t a_ws = BasicMainProbePort();
    const uint16_t a_hub = BasicMainProbePort();
    const uint16_t b_port = BasicMainProbePort();
    const uint16_t b_ws = BasicMainProbePort();
    const std::vector<std::string> hub_args = {
        "chat", "--port", std::to_string(a_port), "--ws_port", std::to_string(a_ws),
        "--redis_host", "127.0.0.1", "--redis_port", "1",
        "--hub_mode", "1", "--hub_peer_port", std::to_string(a_hub),
        "--allowed_peers", "game_chat:peer-s3cret,spare:sec2",
        "--gateway_service_secret", "trust"};
    const std::vector<std::string> spoke_args = {
        "chat", "--port", std::to_string(b_port), "--ws_port", std::to_string(b_ws),
        "--redis_host", "127.0.0.1", "--redis_port", "1",
        "--app_chat_host", "127.0.0.1", "--app_chat_port", std::to_string(a_hub),
        "--game_service_id", "game_chat", "--game_service_secret", "peer-s3cret",
        "--game_id", "game42"};
    std::vector<std::unique_ptr<char[]>> hub_holds, spoke_holds;
    std::vector<char*> hub_argv = BasicMainArgv(hub_args, hub_holds);
    std::vector<char*> spoke_argv = BasicMainArgv(spoke_args, spoke_holds);
    std::atomic<int> rc_hub{12345}, rc_spoke{12345};
    std::thread hub_runner =
        BasicMainRun(rc_hub, static_cast<int>(hub_argv.size()), hub_argv.data());
    std::thread spoke_runner =
        BasicMainRun(rc_spoke, static_cast<int>(spoke_argv.size()), spoke_argv.data());

    BasicMainClient p1, g1, g2;
    if (!p1.ConnectOrDied(a_port, rc_hub) || !g1.ConnectOrDied(b_port, rc_spoke) ||
        !g2.ConnectOrDied(b_port, rc_spoke)) {
      p1.Disconnect();
      g1.Disconnect();
      g2.Disconnect();
      hub_runner.join();
      spoke_runner.join();
      ASSERT_LT(attempt, kMaxBindAttempts - 1) << "service kept losing the port race";
      continue;
    }

    // p1 挂 hub：登录 + 内部面信任门 + game42 绑定（u-1）。
    chirp::auth::LoginRequest p1_login;
    p1_login.set_token("p1");
    ASSERT_TRUE(p1.SendAndWait(chirp::gateway::LOGIN_REQ, p1_login, 1));
    chirp::game_server_gateway::ServerAuthRequest auth;
    auth.set_secret("trust");
    ASSERT_TRUE(p1.SendAndWait(chirp::gateway::SERVER_AUTH_REQ, auth, 2));
    {
      const auto frames = p1.FramesOf(chirp::gateway::SERVER_AUTH_RESP);
      ASSERT_EQ(frames.size(), 1u);
      chirp::game_server_gateway::ServerAuthResponse resp;
      ASSERT_TRUE(resp.ParseFromString(frames[0].body()));
      EXPECT_EQ(resp.code(), chirp::common::OK);
    }
    chirp::game_server_gateway::BindPlayerIdentityRequest bind;
    bind.set_binding_id("b1");
    bind.set_player_id("p1");
    bind.set_game_id("game42");
    bind.set_game_user_id("u-1");
    ASSERT_TRUE(p1.SendAndWait(chirp::gateway::BIND_PLAYER_IDENTITY_REQ, bind, 3));
    {
      const auto frames = p1.FramesOf(chirp::gateway::BIND_PLAYER_IDENTITY_RESP);
      ASSERT_EQ(frames.size(), 1u);
      chirp::game_server_gateway::BindPlayerIdentityResponse resp;
      ASSERT_TRUE(resp.ParseFromString(frames[0].body()));
      EXPECT_EQ(resp.code(), chirp::common::OK);
    }

    // g1 挂 spoke：登录 + 建群（spoke 上行只对非私聊 OK 发送）。
    chirp::auth::LoginRequest g1_login;
    g1_login.set_token("g1");
    ASSERT_TRUE(g1.SendAndWait(chirp::gateway::LOGIN_REQ, g1_login, 1));
    chirp::chat::CreateGroupRequest create;
    create.set_creator_id("g1");
    create.set_group_name("e2e");
    create.add_initial_members("g1");
    // 建群回 CREATE_GROUP_RESP 之外还会推 GROUP_CREATED_NOTIFY，累计帧数等待
    // 会被 notify 抢跑——按 msg id 等待。
    g1.SendFireAndForget(chirp::gateway::CREATE_GROUP_REQ, create);
    ASSERT_TRUE(g1.WaitPumped(
        [&] { return g1.CountOf(chirp::gateway::CREATE_GROUP_RESP) >= 1; }));
    std::string group_id;
    {
      const auto frames = g1.FramesOf(chirp::gateway::CREATE_GROUP_RESP);
      ASSERT_EQ(frames.size(), 1u);
      chirp::chat::CreateGroupResponse resp;
      ASSERT_TRUE(resp.ParseFromString(frames[0].body()));
      ASSERT_EQ(resp.code(), chirp::common::OK);
      group_id = resp.group_id();
      ASSERT_FALSE(group_id.empty());
    }

    // p1 订阅 game42 的该群频道：fan-out 的收件人由此解析。
    chirp::game_server_gateway::SubscribePlayerChannelRequest sub;
    sub.set_player_id("p1");
    sub.set_game_id("game42");
    sub.set_channel_id(group_id);
    p1.SendFireAndForget(chirp::gateway::SUBSCRIBE_PLAYER_CHANNEL_REQ, sub);
    ASSERT_TRUE(p1.WaitPumped(
        [&] { return p1.CountOf(chirp::gateway::SUBSCRIBE_PLAYER_CHANNEL_RESP) >= 1; }));
    {
      const auto frames = p1.FramesOf(chirp::gateway::SUBSCRIBE_PLAYER_CHANNEL_RESP);
      ASSERT_EQ(frames.size(), 1u);
      chirp::game_server_gateway::SubscribePlayerChannelResponse resp;
      ASSERT_TRUE(resp.ParseFromString(frames[0].body()));
      EXPECT_EQ(resp.code(), chirp::common::OK);
    }

    // 群消息上行 → hub fan-out → 订阅者私信副本。spoke 注册是异步重连的：
    // 副包没到就按 2.2s 节奏间隔重试（公会频道 2s 节奏 + 内容变化避开
    // RepeatGuard），副本到达本身就是注册成功的判据。
    std::string fanned_content;
    bool fanned = false;
    for (int i = 0; i < 4 && !fanned; ++i) {
      if (i > 0) {
        std::this_thread::sleep_for(std::chrono::milliseconds(2200));
      }
      fanned_content = i == 0 ? "cross hello" : "cross hello " + std::to_string(i);
      chirp::chat::SendMessageRequest up;
      up.set_sender_id("g1");
      up.set_channel_type(chirp::chat::GUILD);
      up.set_channel_id(group_id);
      up.set_content(fanned_content);
      const size_t before = g1.CountOf(chirp::gateway::SEND_MESSAGE_RESP);
      g1.SendFireAndForget(chirp::gateway::SEND_MESSAGE_REQ, up);
      ASSERT_TRUE(g1.WaitPumped([&] {
        return g1.CountOf(chirp::gateway::SEND_MESSAGE_RESP) > before;
      }));
      {
        const auto frames = g1.FramesOf(chirp::gateway::SEND_MESSAGE_RESP);
        chirp::chat::SendMessageResponse resp;
        ASSERT_TRUE(resp.ParseFromString(frames.back().body()));
        ASSERT_EQ(resp.code(), chirp::common::OK);
      }
      fanned = p1.WaitPumped([&] {
        return p1.CountOf(chirp::gateway::CHAT_MESSAGE_NOTIFY) >= 1;
      }, 2500);
    }
    ASSERT_TRUE(fanned) << "spoke uplink never reached the hub fan-out";
    {
      const auto frames = p1.FramesOf(chirp::gateway::CHAT_MESSAGE_NOTIFY);
      ASSERT_EQ(frames.size(), 1u);
      chirp::chat::ChatMessage copy;
      ASSERT_TRUE(copy.ParseFromString(frames[0].body()));
      EXPECT_EQ(copy.sender_id(), "game42:g1");
      EXPECT_EQ(copy.channel_type(), chirp::chat::PRIVATE);
      EXPECT_EQ(copy.channel_id(), "game42:g1|p1");
      EXPECT_EQ(copy.content(), fanned_content);
    }

    // 离线副本臂：p1 掉线后的上行 → AddOffline(+无通知客户端的 NotifyOffline
    // 空转)，重登补投。
    p1.Disconnect();
    std::this_thread::sleep_for(std::chrono::milliseconds(2200));
    chirp::chat::SendMessageRequest offline_send;
    offline_send.set_sender_id("g1");
    offline_send.set_channel_type(chirp::chat::GUILD);
    offline_send.set_channel_id(group_id);
    offline_send.set_content("offline copy");
    {
      const size_t before = g1.CountOf(chirp::gateway::SEND_MESSAGE_RESP);
      g1.SendFireAndForget(chirp::gateway::SEND_MESSAGE_REQ, offline_send);
      ASSERT_TRUE(g1.WaitPumped([&] {
        return g1.CountOf(chirp::gateway::SEND_MESSAGE_RESP) > before;
      }));
    }
    BasicMainClient p1_back;
    ASSERT_TRUE(p1_back.ConnectOrDied(a_port, rc_hub));
    // 重登回 LOGIN_RESP + 离线补投 notify，按各自 msg id 等待。
    p1_back.SendFireAndForget(chirp::gateway::LOGIN_REQ, p1_login);
    ASSERT_TRUE(p1_back.WaitPumped([&] {
      return p1_back.CountOf(chirp::gateway::LOGIN_RESP) >= 1 &&
             p1_back.CountOf(chirp::gateway::CHAT_MESSAGE_NOTIFY) >= 1;
    }));
    {
      const auto frames = p1_back.FramesOf(chirp::gateway::CHAT_MESSAGE_NOTIFY);
      ASSERT_EQ(frames.size(), 1u);
      chirp::chat::ChatMessage copy;
      ASSERT_TRUE(copy.ParseFromString(frames[0].body()));
      EXPECT_EQ(copy.sender_id(), "game42:g1");
      EXPECT_EQ(copy.content(), "offline copy");
    }

    // 跨平面回复：g2 以目标频道名登录收信；p1 前缀发送 → hub 解析绑定 → 注入
    // spoke → B 把频道名当目标用户投递。
    chirp::auth::LoginRequest g2_login;
    g2_login.set_token("re-chan");
    ASSERT_TRUE(g2.SendAndWait(chirp::gateway::LOGIN_REQ, g2_login, 1));
    chirp::chat::SendMessageRequest reply;
    reply.set_sender_id("p1");
    reply.set_channel_type(chirp::chat::GUILD);
    reply.set_channel_id("game42:re-chan");
    reply.set_content("reply into game");
    p1_back.SendFireAndForget(chirp::gateway::SEND_MESSAGE_REQ, reply);
    ASSERT_TRUE(p1_back.WaitPumped(
        [&] { return p1_back.CountOf(chirp::gateway::SEND_MESSAGE_RESP) >= 1; }));
    {
      const auto frames = p1_back.FramesOf(chirp::gateway::SEND_MESSAGE_RESP);
      ASSERT_EQ(frames.size(), 1u);
      chirp::chat::SendMessageResponse resp;
      ASSERT_TRUE(resp.ParseFromString(frames[0].body()));
      EXPECT_EQ(resp.code(), chirp::common::OK);
    }
    ASSERT_TRUE(g2.WaitPumped([&] {
      return g2.CountOf(chirp::gateway::CHAT_MESSAGE_NOTIFY) >= 1;
    }, 3000));
    {
      const auto frames = g2.FramesOf(chirp::gateway::CHAT_MESSAGE_NOTIFY);
      ASSERT_EQ(frames.size(), 1u);
      chirp::chat::ChatMessage msg;
      ASSERT_TRUE(msg.ParseFromString(frames[0].body()));
      EXPECT_EQ(msg.sender_id(), "u-1");
      EXPECT_EQ(msg.receiver_id(), "re-chan");
      EXPECT_EQ(msg.content(), "reply into game");
    }

    // 注入离线臂：目标本地无会话 → 入 B 的离线队列（无外部可观测，执行路径；
    // OK 回码即 hub 解析 + SendInject 成功）。
    std::this_thread::sleep_for(std::chrono::milliseconds(2200));
    chirp::chat::SendMessageRequest ghost = reply;
    ghost.set_channel_id("game42:ghost-chan");
    ghost.set_content("nobody home");
    {
      const size_t before = p1_back.CountOf(chirp::gateway::SEND_MESSAGE_RESP);
      p1_back.SendFireAndForget(chirp::gateway::SEND_MESSAGE_REQ, ghost);
      ASSERT_TRUE(p1_back.WaitPumped([&] {
        return p1_back.CountOf(chirp::gateway::SEND_MESSAGE_RESP) > before;
      }));
      const auto frames = p1_back.FramesOf(chirp::gateway::SEND_MESSAGE_RESP);
      ASSERT_EQ(frames.size(), 2u);
      chirp::chat::SendMessageResponse resp;
      ASSERT_TRUE(resp.ParseFromString(frames[1].body()));
      EXPECT_EQ(resp.code(), chirp::common::OK);
    }

    p1_back.Disconnect();
    g1.Disconnect();
    g2.Disconnect();
    // 进程内两个 signal_set 都会收到同一个 SIGTERM。
    raise(SIGTERM);
    EXPECT_TRUE(WaitFor([&] { return rc_hub.load() != 12345; }, 3000));
    EXPECT_EQ(rc_hub.load(), 0);
    EXPECT_TRUE(WaitFor([&] { return rc_spoke.load() != 12345; }, 3000));
    EXPECT_EQ(rc_spoke.load(), 0);
    hub_runner.join();
    spoke_runner.join();
    return;
  }
}

}  // namespace
