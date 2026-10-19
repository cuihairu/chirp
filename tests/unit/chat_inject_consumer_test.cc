// Unit tests for the chat-side injection consumer: hub-forwarded
// INJECT_MESSAGE_NOTIFY payloads must pass validation and then flow through
// the same store/deliver/queue tail as SEND_MESSAGE.
#include <gtest/gtest.h>

#include <algorithm>
#include <cstdint>
#include <memory>
#include <string>
#include <vector>

#include "inject_consumer.h"

namespace {

using chirp::chat::ChatMessage;
using chirp::chat::InjectConsumer;
using chirp::chat::InjectHooks;
using chirp::chat::InjectOutcome;
using chirp::chat::TtlInjectIdempotency;
using chirp::common::ErrorCode;
using chirp::game_server_gateway::InjectMessageNotify;

// Records every hook invocation so tests can assert the delivery tail.
struct RecordingHooks {
  std::vector<ChatMessage> stored;
  std::vector<std::string> private_channel_calls;
  std::vector<std::string> delivered;
  std::vector<std::string> queued;
  std::vector<std::string> broadcast_calls;
  std::vector<std::string> broadcast_offline;
  bool receiver_online = true;

  InjectHooks MakeHooks() {
    InjectHooks hooks;
    hooks.private_channel_id =
        [this](const std::string& a, const std::string& b) {
          private_channel_calls.push_back(a + "|" + b);
          // Canonical pair: sorted, matching MessageStore::PrivateChannelId.
          return std::min(a, b) + "|" + std::max(a, b);
        };
    hooks.store_message = [this](const ChatMessage& msg) { stored.push_back(msg); };
    hooks.deliver_private =
        [this](const std::string& receiver_id, const ChatMessage&) {
          if (!receiver_online) {
            return false;
          }
          delivered.push_back(receiver_id);
          return true;
        };
    hooks.queue_offline =
        [this](const std::string& user_id, const ChatMessage&) { queued.push_back(user_id); };
    hooks.broadcast_channel =
        [this](const std::string& channel_id, const ChatMessage&) {
          broadcast_calls.push_back(channel_id);
          return broadcast_offline;
        };
    return hooks;
  }
};

InjectMessageNotify MakeNotify(const std::string& sender_id, const std::string& content) {
  InjectMessageNotify notify;
  auto* req = notify.mutable_message();
  req->set_inject_id("inj-1");
  req->set_sender_kind(chirp::game_server_gateway::SENDER_NPC);
  req->set_sender_id(sender_id);
  req->set_channel_type(static_cast<int32_t>(chirp::chat::PRIVATE));
  req->set_receiver_id("player_1");
  req->set_content(content);
  return notify;
}

// Idempotency guard wired into the consumer; the clock is test-owned so the
// TTL can be fast-forwarded without sleeping.
std::shared_ptr<TtlInjectIdempotency> Guard(int64_t ttl_ms, int64_t* clock_ms) {
  auto guard = std::make_shared<TtlInjectIdempotency>(ttl_ms);
  guard->SetNow([clock_ms] { return *clock_ms; });
  return guard;
}

TEST(InjectConsumerTest, DeliversPrivateToOnlineReceiver) {
  RecordingHooks rec;
  InjectConsumer consumer(rec.MakeHooks());

  const InjectOutcome out = consumer.HandleInject(MakeNotify("npc:blacksmith_01", "hello"));

  EXPECT_EQ(out.code, ErrorCode::OK);
  EXPECT_FALSE(out.message_id.empty());
  ASSERT_EQ(rec.stored.size(), 1u);
  const ChatMessage& msg = rec.stored[0];
  EXPECT_EQ(msg.message_id(), out.message_id);
  EXPECT_EQ(msg.sender_id(), "npc:blacksmith_01");
  EXPECT_EQ(msg.receiver_id(), "player_1");
  EXPECT_EQ(msg.channel_type(), chirp::chat::PRIVATE);
  EXPECT_EQ(msg.channel_id(), "npc:blacksmith_01|player_1");
  EXPECT_EQ(msg.content(), "hello");
  EXPECT_GT(msg.timestamp(), 0);
  EXPECT_EQ(msg.msg_type(), chirp::chat::TEXT);
  // ActorKind 收敛：网关 NPC 枚举按值域契约直传到落库消息，不落成玩家。
  EXPECT_EQ(msg.sender_kind(), chirp::chat::SENDER_NPC);
  ASSERT_EQ(rec.delivered.size(), 1u);
  EXPECT_EQ(rec.delivered[0], "player_1");
  EXPECT_TRUE(rec.queued.empty());
  EXPECT_TRUE(rec.broadcast_calls.empty());
}

TEST(InjectConsumerTest, QueuesPrivateWhenReceiverOffline) {
  RecordingHooks rec;
  rec.receiver_online = false;
  InjectConsumer consumer(rec.MakeHooks());

  const InjectOutcome out = consumer.HandleInject(MakeNotify("system", "maintenance"));

  EXPECT_EQ(out.code, ErrorCode::OK);
  ASSERT_EQ(rec.stored.size(), 1u);
  EXPECT_TRUE(rec.delivered.empty());
  ASSERT_EQ(rec.queued.size(), 1u);
  EXPECT_EQ(rec.queued[0], "player_1");
}

TEST(InjectConsumerTest, BroadcastsChannelAndQueuesOfflineMembers) {
  RecordingHooks rec;
  rec.broadcast_offline = {"member_2", "member_3"};
  InjectConsumer consumer(rec.MakeHooks());

  InjectMessageNotify notify = MakeNotify("trade", "auction open");
  auto* req = notify.mutable_message();
  req->set_sender_kind(chirp::game_server_gateway::SENDER_SERVICE);
  req->set_channel_type(static_cast<int32_t>(chirp::chat::GUILD));
  req->clear_receiver_id();
  req->set_channel_id("guild_7");

  const InjectOutcome out = consumer.HandleInject(notify);

  EXPECT_EQ(out.code, ErrorCode::OK);
  ASSERT_EQ(rec.stored.size(), 1u);
  EXPECT_EQ(rec.stored[0].channel_id(), "guild_7");
  EXPECT_EQ(rec.stored[0].channel_type(), chirp::chat::GUILD);
  EXPECT_EQ(rec.stored[0].sender_kind(), chirp::chat::SENDER_SERVICE);
  ASSERT_EQ(rec.broadcast_calls.size(), 1u);
  EXPECT_EQ(rec.broadcast_calls[0], "guild_7");
  EXPECT_TRUE(rec.delivered.empty());
  ASSERT_EQ(rec.queued.size(), 2u);
  EXPECT_EQ(rec.queued[0], "member_2");
  EXPECT_EQ(rec.queued[1], "member_3");
}

TEST(InjectConsumerTest, PropagatesSystemSenderKind) {
  RecordingHooks rec;
  InjectConsumer consumer(rec.MakeHooks());

  InjectMessageNotify notify = MakeNotify("system", "patch deployed");
  notify.mutable_message()->set_sender_kind(chirp::game_server_gateway::SENDER_SYSTEM);

  const InjectOutcome out = consumer.HandleInject(notify);

  EXPECT_EQ(out.code, ErrorCode::OK);
  ASSERT_EQ(rec.stored.size(), 1u);
  // 值域契约（两枚举同值）：SYSTEM=1 直传，存储/下发/历史全程不降级成玩家。
  EXPECT_EQ(rec.stored[0].sender_kind(), chirp::chat::SENDER_SYSTEM);
}

TEST(InjectConsumerTest, PropagatesPriorityAndTtlForSystemSurfaces) {
  RecordingHooks rec;
  InjectConsumer consumer(rec.MakeHooks());

  // 系统播报臂：priority=URGENT + ttl>0 是走马灯判定二要素（systemSurfaceOf，
  // 与频道无关）；注入走 WORLD 频道（inject 合法域 PRIVATE..WORLD），展示面
  // 元数据必须原样落库，缺一个走马灯就不亮。
  InjectMessageNotify notify = MakeNotify("system", "boss spawned");
  auto* req = notify.mutable_message();
  req->set_channel_type(static_cast<int32_t>(chirp::chat::WORLD));
  req->clear_receiver_id();
  req->set_channel_id("world");
  req->set_priority(chirp::chat::PRIORITY_URGENT);
  req->set_ttl_seconds(60);

  const InjectOutcome out = consumer.HandleInject(notify);

  EXPECT_EQ(out.code, ErrorCode::OK);
  ASSERT_EQ(rec.stored.size(), 1u);
  EXPECT_EQ(rec.stored[0].priority(), chirp::chat::PRIORITY_URGENT);
  EXPECT_EQ(rec.stored[0].ttl_seconds(), 60);

  // 缺省臂：不带 priority/ttl 的注入保持零值（PRIORITY_LOW=0），与既有
  // TEXT 注入形态完全一致——旧游戏侧零感知。
  const InjectOutcome plain = consumer.HandleInject(MakeNotify("npc:blacksmith_01", "hello"));
  EXPECT_EQ(plain.code, ErrorCode::OK);
  ASSERT_EQ(rec.stored.size(), 2u);
  EXPECT_EQ(rec.stored[1].priority(), chirp::chat::PRIORITY_LOW);
  EXPECT_EQ(rec.stored[1].ttl_seconds(), 0);
}

TEST(InjectConsumerTest, BroadcastWithoutMembersStoresOnly) {
  RecordingHooks rec;
  InjectConsumer consumer(rec.MakeHooks());

  InjectMessageNotify notify = MakeNotify("system", "announcement");
  auto* req = notify.mutable_message();
  req->set_channel_type(static_cast<int32_t>(chirp::chat::WORLD));
  req->clear_receiver_id();
  req->set_channel_id("world");

  const InjectOutcome out = consumer.HandleInject(notify);

  EXPECT_EQ(out.code, ErrorCode::OK);
  ASSERT_EQ(rec.stored.size(), 1u);
  EXPECT_TRUE(rec.queued.empty());
}

TEST(InjectConsumerTest, RejectsEmptyContent) {
  RecordingHooks rec;
  InjectConsumer consumer(rec.MakeHooks());

  const InjectOutcome out = consumer.HandleInject(MakeNotify("npc", ""));

  EXPECT_EQ(out.code, ErrorCode::INVALID_PARAM);
  EXPECT_TRUE(out.message_id.empty());
  EXPECT_TRUE(rec.stored.empty());
  EXPECT_TRUE(rec.delivered.empty());
  EXPECT_TRUE(rec.queued.empty());
}

TEST(InjectConsumerTest, RejectsUnknownSenderKind) {
  RecordingHooks rec;
  InjectConsumer consumer(rec.MakeHooks());

  InjectMessageNotify notify = MakeNotify("npc", "hello");
  notify.mutable_message()->set_sender_kind(chirp::game_server_gateway::SENDER_UNKNOWN);

  const InjectOutcome out = consumer.HandleInject(notify);

  EXPECT_EQ(out.code, ErrorCode::INVALID_PARAM);
  EXPECT_TRUE(rec.stored.empty());
}

TEST(InjectConsumerTest, RejectsEmptySenderId) {
  RecordingHooks rec;
  InjectConsumer consumer(rec.MakeHooks());

  const InjectOutcome out = consumer.HandleInject(MakeNotify("", "hello"));

  EXPECT_EQ(out.code, ErrorCode::INVALID_PARAM);
  EXPECT_TRUE(rec.stored.empty());
}

TEST(InjectConsumerTest, RejectsInvalidChannelType) {
  RecordingHooks rec;
  InjectConsumer consumer(rec.MakeHooks());

  InjectMessageNotify notify = MakeNotify("system", "hello");
  notify.mutable_message()->set_channel_type(42);

  const InjectOutcome out = consumer.HandleInject(notify);

  EXPECT_EQ(out.code, ErrorCode::INVALID_PARAM);
  EXPECT_TRUE(rec.stored.empty());
}

TEST(InjectConsumerTest, RejectsNegativeChannelType) {
  RecordingHooks rec;
  InjectConsumer consumer(rec.MakeHooks());

  InjectMessageNotify notify = MakeNotify("system", "hello");
  notify.mutable_message()->set_channel_type(-1);

  const InjectOutcome out = consumer.HandleInject(notify);

  EXPECT_EQ(out.code, ErrorCode::INVALID_PARAM);
  EXPECT_TRUE(rec.stored.empty());
}

TEST(InjectConsumerTest, RejectsPrivateWithoutReceiver) {
  RecordingHooks rec;
  InjectConsumer consumer(rec.MakeHooks());

  InjectMessageNotify notify = MakeNotify("system", "hello");
  notify.mutable_message()->clear_receiver_id();

  const InjectOutcome out = consumer.HandleInject(notify);

  EXPECT_EQ(out.code, ErrorCode::INVALID_PARAM);
  EXPECT_TRUE(rec.stored.empty());
}

TEST(InjectConsumerTest, RejectsChannelWithoutChannelId) {
  RecordingHooks rec;
  InjectConsumer consumer(rec.MakeHooks());

  InjectMessageNotify notify = MakeNotify("system", "hello");
  notify.mutable_message()->set_channel_type(static_cast<int32_t>(chirp::chat::TEAM));
  notify.mutable_message()->clear_receiver_id();

  const InjectOutcome out = consumer.HandleInject(notify);

  EXPECT_EQ(out.code, ErrorCode::INVALID_PARAM);
  EXPECT_TRUE(rec.stored.empty());
}

TEST(InjectConsumerTest, RejectsEmptyNotify) {
  RecordingHooks rec;
  InjectConsumer consumer(rec.MakeHooks());

  const InjectOutcome out = consumer.HandleInject(InjectMessageNotify{});

  EXPECT_EQ(out.code, ErrorCode::INVALID_PARAM);
  EXPECT_TRUE(rec.stored.empty());
}

// InjectConsumerIdempotencyTest: hub 在收到 ack 前可能重投同一个 notify，
// inject_id 是契约上的幂等键——重投必须回吐首次落库的 message_id，并且不再
// 落库/投递/离线排队。
TEST(InjectConsumerIdempotencyTest, ReplayReturnsFirstMessageIdWithoutStoringAgain) {
  RecordingHooks rec;
  int64_t now = 1000;
  InjectConsumer consumer(rec.MakeHooks(), Guard(60 * 1000, &now));

  const InjectOutcome first = consumer.HandleInject(MakeNotify("npc:a", "hello"));
  const InjectOutcome replay = consumer.HandleInject(MakeNotify("npc:a", "hello"));

  EXPECT_EQ(first.code, ErrorCode::OK);
  EXPECT_EQ(replay.code, ErrorCode::OK);
  EXPECT_FALSE(first.message_id.empty());
  EXPECT_EQ(replay.message_id, first.message_id);
  ASSERT_EQ(rec.stored.size(), 1u);
  ASSERT_EQ(rec.delivered.size(), 1u);
  EXPECT_TRUE(rec.queued.empty());
}

TEST(InjectConsumerIdempotencyTest, PayloadDoesNotOverrideCachedMessageId) {
  RecordingHooks rec;
  int64_t now = 1000;
  InjectConsumer consumer(rec.MakeHooks(), Guard(60 * 1000, &now));

  const InjectOutcome first = consumer.HandleInject(MakeNotify("npc:a", "hello"));
  InjectMessageNotify tampered = MakeNotify("npc:a", "TAMPERED");  // same inject_id
  const InjectOutcome out = consumer.HandleInject(tampered);

  EXPECT_EQ(out.code, ErrorCode::OK);
  EXPECT_EQ(out.message_id, first.message_id);
  ASSERT_EQ(rec.stored.size(), 1u);
  EXPECT_EQ(rec.stored[0].content(), "hello");
}

TEST(InjectConsumerIdempotencyTest, InvalidPayloadIsRejectedEvenIfInjectIdWasSeen) {
  RecordingHooks rec;
  int64_t now = 1000;
  InjectConsumer consumer(rec.MakeHooks(), Guard(60 * 1000, &now));

  const InjectOutcome first = consumer.HandleInject(MakeNotify("npc:a", "hello"));
  InjectMessageNotify bad = MakeNotify("npc:a", "");  // empty content, same inject_id
  const InjectOutcome out = consumer.HandleInject(bad);

  EXPECT_EQ(out.code, ErrorCode::INVALID_PARAM);
  EXPECT_TRUE(out.message_id.empty());
  EXPECT_FALSE(first.message_id.empty());
  ASSERT_EQ(rec.stored.size(), 1u);
}

TEST(InjectConsumerIdempotencyTest, DistinctInjectIdsProduceDistinctMessages) {
  RecordingHooks rec;
  int64_t now = 1000;
  InjectConsumer consumer(rec.MakeHooks(), Guard(60 * 1000, &now));

  const InjectOutcome a = consumer.HandleInject(MakeNotify("npc:a", "one"));
  InjectMessageNotify b = MakeNotify("npc:b", "two");
  b.mutable_message()->set_inject_id("inj-2");
  const InjectOutcome out_b = consumer.HandleInject(b);

  EXPECT_EQ(a.code, ErrorCode::OK);
  EXPECT_EQ(out_b.code, ErrorCode::OK);
  EXPECT_NE(a.message_id, out_b.message_id);
  ASSERT_EQ(rec.stored.size(), 2u);
  ASSERT_EQ(rec.delivered.size(), 2u);
}

TEST(InjectConsumerIdempotencyTest, EmptyInjectIdIsNotDeduplicated) {
  RecordingHooks rec;
  int64_t now = 1000;
  InjectConsumer consumer(rec.MakeHooks(), Guard(60 * 1000, &now));

  InjectMessageNotify a = MakeNotify("npc:a", "one");
  a.mutable_message()->clear_inject_id();
  InjectMessageNotify b = MakeNotify("npc:a", "two");
  b.mutable_message()->clear_inject_id();

  const InjectOutcome out_a = consumer.HandleInject(a);
  const InjectOutcome out_b = consumer.HandleInject(b);

  EXPECT_NE(out_a.message_id, out_b.message_id);
  ASSERT_EQ(rec.stored.size(), 2u);
}

TEST(InjectConsumerIdempotencyTest, ReplayDoesNotQueueAgainWhenReceiverIsOffline) {
  RecordingHooks rec;
  rec.receiver_online = false;
  int64_t now = 1000;
  InjectConsumer consumer(rec.MakeHooks(), Guard(60 * 1000, &now));

  const InjectOutcome first = consumer.HandleInject(MakeNotify("npc:a", "hello"));
  const InjectOutcome replay = consumer.HandleInject(MakeNotify("npc:a", "hello"));

  EXPECT_EQ(replay.message_id, first.message_id);
  ASSERT_EQ(rec.stored.size(), 1u);
  EXPECT_TRUE(rec.delivered.empty());
  ASSERT_EQ(rec.queued.size(), 1u);
}

// TtlInjectIdempotencyTest: 守卫自身的簿记——命中/失配/过期/容量封顶，时钟
// 全部可拨快，不依赖真实等待。
TEST(TtlInjectIdempotencyTest, MissesOnUnknownKey) {
  int64_t now = 1000;
  auto guard = Guard(60 * 1000, &now);

  EXPECT_EQ(guard->Lookup("inj-unknown"), std::nullopt);
}

TEST(TtlInjectIdempotencyTest, RemembersAndReturnsMessageId) {
  int64_t now = 1000;
  auto guard = Guard(60 * 1000, &now);
  guard->Remember("inj-1", "msg_a");

  ASSERT_TRUE(guard->Lookup("inj-1").has_value());
  EXPECT_EQ(*guard->Lookup("inj-1"), "msg_a");
  EXPECT_EQ(guard->Size(), 1u);

  // 同一 inject_id 重记：覆盖旧值，尺寸不变。
  guard->Remember("inj-1", "msg_b");
  ASSERT_TRUE(guard->Lookup("inj-1").has_value());
  EXPECT_EQ(*guard->Lookup("inj-1"), "msg_b");
  EXPECT_EQ(guard->Size(), 1u);
}

TEST(TtlInjectIdempotencyTest, ForgetsEntriesAfterTtl) {
  int64_t now = 1000;
  auto guard = Guard(1000, &now);
  guard->Remember("inj-1", "msg_a");
  now = 1999;
  ASSERT_TRUE(guard->Lookup("inj-1").has_value());

  now = 2000;  // expires_at <= now → 过期
  EXPECT_EQ(guard->Lookup("inj-1"), std::nullopt);
  EXPECT_EQ(guard->Size(), 0u);
}

TEST(TtlInjectIdempotencyTest, RememberSweepsExpiredEntriesKeepsLive) {
  int64_t now = 1000;
  auto guard = Guard(1000, &now);
  guard->Remember("inj-old", "msg_a");  // expires 2000
  now = 1500;
  guard->Remember("inj-new", "msg_b");  // expires 2500

  // Remember 先做过期清扫:inj-old 已过期整条 erase,inj-new 原样保留
  // (容量封顶测试只覆盖按最近到期挤出的那条路径,这里补的是 TTL 清扫)。
  now = 2100;
  guard->Remember("inj-fresh", "msg_c");
  EXPECT_EQ(guard->Size(), 2u);
  EXPECT_EQ(guard->Lookup("inj-old"), std::nullopt);
  ASSERT_TRUE(guard->Lookup("inj-new").has_value());
  EXPECT_EQ(*guard->Lookup("inj-new"), "msg_b");
  ASSERT_TRUE(guard->Lookup("inj-fresh").has_value());
  EXPECT_EQ(*guard->Lookup("inj-fresh"), "msg_c");
}

TEST(TtlInjectIdempotencyTest, KeepsWithinCapByDroppingClosestToExpiry) {
  int64_t now = 0;
  auto guard = std::make_shared<TtlInjectIdempotency>(1000, /*max_entries=*/2);
  guard->SetNow([&now] { return now; });

  guard->Remember("inj-a", "msg_a");  // expires 1000
  now = 100;
  guard->Remember("inj-b", "msg_b");  // expires 1100
  now = 200;
  guard->Remember("inj-c", "msg_c");  // 最旧（inj-a）被挤出

  EXPECT_EQ(guard->Size(), 2u);
  EXPECT_EQ(guard->Lookup("inj-a"), std::nullopt);
  ASSERT_TRUE(guard->Lookup("inj-b").has_value());
  EXPECT_EQ(*guard->Lookup("inj-b"), "msg_b");
  ASSERT_TRUE(guard->Lookup("inj-c").has_value());
  EXPECT_EQ(*guard->Lookup("inj-c"), "msg_c");
}

}  // namespace
