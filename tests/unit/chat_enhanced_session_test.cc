// Enhanced chat build (app_chat) session semantics: DistributedChatState
// must share the basic build's (user, device) mutual-kick kernel from
// libs/network/session_registry (TODO: enhanced 会话语义修复) and fan local
// delivery out to every healthy device instead of one user-dimension slot.

#include <gtest/gtest.h>

#include <atomic>
#include <chrono>
#include <cstring>
#include <functional>
#include <future>
#include <memory>
#include <mutex>
#include <string>
#include <thread>
#include <type_traits>
#include <utility>
#include <vector>

#include <asio.hpp>

#include "common/jwt.h"
#include "delivery_ack_manager.h"
#include "network/chat_peer_hub.h"
#include "network/chat_peer_link.h"
#include "network/message_router.h"
#include "network/server_gateway_peer.h"
#include "network/session.h"
#include "proto/auth.pb.h"
#include "proto/chat.pb.h"
#include "proto/common.pb.h"
#include "proto/game_server_gateway.pb.h"
#include "proto/gateway.pb.h"

#include "fake_mysql.h"
#include "fake_servers.h"
#include "in_memory_redis.h"

// The enhanced main keeps its session internals (DistributedChatState,
// HandleLogin, HandleSendMessage, HealthyLocalSessions, ...) in an anonymous
// namespace; pull the file in with main() renamed so the tests below can
// drive them directly - the same pattern chat_managers_test.cc uses for
// main_distributed.cc.
#define main chirp_chat_enhanced_main
#include "main_enhanced.cc"
#undef main

namespace {

using chirp::chat::DeliveryAckManager;
using chirp::gateway::Packet;

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
// parses the payload (ProtobufFraming::Decode itself expects a bare message).
bool DecodeFramed(const std::string& framed, Packet* out) {
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

// Every frame the session received, decoded as a gateway Packet.
std::vector<Packet> FramesOf(const MockSession& session) {
  std::vector<Packet> frames;
  for (const auto& framed : session.sent) {
    Packet pkt;
    if (DecodeFramed(framed, &pkt)) {
      frames.push_back(std::move(pkt));
    }
  }
  return frames;
}

std::vector<Packet> FramesOf(const MockSession& session, chirp::gateway::MsgID msg_id) {
  std::vector<Packet> matches;
  for (const auto& pkt : FramesOf(session)) {
    if (pkt.msg_id() == msg_id) {
      matches.push_back(pkt);
    }
  }
  return matches;
}

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

// The block-list wiring (in flight elsewhere) threads a DeliveryPrefs*
// through HandleLogin/HandleSendMessage; the committed signatures do not
// carry it. Detect which form is present by casting &HandleLogin to the
// matching function-pointer type (SFINAE in the immediate context), then
// dispatch with if constexpr so only the viable call is instantiated. Both
// call expressions mention `prefs` - a template parameter - so they stay
// type-dependent: otherwise GCC checks arity at definition time and hard-
// errors on the other form instead of discarding it.
using HandleLoginWithPrefsFn = void (*)(const chirp::auth::LoginRequest&,
                                        const std::shared_ptr<chirp::network::Session>&,
                                        const std::shared_ptr<DistributedChatState>&,
                                        const std::shared_ptr<HybridMessageStore>&,
                                        const std::shared_ptr<chirp::network::MessageRouter>&,
                                        const chirp::common::LoginTokenVerifier*,
                                        DeliveryAckManager*,
                                        const chirp::chat::DeliveryPrefs*, int64_t);

using HandleSendMessageWithPrefsFn = void (*)(
    const chirp::chat::SendMessageRequest&,
    const std::shared_ptr<chirp::network::Session>&,
    const std::shared_ptr<DistributedChatState>&, const std::shared_ptr<HybridMessageStore>&,
    const std::shared_ptr<MessageDeliveryTracker>&, DeliveryAckManager*,
    const chirp::chat::DeliveryPrefs*,
    const std::shared_ptr<chirp::network::MessageRouter>&, chirp::network::ServerGatewayPeer*,
    const std::string&, const std::string&, chirp::network::ChatPeerLink*, const std::string&,
    chirp::chat::MessageEditHandlers*, chirp::chat::PlayerDirectory*, chirp::network::ChatPeerHub*,
    int64_t);

template <typename Fn, typename = void>
struct CanCastHandleLogin : std::false_type {};
template <typename Fn>
struct CanCastHandleLogin<Fn, std::void_t<decltype(static_cast<Fn>(&HandleLogin))>>
    : std::true_type {};

template <typename Fn, typename = void>
struct CanCastHandleSendMessage : std::false_type {};
template <typename Fn>
struct CanCastHandleSendMessage<Fn, std::void_t<decltype(static_cast<Fn>(&HandleSendMessage))>>
    : std::true_type {};

template <typename Prefs>
void InvokeLogin(const chirp::auth::LoginRequest& req,
                 const std::shared_ptr<chirp::network::Session>& session,
                 const std::shared_ptr<DistributedChatState>& state,
                 const std::shared_ptr<HybridMessageStore>& store,
                 const std::shared_ptr<chirp::network::MessageRouter>& router,
                 const chirp::common::LoginTokenVerifier* verifier, DeliveryAckManager* acks,
                 Prefs* prefs, int64_t seq) {
  if constexpr (CanCastHandleLogin<HandleLoginWithPrefsFn>::value) {
    HandleLogin(req, session, state, store, router, verifier, acks, prefs, seq);
  } else {
    // The always-zero `prefs` term keeps this call type-dependent (and the
    // value unchanged); the branch is only ever instantiated when it fits.
    HandleLogin(req, session, state, store, router, verifier, acks,
                seq + (prefs == nullptr ? int64_t{0} : int64_t{0}));
  }
}

template <typename Prefs>
void InvokeSendMessage(const chirp::chat::SendMessageRequest& req,
                       const std::shared_ptr<chirp::network::Session>& session,
                       const std::shared_ptr<DistributedChatState>& state,
                       const std::shared_ptr<HybridMessageStore>& store,
                       const std::shared_ptr<MessageDeliveryTracker>& tracker,
                       DeliveryAckManager* acks, Prefs* prefs,
                       const std::shared_ptr<chirp::network::MessageRouter>& router,
                       chirp::network::ServerGatewayPeer* hub_peer,
                       const std::string& npc_service_id, const std::string& npc_prefix,
                       chirp::network::ChatPeerLink* spoke_link, const std::string& spoke_game_id,
                       chirp::chat::MessageEditHandlers* edits,
                       chirp::chat::PlayerDirectory* directory, chirp::network::ChatPeerHub* hub,
                       int64_t seq) {
  if constexpr (CanCastHandleSendMessage<HandleSendMessageWithPrefsFn>::value) {
    HandleSendMessage(req, session, state, store, tracker, acks, prefs, router, hub_peer,
                      npc_service_id, npc_prefix, spoke_link, spoke_game_id, edits, directory, hub,
                      seq);
  } else {
    HandleSendMessage(req, session, state, store, tracker, acks, router, hub_peer,
                      npc_service_id, npc_prefix, spoke_link, spoke_game_id,
                      seq + (prefs == nullptr ? int64_t{0} : int64_t{0}));
  }
}

class EnhancedSessionTest : public ::testing::Test {
 protected:
  void SetUp() override {
    // The scripted fake MySQL is target-global state; a test that fails a
    // prefix (the async-store callback vector) must not leak it forward.
    chirp_test::fake_mysql::Reset();
    state_ = std::make_shared<DistributedChatState>();
    state_->instance_id = "enh-test";

    // A dead Redis port keeps the store deterministic: history/offline
    // writes fail over to the in-memory fallback instead of a shared
    // server, and the io_context is never run so the posted MySQL writes
    // never reach the fake driver either.
    store_config_.redis_port = 1;
    store_config_.mysql_pool_size = 1;
    store_ = std::make_shared<HybridMessageStore>(io_, store_config_);
    tracker_ = std::make_shared<MessageDeliveryTracker>(io_, store_);
    router_ = std::make_shared<chirp::network::MessageRouter>(io_, "127.0.0.1", 1);
    // 生产同一份的撤回装配（MakeEnhancedRecallRuntime 在 main_enhanced.cc 里，
    // 测试经 #include 直驱）：默认窗口 2 分钟、可撤回频道 private+guild。
    recall_ = MakeEnhancedRecallRuntime(state_, store_, edit_config_);
  }

  // Scaffold private send through the real send path; returns the response
  // code, and message_id (optional) receives the server-minted id. store/edits
  // 覆盖默认装配：撤回端到端用例会换成带真历史层的那一套。
  chirp::common::ErrorCode SendPrivate(const std::shared_ptr<MockSession>& sender_session,
                                       const std::string& sender, const std::string& receiver,
                                       const std::string& content, std::string* message_id,
                                       const std::shared_ptr<HybridMessageStore>& store_override =
                                           nullptr,
                                       chirp::chat::MessageEditHandlers* edits_override = nullptr) {
    chirp::chat::SendMessageRequest req;
    req.set_sender_id(sender);
    req.set_receiver_id(receiver);
    req.set_channel_type(chirp::chat::PRIVATE);
    req.set_content(content);
    InvokeSendMessage(req, sender_session, state_, store_override ? store_override : store_, tracker_, /*acks=*/nullptr,
                      &delivery_prefs_, router_, /*hub_peer=*/nullptr, /*npc_service_id=*/"",
                      /*npc_prefix=*/"npc:", /*spoke_link=*/nullptr, /*spoke_game_id=*/"",
                      edits_override ? edits_override : recall_.handlers.get(),
                      /*directory=*/nullptr, /*hub=*/nullptr, /*seq=*/1);
    // 同会话多次发送会累积历史帧，这里只断言"有响应"并取最新一帧。
    const auto resps = FramesOf(*sender_session, chirp::gateway::SEND_MESSAGE_RESP);
    EXPECT_FALSE(resps.empty());
    if (resps.empty()) {
      return chirp::common::SERVER_UNAVAILABLE;
    }
    chirp::chat::SendMessageResponse resp;
    if (!resp.ParseFromString(resps.back().body())) {
      return chirp::common::INVALID_PARAM;
    }
    if (message_id != nullptr) {
      *message_id = resp.message_id();
    }
    return resp.code();
  }

  // DELETE_MESSAGE dispatch body through the production wiring; returns the
  // response the requester got.
  chirp::chat::DeleteMessageResponse Recall(const std::shared_ptr<MockSession>& session,
                                            const std::string& message_id,
                                            const std::string& user_id,
                                            bool hard_delete = false,
                                            chirp::chat::MessageEditHandlers* edits_override =
                                                nullptr) {
    chirp::chat::DeleteMessageRequest req;
    req.set_message_id(message_id);
    req.set_user_id(user_id);
    req.set_is_hard_delete(hard_delete);
    HandleDeleteMessage(req, session, state_,
                        edits_override ? *edits_override : *recall_.handlers, /*seq=*/9);
    const auto resps = FramesOf(*session, chirp::gateway::DELETE_MESSAGE_RESP);
    chirp::chat::DeleteMessageResponse resp;
    // 哨兵回码：没响应/解析不了都暴露成 SERVER_UNAVAILABLE，调用方的具体
    // 回码断言会立刻失败，不会把"没回包"误读成 OK。
    if (resps.empty() || !resp.ParseFromString(resps.back().body())) {
      resp.set_code(chirp::common::SERVER_UNAVAILABLE);
    }
    return resp;
  }

  // 撤回端到端的墓碑/历史断言需要真历史层：主 fixture 的 Redis 端口是死的，
  // HybridMessageStore 只有离线队列有内存回退，历史层没有。这几个用例自带
  // InMemoryRedis + store + 召回装配（仍是生产同一个 MakeEnhancedRecallRuntime，
  // 只是绑到活历史层的 store 上）。通过出参在调用方栈上构造，避免把
  // FakeRedisServer 的 handler 引用悬垂在返回值移动上。
  struct LiveHistoryRecall {
    chirp_test::InMemoryRedis redis;
    std::unique_ptr<chirp_test::FakeRedisServer> fake;
    std::shared_ptr<HybridMessageStore> store;
    EnhancedRecallRuntime recall;
  };

  void StartLiveHistory(LiveHistoryRecall& lh) {
    lh.fake = std::make_unique<chirp_test::FakeRedisServer>(
        [&lh](const std::vector<std::string>& args) { return lh.redis.Handle(args); });
    MessageStoreConfig cfg = store_config_;
    cfg.redis_port = lh.fake->port();
    lh.store = std::make_shared<HybridMessageStore>(io_, cfg);
    lh.recall = MakeEnhancedRecallRuntime(state_, lh.store, edit_config_);
  }

  // Scaffold LOGIN_REQ: token doubles as the user id (the verifier stays
  // disabled - nullptr - exactly like a build without --token_secret).
  void Login(const std::shared_ptr<MockSession>& session, const std::string& token,
             const std::string& device_id, int64_t seq, const std::string& platform = "web") {
    chirp::auth::LoginRequest req;
    req.set_token(token);
    req.set_device_id(device_id);
    req.set_platform(platform);
    InvokeLogin(req, session, state_, store_, router_, nullptr, nullptr, &delivery_prefs_, seq);
  }

  // Sends a fully-formed request through the production send path; returns
  // the newest SEND_MESSAGE_RESP code (sentinel SERVER_UNAVAILABLE when no
  // parseable frame came back, so "never answered" cannot read as OK).
  chirp::common::ErrorCode SendReq(
      const chirp::chat::SendMessageRequest& req,
      const std::shared_ptr<MockSession>& sender_session,
      const std::shared_ptr<HybridMessageStore>& store_override = nullptr,
      chirp::chat::MessageEditHandlers* edits_override = nullptr,
      chirp::network::ServerGatewayPeer* hub_peer = nullptr,
      const std::string& npc_service_id = "",
      chirp::network::ChatPeerLink* spoke_link = nullptr,
      const std::string& spoke_game_id = "",
      chirp::chat::PlayerDirectory* directory = nullptr,
      chirp::network::ChatPeerHub* hub = nullptr,
      std::string* message_id = nullptr) {
    InvokeSendMessage(req, sender_session, state_, store_override ? store_override : store_,
                      tracker_, /*acks=*/nullptr, &delivery_prefs_, router_, hub_peer,
                      npc_service_id, /*npc_prefix=*/"npc:", spoke_link, spoke_game_id,
                      edits_override, directory, hub, /*seq=*/1);
    const auto resps = FramesOf(*sender_session, chirp::gateway::SEND_MESSAGE_RESP);
    chirp::chat::SendMessageResponse resp;
    if (resps.empty() || !resp.ParseFromString(resps.back().body())) {
      return chirp::common::SERVER_UNAVAILABLE;
    }
    if (message_id != nullptr) {
      *message_id = resp.message_id();
    }
    return resp.code();
  }

  // LOGIN_REQ with the full custom surface (verifier / acks / router), for
  // the paths the scaffold helper cannot reach.
  void LoginReq(const chirp::auth::LoginRequest& req,
                const std::shared_ptr<MockSession>& session,
                const std::shared_ptr<chirp::network::MessageRouter>& router_override = nullptr,
                const chirp::common::LoginTokenVerifier* verifier = nullptr,
                chirp::chat::DeliveryAckManager* acks = nullptr) {
    InvokeLogin(req, session, state_, store_, router_override ? router_override : router_,
                verifier, acks, &delivery_prefs_, /*seq=*/1);
  }

  asio::io_context io_;
  MessageStoreConfig store_config_;
  std::shared_ptr<DistributedChatState> state_;
  std::shared_ptr<HybridMessageStore> store_;
  std::shared_ptr<MessageDeliveryTracker> tracker_;
  std::shared_ptr<chirp::network::MessageRouter> router_;
  // The Invoke* wrappers always thread this through; the committed handler
  // form drops it (the in-flight one dereferences it per delivery, with no
  // null guard), so hand them a real, empty instance either way.
  chirp::chat::DeliveryPrefs delivery_prefs_;
  // 撤回装配：默认 EditConfig（窗口 2 分钟、private+guild 可撤回）。
  chirp::chat::EditConfig edit_config_;
  EnhancedRecallRuntime recall_;
};

// --- DistributedChatState over the shared SessionRegistry -----------------

TEST_F(EnhancedSessionTest, AddSessionSamePlatformReturnsPreviousSession) {
  auto first = std::make_shared<MockSession>();
  auto second = std::make_shared<MockSession>();

  EXPECT_EQ(state_->AddSession("alice", "phone", "s1", first, "web"), nullptr);
  auto kicked = state_->AddSession("alice", "tablet", "s2", second, "web");

  // 多端在线（P0）：同一个 (user, platform) 槽位被新登录接管，旧会话交还给
  // 调用方去顶（KICK_NOTIFY）——绝不静默覆盖把旧连接变成孤儿。同 device_id
  // 断线重连走的也是这条路（同 platform → 同槽位，幂等重绑）。
  ASSERT_TRUE(kicked);
  EXPECT_EQ(kicked, first);
  EXPECT_EQ(state_->GetUserId(second), "alice");
  // The displaced session keeps its identity until its (late) disconnect
  // arrives; the registry only moves the platform slot.
  EXPECT_EQ(state_->GetUserId(first), "alice");
}

TEST_F(EnhancedSessionTest, AddSessionOtherPlatformCoexists) {
  auto web = std::make_shared<MockSession>();
  auto phone = std::make_shared<MockSession>();

  EXPECT_EQ(state_->AddSession("alice", "tab-1", "s1", web, "web"), nullptr);
  EXPECT_EQ(state_->AddSession("alice", "p1", "s2", phone, "ios"), nullptr);

  EXPECT_EQ(state_->GetUserId(web), "alice");
  EXPECT_EQ(state_->GetUserId(phone), "alice");
  EXPECT_EQ(HealthyLocalSessions(state_, "alice").size(), 2u);
}

TEST_F(EnhancedSessionTest, AddSessionEmptyDeviceSharesDefaultSlot) {
  auto legacy_a = std::make_shared<MockSession>();
  auto legacy_b = std::make_shared<MockSession>();

  EXPECT_EQ(state_->AddSession("alice", "", "s1", legacy_a, ""), nullptr);
  // Legacy clients that never send a platform all map onto the "default"
  // slot and keep the historical one-session-per-user kick behavior.
  auto kicked = state_->AddSession("alice", "", "s2", legacy_b, "");
  ASSERT_TRUE(kicked);
  EXPECT_EQ(kicked, legacy_a);
  EXPECT_EQ(HealthyLocalSessions(state_, "alice").size(), 1u);
}

TEST_F(EnhancedSessionTest, StaleDisconnectKeepsNewerSession) {
  auto old_session = std::make_shared<MockSession>();
  auto new_session = std::make_shared<MockSession>();

  // Re-login takes over the (user, device) slot first; the old connection's
  // disconnect only arrives afterwards (e.g. its late FIN). The stale
  // disconnect must not unregister the session that now owns the slot.
  EXPECT_EQ(state_->AddSession("carol", "p1", "s1", old_session, "ios"), nullptr);
  ASSERT_TRUE(state_->AddSession("carol", "p2", "s2", new_session, "ios"));
  state_->RemoveSession(old_session);

  EXPECT_EQ(state_->GetUserId(old_session), "");
  EXPECT_EQ(state_->GetUserId(new_session), "carol");
  auto healthy = HealthyLocalSessions(state_, "carol");
  ASSERT_EQ(healthy.size(), 1u);
  EXPECT_EQ(healthy[0], new_session);
}

TEST_F(EnhancedSessionTest, RemoveSessionOnlyClearsOwnPlatformSlot) {
  auto web = std::make_shared<MockSession>();
  auto phone = std::make_shared<MockSession>();
  state_->AddSession("alice", "tab-1", "s1", web, "web");
  state_->AddSession("alice", "p1", "s2", phone, "ios");

  state_->RemoveSession(phone);

  EXPECT_EQ(state_->GetUserId(phone), "");
  EXPECT_EQ(state_->GetUserId(web), "alice");
  auto healthy = HealthyLocalSessions(state_, "alice");
  ASSERT_EQ(healthy.size(), 1u);
  EXPECT_EQ(healthy[0], web);

  // Removing an unknown session is a no-op.
  state_->RemoveSession(nullptr);
  EXPECT_EQ(state_->GetUserId(web), "alice");
}

TEST_F(EnhancedSessionTest, HealthyLocalSessionsSkipsHalfClosed) {
  auto live = std::make_shared<MockSession>();
  auto half = std::make_shared<MockSession>();
  half->half_closed = true;
  state_->AddSession("alice", "tab-1", "s1", live, "web");
  state_->AddSession("alice", "p1", "s2", half, "ios");

  auto healthy = HealthyLocalSessions(state_, "alice");
  ASSERT_EQ(healthy.size(), 1u);
  EXPECT_EQ(healthy[0], live);

  EXPECT_TRUE(HealthyLocalSessions(state_, "nobody").empty());
}

// --- HandleLogin: kick contract on top of the registry --------------------

TEST_F(EnhancedSessionTest, LoginSamePlatformKicksPreviousSession) {
  auto first = std::make_shared<MockSession>();
  auto second = std::make_shared<MockSession>();

  Login(first, "alice", "tab-1", 1, "web");
  auto first_logins = FramesOf(*first, chirp::gateway::LOGIN_RESP);
  ASSERT_EQ(first_logins.size(), 1u);
  {
    chirp::auth::LoginResponse resp;
    ASSERT_TRUE(resp.ParseFromString(first_logins[0].body()));
    EXPECT_EQ(resp.code(), chirp::common::OK);
    EXPECT_EQ(resp.user_id(), "alice");
    // 首个登录：没有其他在线端，初始清单为空。
    EXPECT_EQ(resp.online_devices_size(), 0);
  }

  Login(second, "alice", "tab-2", 2, "web");

  // The new session logs in with the kick flags set...
  auto second_logins = FramesOf(*second, chirp::gateway::LOGIN_RESP);
  ASSERT_EQ(second_logins.size(), 1u);
  chirp::auth::LoginResponse resp;
  ASSERT_TRUE(resp.ParseFromString(second_logins[0].body()));
  EXPECT_EQ(resp.code(), chirp::common::OK);
  EXPECT_TRUE(resp.kick_previous());
  EXPECT_EQ(resp.kick().reason(), "logged in on another web");

  // ...and the displaced connection is told instead of silently orphaned.
  auto kicks = FramesOf(*first, chirp::gateway::KICK_NOTIFY);
  ASSERT_EQ(kicks.size(), 1u);
  chirp::auth::KickNotify kick;
  ASSERT_TRUE(kick.ParseFromString(kicks[0].body()));
  EXPECT_EQ(kick.reason(), "logged in on another web");
  EXPECT_TRUE(first->close_after_send);

  // The platform slot now belongs to the new session only.
  auto healthy = HealthyLocalSessions(state_, "alice");
  ASSERT_EQ(healthy.size(), 1u);
  EXPECT_EQ(healthy[0], second);
}

TEST_F(EnhancedSessionTest, LoginOtherPlatformKeepsBothSessionsAndAnnounces) {
  auto web = std::make_shared<MockSession>();
  auto phone = std::make_shared<MockSession>();

  Login(web, "alice", "tab-1", 1, "web");
  Login(phone, "alice", "p1", 2, "ios");

  // 跨 platform 共存：谁也不顶谁。
  EXPECT_EQ(FramesOf(*web, chirp::gateway::KICK_NOTIFY).size(), 0u);
  EXPECT_EQ(FramesOf(*phone, chirp::gateway::KICK_NOTIFY).size(), 0u);

  EXPECT_EQ(state_->GetUserId(web), "alice");
  EXPECT_EQ(state_->GetUserId(phone), "alice");
  EXPECT_EQ(HealthyLocalSessions(state_, "alice").size(), 2u);

  // 先登录的 web 端只收到 ios 上线事件（多端清单变更，风格对齐 KICK_NOTIFY）。
  auto announces = FramesOf(*web, chirp::gateway::DEVICES_PRESENCE_NOTIFY);
  ASSERT_EQ(announces.size(), 1u);
  chirp::auth::DevicesPresenceNotify notify;
  ASSERT_TRUE(notify.ParseFromString(announces[0].body()));
  ASSERT_EQ(notify.devices_size(), 1);
  EXPECT_EQ(notify.devices(0).platform(), "ios");
  EXPECT_EQ(notify.devices(0).device_id(), "p1");
  EXPECT_TRUE(notify.devices(0).online());
  EXPECT_GT(notify.devices(0).ts(), 0);

  // 后登录的 ios 端在登录响应里拿到其他在线端的初始清单（不含自己）。
  auto phone_logins = FramesOf(*phone, chirp::gateway::LOGIN_RESP);
  ASSERT_EQ(phone_logins.size(), 1u);
  chirp::auth::LoginResponse resp;
  ASSERT_TRUE(resp.ParseFromString(phone_logins[0].body()));
  ASSERT_EQ(resp.online_devices_size(), 1);
  EXPECT_EQ(resp.online_devices(0).platform(), "web");
  EXPECT_EQ(resp.online_devices(0).device_id(), "tab-1");
  EXPECT_TRUE(resp.online_devices(0).online());
}

// --- 多端在线：断开下线的清单变更（login 建立的槽位经 RemoveSession 释放）-

TEST_F(EnhancedSessionTest, DisconnectAnnouncesOfflineToRemainingDevices) {
  auto web = std::make_shared<MockSession>();
  auto phone = std::make_shared<MockSession>();
  Login(web, "alice", "tab-1", 1, "web");
  Login(phone, "alice", "p1", 2, "ios");
  // FramesOf 是累计快照：先记下 web 已收到的清单变更数，之后只看增量。
  const size_t announces_before = FramesOf(*web, chirp::gateway::DEVICES_PRESENCE_NOTIFY).size();

  state_->RemoveSession(phone);

  // 剩余端收到 ios 下线；下线的会话自己不需要（也不该）收到。
  auto announces = FramesOf(*web, chirp::gateway::DEVICES_PRESENCE_NOTIFY);
  ASSERT_EQ(announces.size(), announces_before + 1u);
  chirp::auth::DevicesPresenceNotify notify;
  ASSERT_TRUE(notify.ParseFromString(announces.back().body()));
  ASSERT_EQ(notify.devices_size(), 1);
  EXPECT_EQ(notify.devices(0).platform(), "ios");
  EXPECT_EQ(notify.devices(0).device_id(), "p1");
  EXPECT_FALSE(notify.devices(0).online());
  EXPECT_EQ(FramesOf(*phone, chirp::gateway::DEVICES_PRESENCE_NOTIFY).size(), 0u);

  // 被顶的旧会话断开不广播（它没有赢得这个槽位）：web 先下线再重新登录，
  // 旧连接的 stale 断开不应产生 ios→offline 的假事件。web2 登录本身会让
  // phone 收到一条 web 上线事件（合法），所以这里也只看增量。
  auto web2 = std::make_shared<MockSession>();
  Login(web2, "alice", "tab-2", 3, "web");
  const size_t phone_before = FramesOf(*phone, chirp::gateway::DEVICES_PRESENCE_NOTIFY).size();
  state_->RemoveSession(web);                                  // stale disconnect
  EXPECT_EQ(FramesOf(*phone, chirp::gateway::DEVICES_PRESENCE_NOTIFY).size(), phone_before);
}

// --- Local delivery fans out to every healthy device ----------------------

TEST_F(EnhancedSessionTest, PrivateSendFansOutToEveryDevice) {
  auto phone = std::make_shared<MockSession>();
  auto tablet = std::make_shared<MockSession>();
  auto sender = std::make_shared<MockSession>();
  state_->AddSession("bob", "p1", "s1", phone, "ios");
  state_->AddSession("bob", "p2", "s2", tablet, "android");

  chirp::chat::SendMessageRequest req;
  req.set_sender_id("alice");
  req.set_receiver_id("bob");
  req.set_channel_type(chirp::chat::PRIVATE);
  req.set_content("hi bob");
  InvokeSendMessage(req, sender, state_, store_, tracker_, /*acks=*/nullptr, &delivery_prefs_,
                    router_, /*hub_peer=*/nullptr, /*npc_service_id=*/"", /*npc_prefix=*/"npc:",
                    /*spoke_link=*/nullptr, /*spoke_game_id=*/"", recall_.handlers.get(),
                    /*directory=*/nullptr, /*hub=*/nullptr, /*seq=*/3);

  auto resps = FramesOf(*sender, chirp::gateway::SEND_MESSAGE_RESP);
  ASSERT_EQ(resps.size(), 1u);
  chirp::chat::SendMessageResponse resp;
  ASSERT_TRUE(resp.ParseFromString(resps[0].body()));
  EXPECT_EQ(resp.code(), chirp::common::OK);
  EXPECT_FALSE(resp.message_id().empty());

  // One copy per device - not one arbitrary slot.
  for (const auto& device : {phone, tablet}) {
    auto notifies = FramesOf(*device, chirp::gateway::CHAT_MESSAGE_NOTIFY);
    ASSERT_EQ(notifies.size(), 1u);
    chirp::chat::ChatMessage msg;
    ASSERT_TRUE(msg.ParseFromString(notifies[0].body()));
    EXPECT_EQ(msg.content(), "hi bob");
    EXPECT_EQ(msg.sender_id(), "alice");
    EXPECT_EQ(msg.receiver_id(), "bob");
    EXPECT_EQ(msg.channel_id(), "alice|bob");
  }
}

TEST_F(EnhancedSessionTest, PrivateSendQueuesOfflineWhenEveryDeviceHalfClosed) {
  auto bob = std::make_shared<MockSession>();
  auto sender = std::make_shared<MockSession>();
  bob->half_closed = true;
  state_->AddSession("bob", "p1", "s1", bob, "ios");

  chirp::chat::SendMessageRequest req;
  req.set_sender_id("alice");
  req.set_receiver_id("bob");
  req.set_channel_type(chirp::chat::PRIVATE);
  req.set_content("hi bob");
  InvokeSendMessage(req, sender, state_, store_, tracker_, /*acks=*/nullptr, &delivery_prefs_,
                    router_, /*hub_peer=*/nullptr, /*npc_service_id=*/"", /*npc_prefix=*/"npc:",
                    /*spoke_link=*/nullptr, /*spoke_game_id=*/"", recall_.handlers.get(),
                    /*directory=*/nullptr, /*hub=*/nullptr, /*seq=*/4);

  // A half-closed connection counts as offline: nothing is written to it.
  EXPECT_TRUE(bob->sent.empty());

  auto offline = store_->PopOfflineMessages("bob");
  ASSERT_EQ(offline.size(), 1u);
  EXPECT_EQ(offline[0].content, "hi bob");
}

// --- Ack hold: any ack-capable device is enough ---------------------------

TEST_F(EnhancedSessionTest, TrackAckIfCapableHoldsWhenAnyDeviceIsCapable) {
  DeliveryAckManager::Config cfg;
  cfg.timeout_ms = 10000;
  DeliveryAckManager acks(io_, cfg, nullptr, nullptr);

  auto legacy = std::make_shared<MockSession>();
  auto modern = std::make_shared<MockSession>();

  EXPECT_FALSE(TrackAckIfCapable(&acks, {legacy}, "m1", "bob", "payload-1"));
  EXPECT_EQ(acks.pending_count(), 0u);

  acks.MarkCapable(modern);
  EXPECT_TRUE(TrackAckIfCapable(&acks, {legacy, modern}, "m2", "bob", "payload-2"));
  EXPECT_EQ(acks.pending_count(), 1u);
}

// --- 撤回（DELETE_MESSAGE，game_chat_features P0）：enhanced 形态端到端 -----

TEST_F(EnhancedSessionTest, RecallNotifiesEveryDeviceAndErasesHistory) {
  LiveHistoryRecall live;
  StartLiveHistory(live);

  auto alice = std::make_shared<MockSession>();
  auto bob_phone = std::make_shared<MockSession>();
  auto bob_tablet = std::make_shared<MockSession>();
  Login(alice, "alice", "a1", 1);
  // bob 的两条设备会话必须跨 platform 才能共存（多端在线：同型互顶）。
  Login(bob_phone, "bob", "p1", 2, "ios");
  Login(bob_tablet, "bob", "p2", 3, "android");

  std::string mid;
  EXPECT_EQ(SendPrivate(alice, "alice", "bob", "recall me", &mid, live.store,
                        live.recall.handlers.get()),
            chirp::common::OK);
  EXPECT_FALSE(mid.empty());

  const chirp::chat::DeleteMessageResponse resp =
      Recall(alice, mid, "alice", /*hard_delete=*/false, live.recall.handlers.get());
  EXPECT_EQ(resp.code(), chirp::common::OK);
  EXPECT_FALSE(resp.was_permanently_deleted());

  // 撤回广播到达 bob 的每一条设备会话（多端共存语义）。
  for (const auto& device : {bob_phone, bob_tablet}) {
    const auto notifies = FramesOf(*device, chirp::gateway::MESSAGE_DELETED_NOTIFY);
    ASSERT_EQ(notifies.size(), 1u);
    chirp::chat::MessageDeletedNotify notify;
    ASSERT_TRUE(notify.ParseFromString(notifies[0].body()));
    EXPECT_EQ(notify.message_id(), mid);
    EXPECT_EQ(notify.channel_id(), "alice|bob");
    EXPECT_FALSE(notify.is_hard_delete());
    EXPECT_EQ(notify.deleted_by(), "alice");
  }

  // 历史存档只剩墓碑：is_recalled 置位、正文抹除。
  const auto page = live.store->GetHistory("alice|bob", 0, 0, 10);
  ASSERT_EQ(page.size(), 1u);
  EXPECT_EQ(page[0].message_id, mid);
  EXPECT_TRUE(page[0].is_recalled);
  EXPECT_TRUE(page[0].content.empty());
}

TEST_F(EnhancedSessionTest, RecallPurgesOfflineCopyOfAbsentReceiver) {
  LiveHistoryRecall live;
  StartLiveHistory(live);

  auto alice = std::make_shared<MockSession>();
  Login(alice, "alice", "a1", 1);
  // bob 不登录：消息进离线队列，撤回时他收不到 notify，副本必须被回收，
  // 否则下次登录会把原文补投出去。
  std::string mid;
  EXPECT_EQ(SendPrivate(alice, "alice", "bob", "offline secret", &mid, live.store,
                        live.recall.handlers.get()),
            chirp::common::OK);
  ASSERT_EQ(live.store->GetOfflineMessages("bob").size(), 1u);

  const chirp::chat::DeleteMessageResponse resp =
      Recall(alice, mid, "alice", /*hard_delete=*/false, live.recall.handlers.get());
  EXPECT_EQ(resp.code(), chirp::common::OK);
  EXPECT_TRUE(live.store->GetOfflineMessages("bob").empty());

  const auto page = live.store->GetHistory("alice|bob", 0, 0, 10);
  ASSERT_EQ(page.size(), 1u);
  EXPECT_TRUE(page[0].is_recalled);
  EXPECT_TRUE(page[0].content.empty());
}

TEST_F(EnhancedSessionTest, RecallRejectsNonSenderUnknownWorldChannelAndHardDelete) {
  LiveHistoryRecall live;
  StartLiveHistory(live);

  auto alice = std::make_shared<MockSession>();
  auto bob = std::make_shared<MockSession>();
  Login(alice, "alice", "a1", 1);
  Login(bob, "bob", "p1", 2);

  std::string mid;
  ASSERT_EQ(SendPrivate(alice, "alice", "bob", "mine", &mid, live.store,
                        live.recall.handlers.get()),
            chirp::common::OK);

  // 非发送者：回码 AUTH_FAILED（版主请走治理路径，这里没有版主）。
  EXPECT_EQ(Recall(bob, mid, "bob", /*hard_delete=*/false, live.recall.handlers.get())
                .code(),
            chirp::common::AUTH_FAILED);

  // 未知消息（台账里没有）：USER_NOT_FOUND。
  EXPECT_EQ(
      Recall(alice, "no-such-message", "alice", /*hard_delete=*/false,
             live.recall.handlers.get())
          .code(),
      chirp::common::USER_NOT_FOUND);

  // 世界频道不在默认可撤回名单（private,guild）：INVALID_PARAM。
  chirp::chat::SendMessageRequest world_req;
  world_req.set_sender_id("alice");
  world_req.set_channel_type(chirp::chat::WORLD);
  world_req.set_channel_id("world");
  world_req.set_content("world hello");
  InvokeSendMessage(world_req, alice, state_, live.store, tracker_, /*acks=*/nullptr,
                    &delivery_prefs_, router_, /*hub_peer=*/nullptr, /*npc_service_id=*/"",
                    /*npc_prefix=*/"npc:", /*spoke_link=*/nullptr, /*spoke_game_id=*/"",
                    live.recall.handlers.get(), /*directory=*/nullptr, /*hub=*/nullptr,
                    /*seq=*/2);
  std::string world_mid;
  {
    const auto resps = FramesOf(*alice, chirp::gateway::SEND_MESSAGE_RESP);
    ASSERT_GE(resps.size(), 2u);
    chirp::chat::SendMessageResponse resp;
    ASSERT_TRUE(resp.ParseFromString(resps.back().body()));
    ASSERT_EQ(resp.code(), chirp::common::OK);
    world_mid = resp.message_id();
  }
  EXPECT_EQ(
      Recall(alice, world_mid, "alice", /*hard_delete=*/false, live.recall.handlers.get())
          .code(),
      chirp::common::INVALID_PARAM);

  // is_hard_delete 是版主治理路径；enhanced 无角色体系恒非版主 → fail-closed。
  std::string mid2;
  ASSERT_EQ(SendPrivate(alice, "alice", "bob", "hard target", &mid2, live.store,
                        live.recall.handlers.get()),
            chirp::common::OK);
  EXPECT_EQ(
      Recall(alice, mid2, "alice", /*hard_delete=*/true, live.recall.handlers.get()).code(),
      chirp::common::AUTH_FAILED);
  // 被拒的硬删不动历史。
  const auto page = live.store->GetHistory("alice|bob", 0, 0, 10);
  EXPECT_EQ(page.size(), 2u);
}

TEST_F(EnhancedSessionTest, DispatchRoutesDeleteMessageAndRejectsGarbage) {
  chirp::chat::runtime::DistributedDispatchHandlers handlers;
  int calls = 0;
  std::string seen_id;
  handlers.on_delete_message = [&](const std::shared_ptr<chirp::network::Session>&,
                                   const chirp::chat::DeleteMessageRequest& req, int64_t seq) {
    ++calls;
    seen_id = req.message_id();
    EXPECT_EQ(seq, 7);
  };

  auto session = std::make_shared<MockSession>();
  chirp::chat::DeleteMessageRequest req;
  req.set_message_id("m-1");
  req.set_user_id("alice");
  chirp::gateway::Packet pkt;
  pkt.set_msg_id(chirp::gateway::DELETE_MESSAGE_REQ);
  pkt.set_sequence(7);
  pkt.set_body(req.SerializeAsString());
  chirp::chat::runtime::DispatchDistributedPacket(session, pkt, handlers);
  EXPECT_EQ(calls, 1);
  EXPECT_EQ(seen_id, "m-1");

  // 解析失败的 body：不进 handler，但必须回 INVALID_PARAM（撤回请求不许悬着）。
  pkt.set_body(std::string("\xde\xad\xbe\xef", 4));
  chirp::chat::runtime::DispatchDistributedPacket(session, pkt, handlers);
  EXPECT_EQ(calls, 1);
  const auto resps = FramesOf(*session, chirp::gateway::DELETE_MESSAGE_RESP);
  ASSERT_EQ(resps.size(), 1u);
  chirp::chat::DeleteMessageResponse resp;
  ASSERT_TRUE(resp.ParseFromString(resps[0].body()));
  EXPECT_EQ(resp.code(), chirp::common::INVALID_PARAM);

  // 没接 handler（比如旧进程/新客户端）同样不崩，只回 INVALID_PARAM。
  chirp::chat::runtime::DistributedDispatchHandlers bare;
  chirp::chat::runtime::DispatchDistributedPacket(session, pkt, bare);
  EXPECT_EQ(FramesOf(*session, chirp::gateway::DELETE_MESSAGE_RESP).size(), 2u);
}

// --- 发送侧回码边界：空接收方 / 悬空引用（协议面校验臂） -------------------

TEST_F(EnhancedSessionTest, PrivateSendRejectsEmptyReceiverAndDanglingReply) {
  LiveHistoryRecall live;
  StartLiveHistory(live);
  auto alice = std::make_shared<MockSession>();

  chirp::chat::SendMessageRequest req;
  req.set_sender_id("alice");
  req.set_channel_type(chirp::chat::PRIVATE);
  req.set_content("to nobody");
  // 私聊缺接收方：INVALID_PARAM，消息不落库。
  EXPECT_EQ(SendReq(req, alice, live.store, live.recall.handlers.get()),
            chirp::common::INVALID_PARAM);

  // 悬空引用：reply_to 不在同一会话（热层/冷层都查不到）→ INVALID_PARAM。
  req.set_receiver_id("bob");
  req.set_reply_to_message_id("no-such-message");
  EXPECT_EQ(SendReq(req, alice, live.store, live.recall.handlers.get()),
            chirp::common::INVALID_PARAM);
  EXPECT_TRUE(live.store->GetHistory("alice|bob", 0, 0, 10).empty());

  // 引用存在的消息：放行（引用校验的通过臂）。
  std::string first_id;
  ASSERT_EQ(SendPrivate(alice, "alice", "bob", "first", &first_id, live.store,
                        live.recall.handlers.get()),
            chirp::common::OK);
  req.set_content("second");
  req.set_reply_to_message_id(first_id);
  EXPECT_EQ(SendReq(req, alice, live.store, live.recall.handlers.get()), chirp::common::OK);
}

// --- 黑名单：私聊静默成功，两端都不送、不入离线队列 -------------------------

TEST_F(EnhancedSessionTest, PrivateSendSilentlyDropsBlockedReceiver) {
  auto alice = std::make_shared<MockSession>();
  auto bob = std::make_shared<MockSession>();
  Login(bob, "bob", "p1", 1);

  ASSERT_TRUE(delivery_prefs_.BlockUser("bob", "alice"));

  // 拉黑不暴露：发送方照拿 OK，接收端零投递。
  EXPECT_EQ(SendPrivate(alice, "alice", "bob", "shadowed", nullptr), chirp::common::OK);
  EXPECT_EQ(FramesOf(*bob, chirp::gateway::CHAT_MESSAGE_NOTIFY).size(), 0u);
  // 「已投递」的回话阻止离线入队：补投路径也没有副本。
  EXPECT_TRUE(store_->GetOfflineMessages("bob").empty());

  // 排空的 io 走到 MySQL 写回调：注入失败（scripted fake 拒 INSERT）时
  // 异步存储的失败分支只留 Warn，不影响发送结果。
  chirp_test::fake_mysql::FailQueriesMatching("INSERT INTO messages");
  io_.poll();
}

// --- NPC 接收方：私聊改道上行为事件，玩家投递路径整体跳过 -------------------

TEST_F(EnhancedSessionTest, NpcReceiverPublishesUtteranceEvent) {
  chirp::common::Logger::Instance().SetLevel(chirp::common::Logger::Level::kError);
  // 未 Start 的 hub peer：SendEventPublish 在 strand 上快速失败（SERVER_
  // UNAVAILABLE），发布回调走失败告警臂——事件上行的可达性由 npc 链路
  // 冒烟（--smoke-npc）另行证明，这里锁的是改道语义本身。
  auto npc_peer = chirp::network::ServerGatewayPeer::Create(
      io_, chirp::network::ServerGatewayPeer::Options{}, nullptr);

  auto alice = std::make_shared<MockSession>();
  chirp::chat::SendMessageRequest req;
  req.set_sender_id("alice");
  req.set_receiver_id("npc:merchant");
  req.set_channel_type(chirp::chat::PRIVATE);
  req.set_content("hello smith");
  EXPECT_EQ(SendReq(req, alice, nullptr, nullptr, /*hub_peer=*/npc_peer.get(),
                    /*npc_service_id=*/"npc-dialog"),
            chirp::common::OK);

  // 回复 OK = 事件受理，NPC 不是用户：本地无投递、离线队列也不留副本。
  io_.poll();  // strand 上排队的发布回调（fail-fast 臂）
  EXPECT_TRUE(store_->GetOfflineMessages("npc:merchant").empty());
}

// --- 内部面信任门（SERVER_AUTH_REQ）：无密钥忽略 / 坏密钥拒绝并关 --------

TEST_F(EnhancedSessionTest, ServerAuthGateIgnoresWithoutSecretAndRejectsBad) {
  auto session = std::make_shared<MockSession>();
  std::unordered_set<const chirp::network::Session*> trusted;

  chirp::game_server_gateway::ServerAuthRequest auth;
  auth.set_secret("shh");
  chirp::gateway::Packet pkt;
  pkt.set_msg_id(chirp::gateway::SERVER_AUTH_REQ);
  pkt.set_sequence(11);
  pkt.set_body(auth.SerializeAsString());

  // 无 secret 配置：直连模式忽略该帧（历史 enhanced 行为）。
  EXPECT_FALSE(HandleServerAuth(pkt, session, "", &trusted));
  EXPECT_TRUE(session->sent.empty());
  EXPECT_TRUE(trusted.empty());

  // 正确 secret：进信任集 + OK 应答，sequence 原样回带。
  EXPECT_TRUE(HandleServerAuth(pkt, session, "shh", &trusted));
  EXPECT_EQ(trusted.count(session.get()), 1u);
  const auto resps = FramesOf(*session, chirp::gateway::SERVER_AUTH_RESP);
  ASSERT_EQ(resps.size(), 1u);
  chirp::game_server_gateway::ServerAuthResponse resp;
  ASSERT_TRUE(resp.ParseFromString(resps[0].body()));
  EXPECT_EQ(resp.code(), chirp::common::OK);
  EXPECT_GT(resp.server_time_ms(), 0);
  EXPECT_EQ(resps[0].sequence(), 11);
  EXPECT_FALSE(session->close_after_send);

  // 错误 secret：AUTH_FAILED，回完即关（网关管道不许滞留），不进信任集。
  auto stranger = std::make_shared<MockSession>();
  auth.set_secret("nope");
  pkt.set_body(auth.SerializeAsString());
  EXPECT_TRUE(HandleServerAuth(pkt, stranger, "shh", &trusted));
  EXPECT_EQ(trusted.count(stranger.get()), 0u);
  const auto denies = FramesOf(*stranger, chirp::gateway::SERVER_AUTH_RESP);
  ASSERT_EQ(denies.size(), 1u);
  ASSERT_TRUE(resp.ParseFromString(denies[0].body()));
  EXPECT_EQ(resp.code(), chirp::common::AUTH_FAILED);
  EXPECT_TRUE(stranger->close_after_send);

  // 垃圾 body：解析失败同款 AUTH_FAILED；trusted 为空的调用方形态也走过。
  auto garbage = std::make_shared<MockSession>();
  pkt.set_body(std::string("\xde\xad\xbe\xef", 4));
  EXPECT_TRUE(HandleServerAuth(pkt, garbage, "shh", nullptr));
  const auto bad = FramesOf(*garbage, chirp::gateway::SERVER_AUTH_RESP);
  ASSERT_EQ(bad.size(), 1u);
  ASSERT_TRUE(resp.ParseFromString(bad[0].body()));
  EXPECT_EQ(resp.code(), chirp::common::AUTH_FAILED);
}

// --- 登录契约：JWT 验签双向 + scaffold 空 token 拒收 -----------------------

TEST_F(EnhancedSessionTest, LoginVerifiesJwtBothWays) {
  const int64_t now_s = std::chrono::duration_cast<std::chrono::seconds>(
                            std::chrono::system_clock::now().time_since_epoch())
                            .count();
  chirp::common::LoginTokenVerifier verifier("jwt-s3cret");

  // 有效 HS256（sub=alice，exp 在未来）：按 sub 登录。
  auto session = std::make_shared<MockSession>();
  chirp::auth::LoginRequest req;
  req.set_token(chirp::common::JwtSignHS256("alice", now_s - 10, "jwt-s3cret", now_s + 600));
  req.set_device_id("a1");
  req.set_platform("web");
  LoginReq(req, session, nullptr, &verifier);
  auto logins = FramesOf(*session, chirp::gateway::LOGIN_RESP);
  ASSERT_EQ(logins.size(), 1u);
  chirp::auth::LoginResponse ok_resp;
  ASSERT_TRUE(ok_resp.ParseFromString(logins[0].body()));
  EXPECT_EQ(ok_resp.code(), chirp::common::OK);
  EXPECT_EQ(ok_resp.user_id(), "alice");

  // 坏 token：AUTH_FAILED，绝不把 token 本身当身份。
  auto denied = std::make_shared<MockSession>();
  req.set_token("not-a-jwt");
  LoginReq(req, denied, nullptr, &verifier);
  logins = FramesOf(*denied, chirp::gateway::LOGIN_RESP);
  ASSERT_EQ(logins.size(), 1u);
  chirp::auth::LoginResponse deny;
  ASSERT_TRUE(deny.ParseFromString(logins[0].body()));
  EXPECT_EQ(deny.code(), chirp::common::AUTH_FAILED);
  EXPECT_TRUE(deny.user_id().empty());
  EXPECT_EQ(state_->GetUserId(denied), "");

  // scaffold 模式（无 verifier）下空 token 是空身份：INVALID_PARAM。
  auto scaffold = std::make_shared<MockSession>();
  req.set_token("");
  LoginReq(req, scaffold);
  logins = FramesOf(*scaffold, chirp::gateway::LOGIN_RESP);
  ASSERT_EQ(logins.size(), 1u);
  chirp::auth::LoginResponse empty;
  ASSERT_TRUE(empty.ParseFromString(logins[0].body()));
  EXPECT_EQ(empty.code(), chirp::common::INVALID_PARAM);
}

// --- 登录补投：离线消息在 LOGIN_RESP 之后回放，ack-capable 端挂起追踪 ------

TEST_F(EnhancedSessionTest, LoginMarksAckCapableAndRefillsOfflineTracked) {
  DeliveryAckManager::Config cfg;
  cfg.timeout_ms = 10000;
  DeliveryAckManager acks(io_, cfg, nullptr, nullptr);

  auto alice = std::make_shared<MockSession>();
  auto bob = std::make_shared<MockSession>();

  // bob 不在线：私聊进离线队列。
  ASSERT_EQ(SendPrivate(alice, "alice", "bob", "offline refill", nullptr), chirp::common::OK);
  ASSERT_EQ(store_->GetOfflineMessages("bob").size(), 1u);

  // supports_message_ack 的登录：会话标为 ack-capable（后续投递可挂起），
  // 补投副本同样走 Track——未 ack 的补投会回队而不是随连接消失。
  chirp::auth::LoginRequest req;
  req.set_token("bob");
  req.set_device_id("p1");
  req.set_platform("web");
  req.set_supports_message_ack(true);
  LoginReq(req, bob, nullptr, nullptr, &acks);

  const auto notifies = FramesOf(*bob, chirp::gateway::CHAT_MESSAGE_NOTIFY);
  ASSERT_EQ(notifies.size(), 1u);
  chirp::chat::ChatMessage msg;
  ASSERT_TRUE(msg.ParseFromString(notifies[0].body()));
  EXPECT_EQ(msg.content(), "offline refill");
  EXPECT_EQ(acks.pending_count(), 1u);
  EXPECT_TRUE(store_->GetOfflineMessages("bob").empty());
}

// --- 跨实例投递（Redis pub/sub 回调）：坏 body / 拉黑 / 无健康会话 / 扇出 ---

TEST_F(EnhancedSessionTest, CrossInstanceDeliveryFiltersAndFansOut) {
  chirp::common::Logger::Instance().SetLevel(chirp::common::Logger::Level::kError);
  chirp_test::InMemoryRedis redis;
  auto fake = std::make_unique<chirp_test::FakeRedisServer>(
      [&redis](const std::vector<std::string>& args) { return redis.Handle(args); });
  auto router =
      std::make_shared<chirp::network::MessageRouter>(io_, "127.0.0.1", fake->port());
  ASSERT_TRUE(router->Start());

  auto bob = std::make_shared<MockSession>();
  chirp::auth::LoginRequest lreq;
  lreq.set_token("bob");
  lreq.set_device_id("p1");
  lreq.set_platform("web");
  LoginReq(lreq, bob, router);

  const std::string channel = chirp::network::RouterChannels::UserChat("bob");
  auto notify_count = [&] { return FramesOf(*bob, chirp::gateway::CHAT_MESSAGE_NOTIFY).size(); };
  auto make_msg = [](const std::string& sender, const std::string& content) {
    chirp::chat::ChatMessage msg;
    msg.set_message_id("m-" + content);
    msg.set_sender_id(sender);
    msg.set_receiver_id("bob");
    msg.set_channel_type(chirp::chat::PRIVATE);
    msg.set_channel_id("x|bob");
    msg.set_content(content);
    return msg;
  };
  // 投递直到谓词成立：订阅者连接建立前的推送会丢，重试兜住连接竞态。
  // io_ 没有常驻 work，run_for 会立刻返回——poll 排空就绪回调 + 睡眠等
  // 订阅者线程把消息 post 进来。
  auto settle = [&](int ms) {
    for (int i = 0; i < ms / 5; ++i) {
      io_.poll();
      io_.restart();  // 排空后 io 进入 stopped，后续 poll 是 no-op——必须重启
      std::this_thread::sleep_for(std::chrono::milliseconds(5));
    }
  };
  auto deliver_until = [&](const chirp::chat::ChatMessage& msg, size_t want) {
    for (int i = 0; i < 100 && notify_count() < want; ++i) {
      fake->Publish(channel, msg.SerializeAsString());
      settle(50);
    }
    return notify_count() == want;
  };

  // 先证明链路活着：合法消息扇出到本实例的每个健康会话。
  const auto good = make_msg("dave", "live from dave");
  ASSERT_TRUE(deliver_until(good, 1u));

  // 坏 body：解析失败静默丢弃，链路无恙。
  fake->Publish(channel, std::string("\xde\xad\xbe\xef", 4));
  settle(150);
  EXPECT_EQ(notify_count(), 1u);

  // 黑名单：bob 拉黑了发送方，跨实例投递到此为止（不暴露拉黑态）。
  ASSERT_TRUE(delivery_prefs_.BlockUser("bob", "eve"));
  fake->Publish(channel, make_msg("eve", "blocked").SerializeAsString());
  settle(150);
  EXPECT_EQ(notify_count(), 1u);

  // 半关连接不算健康投递目标：无人可送，丢弃。
  bob->half_closed = true;
  fake->Publish(channel, make_msg("cara", "half closed").SerializeAsString());
  settle(150);
  bob->half_closed = false;
  EXPECT_EQ(notify_count(), 1u);

  router->Stop();
}

// --- 历史分页：GetPageBefore 映射回协议（含撤回墓碑与 reply 字段） ---------

TEST_F(EnhancedSessionTest, GetHistoryServesPaginationAndTombstones) {
  LiveHistoryRecall live;
  StartLiveHistory(live);
  auto alice = std::make_shared<MockSession>();
  Login(alice, "alice", "a1", 1);

  std::string m1;
  ASSERT_EQ(SendPrivate(alice, "alice", "bob", "one", &m1, live.store,
                        live.recall.handlers.get()),
            chirp::common::OK);
  // 引用 m1 的消息：历史回读必须带 reply_to_message_id。
  chirp::chat::SendMessageRequest req;
  req.set_sender_id("alice");
  req.set_receiver_id("bob");
  req.set_channel_type(chirp::chat::PRIVATE);
  req.set_content("two");
  req.set_reply_to_message_id(m1);
  std::string m2;
  ASSERT_EQ(SendReq(req, alice, live.store, live.recall.handlers.get(),
                    nullptr, "", nullptr, "", nullptr, nullptr, &m2),
            chirp::common::OK);
  std::string m3;
  ASSERT_EQ(SendPrivate(alice, "alice", "bob", "three", &m3, live.store,
                        live.recall.handlers.get()),
            chirp::common::OK);
  ASSERT_EQ(Recall(alice, m2, "alice", false, live.recall.handlers.get()).code(),
            chirp::common::OK);

  auto retriever = std::make_shared<chirp::chat::PaginatedHistoryRetriever>(live.store);
  chirp::chat::GetHistoryRequest history_req;
  history_req.set_channel_id("alice|bob");
  history_req.set_channel_type(chirp::chat::PRIVATE);
  history_req.set_limit(2);
  HandleGetHistory(history_req, alice, retriever, /*seq=*/5);

  const auto resps = FramesOf(*alice, chirp::gateway::GET_HISTORY_RESP);
  ASSERT_EQ(resps.size(), 1u);
  chirp::chat::GetHistoryResponse resp;
  ASSERT_TRUE(resp.ParseFromString(resps[0].body()));
  EXPECT_EQ(resp.code(), chirp::common::OK);
  EXPECT_TRUE(resp.has_more());
  ASSERT_EQ(resp.messages_size(), 2);

  auto by_id = [&](const std::string& id) -> const chirp::chat::ChatMessage* {
    for (const auto& m : resp.messages()) {
      if (m.message_id() == id) {
        return &m;
      }
    }
    return nullptr;
  };
  // 撤回墓碑照常映射：置位 + 正文抹除。
  const auto* tomb = by_id(m2);
  ASSERT_TRUE(tomb != nullptr);
  EXPECT_TRUE(tomb->is_recalled());
  EXPECT_TRUE(tomb->content().empty());
  EXPECT_EQ(tomb->sender_id(), "alice");
  // 常规条目带全部字段（含引用链）。
  const auto* latest = by_id(m3);
  ASSERT_TRUE(latest != nullptr);
  EXPECT_FALSE(latest->is_recalled());
  EXPECT_EQ(latest->content(), "three");
  const auto* reply = by_id(m1);
  if (reply != nullptr) {  // limit=2 时旧条目可能翻页在外
    EXPECT_EQ(reply->reply_to_message_id(), "");
  }
}

// --- GET_HISTORY_V2：游标面未实现前的显式降级（INVALID_PARAM） --------------

TEST_F(EnhancedSessionTest, GetHistoryV2AlwaysRejects) {
  auto session = std::make_shared<MockSession>();
  HandleGetHistoryV2("{}", session, /*seq=*/3);

  const auto resps = FramesOf(*session, chirp::gateway::GET_HISTORY_V2_RESP);
  ASSERT_EQ(resps.size(), 1u);
  chirp::chat::GetHistoryResponse resp;
  ASSERT_TRUE(resp.ParseFromString(resps[0].body()));
  EXPECT_EQ(resp.code(), chirp::common::INVALID_PARAM);
  EXPECT_FALSE(resp.has_more());
  EXPECT_EQ(resps[0].sequence(), 3);
}

// --- 撤回成员解析的三条空表边界（频道不是台账成员的来源时 notify 静默） -----

TEST_F(EnhancedSessionTest, RecallMemberResolutionEdgesStaySilent) {
  chirp::common::Logger::Instance().SetLevel(chirp::common::Logger::Level::kError);
  // 放开世界频道撤回：非私聊频道没有成员台账，members 返回空表。
  chirp::chat::EditConfig cfg;
  cfg.recall_channel_types = {chirp::chat::PRIVATE, chirp::chat::GUILD, chirp::chat::WORLD};
  EnhancedRecallRuntime rt = MakeEnhancedRecallRuntime(state_, store_, cfg);

  auto alice = std::make_shared<MockSession>();
  auto bob = std::make_shared<MockSession>();
  Login(alice, "alice", "a1", 1);
  Login(bob, "bob", "p1", 2);

  // 世界频道：members 的非私聊分支——撤回成功但无人可 notify。
  chirp::chat::SendMessageRequest world_req;
  world_req.set_sender_id("alice");
  world_req.set_channel_type(chirp::chat::WORLD);
  world_req.set_channel_id("world");
  world_req.set_content("world hello");
  std::string world_mid;
  ASSERT_EQ(SendReq(world_req, alice, nullptr, rt.handlers.get(), nullptr, "", nullptr, "",
                    nullptr, nullptr, &world_mid),
            chirp::common::OK);
  EXPECT_EQ(Recall(alice, world_mid, "alice", false, rt.handlers.get()).code(),
            chirp::common::OK);

  // 自发给自己（"alice|alice"）：对端就是排除者本人，成员表为空。
  std::string self_mid;
  ASSERT_EQ(SendPrivate(alice, "alice", "alice", "note to self", &self_mid, nullptr,
                        rt.handlers.get()),
            chirp::common::OK);
  EXPECT_EQ(Recall(alice, self_mid, "alice", false, rt.handlers.get()).code(),
            chirp::common::OK);

  // 三次撤回都成功，但没有一条 MESSAGE_DELETED_NOTIFY 发给在线的 bob。
  EXPECT_EQ(FramesOf(*bob, chirp::gateway::MESSAGE_DELETED_NOTIFY).size(), 0u);
}

// --- 游戏平面：好友私聊经绑定注入 spoke + 群频道 spoke 上行 -----------------

TEST_F(EnhancedSessionTest, FriendRelayIntoGameAndSpokeUplink) {
  chirp::common::Logger::Instance().SetLevel(chirp::common::Logger::Level::kError);
  // hub+link 独占一个本地 io（与 fixture 的 io 隔离，互不排空对方的工作）。
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
  // registered 的线程契约在 link 的 io 上——经 post 读，避免竞态直读。
  auto registered_on_io = [&] {
    auto task = std::make_shared<std::packaged_task<bool()>>([&] { return link->registered(); });
    asio::post(io, [task] { (*task)(); });
    return task->get_future().get();
  };
  ASSERT_TRUE(WaitFor(registered_on_io));

  // bob 绑定 game42 的游戏身份：好友私聊的镜像注入按绑定走。
  chirp::chat::PlayerDirectory directory(chirp::chat::PlayerDirectory::Options{});
  chirp::game_server_gateway::BindPlayerIdentityRequest bind;
  bind.set_binding_id("b1");
  bind.set_player_id("bob");
  bind.set_game_id("game42");
  bind.set_game_user_id("u-1");
  ASSERT_EQ(directory.HandleBindPlayerIdentity(bind).code(), chirp::common::OK);

  // 私聊 alice→bob + directory/hub：本地照常投递，游戏镜像注入 spoke。
  auto alice = std::make_shared<MockSession>();
  Login(alice, "alice", "a1", 1);
  chirp::chat::SendMessageRequest dm;
  dm.set_sender_id("alice");
  dm.set_receiver_id("bob");
  dm.set_channel_type(chirp::chat::PRIVATE);
  dm.set_content("hi in game too");
  ASSERT_EQ(SendReq(dm, alice, nullptr, nullptr, nullptr, "", nullptr, "", &directory,
                    hub.get()),
            chirp::common::OK);
  ASSERT_TRUE(WaitFor([&] {
    std::lock_guard<std::mutex> lock(inject_mu);
    return injects.size() == 1u;
  }));
  {
    std::lock_guard<std::mutex> lock(inject_mu);
    EXPECT_EQ(injects[0].channel_id(), "u-1");  // 目标是绑定的游戏身份
    EXPECT_EQ(injects[0].sender_id(), "alice");
    EXPECT_EQ(injects[0].content(), "hi in game too");
  }

  // 群频道 + 已注册 spoke：上行 CHANNEL_MESSAGE_NOTIFY 到 hub（spoke 模式）。
  chirp::chat::SendMessageRequest guild;
  guild.set_sender_id("alice");
  guild.set_channel_type(chirp::chat::GUILD);
  guild.set_channel_id("guild_1");
  guild.set_content("guild hello");
  ASSERT_EQ(SendReq(guild, alice, nullptr, nullptr, nullptr, "", link.get(), "game42"),
            chirp::common::OK);
  ASSERT_TRUE(WaitFor([&] {
    std::lock_guard<std::mutex> lock(plane_mu);
    return uplinks.size() == 1u;
  }));
  {
    std::lock_guard<std::mutex> lock(plane_mu);
    EXPECT_EQ(uplinks[0].game_id(), "game42");
    EXPECT_EQ(uplinks[0].channel_id(), "guild_1");
    ASSERT_TRUE(uplinks[0].has_message());
    EXPECT_EQ(uplinks[0].message().content(), "guild hello");
  }

  link->Stop();
  hub->Stop();
  watchdog->cancel();
  io.stop();
  runner.join();
}

// --- 覆盖率批次 23：main_enhanced 的 MySQL 初始化门（进程级 rc==1）--------------
//
// HybridMessageStore::Initialize 先过 MySQL（失败即整体 false，redis 只是
// 告警），main_enhanced 拿到 false 直接 return 1。本 TU 链接的是 fake MySQL
// 实现（chirp_fake_deps_include），所以无需真库也无需拨号：把 fake 的
// connect 置为必败即可确定性走到 rc==1。argv 里的 redis/mysql 仍指向回环
// 死端口，纯为装配路径卫生。
TEST(EnhancedMainTest, MysqlInitializeFailureExitsOne) {
  chirp::common::Logger::Instance().SetLevel(chirp::common::Logger::Level::kError);
  chirp_test::fake_mysql::SetConnectShouldFail(true);
  struct FakeReset {
    ~FakeReset() { chirp_test::fake_mysql::Reset(); }
  } fake_reset;

  const uint16_t port = [] {
    asio::io_context io;
    asio::ip::tcp::acceptor a(io, asio::ip::tcp::endpoint(asio::ip::tcp::v4(), 0));
    return static_cast<uint16_t>(a.local_endpoint().port());
  }();
  const std::vector<std::string> args = {
      "chat", "--port", std::to_string(port),
      "--ws_port", std::to_string(static_cast<uint16_t>(port + 1)),
      "--redis_host", "127.0.0.1", "--redis_port", "1",
      "--mysql_host", "127.0.0.1", "--mysql_port", "1"};
  std::vector<std::unique_ptr<char[]>> holds;
  std::vector<char*> argv;
  for (const auto& a : args) {
    auto buf = std::make_unique<char[]>(a.size() + 1);
    std::memcpy(buf.get(), a.c_str(), a.size() + 1);
    argv.push_back(buf.get());
    holds.push_back(std::move(buf));
  }

  std::atomic<int> rc{12345};
  std::thread runner([&] {
    rc = chirp_chat_enhanced_main(static_cast<int>(argv.size()), argv.data());
  });
  bool exited = false;
  for (int i = 0; i < 2500 && !exited; ++i) {
    exited = rc.load() != 12345;
    if (!exited) {
      std::this_thread::sleep_for(std::chrono::milliseconds(2));
    }
  }
  ASSERT_TRUE(exited) << "main_enhanced never returned from the store gate";
  EXPECT_EQ(rc.load(), 1);
  runner.join();
}

}  // namespace
