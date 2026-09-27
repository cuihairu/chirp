// Enhanced chat build (app_chat) session semantics: DistributedChatState
// must share the basic build's (user, device) mutual-kick kernel from
// libs/network/session_registry (TODO: enhanced 会话语义修复) and fan local
// delivery out to every healthy device instead of one user-dimension slot.

#include <gtest/gtest.h>

#include <memory>
#include <string>
#include <type_traits>
#include <utility>
#include <vector>

#include <asio.hpp>

#include "delivery_ack_manager.h"
#include "network/message_router.h"
#include "network/session.h"
#include "proto/auth.pb.h"
#include "proto/chat.pb.h"
#include "proto/common.pb.h"
#include "proto/gateway.pb.h"

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
    chirp::chat::MessageEditHandlers*, int64_t);

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
                       chirp::chat::MessageEditHandlers* edits, int64_t seq) {
  if constexpr (CanCastHandleSendMessage<HandleSendMessageWithPrefsFn>::value) {
    HandleSendMessage(req, session, state, store, tracker, acks, prefs, router, hub_peer,
                      npc_service_id, npc_prefix, spoke_link, spoke_game_id, edits, seq);
  } else {
    HandleSendMessage(req, session, state, store, tracker, acks, router, hub_peer,
                      npc_service_id, npc_prefix, spoke_link, spoke_game_id,
                      seq + (prefs == nullptr ? int64_t{0} : int64_t{0}));
  }
}

class EnhancedSessionTest : public ::testing::Test {
 protected:
  void SetUp() override {
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
                      edits_override ? edits_override : recall_.handlers.get(), /*seq=*/1);
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
             const std::string& device_id, int64_t seq) {
    chirp::auth::LoginRequest req;
    req.set_token(token);
    req.set_device_id(device_id);
    InvokeLogin(req, session, state_, store_, router_, nullptr, nullptr, &delivery_prefs_, seq);
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

TEST_F(EnhancedSessionTest, AddSessionSameDeviceReturnsPreviousSession) {
  auto first = std::make_shared<MockSession>();
  auto second = std::make_shared<MockSession>();

  EXPECT_EQ(state_->AddSession("alice", "phone", "s1", first), nullptr);
  auto kicked = state_->AddSession("alice", "phone", "s2", second);

  // The same (user, device) pair displaces the previous session so the
  // caller can kick it - never a silent overwrite that orphans the old
  // connection.
  ASSERT_TRUE(kicked);
  EXPECT_EQ(kicked, first);
  EXPECT_EQ(state_->GetUserId(second), "alice");
  // The displaced session keeps its identity until its (late) disconnect
  // arrives; the registry only moves the device slot.
  EXPECT_EQ(state_->GetUserId(first), "alice");
}

TEST_F(EnhancedSessionTest, AddSessionOtherDeviceCoexists) {
  auto phone = std::make_shared<MockSession>();
  auto tablet = std::make_shared<MockSession>();

  EXPECT_EQ(state_->AddSession("alice", "phone", "s1", phone), nullptr);
  EXPECT_EQ(state_->AddSession("alice", "tablet", "s2", tablet), nullptr);

  EXPECT_EQ(state_->GetUserId(phone), "alice");
  EXPECT_EQ(state_->GetUserId(tablet), "alice");
  EXPECT_EQ(HealthyLocalSessions(state_, "alice").size(), 2u);
}

TEST_F(EnhancedSessionTest, AddSessionEmptyDeviceSharesDefaultSlot) {
  auto legacy_a = std::make_shared<MockSession>();
  auto legacy_b = std::make_shared<MockSession>();

  EXPECT_EQ(state_->AddSession("alice", "", "s1", legacy_a), nullptr);
  // Legacy clients that never send a device id all map onto the "default"
  // slot and keep the historical one-session-per-user kick behavior.
  auto kicked = state_->AddSession("alice", "", "s2", legacy_b);
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
  EXPECT_EQ(state_->AddSession("carol", "phone", "s1", old_session), nullptr);
  ASSERT_TRUE(state_->AddSession("carol", "phone", "s2", new_session));
  state_->RemoveSession(old_session);

  EXPECT_EQ(state_->GetUserId(old_session), "");
  EXPECT_EQ(state_->GetUserId(new_session), "carol");
  auto healthy = HealthyLocalSessions(state_, "carol");
  ASSERT_EQ(healthy.size(), 1u);
  EXPECT_EQ(healthy[0], new_session);
}

TEST_F(EnhancedSessionTest, RemoveSessionOnlyClearsOwnDeviceSlot) {
  auto phone = std::make_shared<MockSession>();
  auto tablet = std::make_shared<MockSession>();
  state_->AddSession("alice", "phone", "s1", phone);
  state_->AddSession("alice", "tablet", "s2", tablet);

  state_->RemoveSession(phone);

  EXPECT_EQ(state_->GetUserId(phone), "");
  EXPECT_EQ(state_->GetUserId(tablet), "alice");
  auto healthy = HealthyLocalSessions(state_, "alice");
  ASSERT_EQ(healthy.size(), 1u);
  EXPECT_EQ(healthy[0], tablet);

  // Removing an unknown session is a no-op.
  state_->RemoveSession(nullptr);
  EXPECT_EQ(state_->GetUserId(tablet), "alice");
}

TEST_F(EnhancedSessionTest, HealthyLocalSessionsSkipsHalfClosed) {
  auto live = std::make_shared<MockSession>();
  auto half = std::make_shared<MockSession>();
  half->half_closed = true;
  state_->AddSession("alice", "phone", "s1", live);
  state_->AddSession("alice", "tablet", "s2", half);

  auto healthy = HealthyLocalSessions(state_, "alice");
  ASSERT_EQ(healthy.size(), 1u);
  EXPECT_EQ(healthy[0], live);

  EXPECT_TRUE(HealthyLocalSessions(state_, "nobody").empty());
}

// --- HandleLogin: kick contract on top of the registry --------------------

TEST_F(EnhancedSessionTest, LoginSameDeviceKicksPreviousSession) {
  auto first = std::make_shared<MockSession>();
  auto second = std::make_shared<MockSession>();

  Login(first, "alice", "phone", 1);
  auto first_logins = FramesOf(*first, chirp::gateway::LOGIN_RESP);
  ASSERT_EQ(first_logins.size(), 1u);
  {
    chirp::auth::LoginResponse resp;
    ASSERT_TRUE(resp.ParseFromString(first_logins[0].body()));
    EXPECT_EQ(resp.code(), chirp::common::OK);
    EXPECT_EQ(resp.user_id(), "alice");
  }

  Login(second, "alice", "phone", 2);

  // The new session logs in with the kick flags set...
  auto second_logins = FramesOf(*second, chirp::gateway::LOGIN_RESP);
  ASSERT_EQ(second_logins.size(), 1u);
  chirp::auth::LoginResponse resp;
  ASSERT_TRUE(resp.ParseFromString(second_logins[0].body()));
  EXPECT_EQ(resp.code(), chirp::common::OK);
  EXPECT_TRUE(resp.kick_previous());
  EXPECT_EQ(resp.kick().reason(), "login from another device");

  // ...and the displaced connection is told instead of silently orphaned.
  auto kicks = FramesOf(*first, chirp::gateway::KICK_NOTIFY);
  ASSERT_EQ(kicks.size(), 1u);
  chirp::auth::KickNotify kick;
  ASSERT_TRUE(kick.ParseFromString(kicks[0].body()));
  EXPECT_EQ(kick.reason(), "login from another device");
  EXPECT_TRUE(first->close_after_send);

  // The device slot now belongs to the new session only.
  auto healthy = HealthyLocalSessions(state_, "alice");
  ASSERT_EQ(healthy.size(), 1u);
  EXPECT_EQ(healthy[0], second);
}

TEST_F(EnhancedSessionTest, LoginOtherDeviceKeepsBothSessions) {
  auto phone = std::make_shared<MockSession>();
  auto tablet = std::make_shared<MockSession>();

  Login(phone, "alice", "phone", 1);
  Login(tablet, "alice", "tablet", 2);

  // A second device never kicks the first one.
  EXPECT_EQ(phone->sent.size(), 1u);  // only its LOGIN_RESP
  EXPECT_EQ(FramesOf(*phone, chirp::gateway::KICK_NOTIFY).size(), 0u);
  EXPECT_EQ(FramesOf(*tablet, chirp::gateway::KICK_NOTIFY).size(), 0u);

  EXPECT_EQ(state_->GetUserId(phone), "alice");
  EXPECT_EQ(state_->GetUserId(tablet), "alice");
  EXPECT_EQ(HealthyLocalSessions(state_, "alice").size(), 2u);
}

// --- Local delivery fans out to every healthy device ----------------------

TEST_F(EnhancedSessionTest, PrivateSendFansOutToEveryDevice) {
  auto phone = std::make_shared<MockSession>();
  auto tablet = std::make_shared<MockSession>();
  auto sender = std::make_shared<MockSession>();
  state_->AddSession("bob", "phone", "s1", phone);
  state_->AddSession("bob", "tablet", "s2", tablet);

  chirp::chat::SendMessageRequest req;
  req.set_sender_id("alice");
  req.set_receiver_id("bob");
  req.set_channel_type(chirp::chat::PRIVATE);
  req.set_content("hi bob");
  InvokeSendMessage(req, sender, state_, store_, tracker_, /*acks=*/nullptr, &delivery_prefs_,
                    router_, /*hub_peer=*/nullptr, /*npc_service_id=*/"", /*npc_prefix=*/"npc:",
                    /*spoke_link=*/nullptr, /*spoke_game_id=*/"", recall_.handlers.get(),
                    /*seq=*/3);

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
  state_->AddSession("bob", "phone", "s1", bob);

  chirp::chat::SendMessageRequest req;
  req.set_sender_id("alice");
  req.set_receiver_id("bob");
  req.set_channel_type(chirp::chat::PRIVATE);
  req.set_content("hi bob");
  InvokeSendMessage(req, sender, state_, store_, tracker_, /*acks=*/nullptr, &delivery_prefs_,
                    router_, /*hub_peer=*/nullptr, /*npc_service_id=*/"", /*npc_prefix=*/"npc:",
                    /*spoke_link=*/nullptr, /*spoke_game_id=*/"", recall_.handlers.get(),
                    /*seq=*/4);

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
  Login(bob_phone, "bob", "p1", 2);
  Login(bob_tablet, "bob", "p2", 3);

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
                    live.recall.handlers.get(), /*seq=*/2);
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

}  // namespace
