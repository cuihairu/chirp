// SearchServer 协议面单测（message_search 批）：SERVER_AUTH 信任门、LOGIN
// 身份（脚手架 + JWT 两种形态）、SEARCH_MESSAGE 的鉴权/参数/核对/自愈路径。
// 传输侧用记录式 FakeSession（解码回 Packet），MySQL 侧用 fake 脚本行。
#include <gtest/gtest.h>

#include <cstdint>
#include <ctime>
#include <memory>
#include <optional>
#include <string>
#include <vector>

#include <unistd.h>

#include <asio.hpp>
#include <sqlite3.h>

#include "fake_mysql.h"
#include "index_sync.h"
#include "jwt.h"
#include "logger.h"
#include "message_search_index.h"
#include "mysql_message_store.h"
#include "network/protobuf_framing.h"
#include "network/session.h"
#include "proto/auth.pb.h"
#include "proto/chat.pb.h"
#include "proto/common.pb.h"
#include "proto/gateway.pb.h"
#include "proto/game_server_gateway.pb.h"
#include "search_server.h"

namespace {

using chirp::search::MessageIndexSync;
using chirp::search::MessageSearchIndex;
using chirp::search::SearchServer;
using chirp::chat::MySQLConnectionPool;
namespace fake_mysql = chirp_test::fake_mysql;

// 记录式会话：解码收到的帧回 Packet（与 4 字节长度前缀约定一致）。
class FakeSession : public chirp::network::Session {
 public:
  void Send(std::string bytes) override {
    if (bytes.size() < 4) {
      ++undecodable;
      return;
    }
    const auto u8 = [&bytes](size_t i) {
      return static_cast<uint32_t>(static_cast<unsigned char>(bytes[i]));
    };
    const uint32_t len = (u8(0) << 24) | (u8(1) << 16) | (u8(2) << 8) | u8(3);
    if (len + 4 != bytes.size()) {
      ++undecodable;
      return;
    }
    chirp::gateway::Packet pkt;
    if (chirp::network::ProtobufFraming::Decode(bytes.substr(4), &pkt)) {
      sent.push_back(std::move(pkt));
    } else {
      ++undecodable;
    }
  }
  void SendAndClose(std::string bytes) override {
    Send(std::move(bytes));
    closed = true;
  }
  void Close() override { closed = true; }
  bool IsClosed() const override { return closed; }
  std::string RemoteAddress() const override { return "127.0.0.1:0"; }

  std::vector<chirp::gateway::Packet> sent;
  bool closed = false;
  int undecodable = 0;
};

class SearchServerTest : public ::testing::Test {
 protected:
  void SetUp() override {
    fake_mysql::Reset();
    path_ = "/tmp/chirp_search_server_test_" +
            std::to_string(static_cast<long>(::getpid())) + ".db";
    std::remove(path_.c_str());
    std::remove((path_ + "-wal").c_str());
    std::remove((path_ + "-shm").c_str());
    ASSERT_TRUE(index_.Open(path_, &err_)) << err_;
    pool_ = std::make_unique<MySQLConnectionPool>(1, "h", 3306, "db", "u", "p");
    sync_ = std::make_unique<MessageIndexSync>(index_, *pool_);
  }

  void TearDown() override {
    session_.reset();
    server_.reset();
    sync_.reset();
    pool_.reset();
    std::remove(path_.c_str());
    std::remove((path_ + "-wal").c_str());
    std::remove((path_ + "-shm").c_str());
  }

  // 建一个带 secret 的服务端（大多数用例的默认形态）。
  void MakeServer(const std::string& token_secret = "") {
    SearchServer::Options options;
    options.service_secret = "svc_secret";
    options.token_secret = token_secret;
    session_ = std::make_shared<FakeSession>();
    server_ = std::make_unique<SearchServer>(index_, sync_.get(), std::move(options));
  }

  void Frame(chirp::gateway::MsgID msg_id, int64_t seq, const std::string& body) {
    chirp::gateway::Packet pkt;
    pkt.set_msg_id(msg_id);
    pkt.set_sequence(seq);
    pkt.set_body(body);
    const auto framed = chirp::network::ProtobufFraming::Encode(pkt);
    // Encode 输出自带 4 字节长度前缀；TcpSession 在回调前剥掉它，这里同样
    // 只把包体交给 HandleFrame。
    const std::string wire(reinterpret_cast<const char*>(framed.data()), framed.size());
    server_->HandleFrame(session_, wire.substr(4));
  }

  void DoServerAuth(const std::string& secret) {
    chirp::game_server_gateway::ServerAuthRequest req;
    req.set_service_id("app_sdk_gateway");
    req.set_secret(secret);
    req.set_protocol_version(1);
    Frame(chirp::gateway::SERVER_AUTH_REQ, 1, req.SerializeAsString());
  }

  void DoLogin(const std::string& token) {
    chirp::auth::LoginRequest req;
    req.set_token(token);
    req.set_device_id("dev");
    Frame(chirp::gateway::LOGIN_REQ, 2, req.SerializeAsString());
  }

  void DoSearch(const std::string& keyword, const std::string& channel_id = "") {
    chirp::chat::SearchMessageRequest req;
    req.set_keyword(keyword);
    if (!channel_id.empty()) {
      req.set_channel_id(channel_id);
    }
    req.set_limit(10);
    Frame(chirp::gateway::SEARCH_MESSAGE_REQ, 3, req.SerializeAsString());
  }

  const chirp::gateway::Packet* LastResp(chirp::gateway::MsgID msg_id) const {
    for (auto it = session_->sent.rbegin(); it != session_->sent.rend(); ++it) {
      if (it->msg_id() == msg_id) {
        return &*it;
      }
    }
    return nullptr;
  }

  // 一行 FetchMessageFacts 的脚本数据（message_id, sender_id, sender_kind,
  // receiver_id, is_recalled, content）。
  static std::vector<std::optional<std::string>> FactRow(const char* message_id,
                                                         const char* sender,
                                                         const char* recalled,
                                                         const char* content) {
    return {std::optional<std::string>(message_id), std::optional<std::string>(sender),
            std::optional<std::string>("0"), std::optional<std::string>(),
            std::optional<std::string>(recalled), std::optional<std::string>(content)};
  }

  void Index(const std::string& id, const std::string& channel, int channel_type,
             int64_t ts, const std::string& content) {
    MessageSearchIndex::Input in;
    in.message_id = id;
    in.channel_id = channel;
    in.channel_type = channel_type;
    in.msg_type = 1;
    in.timestamp = ts;
    in.content = content;
    ASSERT_TRUE(index_.IndexMessage(in, &err_)) << err_;
  }

  // 对索引库文件做一次原始 SQL（第二连接）：模拟外部改动——检索仍读 FTS
  // 虚表，而自愈删除引用的映射表已不在。
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
  std::unique_ptr<MySQLConnectionPool> pool_;
  std::unique_ptr<MessageIndexSync> sync_;
  std::unique_ptr<SearchServer> server_;
  std::shared_ptr<FakeSession> session_;
};

TEST_F(SearchServerTest, UnauthenticatedSearchIsRejected) {
  MakeServer();
  DoSearch("hello");
  const auto* resp = LastResp(chirp::gateway::SEARCH_MESSAGE_RESP);
  ASSERT_NE(resp, nullptr);
  chirp::chat::SearchMessageResponse body;
  ASSERT_TRUE(body.ParseFromString(resp->body()));
  EXPECT_EQ(body.code(), chirp::common::AUTH_FAILED);
  EXPECT_EQ(body.matches_size(), 0);
}

TEST_F(SearchServerTest, ServerAuthWrongSecretRejectedAndClosed) {
  MakeServer();
  DoServerAuth("wrong");
  const auto* resp = LastResp(chirp::gateway::SERVER_AUTH_RESP);
  ASSERT_NE(resp, nullptr);
  chirp::game_server_gateway::ServerAuthResponse body;
  ASSERT_TRUE(body.ParseFromString(resp->body()));
  EXPECT_EQ(body.code(), chirp::common::AUTH_FAILED);
  EXPECT_TRUE(session_->closed);
  EXPECT_EQ(server_->trusted_count(), 0u);
}

TEST_F(SearchServerTest, ServerAuthOkMarksTrusted) {
  MakeServer();
  DoServerAuth("svc_secret");
  const auto* resp = LastResp(chirp::gateway::SERVER_AUTH_RESP);
  ASSERT_NE(resp, nullptr);
  chirp::game_server_gateway::ServerAuthResponse body;
  ASSERT_TRUE(body.ParseFromString(resp->body()));
  EXPECT_EQ(body.code(), chirp::common::OK);
  EXPECT_EQ(server_->trusted_count(), 1u);
  EXPECT_FALSE(session_->closed);
}

TEST_F(SearchServerTest, ServerAuthIgnoredWithoutSecret) {
  // 无 secret 形态：帧被忽略（直连调试入口），不回包不信任。
  SearchServer::Options options;
  session_ = std::make_shared<FakeSession>();
  server_ = std::make_unique<SearchServer>(index_, sync_.get(), std::move(options));
  DoServerAuth("anything");
  EXPECT_EQ(session_->sent.size(), 0u);
  EXPECT_EQ(server_->trusted_count(), 0u);
}

TEST_F(SearchServerTest, LoginScaffoldAndIdentity) {
  MakeServer();
  DoLogin("u1");
  const auto* resp = LastResp(chirp::gateway::LOGIN_RESP);
  ASSERT_NE(resp, nullptr);
  chirp::auth::LoginResponse body;
  ASSERT_TRUE(body.ParseFromString(resp->body()));
  EXPECT_EQ(body.code(), chirp::common::OK);
  EXPECT_EQ(body.user_id(), "u1");
  EXPECT_EQ(server_->authenticated_count(), 1u);

  // 登录后检索可用（无 MySQL 脚本行 → 空事实 → 空结果但 OK）。
  DoSearch("hello");
  const auto* search_resp = LastResp(chirp::gateway::SEARCH_MESSAGE_RESP);
  ASSERT_NE(search_resp, nullptr);
  chirp::chat::SearchMessageResponse search_body;
  ASSERT_TRUE(search_body.ParseFromString(search_resp->body()));
  EXPECT_EQ(search_body.code(), chirp::common::OK);
  EXPECT_EQ(search_body.matches_size(), 0);
}

TEST_F(SearchServerTest, CloseClearsIdentity) {
  MakeServer();
  DoLogin("u1");
  EXPECT_EQ(server_->authenticated_count(), 1u);
  server_->HandleClose(session_);
  EXPECT_EQ(server_->authenticated_count(), 0u);
  DoSearch("hello");
  const auto* resp = LastResp(chirp::gateway::SEARCH_MESSAGE_RESP);
  ASSERT_NE(resp, nullptr);
  chirp::chat::SearchMessageResponse body;
  ASSERT_TRUE(body.ParseFromString(resp->body()));
  EXPECT_EQ(body.code(), chirp::common::AUTH_FAILED);
}

TEST_F(SearchServerTest, SearchRejectsEmptyKeywordAndGarbageBody) {
  MakeServer();
  DoLogin("u1");
  // 纯标点：无 token 字符 → INVALID_PARAM
  DoSearch("。。。");
  const auto* resp = LastResp(chirp::gateway::SEARCH_MESSAGE_RESP);
  ASSERT_NE(resp, nullptr);
  chirp::chat::SearchMessageResponse body;
  ASSERT_TRUE(body.ParseFromString(resp->body()));
  EXPECT_EQ(body.code(), chirp::common::INVALID_PARAM);

  // 无法解析的 body 同样 INVALID_PARAM。
  Frame(chirp::gateway::SEARCH_MESSAGE_REQ, 4, "\x01\x02garbage");
  const auto* bad = LastResp(chirp::gateway::SEARCH_MESSAGE_RESP);
  ASSERT_NE(bad, nullptr);
  chirp::chat::SearchMessageResponse bad_body;
  ASSERT_TRUE(bad_body.ParseFromString(bad->body()));
  EXPECT_EQ(bad_body.code(), chirp::common::INVALID_PARAM);
}

TEST_F(SearchServerTest, SearchMergesAuthoritativeFacts) {
  MakeServer();
  // 两条命中：world 频道 + u1/u2 私聊；检索者 u1 两条都可见。
  Index("m1", "world", 1, 100, "hello world");
  Index("m2", "u1|u2", 0, 101, "hello again");
  fake_mysql::PushRows({FactRow("m1", "u1", "0", "hello world"),
                        FactRow("m2", "u2", "0", "hello again")});
  DoLogin("u1");
  DoSearch("hello");

  const auto* resp = LastResp(chirp::gateway::SEARCH_MESSAGE_RESP);
  ASSERT_NE(resp, nullptr);
  EXPECT_EQ(resp->sequence(), 3);  // 应答回显请求序号
  chirp::chat::SearchMessageResponse body;
  ASSERT_TRUE(body.ParseFromString(resp->body()));
  EXPECT_EQ(body.code(), chirp::common::OK);
  ASSERT_EQ(body.matches_size(), 2);
  // 时间降序：m2（101）在前。
  EXPECT_EQ(body.matches(0).message_id(), "m2");
  EXPECT_EQ(body.matches(0).channel_id(), "u1|u2");
  EXPECT_EQ(body.matches(0).channel_type(), chirp::chat::ChannelType::PRIVATE);
  EXPECT_EQ(body.matches(0).sender_id(), "u2");
  EXPECT_EQ(body.matches(0).content(), "hello again");
  EXPECT_EQ(body.matches(1).message_id(), "m1");
  EXPECT_EQ(body.matches(1).sender_id(), "u1");
  EXPECT_FALSE(body.has_more());
}

TEST_F(SearchServerTest, PrivateChannelHiddenFromOutsider) {
  MakeServer();
  Index("m1", "world", 1, 100, "hello world");
  Index("m2", "u1|u2", 0, 101, "hello again");
  fake_mysql::PushRows({FactRow("m1", "u1", "0", "hello world"),
                        FactRow("m2", "u2", "0", "hello again")});
  DoLogin("u3");
  DoSearch("hello");

  const auto* resp = LastResp(chirp::gateway::SEARCH_MESSAGE_RESP);
  ASSERT_NE(resp, nullptr);
  chirp::chat::SearchMessageResponse body;
  ASSERT_TRUE(body.ParseFromString(resp->body()));
  EXPECT_EQ(body.code(), chirp::common::OK);
  ASSERT_EQ(body.matches_size(), 1);
  EXPECT_EQ(body.matches(0).message_id(), "m1");
}

TEST_F(SearchServerTest, InternalPaginationSkipsFullyFilteredPages) {
  // 首页原始命中全是别人私聊（对 u1 全不可见）：游标按「返回的命中」推进，
  // 若服务端不内部翻页，客户端拿不到任何游标会原地打转。这里 limit=2、
  // 原始页大小 limit+1=3——第一页 3 条命中里只有 1 条可见，服务端必须继续
  // 扫第二页攒满 2 条可见命中。
  MakeServer();
  Index("f1", "u4|u5", 0, 103, "hello secret");
  Index("f2", "u4|u5", 0, 102, "hello secret");
  Index("w1", "world", 1, 101, "hello world");
  Index("w2", "world", 1, 100, "hello world");
  // 第一页原始命中（f1,f2,w1）的事实；第二页（w2）的事实。
  fake_mysql::PushRows({FactRow("f1", "u4", "0", "hello secret"),
                        FactRow("f2", "u4", "0", "hello secret"),
                        FactRow("w1", "u1", "0", "hello world")});
  fake_mysql::PushRows({FactRow("w2", "u1", "0", "hello world")});

  chirp::auth::LoginRequest login;
  login.set_token("u1");
  Frame(chirp::gateway::LOGIN_REQ, 2, login.SerializeAsString());
  chirp::chat::SearchMessageRequest req;
  req.set_keyword("hello");
  req.set_limit(2);
  Frame(chirp::gateway::SEARCH_MESSAGE_REQ, 3, req.SerializeAsString());

  const auto* resp = LastResp(chirp::gateway::SEARCH_MESSAGE_RESP);
  ASSERT_NE(resp, nullptr);
  chirp::chat::SearchMessageResponse body;
  ASSERT_TRUE(body.ParseFromString(resp->body()));
  EXPECT_EQ(body.code(), chirp::common::OK);
  ASSERT_EQ(body.matches_size(), 2);
  EXPECT_EQ(body.matches(0).message_id(), "w1");
  EXPECT_EQ(body.matches(1).message_id(), "w2");
  // 原始命中扫尽：尽管曾经过滤，has_more 仍为 false。
  EXPECT_FALSE(body.has_more());
}

TEST_F(SearchServerTest, RecalledHitsExcludedAndSelfHealed) {
  MakeServer();
  Index("m1", "world", 1, 100, "hello world");
  fake_mysql::PushRows({FactRow("m1", "u1", "1", "hello world")});
  DoLogin("u1");
  DoSearch("hello");

  const auto* resp = LastResp(chirp::gateway::SEARCH_MESSAGE_RESP);
  ASSERT_NE(resp, nullptr);
  chirp::chat::SearchMessageResponse body;
  ASSERT_TRUE(body.ParseFromString(resp->body()));
  EXPECT_EQ(body.code(), chirp::common::OK);
  EXPECT_EQ(body.matches_size(), 0);
  // 自愈：撤回的命中已从索引删除（后续查询不再扫到它）。
  EXPECT_EQ(index_.DocumentCount(), 0);
}

TEST_F(SearchServerTest, StaleHitWithoutFactRowSelfHeals) {
  MakeServer();
  Index("m1", "world", 1, 100, "hello world");
  // 脚本行空：库中已无该消息（被物理删除/清库）→ 剔除 + 自愈删索引。
  fake_mysql::PushRows({});
  DoLogin("u1");
  DoSearch("hello");

  const auto* resp = LastResp(chirp::gateway::SEARCH_MESSAGE_RESP);
  ASSERT_NE(resp, nullptr);
  chirp::chat::SearchMessageResponse body;
  ASSERT_TRUE(body.ParseFromString(resp->body()));
  EXPECT_EQ(body.code(), chirp::common::OK);
  EXPECT_EQ(body.matches_size(), 0);
  EXPECT_EQ(index_.DocumentCount(), 0);
}

TEST_F(SearchServerTest, SelfHealWarnsWithoutBreakingResponse) {
  // 库被外部改动（映射表没了）：自愈删除失败只记 WARN，应答照常给
  // （剔命中、code=OK），文档留在索引里等下一轮核对。
  MakeServer();
  Index("m1", "world", 1, 100, "hello world");
  RawExec("DROP TABLE message_map");
  fake_mysql::PushRows({});  // 陈旧命中：库中无事实行
  DoLogin("u1");
  DoSearch("hello");

  const auto* resp = LastResp(chirp::gateway::SEARCH_MESSAGE_RESP);
  ASSERT_NE(resp, nullptr);
  chirp::chat::SearchMessageResponse body;
  ASSERT_TRUE(body.ParseFromString(resp->body()));
  // code=OK + 0 命中 = FTS 扫到过命中（库篡改不影响 fts 读取）、事实核对
  // 走到了剔除臂；索引侧删除失败只降级为 WARN（映射表已被删，DocumentCount
  // 此处只会走 fail-soft 臂返回 0，不做文档存留断言）。
  EXPECT_EQ(body.code(), chirp::common::OK);
  EXPECT_EQ(body.matches_size(), 0);
}

TEST_F(SearchServerTest, RecalledSelfHealWarnsWithoutBreakingResponse) {
  // 同上，走撤回臂：事实行 is_recalled=1 → 自愈删除失败 → WARN + 照常应答。
  MakeServer();
  Index("m1", "world", 1, 100, "hello world");
  RawExec("DROP TABLE message_map");
  fake_mysql::PushRows({FactRow("m1", "alice", "1", "hello world")});
  DoLogin("u1");
  DoSearch("hello");

  const auto* resp = LastResp(chirp::gateway::SEARCH_MESSAGE_RESP);
  ASSERT_NE(resp, nullptr);
  chirp::chat::SearchMessageResponse body;
  ASSERT_TRUE(body.ParseFromString(resp->body()));
  EXPECT_EQ(body.code(), chirp::common::OK);
  EXPECT_EQ(body.matches_size(), 0);
}

TEST_F(SearchServerTest, SearchHonorsContentTypeFilter) {
  MakeServer();
  Index("m1", "world", 1, 100, "alpha");
  // m2 同词但 msg_type=2：类型过滤的判别样本。
  MessageSearchIndex::Input typed = {"m2", "world", 1, /*msg_type=*/2, 101, "alpha"};
  ASSERT_TRUE(index_.IndexMessage(typed, &err_)) << err_;
  fake_mysql::PushRows({FactRow("m2", "alice", "0", "alpha")});
  DoLogin("u1");

  chirp::chat::SearchMessageRequest req;
  req.set_keyword("alpha");
  req.set_limit(10);
  req.add_content_types(2);
  Frame(chirp::gateway::SEARCH_MESSAGE_REQ, 3, req.SerializeAsString());

  const auto* resp = LastResp(chirp::gateway::SEARCH_MESSAGE_RESP);
  ASSERT_NE(resp, nullptr);
  chirp::chat::SearchMessageResponse body;
  ASSERT_TRUE(body.ParseFromString(resp->body()));
  EXPECT_EQ(body.code(), chirp::common::OK);
  ASSERT_EQ(body.matches_size(), 1);
  EXPECT_EQ(body.matches(0).message_id(), "m2");
  EXPECT_EQ(body.matches(0).msg_type(), 2);
}

TEST_F(SearchServerTest, MysqlDownFailsClosed) {
  MakeServer();
  Index("m1", "world", 1, 100, "hello world");
  // 排空池里构造期预建的连接，让下一次取连接吃 SetConnectShouldFail。
  pool_->GetConnection();
  fake_mysql::SetConnectShouldFail(true);
  DoLogin("u1");
  DoSearch("hello");

  const auto* resp = LastResp(chirp::gateway::SEARCH_MESSAGE_RESP);
  ASSERT_NE(resp, nullptr);
  chirp::chat::SearchMessageResponse body;
  ASSERT_TRUE(body.ParseFromString(resp->body()));
  // 索引可能滞后：MySQL 不可用宁给错误不给错结果。
  EXPECT_EQ(body.code(), chirp::common::INTERNAL_ERROR);
  EXPECT_EQ(body.matches_size(), 0);
}

TEST_F(SearchServerTest, NoSyncFailsClosed) {
  // 无 MySQL 形态（sync=nullptr）：检索一律失败关闭。
  SearchServer::Options options;
  options.service_secret = "svc_secret";
  session_ = std::make_shared<FakeSession>();
  server_ = std::make_unique<SearchServer>(index_, nullptr, std::move(options));
  DoLogin("u1");
  DoSearch("hello");
  const auto* resp = LastResp(chirp::gateway::SEARCH_MESSAGE_RESP);
  ASSERT_NE(resp, nullptr);
  chirp::chat::SearchMessageResponse body;
  ASSERT_TRUE(body.ParseFromString(resp->body()));
  EXPECT_EQ(body.code(), chirp::common::INTERNAL_ERROR);
}

TEST_F(SearchServerTest, JwtModeRejectsScaffoldTokenAndAcceptsJwt) {
  MakeServer("jwt_shared_secret");
  DoLogin("u1");  // 非 JWT：拒绝
  const auto* deny = LastResp(chirp::gateway::LOGIN_RESP);
  ASSERT_NE(deny, nullptr);
  chirp::auth::LoginResponse deny_body;
  ASSERT_TRUE(deny_body.ParseFromString(deny->body()));
  EXPECT_EQ(deny_body.code(), chirp::common::AUTH_FAILED);

  // 有效 HS256 JWT（同一 secret 签发、未过期）：接受。
  const int64_t now = static_cast<int64_t>(::time(nullptr));
  DoLogin(chirp::common::JwtSignHS256("u1", now, "jwt_shared_secret", now + 3600));
  const auto* ok = LastResp(chirp::gateway::LOGIN_RESP);
  ASSERT_NE(ok, nullptr);
  chirp::auth::LoginResponse ok_body;
  ASSERT_TRUE(ok_body.ParseFromString(ok->body()));
  EXPECT_EQ(ok_body.code(), chirp::common::OK);
  EXPECT_EQ(ok_body.user_id(), "u1");
}

TEST(SearchServerGarbageFrame, UnparseableFrameIgnored) {
  SearchServer::Options options;
  MessageSearchIndex index;  // 未 Open：不应被触碰
  auto session = std::make_shared<FakeSession>();
  SearchServer server(index, nullptr, std::move(options));
  server.HandleFrame(session, "this is not protobuf");
  EXPECT_EQ(session->sent.size(), 0u);
}

TEST_F(SearchServerTest, ServerAuthGarbageBodyRejectedAndClosed) {
  MakeServer();
  Frame(chirp::gateway::SERVER_AUTH_REQ, 1, "\x09garbage");
  const auto* resp = LastResp(chirp::gateway::SERVER_AUTH_RESP);
  ASSERT_NE(resp, nullptr);
  chirp::game_server_gateway::ServerAuthResponse body;
  ASSERT_TRUE(body.ParseFromString(resp->body()));
  EXPECT_EQ(body.code(), chirp::common::AUTH_FAILED);
  EXPECT_TRUE(session_->closed);
}

TEST_F(SearchServerTest, LoginGarbageBodyInvalidParam) {
  MakeServer();
  Frame(chirp::gateway::LOGIN_REQ, 2, "\x09garbage");
  const auto* resp = LastResp(chirp::gateway::LOGIN_RESP);
  ASSERT_NE(resp, nullptr);
  chirp::auth::LoginResponse body;
  ASSERT_TRUE(body.ParseFromString(resp->body()));
  EXPECT_EQ(body.code(), chirp::common::INVALID_PARAM);
}

TEST_F(SearchServerTest, LoginEmptyTokenInvalidParam) {
  MakeServer();
  DoLogin("");  // 脚手架形态：空 token = 空 user_id
  const auto* resp = LastResp(chirp::gateway::LOGIN_RESP);
  ASSERT_NE(resp, nullptr);
  chirp::auth::LoginResponse body;
  ASSERT_TRUE(body.ParseFromString(resp->body()));
  EXPECT_EQ(body.code(), chirp::common::INVALID_PARAM);
  EXPECT_EQ(server_->authenticated_count(), 0u);
}

TEST_F(SearchServerTest, UnknownMsgIdIgnored) {
  MakeServer();
  // 次级管道只服务检索面：不相关的 2xxx 一律忽略不回包。
  Frame(chirp::gateway::GET_HISTORY_REQ, 5, "");
  EXPECT_EQ(session_->sent.size(), 0u);
}

TEST_F(SearchServerTest, LimitClampedToCap) {
  MakeServer();
  // 51 条可见命中：limit=1000 被钳到 50，第 51 条留在下一页。
  std::vector<std::vector<std::optional<std::string>>> fact_rows;
  for (int i = 0; i < 51; ++i) {
    const std::string id = "m" + std::to_string(i);
    Index(id, "world", 1, 100 + i, "clamp me");
    fact_rows.push_back(FactRow(id.c_str(), "u1", "0", "clamp me"));
  }
  // 一页（limit+1=51 条原始命中）的事实一次取齐；攒满 50 即停。
  fake_mysql::PushRows(std::move(fact_rows));
  DoLogin("u1");
  chirp::chat::SearchMessageRequest req;
  req.set_keyword("clamp");
  req.set_limit(1000);
  Frame(chirp::gateway::SEARCH_MESSAGE_REQ, 3, req.SerializeAsString());

  const auto* resp = LastResp(chirp::gateway::SEARCH_MESSAGE_RESP);
  ASSERT_NE(resp, nullptr);
  EXPECT_EQ(resp->sequence(), 3);
  chirp::chat::SearchMessageResponse body;
  ASSERT_TRUE(body.ParseFromString(resp->body()));
  EXPECT_EQ(body.code(), chirp::common::OK);
  EXPECT_EQ(body.matches_size(), 50);
  EXPECT_TRUE(body.has_more());
}

TEST_F(SearchServerTest, SearchErrorPathFailsClosed) {
  // 索引未打开：Search 报错 → INTERNAL_ERROR（不是空结果）。
  MessageSearchIndex closed_index;  // 未 Open
  SearchServer::Options options;
  options.service_secret = "svc_secret";
  session_ = std::make_shared<FakeSession>();
  server_ = std::make_unique<SearchServer>(closed_index, sync_.get(), std::move(options));
  DoLogin("u1");
  DoSearch("hello");
  const auto* resp = LastResp(chirp::gateway::SEARCH_MESSAGE_RESP);
  ASSERT_NE(resp, nullptr);
  chirp::chat::SearchMessageResponse body;
  ASSERT_TRUE(body.ParseFromString(resp->body()));
  EXPECT_EQ(body.code(), chirp::common::INTERNAL_ERROR);
}

}  // namespace
