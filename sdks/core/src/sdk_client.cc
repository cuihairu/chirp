#include "chirp/sdk_client.h"

#include <array>
#include <chrono>
#include <cstdlib>
#include <deque>
#include <mutex>
#include <unordered_map>
#include <vector>

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

using chirp::gateway::MsgID;

int64_t NowMs() {
  using namespace std::chrono;
  return duration_cast<milliseconds>(system_clock::now().time_since_epoch()).count();
}

std::error_code MakeEc(ChatError e) { return make_error_code(e); }

// 与 web/mobile/unity 客户端一致的退避参数:500ms 起步、翻倍、15s 封顶、
// ±20% 抖动。
constexpr int64_t kReconnectBaseMs = 500;
constexpr int64_t kReconnectMaxMs = 15000;

int64_t BackoffDelayMs(int attempt) {
  int64_t delay = kReconnectBaseMs;
  for (int i = 0; i < attempt && delay < kReconnectMaxMs; ++i) {
    delay *= 2;
  }
  if (delay > kReconnectMaxMs) {
    delay = kReconnectMaxMs;
  }
  // ±20% jitter:抖动幅度随延迟缩放,重连风暴不会整点对齐。
  const auto mod = static_cast<int64_t>(delay / 5);
  if (mod > 0) {
    const auto jitter = static_cast<int64_t>(std::rand() % (2 * mod + 1)) - mod;
    delay += jitter;
  }
  return delay;
}

// 一个 sequence 关联的待完成请求。
struct PendingRequest {
  uint32_t resp_msg_id;
  ResponseCallback cb;
  std::shared_ptr<asio::steady_timer> timer;
};

} // namespace

// ChatClient 实现类 (Pimpl 模式)。公有方法可从任意线程调用;连接、状态
// 迁移、帧解析、pending 表全部固定在内部 io 线程,彼此无需加锁;用户回调
// 表(on_*、notify subs)可能在外部线程写入,经 callbacks_mutex_ 保护。
class ChatClient::Impl {
public:
  explicit Impl(const ChatConfig& config)
      : config_(config),
        state_(ConnectionState::Disconnected),
        work_(asio::make_work_guard(io_context_)),
        heartbeat_timer_(io_context_),
        reconnect_timer_(io_context_),
        thread_([this] { io_context_.run(); }) {
    std::srand(static_cast<unsigned>(NowMs()));
  }

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
    asio::post(io_context_, [this] {
      // Kicked 是终态,但允许玩家在重新 Connect() 时复位重来;显式
      // Connect() 同时取消待定的自动重连并清零退避计数。
      user_disconnect_ = false;
      reconnect_timer_.cancel();
      reconnect_attempts_ = 0;
      kicked_ = false;
      if (state_ == ConnectionState::Connected ||
          state_ == ConnectionState::LoggedIn ||
          state_ == ConnectionState::Connecting) {
        return;
      }
      StartConnect();
    });
  }

  void Disconnect() {
    asio::post(io_context_, [this] {
      user_disconnect_ = true;
      reconnect_timer_.cancel();
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
      if (state_ != ConnectionState::Connected && state_ != ConnectionState::LoggedIn) {
        cb(MakeEc(ChatError::NotConnected), "");
        return;
      }

      chirp::auth::LoginRequest req;
      req.set_token(token);
      req.set_device_id("sdk_device");
      req.set_platform("pc");
      // 前置的 state 检查保证 SendRequest 不会返回 0。
      SendRequest(MsgID::LOGIN_REQ, MsgID::LOGIN_RESP, req.SerializeAsString(),
          [this, cb = std::move(cb)](const std::error_code& ec, const std::string& body) mutable {
            if (ec) {
              cb(ec, "");
              return;
            }
            chirp::auth::LoginResponse resp;
            if (!resp.ParseFromString(body) || resp.code() != chirp::common::OK) {
              cb(MakeEc(ChatError::LoginFailed), "");
              return;
            }
            user_id_ = resp.user_id();
            session_id_ = resp.session_id();
            state_ = ConnectionState::LoggedIn;
            reconnect_attempts_ = 0;
            cb(std::error_code{}, user_id_);
          });
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
      SendPacket(MakePacket(MsgID::LOGOUT_REQ, req.SerializeAsString()));

      // 主动登出与断线不同:不应触发自动重连。
      user_disconnect_ = true;
      reconnect_timer_.cancel();
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
      // fire-and-forget:SEND_MESSAGE_RESP 迟到时找不到 pending 条目,
      // 走通用 stray-response 丢弃路径。需要确认送达时用 Request()。
      SendRequest(MsgID::SEND_MESSAGE_REQ, MsgID::SEND_MESSAGE_RESP, req.SerializeAsString(), nullptr);
    });
  }

  void Request(uint32_t msg_id_req, uint32_t msg_id_resp, const std::string& body,
               ResponseCallback cb) {
    asio::post(io_context_, [this, msg_id_req, msg_id_resp, body, cb = std::move(cb)]() mutable {
      if (state_ != ConnectionState::Connected && state_ != ConnectionState::LoggedIn) {
        if (cb) {
          cb(MakeEc(ChatError::NotConnected), "");
        }
        return;
      }
      // 状态已确认,SendRequest 必然发出。
      (void)SendRequest(msg_id_req, msg_id_resp, body, std::move(cb));
    });
  }

  NotifyHandle OnNotify(uint32_t msg_id, NotifyCallback cb) {
    std::lock_guard<std::mutex> lock(callbacks_mutex_);
    const auto handle = next_notify_handle_++;
    notify_subs_[msg_id][handle] = std::move(cb);
    return handle;
  }

  void OffNotify(NotifyHandle handle) {
    std::lock_guard<std::mutex> lock(callbacks_mutex_);
    for (auto& [msg_id, subs] : notify_subs_) {
      (void)msg_id;
      if (subs.erase(handle) > 0) {
        return;
      }
    }
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
  chirp::gateway::Packet MakePacket(MsgID msg_id, const std::string& body) {
    chirp::gateway::Packet pkt;
    pkt.set_msg_id(msg_id);
    pkt.set_body(body);
    return pkt;
  }

  // 注册 pending 并发送;返回 sequence(0 = 当前不能发送:未连接)。
  int64_t SendRequest(uint32_t msg_id_req, uint32_t msg_id_resp, const std::string& body,
                      ResponseCallback cb) {
    if (state_ != ConnectionState::Connected && state_ != ConnectionState::LoggedIn) {
      return 0;
    }

    auto pkt = MakePacket(static_cast<MsgID>(msg_id_req), body);
    pkt.set_sequence(next_seq_++);

    auto timer = std::make_shared<asio::steady_timer>(io_context_);
    const auto seq = pkt.sequence();
    timer->expires_after(std::chrono::milliseconds(config_.request_timeout_ms));
    timer->async_wait([this, seq](const std::error_code& timer_ec) {
      if (timer_ec) {
        return; // canceled:响应已到或连接已关
      }
      auto it = pending_.find(seq);
      if (it == pending_.end()) {
        return;
      }
      auto cb = std::move(it->second.cb);
      pending_.erase(it);
      if (cb) {
        cb(MakeEc(ChatError::Timeout), "");
      }
    });

    pending_.emplace(seq, PendingRequest{msg_id_resp, std::move(cb), timer});
    SendPacket(pkt);
    return seq;
  }

  void StartConnect() {
    if (config_.enable_websocket) {
      // TCP-only(见 ChatConfig 注释):WS 外壳待 app_gateway 聚合边缘
      // 定案后对齐,这里保持 fast-fail 语义。
      state_ = ConnectionState::Disconnected;
      NotifyDisconnect(MakeEc(ChatError::InvalidParam));
      return;
    }
    state_ = ConnectionState::Connecting;

    auto resolver = std::make_shared<asio::ip::tcp::resolver>(io_context_);
    auto socket = std::make_shared<asio::ip::tcp::socket>(io_context_);

    resolver->async_resolve(
        config_.gateway_host, std::to_string(config_.gateway_port),
        [this, resolver, socket](const std::error_code& ec, asio::ip::tcp::resolver::results_type results) {
          if (ec) {
            ConnectFailed(ec);
            return;
          }

          asio::async_connect(
              *socket, results,
              [this, socket](const std::error_code& connect_ec, const asio::ip::tcp::endpoint&) {
                if (connect_ec) {
                  ConnectFailed(connect_ec);
                  return;
                }

                socket_ = socket;
                framer_.Clear();
                write_q_.clear();
                write_in_flight_ = false;
                closed_ = false;
                user_id_.clear();
                session_id_.clear();
                next_seq_ = 1;
                pending_ping_seq_ = 0;
                missed_pongs_ = 0;
                reconnect_attempts_ = 0;
                state_ = ConnectionState::Connected;
                StartHeartbeat();
                DoRead();
              });
        });
  }

  // 初始 Connect() 或自动重连的失败路径:初始失败只通知(调用方决定是否
  // 再试),运行期自动重连的失败继续按退避表重试。
  void ConnectFailed(const std::error_code& ec) {
    const bool reconnecting = reconnect_attempts_ > 0;
    if (reconnecting) {
      state_ = ConnectionState::WaitingReconnect;
      ScheduleReconnect();
      return;
    }
    state_ = ConnectionState::Disconnected;
    NotifyDisconnect(ec);
  }

  void ScheduleReconnect() {
    if (kicked_ || user_disconnect_) {
      return;
    }
    if (config_.max_reconnect_attempts >= 0 &&
        reconnect_attempts_ >= config_.max_reconnect_attempts) {
      state_ = ConnectionState::Disconnected;
      return;
    }
    state_ = ConnectionState::WaitingReconnect;
    const auto delay = BackoffDelayMs(reconnect_attempts_);
    ++reconnect_attempts_;
    reconnect_timer_.expires_after(std::chrono::milliseconds(delay));
    reconnect_timer_.async_wait([this](const std::error_code& timer_ec) {
      if (timer_ec || kicked_ || user_disconnect_) {
        return;
      }
      StartConnect();
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
            OnConnectionLost(ec);
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

    if (pkt.sequence() != 0) {
      // 心跳回声:pong 必须回显 ping 的非零 sequence 才记为已答;不匹配
      // (或已判答)的 pong 无害丢弃。
      if (pkt.msg_id() == MsgID::HEARTBEAT_PONG) {
        if (pending_ping_seq_ != 0 && pending_ping_seq_ == pkt.sequence()) {
          pending_ping_seq_ = 0;
        }
        return;
      }
      // 响应包:sequence 关联 pending;不匹配的 stray(超时后的迟到响应、
      // fire-and-forget 的 SEND_MESSAGE_RESP)无害丢弃。
      auto it = pending_.find(pkt.sequence());
      if (it == pending_.end()) {
        return;
      }
      if (static_cast<uint32_t>(pkt.msg_id()) != it->second.resp_msg_id) {
        common::Logger::Instance().Warn("sdk: response msg_id mismatch seq=" +
                                std::to_string(pkt.sequence()));
        return;
      }
      auto cb = std::move(it->second.cb);
      it->second.timer->cancel();
      pending_.erase(it);
      if (cb) {
        cb(std::error_code{}, pkt.body());
      }
      return;
    }

    // notify 包(sequence==0)。
    switch (pkt.msg_id()) {
    case MsgID::KICK_NOTIFY:
      HandleKick(pkt);
      break;
    case MsgID::CHAT_MESSAGE_NOTIFY:
      HandleChatNotify(pkt);
      break;
    case MsgID::HEARTBEAT_PONG:
      // 服务端对非零 sequence 的 ping 回 pong(见上),这里只兜底 sequence
      // 缺失的异常 pong。
      common::Logger::Instance().Warn("sdk: heartbeat pong without sequence");
      break;
    default:
      DispatchNotify(pkt.msg_id(), pkt.body());
      break;
    }
  }

  void HandleKick(const chirp::gateway::Packet& pkt) {
    chirp::auth::KickNotify kick;
    if (!kick.ParseFromArray(pkt.body().data(), static_cast<int>(pkt.body().size()))) {
      return;
    }
    // 踢线是终态:先置状态再关连接,pending flush 才能拿到 Kicked 错误,
    // 且重连逻辑不会启动。
    kicked_ = true;
    state_ = ConnectionState::Kicked;
    DoClose(/*notify=*/false, std::error_code{});

    // 通用 notify 订阅者(与便捷回调平行)同样要看到 KICK。
    DispatchNotify(pkt.msg_id(), pkt.body());

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
    DispatchNotify(pkt.msg_id(), pkt.body());
  }

  void DispatchNotify(uint32_t msg_id, const std::string& body) {
    // 拷贝一份订阅快照再回调:回调里 OnNotify/OffNotify 不至于在遍历中
    // 改表。
    std::vector<NotifyCallback> snapshot;
    {
      std::lock_guard<std::mutex> lock(callbacks_mutex_);
      auto it = notify_subs_.find(msg_id);
      if (it == notify_subs_.end()) {
        return;
      }
      snapshot.reserve(it->second.size());
      for (auto& [handle, cb] : it->second) {
        (void)handle;
        snapshot.push_back(cb);
      }
    }
    for (auto& cb : snapshot) {
      if (cb) {
        cb(body);
      }
    }
  }

  void StartHeartbeat() {
    const int interval = config_.heartbeat_interval_seconds;
    if (interval <= 0) {
      return;
    }

    heartbeat_timer_.expires_after(std::chrono::seconds(interval));
    heartbeat_timer_.async_wait([this](const std::error_code& timer_ec) {
      if (timer_ec || !socket_) {
        return;
      }

      // 上一发 ping 还没等到回声:累计丢失;连续 max_missed_pongs 次即判定
      // 连接死亡(死网关/半开连接),走断线重连。
      if (pending_ping_seq_ != 0) {
        ++missed_pongs_;
        if (missed_pongs_ >= config_.max_missed_pongs) {
          OnConnectionLost(MakeEc(ChatError::Timeout));
          return;
        }
      } else {
        missed_pongs_ = 0;
      }

      chirp::gateway::HeartbeatPing ping;
      ping.set_timestamp(NowMs());
      auto pkt = MakePacket(MsgID::HEARTBEAT_PING, ping.SerializeAsString());
      pkt.set_sequence(next_seq_++);
      pending_ping_seq_ = pkt.sequence();
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
            OnConnectionLost(ec);
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

  // 运行中断线(读/写错误、心跳超时):flush 后通知并进入自动重连。
  void OnConnectionLost(const std::error_code& ec) {
    DoClose(/*notify=*/true, ec);
    if (!kicked_) {
      ScheduleReconnect();
    }
  }

  void DoClose(bool notify, const std::error_code& ec) {
    if (closed_) {
      return;
    }
    closed_ = true;
    heartbeat_timer_.cancel();
    pending_ping_seq_ = 0;
    missed_pongs_ = 0;

    // 断线错误码:显式断开用 Closed,踢线用 Kicked,其余透传底层错误。
    std::error_code flush_ec = ec;
    if (!flush_ec) {
      flush_ec = MakeEc(kicked_ ? ChatError::Kicked : ChatError::Closed);
    }

    auto pending = std::move(pending_);
    pending_.clear();
    for (auto& [seq, req] : pending) {
      (void)seq;
      req.timer->cancel();
      if (req.cb) {
        req.cb(flush_ec, "");
      }
    }

    if (socket_) {
      std::error_code ignore;
      socket_->shutdown(asio::ip::tcp::socket::shutdown_both, ignore);
      socket_->close(ignore);
      socket_.reset();
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
  asio::steady_timer reconnect_timer_;
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

  // 以下状态全部只在 io 线程触碰。
  std::unordered_map<int64_t, PendingRequest> pending_;
  int64_t pending_ping_seq_{0};
  int missed_pongs_{0};
  int reconnect_attempts_{0};
  bool kicked_{false};
  bool user_disconnect_{false};

  std::mutex callbacks_mutex_;
  uint64_t next_notify_handle_{1};
  std::unordered_map<uint32_t, std::unordered_map<uint64_t, NotifyCallback>> notify_subs_;
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

void ChatClient::Request(uint32_t msg_id_req, uint32_t msg_id_resp, const std::string& body,
                         ResponseCallback cb) {
  impl_->Request(msg_id_req, msg_id_resp, body, std::move(cb));
}

NotifyHandle ChatClient::OnNotify(uint32_t msg_id, NotifyCallback cb) {
  return impl_->OnNotify(msg_id, std::move(cb));
}

void ChatClient::OffNotify(NotifyHandle handle) {
  impl_->OffNotify(handle);
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
