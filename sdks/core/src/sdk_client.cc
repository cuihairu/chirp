#include "chirp/sdk_client.h"

#include <array>
#include <chrono>
#include <deque>
#include <unordered_map>

#include "common/logger.h"
#include "network/length_prefixed_framer.h"
#include "network/protobuf_framing.h"
#include "proto/auth.pb.h"
#include "proto/chat.pb.h"
#include "proto/common.pb.h"
#include "proto/gateway.pb.h"

namespace chirp {
namespace sdk {

namespace {

int64_t NowMs() {
  using namespace std::chrono;
  return duration_cast<milliseconds>(system_clock::now().time_since_epoch()).count();
}

std::error_code MakeEc(ChatError e) { return make_error_code(e); }

} // namespace

// ChatClient 实现类 (Pimpl 模式)
class ChatClient::Impl {
public:
  explicit Impl(const ChatConfig& config)
      : config_(config),
        state_(ConnectionState::Disconnected),
        work_(asio::make_work_guard(io_context_)),
        heartbeat_timer_(io_context_),
        thread_([this] { io_context_.run(); }) {}

  ~Impl() {
    Disconnect();
    work_.reset();
    io_context_.stop();
    if (thread_.joinable()) {
      thread_.join();
    }
  }

  ConnectionState GetState() const {
    return state_.load();
  }

  void Connect() {
    asio::post(io_context_, [this] { DoConnect(); });
  }

  void Disconnect() {
    asio::post(io_context_, [this] {
      DoClose(/*notify=*/false, std::error_code{});
      state_ = ConnectionState::Disconnected;
    });
  }

  void Login(const std::string& token, LoginCallback cb) {
    asio::post(io_context_, [this, token, cb = std::move(cb)]() mutable {
      if (token.empty()) {
        cb(MakeEc(ChatError::InvalidParam), "");
        return;
      }
      if (state_ != ConnectionState::Connected) {
        cb(MakeEc(ChatError::NotConnected), "");
        return;
      }

      chirp::auth::LoginRequest req;
      req.set_token(token);
      req.set_device_id("sdk_device");
      req.set_platform("pc");

      chirp::gateway::Packet pkt;
      pkt.set_msg_id(chirp::gateway::LOGIN_REQ);
      pkt.set_sequence(next_seq_++);
      pkt.set_body(req.SerializeAsString());

      {
        std::lock_guard<std::mutex> lock(callbacks_mutex_);
        pending_logins_[pkt.sequence()] = std::move(cb);
      }

      SendPacket(pkt);
    });
  }

  void Logout() {
    asio::post(io_context_, [this] {
      if (state_ != ConnectionState::LoggedIn) {
        return;
      }

      chirp::auth::LogoutRequest req;
      req.set_user_id(user_id_);
      req.set_session_id(session_id_);

      chirp::gateway::Packet pkt;
      pkt.set_msg_id(chirp::gateway::LOGOUT_REQ);
      pkt.set_sequence(next_seq_++);
      pkt.set_body(req.SerializeAsString());
      SendPacket(pkt);

      DoClose(/*notify=*/false, std::error_code{});
      state_ = ConnectionState::Disconnected;
    });
  }

  void SendMessage(const std::string& receiver, const std::string& content) {
    asio::post(io_context_, [this, receiver, content] {
      if (state_ != ConnectionState::LoggedIn) {
        return;
      }
      if (receiver.empty()) {
        return;
      }

      chirp::chat::SendMessageRequest req;
      req.set_sender_id(user_id_);
      req.set_receiver_id(receiver);
      req.set_channel_type(chirp::chat::PRIVATE);
      req.set_channel_id(user_id_ <= receiver ? (user_id_ + "|" + receiver) : (receiver + "|" + user_id_));
      req.set_msg_type(chirp::chat::TEXT);
      req.set_content(content);
      req.set_client_timestamp(NowMs());

      chirp::gateway::Packet pkt;
      pkt.set_msg_id(chirp::gateway::SEND_MESSAGE_REQ);
      pkt.set_sequence(next_seq_++);
      pkt.set_body(req.SerializeAsString());
      SendPacket(pkt);
    });
  }

  void SetMessageCallback(MessageCallback cb) {
    std::lock_guard<std::mutex> lock(callbacks_mutex_);
    on_message_ = std::move(cb);
  }

  void SetDisconnectCallback(DisconnectCallback cb) {
    std::lock_guard<std::mutex> lock(callbacks_mutex_);
    on_disconnect_ = std::move(cb);
  }

  void SetKickCallback(KickCallback cb) {
    std::lock_guard<std::mutex> lock(callbacks_mutex_);
    on_kick_ = std::move(cb);
  }

private:
  void DoConnect() {
    if (config_.enable_websocket) {
      NotifyDisconnect(MakeEc(ChatError::InvalidParam));
      return;
    }
    if (state_ != ConnectionState::Disconnected) {
      return;
    }

    state_ = ConnectionState::Connecting;

    auto resolver = std::make_shared<asio::ip::tcp::resolver>(io_context_);
    auto socket = std::make_shared<asio::ip::tcp::socket>(io_context_);

    resolver->async_resolve(
        config_.gateway_host, std::to_string(config_.gateway_port),
        [this, resolver, socket](const std::error_code& ec, asio::ip::tcp::resolver::results_type results) {
          if (ec) {
            state_ = ConnectionState::Disconnected;
            NotifyDisconnect(ec);
            return;
          }

          asio::async_connect(
              *socket, results,
              [this, socket](const std::error_code& connect_ec, const asio::ip::tcp::endpoint&) {
                if (connect_ec) {
                  state_ = ConnectionState::Disconnected;
                  NotifyDisconnect(connect_ec);
                  return;
                }

                socket_ = socket;
                framer_.Clear();
                write_q_.clear();
                write_in_flight_ = false;
                closed_ = false;
                user_id_.clear();
                session_id_.clear();
                state_ = ConnectionState::Connected;
                StartHeartbeat();
                DoRead();
              });
        });
  }

  void DoRead() {
    if (!socket_) {
      return;
    }

    socket_->async_read_some(
        asio::buffer(read_buf_),
        [this](const std::error_code& ec, std::size_t n) {
          if (ec) {
            DoClose(/*notify=*/true, ec);
            return;
          }
          framer_.Append(read_buf_.data(), n);
          while (true) {
            auto frame = framer_.PopFrame();
            if (!frame) {
              break;
            }
            HandleFrame(*frame);
          }
          DoRead();
        });
  }

  void HandleFrame(const std::string& payload) {
    chirp::gateway::Packet pkt;
    if (!chirp::network::ProtobufFraming::Decode(payload, &pkt)) {
      return;
    }

    switch (pkt.msg_id()) {
    case chirp::gateway::LOGIN_RESP:
      HandleLoginResp(pkt);
      break;
    case chirp::gateway::KICK_NOTIFY:
      HandleKick(pkt);
      break;
    case chirp::gateway::CHAT_MESSAGE_NOTIFY:
      HandleChatNotify(pkt);
      break;
    default:
      break;
    }
  }

  void HandleLoginResp(const chirp::gateway::Packet& pkt) {
    chirp::auth::LoginResponse resp;
    if (!resp.ParseFromArray(pkt.body().data(), static_cast<int>(pkt.body().size()))) {
      CompleteLogin(pkt.sequence(), MakeEc(ChatError::LoginFailed), "");
      return;
    }

    if (resp.code() != chirp::common::OK) {
      CompleteLogin(pkt.sequence(), MakeEc(ChatError::LoginFailed), "");
      return;
    }

    user_id_ = resp.user_id();
    session_id_ = resp.session_id();
    state_ = ConnectionState::LoggedIn;
    CompleteLogin(pkt.sequence(), std::error_code{}, user_id_);
  }

  void CompleteLogin(int64_t seq, const std::error_code& ec, const std::string& user_id) {
    LoginCallback cb;
    {
      std::lock_guard<std::mutex> lock(callbacks_mutex_);
      auto it = pending_logins_.find(seq);
      if (it == pending_logins_.end()) {
        return;
      }
      cb = std::move(it->second);
      pending_logins_.erase(it);
    }
    if (cb) {
      cb(ec, user_id);
    }
  }

  void HandleKick(const chirp::gateway::Packet& pkt) {
    chirp::auth::KickNotify kick;
    if (!kick.ParseFromArray(pkt.body().data(), static_cast<int>(pkt.body().size()))) {
      return;
    }
    KickCallback cb;
    {
      std::lock_guard<std::mutex> lock(callbacks_mutex_);
      cb = on_kick_;
    }
    if (cb) {
      cb(kick.reason());
    }
  }

  void HandleChatNotify(const chirp::gateway::Packet& pkt) {
    chirp::chat::ChatMessage msg;
    if (!msg.ParseFromArray(pkt.body().data(), static_cast<int>(pkt.body().size()))) {
      return;
    }
    MessageCallback cb;
    {
      std::lock_guard<std::mutex> lock(callbacks_mutex_);
      cb = on_message_;
    }
    if (cb) {
      cb(msg.sender_id(), msg.content());
    }
  }

  void StartHeartbeat() {
    const int interval = config_.heartbeat_interval_seconds;
    if (interval <= 0) {
      return;
    }

    heartbeat_timer_.expires_after(std::chrono::seconds(interval));
    heartbeat_timer_.async_wait([this](const std::error_code& ec) {
      if (ec || !socket_) {
        return;
      }

      chirp::gateway::HeartbeatPing ping;
      ping.set_timestamp(NowMs());

      chirp::gateway::Packet pkt;
      pkt.set_msg_id(chirp::gateway::HEARTBEAT_PING);
      pkt.set_sequence(next_seq_++);
      pkt.set_body(ping.SerializeAsString());
      SendPacket(pkt);

      StartHeartbeat();
    });
  }

  void SendPacket(const chirp::gateway::Packet& pkt) {
    if (!socket_) {
      return;
    }
    auto framed = chirp::network::ProtobufFraming::Encode(pkt);
    std::string bytes(reinterpret_cast<const char*>(framed.data()), framed.size());
    write_q_.push_back(std::move(bytes));
    if (!write_in_flight_) {
      write_in_flight_ = true;
      DoWrite();
    }
  }

  void DoWrite() {
    if (!socket_) {
      write_q_.clear();
      write_in_flight_ = false;
      return;
    }
    if (write_q_.empty()) {
      write_in_flight_ = false;
      return;
    }

    socket_->async_write_some(
        asio::buffer(write_q_.front()),
        [this](const std::error_code& ec, std::size_t n) {
          if (ec) {
            DoClose(/*notify=*/true, ec);
            return;
          }
          auto& front = write_q_.front();
          front.erase(0, n);
          if (front.empty()) {
            write_q_.pop_front();
          }
          DoWrite();
        });
  }

  void DoClose(bool notify, const std::error_code& ec) {
    if (closed_) {
      return;
    }
    closed_ = true;
    heartbeat_timer_.cancel();

    std::unordered_map<int64_t, LoginCallback> pending_logins;
    {
      std::lock_guard<std::mutex> lock(callbacks_mutex_);
      pending_logins = std::move(pending_logins_);
      pending_logins_.clear();
    }

    if (socket_) {
      std::error_code ignore;
      socket_->shutdown(asio::ip::tcp::socket::shutdown_both, ignore);
      socket_->close(ignore);
      socket_.reset();
    }

    const std::error_code login_ec = ec ? ec : MakeEc(ChatError::NotConnected);
    for (auto& [seq, cb] : pending_logins) {
      (void)seq;
      if (cb) {
        cb(login_ec, "");
      }
    }

    if (notify) {
      NotifyDisconnect(ec);
    }
  }

  void NotifyDisconnect(std::error_code ec) {
    DisconnectCallback cb;
    {
      std::lock_guard<std::mutex> lock(callbacks_mutex_);
      cb = on_disconnect_;
    }
    if (cb) {
      cb(ec);
    }
  }

  ChatConfig config_;
  std::atomic<ConnectionState> state_;
  asio::io_context io_context_;
  asio::executor_work_guard<asio::io_context::executor_type> work_;
  asio::steady_timer heartbeat_timer_;
  std::thread thread_;

  std::shared_ptr<asio::ip::tcp::socket> socket_;
  std::array<uint8_t, 4096> read_buf_{};
  chirp::network::LengthPrefixedFramer framer_;
  std::deque<std::string> write_q_;
  bool write_in_flight_{false};
  bool closed_{false};

  std::string user_id_;
  std::string session_id_;
  int64_t next_seq_{1};

  std::mutex callbacks_mutex_;
  std::unordered_map<int64_t, LoginCallback> pending_logins_;
  MessageCallback on_message_;
  DisconnectCallback on_disconnect_;
  KickCallback on_kick_;
};

// ChatClient 实现
ChatClient::ChatClient(const ChatConfig& config)
    : impl_(std::make_unique<Impl>(config)) {}

ChatClient::~ChatClient() = default;

void ChatClient::Connect() {
  impl_->Connect();
}

void ChatClient::Disconnect() {
  impl_->Disconnect();
}

ConnectionState ChatClient::GetState() const {
  return impl_->GetState();
}

void ChatClient::Login(const std::string& token, LoginCallback cb) {
  impl_->Login(token, std::move(cb));
}

void ChatClient::Logout() {
  impl_->Logout();
}

void ChatClient::SendMessage(const std::string& receiver, const std::string& content) {
  impl_->SendMessage(receiver, content);
}

void ChatClient::SetMessageCallback(MessageCallback cb) {
  impl_->SetMessageCallback(std::move(cb));
}

void ChatClient::SetDisconnectCallback(DisconnectCallback cb) {
  impl_->SetDisconnectCallback(std::move(cb));
}

void ChatClient::SetKickCallback(KickCallback cb) {
  impl_->SetKickCallback(std::move(cb));
}

} // namespace sdk
} // namespace chirp
