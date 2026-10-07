// MessageIndexSync 单测（message_search 批）：fake MySQL 上的全量回填、
// tail 增量泵与查询核对（FetchMessageFacts）。
#include <gtest/gtest.h>

#include <map>
#include <memory>
#include <optional>
#include <string>
#include <vector>

#include <unistd.h>

#include <asio.hpp>

#include "fake_mysql.h"
#include "index_sync.h"
#include "message_search_index.h"
#include "mysql_message_store.h"

namespace {

using chirp::search::MessageIndexSync;
using chirp::search::MessageSearchIndex;
using chirp::chat::MySQLConnectionPool;
namespace fake_mysql = chirp_test::fake_mysql;

class SearchIndexSyncTest : public ::testing::Test {
 protected:
  void SetUp() override {
    fake_mysql::Reset();
    path_ = "/tmp/chirp_search_sync_test_" +
            std::to_string(static_cast<long>(::getpid())) + ".db";
    std::remove(path_.c_str());
    std::remove((path_ + "-wal").c_str());
    std::remove((path_ + "-shm").c_str());
    ASSERT_TRUE(index_.Open(path_, &err_)) << err_;
    pool_ = std::make_unique<MySQLConnectionPool>(1, "h", 3306, "db", "u", "p");
    sync_ = std::make_unique<MessageIndexSync>(index_, *pool_);
  }

  void TearDown() override {
    sync_.reset();
    pool_.reset();
    std::remove(path_.c_str());
    std::remove((path_ + "-wal").c_str());
    std::remove((path_ + "-shm").c_str());
  }

  // 一行 messages 表数据（列序：id, message_id, channel_id, channel_type,
  // msg_type, timestamp, content——与 IngestBatch 的 SELECT 对齐）。fake 不
  // 解析 WHERE，脚本行即「WHERE 过滤后」的结果。
  std::vector<std::optional<std::string>> Row(const char* id, const char* message_id,
                                              const char* content, const char* ts) {
    return {std::optional<std::string>(id), std::optional<std::string>(message_id),
            std::optional<std::string>("world"), std::optional<std::string>("1"),
            std::optional<std::string>("1"), std::optional<std::string>(ts),
            std::optional<std::string>(content)};
  }

  std::string path_;
  std::string err_;
  MessageSearchIndex index_;
  std::unique_ptr<MySQLConnectionPool> pool_;
  std::unique_ptr<MessageIndexSync> sync_;
};

TEST_F(SearchIndexSyncTest, BackfillIngestsBatchesUntilDrained) {
  // 两行一批：第一批吃满触发下一拍，第二批不足 batch 即停（恰是 Backfill
  // 的「吃完」判据路径）。
  fake_mysql::PushRows({Row("1", "m1", "你好世界", "100"), Row("2", "m2", "second", "101")});
  fake_mysql::PushRows({Row("3", "m3", "third", "102")});
  int64_t indexed = 0;
  ASSERT_TRUE(sync_->Backfill(&indexed, &err_, /*batch=*/2)) << err_;
  EXPECT_EQ(indexed, 3);
  EXPECT_EQ(sync_->last_id(), 3u);
  EXPECT_EQ(index_.DocumentCount(), 3);

  // 中文按「每字一 token」入库：子串关键词可命中。
  MessageSearchIndex::Query q;
  q.keyword = "好世";
  q.limit = 10;
  const auto r = index_.Search(q, &err_);
  ASSERT_EQ(r.hits.size(), 1u);
  EXPECT_EQ(r.hits[0].message_id, "m1");
}

TEST_F(SearchIndexSyncTest, BackfillSkipsMalformedRows) {
  // 病态行不卡 tail（IngestBatch 的三道防线全走一遍）。注意 fake 的列数 =
  // 同批脚本行的最大宽度：窄行会被 NULL 补齐（FetchResults 把 NULL 翻成
  // "NULL" 串），所以「短行」防线必须短行独批才能触发。
  // 批 1：只有 2 列短行 → row.size()<7 防线，全部跳过、游标不动。
  fake_mysql::PushRows({
      {std::optional<std::string>("1"), std::optional<std::string>("only-two-cols")},
      {std::optional<std::string>("2"), std::optional<std::string>("m-short-but-plausible")},
  });
  int64_t indexed = 0;
  ASSERT_TRUE(sync_->Backfill(&indexed, &err_, /*batch=*/10)) << err_;
  EXPECT_EQ(indexed, 0);
  EXPECT_EQ(sync_->last_id(), 0u);

  // 批 2：7 列行里空 message_id 与 id=0 各跳一行，好行照常入索引。
  fake_mysql::PushRows({
      Row("2", "", "no message id", "100"),
      Row("0", "zero-id", "zero rowid", "101"),
      Row("3", "m3", "good row", "102"),
  });
  ASSERT_TRUE(sync_->Backfill(&indexed, &err_, /*batch=*/10)) << err_;
  EXPECT_EQ(indexed, 1);
  EXPECT_EQ(sync_->last_id(), 3u);  // 游标只被好行推进
  EXPECT_EQ(index_.DocumentCount(), 1);
}

TEST_F(SearchIndexSyncTest, IngestFailsWhenIndexClosed) {
  // 索引未打开：IndexMessage 失败 → 摄取失败带出错误（不静默吞行）。
  MessageSearchIndex closed_index;
  MessageIndexSync closed_sync(closed_index, *pool_);
  fake_mysql::PushRows({Row("1", "m1", "x", "100")});
  int64_t indexed = 0;
  std::string ingest_err;
  EXPECT_FALSE(closed_sync.Backfill(&indexed, &ingest_err, /*batch=*/10));
  EXPECT_NE(ingest_err.find("index insert failed"), std::string::npos) << ingest_err;
  EXPECT_EQ(indexed, 0);
}

TEST_F(SearchIndexSyncTest, BackfillIngestSqlCarriesRecallFilter) {
  // fake 不解析 WHERE，撤回过滤只能验在 SQL 文本上：每条摄取语句都带
  // is_recalled=0（撤回消息不进索引的保证点）。
  fake_mysql::PushRows({Row("1", "m1", "live", "100")});
  int64_t indexed = 0;
  ASSERT_TRUE(sync_->Backfill(&indexed, &err_, /*batch=*/10)) << err_;
  const auto queries = fake_mysql::TakeQueries();
  ASSERT_FALSE(queries.empty());
  for (const auto& sql : queries) {
    if (sql.find("SELECT id, message_id") == 0) {
      EXPECT_NE(sql.find("is_recalled = 0"), std::string::npos) << sql;
    }
  }
}

TEST_F(SearchIndexSyncTest, PumpTailAdvancesCursorIncrementally) {
  fake_mysql::PushRows({Row("1", "m1", "first", "100")});
  ASSERT_TRUE(sync_->PumpTail(&err_, /*batch=*/10)) << err_;
  EXPECT_EQ(sync_->last_id(), 1u);

  // 新消息入库后再泵一拍：只吃游标之上的增量。
  fake_mysql::PushRows({Row("2", "m2", "second", "101"), Row("3", "m3", "third", "102")});
  ASSERT_TRUE(sync_->PumpTail(&err_, /*batch=*/10)) << err_;
  EXPECT_EQ(sync_->last_id(), 3u);
  EXPECT_EQ(index_.DocumentCount(), 3);
  EXPECT_EQ(sync_->indexed_total(), 3);
}

TEST_F(SearchIndexSyncTest, MysqlFailureFailsAndKeepsCursor) {
  ASSERT_TRUE(sync_->PumpTail(&err_, /*batch=*/10)) << err_;
  fake_mysql::PushQueryError("server gone away");
  std::string pump_err;
  EXPECT_FALSE(sync_->PumpTail(&pump_err, /*batch=*/10));
  EXPECT_FALSE(pump_err.empty());
  // 游标不动：下一拍重试同一批。
  EXPECT_EQ(sync_->last_id(), 0u);
}

TEST_F(SearchIndexSyncTest, IngestFailsWhenMysqlConnectUnavailable) {
  // 先排空池里构造期预建的那条连接（连接成功发生在置失败位之前），摄取拍
  // 拿不到新连接即失败关闭（连接臂，非查询臂）。
  pool_->GetConnection();
  fake_mysql::SetConnectShouldFail(true);
  std::string pump_err;
  EXPECT_FALSE(sync_->PumpTail(&pump_err, /*batch=*/10));
  EXPECT_NE(pump_err.find("mysql connection unavailable"), std::string::npos) << pump_err;
  EXPECT_EQ(sync_->last_id(), 0u);
}

TEST_F(SearchIndexSyncTest, FactsQueryErrorFails) {
  fake_mysql::PushQueryError("facts query blew up");
  std::map<std::string, MessageIndexSync::MessageFact> facts;
  std::string facts_err;
  EXPECT_FALSE(sync_->FetchMessageFacts({"m1"}, &facts, &facts_err));
  EXPECT_NE(facts_err.find("mysql facts query failed"), std::string::npos) << facts_err;
  EXPECT_TRUE(facts.empty());
}

TEST_F(SearchIndexSyncTest, FactsShortRowSkipped) {
  // 2 列脚本行（列数 = 批内最大宽度，这里就是 2）：短行防线跳过，不进结果。
  fake_mysql::PushRows({{std::optional<std::string>("m1"),
                         std::optional<std::string>("alice")}});
  std::map<std::string, MessageIndexSync::MessageFact> facts;
  ASSERT_TRUE(sync_->FetchMessageFacts({"m1"}, &facts, &err_)) << err_;
  EXPECT_TRUE(facts.empty());
}

TEST_F(SearchIndexSyncTest, FetchMessageFactsReturnsRowSlice) {
  fake_mysql::PushRows({{std::optional<std::string>("m1"),
                         std::optional<std::string>("alice"),
                         std::optional<std::string>("0"),
                         std::optional<std::string>("bob"),
                         std::optional<std::string>("0"),
                         std::optional<std::string>("edited content")},
                        {std::optional<std::string>("m2"),
                         std::optional<std::string>("carol"),
                         std::optional<std::string>("2"),
                         std::optional<std::string>(),
                         std::optional<std::string>("1"),
                         std::optional<std::string>("gone")}});
  std::map<std::string, MessageIndexSync::MessageFact> facts;
  ASSERT_TRUE(sync_->FetchMessageFacts({"m1", "m2", "m-missing"}, &facts, &err_)) << err_;
  ASSERT_EQ(facts.size(), 2u);
  EXPECT_EQ(facts["m1"].sender_id, "alice");
  EXPECT_EQ(facts["m1"].sender_kind, 0);
  EXPECT_EQ(facts["m1"].receiver_id, "bob");
  EXPECT_FALSE(facts["m1"].recalled);
  EXPECT_EQ(facts["m1"].content, "edited content");
  EXPECT_TRUE(facts["m2"].recalled);
  EXPECT_EQ(facts["m2"].sender_kind, 2);
  // NULL receiver_id 落成空串（FetchResults 的 NULL 翻译），不炸。
  EXPECT_EQ(facts["m2"].receiver_id, "NULL");
}

TEST_F(SearchIndexSyncTest, FetchMessageFactsEmptyInputShortCircuits) {
  std::map<std::string, MessageIndexSync::MessageFact> facts;
  ASSERT_TRUE(sync_->FetchMessageFacts({}, &facts, &err_));
  EXPECT_TRUE(facts.empty());
  // 空入参不该碰 MySQL：查询日志为空。
  EXPECT_TRUE(fake_mysql::TakeQueries().empty());
}

TEST_F(SearchIndexSyncTest, FetchMessageFactsMysqlDownFails) {
  // 先排空池里构造期预建的那条连接（连接成功发生在置失败位之前），下一次
  // GetConnection 走新建路径才吃 SetConnectShouldFail。
  pool_->GetConnection();
  fake_mysql::SetConnectShouldFail(true);
  std::map<std::string, MessageIndexSync::MessageFact> facts;
  std::string facts_err;
  EXPECT_FALSE(sync_->FetchMessageFacts({"m1"}, &facts, &facts_err));
  EXPECT_FALSE(facts_err.empty());
}

}  // namespace
