#include <gtest/gtest.h>

#include <atomic>
#include <chrono>
#include <filesystem>
#include <fstream>
#include <future>
#include <string>
#include <vector>

#include "backoff.h"
#include "chirp/auth_provider.h"
#include "chirp/chat_event_listener.h"
#include "chirp/command_handler.h"
#include "chirp/file_message_store.h"
#include "chirp/message_interceptor.h"
#include "chirp/message_store.h"
#include "chirp/sdk.h"
#include "chirp/sdk_client.h"
#include "chirp/word_filter.h"

using chirp::sdk::ChatClient;
using chirp::sdk::ChatConfig;
using chirp::sdk::ChatError;
using chirp::sdk::ConnectionState;

namespace {

// Default port values keep the client idle in these tests; no TCP connection
// is ever attempted here (websocket mode fails fast, other calls are guarded
// by the connection state machine).
class SdkClientTest : public ::testing::Test {
protected:
  static constexpr int kWaitMs = 5000;

  static ChatConfig TcpConfig() {
    ChatConfig config;
    config.enable_websocket = false;
    return config;
  }

  static ChatConfig WsConfig() {
    ChatConfig config;
    config.enable_websocket = true;
    return config;
  }
};

// ---------------------------------------------------------------------------
// MemoryMessageStore 纯单元用例(离线,不建连接)。
// ---------------------------------------------------------------------------
namespace {

chirp::chat::ChatMessage MakeStoredMessage(const std::string& channel_id,
                                           int64_t ts,
                                           const std::string& content) {
  chirp::chat::ChatMessage msg;
  msg.set_message_id("m-" + std::to_string(ts));
  msg.set_sender_id("alice");
  msg.set_channel_type(chirp::chat::WORLD);
  msg.set_channel_id(channel_id);
  msg.set_content(content);
  msg.set_timestamp(ts);
  return msg;
}

}  // namespace

TEST(MemoryMessageStoreTest, LoadReturnsNewestFirstWithLimitAndFilter) {
  chirp::sdk::MemoryMessageStore store;
  store.Save(MakeStoredMessage("world", 100, "a"));
  store.Save(MakeStoredMessage("world", 200, "b"));
  store.Save(MakeStoredMessage("world", 300, "c"));

  const auto all = store.Load(chirp::chat::WORLD, "world", 10);
  ASSERT_EQ(all.size(), 3u);
  EXPECT_EQ(all[0].content(), "c");
  EXPECT_EQ(all[1].content(), "b");
  EXPECT_EQ(all[2].content(), "a");

  const auto capped = store.Load(chirp::chat::WORLD, "world", 2);
  ASSERT_EQ(capped.size(), 2u);
  EXPECT_EQ(capped[0].content(), "c");
  EXPECT_EQ(capped[1].content(), "b");

  // before_timestamp 语义:严格早于该时间戳的消息(300 不含)。
  const auto before = store.Load(chirp::chat::WORLD, "world", 10, 300);
  ASSERT_EQ(before.size(), 2u);
  EXPECT_EQ(before[0].content(), "b");
  EXPECT_EQ(before[1].content(), "a");

  EXPECT_TRUE(store.Load(chirp::chat::WORLD, "missing", 10).empty());
  EXPECT_TRUE(store.Load(chirp::chat::WORLD, "world", 0).empty());
}

TEST(MemoryMessageStoreTest, ChannelTypeIsolatesBuckets) {
  chirp::sdk::MemoryMessageStore store;
  auto priv = MakeStoredMessage("shared-id", 100, "p");
  priv.set_channel_type(chirp::chat::PRIVATE);
  store.Save(priv);
  store.Save(MakeStoredMessage("shared-id", 101, "w"));

  const auto from_private = store.Load(chirp::chat::PRIVATE, "shared-id", 10);
  ASSERT_EQ(from_private.size(), 1u);
  EXPECT_EQ(from_private[0].content(), "p");

  const auto from_world = store.Load(chirp::chat::WORLD, "shared-id", 10);
  ASSERT_EQ(from_world.size(), 1u);
  EXPECT_EQ(from_world[0].content(), "w");
}

TEST(MemoryMessageStoreTest, EvictionKeepsNewestPerChannel) {
  chirp::sdk::MemoryMessageStore store(2);
  store.Save(MakeStoredMessage("world", 100, "old"));
  store.Save(MakeStoredMessage("world", 200, "mid"));
  store.Save(MakeStoredMessage("world", 300, "new"));

  const auto kept = store.Load(chirp::chat::WORLD, "world", 10);
  ASSERT_EQ(kept.size(), 2u);
  EXPECT_EQ(kept[0].content(), "new");
  EXPECT_EQ(kept[1].content(), "mid");
}

TEST(MemoryMessageStoreTest, ZeroMaxMeansUnbounded) {
  chirp::sdk::MemoryMessageStore store(0);
  for (int i = 0; i < 5; ++i) {
    store.Save(MakeStoredMessage("world", i, "m" + std::to_string(i)));
  }
  EXPECT_EQ(store.Load(chirp::chat::WORLD, "world", 10).size(), 5u);
}

TEST(MemoryMessageStoreTest, CleanupRemovesOlderAndPrunesBuckets) {
  chirp::sdk::MemoryMessageStore store;
  store.Save(MakeStoredMessage("world", 100, "old"));
  store.Save(MakeStoredMessage("world", 300, "keep"));
  store.Save(MakeStoredMessage("arena", 50, "only-old"));

  store.Cleanup(200);
  const auto world = store.Load(chirp::chat::WORLD, "world", 10);
  ASSERT_EQ(world.size(), 1u);
  EXPECT_EQ(world[0].content(), "keep");
  // 整桶被清掉后 Load 回空,不残留空桶。
  EXPECT_TRUE(store.Load(chirp::chat::WORLD, "arena", 10).empty());
}

TEST(MemoryMessageStoreTest, ReadTrackingDefaultsToUnreadZero) {
  chirp::sdk::MemoryMessageStore store;
  store.Save(MakeStoredMessage("world", 100, "a"));
  // 基类默认实现:不跟踪已读,计数恒 0,MarkRead 无副作用。
  store.MarkRead(chirp::chat::WORLD, "world", "m-100");
  EXPECT_EQ(store.GetUnreadCount(chirp::chat::WORLD, "world"), 0);
}

// ---------------------------------------------------------------------------
// WordFilterInterceptor 纯单元用例(离线,不建连接)。语义对齐服务端
// chirp::chat::WordFilter 的词库格式与替换/拒绝行为。
// ---------------------------------------------------------------------------
TEST(WordFilterInterceptorTest, ParseLexiconDropsBlanksCommentsAndDedupes) {
  const auto terms = chirp::sdk::ParseWordLexicon({
      "  Spam  ",
      "# 注释行",
      "",
      "spam",
      "  dummy\r\n",
      "论坛",
  });
  // lower + 去重 + 字典序(与<std::set> 一致)。
  ASSERT_EQ(terms.size(), 3u);
  EXPECT_EQ(terms[0], "dummy");
  EXPECT_EQ(terms[1], "spam");
  EXPECT_EQ(terms[2], "论坛");
}

TEST(WordFilterInterceptorTest, ReplaceMasksHitsAndCollapsesAdjacentRuns) {
  chirp::sdk::WordFilterOptions opts;
  opts.terms = {"bad dog"};
  chirp::sdk::WordFilterInterceptor filter(opts);

  chirp::chat::SendMessageRequest msg;
  msg.set_content("Bad DOG and bad dog");
  EXPECT_TRUE(filter.OnBeforeSend(msg));
  // 未命中区间保留原大小写,两次命中各自塌缩成一次替换。
  EXPECT_EQ(msg.content(), "** and **");
  EXPECT_EQ(filter.word_count(), 1u);
}

TEST(WordFilterInterceptorTest, ReplaceCollapsesOverlappingTermRuns) {
  chirp::sdk::WordFilterOptions opts;
  opts.terms = {"ab", "bc"};
  opts.replacement = "#";
  chirp::sdk::WordFilterInterceptor filter(opts);

  chirp::chat::SendMessageRequest msg;
  msg.set_content("abc");
  EXPECT_TRUE(filter.OnBeforeSend(msg));
  // 两个词的命中区间首尾相接,重建时塌缩成一次替换。
  EXPECT_EQ(msg.content(), "#");
}

TEST(WordFilterInterceptorTest, ReplaceKeepsUtf8BytesAroundAsciiHits) {
  chirp::sdk::WordFilterOptions opts;
  opts.terms = {"脏话", "damn"};
  chirp::sdk::WordFilterInterceptor filter(opts);

  chirp::chat::SendMessageRequest msg;
  // ASCII 词按 lower 命中;中文字节(UTF-8 多字节)两侧原样保留。
  msg.set_content("你好 damn 世界,真是脏话啊");
  EXPECT_TRUE(filter.OnBeforeSend(msg));
  EXPECT_EQ(msg.content(), "你好 ** 世界,真是**啊");
}

TEST(WordFilterInterceptorTest, RejectPolicyBlocksHitAndPassesCleanText) {
  chirp::sdk::WordFilterOptions opts;
  opts.terms = {"banned"};
  opts.policy = chirp::sdk::WordFilterPolicy::kReject;
  chirp::sdk::WordFilterInterceptor filter(opts);

  chirp::chat::SendMessageRequest hit;
  hit.set_content("totally BANNED words");
  EXPECT_FALSE(filter.OnBeforeSend(hit));

  chirp::chat::SendMessageRequest clean;
  clean.set_content("perfectly fine");
  EXPECT_TRUE(filter.OnBeforeSend(clean));
  EXPECT_EQ(clean.content(), "perfectly fine");  // 无命中不改写
}

TEST(WordFilterInterceptorTest, EmptyLexiconIsANoOp) {
  chirp::sdk::WordFilterInterceptor filter(chirp::sdk::WordFilterOptions{});
  chirp::chat::SendMessageRequest msg;
  msg.set_content("anything at all");
  EXPECT_TRUE(filter.OnBeforeSend(msg));
  EXPECT_EQ(msg.content(), "anything at all");
}

TEST(ChatConfigTest, Defaults) {
  ChatConfig config;
  EXPECT_EQ(config.gateway_host, "localhost");
  EXPECT_EQ(config.gateway_port, 5000u);
  EXPECT_EQ(config.gateway_ws_port, 5001u);
  EXPECT_FALSE(config.enable_websocket);
  EXPECT_EQ(config.heartbeat_interval_seconds, 25);
  EXPECT_EQ(config.max_missed_pongs, 2);
  EXPECT_EQ(config.max_reconnect_attempts, -1);
  EXPECT_EQ(config.request_timeout_ms, 10000);
  // Deprecated (fixed backoff schedule instead) but still defaults to 5.
  EXPECT_EQ(config.reconnect_interval_seconds, 5);
}

TEST(ChatErrorTest, ErrorCodeMessages) {
  auto ec = chirp::sdk::make_error_code(ChatError::NotConnected);
  EXPECT_EQ(ec.category().name(), std::string("chirp.sdk"));
  EXPECT_EQ(ec.message(), "not connected");
  EXPECT_TRUE(ec == chirp::sdk::make_error_code(ChatError::NotConnected));

  EXPECT_EQ(chirp::sdk::make_error_code(ChatError::InvalidParam).message(),
            "invalid parameter");
  EXPECT_EQ(chirp::sdk::make_error_code(ChatError::LoginFailed).message(),
            "login failed");
  EXPECT_EQ(chirp::sdk::make_error_code(ChatError::SendFailed).message(),
            "send failed");
  EXPECT_EQ(chirp::sdk::make_error_code(ChatError::Timeout).message(), "timeout");
  EXPECT_EQ(chirp::sdk::make_error_code(ChatError::OK).message(), "ok");
  EXPECT_EQ(chirp::sdk::make_error_code(ChatError::AlreadyConnected).message(),
            "already connected");
  // Unmapped enum value falls through to the generic message.
  EXPECT_EQ(chirp::sdk::make_error_code(static_cast<ChatError>(999)).message(),
            "unknown error");
}

TEST_F(SdkClientTest, InitialStateIsDisconnected) {
  ChatClient client(TcpConfig());
  EXPECT_EQ(client.GetState(), ConnectionState::Disconnected);
}

TEST_F(SdkClientTest, ConstructAndDestroyAreClean) {
  // Exercises io_context thread startup/shutdown repeatedly.
  for (int i = 0; i < 5; ++i) {
    ChatClient client(TcpConfig());
    EXPECT_EQ(client.GetState(), ConnectionState::Disconnected);
  }
  SUCCEED();
}

TEST_F(SdkClientTest, LoginWithEmptyTokenFailsWithInvalidParam) {
  ChatClient client(TcpConfig());

  std::promise<std::error_code> promise;
  auto future = promise.get_future();
  client.Login("", [&promise](const std::error_code& ec, const std::string& user_id) {
    EXPECT_TRUE(user_id.empty());
    promise.set_value(ec);
  });

  ASSERT_EQ(future.wait_for(std::chrono::milliseconds(kWaitMs)),
            std::future_status::ready);
  EXPECT_EQ(future.get(), chirp::sdk::make_error_code(ChatError::InvalidParam));
}

TEST_F(SdkClientTest, LoginWithoutConnectionFailsWithNotConnected) {
  ChatClient client(TcpConfig());

  std::promise<std::error_code> promise;
  auto future = promise.get_future();
  client.Login("some-token",
               [&promise](const std::error_code& ec, const std::string& user_id) {
                 EXPECT_TRUE(user_id.empty());
                 promise.set_value(ec);
               });

  ASSERT_EQ(future.wait_for(std::chrono::milliseconds(kWaitMs)),
            std::future_status::ready);
  EXPECT_EQ(future.get(), chirp::sdk::make_error_code(ChatError::NotConnected));
}

TEST_F(SdkClientTest, MultiplePendingLoginsAllComplete) {
  ChatClient client(TcpConfig());

  std::vector<std::future<std::error_code>> futures;
  for (int i = 0; i < 3; ++i) {
    auto promise = std::make_shared<std::promise<std::error_code>>();
    futures.push_back(promise->get_future());
    client.Login(
        "tok",
        [promise](const std::error_code& ec, const std::string&) {
          promise->set_value(ec);
        });
  }

  for (auto& f : futures) {
    ASSERT_EQ(f.wait_for(std::chrono::milliseconds(kWaitMs)),
              std::future_status::ready);
    EXPECT_EQ(f.get(), chirp::sdk::make_error_code(ChatError::NotConnected));
  }
}

TEST_F(SdkClientTest, WebsocketModeConnectFailsFastAndNotifies) {
  // enable_websocket=true is rejected before any socket is touched, so the
  // disconnect callback fires without any network IO.
  ChatClient client(WsConfig());

  std::promise<std::error_code> promise;
  auto future = promise.get_future();
  client.SetDisconnectCallback([&promise](const std::error_code& ec) {
    promise.set_value(ec);
  });

  client.Connect();

  ASSERT_EQ(future.wait_for(std::chrono::milliseconds(kWaitMs)),
            std::future_status::ready);
  EXPECT_EQ(future.get(), chirp::sdk::make_error_code(ChatError::InvalidParam));
  EXPECT_EQ(client.GetState(), ConnectionState::Disconnected);
}

TEST_F(SdkClientTest, DisconnectWhenNotConnectedIsSafe) {
  ChatClient client(TcpConfig());
  client.Disconnect();
  client.Disconnect();
  EXPECT_EQ(client.GetState(), ConnectionState::Disconnected);
}

TEST_F(SdkClientTest, LogoutWhenNotLoggedInIsSafe) {
  ChatClient client(TcpConfig());
  client.Logout();
  EXPECT_EQ(client.GetState(), ConnectionState::Disconnected);
}

TEST_F(SdkClientTest, SendMessageWhenNotLoggedInIsSafe) {
  ChatClient client(TcpConfig());

  bool message_cb_called = false;
  client.SetMessageCallback([&message_cb_called](const std::string&, const std::string&) {
    message_cb_called = true;
  });

  client.SendMessage("bob", "hello");  // dropped silently (not logged in)
  client.Disconnect();

  EXPECT_FALSE(message_cb_called);
  EXPECT_EQ(client.GetState(), ConnectionState::Disconnected);
}

TEST_F(SdkClientTest, SetCallbacksThenDestroyClientIsSafe) {
  EXPECT_NO_THROW({
    ChatClient client(TcpConfig());
    client.SetMessageCallback([](const std::string&, const std::string&) {});
    client.SetDisconnectCallback([](const std::error_code&) {});
    client.SetKickCallback([](const std::string&) {});
    client.Disconnect();
    // Disconnect on a never-connected client posts the close path: it must
    // settle in Disconnected rather than leave Connecting/Connected.
    EXPECT_EQ(client.GetState(), ConnectionState::Disconnected);
  });
}

TEST_F(SdkClientTest, LoginAfterDisconnectStillFailsCleanly) {
  ChatClient client(TcpConfig());
  client.Disconnect();

  std::promise<std::error_code> promise;
  auto future = promise.get_future();
  client.Login("tok",
               [&promise](const std::error_code& ec, const std::string&) mutable {
                 promise.set_value(ec);
               });

  ASSERT_EQ(future.wait_for(std::chrono::milliseconds(kWaitMs)),
            std::future_status::ready);
  EXPECT_EQ(future.get(), chirp::sdk::make_error_code(ChatError::NotConnected));
}

TEST_F(SdkClientTest, DisabledHeartbeatIsValidConfig) {
  ChatConfig config = TcpConfig();
  config.heartbeat_interval_seconds = 0;
  ChatClient client(config);
  client.Disconnect();
  EXPECT_EQ(client.GetState(), ConnectionState::Disconnected);
}

}  // namespace

// ---------------------------------------------------------------------------
// Full client lifecycle against a local fake gateway (127.0.0.1 loopback).
// The fake gateway accepts one connection, speaks the length-prefixed
// protobuf framing, and answers LOGIN_REQ / relays messages on demand.
// ---------------------------------------------------------------------------

#include <mutex>

#include "network/protobuf_framing.h"
#include "proto/auth.pb.h"
#include "proto/chat.pb.h"
#include "proto/common.pb.h"
#include "proto/gateway.pb.h"

namespace {

using FramedHandler = std::function<void(const chirp::gateway::Packet& pkt,
                                         std::function<void(chirp::gateway::Packet)> send)>;

class FakeGateway {
 public:
  explicit FakeGateway(FramedHandler handler)
      : handler_(std::move(handler)),
        acceptor_(io_, asio::ip::tcp::endpoint(asio::ip::tcp::v4(), 0)),
        port_(static_cast<uint16_t>(acceptor_.local_endpoint().port())) {
    DoAccept();
    thread_ = std::thread([this] { io_.run(); });
  }

  ~FakeGateway() {
    asio::post(io_, [this] {
      asio::error_code ec;
      acceptor_.close(ec);
      if (socket_) {
        socket_->close(ec);
      }
    });
    io_.stop();
    if (thread_.joinable()) {
      thread_.join();
    }
  }

  uint16_t port() const { return port_; }

  // Blocks until a client connection has been accepted (sync point against
  // the accept-vs-first-push race: async_connect on the client side can
  // complete before this gateway's accept handler runs, and an early Push()
  // would silently hit a null socket_).
  void WaitClient(int ms = 2000) {
    for (int i = 0; i < ms / 2 && !client_connected_; ++i) {
      std::this_thread::sleep_for(std::chrono::milliseconds(2));
    }
  }

  // Closes the accepted connection (keeps listening for new ones).
  void DropConnections() {
    asio::post(io_, [this] {
      asio::error_code ec;
      if (socket_) {
        socket_->close(ec);
      }
    });
  }

  // Server-side push into the accepted connection (notify shape).
  void Push(const chirp::gateway::Packet& pkt) {
    asio::post(io_, [this, pkt] {
      auto sock = socket_;
      if (sock && sock->is_open()) {
        auto framed = chirp::network::ProtobufFraming::Encode(pkt);
        asio::error_code ec;
        asio::write(*sock, asio::buffer(framed), ec);
      }
    });
  }

 private:
  void DoAccept() {
    auto sock = std::make_shared<asio::ip::tcp::socket>(io_);
    acceptor_.async_accept(*sock, [this, sock](const std::error_code& ec) {
      if (ec) {
        return;
      }
      socket_ = sock;
      client_connected_ = true;
      DoRead();
    });
  }

  void DoRead() {
    auto self_socket = socket_;
    socket_->async_read_some(
        asio::buffer(read_buf_.data() + partial_, read_buf_.size() - partial_),
        [this, self_socket](const std::error_code& ec, std::size_t n) mutable {
          if (ec) {
            return;
          }
          partial_ += n;
          Consume();
          if (self_socket->is_open()) {
            DoRead();
          }
        });
  }

  void Consume() {
    while (partial_ >= 4) {
      const uint32_t len =
          (static_cast<uint32_t>(static_cast<uint8_t>(read_buf_[0])) << 24) |
          (static_cast<uint32_t>(static_cast<uint8_t>(read_buf_[1])) << 16) |
          (static_cast<uint32_t>(static_cast<uint8_t>(read_buf_[2])) << 8) |
          static_cast<uint32_t>(static_cast<uint8_t>(read_buf_[3]));
      if (partial_ < 4u + len) {
        break;
      }
      chirp::gateway::Packet pkt;
      if (pkt.ParseFromArray(read_buf_.data() + 4, static_cast<int>(len))) {
        handler_(pkt, [this](chirp::gateway::Packet out) {
          auto framed = chirp::network::ProtobufFraming::Encode(out);
          asio::write(*socket_, asio::buffer(framed));
        });
      }
      partial_ -= 4 + len;
      std::memmove(read_buf_.data(), read_buf_.data() + 4 + len, partial_);
    }
  }

  FramedHandler handler_;
  asio::io_context io_;
  asio::ip::tcp::acceptor acceptor_;
  uint16_t port_;
  std::shared_ptr<asio::ip::tcp::socket> socket_;  // gateway io 线程专有
  std::atomic<bool> client_connected_{false};
  std::array<uint8_t, 8192> read_buf_{};
  size_t partial_ = 0;
  std::thread thread_;
};

class ChatClientLoopbackTest : public ::testing::Test {
 protected:
  static constexpr int kWaitMs = 5000;

  static ChatConfig LoopbackConfig(uint16_t port, int heartbeat_s = 30) {
    ChatConfig config;
    config.gateway_host = "127.0.0.1";
    config.gateway_port = port;
    config.enable_websocket = false;  // sdk tcp path
    config.heartbeat_interval_seconds = heartbeat_s;
    return config;
  }

  void PumpIO(asio::io_context& io, int ms) {
    io.run_for(std::chrono::milliseconds(ms));
    io.restart();
  }
};

TEST_F(ChatClientLoopbackTest, ConnectLoginSendMessageAndReceiveNotify) {
  asio::io_context gateway_io;
  FakeGateway gateway([&](const chirp::gateway::Packet& pkt, auto send) {
    switch (pkt.msg_id()) {
      case chirp::gateway::LOGIN_REQ: {
        chirp::auth::LoginResponse resp;
        resp.set_code(chirp::common::OK);
        resp.set_user_id("user-1");
        resp.set_session_id("sess-1");
        chirp::gateway::Packet out;
        out.set_msg_id(chirp::gateway::LOGIN_RESP);
        out.set_sequence(pkt.sequence());
        out.set_body(resp.SerializeAsString());
        send(out);

        // Push a chat notification right after login
        chirp::chat::ChatMessage msg;
        msg.set_message_id("m1");
        msg.set_sender_id("bob");
        msg.set_content("hello from bob");
        chirp::gateway::Packet notify;
        notify.set_msg_id(chirp::gateway::CHAT_MESSAGE_NOTIFY);
        notify.set_sequence(0);
        notify.set_body(msg.SerializeAsString());
        send(notify);
        break;
      }
      case chirp::gateway::SEND_MESSAGE_REQ: {
        // Echo back a notification so the client callback fires
        chirp::chat::SendMessageRequest req;
        req.ParseFromArray(pkt.body().data(), static_cast<int>(pkt.body().size()));
        chirp::chat::ChatMessage msg;
        msg.set_sender_id(req.sender_id());
        msg.set_content(req.content());
        chirp::gateway::Packet notify;
        notify.set_msg_id(chirp::gateway::CHAT_MESSAGE_NOTIFY);
        notify.set_sequence(0);
        notify.set_body(msg.SerializeAsString());
        send(notify);
        break;
      }
      default:
        break;
    }
  });

  ChatClient client(LoopbackConfig(gateway.port()));

  std::promise<std::error_code> login_promise;
  auto login_future = login_promise.get_future();
  std::atomic<int> messages{0};
  std::string last_sender, last_content;
  std::promise<void> message_promise;
  auto message_future = message_promise.get_future();

  client.SetMessageCallback([&](const std::string& sender, const std::string& content) {
    last_sender = sender;
    last_content = content;
    if (++messages == 2) {
      message_promise.set_value();
    }
  });

  client.Connect();

  // Wait for connection state
  for (int i = 0; i < 2500 && client.GetState() != ConnectionState::Connected; ++i) {
    std::this_thread::sleep_for(std::chrono::milliseconds(2));
  }
  ASSERT_EQ(client.GetState(), ConnectionState::Connected);

  client.Login("token-1", [&login_promise](const std::error_code& ec, const std::string& uid) {
    EXPECT_EQ(uid, "user-1");
    login_promise.set_value(ec);
  });
  ASSERT_EQ(login_future.wait_for(std::chrono::milliseconds(5000)),
            std::future_status::ready);
  EXPECT_FALSE(login_future.get());
  ASSERT_EQ(client.GetState(), ConnectionState::LoggedIn);

  // First notify came right after login
  for (int i = 0; i < 2500 && messages < 1; ++i) {
    std::this_thread::sleep_for(std::chrono::milliseconds(2));
  }
  EXPECT_EQ(messages.load(), 1);
  EXPECT_EQ(last_sender, "bob");

  // Send triggers the echo notify
  client.SendMessage("bob", "hi there");
  ASSERT_EQ(message_future.wait_for(std::chrono::milliseconds(5000)),
            std::future_status::ready);
  EXPECT_EQ(messages.load(), 2);
  EXPECT_EQ(last_content, "hi there");

  client.Disconnect();
  for (int i = 0; i < 2500 && client.GetState() != ConnectionState::Disconnected; ++i) {
    std::this_thread::sleep_for(std::chrono::milliseconds(2));
  }
  EXPECT_EQ(client.GetState(), ConnectionState::Disconnected);
}

TEST_F(ChatClientLoopbackTest, LoginRejectedByServerFailsWithLoginFailed) {
  FakeGateway gateway([](const chirp::gateway::Packet& pkt, auto send) {
    if (pkt.msg_id() == chirp::gateway::LOGIN_REQ) {
      chirp::auth::LoginResponse resp;
      resp.set_code(chirp::common::AUTH_FAILED);
      chirp::gateway::Packet out;
      out.set_msg_id(chirp::gateway::LOGIN_RESP);
      out.set_sequence(pkt.sequence());
      out.set_body(resp.SerializeAsString());
      send(out);
    }
  });

  ChatClient client(LoopbackConfig(gateway.port()));
  client.Connect();
  for (int i = 0; i < 2500 && client.GetState() != ConnectionState::Connected; ++i) {
    std::this_thread::sleep_for(std::chrono::milliseconds(2));
  }
  ASSERT_EQ(client.GetState(), ConnectionState::Connected);

  std::promise<std::error_code> promise;
  auto future = promise.get_future();
  client.Login("bad-token", [&promise](const std::error_code& ec, const std::string& uid) {
    EXPECT_TRUE(uid.empty());
    promise.set_value(ec);
  });
  ASSERT_EQ(future.wait_for(std::chrono::milliseconds(5000)),
            std::future_status::ready);
  EXPECT_EQ(future.get(), chirp::sdk::make_error_code(ChatError::LoginFailed));
  EXPECT_EQ(client.GetState(), ConnectionState::Connected);  // not logged in
  client.Disconnect();
}

TEST_F(ChatClientLoopbackTest, KickNotifyInvokesKickCallback) {
  FakeGateway gateway([](const chirp::gateway::Packet& pkt, auto send) {
    if (pkt.msg_id() == chirp::gateway::LOGIN_REQ) {
      chirp::auth::LoginResponse resp;
      resp.set_code(chirp::common::OK);
      resp.set_user_id("user-9");
      chirp::gateway::Packet out;
      out.set_msg_id(chirp::gateway::LOGIN_RESP);
      out.set_sequence(pkt.sequence());
      out.set_body(resp.SerializeAsString());
      send(out);

      chirp::auth::KickNotify kick;
      kick.set_reason("duplicate login");
      chirp::gateway::Packet kp;
      kp.set_msg_id(chirp::gateway::KICK_NOTIFY);
      kp.set_sequence(0);
      kp.set_body(kick.SerializeAsString());
      send(kp);
    }
  });

  ChatClient client(LoopbackConfig(gateway.port()));
  std::promise<std::string> kick_promise;
  auto kick_future = kick_promise.get_future();
  client.SetKickCallback([&kick_promise](const std::string& reason) {
    kick_promise.set_value(reason);
  });

  client.Connect();
  for (int i = 0; i < 2500 && client.GetState() != ConnectionState::Connected; ++i) {
    std::this_thread::sleep_for(std::chrono::milliseconds(2));
  }
  ASSERT_EQ(client.GetState(), ConnectionState::Connected);

  std::promise<std::error_code> login_promise;
  auto login_future = login_promise.get_future();
  client.Login("tok", [&login_promise](const std::error_code& ec, const std::string&) {
    login_promise.set_value(ec);
  });
  ASSERT_EQ(login_future.wait_for(std::chrono::milliseconds(5000)),
            std::future_status::ready);
  EXPECT_FALSE(login_future.get());

  ASSERT_EQ(kick_future.wait_for(std::chrono::milliseconds(5000)),
            std::future_status::ready);
  EXPECT_EQ(kick_future.get(), "duplicate login");
  client.Disconnect();
}

TEST_F(ChatClientLoopbackTest, HeartbeatPingsAreSentPeriodically) {
  std::atomic<int> pings{0};
  FakeGateway gateway([&pings](const chirp::gateway::Packet& pkt, auto send) {
    if (pkt.msg_id() == chirp::gateway::LOGIN_REQ) {
      chirp::auth::LoginResponse resp;
      resp.set_code(chirp::common::OK);
      resp.set_user_id("u");
      chirp::gateway::Packet out;
      out.set_msg_id(chirp::gateway::LOGIN_RESP);
      out.set_sequence(pkt.sequence());
      out.set_body(resp.SerializeAsString());
      send(out);
    } else if (pkt.msg_id() == chirp::gateway::HEARTBEAT_PING) {
      ++pings;
    }
  });

  ChatClient client(LoopbackConfig(gateway.port(), /*heartbeat_s=*/1));
  client.Connect();
  for (int i = 0; i < 2500 && client.GetState() != ConnectionState::Connected; ++i) {
    std::this_thread::sleep_for(std::chrono::milliseconds(2));
  }
  ASSERT_EQ(client.GetState(), ConnectionState::Connected);

  // Two heartbeat windows should be enough to observe several pings.
  for (int i = 0; i < 2500 && pings.load() < 2; ++i) {
    std::this_thread::sleep_for(std::chrono::milliseconds(2));
  }
  EXPECT_GE(pings.load(), 2);
  client.Disconnect();
}

TEST_F(ChatClientLoopbackTest, ServerCloseFailsPendingLogin) {
  class ClosingGateway {
   public:
    ClosingGateway()
        : acceptor_(io_, asio::ip::tcp::endpoint(asio::ip::tcp::v4(), 0)),
          port_(static_cast<uint16_t>(acceptor_.local_endpoint().port())) {
      DoAccept();
      thread_ = std::thread([this] { io_.run(); });
    }
    ~ClosingGateway() {
      io_.stop();
      if (thread_.joinable()) {
        thread_.join();
      }
    }
  uint16_t port() const { return port_; }

   private:
    void DoAccept() {
      auto sock = std::make_shared<asio::ip::tcp::socket>(io_);
      acceptor_.async_accept(*sock, [this, sock](const std::error_code& ec) {
        if (ec) {
          return;
        }
        // Close immediately: client's pending ops fail with an error.
        asio::error_code ignore;
        sock->close(ignore);
      });
    }
    asio::io_context io_;
    asio::ip::tcp::acceptor acceptor_;
    uint16_t port_;
    std::thread thread_;
  };

  ClosingGateway gateway;
  ChatClient client(LoopbackConfig(gateway.port()));
  std::promise<std::error_code> disc_promise;
  auto disc_future = disc_promise.get_future();
  client.SetDisconnectCallback([&disc_promise](const std::error_code&) {
    disc_promise.set_value(std::error_code{});
  });

  client.Connect();
  // Server closes right after accept: the read error must surface through
  // the disconnect callback. Note: the connection state itself is left as-is
  // by DoClose (only Disconnect()/Logout() reset it), which is the observed
  // implementation behavior.
  ASSERT_EQ(disc_future.wait_for(std::chrono::milliseconds(5000)),
            std::future_status::ready);
}

// Defined below (first used here): polled state wait with a generous budget.
void WaitState(ChatClient& client, ConnectionState want, int ms);

TEST_F(ChatClientLoopbackTest, LogoutAfterLoginClosesCleanly) {
  FakeGateway gateway([](const chirp::gateway::Packet& pkt, auto send) {
    if (pkt.msg_id() == chirp::gateway::LOGIN_REQ) {
      chirp::auth::LoginResponse resp;
      resp.set_code(chirp::common::OK);
      resp.set_user_id("u1");
      resp.set_session_id("s1");
      chirp::gateway::Packet out;
      out.set_msg_id(chirp::gateway::LOGIN_RESP);
      out.set_sequence(pkt.sequence());
      out.set_body(resp.SerializeAsString());
      send(out);
    }
  });

  ChatClient client(LoopbackConfig(gateway.port()));
  client.Connect();
  // WaitState (5s polled) instead of a 600ms inline cap: under load the
  // loopback connect can take arbitrarily long to surface as Connected.
  WaitState(client, ConnectionState::Connected, 5000);

  std::promise<std::error_code> promise;
  auto future = promise.get_future();
  client.Login("t", [&promise](const std::error_code& ec, const std::string&) {
    promise.set_value(ec);
  });
  ASSERT_EQ(future.wait_for(std::chrono::milliseconds(5000)), std::future_status::ready);
  EXPECT_FALSE(future.get());
  ASSERT_EQ(client.GetState(), ConnectionState::LoggedIn);

  client.Logout();  // sends LOGOUT_REQ then closes
  for (int i = 0; i < 2500 && client.GetState() != ConnectionState::Disconnected; ++i) {
    std::this_thread::sleep_for(std::chrono::milliseconds(2));
  }
  EXPECT_EQ(client.GetState(), ConnectionState::Disconnected);
}

TEST_F(ChatClientLoopbackTest, SendAndCloseDataPath) {
  FakeGateway gateway([](const chirp::gateway::Packet& pkt, auto send) {
    if (pkt.msg_id() == chirp::gateway::LOGIN_REQ) {
      chirp::auth::LoginResponse resp;
      resp.set_code(chirp::common::OK);
      resp.set_user_id("u");
      chirp::gateway::Packet out;
      out.set_msg_id(chirp::gateway::LOGIN_RESP);
      out.set_sequence(pkt.sequence());
      out.set_body(resp.SerializeAsString());
      send(out);
    }
  });

  ChatClient client(LoopbackConfig(gateway.port()));
  client.Connect();
  for (int i = 0; i < 2500 && client.GetState() != ConnectionState::Connected; ++i) {
    std::this_thread::sleep_for(std::chrono::milliseconds(2));
  }
  std::promise<std::error_code> promise;
  auto future = promise.get_future();
  client.Login("t", [&promise](const std::error_code& ec, const std::string&) {
    promise.set_value(ec);
  });
  ASSERT_EQ(future.wait_for(std::chrono::milliseconds(5000)), std::future_status::ready);

  // Several messages exercise the write queue / DoWrite chaining.
  for (int i = 0; i < 5; ++i) {
    client.SendMessage("peer", "msg-" + std::to_string(i));
  }
  std::this_thread::sleep_for(std::chrono::milliseconds(100));
  client.Disconnect();
  for (int i = 0; i < 2500 && client.GetState() != ConnectionState::Disconnected; ++i) {
    std::this_thread::sleep_for(std::chrono::milliseconds(2));
  }
  EXPECT_EQ(client.GetState(), ConnectionState::Disconnected);
}

// ---------------------------------------------------------------------------
// Additional edge-path coverage: connect failures, corrupt frames, sequence
// mismatches and idle-state post handlers.
// ---------------------------------------------------------------------------

void WaitState(ChatClient& client, ConnectionState want, int ms = 5000) {
  for (int i = 0; i < ms / 2 && client.GetState() != want; ++i) {
    std::this_thread::sleep_for(std::chrono::milliseconds(2));
  }
  // A wait that silently times out would let every assertion below run
  // against the wrong state; the timeout itself is the error path.
  EXPECT_EQ(client.GetState(), want)
      << "state never reached " << static_cast<int>(want) << " within " << ms
      << "ms (got " << static_cast<int>(client.GetState()) << ")";
}

TEST_F(SdkClientTest, IdleLogoutAndSendPostsAreExecuted) {
  ChatClient client(TcpConfig());
  // The handlers are posted to the internal io thread; give them time to run
  // so the not-logged-in early exits inside the posts are exercised.
  client.Logout();
  client.SendMessage("bob", "hi");
  std::this_thread::sleep_for(std::chrono::milliseconds(200));
  EXPECT_EQ(client.GetState(), ConnectionState::Disconnected);
}

TEST_F(ChatClientLoopbackTest, ConnectWhileAlreadyConnectedIsIgnored) {
  // Connect to a gateway that never answers LOGIN so the client stays in the
  // Connected state; a second Connect() must hit the state guard.
  FakeGateway gateway([](const chirp::gateway::Packet&, auto) {});
  ChatClient client(LoopbackConfig(gateway.port()));
  client.Connect();
  WaitState(client, ConnectionState::Connected);
  ASSERT_EQ(client.GetState(), ConnectionState::Connected);
  client.Connect();  // ignored: state is not Disconnected
  std::this_thread::sleep_for(std::chrono::milliseconds(100));
  EXPECT_EQ(client.GetState(), ConnectionState::Connected);
  client.Disconnect();
}

TEST_F(SdkClientTest, ConnectToDeadPortNotifiesDisconnect) {
  ChatConfig config = TcpConfig();
  config.gateway_host = "127.0.0.1";
  // Find a port with no listener: bind/close a socket to reserve one.
  asio::io_context io;
  asio::ip::tcp::acceptor probe(io, asio::ip::tcp::endpoint(asio::ip::tcp::v4(), 0));
  config.gateway_port = static_cast<uint16_t>(probe.local_endpoint().port());
  probe.close();

  ChatClient client(config);
  std::promise<std::error_code> promise;
  auto future = promise.get_future();
  client.SetDisconnectCallback([&promise](const std::error_code& ec) {
    promise.set_value(ec);
  });
  client.Connect();
  ASSERT_EQ(future.wait_for(std::chrono::milliseconds(kWaitMs)),
            std::future_status::ready);
  EXPECT_TRUE(future.get());
}

TEST_F(SdkClientTest, ConnectToUnresolvableHostNotifiesDisconnect) {
  ChatConfig config = TcpConfig();
  config.gateway_host = "no-such-host.invalid.";
  ChatClient client(config);
  std::promise<std::error_code> promise;
  auto future = promise.get_future();
  client.SetDisconnectCallback([&promise](const std::error_code& ec) {
    promise.set_value(ec);
  });
  client.Connect();
  ASSERT_EQ(future.wait_for(std::chrono::milliseconds(kWaitMs)),
            std::future_status::ready);
  EXPECT_TRUE(future.get());
}

// Gateway double that can write arbitrary raw bytes to the client socket.
class RawGateway {
 public:
  explicit RawGateway(std::function<void(asio::ip::tcp::socket&)> on_connect)
      : acceptor_(io_, asio::ip::tcp::endpoint(asio::ip::tcp::v4(), 0)),
        port_(static_cast<uint16_t>(acceptor_.local_endpoint().port())),
        on_connect_(std::move(on_connect)) {
    DoAccept();
    thread_ = std::thread([this] { io_.run(); });
  }
  ~RawGateway() {
    io_.stop();
    if (thread_.joinable()) {
      thread_.join();
    }
  }
  uint16_t port() const { return port_; }

 private:
  void DoAccept() {
    auto sock = std::make_shared<asio::ip::tcp::socket>(io_);
    acceptor_.async_accept(*sock, [this, sock](const std::error_code& ec) {
      if (ec) {
        return;
      }
      socket_ = sock;  // keep the accepted socket alive past this handler
      on_connect_(*sock);
      DoAccept();
    });
  }
  asio::io_context io_;
  asio::ip::tcp::acceptor acceptor_;
  uint16_t port_;
  std::shared_ptr<asio::ip::tcp::socket> socket_;
  std::function<void(asio::ip::tcp::socket&)> on_connect_;
  std::thread thread_;
};

std::string LpWrap(const std::string& payload) {
  const uint32_t len = static_cast<uint32_t>(payload.size());
  std::string out;
  out.push_back(static_cast<char>((len >> 24) & 0xFF));
  out.push_back(static_cast<char>((len >> 16) & 0xFF));
  out.push_back(static_cast<char>((len >> 8) & 0xFF));
  out.push_back(static_cast<char>(len & 0xFF));
  out += payload;
  return out;
}

TEST_F(ChatClientLoopbackTest, CorruptAndUnknownFramesAreIgnored) {
  std::atomic<int> kicks{0};
  std::atomic<int> messages{0};

  RawGateway gateway([&](asio::ip::tcp::socket& sock) {
    asio::error_code ec;
    // Garbage protobuf payload (Decode must fail).
    asio::write(sock, asio::buffer(LpWrap("\xff\xff\xff\xff")), ec);
    // Unknown msg id: valid protobuf, unhandled switch branch.
    chirp::gateway::Packet unknown;
    unknown.set_msg_id(chirp::gateway::HEARTBEAT_PONG);
    unknown.set_sequence(1);
    asio::write(sock, asio::buffer(LpWrap(unknown.SerializeAsString())), ec);
    // KICK_NOTIFY with a corrupt body.
    chirp::gateway::Packet kick;
    kick.set_msg_id(chirp::gateway::KICK_NOTIFY);
    kick.set_body("###not-a-kick###");
    asio::write(sock, asio::buffer(LpWrap(kick.SerializeAsString())), ec);
    // CHAT_MESSAGE_NOTIFY with a corrupt body.
    chirp::gateway::Packet chat;
    chat.set_msg_id(chirp::gateway::CHAT_MESSAGE_NOTIFY);
    chat.set_body("###not-a-message###");
    asio::write(sock, asio::buffer(LpWrap(chat.SerializeAsString())), ec);
  });

  ChatClient client(LoopbackConfig(gateway.port()));
  client.SetKickCallback([&](const std::string&) { ++kicks; });
  client.SetMessageCallback([&](const std::string&, const std::string&) { ++messages; });

  client.Connect();
  WaitState(client, ConnectionState::Connected);
  ASSERT_EQ(client.GetState(), ConnectionState::Connected);
  std::this_thread::sleep_for(std::chrono::milliseconds(300));

  EXPECT_EQ(kicks.load(), 0);
  EXPECT_EQ(messages.load(), 0);
  client.Disconnect();
}

TEST_F(ChatClientLoopbackTest, LoginRespWithCorruptBodyFailsLogin) {
  FakeGateway gateway([](const chirp::gateway::Packet& pkt, auto send) {
    if (pkt.msg_id() == chirp::gateway::LOGIN_REQ) {
      chirp::gateway::Packet out;
      out.set_msg_id(chirp::gateway::LOGIN_RESP);
      out.set_sequence(pkt.sequence());
      out.set_body("###not-a-login-response###");
      send(out);
    }
  });

  ChatClient client(LoopbackConfig(gateway.port()));
  client.Connect();
  WaitState(client, ConnectionState::Connected);

  std::promise<std::error_code> promise;
  auto future = promise.get_future();
  client.Login("t", [&promise](const std::error_code& ec, const std::string&) {
    promise.set_value(ec);
  });
  ASSERT_EQ(future.wait_for(std::chrono::milliseconds(kWaitMs)),
            std::future_status::ready);
  EXPECT_EQ(future.get(), chirp::sdk::make_error_code(ChatError::LoginFailed));
  client.Disconnect();
}

TEST_F(ChatClientLoopbackTest, LoginRespSequenceMismatchIsDroppedThenDisconnectFails) {
  FakeGateway gateway([](const chirp::gateway::Packet& pkt, auto send) {
    if (pkt.msg_id() == chirp::gateway::LOGIN_REQ) {
      chirp::auth::LoginResponse resp;
      resp.set_code(chirp::common::OK);
      resp.set_user_id("u");
      chirp::gateway::Packet out;
      out.set_msg_id(chirp::gateway::LOGIN_RESP);
      out.set_sequence(pkt.sequence() + 1000);  // mismatch: dropped by client
      out.set_body(resp.SerializeAsString());
      send(out);
    }
  });

  ChatClient client(LoopbackConfig(gateway.port()));
  client.Connect();
  WaitState(client, ConnectionState::Connected);

  std::atomic<int> completions{0};
  client.Login("t", [&](const std::error_code& ec, const std::string&) {
    EXPECT_TRUE(ec);
    ++completions;
  });
  // The mismatched response never completes the login; the pending callback is
  // flushed when the client is disconnected.
  std::this_thread::sleep_for(std::chrono::milliseconds(300));
  EXPECT_EQ(completions.load(), 0);
  client.Disconnect();
  for (int i = 0; i < kWaitMs / 2 && completions.load() < 1; ++i) {
    std::this_thread::sleep_for(std::chrono::milliseconds(2));
  }
  EXPECT_EQ(completions.load(), 1);
}

TEST_F(ChatClientLoopbackTest, LoginWithHeartbeatDisabled) {
  FakeGateway gateway([](const chirp::gateway::Packet& pkt, auto send) {
    if (pkt.msg_id() == chirp::gateway::LOGIN_REQ) {
      chirp::auth::LoginResponse resp;
      resp.set_code(chirp::common::OK);
      resp.set_user_id("u");
      chirp::gateway::Packet out;
      out.set_msg_id(chirp::gateway::LOGIN_RESP);
      out.set_sequence(pkt.sequence());
      out.set_body(resp.SerializeAsString());
      send(out);
    }
  });

  ChatClient client(LoopbackConfig(gateway.port(), /*heartbeat_s=*/0));
  client.Connect();
  WaitState(client, ConnectionState::Connected);

  std::promise<std::error_code> promise;
  auto future = promise.get_future();
  client.Login("t", [&promise](const std::error_code& ec, const std::string&) {
    promise.set_value(ec);
  });
  ASSERT_EQ(future.wait_for(std::chrono::milliseconds(kWaitMs)),
            std::future_status::ready);
  EXPECT_FALSE(future.get());
  // Empty receiver is dropped inside the SendMessage post.
  client.SendMessage("", "no-receiver");
  std::this_thread::sleep_for(std::chrono::milliseconds(150));
  client.Disconnect();
}

TEST_F(ChatClientLoopbackTest, SendAfterServerCloseIsDropped) {
  FakeGateway gateway([](const chirp::gateway::Packet& pkt, auto send) {
    if (pkt.msg_id() == chirp::gateway::LOGIN_REQ) {
      chirp::auth::LoginResponse resp;
      resp.set_code(chirp::common::OK);
      resp.set_user_id("u");
      chirp::gateway::Packet out;
      out.set_msg_id(chirp::gateway::LOGIN_RESP);
      out.set_sequence(pkt.sequence());
      out.set_body(resp.SerializeAsString());
      send(out);
    }
  });

  ChatClient client(LoopbackConfig(gateway.port()));
  client.Connect();
  WaitState(client, ConnectionState::Connected);
  std::promise<std::error_code> promise;
  auto future = promise.get_future();
  client.Login("t", [&promise](const std::error_code& ec, const std::string&) {
    promise.set_value(ec);
  });
  ASSERT_EQ(future.wait_for(std::chrono::milliseconds(kWaitMs)),
            std::future_status::ready);
  EXPECT_FALSE(future.get());
  ASSERT_EQ(client.GetState(), ConnectionState::LoggedIn);

  gateway.DropConnections();
  // The read error surfaces asynchronously; poll until the client has left
  // LoggedIn instead of a fixed nap — under load the notice can take longer
  // than any fixed window, and once noticed the ~500ms backoff may already
  // be firing.
  for (int i = 0; i < kWaitMs / 2 &&
                  (client.GetState() == ConnectionState::LoggedIn ||
                   client.GetState() == ConnectionState::Connected);
       ++i) {
    std::this_thread::sleep_for(std::chrono::milliseconds(2));
  }

  // The send after the transport died must be dropped harmlessly; the
  // reconnect cycle is running (WaitingReconnect, or the backoff already
  // fired into Connecting).
  client.SendMessage("peer", "after-close");
  EXPECT_TRUE(client.GetState() == ConnectionState::WaitingReconnect ||
              client.GetState() == ConnectionState::Connecting)
      << "state " << static_cast<int>(client.GetState());

  // The gateway keeps listening, so the backoff reconnect lands back at
  // Connected (without a login: re-auth is the caller's job).
  WaitState(client, ConnectionState::Connected);
  client.Disconnect();
  WaitState(client, ConnectionState::Disconnected);
  EXPECT_EQ(client.GetState(), ConnectionState::Disconnected);
}

TEST_F(ChatClientLoopbackTest, LargeWriteAgainstResetPeerSurfacesError) {
  FakeGateway gateway([](const chirp::gateway::Packet& pkt, auto send) {
    if (pkt.msg_id() == chirp::gateway::LOGIN_REQ) {
      chirp::auth::LoginResponse resp;
      resp.set_code(chirp::common::OK);
      resp.set_user_id("u");
      chirp::gateway::Packet out;
      out.set_msg_id(chirp::gateway::LOGIN_RESP);
      out.set_sequence(pkt.sequence());
      out.set_body(resp.SerializeAsString());
      send(out);
    }
  });

  ChatClient client(LoopbackConfig(gateway.port()));
  std::atomic<int> disconnects{0};
  client.SetDisconnectCallback([&](const std::error_code&) { ++disconnects; });

  client.Connect();
  WaitState(client, ConnectionState::Connected);
  std::promise<std::error_code> promise;
  auto future = promise.get_future();
  client.Login("t", [&promise](const std::error_code& ec, const std::string&) {
    promise.set_value(ec);
  });
  ASSERT_EQ(future.wait_for(std::chrono::milliseconds(kWaitMs)),
            std::future_status::ready);
  EXPECT_FALSE(future.get());

  // Queue a multi-megabyte write and drop the peer mid-flight: the pending
  // async write must complete with an error and surface through the
  // disconnect callback.
  client.SendMessage("peer", std::string(8 * 1024 * 1024, 'x'));
  std::this_thread::sleep_for(std::chrono::milliseconds(150));
  gateway.DropConnections();

  for (int i = 0; i < kWaitMs / 2 && disconnects.load() == 0; ++i) {
    std::this_thread::sleep_for(std::chrono::milliseconds(2));
  }
  EXPECT_GE(disconnects.load(), 1);
  client.Disconnect();
}

// ---------------------------------------------------------------------------
// WP-6b: generic request/response, notify subscriptions, pong validation,
// terminal kick — the same connection semantics the web/mobile/unity clients
// implement.
// ---------------------------------------------------------------------------

// Login round-trip that blocks on the callback (test-side convenience).
static void LoginSync(ChatClient& client, const std::string& token) {
  std::promise<std::error_code> done;
  client.Login(token, [&done](const std::error_code& ec, const std::string&) {
    done.set_value(ec);
  });
  auto future = done.get_future();
  ASSERT_EQ(future.wait_for(std::chrono::milliseconds(5000)), std::future_status::ready);
  ASSERT_FALSE(future.get());
}

TEST_F(ChatClientLoopbackTest, RequestResponseCorrelatesBySequenceAndMsgId) {
  FakeGateway gateway([](const chirp::gateway::Packet& pkt, auto send) {
    if (pkt.msg_id() == chirp::gateway::LOGIN_REQ) {
      chirp::auth::LoginResponse resp;
      resp.set_code(chirp::common::OK);
      resp.set_user_id("u");
      chirp::gateway::Packet out;
      out.set_msg_id(chirp::gateway::LOGIN_RESP);
      out.set_sequence(pkt.sequence());
      out.set_body(resp.SerializeAsString());
      send(out);
    }
    if (pkt.msg_id() == chirp::gateway::GET_HISTORY_REQ) {
      chirp::chat::GetHistoryResponse resp;
      resp.set_code(chirp::common::OK);
      chirp::gateway::Packet out;
      out.set_msg_id(chirp::gateway::GET_HISTORY_RESP);
      out.set_sequence(pkt.sequence());
      out.set_body(resp.SerializeAsString());
      send(out);
    }
  });

  ChatClient client(LoopbackConfig(gateway.port()));
  client.Connect();
  WaitState(client, ConnectionState::Connected);
  LoginSync(client, "t");

  std::promise<std::error_code> done;
  client.Request(chirp::gateway::GET_HISTORY_REQ, chirp::gateway::GET_HISTORY_RESP,
                 chirp::chat::GetHistoryRequest().SerializeAsString(),
                 [&](const std::error_code& ec, const std::string& body) {
                   if (!ec) {
                     chirp::chat::GetHistoryResponse resp;
                     EXPECT_TRUE(resp.ParseFromString(body));
                   }
                   done.set_value(ec);
                 });
  auto future = done.get_future();
  ASSERT_EQ(future.wait_for(std::chrono::milliseconds(kWaitMs)),
            std::future_status::ready);
  EXPECT_FALSE(future.get());
  client.Disconnect();
}

TEST_F(ChatClientLoopbackTest, RequestWithMismatchedRespMsgIdTimesOut) {
  FakeGateway gateway([](const chirp::gateway::Packet& pkt, auto send) {
    if (pkt.msg_id() == chirp::gateway::LOGIN_REQ) {
      chirp::auth::LoginResponse resp;
      resp.set_code(chirp::common::OK);
      resp.set_user_id("u");
      chirp::gateway::Packet out;
      out.set_msg_id(chirp::gateway::LOGIN_RESP);
      out.set_sequence(pkt.sequence());
      out.set_body(resp.SerializeAsString());
      send(out);
    }
    if (pkt.msg_id() == chirp::gateway::GET_HISTORY_REQ) {
      // Wrong resp msg id on the right sequence: the client must ignore it
      // (stray-response guard) instead of completing the waiter.
      chirp::gateway::Packet out;
      out.set_msg_id(chirp::gateway::GET_USER_GROUPS_RESP);
      out.set_sequence(pkt.sequence());
      out.set_body("");
      send(out);
    }
  });

  ChatConfig config = LoopbackConfig(gateway.port());
  config.request_timeout_ms = 400;
  ChatClient client(config);
  client.Connect();
  WaitState(client, ConnectionState::Connected);
  LoginSync(client, "t");

  std::promise<std::error_code> done;
  client.Request(chirp::gateway::GET_HISTORY_REQ, chirp::gateway::GET_HISTORY_RESP,
                 "", [&done](const std::error_code& ec, const std::string&) {
                   done.set_value(ec);
                 });
  auto future = done.get_future();
  ASSERT_EQ(future.wait_for(std::chrono::milliseconds(kWaitMs)),
            std::future_status::ready);
  EXPECT_EQ(future.get(), make_error_code(ChatError::Timeout));
  client.Disconnect();
}

TEST_F(ChatClientLoopbackTest, NotifySubscribeReceivesBodyAndUnsubscribeStops) {
  FakeGateway gateway([](const chirp::gateway::Packet& pkt, auto send) {
    if (pkt.msg_id() == chirp::gateway::LOGIN_REQ) {
      chirp::auth::LoginResponse resp;
      resp.set_code(chirp::common::OK);
      resp.set_user_id("u");
      chirp::gateway::Packet out;
      out.set_msg_id(chirp::gateway::LOGIN_RESP);
      out.set_sequence(pkt.sequence());
      out.set_body(resp.SerializeAsString());
      send(out);
    }
  });

  ChatClient client(LoopbackConfig(gateway.port()));
  client.Connect();
  WaitState(client, ConnectionState::Connected);
  gateway.WaitClient();  // 接受完成前 Push 会被 null socket_ 静默丢弃

  std::promise<std::string> first_body;
  auto handle = client.OnNotify(chirp::gateway::CHAT_MESSAGE_NOTIFY,
                                [&](const std::string& body) {
                                  first_body.set_value(body);
                                });

  // Server pushes a live message with no sequence (notify shape).
  chirp::gateway::Packet notify;
  notify.set_msg_id(chirp::gateway::CHAT_MESSAGE_NOTIFY);
  chirp::chat::ChatMessage msg;
  msg.set_message_id("m1");
  notify.set_body(msg.SerializeAsString());
  gateway.Push(notify);

  auto future = first_body.get_future();
  ASSERT_EQ(future.wait_for(std::chrono::milliseconds(kWaitMs)),
            std::future_status::ready);
  chirp::chat::ChatMessage parsed;
  ASSERT_TRUE(parsed.ParseFromString(future.get()));
  EXPECT_EQ(parsed.message_id(), "m1");

  client.OffNotify(handle);
  gateway.Push(notify);
  std::this_thread::sleep_for(std::chrono::milliseconds(200));
  client.Disconnect();
}

// sequence==0 且非 KICK/CHAT/PONG 特化通道的包走 switch 的 default 臂:
// 通用 OnNotify 订阅者照常拿到原始 body。
TEST_F(ChatClientLoopbackTest, GenericNotifyIdFallsThroughToOnNotify) {
  FakeGateway gateway([](const chirp::gateway::Packet& pkt, auto send) {
    if (pkt.msg_id() == chirp::gateway::LOGIN_REQ) {
      chirp::auth::LoginResponse resp;
      resp.set_code(chirp::common::OK);
      resp.set_user_id("u");
      chirp::gateway::Packet out;
      out.set_msg_id(chirp::gateway::LOGIN_RESP);
      out.set_sequence(pkt.sequence());
      out.set_body(resp.SerializeAsString());
      send(out);
    }
  });

  ChatClient client(LoopbackConfig(gateway.port()));
  client.Connect();
  WaitState(client, ConnectionState::Connected);
  gateway.WaitClient();  // 接受完成前 Push 会被 null socket_ 静默丢弃

  std::promise<std::string> pushed;
  auto handle = client.OnNotify(chirp::gateway::GET_HISTORY_RESP,
                                [&](const std::string& body) {
                                  pushed.set_value(body);
                                });

  chirp::gateway::Packet stray;
  stray.set_msg_id(chirp::gateway::GET_HISTORY_RESP);
  stray.set_body("raw-generic");
  gateway.Push(stray);

  auto future = pushed.get_future();
  ASSERT_EQ(future.wait_for(std::chrono::milliseconds(kWaitMs)),
            std::future_status::ready);
  EXPECT_EQ(future.get(), "raw-generic");
  client.OffNotify(handle);
  client.Disconnect();
}

TEST_F(ChatClientLoopbackTest, KickIsTerminalFlushesKickedAndNeverReconnects) {
  FakeGateway gateway([](const chirp::gateway::Packet& pkt, auto send) {
    if (pkt.msg_id() == chirp::gateway::LOGIN_REQ) {
      chirp::auth::LoginResponse resp;
      resp.set_code(chirp::common::OK);
      resp.set_user_id("u");
      chirp::gateway::Packet out;
      out.set_msg_id(chirp::gateway::LOGIN_RESP);
      out.set_sequence(pkt.sequence());
      out.set_body(resp.SerializeAsString());
      send(out);
    }
    if (pkt.msg_id() == chirp::gateway::GET_HISTORY_REQ) {
      // Kick in reply to the request: the client must flush the in-flight
      // request with Kicked, enter the terminal state, and stop there.
      chirp::auth::KickNotify kick;
      kick.set_reason("device takeover");
      chirp::gateway::Packet kn;
      kn.set_msg_id(chirp::gateway::KICK_NOTIFY);
      kn.set_body(kick.SerializeAsString());
      send(kn);
    }
  });

  ChatClient client(LoopbackConfig(gateway.port()));
  std::promise<std::string> kicked;
  auto kick_future = kicked.get_future();
  client.SetKickCallback([&kicked](const std::string& reason) { kicked.set_value(reason); });
  client.Connect();
  WaitState(client, ConnectionState::Connected);
  LoginSync(client, "t");

  std::promise<std::error_code> flushed;
  auto flush_future = flushed.get_future();
  client.Request(chirp::gateway::GET_HISTORY_REQ, chirp::gateway::GET_HISTORY_RESP, "",
                 [&flushed](const std::error_code& ec, const std::string&) {
                   flushed.set_value(ec);
                 });

  ASSERT_EQ(kick_future.wait_for(std::chrono::milliseconds(kWaitMs)),
            std::future_status::ready);
  EXPECT_EQ(kick_future.get(), "device takeover");
  EXPECT_EQ(client.GetState(), ConnectionState::Kicked);
  ASSERT_EQ(flush_future.wait_for(std::chrono::milliseconds(kWaitMs)),
            std::future_status::ready);
  EXPECT_EQ(flush_future.get(), make_error_code(ChatError::Kicked));

  // Terminal: the backoff loop never reschedules out of Kicked.
  std::this_thread::sleep_for(std::chrono::milliseconds(1200));
  EXPECT_EQ(client.GetState(), ConnectionState::Kicked);
}

TEST_F(ChatClientLoopbackTest, AnsweredHeartbeatsKeepTheConnectionAlive) {
  FakeGateway gateway([](const chirp::gateway::Packet& pkt, auto send) {
    if (pkt.msg_id() == chirp::gateway::LOGIN_REQ) {
      chirp::auth::LoginResponse resp;
      resp.set_code(chirp::common::OK);
      resp.set_user_id("u");
      chirp::gateway::Packet out;
      out.set_msg_id(chirp::gateway::LOGIN_RESP);
      out.set_sequence(pkt.sequence());
      out.set_body(resp.SerializeAsString());
      send(out);
    }
    if (pkt.msg_id() == chirp::gateway::HEARTBEAT_PING) {
      // Pong must echo the ping's non-zero sequence to be credited.
      chirp::gateway::HeartbeatPong pong;
      chirp::gateway::Packet out;
      out.set_msg_id(chirp::gateway::HEARTBEAT_PONG);
      out.set_sequence(pkt.sequence());
      out.set_body(pong.SerializeAsString());
      send(out);
    }
  });

  ChatClient client(LoopbackConfig(gateway.port(), /*heartbeat_s=*/1));
  client.Connect();
  WaitState(client, ConnectionState::Connected);
  LoginSync(client, "t");

  // Three heartbeat periods with answered pongs: must never trip the
  // missed-pong death detection.
  std::this_thread::sleep_for(std::chrono::milliseconds(3500));
  EXPECT_EQ(client.GetState(), ConnectionState::LoggedIn);
  client.Disconnect();
}

TEST_F(ChatClientLoopbackTest, UnansweredHeartbeatsKillTheConnectionAndReconnect) {
  std::atomic<int> pings{0};
  FakeGateway gateway([&pings](const chirp::gateway::Packet& pkt, auto send) {
    if (pkt.msg_id() == chirp::gateway::HEARTBEAT_PING) {
      ++pings;  // observed but never answered: the missed-pong counter
    }
    if (pkt.msg_id() == chirp::gateway::LOGIN_REQ) {
      chirp::auth::LoginResponse resp;
      resp.set_code(chirp::common::OK);
      resp.set_user_id("u");
      chirp::gateway::Packet out;
      out.set_msg_id(chirp::gateway::LOGIN_RESP);
      out.set_sequence(pkt.sequence());
      out.set_body(resp.SerializeAsString());
      send(out);
    }
    // Pings never answered: the second missed pong ends the connection.
  });

  ChatClient client(LoopbackConfig(gateway.port(), /*heartbeat_s=*/1));
  client.Connect();
  WaitState(client, ConnectionState::Connected);

  // t=1 ping1; t=2 miss 1 + ping2; t=3 miss 2 -> reconnect. Wait for the
  // loss and the first backoff attempt to land back at Connected.
  WaitState(client, ConnectionState::WaitingReconnect);
  WaitState(client, ConnectionState::Connected);
  // Two unanswered pings (the max_missed_pongs=2 budget) is what killed the
  // first connection; a silent wait would not prove pings were ever sent.
  EXPECT_GE(pings.load(), 2);
  EXPECT_EQ(client.GetState(), ConnectionState::Connected);
  client.Disconnect();
}

// ---------------------------------------------------------------------------
// Reconnect schedule and give-up paths.
// ---------------------------------------------------------------------------

// The backoff helper lives in an internal header precisely so this schedule
// walk does not need tens of seconds of live reconnect time.
TEST(BackoffScheduleTest, DoublesThenCapsWithJitter) {
  // attempt 0: the 500ms base with ±20% jitter.
  for (int i = 0; i < 50; ++i) {
    const auto d = chirp::sdk::internal::BackoffDelayMs(0);
    EXPECT_GE(d, 400);
    EXPECT_LE(d, 600);
  }
  // Doubling: attempt 3 is 8x base within the jitter band.
  for (int i = 0; i < 50; ++i) {
    const auto d = chirp::sdk::internal::BackoffDelayMs(3);
    EXPECT_GE(d, 3200);
    EXPECT_LE(d, 4800);
  }
  // The cap: every attempt past the doubling horizon clamps to 15s (jittered).
  for (const int attempt : {6, 20, 1000}) {
    const auto d = chirp::sdk::internal::BackoffDelayMs(attempt);
    EXPECT_GE(d, 12000);
    EXPECT_LE(d, 18000);
  }
}

TEST_F(ChatClientLoopbackTest, ReconnectGivesUpAfterMaxAttempts) {
  auto gateway = std::make_unique<FakeGateway>(
      [](const chirp::gateway::Packet& pkt, auto send) {
        if (pkt.msg_id() == chirp::gateway::LOGIN_REQ) {
          chirp::auth::LoginResponse resp;
          resp.set_code(chirp::common::OK);
          resp.set_user_id("u");
          chirp::gateway::Packet out;
          out.set_msg_id(chirp::gateway::LOGIN_RESP);
          out.set_sequence(pkt.sequence());
          out.set_body(resp.SerializeAsString());
          send(out);
        }
      });
  ChatConfig config = LoopbackConfig(gateway->port(), /*heartbeat_s=*/30);
  config.max_reconnect_attempts = 1;  // one automatic reconnect, then give up
  ChatClient client(config);
  client.Connect();
  WaitState(client, ConnectionState::Connected);
  LoginSync(client, "t");

  // Destroying the gateway closes both the live connection and the listener,
  // so the automatic reconnect dials a dead port.
  gateway.reset();

  // Loss -> WaitingReconnect (~0.5s backoff) -> failed dial -> attempt cap ->
  // Disconnected, and it must stay down instead of dialing forever.
  const auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(5);
  while (std::chrono::steady_clock::now() < deadline &&
         client.GetState() != ConnectionState::Disconnected) {
    std::this_thread::sleep_for(std::chrono::milliseconds(5));
  }
  EXPECT_EQ(client.GetState(), ConnectionState::Disconnected);
  std::this_thread::sleep_for(std::chrono::milliseconds(1200));
  EXPECT_EQ(client.GetState(), ConnectionState::Disconnected);
  client.Disconnect();
}

TEST_F(ChatClientLoopbackTest, DisconnectDuringWaitingReconnectStaysDown) {
  FakeGateway gateway([](const chirp::gateway::Packet& pkt, auto send) {
    if (pkt.msg_id() == chirp::gateway::LOGIN_REQ) {
      chirp::auth::LoginResponse resp;
      resp.set_code(chirp::common::OK);
      resp.set_user_id("u");
      chirp::gateway::Packet out;
      out.set_msg_id(chirp::gateway::LOGIN_RESP);
      out.set_sequence(pkt.sequence());
      out.set_body(resp.SerializeAsString());
      send(out);
    }
  });

  ChatClient client(LoopbackConfig(gateway.port(), /*heartbeat_s=*/30));
  client.Connect();
  WaitState(client, ConnectionState::Connected);
  LoginSync(client, "t");
  gateway.DropConnections();
  WaitState(client, ConnectionState::WaitingReconnect);
  client.Disconnect();  // cancels the pending reconnect timer

  // The gateway is still listening: had the reconnect timer fired, the client
  // would come back. A user-driven disconnect means it stays down.
  std::this_thread::sleep_for(std::chrono::milliseconds(1200));
  EXPECT_EQ(client.GetState(), ConnectionState::Disconnected);
}

TEST_F(ChatClientLoopbackTest, PongWithoutSequenceIsIgnored) {
  FakeGateway gateway([](const chirp::gateway::Packet& pkt, auto send) {
    if (pkt.msg_id() == chirp::gateway::LOGIN_REQ) {
      chirp::auth::LoginResponse resp;
      resp.set_code(chirp::common::OK);
      resp.set_user_id("u");
      chirp::gateway::Packet out;
      out.set_msg_id(chirp::gateway::LOGIN_RESP);
      out.set_sequence(pkt.sequence());
      out.set_body(resp.SerializeAsString());
      send(out);
    }
  });

  ChatClient client(LoopbackConfig(gateway.port(), /*heartbeat_s=*/30));
  client.Connect();
  WaitState(client, ConnectionState::Connected);
  LoginSync(client, "t");

  // A pong without the echoed sequence is protocol noise: warned about and
  // dropped, never taken as heartbeat credit nor as a stray response.
  chirp::gateway::HeartbeatPong pong;
  chirp::gateway::Packet out;
  out.set_msg_id(chirp::gateway::HEARTBEAT_PONG);  // sequence stays 0
  out.set_body(pong.SerializeAsString());
  gateway.Push(out);

  std::this_thread::sleep_for(std::chrono::milliseconds(200));
  EXPECT_EQ(client.GetState(), ConnectionState::LoggedIn);
  client.Disconnect();
}

TEST_F(ChatClientLoopbackTest, SendMessageWithEmptyReceiverIsDropped) {
  std::atomic<int> sends{0};
  FakeGateway gateway([&sends](const chirp::gateway::Packet& pkt, auto send) {
    if (pkt.msg_id() == chirp::gateway::LOGIN_REQ) {
      chirp::auth::LoginResponse resp;
      resp.set_code(chirp::common::OK);
      resp.set_user_id("u");
      chirp::gateway::Packet out;
      out.set_msg_id(chirp::gateway::LOGIN_RESP);
      out.set_sequence(pkt.sequence());
      out.set_body(resp.SerializeAsString());
      send(out);
    }
    if (pkt.msg_id() == chirp::gateway::SEND_MESSAGE_REQ) {
      ++sends;
    }
  });

  ChatClient client(LoopbackConfig(gateway.port()));
  client.Connect();
  WaitState(client, ConnectionState::Connected);
  LoginSync(client, "t");
  client.SendMessage("", "ghost");  // empty receiver: dropped client-side
  std::this_thread::sleep_for(std::chrono::milliseconds(300));
  EXPECT_EQ(sends.load(), 0);
  EXPECT_EQ(client.GetState(), ConnectionState::LoggedIn);
  client.Disconnect();
}

TEST_F(SdkClientTest, RequestWhenNotConnectedReportsNotConnected) {
  ChatClient client(TcpConfig());
  std::promise<std::error_code> done;
  client.Request(chirp::gateway::GET_HISTORY_REQ, chirp::gateway::GET_HISTORY_RESP, "{}",
                 [&done](const std::error_code& ec, const std::string&) {
                   done.set_value(ec);
                 });
  auto future = done.get_future();
  ASSERT_EQ(future.wait_for(std::chrono::milliseconds(5000)), std::future_status::ready);
  EXPECT_EQ(future.get(), chirp::sdk::make_error_code(ChatError::NotConnected));
}

TEST_F(ChatClientLoopbackTest, UnknownNotifyIsDispatchedAndIgnored) {
  FakeGateway gateway([](const chirp::gateway::Packet&, auto) {});

  ChatClient client(LoopbackConfig(gateway.port()));
  client.Connect();
  WaitState(client, ConnectionState::Connected);

  // A sequence-less frame with a msg id nobody handles reaches the generic
  // notify dispatch; with no subscriber it is dropped and the connection
  // stays up.
  chirp::gateway::Packet out;
  out.set_msg_id(static_cast<chirp::gateway::MsgID>(9999));  // sequence stays 0
  out.set_body("\x01\x02");
  gateway.Push(out);

  std::this_thread::sleep_for(std::chrono::milliseconds(200));
  EXPECT_EQ(client.GetState(), ConnectionState::Connected);
  client.Disconnect();
}

// ---------------------------------------------------------------------------
// 钩子接口接线(MessageInterceptor / ChatEventListener / AuthProvider /
// CommandHandler / MessageStore)。所有钩子在 io 线程触发,测试用
// promise/mutex 与测试线程同步。
// ---------------------------------------------------------------------------

// 记录型监听器:捕获状态序列与生命周期事件。
class RecordingListener : public chirp::sdk::ChatEventListener {
 public:
  void OnConnectionStateChanged(int state) override {
    std::lock_guard<std::mutex> lock(mu);
    states.push_back(state);
  }
  void OnLoginResult(int code, const std::string& user_id) override {
    std::lock_guard<std::mutex> lock(mu);
    login_codes.push_back(code);
    login_users.push_back(user_id);
  }
  void OnKicked(const std::string& reason) override {
    std::lock_guard<std::mutex> lock(mu);
    kick_reasons.push_back(reason);
  }
  void OnReconnecting(int attempt, int delay_ms) override {
    std::lock_guard<std::mutex> lock(mu);
    reconnectings.emplace_back(attempt, delay_ms);
  }
  void OnReconnected() override {
    std::lock_guard<std::mutex> lock(mu);
    ++reconnected;
  }
  void OnMessageReceived(const chirp::chat::ChatMessage& msg) override {
    std::lock_guard<std::mutex> lock(mu);
    messages.push_back(msg.content());
  }

  std::mutex mu;
  std::vector<int> states;
  std::vector<int> login_codes;
  std::vector<std::string> login_users;
  std::vector<std::string> kick_reasons;
  std::vector<std::pair<int, int>> reconnectings;
  std::vector<std::string> messages;
  int reconnected = 0;
};

// 脚本化拦截器:用 std::function 定制两个拦截点,回调触发打旗标。
class ScriptedInterceptor : public chirp::sdk::MessageInterceptor {
 public:
  std::function<bool(chirp::chat::SendMessageRequest&)> on_before_send;
  std::function<bool(chirp::chat::ChatMessage&)> on_before_receive;
  std::atomic<bool> before_send_called{false};
  std::atomic<bool> after_send_called{false};
  std::atomic<bool> after_receive_called{false};

  bool OnBeforeSend(chirp::chat::SendMessageRequest& msg) override {
    before_send_called = true;
    return on_before_send ? on_before_send(msg) : true;
  }
  void OnAfterSend(const chirp::chat::SendMessageRequest&) override {
    after_send_called = true;
  }
  bool OnBeforeReceive(chirp::chat::ChatMessage& msg) override {
    return on_before_receive ? on_before_receive(msg) : true;
  }
  void OnAfterReceive(const chirp::chat::ChatMessage&) override {
    after_receive_called = true;
  }
};

// 固定 token 的认证提供者,记录 GetToken/OnTokenExpired/OnAuthResult 调用。
class ScriptedAuthProvider : public chirp::sdk::AuthProvider {
 public:
  explicit ScriptedAuthProvider(std::string token) : token_(std::move(token)) {}

  std::string GetToken() override {
    ++get_token_calls;
    return token_;
  }
  void OnTokenExpired(std::function<void(const std::string&)> renew) override {
    // 先写 renew 再发布 expired_calls,测试线程轮询到计数即可安全取用。
    last_renew = std::move(renew);
    ++expired_calls;
  }
  void OnAuthResult(int code, const std::string& user_id) override {
    std::lock_guard<std::mutex> lock(mu);
    auth_results.emplace_back(code, user_id);
  }

  std::atomic<int> get_token_calls{0};
  std::atomic<int> expired_calls{0};
  std::function<void(const std::string&)> last_renew;
  std::mutex mu;
  std::vector<std::pair<int, std::string>> auth_results;

 private:
  std::string token_;
};

// 最小命令处理器:记录 Execute 入参,返回值可脚本化。
class RecordingCommand : public chirp::sdk::CommandHandler {
 public:
  RecordingCommand(std::string name, bool result)
      : name_(std::move(name)), result_(result) {}

  std::string GetName() const override { return name_; }
  std::string GetDescription() const override { return "test command"; }
  bool Execute(const std::string& args, const std::string& sender_id) override {
    std::lock_guard<std::mutex> lock(mu);
    executed_args.push_back(args);
    executed_senders.push_back(sender_id);
    return result_;
  }

  std::mutex mu;
  std::vector<std::string> executed_args;
  std::vector<std::string> executed_senders;

 private:
  std::string name_;
  bool result_;
};

// 服务端看到的 SEND_MESSAGE_REQ 计数(带互斥的内容捕获)。
struct SendCapture {
  std::mutex mu;
  int count = 0;
  std::vector<std::string> contents;

  void Record(const chirp::chat::SendMessageRequest& req) {
    std::lock_guard<std::mutex> lock(mu);
    ++count;
    contents.push_back(req.content());
  }
  int Count() {
    std::lock_guard<std::mutex> lock(mu);
    return count;
  }
};

// 登录响应脚本:第 n 次 LOGIN_REQ 回 code_n;记录收到的 token。
struct LoginScript {
  std::mutex mu;
  int count = 0;
  std::vector<std::string> tokens;
  std::vector<chirp::common::ErrorCode> codes;

  // 记录请求并返回应答。codes 未覆盖的请求默认回 OK。
  chirp::auth::LoginResponse OnLogin(const chirp::gateway::Packet& pkt) {
    chirp::auth::LoginRequest req;
    req.ParseFromArray(pkt.body().data(), static_cast<int>(pkt.body().size()));
    chirp::common::ErrorCode code = chirp::common::OK;
    {
      std::lock_guard<std::mutex> lock(mu);
      tokens.push_back(req.token());
      if (codes.size() > static_cast<size_t>(count)) {
        code = codes[static_cast<size_t>(count)];
      }
      ++count;
    }
    chirp::auth::LoginResponse resp;
    resp.set_code(code);
    resp.set_user_id("user-1");
    resp.set_session_id("sess-1");
    return resp;
  }
};

chirp::gateway::Packet MakeLoginRespPacket(uint32_t sequence,
                                           const chirp::auth::LoginResponse& resp) {
  chirp::gateway::Packet out;
  out.set_msg_id(chirp::gateway::LOGIN_RESP);
  out.set_sequence(sequence);
  out.set_body(resp.SerializeAsString());
  return out;
}

chirp::gateway::Packet MakeChatNotifyPacket(const chirp::chat::ChatMessage& msg) {
  chirp::gateway::Packet out;
  out.set_msg_id(chirp::gateway::CHAT_MESSAGE_NOTIFY);
  out.set_sequence(0);
  out.set_body(msg.SerializeAsString());
  return out;
}

void PushWorldMessage(FakeGateway& gateway, const std::string& content) {
  chirp::chat::ChatMessage msg;
  msg.set_message_id("m-" + content);
  msg.set_sender_id("bob");
  msg.set_channel_type(chirp::chat::WORLD);
  msg.set_channel_id("world");
  msg.set_content(content);
  gateway.Push(MakeChatNotifyPacket(msg));
}

TEST_F(ChatClientLoopbackTest, InterceptorModifiesOutgoingRequest) {
  SendCapture sends;
  FakeGateway gateway([&](const chirp::gateway::Packet& pkt, auto send) {
    if (pkt.msg_id() == chirp::gateway::LOGIN_REQ) {
      chirp::auth::LoginResponse resp;
      resp.set_code(chirp::common::OK);
      resp.set_user_id("user-1");
      send(MakeLoginRespPacket(pkt.sequence(), resp));
    }
    if (pkt.msg_id() == chirp::gateway::SEND_MESSAGE_REQ) {
      chirp::chat::SendMessageRequest req;
      req.ParseFromArray(pkt.body().data(), static_cast<int>(pkt.body().size()));
      sends.Record(req);
    }
  });

  auto interceptor = std::make_shared<ScriptedInterceptor>();
  interceptor->on_before_send = [](chirp::chat::SendMessageRequest& req) {
    req.set_content("[checked] " + req.content());
    return true;
  };

  ChatClient client(LoopbackConfig(gateway.port()));
  client.SetMessageInterceptor(interceptor);
  client.Connect();
  WaitState(client, ConnectionState::Connected);
  LoginSync(client, "t");

  client.SendMessage("bob", "hello");
  std::this_thread::sleep_for(std::chrono::milliseconds(300));

  ASSERT_EQ(sends.Count(), 1);
  std::vector<std::string> contents;
  {
    std::lock_guard<std::mutex> lock(sends.mu);
    contents = sends.contents;
  }
  EXPECT_EQ(contents[0], "[checked] hello");
  client.Disconnect();
}

TEST_F(ChatClientLoopbackTest, InterceptorBlocksSendAndSkipsAfterSend) {
  SendCapture sends;
  FakeGateway gateway([&](const chirp::gateway::Packet& pkt, auto send) {
    if (pkt.msg_id() == chirp::gateway::LOGIN_REQ) {
      chirp::auth::LoginResponse resp;
      resp.set_code(chirp::common::OK);
      resp.set_user_id("user-1");
      send(MakeLoginRespPacket(pkt.sequence(), resp));
    }
    if (pkt.msg_id() == chirp::gateway::SEND_MESSAGE_REQ) {
      chirp::chat::SendMessageRequest req;
      req.ParseFromArray(pkt.body().data(), static_cast<int>(pkt.body().size()));
      sends.Record(req);
    }
  });

  auto interceptor = std::make_shared<ScriptedInterceptor>();
  interceptor->on_before_send = [](chirp::chat::SendMessageRequest&) {
    return false;
  };

  ChatClient client(LoopbackConfig(gateway.port()));
  client.SetMessageInterceptor(interceptor);
  client.Connect();
  WaitState(client, ConnectionState::Connected);
  LoginSync(client, "t");

  client.SendMessage("bob", "blocked");
  std::this_thread::sleep_for(std::chrono::milliseconds(300));

  EXPECT_EQ(sends.Count(), 0);
  EXPECT_FALSE(interceptor->after_send_called.load());
  client.Disconnect();
}

TEST_F(ChatClientLoopbackTest, AfterSendFiresOnceRequestIsWritten) {
  FakeGateway gateway([&](const chirp::gateway::Packet& pkt, auto send) {
    if (pkt.msg_id() == chirp::gateway::LOGIN_REQ) {
      chirp::auth::LoginResponse resp;
      resp.set_code(chirp::common::OK);
      resp.set_user_id("user-1");
      send(MakeLoginRespPacket(pkt.sequence(), resp));
    }
  });

  auto interceptor = std::make_shared<ScriptedInterceptor>();
  ChatClient client(LoopbackConfig(gateway.port()));
  client.SetMessageInterceptor(interceptor);
  client.Connect();
  WaitState(client, ConnectionState::Connected);
  LoginSync(client, "t");

  client.SendMessage("bob", "plain");
  for (int i = 0; i < 2500 && !interceptor->after_send_called.load(); ++i) {
    std::this_thread::sleep_for(std::chrono::milliseconds(2));
  }
  EXPECT_TRUE(interceptor->before_send_called.load());
  EXPECT_TRUE(interceptor->after_send_called.load());
  client.Disconnect();
}

TEST_F(ChatClientLoopbackTest, InterceptorRewritesIncomingMessageButNotRawFrame) {
  FakeGateway gateway([&](const chirp::gateway::Packet& pkt, auto send) {
    if (pkt.msg_id() == chirp::gateway::LOGIN_REQ) {
      chirp::auth::LoginResponse resp;
      resp.set_code(chirp::common::OK);
      resp.set_user_id("user-1");
      send(MakeLoginRespPacket(pkt.sequence(), resp));
    }
  });

  auto interceptor = std::make_shared<ScriptedInterceptor>();
  interceptor->on_before_receive = [](chirp::chat::ChatMessage& msg) {
    msg.set_content("clean " + msg.content());
    return true;
  };

  ChatClient client(LoopbackConfig(gateway.port()));
  client.SetMessageInterceptor(interceptor);
  client.Connect();
  WaitState(client, ConnectionState::Connected);
  LoginSync(client, "t");

  std::promise<std::string> shown;
  auto shown_future = shown.get_future();
  client.SetMessageCallback([&](const std::string&, const std::string& content) {
    shown.set_value(content);
  });

  std::promise<std::string> raw_body;
  auto raw_future = raw_body.get_future();
  client.OnNotify(chirp::gateway::CHAT_MESSAGE_NOTIFY,
                  [&](const std::string& body) { raw_body.set_value(body); });

  PushWorldMessage(gateway, "dirty");

  ASSERT_EQ(shown_future.wait_for(std::chrono::milliseconds(kWaitMs)),
            std::future_status::ready);
  EXPECT_EQ(shown_future.get(), "clean dirty");

  // 原始分发不受拦截器改写影响:OnNotify 拿到的仍是 wire body。
  ASSERT_EQ(raw_future.wait_for(std::chrono::milliseconds(kWaitMs)),
            std::future_status::ready);
  chirp::chat::ChatMessage raw;
  ASSERT_TRUE(raw.ParseFromString(raw_future.get()));
  EXPECT_EQ(raw.content(), "dirty");
  EXPECT_TRUE(interceptor->after_receive_called.load());
  client.Disconnect();
}

TEST_F(ChatClientLoopbackTest, InterceptorDropsIncomingMessageEntirely) {
  FakeGateway gateway([&](const chirp::gateway::Packet& pkt, auto send) {
    if (pkt.msg_id() == chirp::gateway::LOGIN_REQ) {
      chirp::auth::LoginResponse resp;
      resp.set_code(chirp::common::OK);
      resp.set_user_id("user-1");
      send(MakeLoginRespPacket(pkt.sequence(), resp));
    }
  });

  auto interceptor = std::make_shared<ScriptedInterceptor>();
  interceptor->on_before_receive = [](chirp::chat::ChatMessage&) {
    return false;
  };

  ChatClient client(LoopbackConfig(gateway.port()));
  client.SetMessageInterceptor(interceptor);
  client.SetMessageStore(std::make_unique<chirp::sdk::MemoryMessageStore>());
  auto listener = std::make_shared<RecordingListener>();
  client.AddListener(listener);
  client.Connect();
  WaitState(client, ConnectionState::Connected);
  LoginSync(client, "t");

  std::atomic<int> shown{0};
  client.SetMessageCallback([&](const std::string&, const std::string&) {
    ++shown;
  });
  std::atomic<int> raw{0};
  client.OnNotify(chirp::gateway::CHAT_MESSAGE_NOTIFY,
                  [&](const std::string&) { ++raw; });

  PushWorldMessage(gateway, "spam");
  std::this_thread::sleep_for(std::chrono::milliseconds(300));

  EXPECT_EQ(shown.load(), 0);
  EXPECT_EQ(raw.load(), 0);
  EXPECT_FALSE(interceptor->after_receive_called.load());
  EXPECT_TRUE(client.LoadHistory(chirp::chat::WORLD, "world", 10).empty());
  {
    std::lock_guard<std::mutex> lock(listener->mu);
    EXPECT_TRUE(listener->messages.empty());
  }
  client.Disconnect();
}

TEST_F(ChatClientLoopbackTest, ListenerSeesLifecycleAndReconnectSequence) {
  FakeGateway gateway([&](const chirp::gateway::Packet& pkt, auto send) {
    if (pkt.msg_id() == chirp::gateway::LOGIN_REQ) {
      chirp::auth::LoginResponse resp;
      resp.set_code(chirp::common::OK);
      resp.set_user_id("user-1");
      send(MakeLoginRespPacket(pkt.sequence(), resp));
    }
  });

  ChatClient client(LoopbackConfig(gateway.port()));
  auto listener = std::make_shared<RecordingListener>();
  client.AddListener(listener);
  client.Connect();
  WaitState(client, ConnectionState::Connected);
  LoginSync(client, "t");

  gateway.DropConnections();
  // CI coverage runner 的 io 线程可能被抢占秒级(断线回调、重连成功后的
  // OnReconnected fan-out 都在 io 线程),轮询窗口给足 3s;正常路径毫秒级
  // 即 break,窗口只在调度被饿时兜底。
  for (int i = 0; i < 1500; ++i) {
    {
      std::lock_guard<std::mutex> lock(listener->mu);
      if (!listener->reconnectings.empty()) {
        break;
      }
    }
    std::this_thread::sleep_for(std::chrono::milliseconds(2));
  }
  {
    std::lock_guard<std::mutex> lock(listener->mu);
    ASSERT_FALSE(listener->reconnectings.empty());
    EXPECT_EQ(listener->reconnectings[0].first, 1);
    // 500ms 基础退避 ±20% 抖动。
    EXPECT_GE(listener->reconnectings[0].second, 400);
    EXPECT_LE(listener->reconnectings[0].second, 600);
  }
  WaitState(client, ConnectionState::Connected);
  for (int i = 0; i < 1500; ++i) {
    std::lock_guard<std::mutex> lock(listener->mu);
    if (listener->reconnected > 0) {
      break;
    }
    std::this_thread::sleep_for(std::chrono::milliseconds(2));
  }
  {
    std::lock_guard<std::mutex> lock(listener->mu);
    // Connecting -> Connected -> LoggedIn -> WaitingReconnect -> 重连
    // Connecting -> Connected。
    const std::vector<int> want = {
        static_cast<int>(ConnectionState::Connecting),
        static_cast<int>(ConnectionState::Connected),
        static_cast<int>(ConnectionState::LoggedIn),
        static_cast<int>(ConnectionState::WaitingReconnect),
        static_cast<int>(ConnectionState::Connecting),
        static_cast<int>(ConnectionState::Connected),
    };
    EXPECT_EQ(listener->states, want);
    EXPECT_EQ(listener->reconnected, 1);
  }
  client.Disconnect();
}

TEST_F(ChatClientLoopbackTest, ListenerSeesKickedTerminalState) {
  FakeGateway gateway([&](const chirp::gateway::Packet& pkt, auto send) {
    if (pkt.msg_id() == chirp::gateway::LOGIN_REQ) {
      chirp::auth::LoginResponse resp;
      resp.set_code(chirp::common::OK);
      resp.set_user_id("user-1");
      send(MakeLoginRespPacket(pkt.sequence(), resp));

      chirp::auth::KickNotify kick;
      kick.set_reason("banned");
      chirp::gateway::Packet kp;
      kp.set_msg_id(chirp::gateway::KICK_NOTIFY);
      kp.set_sequence(0);
      kp.set_body(kick.SerializeAsString());
      send(kp);
    }
  });

  ChatClient client(LoopbackConfig(gateway.port()));
  auto listener = std::make_shared<RecordingListener>();
  client.AddListener(listener);
  client.Connect();
  WaitState(client, ConnectionState::Connected);
  LoginSync(client, "t");

  for (int i = 0; i < 2500; ++i) {
    {
      std::lock_guard<std::mutex> lock(listener->mu);
      if (!listener->kick_reasons.empty()) {
        break;
      }
    }
    std::this_thread::sleep_for(std::chrono::milliseconds(2));
  }
  {
    std::lock_guard<std::mutex> lock(listener->mu);
    ASSERT_FALSE(listener->kick_reasons.empty());
    EXPECT_EQ(listener->kick_reasons[0], "banned");
    ASSERT_FALSE(listener->states.empty());
    EXPECT_EQ(listener->states.back(),
              static_cast<int>(ConnectionState::Kicked));
  }
  client.Disconnect();
}

TEST_F(ChatClientLoopbackTest, ListenerSeesLoginResultCodes) {
  FakeGateway gateway([&](const chirp::gateway::Packet& pkt, auto send) {
    if (pkt.msg_id() == chirp::gateway::LOGIN_REQ) {
      chirp::auth::LoginResponse resp;
      resp.set_code(chirp::common::AUTH_FAILED);
      send(MakeLoginRespPacket(pkt.sequence(), resp));
    }
  });

  ChatClient client(LoopbackConfig(gateway.port()));
  auto listener = std::make_shared<RecordingListener>();
  client.AddListener(listener);
  client.Connect();
  WaitState(client, ConnectionState::Connected);

  std::promise<std::error_code> login_promise;
  auto login_future = login_promise.get_future();
  client.Login("bad", [&login_promise](const std::error_code& ec, const std::string&) {
    login_promise.set_value(ec);
  });
  ASSERT_EQ(login_future.wait_for(std::chrono::milliseconds(kWaitMs)),
            std::future_status::ready);
  EXPECT_EQ(login_future.get(), make_error_code(ChatError::LoginFailed));
  {
    std::lock_guard<std::mutex> lock(listener->mu);
    ASSERT_EQ(listener->login_codes.size(), 1u);
    EXPECT_EQ(listener->login_codes[0],
              static_cast<int>(chirp::common::AUTH_FAILED));
    EXPECT_EQ(listener->login_users[0], "");
  }
  client.Disconnect();
}

TEST_F(ChatClientLoopbackTest, LoginBodyParseFailureReportsUnknownCode) {
  FakeGateway gateway([&](const chirp::gateway::Packet& pkt, auto send) {
    if (pkt.msg_id() == chirp::gateway::LOGIN_REQ) {
      chirp::gateway::Packet out;
      out.set_msg_id(chirp::gateway::LOGIN_RESP);
      out.set_sequence(pkt.sequence());
      out.set_body("not-a-login-response");
      send(out);
    }
  });

  ChatClient client(LoopbackConfig(gateway.port()));
  auto listener = std::make_shared<RecordingListener>();
  client.AddListener(listener);
  client.Connect();
  WaitState(client, ConnectionState::Connected);

  std::promise<std::error_code> login_promise;
  auto login_future = login_promise.get_future();
  client.Login("t", [&login_promise](const std::error_code& ec, const std::string&) {
    login_promise.set_value(ec);
  });
  ASSERT_EQ(login_future.wait_for(std::chrono::milliseconds(kWaitMs)),
            std::future_status::ready);
  EXPECT_EQ(login_future.get(), make_error_code(ChatError::LoginFailed));
  {
    std::lock_guard<std::mutex> lock(listener->mu);
    ASSERT_EQ(listener->login_codes.size(), 1u);
    EXPECT_EQ(listener->login_codes[0], -1);
  }
  client.Disconnect();
}

TEST_F(ChatClientLoopbackTest, AuthProviderSuppliesTokenForEmptyLogin) {
  LoginScript script;
  FakeGateway gateway([&](const chirp::gateway::Packet& pkt, auto send) {
    if (pkt.msg_id() == chirp::gateway::LOGIN_REQ) {
      send(MakeLoginRespPacket(pkt.sequence(), script.OnLogin(pkt)));
    }
  });

  auto provider = std::make_shared<ScriptedAuthProvider>("tok-from-provider");
  ChatClient client(LoopbackConfig(gateway.port()));
  client.SetAuthProvider(provider);
  auto listener = std::make_shared<RecordingListener>();
  client.AddListener(listener);
  client.Connect();
  WaitState(client, ConnectionState::Connected);

  std::promise<std::error_code> login_promise;
  auto login_future = login_promise.get_future();
  client.Login("", [&login_promise](const std::error_code& ec, const std::string&) {
    login_promise.set_value(ec);
  });
  ASSERT_EQ(login_future.wait_for(std::chrono::milliseconds(kWaitMs)),
            std::future_status::ready);
  EXPECT_FALSE(login_future.get());
  EXPECT_EQ(provider->get_token_calls.load(), 1);
  {
    std::lock_guard<std::mutex> lock(script.mu);
    EXPECT_EQ(script.tokens, std::vector<std::string>{"tok-from-provider"});
  }
  {
    std::lock_guard<std::mutex> lock(listener->mu);
    ASSERT_EQ(listener->login_codes.size(), 1u);
    EXPECT_EQ(listener->login_codes[0], static_cast<int>(chirp::common::OK));
    EXPECT_EQ(listener->login_users[0], "user-1");
  }
  client.Disconnect();
}

// AUTH_FAILED -> 续期(renew) -> 重登成功的完整链路。
TEST_F(ChatClientLoopbackTest, AuthProviderRenewRecoversFromExpiredToken) {
  LoginScript script;
  {
    std::lock_guard<std::mutex> lock(script.mu);
    script.codes = {chirp::common::AUTH_FAILED, chirp::common::OK};
  }
  FakeGateway gateway([&](const chirp::gateway::Packet& pkt, auto send) {
    if (pkt.msg_id() == chirp::gateway::LOGIN_REQ) {
      send(MakeLoginRespPacket(pkt.sequence(), script.OnLogin(pkt)));
    }
  });

  auto provider = std::make_shared<ScriptedAuthProvider>("stale-token");
  ChatClient client(LoopbackConfig(gateway.port()));
  client.SetAuthProvider(provider);
  auto listener = std::make_shared<RecordingListener>();
  client.AddListener(listener);
  client.Connect();
  WaitState(client, ConnectionState::Connected);

  std::promise<std::error_code> login_promise;
  auto login_future = login_promise.get_future();
  client.Login("", [&login_promise](const std::error_code& ec, const std::string&) {
    login_promise.set_value(ec);
  });

  // 游戏侧异步刷新 token 后调 renew。
  for (int i = 0; i < 2500 && provider->expired_calls.load() == 0; ++i) {
    std::this_thread::sleep_for(std::chrono::milliseconds(2));
  }
  ASSERT_EQ(provider->expired_calls.load(), 1);
  ASSERT_TRUE(provider->last_renew);
  provider->last_renew("fresh-token");

  ASSERT_EQ(login_future.wait_for(std::chrono::milliseconds(kWaitMs)),
            std::future_status::ready);
  EXPECT_FALSE(login_future.get());
  {
    std::lock_guard<std::mutex> lock(script.mu);
    EXPECT_EQ(script.tokens,
              (std::vector<std::string>{"stale-token", "fresh-token"}));
  }
  {
    std::lock_guard<std::mutex> lock(listener->mu);
    // 两次判定:先 AUTH_FAILED(续期前),再 OK。
    const std::vector<int> want_codes = {
        static_cast<int>(chirp::common::AUTH_FAILED),
        static_cast<int>(chirp::common::OK),
    };
    EXPECT_EQ(listener->login_codes, want_codes);
    EXPECT_EQ(listener->login_users, (std::vector<std::string>{"", "user-1"}));
  }
  std::vector<std::pair<int, std::string>> auth_results;
  {
    std::lock_guard<std::mutex> lock(provider->mu);
    auth_results = provider->auth_results;
  }
  ASSERT_EQ(auth_results.size(), 2u);
  EXPECT_EQ(auth_results[0],
            std::make_pair(static_cast<int>(chirp::common::AUTH_FAILED),
                           std::string("")));
  EXPECT_EQ(auth_results[1],
            std::make_pair(static_cast<int>(chirp::common::OK),
                           std::string("user-1")));
  client.Disconnect();
}

// 游戏不调 renew:续期超时后按设计文档进入 Disconnected 并回报失败。
TEST_F(ChatClientLoopbackTest, AuthProviderUnrenewedLoginTimesOutToDisconnected) {
  ChatConfig config = LoopbackConfig(0);  // port 稍后填
  config.request_timeout_ms = 200;

  LoginScript script;
  {
    std::lock_guard<std::mutex> lock(script.mu);
    script.codes = {chirp::common::AUTH_FAILED};
  }
  FakeGateway gateway([&](const chirp::gateway::Packet& pkt, auto send) {
    if (pkt.msg_id() == chirp::gateway::LOGIN_REQ) {
      send(MakeLoginRespPacket(pkt.sequence(), script.OnLogin(pkt)));
    }
  });
  config.gateway_port = gateway.port();

  auto provider = std::make_shared<ScriptedAuthProvider>("stale-token");
  ChatClient client(config);
  client.SetAuthProvider(provider);
  client.Connect();
  WaitState(client, ConnectionState::Connected);

  std::promise<std::error_code> login_promise;
  auto login_future = login_promise.get_future();
  client.Login("", [&login_promise](const std::error_code& ec, const std::string&) {
    login_promise.set_value(ec);
  });

  ASSERT_EQ(login_future.wait_for(std::chrono::milliseconds(kWaitMs)),
            std::future_status::ready);
  EXPECT_EQ(login_future.get(), make_error_code(ChatError::LoginFailed));
  EXPECT_EQ(provider->expired_calls.load(), 1);
  // 超时路径先 SetState 再回调,这里状态必已是 Disconnected。
  EXPECT_EQ(client.GetState(), ConnectionState::Disconnected);
}

// 续期后仍 AUTH_FAILED:不再触发第二次 OnTokenExpired,直接失败。
TEST_F(ChatClientLoopbackTest, AuthProviderRenewedFailureDoesNotRenewAgain) {
  LoginScript script;
  {
    std::lock_guard<std::mutex> lock(script.mu);
    script.codes = {chirp::common::AUTH_FAILED, chirp::common::AUTH_FAILED};
  }
  FakeGateway gateway([&](const chirp::gateway::Packet& pkt, auto send) {
    if (pkt.msg_id() == chirp::gateway::LOGIN_REQ) {
      send(MakeLoginRespPacket(pkt.sequence(), script.OnLogin(pkt)));
    }
  });

  auto provider = std::make_shared<ScriptedAuthProvider>("stale-token");
  ChatClient client(LoopbackConfig(gateway.port()));
  client.SetAuthProvider(provider);
  client.Connect();
  WaitState(client, ConnectionState::Connected);

  std::promise<std::error_code> login_promise;
  auto login_future = login_promise.get_future();
  client.Login("", [&login_promise](const std::error_code& ec, const std::string&) {
    login_promise.set_value(ec);
  });

  for (int i = 0; i < 2500 && provider->expired_calls.load() == 0; ++i) {
    std::this_thread::sleep_for(std::chrono::milliseconds(2));
  }
  ASSERT_EQ(provider->expired_calls.load(), 1);
  provider->last_renew("fresh-token");

  ASSERT_EQ(login_future.wait_for(std::chrono::milliseconds(kWaitMs)),
            std::future_status::ready);
  EXPECT_EQ(login_future.get(), make_error_code(ChatError::LoginFailed));
  std::this_thread::sleep_for(std::chrono::milliseconds(200));
  EXPECT_EQ(provider->expired_calls.load(), 1);  // 不二次续期
  client.Disconnect();
}

// 续期挂起期间发起新一轮 Login:旧回调以 LoginFailed 收尾,新链接管。
TEST_F(ChatClientLoopbackTest, AuthProviderPendingRenewalSupersededByNewLogin) {
  LoginScript script;
  {
    std::lock_guard<std::mutex> lock(script.mu);
    script.codes = {chirp::common::AUTH_FAILED, chirp::common::OK};
  }
  FakeGateway gateway([&](const chirp::gateway::Packet& pkt, auto send) {
    if (pkt.msg_id() == chirp::gateway::LOGIN_REQ) {
      send(MakeLoginRespPacket(pkt.sequence(), script.OnLogin(pkt)));
    }
  });

  auto provider = std::make_shared<ScriptedAuthProvider>("stale-token");
  ChatClient client(LoopbackConfig(gateway.port()));
  client.SetAuthProvider(provider);
  client.Connect();
  WaitState(client, ConnectionState::Connected);

  std::promise<std::error_code> first_done;
  auto first_future = first_done.get_future();
  client.Login("", [&first_done](const std::error_code& ec, const std::string&) {
    first_done.set_value(ec);
  });

  for (int i = 0; i < 2500 && provider->expired_calls.load() == 0; ++i) {
    std::this_thread::sleep_for(std::chrono::milliseconds(2));
  }
  ASSERT_EQ(provider->expired_calls.load(), 1);
  // 不调 renew,直接发起新登录(显式 token)。
  std::promise<std::error_code> second_done;
  auto second_future = second_done.get_future();
  client.Login("direct-token",
               [&second_done](const std::error_code& ec, const std::string&) {
                 second_done.set_value(ec);
               });

  ASSERT_EQ(first_future.wait_for(std::chrono::milliseconds(kWaitMs)),
            std::future_status::ready);
  EXPECT_EQ(first_future.get(), make_error_code(ChatError::LoginFailed));
  ASSERT_EQ(second_future.wait_for(std::chrono::milliseconds(kWaitMs)),
            std::future_status::ready);
  EXPECT_FALSE(second_future.get());
  {
    std::lock_guard<std::mutex> lock(script.mu);
    EXPECT_EQ(script.tokens,
              (std::vector<std::string>{"stale-token", "direct-token"}));
  }
  client.Disconnect();
}

// 超时已被判定后游戏侧才调 renew:迟到闭包必须被无视,不得复活状态机
// 或再发登录包。
TEST_F(ChatClientLoopbackTest, AuthProviderLateRenewIgnoredAfterTimeout) {
  ChatConfig config = LoopbackConfig(0);
  // 预算要远大于回环响应在 io 线程上的处理延迟:200ms 时高负载下通用
  // 请求超时会先于 AUTH_FAILED 响应处理落地,登录以 Timeout(6) 收尾、
  // 续期路径(expired_calls)根本没走到。2s 只影响"游戏不调 renew"的
  // 等待窗,迟到续期断言在 login 完成后才发生,预算放宽不弱化语义。
  config.request_timeout_ms = 2000;

  LoginScript script;
  {
    std::lock_guard<std::mutex> lock(script.mu);
    script.codes = {chirp::common::AUTH_FAILED};
  }
  FakeGateway gateway([&](const chirp::gateway::Packet& pkt, auto send) {
    if (pkt.msg_id() == chirp::gateway::LOGIN_REQ) {
      send(MakeLoginRespPacket(pkt.sequence(), script.OnLogin(pkt)));
    }
  });
  config.gateway_port = gateway.port();

  auto provider = std::make_shared<ScriptedAuthProvider>("stale-token");
  ChatClient client(config);
  client.SetAuthProvider(provider);
  client.Connect();
  WaitState(client, ConnectionState::Connected);

  std::promise<std::error_code> login_promise;
  auto login_future = login_promise.get_future();
  client.Login("", [&login_promise](const std::error_code& ec, const std::string&) {
    login_promise.set_value(ec);
  });
  ASSERT_EQ(login_future.wait_for(std::chrono::milliseconds(kWaitMs)),
            std::future_status::ready);
  EXPECT_EQ(login_future.get(), make_error_code(ChatError::LoginFailed));
  ASSERT_EQ(provider->expired_calls.load(), 1);

  // 超时判定后迟到续期:闭包照常 post,但必须原地返回。
  provider->last_renew("late-token");
  std::this_thread::sleep_for(std::chrono::milliseconds(50));
  EXPECT_EQ(client.GetState(), ConnectionState::Disconnected);
  EXPECT_EQ(provider->expired_calls.load(), 1);
  {
    std::lock_guard<std::mutex> lock(script.mu);
    EXPECT_EQ(script.tokens, std::vector<std::string>{"stale-token"});
  }
  client.Disconnect();
}

// 续期回调交回空 token:本轮登录以 LoginFailed 收尾,连接保持不拆。
TEST_F(ChatClientLoopbackTest, AuthProviderRenewWithEmptyTokenFailsLogin) {
  LoginScript script;
  {
    std::lock_guard<std::mutex> lock(script.mu);
    script.codes = {chirp::common::AUTH_FAILED};
  }
  FakeGateway gateway([&](const chirp::gateway::Packet& pkt, auto send) {
    if (pkt.msg_id() == chirp::gateway::LOGIN_REQ) {
      send(MakeLoginRespPacket(pkt.sequence(), script.OnLogin(pkt)));
    }
  });

  auto provider = std::make_shared<ScriptedAuthProvider>("stale-token");
  ChatClient client(LoopbackConfig(gateway.port()));
  client.SetAuthProvider(provider);
  client.Connect();
  WaitState(client, ConnectionState::Connected);

  std::promise<std::error_code> login_promise;
  auto login_future = login_promise.get_future();
  client.Login("", [&login_promise](const std::error_code& ec, const std::string&) {
    login_promise.set_value(ec);
  });

  for (int i = 0; i < 2500 && provider->expired_calls.load() == 0; ++i) {
    std::this_thread::sleep_for(std::chrono::milliseconds(2));
  }
  ASSERT_EQ(provider->expired_calls.load(), 1);
  provider->last_renew("");  // 游戏侧续期失败,交回空 token

  ASSERT_EQ(login_future.wait_for(std::chrono::milliseconds(kWaitMs)),
            std::future_status::ready);
  EXPECT_EQ(login_future.get(), make_error_code(ChatError::LoginFailed));
  // 与超时路径不同:活跃续期只报失败,不主动拆掉还活着的连接。
  EXPECT_EQ(client.GetState(), ConnectionState::Connected);
  EXPECT_EQ(provider->expired_calls.load(), 1);
  {
    std::lock_guard<std::mutex> lock(script.mu);
    EXPECT_EQ(script.tokens, std::vector<std::string>{"stale-token"});
  }
  client.Disconnect();
}

// 续期挂起期间显式断开:悬挂的 Login 回调以 Closed 收尾,不悬挂到超时。
TEST_F(ChatClientLoopbackTest, DisconnectFlushesPendingRenewal) {
  LoginScript script;
  {
    std::lock_guard<std::mutex> lock(script.mu);
    script.codes = {chirp::common::AUTH_FAILED};
  }
  FakeGateway gateway([&](const chirp::gateway::Packet& pkt, auto send) {
    if (pkt.msg_id() == chirp::gateway::LOGIN_REQ) {
      send(MakeLoginRespPacket(pkt.sequence(), script.OnLogin(pkt)));
    }
  });

  auto provider = std::make_shared<ScriptedAuthProvider>("stale-token");
  ChatClient client(LoopbackConfig(gateway.port()));
  client.SetAuthProvider(provider);
  client.Connect();
  WaitState(client, ConnectionState::Connected);

  std::promise<std::error_code> login_promise;
  auto login_future = login_promise.get_future();
  client.Login("", [&login_promise](const std::error_code& ec, const std::string&) {
    login_promise.set_value(ec);
  });

  for (int i = 0; i < 2500 && provider->expired_calls.load() == 0; ++i) {
    std::this_thread::sleep_for(std::chrono::milliseconds(2));
  }
  ASSERT_EQ(provider->expired_calls.load(), 1);

  client.Disconnect();
  ASSERT_EQ(login_future.wait_for(std::chrono::milliseconds(kWaitMs)),
            std::future_status::ready);
  EXPECT_EQ(login_future.get(), make_error_code(ChatError::Closed));
  // flush 回调先于 DoClose 之后的 SetState 执行,状态用等待收敛。
  WaitState(client, ConnectionState::Disconnected);
  EXPECT_EQ(client.GetState(), ConnectionState::Disconnected);
}

TEST_F(ChatClientLoopbackTest, CommandIsHandledLocallyWithoutSending) {
  SendCapture sends;
  FakeGateway gateway([&](const chirp::gateway::Packet& pkt, auto send) {
    if (pkt.msg_id() == chirp::gateway::LOGIN_REQ) {
      chirp::auth::LoginResponse resp;
      resp.set_code(chirp::common::OK);
      resp.set_user_id("user-1");
      send(MakeLoginRespPacket(pkt.sequence(), resp));
    }
    if (pkt.msg_id() == chirp::gateway::SEND_MESSAGE_REQ) {
      chirp::chat::SendMessageRequest req;
      req.ParseFromArray(pkt.body().data(), static_cast<int>(pkt.body().size()));
      sends.Record(req);
    }
  });

  // 所有权交给 SDK(唯一),测试侧用裸指针读取记录。
  auto* trade = new RecordingCommand("trade", true);
  ChatClient client(LoopbackConfig(gateway.port()));
  client.RegisterCommand(std::unique_ptr<chirp::sdk::CommandHandler>(trade));
  client.Connect();
  WaitState(client, ConnectionState::Connected);
  LoginSync(client, "t");

  client.SendMessage("bob", "/trade alice 100");
  std::this_thread::sleep_for(std::chrono::milliseconds(300));

  EXPECT_EQ(sends.Count(), 0);
  {
    std::lock_guard<std::mutex> lock(trade->mu);
    ASSERT_EQ(trade->executed_args.size(), 1u);
    EXPECT_EQ(trade->executed_args[0], "alice 100");
    EXPECT_EQ(trade->executed_senders[0], "user-1");
  }
  client.Disconnect();
}

// 命令名匹配但 Execute 返回 false,且没有其他 handler:消息本地丢弃。
TEST_F(ChatClientLoopbackTest, UnclaimedCommandIsDroppedLocally) {
  SendCapture sends;
  FakeGateway gateway([&](const chirp::gateway::Packet& pkt, auto send) {
    if (pkt.msg_id() == chirp::gateway::LOGIN_REQ) {
      chirp::auth::LoginResponse resp;
      resp.set_code(chirp::common::OK);
      resp.set_user_id("user-1");
      send(MakeLoginRespPacket(pkt.sequence(), resp));
    }
    if (pkt.msg_id() == chirp::gateway::SEND_MESSAGE_REQ) {
      chirp::chat::SendMessageRequest req;
      req.ParseFromArray(pkt.body().data(), static_cast<int>(pkt.body().size()));
      sends.Record(req);
    }
  });

  auto* trade = new RecordingCommand("trade", false);
  ChatClient client(LoopbackConfig(gateway.port()));
  client.RegisterCommand(std::unique_ptr<chirp::sdk::CommandHandler>(trade));
  client.Connect();
  WaitState(client, ConnectionState::Connected);
  LoginSync(client, "t");

  // "/trade"(无参数):命令名匹配、Execute 返回 false -> 本地丢弃。
  client.SendMessage("bob", "/trade");
  // "/dance":无 handler 认领 -> 本地丢弃,Execute 不会被调用。
  client.SendMessage("bob", "/dance");
  std::this_thread::sleep_for(std::chrono::milliseconds(300));

  EXPECT_EQ(sends.Count(), 0);
  {
    std::lock_guard<std::mutex> lock(trade->mu);
    ASSERT_EQ(trade->executed_args.size(), 1u);  // 只有 "/trade" 命中
    EXPECT_EQ(trade->executed_args[0], "");
  }
  client.Disconnect();
}

// 零命令注册:'/' 消息按普通文本发送(向后兼容)。
TEST_F(ChatClientLoopbackTest, SlashMessagePassesThroughWithoutCommands) {
  SendCapture sends;
  FakeGateway gateway([&](const chirp::gateway::Packet& pkt, auto send) {
    if (pkt.msg_id() == chirp::gateway::LOGIN_REQ) {
      chirp::auth::LoginResponse resp;
      resp.set_code(chirp::common::OK);
      resp.set_user_id("user-1");
      send(MakeLoginRespPacket(pkt.sequence(), resp));
    }
    if (pkt.msg_id() == chirp::gateway::SEND_MESSAGE_REQ) {
      chirp::chat::SendMessageRequest req;
      req.ParseFromArray(pkt.body().data(), static_cast<int>(pkt.body().size()));
      sends.Record(req);
    }
  });

  ChatClient client(LoopbackConfig(gateway.port()));
  client.Connect();
  WaitState(client, ConnectionState::Connected);
  LoginSync(client, "t");

  client.SendMessage("bob", "/dance");
  for (int i = 0; i < 2500 && sends.Count() == 0; ++i) {
    std::this_thread::sleep_for(std::chrono::milliseconds(2));
  }
  ASSERT_EQ(sends.Count(), 1);
  {
    std::lock_guard<std::mutex> lock(sends.mu);
    EXPECT_EQ(sends.contents[0], "/dance");
  }
  client.Disconnect();
}

// 收发消息都落 store;转发查询 newest-first。
TEST_F(ChatClientLoopbackTest, StoreSavesReceivedAndSentMessages) {
  FakeGateway gateway([&](const chirp::gateway::Packet& pkt, auto send) {
    if (pkt.msg_id() == chirp::gateway::LOGIN_REQ) {
      chirp::auth::LoginResponse resp;
      resp.set_code(chirp::common::OK);
      resp.set_user_id("user-1");
      send(MakeLoginRespPacket(pkt.sequence(), resp));
    }
  });

  ChatClient client(LoopbackConfig(gateway.port()));
  client.SetMessageStore(std::make_unique<chirp::sdk::MemoryMessageStore>());
  client.Connect();
  WaitState(client, ConnectionState::Connected);
  LoginSync(client, "t");

  PushWorldMessage(gateway, "recv-text");
  for (int i = 0; i < 2500; ++i) {
    if (!client.LoadHistory(chirp::chat::WORLD, "world", 10).empty()) {
      break;
    }
    std::this_thread::sleep_for(std::chrono::milliseconds(2));
  }
  const auto world = client.LoadHistory(chirp::chat::WORLD, "world", 10);
  ASSERT_EQ(world.size(), 1u);
  EXPECT_EQ(world[0].content(), "recv-text");

  client.SendMessage("bob", "sent-text");
  // user_id_="user-1" > "bob",私聊 channel_id 为 "bob|user-1"。
  const std::string private_channel = "bob|user-1";
  for (int i = 0; i < 2500; ++i) {
    if (!client.LoadHistory(chirp::chat::PRIVATE, private_channel, 10).empty()) {
      break;
    }
    std::this_thread::sleep_for(std::chrono::milliseconds(2));
  }
  const auto priv = client.LoadHistory(chirp::chat::PRIVATE, private_channel, 10);
  ASSERT_EQ(priv.size(), 1u);
  EXPECT_EQ(priv[0].content(), "sent-text");
  EXPECT_EQ(priv[0].receiver_id(), "bob");

  client.MarkRead(chirp::chat::PRIVATE, private_channel, priv[0].message_id());
  EXPECT_EQ(client.GetUnreadCount(chirp::chat::PRIVATE, private_channel), 0);
  // 清理"远期之前"的全部消息。
  client.CleanupMessages(9999999999999LL);
  EXPECT_TRUE(client.LoadHistory(chirp::chat::PRIVATE, private_channel, 10).empty());
  client.Disconnect();
}

TEST_F(ChatClientLoopbackTest, StorePassThroughWithoutStoreIsNoop) {
  // 端口 1 仅为占位:不调用 Connect(),不会有任何 TCP 活动。
  ChatClient client(LoopbackConfig(1));
  EXPECT_TRUE(client.LoadHistory(chirp::chat::WORLD, "world", 10).empty());
  EXPECT_EQ(client.GetUnreadCount(chirp::chat::WORLD, "world"), 0);
  client.MarkRead(chirp::chat::WORLD, "world", "m-1");
  client.CleanupMessages(0);
  SUCCEED();
}


// ---------------------------------------------------------------------------
// Batch C branch/function coverage probes.
// ---------------------------------------------------------------------------

// 119: re-Login while already LoggedIn takes the state-guard short-circuit
// (state != Connected true, state != LoggedIn false).
TEST_F(ChatClientLoopbackTest, LoginAgainWhileLoggedInStillComplets) {
  FakeGateway gateway([](const chirp::gateway::Packet& pkt, auto send) {
    if (pkt.msg_id() == chirp::gateway::LOGIN_REQ) {
      chirp::auth::LoginResponse resp;
      resp.set_code(chirp::common::OK);
      resp.set_user_id("user-1");
      resp.set_session_id("sess-1");
      send(MakeLoginRespPacket(pkt.sequence(), resp));
    }
  });
  ChatClient client(LoopbackConfig(gateway.port()));
  client.Connect();
  WaitState(client, ConnectionState::Connected);
  LoginSync(client, "t1");

  std::promise<std::error_code> second;
  auto future = second.get_future();
  client.Login("t2", [&second](const std::error_code& ec, const std::string&) {
    second.set_value(ec);
  });
  ASSERT_EQ(future.wait_for(std::chrono::milliseconds(kWaitMs)),
            std::future_status::ready);
  EXPECT_FALSE(future.get());
  EXPECT_EQ(client.GetState(), ConnectionState::LoggedIn);
  client.Disconnect();
}

// 293: Request while merely Connected (state != Connected false short-circuit)
// still reaches the gateway; also exercises a heap-sized body capture.
TEST_F(ChatClientLoopbackTest, RequestWhileConnectedBeforeLoginReachesGateway) {
  std::atomic<bool> seen{false};
  FakeGateway gateway([&](const chirp::gateway::Packet& pkt, auto send) {
    if (pkt.msg_id() == chirp::gateway::GET_HISTORY_REQ) {
      seen = true;
      chirp::gateway::Packet out;
      out.set_msg_id(chirp::gateway::GET_HISTORY_RESP);
      out.set_sequence(pkt.sequence());
      out.set_body("{}");
      send(out);
    }
  });
  ChatClient client(LoopbackConfig(gateway.port()));
  client.Connect();
  WaitState(client, ConnectionState::Connected);
  ASSERT_EQ(client.GetState(), ConnectionState::Connected);

  std::promise<std::error_code> done;
  auto future = done.get_future();
  const std::string body(120, 'b');
  client.Request(chirp::gateway::GET_HISTORY_REQ, chirp::gateway::GET_HISTORY_RESP,
                 body, [&done](const std::error_code& ec, const std::string&) {
                   done.set_value(ec);
                 });
  ASSERT_EQ(future.wait_for(std::chrono::milliseconds(kWaitMs)),
            std::future_status::ready);
  EXPECT_TRUE(seen.load());
  client.Disconnect();
}

// 292/293: Request with a null callback while disconnected must swallow the
// error silently (empty std::function capture + if (cb) false arm).
TEST_F(SdkClientTest, RequestWithNullCallbackIsSilentWhenNotConnected) {
  ChatClient client(TcpConfig());
  client.Request(chirp::gateway::GET_HISTORY_REQ, chirp::gateway::GET_HISTORY_RESP,
                 "", nullptr);
  std::this_thread::sleep_for(std::chrono::milliseconds(200));
  EXPECT_EQ(client.GetState(), ConnectionState::Disconnected);
}

// 107: heap-sized token capture on the Login lambda (string move/ctor arm).
TEST_F(SdkClientTest, LoginWithLongTokenReportsNotConnected) {
  ChatClient client(TcpConfig());
  const std::string long_token(80, 'x');
  std::promise<std::error_code> done;
  auto future = done.get_future();
  client.Login(long_token,
               [&done](const std::error_code& ec, const std::string&) {
                 done.set_value(ec);
               });
  ASSERT_EQ(future.wait_for(std::chrono::milliseconds(kWaitMs)),
            std::future_status::ready);
  EXPECT_EQ(future.get(), chirp::sdk::make_error_code(ChatError::NotConnected));
}

// 107: empty token + provider that also returns empty -> InvalidParam via
// the provider/GetToken path (provider present, effective stays empty).
TEST_F(SdkClientTest, LoginWithEmptyProviderTokenFailsInvalidParam) {
  ChatClient client(TcpConfig());
  client.SetAuthProvider(std::make_shared<ScriptedAuthProvider>(""));

  std::promise<std::error_code> done;
  auto future = done.get_future();
  client.Login("", [&done](const std::error_code& ec, const std::string&) {
    done.set_value(ec);
  });
  ASSERT_EQ(future.wait_for(std::chrono::milliseconds(kWaitMs)),
            std::future_status::ready);
  EXPECT_EQ(future.get(), chirp::sdk::make_error_code(ChatError::InvalidParam));
}

// 268: forward channel_id arm (user_id <= receiver, taken as "user-1" <=
// "zoe"), heap receiver capture, and empty content which skips the command
// predicate at 256 and still saves/sends.
TEST_F(ChatClientLoopbackTest, SendMessageForwardChannelAndEmptyContent) {
  SendCapture sends;
  std::mutex mu;
  std::vector<chirp::chat::SendMessageRequest> reqs;
  FakeGateway gateway([&](const chirp::gateway::Packet& pkt, auto send) {
    if (pkt.msg_id() == chirp::gateway::LOGIN_REQ) {
      chirp::auth::LoginResponse resp;
      resp.set_code(chirp::common::OK);
      resp.set_user_id("user-1");
      resp.set_session_id("sess-1");
      send(MakeLoginRespPacket(pkt.sequence(), resp));
    }
    if (pkt.msg_id() == chirp::gateway::SEND_MESSAGE_REQ) {
      chirp::chat::SendMessageRequest req;
      req.ParseFromArray(pkt.body().data(), static_cast<int>(pkt.body().size()));
      sends.Record(req);
      std::lock_guard<std::mutex> lock(mu);
      reqs.push_back(req);
    }
  });

  ChatClient client(LoopbackConfig(gateway.port()));
  client.SetMessageStore(std::make_unique<chirp::sdk::MemoryMessageStore>());
  client.Connect();
  WaitState(client, ConnectionState::Connected);
  LoginSync(client, "t");

  client.SendMessage("zoe", "hi");                        // forward, SSO ids
  client.SendMessage(std::string(60, 'z'), "hi");         // forward, heap receiver
  client.SendMessage("zoe", "");                          // empty content
  std::this_thread::sleep_for(std::chrono::milliseconds(500));

  ASSERT_EQ(sends.Count(), 3);
  {
    std::lock_guard<std::mutex> lock(mu);
    ASSERT_EQ(reqs.size(), 3u);
    EXPECT_EQ(reqs[0].channel_id(), "user-1|zoe");
    EXPECT_EQ(reqs[1].channel_id(), "user-1|" + std::string(60, 'z'));
    EXPECT_EQ(reqs[2].content(), "");
  }
  client.Disconnect();
}

// 694: the OnMessageReceived listener lambda inside HandleChatNotify only
// runs when a listener is registered AND a chat notify arrives unblocked.
TEST_F(ChatClientLoopbackTest, ListenerReceivesChatNotify) {
  FakeGateway gateway([](const chirp::gateway::Packet& pkt, auto send) {
    if (pkt.msg_id() == chirp::gateway::LOGIN_REQ) {
      chirp::auth::LoginResponse resp;
      resp.set_code(chirp::common::OK);
      resp.set_user_id("user-1");
      resp.set_session_id("sess-1");
      send(MakeLoginRespPacket(pkt.sequence(), resp));
    }
  });

  auto listener = std::make_shared<RecordingListener>();
  ChatClient client(LoopbackConfig(gateway.port()));
  client.AddListener(listener);
  client.Connect();
  WaitState(client, ConnectionState::Connected);
  LoginSync(client, "t");

  PushWorldMessage(gateway, "hello-listener");
  bool got = false;
  for (int i = 0; i < 150 && !got; ++i) {
    {
      std::lock_guard<std::mutex> lock(listener->mu);
      got = !listener->messages.empty();
    }
    if (!got) {
      std::this_thread::sleep_for(std::chrono::milliseconds(20));
    }
  }
  {
    std::lock_guard<std::mutex> lock(listener->mu);
    ASSERT_EQ(listener->messages.size(), 1u);
    EXPECT_EQ(listener->messages[0], "hello-listener");
  }
  client.Disconnect();
}

// 426: first handler misses so the loop continues to the second (hit);
// equal-length name mismatch exercises the string== internals; trailing
// space yields an empty args string via substr (418).
TEST_F(ChatClientLoopbackTest, SecondCommandHandlerClaimsAndEqualLengthMiss) {
  SendCapture sends;
  FakeGateway gateway([&](const chirp::gateway::Packet& pkt, auto send) {
    if (pkt.msg_id() == chirp::gateway::LOGIN_REQ) {
      chirp::auth::LoginResponse resp;
      resp.set_code(chirp::common::OK);
      resp.set_user_id("user-1");
      resp.set_session_id("sess-1");
      send(MakeLoginRespPacket(pkt.sequence(), resp));
    }
    if (pkt.msg_id() == chirp::gateway::SEND_MESSAGE_REQ) {
      chirp::chat::SendMessageRequest req;
      req.ParseFromArray(pkt.body().data(), static_cast<int>(pkt.body().size()));
      sends.Record(req);
    }
  });

  auto* abcd = new RecordingCommand("abcd", false);
  auto* trade = new RecordingCommand("trade", true);
  ChatClient client(LoopbackConfig(gateway.port()));
  client.RegisterCommand(std::unique_ptr<chirp::sdk::CommandHandler>(abcd));
  client.RegisterCommand(std::unique_ptr<chirp::sdk::CommandHandler>(trade));
  client.Connect();
  WaitState(client, ConnectionState::Connected);
  LoginSync(client, "t");

  client.SendMessage("bob", "/trade alice 1");  // first miss, second hit
  client.SendMessage("bob", "/abce");           // equal-length name miss
  client.SendMessage("bob", "/trade ");         // hit with empty substr args
  std::this_thread::sleep_for(std::chrono::milliseconds(400));

  EXPECT_EQ(sends.Count(), 0);
  {
    std::lock_guard<std::mutex> lock(abcd->mu);
    EXPECT_TRUE(abcd->executed_args.empty());
  }
  {
    std::lock_guard<std::mutex> lock(trade->mu);
    ASSERT_EQ(trade->executed_args.size(), 2u);
    EXPECT_EQ(trade->executed_args[0], "alice 1");
    EXPECT_EQ(trade->executed_args[1], "");
  }
  client.Disconnect();
}

// 600: a pong whose sequence does not match pending_ping_seq_ must be
// dropped without crediting the heartbeat.
TEST_F(ChatClientLoopbackTest, StalePongSequenceIsIgnored) {
  FakeGateway gateway([](const chirp::gateway::Packet& pkt, auto send) {
    if (pkt.msg_id() == chirp::gateway::LOGIN_REQ) {
      chirp::auth::LoginResponse resp;
      resp.set_code(chirp::common::OK);
      resp.set_user_id("user-1");
      resp.set_session_id("sess-1");
      send(MakeLoginRespPacket(pkt.sequence(), resp));
    }
    if (pkt.msg_id() == chirp::gateway::HEARTBEAT_PING) {
      chirp::gateway::HeartbeatPong pong;
      chirp::gateway::Packet out;
      out.set_msg_id(chirp::gateway::HEARTBEAT_PONG);
      out.set_sequence(pkt.sequence() + 1);  // stale: never matches
      out.set_body(pong.SerializeAsString());
      send(out);
    }
  });

  ChatClient client(LoopbackConfig(gateway.port(), /*heartbeat_s=*/1));
  client.Connect();
  WaitState(client, ConnectionState::Connected);
  LoginSync(client, "t");
  // First ping fires at ~1s; the mismatched pong leaves pending_ping_seq_
  // set. Disconnect before the second ping could trip death detection.
  std::this_thread::sleep_for(std::chrono::milliseconds(1500));
  EXPECT_EQ(client.GetState(), ConnectionState::LoggedIn);
  client.Disconnect();
}

// 89: second Connect() while the first is still Connecting hits the
// Connecting arm of the state guard; if the first already reached Connected
// it hits the Connected arm instead.
TEST_F(ChatClientLoopbackTest, ConnectWhileConnectingIsIgnored) {
  FakeGateway gateway([](const chirp::gateway::Packet&, auto) {});
  ChatClient client(LoopbackConfig(gateway.port()));
  client.Connect();
  client.Connect();  // runs right after the first post on the io thread
  // Poll instead of a fixed 150ms nap: under load the first Connect may not
  // have left Disconnected yet, which is the state the guard must have hit.
  for (int i = 0; i < kWaitMs / 2 && client.GetState() == ConnectionState::Disconnected;
       ++i) {
    std::this_thread::sleep_for(std::chrono::milliseconds(2));
  }
  EXPECT_NE(client.GetState(), ConnectionState::Disconnected);
  client.Disconnect();
}

// 88/89: second Connect() while LoggedIn takes the LoggedIn arm of the
// guard (the Connecting/Connected arms are covered by the probe above and
// ConnectWhileAlreadyConnectedIsIgnored).
TEST_F(ChatClientLoopbackTest, ConnectWhileLoggedInIsIgnored) {
  FakeGateway gateway([](const chirp::gateway::Packet& pkt, auto send) {
    if (pkt.msg_id() == chirp::gateway::LOGIN_REQ) {
      chirp::auth::LoginResponse resp;
      resp.set_code(chirp::common::OK);
      resp.set_user_id("user-1");
      resp.set_session_id("sess-1");
      send(MakeLoginRespPacket(pkt.sequence(), resp));
    }
  });
  ChatClient client(LoopbackConfig(gateway.port()));
  client.Connect();
  WaitState(client, ConnectionState::Connected);
  LoginSync(client, "t");
  ASSERT_EQ(client.GetState(), ConnectionState::LoggedIn);
  client.Connect();  // ignored: state is LoggedIn
  std::this_thread::sleep_for(std::chrono::milliseconds(150));
  EXPECT_EQ(client.GetState(), ConnectionState::LoggedIn);
  client.Disconnect();
}


// 426: a null handler entry still passes HasCommands() (non-empty vector)
// and must be skipped by the `if (handler && ...)` guard in the dispatch
// loop.
TEST_F(ChatClientLoopbackTest, NullCommandHandlerIsSkippedInDispatch) {
  SendCapture sends;
  FakeGateway gateway([&](const chirp::gateway::Packet& pkt, auto send) {
    if (pkt.msg_id() == chirp::gateway::LOGIN_REQ) {
      chirp::auth::LoginResponse resp;
      resp.set_code(chirp::common::OK);
      resp.set_user_id("user-1");
      resp.set_session_id("sess-1");
      send(MakeLoginRespPacket(pkt.sequence(), resp));
    }
    if (pkt.msg_id() == chirp::gateway::SEND_MESSAGE_REQ) {
      chirp::chat::SendMessageRequest req;
      req.ParseFromArray(pkt.body().data(), static_cast<int>(pkt.body().size()));
      sends.Record(req);
    }
  });

  auto* trade = new RecordingCommand("trade", true);
  ChatClient client(LoopbackConfig(gateway.port()));
  client.RegisterCommand(std::unique_ptr<chirp::sdk::CommandHandler>(nullptr));
  client.RegisterCommand(std::unique_ptr<chirp::sdk::CommandHandler>(trade));
  client.Connect();
  WaitState(client, ConnectionState::Connected);
  LoginSync(client, "t");

  client.SendMessage("bob", "/trade x");
  std::this_thread::sleep_for(std::chrono::milliseconds(300));
  EXPECT_EQ(sends.Count(), 0);
  {
    std::lock_guard<std::mutex> lock(trade->mu);
    ASSERT_EQ(trade->executed_args.size(), 1u);
    EXPECT_EQ(trade->executed_args[0], "x");
  }
  client.Disconnect();
}


// Large (heap-allocated) std::function captures exercise the non-SBO move
// arm of the closure construction inside asio::post on the Login/Request
// lines (107/292).
struct BigCallbackCapture {
  char pad[160]{};
};

TEST_F(SdkClientTest, LoginWithHeapFunctionCallbackReportsNotConnected) {
  ChatClient client(TcpConfig());
  BigCallbackCapture big;
  std::promise<std::error_code> done;
  auto future = done.get_future();
  client.Login("t", [big, &done](const std::error_code& ec, const std::string&) {
    done.set_value(ec);
  });
  ASSERT_EQ(future.wait_for(std::chrono::milliseconds(kWaitMs)),
            std::future_status::ready);
  EXPECT_EQ(future.get(), chirp::sdk::make_error_code(ChatError::NotConnected));
}

TEST_F(SdkClientTest, RequestWithHeapFunctionCallbackReportsNotConnected) {
  ChatClient client(TcpConfig());
  BigCallbackCapture big;
  std::promise<std::error_code> done;
  auto future = done.get_future();
  client.Request(chirp::gateway::GET_HISTORY_REQ, chirp::gateway::GET_HISTORY_RESP,
                 "{}", [big, &done](const std::error_code& ec, const std::string&) {
                   done.set_value(ec);
                 });
  ASSERT_EQ(future.wait_for(std::chrono::milliseconds(kWaitMs)),
            std::future_status::ready);
  EXPECT_EQ(future.get(), chirp::sdk::make_error_code(ChatError::NotConnected));
}

// ---------------------------------------------------------------------------
// Convenience API(类型化请求-响应便捷方法)端到端:每个方法至少一条往返
// 用例(请求字段透传 + 响应解析);本地参数校验、NotConnected、BadResponse、
// 超时各有一条专属用例。ec 契约:只报传输/协议错误,业务码读 resp.code()。
// ---------------------------------------------------------------------------

// 同步发起一次便捷调用:等待回调,返回响应体,ec 写回 ec_out。
template <typename Resp>
Resp WaitConvenienceRpc(
    const std::function<void(std::function<void(const std::error_code&, const Resp&)>)>& invoke,
    std::error_code& ec_out, int wait_ms) {
  std::promise<Resp> done;
  auto future = done.get_future();
  invoke([&](const std::error_code& ec, const Resp& resp) {
    ec_out = ec;
    done.set_value(resp);
  });
  EXPECT_EQ(future.wait_for(std::chrono::milliseconds(wait_ms)), std::future_status::ready);
  return future.get();
}

class ConvenienceApiTest : public ChatClientLoopbackTest {
 protected:
  // 登录脚本化;LOGIN 之外的包交给用例提供的 dispatcher(未匹配的帧忽略,
  // 以免 TearDown 的 LOGOUT_REQ 触发误断言)。
  void StartGateway(FramedHandler dispatcher) {
    gateway_ = std::make_unique<FakeGateway>(
        [dispatcher = std::move(dispatcher)](const chirp::gateway::Packet& pkt, auto send) {
          if (pkt.msg_id() == chirp::gateway::LOGIN_REQ) {
            chirp::auth::LoginResponse resp;
            resp.set_code(chirp::common::OK);
            resp.set_user_id("sdk-user");
            resp.set_session_id("sess-1");
            chirp::gateway::Packet out;
            out.set_msg_id(chirp::gateway::LOGIN_RESP);
            out.set_sequence(pkt.sequence());
            out.set_body(resp.SerializeAsString());
            send(out);
            return;
          }
          dispatcher(pkt, std::move(send));
        });
  }

  void ConnectAndLogin() {
    client_ = std::make_unique<ChatClient>(LoopbackConfig(gateway_->port()));
    client_->Connect();
    WaitState(*client_, ConnectionState::Connected, kWaitMs);
    ASSERT_EQ(client_->GetState(), ConnectionState::Connected);
    std::promise<std::error_code> done;
    auto future = done.get_future();
    client_->Login("tok", [&](const std::error_code& ec, const std::string& uid) {
      EXPECT_EQ(uid, "sdk-user");
      done.set_value(ec);
    });
    ASSERT_EQ(future.wait_for(std::chrono::milliseconds(kWaitMs)), std::future_status::ready);
    ASSERT_FALSE(future.get());
    ASSERT_EQ(client_->GetState(), ConnectionState::LoggedIn);
  }

  void TearDown() override {
    if (client_) {
      client_->Disconnect();
      WaitState(*client_, ConnectionState::Disconnected, 2000);
      client_.reset();
    }
    gateway_.reset();
  }

  // 响应帧样板:回显 sequence,挂 resp_msg_id 与序列化 body。
  static chirp::gateway::Packet RespFor(const chirp::gateway::Packet& req,
                                        chirp::gateway::MsgID resp_msg_id,
                                        const std::string& body) {
    chirp::gateway::Packet out;
    out.set_msg_id(resp_msg_id);
    out.set_sequence(req.sequence());
    out.set_body(body);
    return out;
  }

  template <typename Resp>
  Resp WaitRpc(const std::function<void(std::function<void(const std::error_code&, const Resp&)>)>& invoke,
               std::error_code& ec_out) {
    return WaitConvenienceRpc<Resp>(invoke, ec_out, kWaitMs);
  }

  std::unique_ptr<FakeGateway> gateway_;
  std::unique_ptr<ChatClient> client_;
};

TEST_F(ConvenienceApiTest, SendOptionsPrivateCarriesReplyAndNormalizesChannelId) {
  std::string seen_receiver, seen_channel, seen_reply, seen_content;
  int seen_channel_type = -1;
  StartGateway([&](const chirp::gateway::Packet& pkt, auto send) {
    if (pkt.msg_id() != chirp::gateway::SEND_MESSAGE_REQ) {
      return;
    }
    chirp::chat::SendMessageRequest req;
    ASSERT_TRUE(req.ParseFromString(pkt.body()));
    seen_receiver = req.receiver_id();
    seen_channel = req.channel_id();
    seen_reply = req.reply_to_message_id();
    seen_content = req.content();
    seen_channel_type = req.channel_type();
    chirp::chat::SendMessageResponse resp;
    resp.set_code(chirp::common::OK);
    resp.set_message_id("m-42");
    send(RespFor(pkt, chirp::gateway::SEND_MESSAGE_RESP, resp.SerializeAsString()));
  });
  ConnectAndLogin();

  ChatClient::SendOptions opts;
  opts.receiver_id = "alice";
  opts.reply_to_message_id = "orig-1";
  std::error_code ec;
  auto resp = WaitRpc<chirp::chat::SendMessageResponse>([&](auto cb) {
    client_->SendMessage(opts, "hello there", cb);
  }, ec);
  ASSERT_FALSE(ec);
  EXPECT_EQ(resp.code(), chirp::common::OK);
  EXPECT_EQ(resp.message_id(), "m-42");
  // 归一化契约:"alice" <= "sdk-user" → "alice|sdk-user"(与服务端一致)。
  EXPECT_EQ(seen_receiver, "alice");
  EXPECT_EQ(seen_channel, "alice|sdk-user");
  EXPECT_EQ(seen_reply, "orig-1");
  EXPECT_EQ(seen_content, "hello there");
  EXPECT_EQ(seen_channel_type, chirp::chat::PRIVATE);
}

TEST_F(ConvenienceApiTest, SendOptionsNonPrivateCarriesExplicitChannelId) {
  std::string seen_channel;
  int seen_type = -1;
  StartGateway([&](const chirp::gateway::Packet& pkt, auto send) {
    if (pkt.msg_id() != chirp::gateway::SEND_MESSAGE_REQ) {
      return;
    }
    chirp::chat::SendMessageRequest req;
    ASSERT_TRUE(req.ParseFromString(pkt.body()));
    seen_channel = req.channel_id();
    seen_type = req.channel_type();
    chirp::chat::SendMessageResponse resp;
    resp.set_code(chirp::common::OK);
    resp.set_message_id("m-8");
    send(RespFor(pkt, chirp::gateway::SEND_MESSAGE_RESP, resp.SerializeAsString()));
  });
  ConnectAndLogin();

  // 非 PRIVATE 通道不走归一化:显式 channel_id 原样透传。
  ChatClient::SendOptions opts;
  opts.channel_type = chirp::chat::WORLD;
  opts.channel_id = "world-0";
  std::error_code ec;
  (void)WaitRpc<chirp::chat::SendMessageResponse>([&](auto cb) {
    client_->SendMessage(opts, "gg", cb);
  }, ec);
  EXPECT_FALSE(ec);
  EXPECT_EQ(seen_channel, "world-0");
  EXPECT_EQ(seen_type, chirp::chat::WORLD);
}

TEST_F(ConvenienceApiTest, SendOptionsValidatesLocallyWithoutServerRoundTrip) {
  std::atomic<int> non_login_packets{0};
  StartGateway([&](const chirp::gateway::Packet&, auto) { ++non_login_packets; });
  ConnectAndLogin();

  const auto invalid = chirp::sdk::make_error_code(chirp::sdk::ChatError::InvalidParam);
  std::error_code ec;
  (void)WaitRpc<chirp::chat::SendMessageResponse>([&](auto cb) {
    ChatClient::SendOptions world_without_channel;
    world_without_channel.channel_type = chirp::chat::WORLD;
    client_->SendMessage(world_without_channel, "hi", cb);
  }, ec);
  EXPECT_EQ(ec, invalid);

  (void)WaitRpc<chirp::chat::SendMessageResponse>([&](auto cb) {
    ChatClient::SendOptions private_without_receiver;
    client_->SendMessage(private_without_receiver, "hi", cb);
  }, ec);
  EXPECT_EQ(ec, invalid);

  (void)WaitRpc<chirp::chat::SendMessageResponse>([&](auto cb) {
    ChatClient::SendOptions opts;
    opts.receiver_id = "alice";
    client_->SendMessage(opts, "", cb);
  }, ec);
  EXPECT_EQ(ec, invalid);

  std::this_thread::sleep_for(std::chrono::milliseconds(100));
  EXPECT_EQ(non_login_packets.load(), 0);
}

TEST_F(ConvenienceApiTest, SendOptionsBusinessCodeStaysInResponseNotErrorCode) {
  StartGateway([&](const chirp::gateway::Packet& pkt, auto send) {
    if (pkt.msg_id() != chirp::gateway::SEND_MESSAGE_REQ) {
      return;
    }
    chirp::chat::SendMessageResponse resp;
    resp.set_code(chirp::common::CONTENT_TOO_LONG);
    send(RespFor(pkt, chirp::gateway::SEND_MESSAGE_RESP, resp.SerializeAsString()));
  });
  ConnectAndLogin();

  ChatClient::SendOptions opts;
  opts.receiver_id = "alice";
  std::error_code ec;
  auto resp = WaitRpc<chirp::chat::SendMessageResponse>([&](auto cb) {
    client_->SendMessage(opts, "way too long...", cb);
  }, ec);
  // 业务拒绝不折进 ec:传输层 OK,结果读 resp.code()。
  EXPECT_FALSE(ec);
  EXPECT_EQ(resp.code(), chirp::common::CONTENT_TOO_LONG);
}

TEST_F(ConvenienceApiTest, OptionsSendRunsInterceptorAndLocalStore) {
  std::string seen_content;
  StartGateway([&](const chirp::gateway::Packet& pkt, auto send) {
    if (pkt.msg_id() != chirp::gateway::SEND_MESSAGE_REQ) {
      return;
    }
    chirp::chat::SendMessageRequest req;
    ASSERT_TRUE(req.ParseFromString(pkt.body()));
    seen_content = req.content();
    chirp::chat::SendMessageResponse resp;
    resp.set_code(chirp::common::OK);
    resp.set_message_id("m-7");
    send(RespFor(pkt, chirp::gateway::SEND_MESSAGE_RESP, resp.SerializeAsString()));
  });
  ConnectAndLogin();

  auto interceptor = std::make_shared<ScriptedInterceptor>();
  interceptor->on_before_send = [](chirp::chat::SendMessageRequest& msg) {
    msg.set_content("censored");
    return true;
  };
  client_->SetMessageInterceptor(interceptor);
  auto store = std::make_unique<chirp::sdk::MemoryMessageStore>();
  auto* store_ptr = store.get();
  client_->SetMessageStore(std::move(store));

  ChatClient::SendOptions opts;
  opts.receiver_id = "alice";
  opts.reply_to_message_id = "orig-9";
  std::error_code ec;
  (void)WaitRpc<chirp::chat::SendMessageResponse>([&](auto cb) {
    client_->SendMessage(opts, "secret", cb);
  }, ec);
  ASSERT_FALSE(ec);
  // 拦截器改写后上服务端;本地存档保留改写内容与引用字段(同一归一化 key)。
  EXPECT_EQ(seen_content, "censored");
  const auto local = store_ptr->Load(chirp::chat::PRIVATE, "alice|sdk-user", 10);
  ASSERT_FALSE(local.empty());
  EXPECT_EQ(local.front().content(), "censored");
  EXPECT_EQ(local.front().reply_to_message_id(), "orig-9");
}

TEST_F(ConvenienceApiTest, WordFilterRewritesContentBeforeWireAndArchive) {
  std::string seen_content;
  StartGateway([&](const chirp::gateway::Packet& pkt, auto send) {
    if (pkt.msg_id() != chirp::gateway::SEND_MESSAGE_REQ) {
      return;
    }
    chirp::chat::SendMessageRequest req;
    ASSERT_TRUE(req.ParseFromString(pkt.body()));
    seen_content = req.content();
    chirp::chat::SendMessageResponse resp;
    resp.set_code(chirp::common::OK);
    resp.set_message_id("m-8");
    send(RespFor(pkt, chirp::gateway::SEND_MESSAGE_RESP, resp.SerializeAsString()));
  });
  ConnectAndLogin();

  chirp::sdk::WordFilterOptions opts;
  opts.terms = chirp::sdk::ParseWordLexicon({"damn"});
  client_->SetMessageInterceptor(
      std::make_shared<chirp::sdk::WordFilterInterceptor>(opts));
  auto store = std::make_unique<chirp::sdk::MemoryMessageStore>();
  auto* store_ptr = store.get();
  client_->SetMessageStore(std::move(store));

  ChatClient::SendOptions send_opts;
  send_opts.receiver_id = "alice";
  std::error_code ec;
  (void)WaitRpc<chirp::chat::SendMessageResponse>([&](auto cb) {
    client_->SendMessage(send_opts, "well damn, hi", cb);
  }, ec);
  ASSERT_FALSE(ec);
  // 服务端与本地存档拿到的都是改写后的内容。
  EXPECT_EQ(seen_content, "well **, hi");
  const auto local = store_ptr->Load(chirp::chat::PRIVATE, "alice|sdk-user", 10);
  ASSERT_FALSE(local.empty());
  EXPECT_EQ(local.front().content(), "well **, hi");
}

TEST_F(ConvenienceApiTest, WordFilterRejectStopsSendWithoutServerRoundTrip) {
  std::atomic<int> sends{0};
  StartGateway([&](const chirp::gateway::Packet& pkt, auto) {
    if (pkt.msg_id() == chirp::gateway::SEND_MESSAGE_REQ) {
      ++sends;
    }
  });
  ConnectAndLogin();

  chirp::sdk::WordFilterOptions opts;
  opts.terms = {"banned"};
  opts.policy = chirp::sdk::WordFilterPolicy::kReject;
  client_->SetMessageInterceptor(
      std::make_shared<chirp::sdk::WordFilterInterceptor>(opts));

  ChatClient::SendOptions send_opts;
  send_opts.receiver_id = "alice";
  std::error_code ec;
  (void)WaitRpc<chirp::chat::SendMessageResponse>([&](auto cb) {
    client_->SendMessage(send_opts, "banned goods", cb);
  }, ec);
  EXPECT_EQ(ec, chirp::sdk::make_error_code(chirp::sdk::ChatError::SendFailed));
  std::this_thread::sleep_for(std::chrono::milliseconds(100));
  EXPECT_EQ(sends.load(), 0);
}

TEST_F(ConvenienceApiTest, OptionsSendBlockedByInterceptorReportsSendFailed) {
  std::atomic<int> sends{0};
  StartGateway([&](const chirp::gateway::Packet& pkt, auto) {
    if (pkt.msg_id() == chirp::gateway::SEND_MESSAGE_REQ) {
      ++sends;
    }
  });
  ConnectAndLogin();

  auto interceptor = std::make_shared<ScriptedInterceptor>();
  interceptor->on_before_send = [](chirp::chat::SendMessageRequest&) { return false; };
  client_->SetMessageInterceptor(interceptor);

  ChatClient::SendOptions opts;
  opts.receiver_id = "alice";
  std::error_code ec;
  (void)WaitRpc<chirp::chat::SendMessageResponse>([&](auto cb) {
    client_->SendMessage(opts, "banned", cb);
  }, ec);
  // 拦截器拒绝以传输层错误上报(本地未发包),不产生服务端往返。
  EXPECT_EQ(ec, chirp::sdk::make_error_code(chirp::sdk::ChatError::SendFailed));
  std::this_thread::sleep_for(std::chrono::milliseconds(100));
  EXPECT_EQ(sends.load(), 0);
}

TEST_F(ConvenienceApiTest, FetchHistoryRoundTrip) {
  std::string seen_user, seen_channel;
  int seen_type = -1, seen_limit = -1;
  int64_t seen_before = -1;
  StartGateway([&](const chirp::gateway::Packet& pkt, auto send) {
    if (pkt.msg_id() != chirp::gateway::GET_HISTORY_REQ) {
      return;
    }
    chirp::chat::GetHistoryRequest req;
    ASSERT_TRUE(req.ParseFromString(pkt.body()));
    seen_user = req.user_id();
    seen_channel = req.channel_id();
    seen_type = req.channel_type();
    seen_limit = req.limit();
    seen_before = req.before_timestamp();
    chirp::chat::GetHistoryResponse resp;
    resp.set_code(chirp::common::OK);
    resp.set_has_more(true);
    auto* msg = resp.add_messages();
    msg->set_message_id("old-1");
    msg->set_content("first");
    send(RespFor(pkt, chirp::gateway::GET_HISTORY_RESP, resp.SerializeAsString()));
  });
  ConnectAndLogin();

  std::error_code ec;
  auto resp = WaitRpc<chirp::chat::GetHistoryResponse>([&](auto cb) {
    client_->FetchHistory(chirp::chat::GUILD, "g-1", 25, 12345, cb);
  }, ec);
  ASSERT_FALSE(ec);
  EXPECT_EQ(resp.code(), chirp::common::OK);
  ASSERT_EQ(resp.messages_size(), 1);
  EXPECT_EQ(resp.messages(0).message_id(), "old-1");
  EXPECT_TRUE(resp.has_more());
  EXPECT_EQ(seen_user, "sdk-user");
  EXPECT_EQ(seen_channel, "g-1");
  EXPECT_EQ(seen_type, chirp::chat::GUILD);
  EXPECT_EQ(seen_limit, 25);
  EXPECT_EQ(seen_before, 12345);
}

TEST_F(ConvenienceApiTest, MarkChannelReadAndFetchUnreadCountRoundTrip) {
  StartGateway([&](const chirp::gateway::Packet& pkt, auto send) {
    if (pkt.msg_id() == chirp::gateway::MARK_READ_REQ) {
      chirp::chat::MarkReadRequest req;
      ASSERT_TRUE(req.ParseFromString(pkt.body()));
      EXPECT_EQ(req.user_id(), "sdk-user");
      EXPECT_EQ(req.channel_id(), "ch-1");
      EXPECT_EQ(req.channel_type(), chirp::chat::WORLD);
      EXPECT_EQ(req.message_id(), "msg-99");
      chirp::chat::MarkReadResponse resp;
      resp.set_code(chirp::common::OK);
      send(RespFor(pkt, chirp::gateway::MARK_READ_RESP, resp.SerializeAsString()));
      return;
    }
    if (pkt.msg_id() == chirp::gateway::GET_UNREAD_COUNT_REQ) {
      chirp::chat::GetUnreadCountResponse resp;
      resp.set_code(chirp::common::OK);
      resp.set_total_unread(7);
      auto* channel = resp.add_channels();
      channel->set_channel_id("ch-1");
      channel->set_count(5);
      send(RespFor(pkt, chirp::gateway::GET_UNREAD_COUNT_RESP, resp.SerializeAsString()));
      return;
    }
  });
  ConnectAndLogin();

  std::error_code ec;
  (void)WaitRpc<chirp::chat::MarkReadResponse>([&](auto cb) {
    client_->MarkChannelRead(chirp::chat::WORLD, "ch-1", "msg-99", cb);
  }, ec);
  EXPECT_FALSE(ec);
  const auto unread = WaitRpc<chirp::chat::GetUnreadCountResponse>([&](auto cb) {
    client_->FetchUnreadCount(cb);
  }, ec);
  EXPECT_FALSE(ec);
  EXPECT_EQ(unread.total_unread(), 7);
  ASSERT_EQ(unread.channels_size(), 1);
  EXPECT_EQ(unread.channels(0).count(), 5);
}

TEST_F(ConvenienceApiTest, BlocklistRoundTrip) {
  StartGateway([&](const chirp::gateway::Packet& pkt, auto send) {
    switch (pkt.msg_id()) {
    case chirp::gateway::BLOCK_MESSAGE_SENDER_REQ: {
      chirp::chat::BlockMessageSenderRequest req;
      ASSERT_TRUE(req.ParseFromString(pkt.body()));
      EXPECT_EQ(req.target_user_id(), "troll");
      chirp::chat::BlockMessageSenderResponse resp;
      resp.set_code(chirp::common::OK);
      resp.set_target_user_id("troll");
      send(RespFor(pkt, chirp::gateway::BLOCK_MESSAGE_SENDER_RESP, resp.SerializeAsString()));
      return;
    }
    case chirp::gateway::UNBLOCK_MESSAGE_SENDER_REQ: {
      chirp::chat::UnblockMessageSenderResponse resp;
      resp.set_code(chirp::common::OK);
      resp.set_target_user_id("troll");
      send(RespFor(pkt, chirp::gateway::UNBLOCK_MESSAGE_SENDER_RESP, resp.SerializeAsString()));
      return;
    }
    case chirp::gateway::GET_BLOCKED_SENDERS_REQ: {
      chirp::chat::GetBlockedSendersResponse resp;
      resp.set_code(chirp::common::OK);
      resp.add_target_user_ids("troll");
      resp.add_target_user_ids("spammer");
      send(RespFor(pkt, chirp::gateway::GET_BLOCKED_SENDERS_RESP, resp.SerializeAsString()));
      return;
    }
    default:
      return;
    }
  });
  ConnectAndLogin();

  std::error_code ec;
  const auto blocked = WaitRpc<chirp::chat::BlockMessageSenderResponse>([&](auto cb) {
    client_->BlockUser("troll", cb);
  }, ec);
  EXPECT_FALSE(ec);
  EXPECT_EQ(blocked.target_user_id(), "troll");
  (void)WaitRpc<chirp::chat::UnblockMessageSenderResponse>([&](auto cb) {
    client_->UnblockUser("troll", cb);
  }, ec);
  EXPECT_FALSE(ec);
  const auto list = WaitRpc<chirp::chat::GetBlockedSendersResponse>([&](auto cb) {
    client_->FetchBlockedUsers(cb);
  }, ec);
  EXPECT_FALSE(ec);
  ASSERT_EQ(list.target_user_ids_size(), 2);
  EXPECT_EQ(list.target_user_ids(0), "troll");
}

TEST_F(ConvenienceApiTest, ChannelMuteRoundTrip) {
  StartGateway([&](const chirp::gateway::Packet& pkt, auto send) {
    if (pkt.msg_id() == chirp::gateway::SET_CHANNEL_MUTE_REQ) {
      chirp::chat::SetChannelMuteRequest req;
      ASSERT_TRUE(req.ParseFromString(pkt.body()));
      EXPECT_EQ(req.channel_type(), chirp::chat::GUILD);
      EXPECT_TRUE(req.muted());
      chirp::chat::SetChannelMuteResponse resp;
      resp.set_code(chirp::common::OK);
      resp.set_channel_type(chirp::chat::GUILD);
      resp.set_muted(true);
      send(RespFor(pkt, chirp::gateway::SET_CHANNEL_MUTE_RESP, resp.SerializeAsString()));
      return;
    }
    if (pkt.msg_id() == chirp::gateway::GET_CHANNEL_MUTES_REQ) {
      chirp::chat::GetChannelMutesResponse resp;
      resp.set_code(chirp::common::OK);
      for (auto type : {chirp::chat::WORLD, chirp::chat::GUILD, chirp::chat::TEAM}) {
        auto* state = resp.add_states();
        state->set_channel_type(type);
        state->set_muted(type == chirp::chat::GUILD);
      }
      send(RespFor(pkt, chirp::gateway::GET_CHANNEL_MUTES_RESP, resp.SerializeAsString()));
      return;
    }
  });
  ConnectAndLogin();

  std::error_code ec;
  const auto set = WaitRpc<chirp::chat::SetChannelMuteResponse>([&](auto cb) {
    client_->SetChannelMute(chirp::chat::GUILD, true, cb);
  }, ec);
  EXPECT_FALSE(ec);
  EXPECT_TRUE(set.muted());
  const auto mutes = WaitRpc<chirp::chat::GetChannelMutesResponse>([&](auto cb) {
    client_->FetchChannelMutes(cb);
  }, ec);
  EXPECT_FALSE(ec);
  ASSERT_EQ(mutes.states_size(), 3);
  EXPECT_TRUE(mutes.states(1).muted());
}

TEST_F(ConvenienceApiTest, TypingIndicatorBroadcastAndQuery) {
  StartGateway([&](const chirp::gateway::Packet& pkt, auto send) {
    if (pkt.msg_id() == chirp::gateway::TYPING_INDICATOR_NOTIFY) {
      // 客户端裸发:sequence==0,不进 pending(无响应帧)。
      EXPECT_EQ(pkt.sequence(), 0);
      chirp::chat::TypingIndicator req;
      ASSERT_TRUE(req.ParseFromString(pkt.body()));
      EXPECT_EQ(req.user_id(), "sdk-user");
      EXPECT_EQ(req.channel_id(), "world-1");
      EXPECT_EQ(req.channel_type(), chirp::chat::WORLD);
      EXPECT_TRUE(req.is_typing());
      return;
    }
    if (pkt.msg_id() == chirp::gateway::GET_TYPING_USERS_REQ) {
      chirp::chat::GetTypingUsersResponse resp;
      resp.set_code(chirp::common::OK);
      resp.add_typing_user_ids("fast-fingers");
      resp.add_usernames("FastFingers");
      send(RespFor(pkt, chirp::gateway::GET_TYPING_USERS_RESP, resp.SerializeAsString()));
      return;
    }
  });
  ConnectAndLogin();

  // 裸发无回调;同连接 TCP 有序,后续往返必然晚于 typing 帧到达。
  client_->SendTypingIndicator(chirp::chat::WORLD, "world-1", true);
  std::error_code ec;
  const auto typing = WaitRpc<chirp::chat::GetTypingUsersResponse>([&](auto cb) {
    client_->FetchTypingUsers(chirp::chat::WORLD, "world-1", cb);
  }, ec);
  EXPECT_FALSE(ec);
  ASSERT_EQ(typing.typing_user_ids_size(), 1);
  EXPECT_EQ(typing.typing_user_ids(0), "fast-fingers");
}

TEST_F(ConvenienceApiTest, EditDeleteReactionsAndReceiptsRoundTrip) {
  StartGateway([&](const chirp::gateway::Packet& pkt, auto send) {
    switch (pkt.msg_id()) {
    case chirp::gateway::EDIT_MESSAGE_REQ: {
      chirp::chat::EditMessageRequest req;
      ASSERT_TRUE(req.ParseFromString(pkt.body()));
      EXPECT_EQ(req.user_id(), "sdk-user");
      EXPECT_EQ(req.new_content(), "edited");
      chirp::chat::EditMessageResponse resp;
      resp.set_code(chirp::common::OK);
      send(RespFor(pkt, chirp::gateway::EDIT_MESSAGE_RESP, resp.SerializeAsString()));
      return;
    }
    case chirp::gateway::DELETE_MESSAGE_REQ: {
      chirp::chat::DeleteMessageRequest req;
      ASSERT_TRUE(req.ParseFromString(pkt.body()));
      EXPECT_EQ(req.user_id(), "sdk-user");
      EXPECT_TRUE(req.is_hard_delete());
      chirp::chat::DeleteMessageResponse resp;
      resp.set_code(chirp::common::OK);
      resp.set_was_permanently_deleted(true);
      send(RespFor(pkt, chirp::gateway::DELETE_MESSAGE_RESP, resp.SerializeAsString()));
      return;
    }
    case chirp::gateway::ADD_REACTION_REQ: {
      chirp::chat::AddReactionRequest req;
      ASSERT_TRUE(req.ParseFromString(pkt.body()));
      EXPECT_EQ(req.emoji(), "\xF0\x9F\x91\x8D");
      chirp::chat::AddReactionResponse resp;
      resp.set_code(chirp::common::OK);
      send(RespFor(pkt, chirp::gateway::ADD_REACTION_RESP, resp.SerializeAsString()));
      return;
    }
    case chirp::gateway::REMOVE_REACTION_REQ: {
      chirp::chat::RemoveReactionResponse resp;
      resp.set_code(chirp::common::OK);
      send(RespFor(pkt, chirp::gateway::REMOVE_REACTION_RESP, resp.SerializeAsString()));
      return;
    }
    case chirp::gateway::GET_REACTIONS_REQ: {
      chirp::chat::GetReactionsRequest req;
      ASSERT_TRUE(req.ParseFromString(pkt.body()));
      EXPECT_TRUE(req.emoji().empty());  // 空 emoji = 拉全部
      chirp::chat::GetReactionsResponse resp;
      resp.set_code(chirp::common::OK);
      send(RespFor(pkt, chirp::gateway::GET_REACTIONS_RESP, resp.SerializeAsString()));
      return;
    }
    case chirp::gateway::GET_READ_RECEIPTS_REQ: {
      chirp::chat::GetReadReceiptsRequest req;
      ASSERT_TRUE(req.ParseFromString(pkt.body()));
      EXPECT_EQ(req.message_id(), "m-1");
      chirp::chat::GetReadReceiptsResponse resp;
      resp.set_code(chirp::common::OK);
      auto* receipt = resp.add_receipts();
      receipt->set_user_id("peer");
      receipt->set_message_id("m-1");
      send(RespFor(pkt, chirp::gateway::GET_READ_RECEIPTS_RESP, resp.SerializeAsString()));
      return;
    }
    default:
      return;
    }
  });
  ConnectAndLogin();

  std::error_code ec;
  (void)WaitRpc<chirp::chat::EditMessageResponse>([&](auto cb) {
    client_->EditMessage("m-1", "edited", cb);
  }, ec);
  EXPECT_FALSE(ec);
  const auto deleted = WaitRpc<chirp::chat::DeleteMessageResponse>([&](auto cb) {
    client_->DeleteMessage("m-1", true, cb);
  }, ec);
  EXPECT_FALSE(ec);
  EXPECT_TRUE(deleted.was_permanently_deleted());
  (void)WaitRpc<chirp::chat::AddReactionResponse>([&](auto cb) {
    client_->AddReaction("m-1", "\xF0\x9F\x91\x8D", cb);
  }, ec);
  EXPECT_FALSE(ec);
  (void)WaitRpc<chirp::chat::RemoveReactionResponse>([&](auto cb) {
    client_->RemoveReaction("m-1", "\xF0\x9F\x91\x8D", cb);
  }, ec);
  EXPECT_FALSE(ec);
  (void)WaitRpc<chirp::chat::GetReactionsResponse>([&](auto cb) {
    client_->FetchReactions("m-1", "", cb);
  }, ec);
  EXPECT_FALSE(ec);
  const auto receipts = WaitRpc<chirp::chat::GetReadReceiptsResponse>([&](auto cb) {
    client_->FetchReadReceipts("m-1", cb);
  }, ec);
  EXPECT_FALSE(ec);
  ASSERT_EQ(receipts.receipts_size(), 1);
  EXPECT_EQ(receipts.receipts(0).user_id(), "peer");
}

TEST_F(ConvenienceApiTest, RecallSendsSoftDeleteForTheAuthor) {
  StartGateway([&](const chirp::gateway::Packet& pkt, auto send) {
    if (pkt.msg_id() == chirp::gateway::DELETE_MESSAGE_REQ) {
      chirp::chat::DeleteMessageRequest req;
      ASSERT_TRUE(req.ParseFromString(pkt.body()));
      EXPECT_EQ(req.message_id(), "m-7");
      EXPECT_EQ(req.user_id(), "sdk-user");
      // 撤回就是软删：hard_delete 保持 false，由服务端按撤回窗口/频道判定。
      EXPECT_FALSE(req.is_hard_delete());
      chirp::chat::DeleteMessageResponse resp;
      resp.set_code(chirp::common::OK);
      send(RespFor(pkt, chirp::gateway::DELETE_MESSAGE_RESP, resp.SerializeAsString()));
      return;
    }
  });
  ConnectAndLogin();

  std::error_code ec;
  const auto resp = WaitRpc<chirp::chat::DeleteMessageResponse>([&](auto cb) {
    client_->RecallMessage("m-7", cb);
  }, ec);
  EXPECT_FALSE(ec);
  EXPECT_EQ(resp.code(), chirp::common::OK);
  EXPECT_FALSE(resp.was_permanently_deleted());
}

TEST_F(ConvenienceApiTest, BulkDeleteAndMentionSuggestionsRoundTrip) {
  StartGateway([&](const chirp::gateway::Packet& pkt, auto send) {
    if (pkt.msg_id() == chirp::gateway::BULK_DELETE_REQ) {
      chirp::chat::BulkDeleteRequest req;
      ASSERT_TRUE(req.ParseFromString(pkt.body()));
      EXPECT_EQ(req.requester_id(), "sdk-user");
      EXPECT_EQ(req.channel_id(), "ch-9");
      ASSERT_EQ(req.message_ids_size(), 2);
      EXPECT_EQ(req.message_ids(0), "m-1");
      chirp::chat::BulkDeleteResponse resp;
      resp.set_code(chirp::common::OK);
      resp.set_deleted_count(2);
      send(RespFor(pkt, chirp::gateway::BULK_DELETE_RESP, resp.SerializeAsString()));
      return;
    }
    if (pkt.msg_id() == chirp::gateway::GET_MENTION_SUGGESTIONS_REQ) {
      chirp::chat::GetMentionSuggestionsRequest req;
      ASSERT_TRUE(req.ParseFromString(pkt.body()));
      EXPECT_EQ(req.user_id(), "sdk-user");
      EXPECT_EQ(req.query(), "bo");
      chirp::chat::GetMentionSuggestionsResponse resp;
      resp.set_code(chirp::common::OK);
      send(RespFor(pkt, chirp::gateway::GET_MENTION_SUGGESTIONS_RESP, resp.SerializeAsString()));
      return;
    }
  });
  ConnectAndLogin();

  std::error_code ec;
  const auto bulk = WaitRpc<chirp::chat::BulkDeleteResponse>([&](auto cb) {
    client_->BulkDeleteMessages({"m-1", "m-2"}, "ch-9", cb);
  }, ec);
  EXPECT_FALSE(ec);
  EXPECT_EQ(bulk.deleted_count(), 2);
  (void)WaitRpc<chirp::chat::GetMentionSuggestionsResponse>([&](auto cb) {
    client_->FetchMentionSuggestions("ch-9", "bo", cb);
  }, ec);
  EXPECT_FALSE(ec);
}

TEST_F(ConvenienceApiTest, GroupLifecycleRoundTrip) {
  StartGateway([&](const chirp::gateway::Packet& pkt, auto send) {
    switch (pkt.msg_id()) {
    case chirp::gateway::CREATE_GROUP_REQ: {
      chirp::chat::CreateGroupRequest req;
      ASSERT_TRUE(req.ParseFromString(pkt.body()));
      EXPECT_EQ(req.creator_id(), "sdk-user");
      EXPECT_EQ(req.group_name(), "guild");
      EXPECT_EQ(req.description(), "desc");
      chirp::chat::CreateGroupResponse resp;
      resp.set_code(chirp::common::OK);
      resp.set_group_id("g-1");
      send(RespFor(pkt, chirp::gateway::CREATE_GROUP_RESP, resp.SerializeAsString()));
      return;
    }
    case chirp::gateway::JOIN_GROUP_REQ: {
      chirp::chat::JoinGroupRequest req;
      ASSERT_TRUE(req.ParseFromString(pkt.body()));
      EXPECT_EQ(req.user_id(), "sdk-user");
      EXPECT_EQ(req.group_id(), "g-1");
      chirp::chat::JoinGroupResponse resp;
      resp.set_code(chirp::common::OK);
      resp.mutable_group()->set_group_id("g-1");
      send(RespFor(pkt, chirp::gateway::JOIN_GROUP_RESP, resp.SerializeAsString()));
      return;
    }
    case chirp::gateway::INVITE_TO_GROUP_REQ: {
      chirp::chat::InviteToGroupRequest req;
      ASSERT_TRUE(req.ParseFromString(pkt.body()));
      EXPECT_EQ(req.inviter_id(), "sdk-user");
      EXPECT_EQ(req.target_user_id(), "pal");
      chirp::chat::InviteToGroupResponse resp;
      resp.set_code(chirp::common::OK);
      send(RespFor(pkt, chirp::gateway::INVITE_TO_GROUP_RESP, resp.SerializeAsString()));
      return;
    }
    case chirp::gateway::KICK_MEMBER_REQ: {
      chirp::chat::KickMemberRequest req;
      ASSERT_TRUE(req.ParseFromString(pkt.body()));
      EXPECT_EQ(req.requester_id(), "sdk-user");
      EXPECT_EQ(req.target_user_id(), "rogue");
      chirp::chat::KickMemberResponse resp;
      resp.set_code(chirp::common::OK);
      send(RespFor(pkt, chirp::gateway::KICK_MEMBER_RESP, resp.SerializeAsString()));
      return;
    }
    case chirp::gateway::GET_GROUP_INFO_REQ: {
      chirp::chat::GetGroupInfoResponse resp;
      resp.set_code(chirp::common::OK);
      resp.mutable_group()->set_group_name("guild");
      send(RespFor(pkt, chirp::gateway::GET_GROUP_INFO_RESP, resp.SerializeAsString()));
      return;
    }
    case chirp::gateway::GET_GROUP_MEMBERS_REQ: {
      chirp::chat::GetGroupMembersRequest req;
      ASSERT_TRUE(req.ParseFromString(pkt.body()));
      EXPECT_EQ(req.limit(), 10);
      EXPECT_EQ(req.offset(), 0);
      chirp::chat::GetGroupMembersResponse resp;
      resp.set_code(chirp::common::OK);
      resp.set_total_count(1);
      send(RespFor(pkt, chirp::gateway::GET_GROUP_MEMBERS_RESP, resp.SerializeAsString()));
      return;
    }
    case chirp::gateway::LEAVE_GROUP_REQ: {
      chirp::chat::LeaveGroupResponse resp;
      resp.set_code(chirp::common::OK);
      send(RespFor(pkt, chirp::gateway::LEAVE_GROUP_RESP, resp.SerializeAsString()));
      return;
    }
    case chirp::gateway::GET_USER_GROUPS_REQ: {
      chirp::chat::GetUserGroupsRequest req;
      ASSERT_TRUE(req.ParseFromString(pkt.body()));
      EXPECT_EQ(req.user_id(), "sdk-user");
      chirp::chat::GetUserGroupsResponse resp;
      resp.set_code(chirp::common::OK);
      resp.set_total_count(1);
      send(RespFor(pkt, chirp::gateway::GET_USER_GROUPS_RESP, resp.SerializeAsString()));
      return;
    }
    default:
      return;
    }
  });
  ConnectAndLogin();

  std::error_code ec;
  const auto created = WaitRpc<chirp::chat::CreateGroupResponse>([&](auto cb) {
    client_->CreateGroup("guild", "desc", cb);
  }, ec);
  EXPECT_FALSE(ec);
  EXPECT_EQ(created.group_id(), "g-1");
  (void)WaitRpc<chirp::chat::JoinGroupResponse>([&](auto cb) {
    client_->JoinGroup("g-1", cb);
  }, ec);
  EXPECT_FALSE(ec);
  (void)WaitRpc<chirp::chat::InviteToGroupResponse>([&](auto cb) {
    client_->InviteToGroup("g-1", "pal", cb);
  }, ec);
  EXPECT_FALSE(ec);
  (void)WaitRpc<chirp::chat::KickMemberResponse>([&](auto cb) {
    client_->KickMember("g-1", "rogue", cb);
  }, ec);
  EXPECT_FALSE(ec);
  const auto info = WaitRpc<chirp::chat::GetGroupInfoResponse>([&](auto cb) {
    client_->FetchGroupInfo("g-1", cb);
  }, ec);
  EXPECT_FALSE(ec);
  EXPECT_EQ(info.group().group_name(), "guild");
  const auto members = WaitRpc<chirp::chat::GetGroupMembersResponse>([&](auto cb) {
    client_->FetchGroupMembers("g-1", 10, 0, cb);
  }, ec);
  EXPECT_FALSE(ec);
  EXPECT_EQ(members.total_count(), 1);
  (void)WaitRpc<chirp::chat::LeaveGroupResponse>([&](auto cb) {
    client_->LeaveGroup("g-1", cb);
  }, ec);
  EXPECT_FALSE(ec);
  const auto groups = WaitRpc<chirp::chat::GetUserGroupsResponse>([&](auto cb) {
    client_->FetchUserGroups(10, 0, cb);
  }, ec);
  EXPECT_FALSE(ec);
  EXPECT_EQ(groups.total_count(), 1);
}

TEST_F(SdkClientTest, ConvenienceRequestsFailFastWhenNotConnected) {
  ChatClient client(TcpConfig());
  const auto not_connected = chirp::sdk::make_error_code(chirp::sdk::ChatError::NotConnected);
  std::error_code ec;
  WaitConvenienceRpc<chirp::chat::GetHistoryResponse>([&](auto cb) {
    client.FetchHistory(chirp::chat::WORLD, "w", 10, 0, cb);
  }, ec, 5000);
  EXPECT_EQ(ec, not_connected);
}

// 全量矩阵:26 个待回调便捷方法的 NotConnected 快速失败臂逐一覆盖
// (所有方法共享同一守卫,但代码是按方法展开的,需逐个触达)。
TEST_F(SdkClientTest, AllConvenienceMethodsFailFastWhenNotConnected) {
  ChatClient client(TcpConfig());
  const auto not_connected = chirp::sdk::make_error_code(chirp::sdk::ChatError::NotConnected);
  std::error_code ec;
  ChatClient::SendOptions opts;
  opts.receiver_id = "alice";

  (void)WaitConvenienceRpc<chirp::chat::SendMessageResponse>([&](auto cb) {
    client.SendMessage(opts, "hi", cb);
  }, ec, 5000);
  EXPECT_EQ(ec, not_connected);
  (void)WaitConvenienceRpc<chirp::chat::MarkReadResponse>([&](auto cb) {
    client.MarkChannelRead(chirp::chat::WORLD, "w", "m", cb);
  }, ec, 5000);
  EXPECT_EQ(ec, not_connected);
  (void)WaitConvenienceRpc<chirp::chat::GetUnreadCountResponse>([&](auto cb) {
    client.FetchUnreadCount(cb);
  }, ec, 5000);
  EXPECT_EQ(ec, not_connected);
  (void)WaitConvenienceRpc<chirp::chat::BlockMessageSenderResponse>([&](auto cb) {
    client.BlockUser("t", cb);
  }, ec, 5000);
  EXPECT_EQ(ec, not_connected);
  (void)WaitConvenienceRpc<chirp::chat::UnblockMessageSenderResponse>([&](auto cb) {
    client.UnblockUser("t", cb);
  }, ec, 5000);
  EXPECT_EQ(ec, not_connected);
  (void)WaitConvenienceRpc<chirp::chat::GetBlockedSendersResponse>([&](auto cb) {
    client.FetchBlockedUsers(cb);
  }, ec, 5000);
  EXPECT_EQ(ec, not_connected);
  (void)WaitConvenienceRpc<chirp::chat::SetChannelMuteResponse>([&](auto cb) {
    client.SetChannelMute(chirp::chat::GUILD, true, cb);
  }, ec, 5000);
  EXPECT_EQ(ec, not_connected);
  (void)WaitConvenienceRpc<chirp::chat::GetChannelMutesResponse>([&](auto cb) {
    client.FetchChannelMutes(cb);
  }, ec, 5000);
  EXPECT_EQ(ec, not_connected);
  (void)WaitConvenienceRpc<chirp::chat::GetTypingUsersResponse>([&](auto cb) {
    client.FetchTypingUsers(chirp::chat::WORLD, "w", cb);
  }, ec, 5000);
  EXPECT_EQ(ec, not_connected);
  (void)WaitConvenienceRpc<chirp::chat::EditMessageResponse>([&](auto cb) {
    client.EditMessage("m", "new", cb);
  }, ec, 5000);
  EXPECT_EQ(ec, not_connected);
  (void)WaitConvenienceRpc<chirp::chat::DeleteMessageResponse>([&](auto cb) {
    client.DeleteMessage("m", false, cb);
  }, ec, 5000);
  EXPECT_EQ(ec, not_connected);
  (void)WaitConvenienceRpc<chirp::chat::DeleteMessageResponse>([&](auto cb) {
    client.RecallMessage("m", cb);
  }, ec, 5000);
  EXPECT_EQ(ec, not_connected);
  (void)WaitConvenienceRpc<chirp::chat::AddReactionResponse>([&](auto cb) {
    client.AddReaction("m", "e", cb);
  }, ec, 5000);
  EXPECT_EQ(ec, not_connected);
  (void)WaitConvenienceRpc<chirp::chat::RemoveReactionResponse>([&](auto cb) {
    client.RemoveReaction("m", "e", cb);
  }, ec, 5000);
  EXPECT_EQ(ec, not_connected);
  (void)WaitConvenienceRpc<chirp::chat::GetReactionsResponse>([&](auto cb) {
    client.FetchReactions("m", "e", cb);
  }, ec, 5000);
  EXPECT_EQ(ec, not_connected);
  (void)WaitConvenienceRpc<chirp::chat::GetReadReceiptsResponse>([&](auto cb) {
    client.FetchReadReceipts("m", cb);
  }, ec, 5000);
  EXPECT_EQ(ec, not_connected);
  (void)WaitConvenienceRpc<chirp::chat::BulkDeleteResponse>([&](auto cb) {
    client.BulkDeleteMessages({"m"}, "ch", cb);
  }, ec, 5000);
  EXPECT_EQ(ec, not_connected);
  (void)WaitConvenienceRpc<chirp::chat::GetMentionSuggestionsResponse>([&](auto cb) {
    client.FetchMentionSuggestions("ch", "q", cb);
  }, ec, 5000);
  EXPECT_EQ(ec, not_connected);
  (void)WaitConvenienceRpc<chirp::chat::CreateGroupResponse>([&](auto cb) {
    client.CreateGroup("g", "d", cb);
  }, ec, 5000);
  EXPECT_EQ(ec, not_connected);
  (void)WaitConvenienceRpc<chirp::chat::JoinGroupResponse>([&](auto cb) {
    client.JoinGroup("g", cb);
  }, ec, 5000);
  EXPECT_EQ(ec, not_connected);
  (void)WaitConvenienceRpc<chirp::chat::LeaveGroupResponse>([&](auto cb) {
    client.LeaveGroup("g", cb);
  }, ec, 5000);
  EXPECT_EQ(ec, not_connected);
  (void)WaitConvenienceRpc<chirp::chat::InviteToGroupResponse>([&](auto cb) {
    client.InviteToGroup("g", "u", cb);
  }, ec, 5000);
  EXPECT_EQ(ec, not_connected);
  (void)WaitConvenienceRpc<chirp::chat::KickMemberResponse>([&](auto cb) {
    client.KickMember("g", "u", cb);
  }, ec, 5000);
  EXPECT_EQ(ec, not_connected);
  (void)WaitConvenienceRpc<chirp::chat::GetGroupInfoResponse>([&](auto cb) {
    client.FetchGroupInfo("g", cb);
  }, ec, 5000);
  EXPECT_EQ(ec, not_connected);
  (void)WaitConvenienceRpc<chirp::chat::GetGroupMembersResponse>([&](auto cb) {
    client.FetchGroupMembers("g", 10, 0, cb);
  }, ec, 5000);
  EXPECT_EQ(ec, not_connected);
  (void)WaitConvenienceRpc<chirp::chat::GetUserGroupsResponse>([&](auto cb) {
    client.FetchUserGroups(10, 0, cb);
  }, ec, 5000);
  EXPECT_EQ(ec, not_connected);

  // 裸发路径:未连接时 SendPacket 无 socket,静默丢弃(覆盖 socket_ 空臂)。
  client.SendTypingIndicator(chirp::chat::WORLD, "w", true);
  std::this_thread::sleep_for(std::chrono::milliseconds(100));
}

// ec 文案映射:message(int) 按枚举逐一分支,全部断言一遍(未知码走 default)。
TEST(SdkErrorMessages, EveryChatErrorCodeHasExpectedMessage) {
  using chirp::sdk::make_error_code;
  using CE = chirp::sdk::ChatError;
  EXPECT_EQ(make_error_code(CE::OK).message(), "ok");
  EXPECT_EQ(make_error_code(CE::NotConnected).message(), "not connected");
  EXPECT_EQ(make_error_code(CE::AlreadyConnected).message(), "already connected");
  EXPECT_EQ(make_error_code(CE::LoginFailed).message(), "login failed");
  EXPECT_EQ(make_error_code(CE::SendFailed).message(), "send failed");
  EXPECT_EQ(make_error_code(CE::InvalidParam).message(), "invalid parameter");
  EXPECT_EQ(make_error_code(CE::Timeout).message(), "timeout");
  EXPECT_EQ(make_error_code(CE::Closed).message(), "connection closed");
  EXPECT_EQ(make_error_code(CE::Kicked).message(), "kicked");
  EXPECT_EQ(make_error_code(CE::BadResponse).message(), "bad response");
  EXPECT_EQ(make_error_code(static_cast<CE>(42)).message(), "unknown error");
}

TEST_F(ConvenienceApiTest, UnparseableResponseReportsBadResponse) {
  StartGateway([&](const chirp::gateway::Packet& pkt, auto send) {
    if (pkt.msg_id() != chirp::gateway::GET_HISTORY_REQ) {
      return;
    }
    // 'n' 解出 tag(field 13, wire type 6):非法 wire type,ParseFromString 必败。
    send(RespFor(pkt, chirp::gateway::GET_HISTORY_RESP, "not-proto"));
  });
  ConnectAndLogin();
  std::error_code ec;
  (void)WaitRpc<chirp::chat::GetHistoryResponse>([&](auto cb) {
    client_->FetchHistory(chirp::chat::WORLD, "w", 10, 0, cb);
  }, ec);
  EXPECT_EQ(ec, chirp::sdk::make_error_code(chirp::sdk::ChatError::BadResponse));
}

TEST_F(ConvenienceApiTest, UnansweredConvenienceRequestTimesOut) {
  StartGateway([](const chirp::gateway::Packet&, auto) {});
  auto config = LoopbackConfig(gateway_->port());
  config.request_timeout_ms = 200;
  client_ = std::make_unique<ChatClient>(config);
  client_->Connect();
  WaitState(*client_, ConnectionState::Connected, kWaitMs);
  std::promise<std::error_code> done;
  auto future = done.get_future();
  client_->Login("tok", [&](const std::error_code& ec, const std::string&) { done.set_value(ec); });
  ASSERT_EQ(future.wait_for(std::chrono::milliseconds(kWaitMs)), std::future_status::ready);
  ASSERT_EQ(client_->GetState(), ConnectionState::LoggedIn);

  std::error_code ec;
  (void)WaitRpc<chirp::chat::GetUnreadCountResponse>([&](auto cb) {
    client_->FetchUnreadCount(cb);
  }, ec);
  EXPECT_EQ(ec, chirp::sdk::make_error_code(chirp::sdk::ChatError::Timeout));
}

// ---------------------------------------------------------------------------
// FileMessageStore: file-backed persistent store, byte-format aligned with
// the C# FileMessageStore (CHIRPLOG1 + [1B kind][4B len LE][payload]).
// These are offline cases: no client, no gateway, just the store.

class FileMessageStoreTest : public ::testing::Test {
 protected:
  void SetUp() override {
    dir_ = std::filesystem::temp_directory_path() /
           ("chirp_file_store_" + std::to_string(++instance_counter_));
    std::filesystem::create_directories(dir_);
  }
  void TearDown() override {
    std::error_code ec;
    std::filesystem::remove_all(dir_, ec);
  }

  std::string Path() const { return (dir_ / "archive.log").string(); }

  chirp::sdk::FileMessageStore::Options Opts(size_t max_per_channel = 0) {
    return {Path(), max_per_channel};
  }

  static void AppendGarbageTail(const std::string& path,
                                const std::string& bytes) {
    std::ofstream out(path, std::ios::app | std::ios::binary);
    out.write(bytes.data(), static_cast<std::streamsize>(bytes.size()));
  }

  static size_t FileSize(const std::string& path) {
    return static_cast<size_t>(std::filesystem::file_size(path));
  }

  std::vector<std::string> LoadContents(chirp::sdk::FileMessageStore& store,
                                        const std::string& channel) {
    std::vector<std::string> contents;
    for (const auto& m :
         store.Load(chirp::chat::WORLD, channel, 50)) {
      contents.push_back(m.content());
    }
    return contents;
  }

  std::filesystem::path dir_;
  static std::atomic<int> instance_counter_;
};

std::atomic<int> FileMessageStoreTest::instance_counter_{0};

TEST_F(FileMessageStoreTest, ReplayRoundTripKeepsOrderAndFilters) {
  {
    chirp::sdk::FileMessageStore store(Opts());
    store.Save(MakeStoredMessage("world", 100, "a"));
    store.Save(MakeStoredMessage("world", 200, "b"));
    store.Save(MakeStoredMessage("world", 300, "c"));
  }
  chirp::sdk::FileMessageStore reopened(Opts());
  EXPECT_EQ(LoadContents(reopened, "world"),
            (std::vector<std::string>{"c", "b", "a"}));

  // before_timestamp strictly excludes the boundary, newest first.
  std::vector<std::string> before;
  for (const auto& m : reopened.Load(chirp::chat::WORLD, "world", 50, 300)) {
    before.push_back(m.content());
  }
  EXPECT_EQ(before, (std::vector<std::string>{"b", "a"}));
  EXPECT_EQ(reopened.Load(chirp::chat::WORLD, "absent", 10).size(), 0u);
  EXPECT_EQ(reopened.Load(chirp::chat::WORLD, "world", 0).size(), 0u);
}

TEST_F(FileMessageStoreTest, ReadCursorPersistsAcrossInstances) {
  {
    chirp::sdk::FileMessageStore store(Opts());
    store.Save(MakeStoredMessage("world", 100, "a"));
    store.Save(MakeStoredMessage("world", 200, "b"));
    store.MarkRead(chirp::chat::WORLD, "world", "m-100");
    store.MarkRead(chirp::chat::WORLD, "world", "m-100");  // dedup: lean log
    store.MarkRead(chirp::chat::WORLD, "world", "");       // empty id: no-op
  }
  chirp::sdk::FileMessageStore reopened(Opts());
  EXPECT_EQ(reopened.GetUnreadCount(chirp::chat::WORLD, "world"), 1);
}

TEST_F(FileMessageStoreTest, EvictionIsInMemoryUntilCompact) {
  {
    chirp::sdk::FileMessageStore capped(Opts(3));
    for (const int ts : {100, 200, 300, 400}) {
      capped.Save(MakeStoredMessage("world", ts, "m" + std::to_string(ts)));
    }
    EXPECT_EQ(LoadContents(capped, "world").size(), 3u);
  }
  // Unlimited instance replays everything the log still holds: eviction
  // never rewrote the file.
  {
    chirp::sdk::FileMessageStore revived(Opts());
    EXPECT_EQ(LoadContents(revived, "world").size(), 4u);
  }
  // Compacting from the capped instance retires the evicted entry on disk.
  {
    chirp::sdk::FileMessageStore capped(Opts(3));
    capped.Compact();
  }
  chirp::sdk::FileMessageStore after(Opts());
  EXPECT_EQ(LoadContents(after, "world").size(), 3u);
}

TEST_F(FileMessageStoreTest, CleanupRewritesLogWithoutOldMessages) {
  {
    chirp::sdk::FileMessageStore store(Opts());
    store.Save(MakeStoredMessage("world", 100, "old"));
    store.Save(MakeStoredMessage("world", 200, "keep"));
    store.Cleanup(150);  // removes + compacts in one step
  }
  chirp::sdk::FileMessageStore reopened(Opts());
  EXPECT_EQ(LoadContents(reopened, "world"),
            (std::vector<std::string>{"keep"}));
}

TEST_F(FileMessageStoreTest, TruncatedTailIsRepairedOnReplay) {
  {
    chirp::sdk::FileMessageStore store(Opts());
    store.Save(MakeStoredMessage("world", 100, "a"));
    store.Save(MakeStoredMessage("world", 200, "b"));
  }
  // Half-written record: header announces 100 payload bytes, 3 arrive.
  // (Split literals: "\x00a" would greedily parse as hex 0x00a.)
  AppendGarbageTail(Path(), std::string("\x01\x64\x00\x00" "\x00" "abc"));
  {
    chirp::sdk::FileMessageStore store(Opts());
    EXPECT_EQ(LoadContents(store, "world").size(), 2u);
    // The tail was truncated to the clean boundary: appending still works.
    store.Save(MakeStoredMessage("world", 300, "c"));
  }
  chirp::sdk::FileMessageStore reopened(Opts());
  EXPECT_EQ(LoadContents(reopened, "world"),
            (std::vector<std::string>{"c", "b", "a"}));
}

TEST_F(FileMessageStoreTest, BadMagicResetsWithoutThrowing) {
  AppendGarbageTail(Path(), "this is not a chirp log at all");
  {
    chirp::sdk::FileMessageStore store(Opts());
    store.Save(MakeStoredMessage("world", 100, "a"));
  }
  chirp::sdk::FileMessageStore reopened(Opts());
  EXPECT_EQ(LoadContents(reopened, "world"),
            (std::vector<std::string>{"a"}));
}

TEST_F(FileMessageStoreTest, CompactDropsReadMarksOfEvictedMessages) {
  {
    chirp::sdk::FileMessageStore capped(Opts(2));
    capped.Save(MakeStoredMessage("world", 100, "a"));  // evicted below
    capped.Save(MakeStoredMessage("world", 200, "b"));
    capped.MarkRead(chirp::chat::WORLD, "world", "m-100");
    capped.Save(MakeStoredMessage("world", 300, "c"));  // evicts m-100
    capped.Compact();  // dead read mark must not survive the rewrite
  }
  chirp::sdk::FileMessageStore reopened(Opts());
  EXPECT_EQ(reopened.GetUnreadCount(chirp::chat::WORLD, "world"), 2);
  EXPECT_EQ(LoadContents(reopened, "world").size(), 2u);
}

TEST_F(FileMessageStoreTest, SendSideEntriesWithoutIdNeverCountUnread) {
  chirp::chat::ChatMessage sent = MakeStoredMessage("world", 100, "mine");
  sent.set_message_id("");  // fire-and-forget: no server id yet
  chirp::sdk::FileMessageStore store(Opts());
  store.Save(sent);
  EXPECT_EQ(store.GetUnreadCount(chirp::chat::WORLD, "world"), 0);
}

}  // namespace

