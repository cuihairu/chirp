// 撤回墓碑原语（recall_tombstone.h）的单元测试：基础形态的内存历史向量与
// Redis 历史镜像共用这一份「置位 + 抹除正文」的实现，HybridMessageStore 的热层
// 改写也走它。墓碑的语义是原文不复存在，所以除了 is_recalled，content 必须一并
// 清空——只置位等于原文还躺在存档里。

#include <cstdint>
#include <memory>
#include <string>
#include <vector>

#include <gtest/gtest.h>

#include "fake_servers.h"
#include "in_memory_redis.h"
#include "network/redis_client.h"
#include "proto/chat.pb.h"
#include "recall_tombstone.h"

namespace {

chirp::chat::ChatMessage MakeMsg(const std::string& id, const std::string& content) {
  chirp::chat::ChatMessage msg;
  msg.set_message_id(id);
  msg.set_sender_id("alice");
  msg.set_content(content);
  msg.set_timestamp(1000);
  return msg;
}

// --- 内存历史向量 ---------------------------------------------------------

TEST(RecallTombstoneMemoryTest, MarksInPlaceAndErasesBody) {
  std::vector<chirp::chat::ChatMessage> history = {
      MakeMsg("m1", "the original text"), MakeMsg("m2", "keep me")};

  chirp::chat::MarkRecalledInMemory(history, "m1");

  EXPECT_TRUE(history[0].is_recalled());
  EXPECT_TRUE(history[0].content().empty());
  EXPECT_EQ(history[0].message_id(), "m1");
  EXPECT_EQ(history[0].sender_id(), "alice");  // 只抹正文,其余字段原样
  EXPECT_FALSE(history[1].is_recalled());
  EXPECT_EQ(history[1].content(), "keep me");
}

TEST(RecallTombstoneMemoryTest, IdempotentAndUnknownIdIsNoop) {
  std::vector<chirp::chat::ChatMessage> history = {MakeMsg("m1", "secret")};
  chirp::chat::MarkRecalledInMemory(history, "m1");
  const chirp::chat::ChatMessage after_first = history[0];

  chirp::chat::MarkRecalledInMemory(history, "m1");  // 重复撤回
  EXPECT_EQ(history[0].SerializeAsString(), after_first.SerializeAsString());

  chirp::chat::MarkRecalledInMemory(history, "nope");  // 未命中:什么都不动
  ASSERT_EQ(history.size(), 1u);
  EXPECT_TRUE(history[0].is_recalled());
  EXPECT_TRUE(history[0].content().empty());
}

// --- Redis 历史镜像（ChatMessage proto 字节） ------------------------------

class RecallTombstoneRedisTest : public ::testing::Test {
 protected:
  void SetUp() override {
    store_ = std::make_unique<chirp_test::InMemoryRedis>();
    fake_ = std::make_unique<chirp_test::FakeRedisServer>(
        [this](const std::vector<std::string>& args) { return store_->Handle(args); });
    client_ = std::make_unique<chirp::network::RedisClient>("127.0.0.1", fake_->port());
  }

  void TearDown() override {
    client_.reset();
    fake_.reset();
  }

  void Seed(const std::vector<std::string>& blobs) {
    for (const auto& blob : blobs) {
      store_->PushDirect(key_, blob);
    }
  }

  std::unique_ptr<chirp_test::InMemoryRedis> store_;
  std::unique_ptr<chirp_test::FakeRedisServer> fake_;
  std::unique_ptr<chirp::network::RedisClient> client_;
  const std::string key_ = "chirp:chat:history:ch1";
};

TEST_F(RecallTombstoneRedisTest, RewritesHitInPlaceKeepingOrder) {
  Seed({MakeMsg("m1", "first").SerializeAsString(),
        "garbage-not-proto",
        MakeMsg("m2", "second").SerializeAsString()});

  EXPECT_TRUE(chirp::chat::MarkRecalledInRedisList(*client_, key_, "m2"));

  const auto blobs = store_->ListDirect(key_);
  ASSERT_EQ(blobs.size(), 3u);
  chirp::chat::ChatMessage head;
  ASSERT_TRUE(head.ParseFromArray(blobs[0].data(), static_cast<int>(blobs[0].size())));
  EXPECT_EQ(head.message_id(), "m1");
  EXPECT_FALSE(head.is_recalled());
  EXPECT_EQ(head.content(), "first");
  EXPECT_EQ(blobs[1], "garbage-not-proto");  // 解析不了的条目不碰也不中断
  chirp::chat::ChatMessage hit;
  ASSERT_TRUE(hit.ParseFromArray(blobs[2].data(), static_cast<int>(blobs[2].size())));
  EXPECT_EQ(hit.message_id(), "m2");
  EXPECT_TRUE(hit.is_recalled());
  EXPECT_TRUE(hit.content().empty());
}

TEST_F(RecallTombstoneRedisTest, IdempotentSecondMarkWritesNothing) {
  const std::string blob = MakeMsg("m1", "text").SerializeAsString();
  Seed({blob});
  ASSERT_TRUE(chirp::chat::MarkRecalledInRedisList(*client_, key_, "m1"));
  const auto after_first = store_->ListDirect(key_);
  ASSERT_EQ(after_first.size(), 1u);

  // 已置位的条目跳过回写：既幂等，也省掉一次无谓的 LSET。
  EXPECT_TRUE(chirp::chat::MarkRecalledInRedisList(*client_, key_, "m1"));
  EXPECT_EQ(store_->ListDirect(key_)[0], after_first[0]);

  // 未命中同样是成功（无事可做不等于失败），且不改写任何条目。
  EXPECT_TRUE(chirp::chat::MarkRecalledInRedisList(*client_, key_, "absent"));
  EXPECT_EQ(store_->ListDirect(key_)[0], after_first[0]);
}

TEST_F(RecallTombstoneRedisTest, EmptyKeySucceedsWithoutWriting) {
  EXPECT_TRUE(chirp::chat::MarkRecalledInRedisList(*client_, "chirp:chat:history:empty", "m1"));
}

TEST_F(RecallTombstoneRedisTest, ReportsWriteFailure) {
  chirp_test::InMemoryRedis backing;
  chirp_test::FakeRedisServer refusing(
      [&backing](const std::vector<std::string>& args) {
        if (args[0] == "LSET") {
          return std::string("-ERR injected\r\n");
        }
        return backing.Handle(args);
      });
  chirp::network::RedisClient client("127.0.0.1", refusing.port());
  backing.PushDirect("k", MakeMsg("m1", "text").SerializeAsString());

  EXPECT_FALSE(chirp::chat::MarkRecalledInRedisList(client, "k", "m1"));
  // 写失败时条目保持原状(未置位、原文俱在),由调用方决定补偿或告警。
  chirp::chat::ChatMessage untouched;
  const auto blobs = backing.ListDirect("k");
  ASSERT_TRUE(untouched.ParseFromArray(blobs[0].data(),
                                       static_cast<int>(blobs[0].size())));
  EXPECT_FALSE(untouched.is_recalled());
  EXPECT_EQ(untouched.content(), "text");
}

TEST_F(RecallTombstoneRedisTest, PartialFailureKeepsFailingResult) {
  // 两条同 id 命中:第一条 LSET 注入失败,第二条照常回写。第二次的 LSET
  // 虽然成功,但短路右侧的 ok 已是 false —— 整体结果保持失败;已写成功的
  // 条目不回滚,由调用方按返回值决定补偿。
  chirp_test::InMemoryRedis backing;
  int lset_calls = 0;
  chirp_test::FakeRedisServer partial(
      [&backing, &lset_calls](const std::vector<std::string>& args) {
        if (args[0] == "LSET" && ++lset_calls == 1) {
          return std::string("-ERR injected\r\n");
        }
        return backing.Handle(args);
      });
  chirp::network::RedisClient client("127.0.0.1", partial.port());
  backing.PushDirect("k", MakeMsg("m1", "first").SerializeAsString());
  backing.PushDirect("k", MakeMsg("m1", "second").SerializeAsString());

  EXPECT_FALSE(chirp::chat::MarkRecalledInRedisList(client, "k", "m1"));
  const auto blobs = backing.ListDirect("k");
  ASSERT_EQ(blobs.size(), 2u);
  chirp::chat::ChatMessage head, tail;
  ASSERT_TRUE(head.ParseFromArray(blobs[0].data(), static_cast<int>(blobs[0].size())));
  EXPECT_FALSE(head.is_recalled());  // 失败的条目保持原状
  EXPECT_EQ(head.content(), "first");
  ASSERT_TRUE(tail.ParseFromArray(blobs[1].data(), static_cast<int>(blobs[1].size())));
  EXPECT_TRUE(tail.is_recalled());  // 成功的条目照常落位
  EXPECT_TRUE(tail.content().empty());
}

} // namespace
