#include "chirp/sdk_client.h"

#include <array>
#include <chrono>
#include <cstdlib>
#include <deque>
#include <functional>
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

#include "chirp/auth_provider.h"
#include "chirp/chat_event_listener.h"
#include "chirp/command_handler.h"
#include "chirp/message_interceptor.h"
#include "chirp/message_store.h"

#include "backoff.h"

namespace chirp {
namespace sdk {

namespace {

using chirp::gateway::MsgID;

int64_t NowMs() {
  using namespace std::chrono;
  return duration_cast<milliseconds>(system_clock::now().time_since_epoch()).count();
}

std::error_code MakeEc(ChatError e) { return make_error_code(e); }

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
        renew_timer_(io_context_),
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
      SetState(ConnectionState::Disconnected);
    });
  }

  void Login(const std::string& token, LoginCallback cb) {
    asio::post(io_context_, [this, token, cb = std::move(cb)]() mutable {
      // 注册了 AuthProvider 时空 token 由 provider 签发;显式 token 优先。
      std::string effective = token;
      if (effective.empty()) {
        if (auto provider = SnapshotAuthProvider()) {
          effective = provider->GetToken();
        }
      }
      if (effective.empty()) {
        cb(MakeEc(ChatError::InvalidParam), "");
        return;
      }
      if (state_ != ConnectionState::Connected && state_ != ConnectionState::LoggedIn) {
        cb(MakeEc(ChatError::NotConnected), "");
        return;
      }
      // 新一轮登录链:丢弃悬挂中的续期回调,重新允许一次 token 续期。
      if (auth_renewing_) {
        auth_renewing_ = false;
        auto stale = std::move(pending_renew_cb_);
        pending_renew_cb_ = nullptr;
        if (stale) {
          stale(MakeEc(ChatError::LoginFailed), "");
        }
      }
      renewal_used_ = false;
      renew_timer_.cancel();
      IssueLogin(effective, std::move(cb));
    });
  }

  // 发 LOGIN_REQ 并处理响应;首次登录与续期重登共用。io 线程调用。
  void IssueLogin(const std::string& token, LoginCallback cb) {
    chirp::auth::LoginRequest req;
    req.set_token(token);
    req.set_device_id("sdk_device");
    req.set_platform("pc");
    SendRequest(MsgID::LOGIN_REQ, MsgID::LOGIN_RESP, req.SerializeAsString(),
        [this, cb = std::move(cb)](const std::error_code& ec, const std::string& body) mutable {
          if (ec) {
            cb(ec, "");
            return;
          }
          chirp::auth::LoginResponse resp;
          const bool parsed = resp.ParseFromString(body);
          if (!parsed || resp.code() != chirp::common::OK) {
            // 解析失败没有可信 code,以 -1 上报。
            HandleLoginFailure(parsed ? static_cast<int>(resp.code()) : -1,
                               std::move(cb));
            return;
          }
          user_id_ = resp.user_id();
          session_id_ = resp.session_id();
          auth_renewing_ = false;
          renew_timer_.cancel();
          SetState(ConnectionState::LoggedIn);
          reconnect_attempts_ = 0;
          NotifyAuthOutcome(static_cast<int>(chirp::common::OK), user_id_);
          cb(std::error_code{}, user_id_);
        });
  }

  // 登录失败收尾:AUTH_FAILED 且注册了 provider 且本轮登录链尚未续期时,
  // 给一次 token 续期机会(OnTokenExpired -> renew -> IssueLogin);续期后
  // 仍失败不再续期,直接失败。
  void HandleLoginFailure(int code, LoginCallback cb) {
    if (code == static_cast<int>(chirp::common::AUTH_FAILED) && !renewal_used_) {
      if (auto provider = SnapshotAuthProvider()) {
        renewal_used_ = true;
        auth_renewing_ = true;
        pending_renew_cb_ = std::move(cb);
        NotifyAuthOutcome(code, "");
        // 游戏迟迟不调 renew:按设计文档进入 Disconnected 并回报失败。
        renew_timer_.expires_after(
            std::chrono::milliseconds(config_.request_timeout_ms));
        renew_timer_.async_wait([this](const std::error_code& timer_ec) {
          if (timer_ec || !auth_renewing_) {
            return;  // 已续期或连接已关;cb 由续期/DoClose 路径处理
          }
          auth_renewing_ = false;
          auto cb = std::move(pending_renew_cb_);
          pending_renew_cb_ = nullptr;
          // 续期超时 = 本轮会话认证失败:不进入自动重连(挂起的 read 会因
          // close 以 aborted 完成,提前标记避免它再拉起 ScheduleReconnect)。
          user_disconnect_ = true;
          DoClose(/*notify=*/false, std::error_code{});
          SetState(ConnectionState::Disconnected);
          if (cb) {
            cb(MakeEc(ChatError::LoginFailed), "");
          }
        });
        // renew 可能在游戏线程被异步调用,post 回 io 线程重登;与其它公开
        // 方法一样,client 析构后再调用属于调用方契约破坏。
        provider->OnTokenExpired([this](const std::string& new_token) {
          asio::post(io_context_, [this, new_token] {
            if (!auth_renewing_) {
              return;  // 已超时/已关闭/被新一轮登录取代
            }
            auth_renewing_ = false;  // 每轮登录链至多续期一次
            auto cb = std::move(pending_renew_cb_);
            pending_renew_cb_ = nullptr;
            renew_timer_.cancel();
            if (new_token.empty() || state_ != ConnectionState::Connected) {
              if (cb) {
                cb(MakeEc(ChatError::LoginFailed), "");
              }
              return;
            }
            IssueLogin(new_token, std::move(cb));
          });
        });
        return;
      }
    }
    NotifyAuthOutcome(code, "");
    if (cb) {
      cb(MakeEc(ChatError::LoginFailed), "");
    }
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
      SetState(ConnectionState::Disconnected);
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
      // 注册了命令时,'/xxx' 形态的消息走本地命令路由,不进聊天通道。
      // 零注册时保持旧行为:'/' 消息按普通文本发送(向后兼容)。
      if (!content.empty() && content.front() == '/' && HasCommands()) {
        if (!DispatchCommand(content)) {
          // 无 handler 认领:本地丢弃;unknown command 的提示由引擎层负责。
          common::Logger::Instance().Warn("sdk: unknown command: " + content);
        }
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

      auto interceptor = SnapshotInterceptor();
      if (interceptor && !interceptor->OnBeforeSend(req)) {
        common::Logger::Instance().Warn("sdk: send blocked by interceptor");
        return;
      }
      if (auto store = SnapshotStore()) {
        store->Save(MakeStoredSentMessage(req));
      }
      // fire-and-forget:SEND_MESSAGE_RESP 迟到时找不到 pending 条目,
      // 走通用 stray-response 丢弃路径。需要确认送达时用 Request()。
      SendRequest(MsgID::SEND_MESSAGE_REQ, MsgID::SEND_MESSAGE_RESP, req.SerializeAsString(), nullptr);
      if (interceptor) {
        interceptor->OnAfterSend(req);
      }
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
      SendRequest(msg_id_req, msg_id_resp, body, std::move(cb));
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

  // ---- 钩子注册(docs/design-notes/sdk_hooks.md):任意线程可调,须在
  // Connect() 之前完成。回调统一在 io 线程触发,锁内拷快照、锁外调用。
  void SetMessageInterceptor(std::shared_ptr<MessageInterceptor> interceptor) {
    std::lock_guard<std::mutex> lock(callbacks_mutex_);
    interceptor_ = std::move(interceptor);
  }

  void SetAuthProvider(std::shared_ptr<AuthProvider> provider) {
    std::lock_guard<std::mutex> lock(callbacks_mutex_);
    auth_provider_ = std::move(provider);
  }

  void SetMessageStore(std::unique_ptr<MessageStore> store) {
    std::lock_guard<std::mutex> lock(callbacks_mutex_);
    message_store_ = std::move(store);
  }

  void AddListener(std::shared_ptr<ChatEventListener> listener) {
    std::lock_guard<std::mutex> lock(callbacks_mutex_);
    listeners_.push_back(std::move(listener));
  }

  void RegisterCommand(std::unique_ptr<CommandHandler> handler) {
    std::lock_guard<std::mutex> lock(callbacks_mutex_);
    commands_.push_back(std::move(handler));
  }

  // MessageStore 转发查询:store 可从游戏线程直接访问(自定义 store 的
  // 线程安全由实现方负责,MemoryMessageStore 内置互斥)。未设置时 no-op。
  std::vector<chirp::chat::ChatMessage> LoadHistory(
      chirp::chat::ChannelType type, const std::string& channel_id,
      int limit, int64_t before_timestamp) {
    auto store = SnapshotStore();
    if (!store) {
      return {};
    }
    return store->Load(type, channel_id, limit, before_timestamp);
  }

  void MarkRead(chirp::chat::ChannelType type, const std::string& channel_id,
                const std::string& message_id) {
    if (auto store = SnapshotStore()) {
      store->MarkRead(type, channel_id, message_id);
    }
  }

  int GetUnreadCount(chirp::chat::ChannelType type,
                     const std::string& channel_id) {
    if (auto store = SnapshotStore()) {
      return store->GetUnreadCount(type, channel_id);
    }
    return 0;
  }

  void CleanupMessages(int64_t older_than) {
    if (auto store = SnapshotStore()) {
      store->Cleanup(older_than);
    }
  }

private:
  chirp::gateway::Packet MakePacket(MsgID msg_id, const std::string& body) {
    chirp::gateway::Packet pkt;
    pkt.set_msg_id(msg_id);
    pkt.set_body(body);
    return pkt;
  }

  bool HasCommands() {
    std::lock_guard<std::mutex> lock(callbacks_mutex_);
    return !commands_.empty();
  }

  // 本地命令路由:解析 "/cmd args"(首 token 为命令名,余串空格保留),
  // 按注册顺序找 GetName() 匹配的 handler 执行;Execute 返回 false 继续
  // 找下一个。返回是否被认领(false = 全 miss)。
  bool DispatchCommand(const std::string& content) {
    const std::string body = content.substr(1);
    const auto space = body.find(' ');
    const std::string name =
        body.substr(0, space == std::string::npos ? body.size() : space);
    const std::string args =
        space == std::string::npos ? "" : body.substr(space + 1);

    std::vector<std::shared_ptr<CommandHandler>> snapshot;
    {
      std::lock_guard<std::mutex> lock(callbacks_mutex_);
      snapshot = commands_;
    }
    for (auto& handler : snapshot) {
      if (handler && handler->GetName() == name) {
        return handler->Execute(args, user_id_);
      }
    }
    return false;
  }

  // 发送侧本地存档:用与请求等价的字段构造 ChatMessage(message_id 由
  // 服务端签发,fire-and-forget 拿不到,留空)。
  chirp::chat::ChatMessage MakeStoredSentMessage(
      const chirp::chat::SendMessageRequest& req) {
    chirp::chat::ChatMessage msg;
    msg.set_sender_id(req.sender_id());
    msg.set_receiver_id(req.receiver_id());
    msg.set_channel_type(req.channel_type());
    msg.set_channel_id(req.channel_id());
    msg.set_msg_type(req.msg_type());
    msg.set_content(req.content());
    msg.set_timestamp(req.client_timestamp());
    if (!req.reply_to_message_id().empty()) {
      msg.set_reply_to_message_id(req.reply_to_message_id());
    }
    return msg;
  }

  // 注册 pending 并发送。调用方必须已确认状态为 Connected/LoggedIn(三个
  // 调用点都先做了 state 检查);这里不再重复检查,保持写路径无死分支。
  void SendRequest(uint32_t msg_id_req, uint32_t msg_id_resp, const std::string& body,
                   ResponseCallback cb) {
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
  }

  void StartConnect() {
    if (config_.enable_websocket) {
      // TCP-only(见 ChatConfig 注释):WS 外壳待 app_gateway 聚合边缘
      // 定案后对齐,这里保持 fast-fail 语义。
      SetState(ConnectionState::Disconnected);
      NotifyDisconnect(MakeEc(ChatError::InvalidParam));
      return;
    }
    SetState(ConnectionState::Connecting);

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

                // 自动重连成功(而非初次连接)要单独通知;须在计数清零前判定。
                const bool reconnected = reconnect_attempts_ > 0;
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
                SetState(ConnectionState::Connected);
                StartHeartbeat();
                DoRead();
                if (reconnected) {
                  NotifyListeners([](ChatEventListener& l) { l.OnReconnected(); });
                }
              });
        });
  }

  // 初始 Connect() 或自动重连的失败路径:初始失败只通知(调用方决定是否
  // 再试),运行期自动重连的失败继续按退避表重试。
  void ConnectFailed(const std::error_code& ec) {
    const bool reconnecting = reconnect_attempts_ > 0;
    if (reconnecting) {
      SetState(ConnectionState::WaitingReconnect);
      ScheduleReconnect();
      return;
    }
    SetState(ConnectionState::Disconnected);
    NotifyDisconnect(ec);
  }

  void ScheduleReconnect() {
    if (kicked_ || user_disconnect_) {
      return;
    }
    if (config_.max_reconnect_attempts >= 0 &&
        reconnect_attempts_ >= config_.max_reconnect_attempts) {
      SetState(ConnectionState::Disconnected);
      return;
    }
    SetState(ConnectionState::WaitingReconnect);
    const int64_t delay = internal::BackoffDelayMs(reconnect_attempts_);
    ++reconnect_attempts_;
    NotifyListeners([attempt = reconnect_attempts_, delay](ChatEventListener& l) {
      l.OnReconnecting(attempt, static_cast<int>(delay));
    });
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
    SetState(ConnectionState::Kicked);
    DoClose(/*notify=*/false, std::error_code{});

    // 通用 notify 订阅者(与便捷回调平行)同样要看到 KICK。
    DispatchNotify(pkt.msg_id(), pkt.body());

    NotifyListeners([&kick](ChatEventListener& l) { l.OnKicked(kick.reason()); });

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
    auto interceptor = SnapshotInterceptor();
    if (interceptor && !interceptor->OnBeforeReceive(msg)) {
      // 拦截器丢弃:不存储、不触发任何回调,也不进入原始分发。
      return;
    }
    if (auto store = SnapshotStore()) {
      store->Save(msg);
    }
    MessageCallback cb;
    {
      std::lock_guard<std::mutex> lock(callbacks_mutex_);
      cb = on_message_;
    }
    if (cb) {
      cb(msg.sender_id(), msg.content());
    }
    if (interceptor) {
      interceptor->OnAfterReceive(msg);
    }
    NotifyListeners([&msg](ChatEventListener& l) { l.OnMessageReceived(msg); });
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
    renew_timer_.cancel();
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

    // 登录续期挂起时连接关闭:把悬挂的 Login 回调一并 flush,不让游戏空等。
    if (auth_renewing_) {
      auth_renewing_ = false;
      auto renew_cb = std::move(pending_renew_cb_);
      pending_renew_cb_ = nullptr;
      if (renew_cb) {
        renew_cb(flush_ec, "");
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

  // ---- 钩子快照辅助:锁内拷 shared_ptr,锁外调用,与既有回调拷贝同款。
  std::shared_ptr<MessageInterceptor> SnapshotInterceptor() {
    std::lock_guard<std::mutex> lock(callbacks_mutex_);
    return interceptor_;
  }

  std::shared_ptr<AuthProvider> SnapshotAuthProvider() {
    std::lock_guard<std::mutex> lock(callbacks_mutex_);
    return auth_provider_;
  }

  std::shared_ptr<MessageStore> SnapshotStore() {
    std::lock_guard<std::mutex> lock(callbacks_mutex_);
    return message_store_;
  }

  // 拷贝监听器快照后逐个 fan-out;回调里再调 AddListener 不会死锁。
  void NotifyListeners(const std::function<void(ChatEventListener&)>& fn) {
    std::vector<std::shared_ptr<ChatEventListener>> snapshot;
    {
      std::lock_guard<std::mutex> lock(callbacks_mutex_);
      snapshot = listeners_;
    }
    for (auto& listener : snapshot) {
      if (listener) {
        fn(*listener);
      }
    }
  }

  // 全部状态迁移收敛到这里:赋值后同步通知监听器(io 线程)。
  void SetState(ConnectionState state) {
    state_ = state;
    NotifyListeners([state](ChatEventListener& l) {
      l.OnConnectionStateChanged(static_cast<int>(state));
    });
  }

  // 服务端登录判定的统一出口:同时通知监听器(OnLoginResult)与认证提供者
  // (OnAuthResult)。本地参数校验(InvalidParam/NotConnected)不在此列。
  void NotifyAuthOutcome(int code, const std::string& user_id) {
    NotifyListeners([&](ChatEventListener& l) { l.OnLoginResult(code, user_id); });
    if (auto provider = SnapshotAuthProvider()) {
      provider->OnAuthResult(code, user_id);
    }
  }

public:
  // ---- 便捷 API(服务端往返)。Impl 方法在 io 线程执行;公开转发统一
  // asio::post。ec 只覆盖传输层(NotConnected/Timeout/Closed/Kicked)与
  // 协议异常(BadResponse);业务结果在 resp.code() 里,调用方自读。

  bool ReadyForRequests() const {
    const auto s = state_.load();
    return s == ConnectionState::Connected || s == ConnectionState::LoggedIn;
  }

  // 类型化请求:发 req,按模板参数解析响应;解析失败报 BadResponse。
  template <typename Resp>
  void TypedRequest(MsgID req_id, MsgID resp_id, const google::protobuf::Message& req,
                    std::function<void(const std::error_code&, const Resp&)> cb) {
    SendRequest(req_id, resp_id, req.SerializeAsString(),
        [cb = std::move(cb)](const std::error_code& ec, const std::string& body) mutable {
          if (ec) {
            cb(ec, Resp{});
            return;
          }
          Resp resp;
          if (!resp.ParseFromString(body)) {
            cb(MakeEc(ChatError::BadResponse), Resp{});
            return;
          }
          cb(std::error_code{}, resp);
        });
  }

  // 扩展发送:群/世界/私聊/引用一条龙。与 fire-and-forget 版不同,这里不
  // 做命令路由(调用方显式指定了完整语义);拦截器与本地存档行为一致。
  void SendMessage(const SendOptions& opts, const std::string& content,
                   SendResponseCallback cb) {
    asio::post(io_context_, [this, opts, content, cb = std::move(cb)] {
      if (!ReadyForRequests()) {
        cb(MakeEc(ChatError::NotConnected), {});
        return;
      }
      if (content.empty() ||
          (opts.channel_type == chirp::chat::PRIVATE && opts.receiver_id.empty())) {
        cb(MakeEc(ChatError::InvalidParam), {});
        return;
      }

      chirp::chat::SendMessageRequest req;
      req.set_sender_id(user_id_);
      req.set_channel_type(opts.channel_type);
      if (opts.channel_type == chirp::chat::PRIVATE) {
        req.set_receiver_id(opts.receiver_id);
        req.set_channel_id(user_id_ <= opts.receiver_id
                               ? (user_id_ + "|" + opts.receiver_id)
                               : (opts.receiver_id + "|" + user_id_));
      } else {
        if (opts.channel_id.empty()) {
          cb(MakeEc(ChatError::InvalidParam), {});
          return;
        }
        req.set_channel_id(opts.channel_id);
      }
      req.set_msg_type(chirp::chat::TEXT);
      req.set_content(content);
      req.set_client_timestamp(NowMs());
      if (!opts.reply_to_message_id.empty()) {
        req.set_reply_to_message_id(opts.reply_to_message_id);
      }

      auto interceptor = SnapshotInterceptor();
      if (interceptor && !interceptor->OnBeforeSend(req)) {
        common::Logger::Instance().Warn("sdk: send blocked by interceptor");
        cb(MakeEc(ChatError::SendFailed), {});
        return;
      }
      if (auto store = SnapshotStore()) {
        store->Save(MakeStoredSentMessage(req));
      }
      TypedRequest(MsgID::SEND_MESSAGE_REQ, MsgID::SEND_MESSAGE_RESP, req, std::move(cb));
      if (interceptor) {
        interceptor->OnAfterSend(req);
      }
    });
  }

  void FetchHistory(chirp::chat::ChannelType type, const std::string& channel_id,
                    int limit, int64_t before_timestamp, HistoryCallback cb) {
    asio::post(io_context_, [this, type, channel_id, limit, before_timestamp,
                             cb = std::move(cb)] {
      if (!ReadyForRequests()) {
        cb(MakeEc(ChatError::NotConnected), {});
        return;
      }
      chirp::chat::GetHistoryRequest req;
      req.set_user_id(user_id_);
      req.set_channel_type(type);
      req.set_channel_id(channel_id);
      req.set_before_timestamp(before_timestamp);
      req.set_limit(limit);
      TypedRequest(MsgID::GET_HISTORY_REQ, MsgID::GET_HISTORY_RESP, req, std::move(cb));
    });
  }

  void MarkChannelRead(chirp::chat::ChannelType type, const std::string& channel_id,
                       const std::string& message_id, MarkReadCallback cb) {
    asio::post(io_context_, [this, type, channel_id, message_id, cb = std::move(cb)] {
      if (!ReadyForRequests()) {
        cb(MakeEc(ChatError::NotConnected), {});
        return;
      }
      chirp::chat::MarkReadRequest req;
      req.set_user_id(user_id_);
      req.set_channel_type(type);
      req.set_channel_id(channel_id);
      req.set_message_id(message_id);
      req.set_read_timestamp(NowMs());
      TypedRequest(MsgID::MARK_READ_REQ, MsgID::MARK_READ_RESP, req, std::move(cb));
    });
  }

  void FetchUnreadCount(UnreadCountCallback cb) {
    asio::post(io_context_, [this, cb = std::move(cb)] {
      if (!ReadyForRequests()) {
        cb(MakeEc(ChatError::NotConnected), {});
        return;
      }
      chirp::chat::GetUnreadCountRequest req;
      req.set_user_id(user_id_);
      TypedRequest(MsgID::GET_UNREAD_COUNT_REQ, MsgID::GET_UNREAD_COUNT_RESP, req, std::move(cb));
    });
  }

  void BlockUser(const std::string& user_id, BlockSenderCallback cb) {
    asio::post(io_context_, [this, user_id, cb = std::move(cb)] {
      if (!ReadyForRequests()) {
        cb(MakeEc(ChatError::NotConnected), {});
        return;
      }
      chirp::chat::BlockMessageSenderRequest req;
      req.set_target_user_id(user_id);
      TypedRequest(MsgID::BLOCK_MESSAGE_SENDER_REQ, MsgID::BLOCK_MESSAGE_SENDER_RESP,
                   req, std::move(cb));
    });
  }

  void UnblockUser(const std::string& user_id, UnblockSenderCallback cb) {
    asio::post(io_context_, [this, user_id, cb = std::move(cb)] {
      if (!ReadyForRequests()) {
        cb(MakeEc(ChatError::NotConnected), {});
        return;
      }
      chirp::chat::UnblockMessageSenderRequest req;
      req.set_target_user_id(user_id);
      TypedRequest(MsgID::UNBLOCK_MESSAGE_SENDER_REQ, MsgID::UNBLOCK_MESSAGE_SENDER_RESP,
                   req, std::move(cb));
    });
  }

  void FetchBlockedUsers(BlockedSendersCallback cb) {
    asio::post(io_context_, [this, cb = std::move(cb)] {
      if (!ReadyForRequests()) {
        cb(MakeEc(ChatError::NotConnected), {});
        return;
      }
      TypedRequest(MsgID::GET_BLOCKED_SENDERS_REQ, MsgID::GET_BLOCKED_SENDERS_RESP,
                   chirp::chat::GetBlockedSendersRequest{}, std::move(cb));
    });
  }

  void SetChannelMute(chirp::chat::ChannelType type, bool muted, SetMuteCallback cb) {
    asio::post(io_context_, [this, type, muted, cb = std::move(cb)] {
      if (!ReadyForRequests()) {
        cb(MakeEc(ChatError::NotConnected), {});
        return;
      }
      chirp::chat::SetChannelMuteRequest req;
      req.set_channel_type(type);
      req.set_muted(muted);
      TypedRequest(MsgID::SET_CHANNEL_MUTE_REQ, MsgID::SET_CHANNEL_MUTE_RESP, req, std::move(cb));
    });
  }

  void FetchChannelMutes(ChannelMutesCallback cb) {
    asio::post(io_context_, [this, cb = std::move(cb)] {
      if (!ReadyForRequests()) {
        cb(MakeEc(ChatError::NotConnected), {});
        return;
      }
      TypedRequest(MsgID::GET_CHANNEL_MUTES_REQ, MsgID::GET_CHANNEL_MUTES_RESP,
                   chirp::chat::GetChannelMutesRequest{}, std::move(cb));
    });
  }

  // "正在输入"广播:NOTIFY 无响应帧,直接裸发(sequence 0),不进 pending。
  void SendTypingIndicator(chirp::chat::ChannelType type, const std::string& channel_id,
                           bool is_typing) {
    asio::post(io_context_, [this, type, channel_id, is_typing] {
      if (!ReadyForRequests()) {
        return;
      }
      chirp::chat::TypingIndicator req;
      req.set_channel_type(type);
      req.set_channel_id(channel_id);
      req.set_user_id(user_id_);
      req.set_is_typing(is_typing);
      req.set_timestamp(NowMs());
      auto pkt = MakePacket(MsgID::TYPING_INDICATOR_NOTIFY, req.SerializeAsString());
      pkt.set_sequence(0);
      SendPacket(pkt);
    });
  }

  void FetchTypingUsers(chirp::chat::ChannelType type, const std::string& channel_id,
                        TypingUsersCallback cb) {
    asio::post(io_context_, [this, type, channel_id, cb = std::move(cb)] {
      if (!ReadyForRequests()) {
        cb(MakeEc(ChatError::NotConnected), {});
        return;
      }
      chirp::chat::GetTypingUsersRequest req;
      req.set_channel_type(type);
      req.set_channel_id(channel_id);
      TypedRequest(MsgID::GET_TYPING_USERS_REQ, MsgID::GET_TYPING_USERS_RESP, req, std::move(cb));
    });
  }

  void EditMessage(const std::string& message_id, const std::string& content,
                   EditMessageCallback cb) {
    asio::post(io_context_, [this, message_id, content, cb = std::move(cb)] {
      if (!ReadyForRequests()) {
        cb(MakeEc(ChatError::NotConnected), {});
        return;
      }
      chirp::chat::EditMessageRequest req;
      req.set_message_id(message_id);
      req.set_user_id(user_id_);
      req.set_new_content(content);
      req.set_edit_timestamp(NowMs());
      TypedRequest(MsgID::EDIT_MESSAGE_REQ, MsgID::EDIT_MESSAGE_RESP, req, std::move(cb));
    });
  }

  void DeleteMessage(const std::string& message_id, bool hard_delete,
                     DeleteMessageCallback cb) {
    asio::post(io_context_, [this, message_id, hard_delete, cb = std::move(cb)] {
      if (!ReadyForRequests()) {
        cb(MakeEc(ChatError::NotConnected), {});
        return;
      }
      chirp::chat::DeleteMessageRequest req;
      req.set_message_id(message_id);
      req.set_user_id(user_id_);
      req.set_is_hard_delete(hard_delete);
      TypedRequest(MsgID::DELETE_MESSAGE_REQ, MsgID::DELETE_MESSAGE_RESP, req, std::move(cb));
    });
  }

  void RecallMessage(const std::string& message_id, DeleteMessageCallback cb) {
    asio::post(io_context_, [this, message_id, cb = std::move(cb)] {
      if (!ReadyForRequests()) {
        cb(MakeEc(ChatError::NotConnected), {});
        return;
      }
      // 撤回 = 软删：服务端按撤回窗口与可撤回频道判定（默认私聊/公会 2 分钟内），
      // 超窗回 INVALID_PARAM。hard_delete 仅版主语义，普通玩家用不到。
      chirp::chat::DeleteMessageRequest req;
      req.set_message_id(message_id);
      req.set_user_id(user_id_);
      TypedRequest(MsgID::DELETE_MESSAGE_REQ, MsgID::DELETE_MESSAGE_RESP, req, std::move(cb));
    });
  }

  void AddReaction(const std::string& message_id, const std::string& emoji,
                   AddReactionCallback cb) {
    asio::post(io_context_, [this, message_id, emoji, cb = std::move(cb)] {
      if (!ReadyForRequests()) {
        cb(MakeEc(ChatError::NotConnected), {});
        return;
      }
      chirp::chat::AddReactionRequest req;
      req.set_message_id(message_id);
      req.set_user_id(user_id_);
      req.set_emoji(emoji);
      TypedRequest(MsgID::ADD_REACTION_REQ, MsgID::ADD_REACTION_RESP, req, std::move(cb));
    });
  }

  void RemoveReaction(const std::string& message_id, const std::string& emoji,
                      RemoveReactionCallback cb) {
    asio::post(io_context_, [this, message_id, emoji, cb = std::move(cb)] {
      if (!ReadyForRequests()) {
        cb(MakeEc(ChatError::NotConnected), {});
        return;
      }
      chirp::chat::RemoveReactionRequest req;
      req.set_message_id(message_id);
      req.set_user_id(user_id_);
      req.set_emoji(emoji);
      TypedRequest(MsgID::REMOVE_REACTION_REQ, MsgID::REMOVE_REACTION_RESP, req, std::move(cb));
    });
  }

  void FetchReactions(const std::string& message_id, const std::string& emoji,
                      ReactionsCallback cb) {
    asio::post(io_context_, [this, message_id, emoji, cb = std::move(cb)] {
      if (!ReadyForRequests()) {
        cb(MakeEc(ChatError::NotConnected), {});
        return;
      }
      chirp::chat::GetReactionsRequest req;
      req.set_message_id(message_id);
      req.set_emoji(emoji);
      TypedRequest(MsgID::GET_REACTIONS_REQ, MsgID::GET_REACTIONS_RESP, req, std::move(cb));
    });
  }

  void FetchReadReceipts(const std::string& message_id, ReadReceiptsCallback cb) {
    asio::post(io_context_, [this, message_id, cb = std::move(cb)] {
      if (!ReadyForRequests()) {
        cb(MakeEc(ChatError::NotConnected), {});
        return;
      }
      chirp::chat::GetReadReceiptsRequest req;
      req.set_message_id(message_id);
      TypedRequest(MsgID::GET_READ_RECEIPTS_REQ, MsgID::GET_READ_RECEIPTS_RESP, req, std::move(cb));
    });
  }

  void BulkDeleteMessages(const std::vector<std::string>& message_ids,
                          const std::string& channel_id, BulkDeleteCallback cb) {
    asio::post(io_context_, [this, message_ids, channel_id, cb = std::move(cb)] {
      if (!ReadyForRequests()) {
        cb(MakeEc(ChatError::NotConnected), {});
        return;
      }
      chirp::chat::BulkDeleteRequest req;
      for (const auto& id : message_ids) {
        req.add_message_ids(id);
      }
      req.set_requester_id(user_id_);
      req.set_channel_id(channel_id);
      TypedRequest(MsgID::BULK_DELETE_REQ, MsgID::BULK_DELETE_RESP, req, std::move(cb));
    });
  }

  void FetchMentionSuggestions(const std::string& channel_id, const std::string& query,
                               MentionSuggestionsCallback cb) {
    asio::post(io_context_, [this, channel_id, query, cb = std::move(cb)] {
      if (!ReadyForRequests()) {
        cb(MakeEc(ChatError::NotConnected), {});
        return;
      }
      chirp::chat::GetMentionSuggestionsRequest req;
      req.set_user_id(user_id_);
      req.set_channel_id(channel_id);
      req.set_query(query);
      TypedRequest(MsgID::GET_MENTION_SUGGESTIONS_REQ, MsgID::GET_MENTION_SUGGESTIONS_RESP,
                   req, std::move(cb));
    });
  }

  void CreateGroup(const std::string& group_name, const std::string& description,
                   CreateGroupCallback cb) {
    asio::post(io_context_, [this, group_name, description, cb = std::move(cb)] {
      if (!ReadyForRequests()) {
        cb(MakeEc(ChatError::NotConnected), {});
        return;
      }
      chirp::chat::CreateGroupRequest req;
      req.set_creator_id(user_id_);
      req.set_group_name(group_name);
      req.set_description(description);
      TypedRequest(MsgID::CREATE_GROUP_REQ, MsgID::CREATE_GROUP_RESP, req, std::move(cb));
    });
  }

  void JoinGroup(const std::string& group_id, JoinGroupCallback cb) {
    asio::post(io_context_, [this, group_id, cb = std::move(cb)] {
      if (!ReadyForRequests()) {
        cb(MakeEc(ChatError::NotConnected), {});
        return;
      }
      chirp::chat::JoinGroupRequest req;
      req.set_user_id(user_id_);
      req.set_group_id(group_id);
      TypedRequest(MsgID::JOIN_GROUP_REQ, MsgID::JOIN_GROUP_RESP, req, std::move(cb));
    });
  }

  void LeaveGroup(const std::string& group_id, LeaveGroupCallback cb) {
    asio::post(io_context_, [this, group_id, cb = std::move(cb)] {
      if (!ReadyForRequests()) {
        cb(MakeEc(ChatError::NotConnected), {});
        return;
      }
      chirp::chat::LeaveGroupRequest req;
      req.set_user_id(user_id_);
      req.set_group_id(group_id);
      TypedRequest(MsgID::LEAVE_GROUP_REQ, MsgID::LEAVE_GROUP_RESP, req, std::move(cb));
    });
  }

  void InviteToGroup(const std::string& group_id, const std::string& user_id,
                     InviteToGroupCallback cb) {
    asio::post(io_context_, [this, group_id, user_id, cb = std::move(cb)] {
      if (!ReadyForRequests()) {
        cb(MakeEc(ChatError::NotConnected), {});
        return;
      }
      chirp::chat::InviteToGroupRequest req;
      req.set_inviter_id(user_id_);
      req.set_group_id(group_id);
      req.set_target_user_id(user_id);
      TypedRequest(MsgID::INVITE_TO_GROUP_REQ, MsgID::INVITE_TO_GROUP_RESP, req, std::move(cb));
    });
  }

  void KickMember(const std::string& group_id, const std::string& user_id,
                  KickMemberCallback cb) {
    asio::post(io_context_, [this, group_id, user_id, cb = std::move(cb)] {
      if (!ReadyForRequests()) {
        cb(MakeEc(ChatError::NotConnected), {});
        return;
      }
      chirp::chat::KickMemberRequest req;
      req.set_requester_id(user_id_);
      req.set_group_id(group_id);
      req.set_target_user_id(user_id);
      TypedRequest(MsgID::KICK_MEMBER_REQ, MsgID::KICK_MEMBER_RESP, req, std::move(cb));
    });
  }

  void FetchGroupInfo(const std::string& group_id, GroupInfoCallback cb) {
    asio::post(io_context_, [this, group_id, cb = std::move(cb)] {
      if (!ReadyForRequests()) {
        cb(MakeEc(ChatError::NotConnected), {});
        return;
      }
      chirp::chat::GetGroupInfoRequest req;
      req.set_group_id(group_id);
      TypedRequest(MsgID::GET_GROUP_INFO_REQ, MsgID::GET_GROUP_INFO_RESP, req, std::move(cb));
    });
  }

  void FetchGroupMembers(const std::string& group_id, int limit, int offset,
                         GroupMembersCallback cb) {
    asio::post(io_context_, [this, group_id, limit, offset, cb = std::move(cb)] {
      if (!ReadyForRequests()) {
        cb(MakeEc(ChatError::NotConnected), {});
        return;
      }
      chirp::chat::GetGroupMembersRequest req;
      req.set_group_id(group_id);
      req.set_limit(limit);
      req.set_offset(offset);
      TypedRequest(MsgID::GET_GROUP_MEMBERS_REQ, MsgID::GET_GROUP_MEMBERS_RESP, req, std::move(cb));
    });
  }

  void FetchUserGroups(int limit, int offset, UserGroupsCallback cb) {
    asio::post(io_context_, [this, limit, offset, cb = std::move(cb)] {
      if (!ReadyForRequests()) {
        cb(MakeEc(ChatError::NotConnected), {});
        return;
      }
      chirp::chat::GetUserGroupsRequest req;
      req.set_user_id(user_id_);
      req.set_limit(limit);
      req.set_offset(offset);
      TypedRequest(MsgID::GET_USER_GROUPS_REQ, MsgID::GET_USER_GROUPS_RESP, req, std::move(cb));
    });
  }

private:
  ChatConfig config_;
  std::atomic<ConnectionState> state_;
  asio::io_context io_context_;
  asio::executor_work_guard<asio::io_context::executor_type> work_;
  asio::steady_timer heartbeat_timer_;
  asio::steady_timer reconnect_timer_;
  // 登录续期超时(游戏迟迟不调 renew 则判失败);同样只在 io 线程操作。
  asio::steady_timer renew_timer_;
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

  // 登录续期(AUTH_FAILED -> OnTokenExpired -> renew):同样只在 io 线程。
  // auth_renewing_ = renew 在途;renewal_used_ = 本轮登录链已用过续期机会
  // (公开 Login() 复位,续期后的再次 AUTH_FAILED 不再续期)。
  bool auth_renewing_{false};
  bool renewal_used_{false};
  LoginCallback pending_renew_cb_;

  std::mutex callbacks_mutex_;
  uint64_t next_notify_handle_{1};
  std::unordered_map<uint32_t, std::unordered_map<uint64_t, NotifyCallback>> notify_subs_;
  MessageCallback on_message_;
  DisconnectCallback on_disconnect_;
  KickCallback on_kick_;

  // 钩子注册表(callbacks_mutex_ 保护)。入参 unique_ptr 的项内部转
  // shared_ptr,便于快照拷贝、锁外调用。
  std::shared_ptr<MessageInterceptor> interceptor_;
  std::shared_ptr<AuthProvider> auth_provider_;
  std::shared_ptr<MessageStore> message_store_;
  std::vector<std::shared_ptr<ChatEventListener>> listeners_;
  std::vector<std::shared_ptr<CommandHandler>> commands_;
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

void ChatClient::SetMessageInterceptor(std::shared_ptr<MessageInterceptor> interceptor) {
  impl_->SetMessageInterceptor(std::move(interceptor));
}

void ChatClient::SetAuthProvider(std::shared_ptr<AuthProvider> provider) {
  impl_->SetAuthProvider(std::move(provider));
}

void ChatClient::SetMessageStore(std::unique_ptr<MessageStore> store) {
  impl_->SetMessageStore(std::move(store));
}

void ChatClient::AddListener(std::shared_ptr<ChatEventListener> listener) {
  impl_->AddListener(std::move(listener));
}

void ChatClient::RegisterCommand(std::unique_ptr<CommandHandler> handler) {
  impl_->RegisterCommand(std::move(handler));
}

std::vector<chirp::chat::ChatMessage> ChatClient::LoadHistory(
    chirp::chat::ChannelType type, const std::string& channel_id,
    int limit, int64_t before_timestamp) {
  return impl_->LoadHistory(type, channel_id, limit, before_timestamp);
}

void ChatClient::MarkRead(chirp::chat::ChannelType type,
                          const std::string& channel_id,
                          const std::string& message_id) {
  impl_->MarkRead(type, channel_id, message_id);
}

int ChatClient::GetUnreadCount(chirp::chat::ChannelType type,
                               const std::string& channel_id) {
  return impl_->GetUnreadCount(type, channel_id);
}

void ChatClient::CleanupMessages(int64_t older_than) {
  impl_->CleanupMessages(older_than);
}

// ---- 便捷 API 转发:任意线程可调,Impl 内 post 到 io 线程执行。

void ChatClient::SendMessage(const SendOptions& opts, const std::string& content,
                             SendResponseCallback cb) {
  impl_->SendMessage(opts, content, std::move(cb));
}

void ChatClient::FetchHistory(chirp::chat::ChannelType type, const std::string& channel_id,
                              int limit, int64_t before_timestamp, HistoryCallback cb) {
  impl_->FetchHistory(type, channel_id, limit, before_timestamp, std::move(cb));
}

void ChatClient::MarkChannelRead(chirp::chat::ChannelType type, const std::string& channel_id,
                                 const std::string& message_id, MarkReadCallback cb) {
  impl_->MarkChannelRead(type, channel_id, message_id, std::move(cb));
}

void ChatClient::FetchUnreadCount(UnreadCountCallback cb) {
  impl_->FetchUnreadCount(std::move(cb));
}

void ChatClient::BlockUser(const std::string& user_id, BlockSenderCallback cb) {
  impl_->BlockUser(user_id, std::move(cb));
}

void ChatClient::UnblockUser(const std::string& user_id, UnblockSenderCallback cb) {
  impl_->UnblockUser(user_id, std::move(cb));
}

void ChatClient::FetchBlockedUsers(BlockedSendersCallback cb) {
  impl_->FetchBlockedUsers(std::move(cb));
}

void ChatClient::SetChannelMute(chirp::chat::ChannelType type, bool muted,
                                SetMuteCallback cb) {
  impl_->SetChannelMute(type, muted, std::move(cb));
}

void ChatClient::FetchChannelMutes(ChannelMutesCallback cb) {
  impl_->FetchChannelMutes(std::move(cb));
}

void ChatClient::SendTypingIndicator(chirp::chat::ChannelType type,
                                     const std::string& channel_id, bool is_typing) {
  impl_->SendTypingIndicator(type, channel_id, is_typing);
}

void ChatClient::FetchTypingUsers(chirp::chat::ChannelType type,
                                  const std::string& channel_id, TypingUsersCallback cb) {
  impl_->FetchTypingUsers(type, channel_id, std::move(cb));
}

void ChatClient::EditMessage(const std::string& message_id, const std::string& content,
                             EditMessageCallback cb) {
  impl_->EditMessage(message_id, content, std::move(cb));
}

void ChatClient::DeleteMessage(const std::string& message_id, bool hard_delete,
                               DeleteMessageCallback cb) {
  impl_->DeleteMessage(message_id, hard_delete, std::move(cb));
}

void ChatClient::RecallMessage(const std::string& message_id, DeleteMessageCallback cb) {
  impl_->RecallMessage(message_id, std::move(cb));
}

void ChatClient::AddReaction(const std::string& message_id, const std::string& emoji,
                             AddReactionCallback cb) {
  impl_->AddReaction(message_id, emoji, std::move(cb));
}

void ChatClient::RemoveReaction(const std::string& message_id, const std::string& emoji,
                                RemoveReactionCallback cb) {
  impl_->RemoveReaction(message_id, emoji, std::move(cb));
}

void ChatClient::FetchReactions(const std::string& message_id, const std::string& emoji,
                                ReactionsCallback cb) {
  impl_->FetchReactions(message_id, emoji, std::move(cb));
}

void ChatClient::FetchReadReceipts(const std::string& message_id, ReadReceiptsCallback cb) {
  impl_->FetchReadReceipts(message_id, std::move(cb));
}

void ChatClient::BulkDeleteMessages(const std::vector<std::string>& message_ids,
                                    const std::string& channel_id, BulkDeleteCallback cb) {
  impl_->BulkDeleteMessages(message_ids, channel_id, std::move(cb));
}

void ChatClient::FetchMentionSuggestions(const std::string& channel_id,
                                         const std::string& query,
                                         MentionSuggestionsCallback cb) {
  impl_->FetchMentionSuggestions(channel_id, query, std::move(cb));
}

void ChatClient::CreateGroup(const std::string& group_name, const std::string& description,
                             CreateGroupCallback cb) {
  impl_->CreateGroup(group_name, description, std::move(cb));
}

void ChatClient::JoinGroup(const std::string& group_id, JoinGroupCallback cb) {
  impl_->JoinGroup(group_id, std::move(cb));
}

void ChatClient::LeaveGroup(const std::string& group_id, LeaveGroupCallback cb) {
  impl_->LeaveGroup(group_id, std::move(cb));
}

void ChatClient::InviteToGroup(const std::string& group_id, const std::string& user_id,
                               InviteToGroupCallback cb) {
  impl_->InviteToGroup(group_id, user_id, std::move(cb));
}

void ChatClient::KickMember(const std::string& group_id, const std::string& user_id,
                            KickMemberCallback cb) {
  impl_->KickMember(group_id, user_id, std::move(cb));
}

void ChatClient::FetchGroupInfo(const std::string& group_id, GroupInfoCallback cb) {
  impl_->FetchGroupInfo(group_id, std::move(cb));
}

void ChatClient::FetchGroupMembers(const std::string& group_id, int limit, int offset,
                                   GroupMembersCallback cb) {
  impl_->FetchGroupMembers(group_id, limit, offset, std::move(cb));
}

void ChatClient::FetchUserGroups(int limit, int offset, UserGroupsCallback cb) {
  impl_->FetchUserGroups(limit, offset, std::move(cb));
}

} // namespace sdk
} // namespace chirp
