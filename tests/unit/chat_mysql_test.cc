// Unit tests for the MySQL-backed chat storage stack: MySQLConnection /
// MySQLConnectionPool / MySQLMessageStore (fake MySQL C API), the hybrid
// Redis+MySQL store, the migration worker, the delivery tracker and the
// paginated history retriever (fake Redis + fake MySQL).

#include <gtest/gtest.h>

#include <memory>

#include <asio.hpp>

#include "fake_mysql.h"
#include "fake_servers.h"
#include "hybrid_message_store.h"
#include "in_memory_redis.h"
#include "message_delivery_tracker.h"
#include "message_migration_worker.h"
#include "message_store_config.h"
#include "mysql_message_store.h"
#include "paginated_history_retriever.h"

namespace {

using chirp::chat::HybridMessageStore;
using chirp::chat::MessageData;
using chirp::chat::MessageDeliveryTracker;
using chirp::chat::MessageMigrationWorker;
using chirp::chat::MessageStoreConfig;
using chirp::chat::MySQLConnection;
using chirp::chat::MySQLConnectionPool;
using chirp::chat::MySQLMessageStore;
using chirp::chat::StoredMessage;
using chirp::chat::PaginatedHistoryRetriever;
namespace fake_mysql = chirp_test::fake_mysql;

MessageData MakeMessage(const std::string& id,
                        const std::string& channel = "ch1",
                        int64_t ts = 1000,
                        const std::string& receiver = "") {
  MessageData msg;
  msg.message_id = id;
  msg.sender_id = "sender";
  msg.receiver_id = receiver;
  msg.channel_id = channel;
  msg.channel_type = 0;
  msg.msg_type = 0;
  msg.content = "hello " + id;
  msg.timestamp = ts;
  msg.created_at = ts;
  return msg;
}

StoredMessage ToMySql(const MessageData& msg) {
  StoredMessage out;
  out.message_id = msg.message_id;
  out.sender_id = msg.sender_id;
  out.receiver_id = msg.receiver_id;
  out.channel_id = msg.channel_id;
  out.channel_type = msg.channel_type;
  out.msg_type = msg.msg_type;
  out.content = msg.content;
  out.timestamp = msg.timestamp;
  out.created_at = msg.created_at;
  return out;
}

// ---------------------------------------------------------------------------
// MySQLConnection / pool / store
// ---------------------------------------------------------------------------

class MySqlStoreTest : public ::testing::Test {
 protected:
  void SetUp() override { fake_mysql::Reset(); }
};

TEST_F(MySqlStoreTest, ConnectionLifecycle) {
  MySQLConnection conn("h", 3306, "db", "u", "p");
  EXPECT_TRUE(conn.Connect());
  EXPECT_TRUE(conn.IsConnected());
  EXPECT_TRUE(conn.Connect());  // idempotent when already connected
  conn.Disconnect();
  EXPECT_FALSE(conn.IsConnected());
}

TEST_F(MySqlStoreTest, ConnectionConnectFails) {
  fake_mysql::SetConnectShouldFail(true);
  MySQLConnection conn("h", 3306, "db", "u", "p");
  EXPECT_FALSE(conn.Connect());
}

TEST_F(MySqlStoreTest, QueryAndExecuteGuards) {
  MySQLConnection conn("h", 3306, "db", "u", "p");
  // Query/Execute before connecting fail.
  EXPECT_FALSE(conn.Query("SELECT 1"));
  EXPECT_FALSE(conn.Execute("SELECT 1"));

  ASSERT_TRUE(conn.Connect());
  fake_mysql::PushRows({{"1", "a"}});
  EXPECT_TRUE(conn.Query("SELECT 1"));
  auto rows = conn.FetchResults();
  ASSERT_EQ(rows.size(), 1u);
  EXPECT_EQ(rows[0][0], "1");
  EXPECT_EQ(rows[0][1], "a");

  // A second query frees the previous result.
  fake_mysql::PushRows({});
  EXPECT_TRUE(conn.Query("SELECT 2"));
  EXPECT_TRUE(conn.FetchResults().empty());

  EXPECT_TRUE(conn.Execute("UPDATE t SET x = 1"));
  fake_mysql::PushQueryError("bad");
  EXPECT_FALSE(conn.Query("SELECT 3"));
  fake_mysql::PushQueryError("bad");
  EXPECT_FALSE(conn.Execute("UPDATE t SET x = 2"));

  EXPECT_EQ(conn.LastInsertId(), 0u);
  EXPECT_EQ(conn.AffectedRows(), 0u);
  EXPECT_EQ(conn.Escape("a'b"), "a\\'b");
}

TEST_F(MySqlStoreTest, FetchResultsTranslatesNullCells) {
  MySQLConnection conn("h", 3306, "db", "u", "p");
  ASSERT_TRUE(conn.Connect());
  fake_mysql::PushRows({{std::nullopt, "x"}});
  ASSERT_TRUE(conn.Query("SELECT"));
  auto rows = conn.FetchResults();
  ASSERT_EQ(rows.size(), 1u);
  EXPECT_EQ(rows[0][0], "NULL");
}

TEST_F(MySqlStoreTest, PoolGrowsOnDemandAndReusesConnections) {
  MySQLConnectionPool pool(2, "h", 3306, "db", "u", "p");
  EXPECT_EQ(pool.GetPoolSize(), 2u);

  auto a = pool.GetConnection();
  ASSERT_TRUE(a != nullptr);
  auto b = pool.GetConnection();
  ASSERT_TRUE(b != nullptr);

  // Returning under the cap keeps them available for reuse.
  pool.ReturnConnection(std::move(a));
  EXPECT_EQ(pool.GetAvailableCount(), 1u);
  auto c = pool.GetConnection();
  ASSERT_TRUE(c != nullptr);
  EXPECT_EQ(pool.GetAvailableCount(), 0u);
}

TEST_F(MySqlStoreTest, DrainedPoolCreatesConnectionOnDemand) {
  MySQLConnectionPool pool(1, "h", 3306, "db", "u", "p");
  auto first = pool.GetConnection();  // pops the pre-created connection
  ASSERT_TRUE(first != nullptr);
  EXPECT_EQ(pool.GetAvailableCount(), 0u);

  // The pool is empty now: the next checkout creates a fresh connection.
  auto second = pool.GetConnection();
  ASSERT_TRUE(second != nullptr);
  EXPECT_TRUE(second->IsConnected());
  EXPECT_EQ(fake_mysql::LiveHandles(), 2);
}

TEST_F(MySqlStoreTest, PoolWithFailingConnectionsReturnsNull) {
  fake_mysql::SetConnectShouldFail(true);
  MySQLConnectionPool pool(1, "h", 3306, "db", "u", "p");
  EXPECT_EQ(pool.GetAvailableCount(), 0u);
  EXPECT_EQ(pool.GetConnection(), nullptr);
}

TEST_F(MySqlStoreTest, InitFailureMakesConnectFail) {
  // mysql_init returning nullptr leaves no handle for Connect to use.
  fake_mysql::SetInitShouldFail(true);
  MySQLConnection conn("h", 3306, "db", "u", "p");
  EXPECT_FALSE(conn.Connect());
  EXPECT_FALSE(conn.IsConnected());
}

TEST_F(MySqlStoreTest, FetchResultsWithoutQueryIsEmpty) {
  MySQLConnection conn("h", 3306, "db", "u", "p");
  ASSERT_TRUE(conn.Connect());
  // No Query ran, so there is no result set to fetch.
  EXPECT_TRUE(conn.FetchResults().empty());
}

TEST_F(MySqlStoreTest, DeadPoolMakesStoreReadsReturnEmpty) {
  fake_mysql::SetConnectShouldFail(true);
  auto pool = std::make_shared<MySQLConnectionPool>(1, "h", 3306, "db", "u", "p");
  MySQLMessageStore store(pool);
  // Both readers fail closed when the pool cannot produce a connection.
  EXPECT_TRUE(store.GetHistory("ch", 0, 1000, 10).empty());
  EXPECT_TRUE(store.GetOfflineMessages("u9").empty());
}

TEST_F(MySqlStoreTest, InitializeFailsWhenReceiptTableCreateFails) {
  auto pool = std::make_shared<MySQLConnectionPool>(1, "h", 3306, "db", "u", "p");
  MySQLMessageStore store(pool);
  // The first two CREATE TABLE statements still succeed; only the
  // read_receipts one fails.
  fake_mysql::FailQueriesMatching(
      std::string("\n    CREATE TABLE IF NOT EXISTS read_receipts"));
  EXPECT_FALSE(store.Initialize());
}

TEST_F(MySqlStoreTest, StoreInitializeAndFailures) {
  auto pool = std::make_shared<MySQLConnectionPool>(1, "h", 3306, "db", "u", "p");
  MySQLMessageStore store(pool);
  EXPECT_TRUE(store.Initialize());  // three CREATE TABLE statements

  fake_mysql::PushQueryError("dd1");
  EXPECT_FALSE(store.Initialize());
  fake_mysql::PushQueryError("dd2");
  fake_mysql::PushQueryError("dd3");
  EXPECT_FALSE(store.Initialize());

  fake_mysql::SetConnectShouldFail(true);
  EXPECT_FALSE(store.Initialize());
}

TEST_F(MySqlStoreTest, StoreMessageRoundTrip) {
  auto pool = std::make_shared<MySQLConnectionPool>(1, "h", 3306, "db", "u", "p");
  MySQLMessageStore store(pool);

  StoredMessage msg;
  msg.message_id = "m1";
  msg.sender_id = "s";
  msg.receiver_id = "r";
  msg.channel_id = "ch";
  msg.channel_type = 0;
  msg.msg_type = 1;
  msg.content = "it's \"quoted\"";
  msg.timestamp = 1000;
  msg.created_at = 1000;
  EXPECT_TRUE(store.StoreMessage(msg));

  auto queries = fake_mysql::TakeQueries();
  ASSERT_FALSE(queries.empty());
  EXPECT_NE(queries.back().find("INSERT INTO messages"), std::string::npos);
  EXPECT_NE(queries.back().find("it\\'s \\\"quoted\\\""), std::string::npos);

  fake_mysql::SetConnectShouldFail(true);
  MySQLMessageStore dead_store(std::make_shared<MySQLConnectionPool>(1, "h", 3306, "db", "u", "p"));
  EXPECT_FALSE(dead_store.StoreMessage(msg));
}

TEST_F(MySqlStoreTest, HistoryQueriesAndParsing) {
  auto pool = std::make_shared<MySQLConnectionPool>(1, "h", 3306, "db", "u", "p");
  MySQLMessageStore store(pool);

  fake_mysql::PushRows({{"m2", "s", "r", "ch", "0", "1", "c2", "2000"},
                        {"m1", "s", "r", "ch", "0", "1", "c1", "1000"}});
  auto history = store.GetHistory("ch", 0, 0, 10);
  ASSERT_EQ(history.size(), 2u);
  // Reversed into chronological order.
  EXPECT_EQ(history[0].message_id, "m1");
  EXPECT_EQ(history[1].message_id, "m2");
  EXPECT_EQ(history[0].timestamp, 1000);

  fake_mysql::PushRows({{"m1", "s", "r", "ch", "0", "1", "c1", "1000"}});
  auto offline = store.GetOfflineMessages("r");
  ASSERT_EQ(offline.size(), 1u);
  EXPECT_EQ(offline[0].message_id, "m1");

  fake_mysql::PushQueryError("x");
  EXPECT_TRUE(store.GetHistory("ch", 0, 0, 10).empty());
  fake_mysql::PushQueryError("x");
  EXPECT_TRUE(store.GetOfflineMessages("r").empty());
  fake_mysql::SetConnectShouldFail(true);
  EXPECT_TRUE(store.GetHistory("ch", 0, 0, 10).empty());
  EXPECT_TRUE(store.GetOfflineMessages("r").empty());
}

TEST_F(MySqlStoreTest, StoreMessageWritesReplyColumn) {
  auto pool = std::make_shared<MySQLConnectionPool>(1, "h", 3306, "db", "u", "p");
  MySQLMessageStore store(pool);

  StoredMessage msg;
  msg.message_id = "m1";
  msg.sender_id = "s";
  msg.channel_id = "ch";
  msg.msg_type = 0;
  msg.channel_type = 0;
  msg.timestamp = 1000;
  msg.created_at = 1000;
  msg.reply_to_message_id = "m0";  // 消息引用（P1）
  EXPECT_TRUE(store.StoreMessage(msg));

  auto queries = fake_mysql::TakeQueries();
  ASSERT_FALSE(queries.empty());
  EXPECT_NE(queries.back().find("INSERT INTO messages"), std::string::npos);
  EXPECT_NE(queries.back().find("reply_to"), std::string::npos);
  EXPECT_NE(queries.back().find("'m0'"), std::string::npos);
}

TEST_F(MySqlStoreTest, HistoryParsesReplyColumnAndToleratesLegacyRows) {
  auto pool = std::make_shared<MySQLConnectionPool>(1, "h", 3306, "db", "u", "p");
  MySQLMessageStore store(pool);

  // 新 schema：第 9 列 reply_to。
  fake_mysql::PushRows({{"m1", "s", "r", "ch", "0", "1", "c1", "1000", "m0"}});
  auto history = store.GetHistory("ch", 0, 0, 10);
  ASSERT_EQ(history.size(), 1u);
  EXPECT_EQ(history[0].reply_to_message_id, "m0");

  // 旧 schema 行（未回填 reply_to 列）按空引用处理。
  fake_mysql::PushRows({{"m2", "s", "r", "ch", "0", "1", "c2", "2000"}});
  auto legacy = store.GetHistory("ch", 0, 0, 10);
  ASSERT_EQ(legacy.size(), 1u);
  EXPECT_EQ(legacy[0].reply_to_message_id, "");
}

TEST_F(MySqlStoreTest, InitializeToleratesAlterFailure) {
  auto pool = std::make_shared<MySQLConnectionPool>(1, "h", 3306, "db", "u", "p");
  MySQLMessageStore store(pool);
  // 老库幂等补列：ALTER 失败（列已存在是最常见原因）只记日志，不判
  // 初始化失败——三张 CREATE TABLE 仍全部成功。
  fake_mysql::FailQueriesMatching("ALTER TABLE messages");
  EXPECT_TRUE(store.Initialize());
}

TEST_F(MySqlStoreTest, MessageExistsChecksChannelAndMessage) {
  auto pool = std::make_shared<MySQLConnectionPool>(1, "h", 3306, "db", "u", "p");
  MySQLMessageStore store(pool);

  fake_mysql::PushRows({{"1"}});
  EXPECT_TRUE(store.MessageExists("ch1", "m1"));
  auto queries = fake_mysql::TakeQueries();
  ASSERT_FALSE(queries.empty());
  EXPECT_NE(queries.back().find("SELECT 1 FROM messages"), std::string::npos);
  EXPECT_NE(queries.back().find("channel_id = 'ch1'"), std::string::npos);
  EXPECT_NE(queries.back().find("message_id = 'm1'"), std::string::npos);

  fake_mysql::PushRows({});
  EXPECT_FALSE(store.MessageExists("ch1", "m1"));

  fake_mysql::PushQueryError("select failed");
  EXPECT_FALSE(store.MessageExists("ch1", "m1"));

  // 池中留有现成连接时 GetConnection 直接复用、不走 Connect，所以
  // SetConnectShouldFail 对旧池形同虚设；换全新池让池构造与现场补连
  // 都失败，GetConnection 返回 null，MessageExists 按不可用回 false。
  fake_mysql::SetConnectShouldFail(true);
  MySQLMessageStore dead_store(std::make_shared<MySQLConnectionPool>(1, "h", 3306, "db", "u", "p"));
  EXPECT_FALSE(dead_store.MessageExists("ch1", "m1"));
}

TEST_F(MySqlStoreTest, ReceiptsUnreadAndMutations) {
  auto pool = std::make_shared<MySQLConnectionPool>(1, "h", 3306, "db", "u", "p");
  MySQLMessageStore store(pool);

  EXPECT_TRUE(store.StoreMessage(ToMySql(MakeMessage("m1"))));
  fake_mysql::PushQueryError("x");
  EXPECT_FALSE(store.StoreMessage(ToMySql(MakeMessage("m2"))));

  EXPECT_TRUE(store.ClearOfflineMessages("r"));
  fake_mysql::PushQueryError("x");
  EXPECT_FALSE(store.ClearOfflineMessages("r"));

  EXPECT_TRUE(store.StoreReadReceipt("m1", "u1", 5));
  fake_mysql::PushQueryError("x");
  EXPECT_FALSE(store.StoreReadReceipt("m1", "u1", 5));

  fake_mysql::PushRows({{"m1", "u1", "5"}});
  auto receipts = store.GetReadReceipts("m1");
  ASSERT_EQ(receipts.size(), 1u);
  EXPECT_EQ(receipts[0].user_id, "u1");
  fake_mysql::PushQueryError("x");
  EXPECT_TRUE(store.GetReadReceipts("m1").empty());

  EXPECT_TRUE(store.MarkAsRead("u1", "ch", 0, "m1", 5));
  fake_mysql::PushQueryError("x");
  EXPECT_FALSE(store.MarkAsRead("u1", "ch", 0, "m1", 5));

  fake_mysql::PushRows({{"7"}});
  EXPECT_EQ(store.GetUnreadCount("u1"), 7);
  fake_mysql::PushRows({{std::nullopt}});
  EXPECT_EQ(store.GetUnreadCount("u1"), 0);  // SUM() of nothing
  fake_mysql::PushRows({});
  EXPECT_EQ(store.GetUnreadCount("u1"), 0);
  fake_mysql::PushQueryError("x");
  EXPECT_EQ(store.GetUnreadCount("u1"), 0);

  fake_mysql::PushRows({{"ch1", "3"}, {"ch2", "4"}});
  auto unread = store.GetAllUnread("u1");
  ASSERT_EQ(unread.size(), 2u);
  EXPECT_EQ(unread[0].first, "ch1");
  fake_mysql::PushQueryError("x");
  EXPECT_TRUE(store.GetAllUnread("u1").empty());

  fake_mysql::SetConnectShouldFail(true);
  MySQLMessageStore dead_store(std::make_shared<MySQLConnectionPool>(1, "h", 3306, "db", "u", "p"));
  EXPECT_FALSE(dead_store.ClearOfflineMessages("r"));
  EXPECT_FALSE(dead_store.StoreReadReceipt("m", "u", 1));
  EXPECT_TRUE(dead_store.GetReadReceipts("m").empty());
  EXPECT_FALSE(dead_store.MarkAsRead("u", "c", 0, "m", 1));
  EXPECT_EQ(dead_store.GetUnreadCount("u"), 0);
  EXPECT_TRUE(dead_store.GetAllUnread("u").empty());
}

// ---------------------------------------------------------------------------
// HybridMessageStore (fake Redis for the hot path, fake MySQL for cold)
// ---------------------------------------------------------------------------

class HybridStoreTest : public ::testing::Test {
 protected:
  void SetUp() override {
    fake_mysql::Reset();
    redis_ = std::make_unique<chirp_test::InMemoryRedis>();
    fake_ = std::make_unique<chirp_test::FakeRedisServer>(
        [this](const std::vector<std::string>& args) { return redis_->Handle(args); });

    MessageStoreConfig cfg;
    cfg.redis_port = fake_->port();
    cfg.mysql_pool_size = 1;
    store_ = std::make_unique<HybridMessageStore>(io_, cfg);
  }

  void TearDown() override {
    store_.reset();
    fake_.reset();
  }

  asio::io_context io_;
  std::unique_ptr<chirp_test::InMemoryRedis> redis_;
  std::unique_ptr<chirp_test::FakeRedisServer> fake_;
  std::unique_ptr<HybridMessageStore> store_;
};

TEST_F(HybridStoreTest, MessageDataSerializationRoundTrip) {
  MessageData msg = MakeMessage("m1");
  msg.reply_to_message_id = "m0";
  const std::string blob = msg.SerializeAsString();
  MessageData parsed;
  ASSERT_TRUE(parsed.ParseFromArray(blob.data(), static_cast<int>(blob.size())));
  EXPECT_EQ(parsed.message_id, "m1");
  EXPECT_EQ(parsed.channel_id, "ch1");
  EXPECT_EQ(parsed.content, "hello m1");
  // 消息引用（P1）：引用 ID 随序列化往返，不因存储落盘而丢失。
  EXPECT_EQ(parsed.reply_to_message_id, "m0");

  EXPECT_FALSE(parsed.ParseFromArray("garbage", 7));
}

TEST_F(HybridStoreTest, InitializeToleratesDeadRedisAndMysqlFailures) {
  EXPECT_TRUE(store_->Initialize());
  store_->Shutdown();

  fake_mysql::PushQueryError("create table failed");
  EXPECT_FALSE(store_->Initialize());

  fake_mysql::SetConnectShouldFail(true);
  HybridMessageStore dead_redis(io_, [] {
    MessageStoreConfig cfg;
    cfg.redis_port = 1;  // nothing listening
    return cfg;
    }());
  EXPECT_FALSE(dead_redis.Initialize());
}

TEST_F(HybridStoreTest, StoreMessageWritesHistoryTiersOnly) {
  ASSERT_TRUE(store_->Initialize());

  EXPECT_TRUE(store_->StoreMessage(MakeMessage("m1", "ch1", 1000, "r1")));
  // StoreMessage persists history only: whether the receiver needs an
  // offline copy is decided by the send handler from the router's
  // delivery count, so the offline queue must stay untouched here.
  EXPECT_EQ(redis_->ListDirect("chirp:chat:history:ch1").size(), 1u);
  EXPECT_TRUE(redis_->ListDirect("chirp:chat:offline:*").empty());

  // Group channel messages land in their own history list.
  MessageData group = MakeMessage("m2", "ch2", 2000);
  group.channel_type = 1;
  EXPECT_TRUE(store_->StoreMessage(group));
  EXPECT_EQ(redis_->ListDirect("chirp:chat:history:ch2").size(), 1u);
  EXPECT_TRUE(redis_->ListDirect("chirp:chat:offline:*").empty());

  fake_mysql::PushQueryError("insert failed");
  EXPECT_FALSE(store_->StoreMessage(MakeMessage("m3")));
}

TEST_F(HybridStoreTest, StoreMessageAsyncDeliversCallback) {
  ASSERT_TRUE(store_->Initialize());

  std::promise<bool> done;
  auto future = done.get_future();
  store_->StoreMessageAsync(MakeMessage("m1", "ch1", 1000, "r1"),
                            [&](bool ok) { done.set_value(ok); });
  io_.run();
  ASSERT_EQ(future.wait_for(std::chrono::seconds(2)), std::future_status::ready);
  EXPECT_TRUE(future.get());
  EXPECT_EQ(redis_->ListDirect("chirp:chat:history:ch1").size(), 1u);
}

TEST_F(HybridStoreTest, GetHistoryMergesRedisAndMysqlWithoutDuplicates) {
  ASSERT_TRUE(store_->Initialize());

  store_->StoreMessage(MakeMessage("m1", "ch", 1000));
  store_->StoreMessage(MakeMessage("m2", "ch", 2000));

  // MySQL returns m2 again (duplicate) plus m3 (new).
  fake_mysql::PushRows({{"m2", "s", "r", "ch", "0", "0", "c2", "2000"},
                        {"m3", "s", "r", "ch", "0", "0", "c3", "3000"}});
  auto history = store_->GetHistory("ch", 0, 0, 10);
  ASSERT_EQ(history.size(), 3u);
  EXPECT_EQ(history[0].message_id, "m1");
  EXPECT_EQ(history[2].message_id, "m3");

  // before_timestamp filtering drops newer messages.
  auto older = store_->GetHistory("ch", 0, 2500, 10);
  EXPECT_EQ(older.size(), 2u);  // m1, m2
}

TEST_F(HybridStoreTest, GetHistoryV2CursorPagination) {
  ASSERT_TRUE(store_->Initialize());

  store_->StoreMessage(MakeMessage("m1", "ch", 1000));
  store_->StoreMessage(MakeMessage("m2", "ch", 2000));
  store_->StoreMessage(MakeMessage("m3", "ch", 3000));

  std::string next;
  // The Redis hot path only returns the newest `limit` messages.
  auto page = store_->GetHistoryV2("ch", 0, "", 2, &next);
  ASSERT_EQ(page.size(), 2u);  // m2, m3
  EXPECT_FALSE(next.empty());  // full page -> cursor emitted
  EXPECT_EQ(next, "2000:0");

  // A large limit pulls everything back.
  page = store_->GetHistoryV2("ch", 0, "not-a-cursor", 10, &next);
  EXPECT_EQ(page.size(), 3u);

  page = store_->GetHistoryV2("ch", 0, "2500:0", 10, &next);
  EXPECT_EQ(page.size(), 2u);  // m1, m2

  // Less than a full page -> no next cursor.
  std::string none;
  store_->GetHistoryV2("ch", 0, "", 100, &none);
  EXPECT_TRUE(none.empty());
}

TEST_F(HybridStoreTest, HasMessageResolvesHotTierThenMysqlColdTier) {
  ASSERT_TRUE(store_->Initialize());

  // 热层命中：StoreMessage 同步写 Redis，刚发出的消息立即可作引用目标。
  MessageData hot = MakeMessage("m1", "ch1", 1000);
  hot.reply_to_message_id = "m0";
  ASSERT_TRUE(store_->StoreMessage(hot));
  EXPECT_TRUE(store_->HasMessage("ch1", "m1"));

  // 热层未命中（其他频道）→ 落 MySQL 冷层：无行 → 不存在。
  fake_mysql::PushRows({});
  EXPECT_FALSE(store_->HasMessage("ch2", "m1"));

  // 热层未命中、冷层命中（已从 Redis 列表老化的消息仍可被引用）。
  fake_mysql::PushRows({{"1"}});
  EXPECT_TRUE(store_->HasMessage("ch3", "m-aged"));

  // 空参数守卫。
  EXPECT_FALSE(store_->HasMessage("", "m1"));
  EXPECT_FALSE(store_->HasMessage("ch1", ""));
}

TEST_F(HybridStoreTest, GetHistoryMysqlMergeCarriesReply) {
  ASSERT_TRUE(store_->Initialize());

  // 热层只有一条；limit 要求两条 → 触发 MySQL 回退合并。
  ASSERT_TRUE(store_->StoreMessage(MakeMessage("m2", "ch", 2000)));
  fake_mysql::PushRows({{"m1", "s", "r", "ch", "0", "0", "hello m1", "1000", "m0"}});
  auto page = store_->GetHistory("ch", 0, 0, 2);
  ASSERT_EQ(page.size(), 2u);
  const MessageData* old = nullptr;
  const MessageData* fresh = nullptr;
  for (const auto& m : page) {
    if (m.message_id == "m1") old = &m;
    if (m.message_id == "m2") fresh = &m;
  }
  ASSERT_NE(old, nullptr);
  ASSERT_NE(fresh, nullptr);
  EXPECT_EQ(old->reply_to_message_id, "m0");
  EXPECT_EQ(fresh->reply_to_message_id, "");
}

TEST_F(HybridStoreTest, OfflineQueueOperations) {
  ASSERT_TRUE(store_->Initialize());

  EXPECT_TRUE(store_->AddOfflineMessage("r1", MakeMessage("m1", "ch", 1000, "r1").SerializeAsString()));
  EXPECT_TRUE(store_->AddOfflineMessage("r1", MakeMessage("m2", "ch", 2000, "r1").SerializeAsString()));

  auto messages = store_->GetOfflineMessages("r1");
  ASSERT_EQ(messages.size(), 2u);
  EXPECT_EQ(messages[0].message_id, "m1");
  EXPECT_EQ(messages[1].message_id, "m2");

  auto popped = store_->PopOfflineMessages("r1");
  EXPECT_EQ(popped.size(), 2u);
  EXPECT_TRUE(store_->GetOfflineMessages("r1").empty());

  EXPECT_TRUE(store_->AddOfflineMessage("r1", MakeMessage("m3", "ch", 3000, "r1").SerializeAsString()));
  EXPECT_TRUE(store_->ClearOfflineMessages("r1"));
  EXPECT_TRUE(store_->GetOfflineMessages("r1").empty());
}

TEST_F(HybridStoreTest, OfflineQueueFallsBackToMemoryWhenRedisDown) {
  // Same pattern as InitializeToleratesDeadRedisAndMysqlFailures: port 1
  // refuses every connection, so every Redis command fails.
  MessageStoreConfig cfg;
  cfg.redis_port = 1;
  HybridMessageStore dead_redis(io_, cfg);

  // With Redis unreachable, every RPush fails and the message must land
  // in the in-memory fallback instead of being dropped.
  EXPECT_FALSE(dead_redis.AddOfflineMessage("r1", MakeMessage("m1", "ch", 1000, "r1").SerializeAsString()));
  EXPECT_FALSE(dead_redis.AddOfflineMessage("r2", MakeMessage("m2", "ch", 2000, "r2").SerializeAsString()));

  auto r1 = dead_redis.GetOfflineMessages("r1");
  ASSERT_EQ(r1.size(), 1u);
  EXPECT_EQ(r1[0].message_id, "m1");
  auto r2 = dead_redis.GetOfflineMessages("r2");
  ASSERT_EQ(r2.size(), 1u);
  EXPECT_EQ(r2[0].message_id, "m2");

  // Pop drains the fallback queue; other users are unaffected.
  auto popped = dead_redis.PopOfflineMessages("r1");
  ASSERT_EQ(popped.size(), 1u);
  EXPECT_EQ(popped[0].message_id, "m1");
  EXPECT_TRUE(dead_redis.GetOfflineMessages("r1").empty());
  EXPECT_EQ(dead_redis.GetOfflineMessages("r2").size(), 1u);

  // Clear removes the remaining fallback entry without Redis.
  EXPECT_FALSE(dead_redis.ClearOfflineMessages("r2"));  // Redis Del still fails...
  EXPECT_TRUE(dead_redis.GetOfflineMessages("r2").empty());  // ...but fallback is gone.
}

TEST_F(HybridStoreTest, OfflineFallbackQueueIsCappedPerUser) {
  // Redis down so every push lands in the in-memory fallback; the queue
  // must cap itself instead of growing without bound.
  MessageStoreConfig cfg;
  cfg.redis_port = 1;
  HybridMessageStore dead_redis(io_, cfg);

  EXPECT_FALSE(dead_redis.AddOfflineMessage("r1", MakeMessage("m0", "ch", 0, "r1").SerializeAsString()));
  for (int i = 1; i <= 1024; ++i) {
    dead_redis.AddOfflineMessage(
        "r1", MakeMessage("m" + std::to_string(i), "ch", i, "r1").SerializeAsString());
  }

  // The oldest entry was evicted; exactly the newest 1024 remain.
  auto remaining = dead_redis.GetOfflineMessages("r1");
  ASSERT_EQ(remaining.size(), 1024u);
  EXPECT_EQ(remaining.front().message_id, "m1");
  EXPECT_EQ(remaining.back().message_id, "m1024");
}

TEST_F(HybridStoreTest, RemoveOfflineMessageDropsRedisAndFallbackCopies) {
  ASSERT_TRUE(store_->Initialize());

  // Redis-backed copy: the late-ack cleanup removes it via LREM.
  const std::string blob1 = MakeMessage("m1", "ch", 1000, "r1").SerializeAsString();
  EXPECT_TRUE(store_->AddOfflineMessage("r1", blob1));
  EXPECT_TRUE(store_->RemoveOfflineMessage("r1", blob1));
  EXPECT_TRUE(store_->GetOfflineMessages("r1").empty());

  // An unknown payload has nothing to remove.
  EXPECT_FALSE(store_->RemoveOfflineMessage("r1", "not-stored"));

  // Fallback copy (Redis down): the in-memory queue is scanned instead, and
  // the user's fallback entry disappears with its last message.
  MessageStoreConfig cfg;
  cfg.redis_port = 1;
  HybridMessageStore dead_redis(io_, cfg);
  const std::string blob2 = MakeMessage("m2", "ch", 2000, "r2").SerializeAsString();
  EXPECT_FALSE(dead_redis.AddOfflineMessage("r2", blob2));
  EXPECT_TRUE(dead_redis.RemoveOfflineMessage("r2", blob2));
  EXPECT_TRUE(dead_redis.GetOfflineMessages("r2").empty());
  EXPECT_FALSE(dead_redis.RemoveOfflineMessage("r2", blob2));  // already gone

  // Two fallback copies: a scan that misses the first element still reaches
  // the second (loop-continue arm), then a non-matching remove walks off the
  // end without erasing (loop-exit-without-match arm).
  const std::string a = MakeMessage("ma", "ch", 1, "r3").SerializeAsString();
  const std::string b = MakeMessage("mb", "ch", 2, "r3").SerializeAsString();
  EXPECT_FALSE(dead_redis.AddOfflineMessage("r3", a));
  EXPECT_FALSE(dead_redis.AddOfflineMessage("r3", b));
  EXPECT_TRUE(dead_redis.RemoveOfflineMessage("r3", b));
  EXPECT_FALSE(dead_redis.RemoveOfflineMessage("r3", "not-in-queue"));
  EXPECT_TRUE(dead_redis.RemoveOfflineMessage("r3", a));
}

TEST_F(HybridStoreTest, DeliveryTrackingLifecycle) {
  ASSERT_TRUE(store_->Initialize());

  const std::string tracking = store_->TrackMessage("m1", "r1", INT64_MAX);
  EXPECT_TRUE(tracking.rfind("track_", 0) == 0);

  auto status = store_->GetDeliveryStatus("m1", "r1");
  ASSERT_TRUE(status.has_value());
  EXPECT_EQ(status->status, chirp::chat::DeliveryState::kPending);

  EXPECT_TRUE(store_->AcknowledgeMessage("m1", "r1"));
  status = store_->GetDeliveryStatus("m1", "r1");
  ASSERT_TRUE(status.has_value());
  EXPECT_EQ(status->status, chirp::chat::DeliveryState::kDelivered);

  EXPECT_TRUE(store_->FailMessage("m2", "r1", "boom"));
  status = store_->GetDeliveryStatus("m2", "r1");
  ASSERT_TRUE(status.has_value());
  EXPECT_EQ(status->status, chirp::chat::DeliveryState::kFailed);
  EXPECT_EQ(status->last_error, "boom");

  EXPECT_FALSE(store_->GetDeliveryStatus("ghost", "r1").has_value());
}

TEST_F(HybridStoreTest, PendingDeliveriesFilterByExpiry) {
  ASSERT_TRUE(store_->Initialize());

  store_->TrackMessage("m1", "r1", 500);   // expired long ago
  store_->TrackMessage("m2", "r2", 9999999999999);  // far future

  auto due = store_->GetPendingDeliveries(1000);
  ASSERT_EQ(due.size(), 1u);
  EXPECT_EQ(due[0].message_id, "m1");
}

TEST_F(HybridStoreTest, PrivateChannelIdOrderingAndAccessors) {
  EXPECT_EQ(HybridMessageStore::PrivateChannelId("a", "b"), "a|b");
  EXPECT_EQ(HybridMessageStore::PrivateChannelId("b", "a"), "a|b");
  EXPECT_NE(store_->GetRedisClient(), nullptr);
  EXPECT_NE(store_->GetMySQLStore(), nullptr);
  EXPECT_EQ(store_->GetConfig().mysql_pool_size, 1u);
}

// ---------------------------------------------------------------------------
// MessageDeliveryTracker
// ---------------------------------------------------------------------------

class DeliveryTrackerTest : public ::testing::Test {
 protected:
  void SetUp() override {
    fake_mysql::Reset();
    redis_ = std::make_unique<chirp_test::InMemoryRedis>();
    fake_ = std::make_unique<chirp_test::FakeRedisServer>(
        [this](const std::vector<std::string>& args) { return redis_->Handle(args); });

    MessageStoreConfig cfg;
    cfg.redis_port = fake_->port();
    store_ = std::make_shared<HybridMessageStore>(io_, cfg);
    ASSERT_TRUE(store_->Initialize());
  }

  void TearDown() override {
    tracker_.reset();
    store_.reset();
    fake_.reset();
  }

  asio::io_context io_;
  std::unique_ptr<chirp_test::InMemoryRedis> redis_;
  std::unique_ptr<chirp_test::FakeRedisServer> fake_;
  std::shared_ptr<HybridMessageStore> store_;
  std::unique_ptr<MessageDeliveryTracker> tracker_;
};

TEST_F(DeliveryTrackerTest, TrackAcknowledgeFailAndStats) {
  chirp::chat::MessageDeliveryTracker::Config cfg;
  cfg.check_interval_seconds = 0;  // fire immediately when started
  tracker_ = std::make_unique<MessageDeliveryTracker>(io_, store_, cfg);

  std::vector<std::string> events;
  tracker_->SetDeliveryCallback(
      [&](const std::string& id, const std::string&, chirp::chat::DeliveryState,
          const std::string&) { events.push_back(id); });

  tracker_->TrackMessage("m1", "r1", INT64_MAX);
  tracker_->Acknowledge("m1", "r1");
  tracker_->Fail("m2", "r1", "boom");
  ASSERT_EQ(events.size(), 2u);

  auto status = tracker_->GetStatus("m1", "r1");
  ASSERT_TRUE(status.has_value());
  EXPECT_EQ(status->status, chirp::chat::DeliveryState::kDelivered);

  auto stats = tracker_->GetStats();
  EXPECT_EQ(stats.successful_deliveries, 1u);
  EXPECT_EQ(stats.failed_deliveries, 1u);
  EXPECT_EQ(stats.total_tracked, 1u);

  tracker_->Stop();
}

TEST_F(DeliveryTrackerTest, StartStopIdempotentAndTimeoutCheckFailsPending) {
  chirp::chat::MessageDeliveryTracker::Config cfg;
  cfg.check_interval_seconds = 0;
  cfg.delivery_timeout_seconds = 0;  // everything is instantly overdue
  tracker_ = std::make_unique<MessageDeliveryTracker>(io_, store_, cfg);

  // Double start/stop are idempotent.
  tracker_->Start();
  tracker_->Start();
  EXPECT_TRUE(tracker_->GetStats().total_tracked == 0u);

  store_->TrackMessage("late", "r1", 1);  // expires in the past
  store_->TrackMessage("doomed", "r1", 1);  // expires in the past, still pending
  // "late" is still listed by GetPendingDeliveries but already delivered:
  // RunCheck must take the `status && status != kPending` short-circuit and
  // leave it delivered; "doomed" stays kPending and is failed on timeout.
  store_->AcknowledgeMessage("late", "r1");

  // A pending-list entry whose delivery status key has expired/removed: the
  // tracker still sees it in GetPendingDeliveries, but GetDeliveryStatus
  // returns nullopt, so RunCheck takes the `status &&` short-circuit false
  // branch (never reaches Fail or the kPending compare).
  store_->TrackMessage("vanish", "r1", 1);
  ASSERT_TRUE(store_->GetRedisClient()->Del("chirp:chat:delivery:vanish:r1"));

  // Run the io loop: the timer fires RunCheck immediately.
  std::thread runner([this] { io_.run(); });
  std::this_thread::sleep_for(std::chrono::milliseconds(300));
  tracker_->Stop();
  runner.join();

  EXPECT_FALSE(store_->GetDeliveryStatus("vanish", "r1").has_value());
  EXPECT_EQ(store_->GetDeliveryStatus("late", "r1")->status,
            chirp::chat::DeliveryState::kDelivered);
  EXPECT_EQ(store_->GetDeliveryStatus("doomed", "r1")->status,
            chirp::chat::DeliveryState::kFailed);

  auto status = store_->GetDeliveryStatus("late", "r1");
  ASSERT_TRUE(status.has_value());
  EXPECT_EQ(status->status, chirp::chat::DeliveryState::kDelivered);

  status = store_->GetDeliveryStatus("doomed", "r1");
  ASSERT_TRUE(status.has_value());
  EXPECT_EQ(status->status, chirp::chat::DeliveryState::kFailed);

  // Default-config constructor and Stop without Start.
  MessageDeliveryTracker idle(io_, store_);
  idle.Stop();
}

// ---------------------------------------------------------------------------
// MessageMigrationWorker
// ---------------------------------------------------------------------------

class MigrationWorkerTest : public ::testing::Test {
 protected:
  void SetUp() override {
    fake_mysql::Reset();
    redis_ = std::make_unique<chirp_test::InMemoryRedis>();
    fake_ = std::make_unique<chirp_test::FakeRedisServer>(
        [this](const std::vector<std::string>& args) { return redis_->Handle(args); });

    cfg_.redis_port = fake_->port();
    store_ = std::make_shared<HybridMessageStore>(io_, cfg_);
    ASSERT_TRUE(store_->Initialize());
    worker_ = std::make_unique<MessageMigrationWorker>(io_, store_, cfg_);
  }

  void TearDown() override {
    worker_.reset();
    store_.reset();
    fake_.reset();
  }

  MessageStoreConfig cfg_;
  asio::io_context io_;
  std::unique_ptr<chirp_test::InMemoryRedis> redis_;
  std::unique_ptr<chirp_test::FakeRedisServer> fake_;
  std::shared_ptr<HybridMessageStore> store_;
  std::unique_ptr<MessageMigrationWorker> worker_;
};

TEST_F(MigrationWorkerTest, StartStopAndDisabledByConfig) {
  worker_->Start();
  worker_->Start();  // idempotent
  worker_->Stop();
  worker_->Stop();  // idempotent

  MessageStoreConfig disabled = cfg_;
  disabled.enable_migration = false;
  MessageMigrationWorker off(io_, store_, disabled);
  off.Start();  // no-op
  off.Stop();
  EXPECT_EQ(off.GetStats().batches_processed, 0);
}

TEST_F(MigrationWorkerTest, RunMigrationNowMigratesHistoryAndOffline) {
  // Seed one channel history entry and one offline entry whose receiver is
  // empty (the worker fills it from the key).
  const std::string blob = MakeMessage("h1", "chan1", 1000).SerializeAsString();
  redis_->PushDirect("chirp:chat:history:chan1", blob);
  const std::string offline_blob = MakeMessage("o1", "x", 2000).SerializeAsString();
  redis_->PushDirect("chirp:chat:offline:user9", offline_blob);

  worker_->RunMigrationNow();
  io_.run();

  const auto stats = worker_->GetStats();
  EXPECT_EQ(stats.total_migrated, 2);
  EXPECT_EQ(stats.total_failed, 0);
  EXPECT_EQ(stats.batches_processed, 1);
  EXPECT_GE(stats.last_migration_time_ms, 0);

  // The INSERTs went through the fake MySQL driver.
  auto queries = fake_mysql::TakeQueries();
  EXPECT_GE(queries.size(), 2u);
}

TEST_F(MigrationWorkerTest, RunMigrationNowSkipsGarbageAndCountsFailures) {
  redis_->PushDirect("chirp:chat:history:chan1", "not-a-message");
  fake_mysql::PushQueryError("insert failed");
  redis_->PushDirect("chirp:chat:history:chan2",
                     MakeMessage("h2", "chan2", 1).SerializeAsString());

  worker_->RunMigrationNow();
  io_.run();

  const auto stats = worker_->GetStats();
  EXPECT_EQ(stats.total_migrated, 0);
  EXPECT_EQ(stats.total_failed, 1);  // the parseable row failed to store
}

TEST_F(MigrationWorkerTest, RunMigrationNowCountsOfflineFailures) {
  // A parseable offline message whose INSERT fails is counted as failed too.
  redis_->PushDirect("chirp:chat:offline:user9",
                     MakeMessage("o1", "x", 2000).SerializeAsString());
  fake_mysql::PushQueryError("insert failed");

  worker_->RunMigrationNow();
  io_.run();

  const auto stats = worker_->GetStats();
  EXPECT_EQ(stats.total_migrated, 0);
  EXPECT_EQ(stats.total_failed, 1);
}

TEST_F(MigrationWorkerTest, ScheduledRunExecutesViaTimer) {
  cfg_.migration_interval_seconds = 0;  // fire immediately
  worker_ = std::make_unique<MessageMigrationWorker>(io_, store_, cfg_);
  worker_->Start();

  std::thread runner([this] { io_.run(); });
  // Poll until the first batch lands instead of a fixed sleep: under
  // coverage builds / load the io thread can take longer than any fixed nap
  // to arm the zero-interval timer, and a Stop() landing first suppresses
  // the batch entirely (running_ gates ScheduleNextRun).
  const auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(5);
  while (worker_->GetStats().batches_processed < 1 &&
         std::chrono::steady_clock::now() < deadline) {
    std::this_thread::sleep_for(std::chrono::milliseconds(5));
  }
  worker_->Stop();
  runner.join();
  // A zero interval fires the batch repeatedly until stopped.
  EXPECT_GE(worker_->GetStats().batches_processed, 1);
}

// ---------------------------------------------------------------------------
// PaginatedHistoryRetriever
// ---------------------------------------------------------------------------

class PaginatedRetrieverTest : public ::testing::Test {
 protected:
  void SetUp() override {
    fake_mysql::Reset();
    redis_ = std::make_unique<chirp_test::InMemoryRedis>();
    fake_ = std::make_unique<chirp_test::FakeRedisServer>(
        [this](const std::vector<std::string>& args) { return redis_->Handle(args); });

    MessageStoreConfig cfg;
    cfg.redis_port = fake_->port();
    store_ = std::make_shared<HybridMessageStore>(io_, cfg);
    ASSERT_TRUE(store_->Initialize());
  }

  void TearDown() override {
    store_.reset();
    fake_.reset();
  }

  asio::io_context io_;
  std::unique_ptr<chirp_test::InMemoryRedis> redis_;
  std::unique_ptr<chirp_test::FakeRedisServer> fake_;
  std::shared_ptr<HybridMessageStore> store_;
};

TEST_F(PaginatedRetrieverTest, PageTokenSerializeDeserialize) {
  chirp::chat::PaginatedHistoryRetriever::PageToken token;
  token.cursor = "chan";
  token.timestamp = 12345;
  token.page_size = 20;
  const std::string blob = token.Serialize();
  auto parsed = chirp::chat::PaginatedHistoryRetriever::PageToken::Deserialize(blob);
  EXPECT_EQ(parsed.cursor, "chan");
  EXPECT_EQ(parsed.timestamp, 12345);
  EXPECT_EQ(parsed.page_size, 20);
  EXPECT_TRUE(parsed.IsValid());

  auto bad = chirp::chat::PaginatedHistoryRetriever::PageToken::Deserialize("garbage");
  EXPECT_FALSE(bad.IsValid());
}

TEST_F(PaginatedRetrieverTest, FirstNextAndBeforePages) {
  ASSERT_TRUE(store_->Initialize());
  store_->StoreMessage(MakeMessage("m1", "ch", 1000));
  store_->StoreMessage(MakeMessage("m2", "ch", 2000));
  store_->StoreMessage(MakeMessage("m3", "ch", 3000));

  PaginatedHistoryRetriever retriever(store_);

  auto first = retriever.GetFirstPage("ch", 0, 10);
  EXPECT_EQ(first.messages.size(), 3u);
  EXPECT_TRUE(first.has_more);
  EXPECT_EQ(first.next_page.cursor, "ch");
  EXPECT_EQ(first.next_page.timestamp, 1000);

  // Nothing is older than the first page's oldest message.
  auto next = retriever.GetNextPage(first.next_page);
  EXPECT_TRUE(next.messages.empty());
  auto invalid = retriever.GetNextPage(
      chirp::chat::PaginatedHistoryRetriever::PageToken{});
  EXPECT_TRUE(invalid.messages.empty());

  auto before = retriever.GetPageBefore("ch", 0, 2500, 10);
  EXPECT_EQ(before.messages.size(), 2u);

  auto after = retriever.GetPageAfter("ch", 0, 1500, 10);
  ASSERT_EQ(after.messages.size(), 2u);
  EXPECT_EQ(after.messages[0].message_id, "m2");
}

TEST_F(PaginatedRetrieverTest, SearchStubAndTimeRange) {
  ASSERT_TRUE(store_->Initialize());
  store_->StoreMessage(MakeMessage("m1", "ch", 1000));
  store_->StoreMessage(MakeMessage("m2", "ch", 2000));
  store_->StoreMessage(MakeMessage("m3", "ch", 3000));

  PaginatedHistoryRetriever retriever(store_);
  EXPECT_TRUE(retriever.Search("ch", 0, "hello", 10).empty());

  auto ranged = retriever.GetTimeRange("ch", 0, 1500, 2500, 10);
  ASSERT_EQ(ranged.size(), 1u);
  EXPECT_EQ(ranged[0].message_id, "m2");
}

TEST_F(PaginatedRetrieverTest, NextPageFillsTokenWhenOlderMessagesExist) {
  ASSERT_TRUE(store_->Initialize());
  store_->StoreMessage(MakeMessage("m1", "ch", 1000));
  store_->StoreMessage(MakeMessage("m2", "ch", 2000));
  store_->StoreMessage(MakeMessage("m3", "ch", 3000));

  PaginatedHistoryRetriever retriever(store_);

  // A cursor in the middle of the history: the older page is non-empty, so
  // the continuation token must be filled.
  chirp::chat::PaginatedHistoryRetriever::PageToken token;
  token.cursor = "ch";
  token.timestamp = 3000;
  token.page_size = 2;
  ASSERT_TRUE(token.IsValid());

  auto next = retriever.GetNextPage(token);
  // The hot tier serves the newest page_size slice below the cursor, so only
  // m2 (2000 < 3000) comes back -- but the older page is non-empty, which is
  // what fills the continuation token.
  ASSERT_EQ(next.messages.size(), 1u);
  EXPECT_EQ(next.messages[0].message_id, "m2");
  EXPECT_TRUE(next.has_more);
  EXPECT_EQ(next.next_page.cursor, "ch");
  EXPECT_EQ(next.next_page.timestamp, 2000);
  EXPECT_EQ(next.next_page.page_size, 2);
}

TEST_F(PaginatedRetrieverTest, AfterAndTimeRangeStopAtPageSizeCap) {
  ASSERT_TRUE(store_->Initialize());
  store_->StoreMessage(MakeMessage("m1", "ch", 1000));
  store_->StoreMessage(MakeMessage("m2", "ch", 2000));
  store_->StoreMessage(MakeMessage("m3", "ch", 3000));

  PaginatedHistoryRetriever retriever(store_);

  // Three candidates but page_size 2: the filter loop must stop at the cap.
  auto after = retriever.GetPageAfter("ch", 0, 500, 2);
  ASSERT_EQ(after.messages.size(), 2u);
  EXPECT_EQ(after.messages[0].message_id, "m1");
  EXPECT_EQ(after.messages[1].message_id, "m2");

  auto ranged = retriever.GetTimeRange("ch", 0, 1000, 3000, 2);
  ASSERT_EQ(ranged.size(), 2u);
  EXPECT_EQ(ranged[1].message_id, "m2");
}


// ---------------------------------------------------------------------------
// Batch D: hybrid delivery parse arms + paginated token/time + D0
// ---------------------------------------------------------------------------

TEST_F(MySqlStoreTest, GetUnreadCountRejectsEmptyCell) {
  // rows[0].empty() is the third arm of `rows.empty() || rows[0].empty() ||
  // rows[0][0] == "NULL"`.
  auto pool = std::make_shared<MySQLConnectionPool>(1, "h", 3306, "db", "u", "p");
  MySQLMessageStore store(pool);
  fake_mysql::PushRows({{}});
  EXPECT_EQ(store.GetUnreadCount("u1"), 0);
  // And the explicit "NULL" string arm (cell present but nullopt-mapped).
  fake_mysql::PushRows({{"NULL"}});
  EXPECT_EQ(store.GetUnreadCount("u1"), 0);
}

TEST_F(MySqlStoreTest, D0DestroysThroughBasePointer) {
  // `unique_ptr<MessageStore>` reset runs MySQLMessageStore's virtual dtor
  // (D0) — the function-gap arm for mysql_message_store.h:95.
  auto pool = std::make_shared<MySQLConnectionPool>(1, "h", 3306, "db", "u", "p");
  std::unique_ptr<chirp::chat::MessageStore> store =
      std::make_unique<MySQLMessageStore>(pool);
  EXPECT_TRUE(store->Initialize());
  store.reset();
  SUCCEED();
}

TEST_F(HybridStoreTest, GetDeliveryStatusParsesColonlessValue) {
  // A stored value with no ':' skips the whole parse block (arm of
  // colon1 != npos is false) and returns default-typed DeliveryInfo.
  ASSERT_TRUE(store_->Initialize());
  // TrackMessage writes "status:created_at"; overwrite with a raw value via
  // the public path is unavailable, so use a message id that yields a key
  // we can… no: use redis Get path by failing Parse? Instead: TrackMessage
  // then FailMessage with empty error still has colons. Write directly through
  // the store's redis client.
  auto redis = store_->GetRedisClient();
  ASSERT_NE(redis, nullptr);
  const std::string key = "chirp:chat:delivery:colonless:r1";
  ASSERT_TRUE(redis->SetEx(key, "nocolon", 60));
  auto status = store_->GetDeliveryStatus("colonless", "r1");
  ASSERT_TRUE(status.has_value());
  EXPECT_EQ(status->message_id, "colonless");
  EXPECT_EQ(status->receiver_id, "r1");
}

TEST_F(HybridStoreTest, GetPendingDeliveriesSkipsMalformedEntries) {
  // Entries with zero or one colon fail the two-colon parse and are skipped.
  ASSERT_TRUE(store_->Initialize());
  auto redis = store_->GetRedisClient();
  ASSERT_NE(redis, nullptr);
  // Seed pending list with: no colon, one colon, a non-numeric expiry, and
  // a well-formed entry. PendingDeliveryKey is private; TrackMessage always
  // writes well-formed entries. Push malformed ones through the same key the
  // tracker uses by tracking one real message first, then… we need the key.
  // Use RPush on the documented constant from DeliveryKey pattern.
  ASSERT_TRUE(redis->RPush("chirp:chat:pending_delivery", "nocolon"));
  ASSERT_TRUE(redis->RPush("chirp:chat:pending_delivery", "one:colon"));
  ASSERT_TRUE(redis->RPush("chirp:chat:pending_delivery", "m2:r2:not-a-number"));
  // Trailing colon: the expiry slice is empty, exercising ParseI64's
  // empty-string arm (a distinct failure from non-numeric text).
  ASSERT_TRUE(redis->RPush("chirp:chat:pending_delivery", "m3:r3:"));
  ASSERT_TRUE(redis->RPush("chirp:chat:pending_delivery", "m1:r1:100"));
  auto due = store_->GetPendingDeliveries(1000);
  ASSERT_EQ(due.size(), 1u);
  EXPECT_EQ(due[0].message_id, "m1");
  EXPECT_EQ(due[0].receiver_id, "r1");
}

TEST_F(HybridStoreTest, GetPendingDeliveriesParsesNpcReceiverEntries) {
  // Regression: receiver ids may contain colons (NPC receivers are
  // "npc:<name>"), so TrackMessage writes four-segment entries like
  // "m1:npc:blacksmith_01:<expires>". The old first/second-colon split fed
  // "blacksmith_01:<expires>" to std::stoll and the unhandled throw aborted
  // the whole chat process on the delivery-tracker strand.
  ASSERT_TRUE(store_->Initialize());
  auto redis = store_->GetRedisClient();
  ASSERT_NE(redis, nullptr);
  ASSERT_TRUE(redis->RPush("chirp:chat:pending_delivery",
                           "m1:npc:blacksmith_01:100"));
  auto due = store_->GetPendingDeliveries(1000);
  ASSERT_EQ(due.size(), 1u);
  EXPECT_EQ(due[0].message_id, "m1");
  EXPECT_EQ(due[0].receiver_id, "npc:blacksmith_01");
}

TEST_F(HybridStoreTest, FailMessageCarriesLongLastError) {
  // Exercise the third-colon parse (last_error = substr(colon2+1)) with a
  // multi-segment error so colon2 lands after the timestamp.
  ASSERT_TRUE(store_->Initialize());
  const std::string long_err(80, 'e');
  EXPECT_TRUE(store_->FailMessage("mX", "r1", long_err));
  auto status = store_->GetDeliveryStatus("mX", "r1");
  ASSERT_TRUE(status.has_value());
  EXPECT_EQ(status->status, chirp::chat::DeliveryState::kFailed);
  EXPECT_EQ(status->last_error, long_err);
}

TEST_F(PaginatedRetrieverTest, DeserializeSinglePipeYieldsEmptyCursor) {
  // pos2 == npos: the compound `pos1 != npos && pos2 != npos` fails on the
  // second arm; the token stays default (cursor empty → !IsValid).
  auto partial = chirp::chat::PaginatedHistoryRetriever::PageToken::Deserialize("a|b");
  EXPECT_FALSE(partial.IsValid());
  EXPECT_TRUE(partial.cursor.empty());
  // And the no-pipe arm (already covered by "garbage").
}

TEST_F(PaginatedRetrieverTest, GetTimeRangeOutsideAllMessagesIsEmpty) {
  ASSERT_TRUE(store_->Initialize());
  store_->StoreMessage(MakeMessage("m1", "ch", 1000));
  store_->StoreMessage(MakeMessage("m2", "ch", 2000));

  PaginatedHistoryRetriever retriever(store_);
  // Range entirely after every message: both compare arms fail every row.
  EXPECT_TRUE(retriever.GetTimeRange("ch", 0, 5000, 6000, 10).empty());
  // Range entirely before every message.
  EXPECT_TRUE(retriever.GetTimeRange("ch", 0, 1, 10, 10).empty());
}

TEST_F(PaginatedRetrieverTest, GetTimeRangeUsesInclusiveBoundaries) {
  ASSERT_TRUE(store_->Initialize());
  store_->StoreMessage(MakeMessage("m1", "ch", 1000));
  store_->StoreMessage(MakeMessage("m2", "ch", 2000));
  store_->StoreMessage(MakeMessage("m3", "ch", 3000));

  PaginatedHistoryRetriever retriever(store_);
  // Inclusive start: the row exactly at start_timestamp is kept; the row
  // before it is the only miss on `>= start`.
  auto ranged = retriever.GetTimeRange("ch", 0, 2000, 6000, 10);
  ASSERT_EQ(ranged.size(), 2u);
  EXPECT_EQ(ranged[0].message_id, "m2");
  EXPECT_EQ(ranged[1].message_id, "m3");

  // end_timestamp <= 0 disables GetHistory's before_timestamp pre-filter, so
  // every row reaches line 171. Then `ts >= start && ts <= end` takes the
  // true/false arm (row after start but after end=0).
  auto past_end = retriever.GetTimeRange("ch", 0, 1500, 0, 10);
  EXPECT_TRUE(past_end.empty());
}

}  // namespace
