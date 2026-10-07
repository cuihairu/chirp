// MessageSearchIndex 单测（message_search 批）：真实 SQLite（/tmp 临时库）
// 上的索引、检索、过滤、游标翻页、幂等覆写与删除自愈。
#include <gtest/gtest.h>

#include <cstdio>
#include <string>
#include <unistd.h>

#include <sqlite3.h>

#include "message_search_index.h"
#include "text_segmenter.h"

namespace {

using chirp::search::MessageSearchIndex;

std::string TmpIndexPath(const std::string& tag) {
  return "/tmp/chirp_search_index_test_" + tag + "_" +
         std::to_string(static_cast<long>(::getpid())) + ".db";
}

MessageSearchIndex::Input MakeInput(const std::string& id, const std::string& channel,
                                    int channel_type, int64_t ts,
                                    const std::string& content, int msg_type = 1) {
  MessageSearchIndex::Input in;
  in.message_id = id;
  in.channel_id = channel;
  in.channel_type = channel_type;
  in.msg_type = msg_type;
  in.timestamp = ts;
  in.content = content;
  return in;
}

class SearchIndexTest : public ::testing::Test {
 protected:
  void SetUp() override {
    path_ = TmpIndexPath("main");
    std::remove(path_.c_str());
    std::remove((path_ + "-wal").c_str());
    std::remove((path_ + "-shm").c_str());
    ASSERT_TRUE(index_.Open(path_, &err_)) << err_;
  }

  void TearDown() override {
    std::remove(path_.c_str());
    std::remove((path_ + "-wal").c_str());
    std::remove((path_ + "-shm").c_str());
  }

  // 对索引背后的库文件做一次原始 SQL（第二连接）：模拟外部改动/损坏——
  // 索引连接靠 sqlite 的 schema 版本感知在下一条语句前重新编译。
  void RawExec(const std::string& sql) {
    sqlite3* raw = nullptr;
    ASSERT_EQ(sqlite3_open(path_.c_str(), &raw), SQLITE_OK);
    char* msg = nullptr;
    const int rc = sqlite3_exec(raw, sql.c_str(), nullptr, nullptr, &msg);
    const std::string msg_text = msg != nullptr ? msg : "";
    sqlite3_free(msg);
    ASSERT_EQ(rc, SQLITE_OK) << msg_text;
    ASSERT_EQ(sqlite3_close(raw), SQLITE_OK);
  }

  std::string path_;
  std::string err_;
  MessageSearchIndex index_;
};

TEST_F(SearchIndexTest, OpenIsIdempotentGuarded) {
  MessageSearchIndex second;
  ASSERT_TRUE(second.Open(TmpIndexPath("second"), &err_)) << err_;
  // 同一实例重复 Open 拒绝（已有连接）。
  std::string again_err;
  EXPECT_FALSE(index_.Open(path_, &again_err));
  EXPECT_EQ(index_.DocumentCount(), 0);
}

TEST_F(SearchIndexTest, IndexAndSearchChineseSubstring) {
  ASSERT_TRUE(index_.IndexMessage(
      MakeInput("m1", "world", 1, 100, "今天天气不错"), &err_)) << err_;
  ASSERT_TRUE(index_.IndexMessage(
      MakeInput("m2", "world", 1, 110, "明天会更好"), &err_)) << err_;

  MessageSearchIndex::Query q;
  q.keyword = "天气";
  q.limit = 10;
  const MessageSearchIndex::QueryResult r = index_.Search(q, &err_);
  EXPECT_TRUE(err_.empty()) << err_;
  ASSERT_EQ(r.hits.size(), 1u);
  EXPECT_EQ(r.hits[0].message_id, "m1");
  EXPECT_EQ(r.hits[0].channel_id, "world");
  EXPECT_EQ(r.hits[0].timestamp, 100);
  EXPECT_FALSE(r.has_more);
}

TEST(SearchIndexStandalone, LatinSearchIsCaseFoldedWordMatch) {
  MessageSearchIndex index;
  std::string err;
  const std::string path = TmpIndexPath("latin");
  std::remove(path.c_str());
  ASSERT_TRUE(index.Open(path, &err)) << err;
  ASSERT_TRUE(index.IndexMessage(MakeInput("m1", "world", 1, 1, "Hello World"), &err));
  ASSERT_TRUE(index.IndexMessage(MakeInput("m2", "world", 1, 2, "yellow journalism"), &err));
  MessageSearchIndex::Query q;
  q.keyword = "hello";
  q.limit = 10;
  const auto r = index.Search(q, &err);
  EXPECT_EQ(r.hits.size(), 1u);
  EXPECT_EQ(r.hits[0].message_id, "m1");
  std::remove(path.c_str());
}

TEST_F(SearchIndexTest, ChannelScopeAndTypeFilter) {
  ASSERT_TRUE(index_.IndexMessage(MakeInput("m1", "c1", 1, 100, "alpha"), &err_));
  ASSERT_TRUE(index_.IndexMessage(MakeInput("m2", "c2", 1, 101, "alpha"), &err_));
  // m3 与 m1 同频道（c1）但 msg_type=2：类型过滤的判别样本。
  ASSERT_TRUE(index_.IndexMessage(MakeInput("m3", "c1", 1, 102, "alpha", 2), &err_));

  MessageSearchIndex::Query q;
  q.keyword = "alpha";
  q.channel_id = "c1";
  q.limit = 10;
  auto r = index_.Search(q, &err_);
  ASSERT_EQ(r.hits.size(), 2u);
  EXPECT_EQ(r.hits[0].message_id, "m3");  // timestamp 降序
  EXPECT_EQ(r.hits[1].message_id, "m1");

  MessageSearchIndex::Query typed;
  typed.keyword = "alpha";
  typed.channel_id = "c1";
  typed.content_types = {2};
  typed.limit = 10;
  r = index_.Search(typed, &err_);
  ASSERT_EQ(r.hits.size(), 1u);
  EXPECT_EQ(r.hits[0].message_id, "m3");
}

TEST_F(SearchIndexTest, CursorPaginationAndHasMore) {
  for (int i = 0; i < 5; ++i) {
    ASSERT_TRUE(index_.IndexMessage(
        MakeInput("m" + std::to_string(i), "c", 1, 100 + i, "hit"), &err_));
  }
  MessageSearchIndex::Query q;
  q.keyword = "hit";
  q.limit = 2;
  auto page1 = index_.Search(q, &err_);
  // has_more 置位时带回第 limit+1 条（边界样本）：翻页游标必须以它推进，
  // 否则下一页与本页重叠。
  ASSERT_EQ(page1.hits.size(), 3u);
  EXPECT_EQ(page1.hits[0].message_id, "m4");
  EXPECT_EQ(page1.hits[1].message_id, "m3");
  EXPECT_EQ(page1.hits[2].message_id, "m2");
  EXPECT_TRUE(page1.has_more);

  MessageSearchIndex::Query page2q;
  page2q.keyword = "hit";
  page2q.limit = 2;
  page2q.before_timestamp = page1.hits.back().timestamp;
  page2q.before_message_id = page1.hits.back().message_id;
  const auto page2 = index_.Search(page2q, &err_);
  // 余量不足 limit+1：hits 恰为余量（2 条），has_more=false。
  ASSERT_EQ(page2.hits.size(), 2u);
  EXPECT_EQ(page2.hits[0].message_id, "m1");
  EXPECT_EQ(page2.hits[1].message_id, "m0");
  EXPECT_FALSE(page2.has_more);

  // 扫尽页没有边界样本：hits 恰为余量。
  MessageSearchIndex::Query page3q;
  page3q.keyword = "hit";
  page3q.limit = 2;
  page3q.before_timestamp = page2.hits[0].timestamp;
  page3q.before_message_id = page2.hits[0].message_id;
  const auto page3 = index_.Search(page3q, &err_);
  ASSERT_EQ(page3.hits.size(), 1u);
  EXPECT_EQ(page3.hits[0].message_id, "m0");
  EXPECT_FALSE(page3.has_more);
}

TEST_F(SearchIndexTest, ReindexIsIdempotentOverwrite) {
  ASSERT_TRUE(index_.IndexMessage(MakeInput("m1", "c", 1, 100, "old text"), &err_));
  // 同 id 重投（tail 泵可能重复喂）：更新内容后旧词不再命中、新词命中，
  // DocumentCount 不涨。
  ASSERT_TRUE(index_.IndexMessage(MakeInput("m1", "c", 1, 100, "new text"), &err_));
  EXPECT_EQ(index_.DocumentCount(), 1);

  MessageSearchIndex::Query old_q;
  old_q.keyword = "old";
  old_q.limit = 10;
  EXPECT_EQ(index_.Search(old_q, &err_).hits.size(), 0u);

  MessageSearchIndex::Query new_q;
  new_q.keyword = "new";
  new_q.limit = 10;
  const auto r = index_.Search(new_q, &err_);
  ASSERT_EQ(r.hits.size(), 1u);
  EXPECT_EQ(r.hits[0].message_id, "m1");
}

TEST_F(SearchIndexTest, DeleteRemovesAndToleratesRepeat) {
  ASSERT_TRUE(index_.IndexMessage(MakeInput("m1", "c", 1, 100, "gone"), &err_));
  EXPECT_EQ(index_.DocumentCount(), 1);
  ASSERT_TRUE(index_.DeleteMessage("m1", &err_));
  // 自愈路径允许重复删除：不存在时静默成功。
  ASSERT_TRUE(index_.DeleteMessage("m1", &err_));
  EXPECT_EQ(index_.DocumentCount(), 0);

  MessageSearchIndex::Query q;
  q.keyword = "gone";
  q.limit = 10;
  EXPECT_EQ(index_.Search(q, &err_).hits.size(), 0u);
}

TEST_F(SearchIndexTest, OversizedContentSkipsWithoutBreakingTail) {
  // >4MiB 正文不入索引但按成功处理（tail 泵不该被一条病态行卡死）。
  MessageSearchIndex::Input big = MakeInput("big", "c", 1, 1, "");
  big.content.assign(4 * 1024 * 1024 + 1, 'x');
  EXPECT_TRUE(index_.IndexMessage(big, &err_));
  EXPECT_EQ(index_.DocumentCount(), 0);
}

TEST_F(SearchIndexTest, EmptyKeywordFailsQuery) {
  MessageSearchIndex::Query q;
  q.keyword = "。。。";
  q.limit = 10;
  const auto r = index_.Search(q, &err_);
  EXPECT_FALSE(err_.empty());
  EXPECT_TRUE(r.hits.empty());
}

TEST_F(SearchIndexTest, PersistsAcrossReopen) {
  ASSERT_TRUE(index_.IndexMessage(MakeInput("m1", "c", 1, 100, "persist 你"), &err_));
  MessageSearchIndex reopened;
  ASSERT_TRUE(reopened.Open(path_, &err_)) << err_;
  MessageSearchIndex::Query q;
  q.keyword = "persist";
  q.limit = 10;
  const auto r = reopened.Search(q, &err_);
  ASSERT_EQ(r.hits.size(), 1u);
  EXPECT_EQ(r.hits[0].message_id, "m1");
}

TEST(SearchIndexClosed, OperationsOnUnopenedIndexFailSoft) {
  MessageSearchIndex index;
  std::string err;
  EXPECT_FALSE(index.IndexMessage(MakeInput("m", "c", 1, 1, "x"), &err));
  EXPECT_FALSE(index.DeleteMessage("m", &err));
  const auto r = index.Search(MessageSearchIndex::Query{}, &err);
  EXPECT_TRUE(r.hits.empty());
  EXPECT_EQ(index.DocumentCount(), 0);
}

TEST(SearchIndexOpenFailure, DirectoryPathFailsOpen) {
  MessageSearchIndex index;
  std::string err;
  EXPECT_FALSE(index.Open("/tmp", &err));
  EXPECT_FALSE(err.empty());
}

TEST(SearchIndexOpenFailure, CorruptDatabaseFailsOpen) {
  // 非 SQLite 垃圾字节：open 惰性成功，schema 语句在 prepare/step 时报
  // "file is not a database"——Open 的失败路径与 Stmt::Run 的错误分支。
  const std::string path = TmpIndexPath("corrupt");
  {
    std::FILE* f = std::fopen(path.c_str(), "wb");
    ASSERT_NE(f, nullptr);
    for (int i = 0; i < 512; ++i) {
      std::fputc('A' + (i % 26), f);
    }
    std::fclose(f);
  }
  MessageSearchIndex index;
  std::string err;
  EXPECT_FALSE(index.Open(path, &err));
  EXPECT_FALSE(err.empty());
  std::remove(path.c_str());
  std::remove((path + "-wal").c_str());
  std::remove((path + "-shm").c_str());
}

// ---- 外部篡改防线（第二连接改 schema / 删 shadow 表）：库文件不是只有本
// 进程会碰，语句族在每个 prepare/step 失败臂上的行为都必须是「报错并回滚，
// 不留半条文档」。 ----

TEST_F(SearchIndexTest, IndexMessageFailsWhenFtsTableDropped) {
  ASSERT_TRUE(index_.IndexMessage(MakeInput("m1", "c", 1, 100, "seed"), &err_));
  RawExec("DROP TABLE message_fts");
  std::string tamper_err;
  EXPECT_FALSE(index_.IndexMessage(MakeInput("m2", "c", 1, 101, "x"), &tamper_err));
  EXPECT_NE(tamper_err.find("sqlite prepare delete"), std::string::npos) << tamper_err;
  // 事务回滚：映射表没有半条新文档。
  EXPECT_EQ(index_.DocumentCount(), 1);
}

TEST_F(SearchIndexTest, IndexMessageFailsWhenFtsLosesColumns) {
  RawExec("DROP TABLE message_fts");
  RawExec("CREATE VIRTUAL TABLE message_fts USING fts5(content)");
  // del 阶段只按 rowid 删（新表照样成立），INSERT 命名的镜像列已不存在。
  std::string tamper_err;
  EXPECT_FALSE(index_.IndexMessage(MakeInput("m1", "c", 1, 100, "x"), &tamper_err));
  EXPECT_NE(tamper_err.find("sqlite prepare insert fts"), std::string::npos) << tamper_err;
}

TEST_F(SearchIndexTest, IndexMessageFailsWhenMapLosesColumn) {
  RawExec("DROP TABLE message_map");
  RawExec("CREATE TABLE message_map(message_id TEXT PRIMARY KEY)");
  // del_map 按 PK 删照常编译，ins_map 命名的 fts_row 列已不存在。
  std::string tamper_err;
  EXPECT_FALSE(index_.IndexMessage(MakeInput("m1", "c", 1, 100, "x"), &tamper_err));
  EXPECT_NE(tamper_err.find("sqlite prepare insert map"), std::string::npos) << tamper_err;
}

TEST_F(SearchIndexTest, IndexMessageFailsOnMapConstraint) {
  RawExec("DROP TABLE message_map");
  RawExec("CREATE TABLE message_map(message_id TEXT PRIMARY KEY,"
          " fts_row INTEGER NOT NULL, extra TEXT NOT NULL)");
  // 语句全部可编译，ins_map 在 step 期撞 NOT NULL 约束（Stmt::Run 错误臂）。
  std::string tamper_err;
  EXPECT_FALSE(index_.IndexMessage(MakeInput("m1", "c", 1, 100, "x"), &tamper_err));
  EXPECT_NE(tamper_err.find("sqlite step"), std::string::npos) << tamper_err;
}

TEST_F(SearchIndexTest, IndexMessageFailsWhenFtsDataShadowDropped) {
  ASSERT_TRUE(index_.IndexMessage(MakeInput("m1", "c", 1, 100, "seed"), &err_));
  RawExec("DROP TABLE message_fts_data");
  // del 语句可编译，step 期 FTS5 影子数据缺失报 malformed。
  std::string tamper_err;
  EXPECT_FALSE(index_.IndexMessage(MakeInput("m2", "c", 1, 101, "x"), &tamper_err));
  EXPECT_NE(tamper_err.find("sqlite step"), std::string::npos) << tamper_err;
}

TEST_F(SearchIndexTest, DeleteMessageFailsWhenMapDropped) {
  ASSERT_TRUE(index_.IndexMessage(MakeInput("m1", "c", 1, 100, "seed"), &err_));
  RawExec("DROP TABLE message_map");
  // del_fts 的子查询引用 message_map，prepare 期即解析失败。
  std::string tamper_err;
  EXPECT_FALSE(index_.DeleteMessage("m1", &tamper_err));
  EXPECT_NE(tamper_err.find("sqlite prepare delete"), std::string::npos) << tamper_err;
}

TEST_F(SearchIndexTest, SearchFailsOnTamperedFts) {
  ASSERT_TRUE(index_.IndexMessage(MakeInput("m1", "c", 1, 100, "needle"), &err_));
  RawExec("DROP TABLE message_fts_data");
  MessageSearchIndex::Query q;
  q.keyword = "needle";
  q.limit = 10;
  std::string search_err;
  const auto r = index_.Search(q, &search_err);
  EXPECT_TRUE(r.hits.empty());
  EXPECT_FALSE(search_err.empty()) << search_err;
}

TEST_F(SearchIndexTest, SearchFailsWhenFtsDropped) {
  RawExec("DROP TABLE message_fts");
  MessageSearchIndex::Query q;
  q.keyword = "needle";
  q.limit = 10;
  std::string search_err;
  const auto r = index_.Search(q, &search_err);
  EXPECT_TRUE(r.hits.empty());
  EXPECT_NE(search_err.find("sqlite prepare search"), std::string::npos) << search_err;
}

TEST_F(SearchIndexTest, DocumentCountFailsSoftWhenMapDropped) {
  ASSERT_TRUE(index_.IndexMessage(MakeInput("m1", "c", 1, 100, "seed"), &err_));
  RawExec("DROP TABLE message_map");
  // 统计语句编译失败按 0 处理（stats 日志用途，不抛错）。
  EXPECT_EQ(index_.DocumentCount(), 0);
}

}  // namespace
