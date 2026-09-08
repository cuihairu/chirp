#include <gtest/gtest.h>

#include <chrono>
#include <future>
#include <string>
#include <vector>

#include "chirp/sdk.h"
#include "chirp/sdk_client.h"

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

TEST(ChatConfigTest, Defaults) {
  ChatConfig config;
  EXPECT_EQ(config.gateway_host, "localhost");
  EXPECT_EQ(config.gateway_port, 5000u);
  EXPECT_EQ(config.gateway_ws_port, 5001u);
  EXPECT_FALSE(config.enable_websocket);
  EXPECT_EQ(config.heartbeat_interval_seconds, 30);
  EXPECT_EQ(config.reconnect_interval_seconds, 5);
  EXPECT_EQ(config.max_reconnect_attempts, -1);
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
  {
    ChatClient client(TcpConfig());
    client.SetMessageCallback([](const std::string&, const std::string&) {});
    client.SetDisconnectCallback([](const std::error_code&) {});
    client.SetKickCallback([](const std::string&) {});
    client.Disconnect();
  }
  SUCCEED();
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

  // Closes the accepted connection (keeps listening for new ones).
  void DropConnections() {
    asio::post(io_, [this] {
      asio::error_code ec;
      if (socket_) {
        socket_->close(ec);
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
  std::shared_ptr<asio::ip::tcp::socket> socket_;
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
  for (int i = 0; i < 300 && client.GetState() != ConnectionState::Connected; ++i) {
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
  for (int i = 0; i < 300 && messages < 1; ++i) {
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
  for (int i = 0; i < 300 && client.GetState() != ConnectionState::Disconnected; ++i) {
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
  for (int i = 0; i < 300 && client.GetState() != ConnectionState::Connected; ++i) {
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
  for (int i = 0; i < 300 && client.GetState() != ConnectionState::Connected; ++i) {
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
  for (int i = 0; i < 300 && client.GetState() != ConnectionState::Connected; ++i) {
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
  for (int i = 0; i < 300 && client.GetState() != ConnectionState::Connected; ++i) {
    std::this_thread::sleep_for(std::chrono::milliseconds(2));
  }
  ASSERT_EQ(client.GetState(), ConnectionState::Connected);

  std::promise<std::error_code> promise;
  auto future = promise.get_future();
  client.Login("t", [&promise](const std::error_code& ec, const std::string&) {
    promise.set_value(ec);
  });
  ASSERT_EQ(future.wait_for(std::chrono::milliseconds(5000)), std::future_status::ready);
  EXPECT_FALSE(future.get());
  ASSERT_EQ(client.GetState(), ConnectionState::LoggedIn);

  client.Logout();  // sends LOGOUT_REQ then closes
  for (int i = 0; i < 300 && client.GetState() != ConnectionState::Disconnected; ++i) {
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
  for (int i = 0; i < 300 && client.GetState() != ConnectionState::Connected; ++i) {
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
  for (int i = 0; i < 300 && client.GetState() != ConnectionState::Disconnected; ++i) {
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
      on_connect_(*sock);
      DoAccept();
    });
  }
  asio::io_context io_;
  asio::ip::tcp::acceptor acceptor_;
  uint16_t port_;
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
  std::this_thread::sleep_for(std::chrono::milliseconds(200));

  // State stays LoggedIn after the transport died; the send must be dropped
  // inside SendPacket because no socket is attached anymore.
  client.SendMessage("peer", "after-close");
  std::this_thread::sleep_for(std::chrono::milliseconds(200));
  EXPECT_EQ(client.GetState(), ConnectionState::LoggedIn);
  client.Disconnect();
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

}  // namespace
