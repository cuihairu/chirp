#include <atomic>
#include <chrono>
#include <cstdint>
#include <cstdlib>
#include <memory>
#include <mutex>
#include <random>
#include <string>
#include <unordered_map>
#include <vector>

#include <asio.hpp>

#include "logger.h"
#include "delivery_ack_manager.h"
#include "distributed_dispatch.h"
#include "login_token_verifier.h"
#include "network/notification_client.h"
#include "push_bridge.h"
#include "distributed_runtime.h"
#include "network/message_router.h"
#include "network/redis_client.h"
#include "network/session.h"
#include "network/tcp_server.h"
#include "network/websocket_server.h"
#include "proto/auth.pb.h"
#include "proto/chat.pb.h"
#include "proto/common.pb.h"
#include "runtime_utils.h"

namespace {

using chirp::common::Logger;

struct DistributedChatState {
  void AddSession(const std::string& user_id, const std::shared_ptr<chirp::network::Session>& session) {
    std::lock_guard<std::mutex> lock(mu);
    local_sessions[user_id] = session;
    session_to_user[session.get()] = user_id;
  }

  void RemoveSession(chirp::network::Session* session) {
    std::lock_guard<std::mutex> lock(mu);
    const auto it = session_to_user.find(session);
    if (it == session_to_user.end()) {
      return;
    }
    // Only clear the user slot while it still points at THIS session: a
    // newer login for the same user may already own it, and a stale
    // disconnect (e.g. the send client's late FIN) must not unregister
    // the current session.
    const auto sit = local_sessions.find(it->second);
    if (sit != local_sessions.end() && sit->second.lock().get() == session) {
      local_sessions.erase(sit);
    }
    session_to_user.erase(it);
  }

  std::shared_ptr<chirp::network::Session> GetLocalSession(const std::string& user_id) {
    std::lock_guard<std::mutex> lock(mu);
    const auto it = local_sessions.find(user_id);
    if (it == local_sessions.end()) {
      return nullptr;
    }
    return it->second.lock();
  }
  bool IsUserLocal(const std::string& user_id) {
    std::lock_guard<std::mutex> lock(mu);
    const auto it = local_sessions.find(user_id);
    return it != local_sessions.end() && !it->second.expired();
  }

  std::string GetUserId(chirp::network::Session* session) {
    std::lock_guard<std::mutex> lock(mu);
    const auto it = session_to_user.find(session);
    return it != session_to_user.end() ? it->second : "";
  }

  std::mutex mu;
  std::unordered_map<std::string, std::weak_ptr<chirp::network::Session>> local_sessions;
  std::unordered_map<void*, std::string> session_to_user;
  std::string instance_id;
};

struct DistributedMessageStore {
  std::string OfflineKey(const std::string& user_id) const {
    return "chirp:chat:offline:" + user_id;
  }

  std::string HistoryKey(const std::string& channel_id) const {
    return "chirp:chat:history:" + channel_id;
  }

  std::string PrivateChannelId(const std::string& a, const std::string& b) const {
    return a < b ? a + "|" + b : b + "|" + a;
  }

  void AddOffline(const std::string& receiver_id, const std::string& message) const {
    if (!redis || receiver_id.empty()) {
      return;
    }
    redis->RPush(OfflineKey(receiver_id), message);
    redis->Expire(OfflineKey(receiver_id), offline_ttl_seconds);
  }

  // Delivery-ack requeue path: pushes the exact bytes that were tracked so a
  // late ack can remove the copy byte-for-byte.
  void AddOfflineBytes(const std::string& receiver_id, const std::string& bytes) const {
    AddOffline(receiver_id, bytes);
  }

  // Late-ack cleanup: drop the offline copy the client confirmed after it had
  // already been requeued (byte-for-byte match against the queued entry).
  bool RemoveOffline(const std::string& receiver_id, const std::string& bytes) const {
    if (!redis || receiver_id.empty()) {
      return false;
    }
    return redis->LRem(OfflineKey(receiver_id), 1, bytes) > 0;
  }

  std::vector<std::string> PopOffline(const std::string& user_id) const {
    if (!redis || user_id.empty()) {
      return {};
    }
    auto messages = redis->LRange(OfflineKey(user_id), 0, -1);
    redis->Del(OfflineKey(user_id));
    return messages;
  }

  void AddToHistory(const std::string& channel_id, const std::string& message) const {
    if (!redis || channel_id.empty()) {
      return;
    }
    redis->RPush(HistoryKey(channel_id), message);
  }

  std::vector<std::string> GetHistory(const std::string& channel_id, int limit) const {
    if (!redis) {
      return {};
    }
    if (limit <= 0) {
      limit = 50;
    }
    return redis->LRange(HistoryKey(channel_id), -limit, -1);
  }

  // 消息引用（game_chat_features P1）：回复目标是否仍在该会话的历史列表里。
  // 历史只存于 Redis（序列化 ChatMessage），全列表扫描即完整判据。
  bool HasMessage(const std::string& channel_id, const std::string& message_id) const {
    if (!redis || channel_id.empty() || message_id.empty()) {
      return false;
    }
    for (const auto& raw : redis->LRange(HistoryKey(channel_id), 0, -1)) {
      chirp::chat::ChatMessage msg;
      if (msg.ParseFromArray(raw.data(), static_cast<int>(raw.size())) &&
          msg.message_id() == message_id) {
        return true;
      }
    }
    return false;
  }

  std::shared_ptr<chirp::network::RedisClient> redis;
  int offline_ttl_seconds{0};
};

void HandleSendMessage(const chirp::chat::SendMessageRequest& req,
                       const std::shared_ptr<chirp::network::Session>& sender_session,
                       const std::shared_ptr<DistributedChatState>& state,
                       const std::shared_ptr<DistributedMessageStore>& store,
                       const std::shared_ptr<chirp::network::MessageRouter>& router,
                       chirp::chat::PushBridge& push,
                       chirp::chat::DeliveryAckManager* acks,
                       int64_t seq) {
  chirp::chat::ChatMessage msg;
  msg.set_message_id(chirp::chat::runtime::GenerateMessageId());
  msg.set_sender_id(req.sender_id());
  msg.set_receiver_id(req.receiver_id());
  msg.set_channel_type(req.channel_type());
  msg.set_msg_type(req.msg_type());
  msg.set_content(req.content());
  msg.set_timestamp(chirp::chat::runtime::NowMs());

  std::string channel_id;
  if (req.channel_type() == chirp::chat::PRIVATE) {
    if (req.receiver_id().empty()) {
      chirp::chat::SendMessageResponse resp;
      resp.set_code(chirp::common::INVALID_PARAM);
      resp.set_server_timestamp(chirp::chat::runtime::NowMs());
      chirp::chat::runtime::SendPacket(sender_session, chirp::gateway::SEND_MESSAGE_RESP, seq, resp.SerializeAsString());
      return;
    }
    channel_id = store->PrivateChannelId(req.sender_id(), req.receiver_id());
  } else {
    channel_id = req.channel_id();
  }
  msg.set_channel_id(channel_id);

  // 消息引用（game_chat_features P1）：回复目标必须存在于同一会话的历史里，
  // 否则拒绝——悬空引用让客户端渲染不出被引用消息的摘要。
  if (!req.reply_to_message_id().empty() &&
      !store->HasMessage(channel_id, req.reply_to_message_id())) {
    chirp::chat::SendMessageResponse resp;
    resp.set_code(chirp::common::INVALID_PARAM);
    resp.set_server_timestamp(chirp::chat::runtime::NowMs());
    chirp::chat::runtime::SendPacket(sender_session, chirp::gateway::SEND_MESSAGE_RESP,
                                     seq, resp.SerializeAsString());
    return;
  }
  msg.set_reply_to_message_id(req.reply_to_message_id());

  store->AddToHistory(channel_id, msg.SerializeAsString());

  chirp::chat::SendMessageResponse resp;
  resp.set_code(chirp::common::OK);
  resp.set_message_id(msg.message_id());
  resp.set_server_timestamp(msg.timestamp());
  chirp::chat::runtime::SendPacket(sender_session, chirp::gateway::SEND_MESSAGE_RESP, seq, resp.SerializeAsString());

  if (req.channel_type() == chirp::chat::PRIVATE) {
    // Delivered when a live session got it - locally, or via another
    // instance's user subscription (the PUBLISH receiver count says so).
    // Only a message that reached nobody goes to the offline queue, and
    // that same condition fires the notification-plane push.
    const std::string msg_bytes = msg.SerializeAsString();
    const int64_t receivers = router->SendChatMessageCount(
        req.receiver_id(), msg_bytes, [&](const std::string& user_id) -> bool {
          auto recv_session = state->GetLocalSession(user_id);
          // A receiver whose connection already sent FIN would "consume" the
          // message without ever reading it; report not-delivered so the
          // caller queues it offline. Ack-capable receivers additionally
          // hold the delivery until MESSAGE_ACK.
          if (!recv_session || recv_session->PeerHalfClosed()) {
            return false;
          }
          if (acks && acks->IsCapable(recv_session.get())) {
            acks->Track(msg.message_id(), user_id, msg_bytes);
          }
          chirp::chat::runtime::SendChatNotify(recv_session, msg);
          Logger::Instance().Info("Message delivered locally to " + user_id);
          return true;
        });

    if (receivers <= 0) {
      store->AddOffline(req.receiver_id(), msg.SerializeAsString());
      push.NotifyOffline(msg, req.receiver_id());
      Logger::Instance().Info("Message stored offline for " + req.receiver_id());
    }
  } else {
    router->BroadcastToGroup(channel_id, msg.SerializeAsString());
  }
}

void HandleLogin(const chirp::auth::LoginRequest& req,
                 const std::shared_ptr<chirp::network::Session>& session,
                 const std::shared_ptr<DistributedChatState>& state,
                 const std::shared_ptr<DistributedMessageStore>& store,
                 const std::shared_ptr<chirp::network::MessageRouter>& router,
                 const chirp::common::LoginTokenVerifier* token_verifier,
                 chirp::chat::DeliveryAckManager* acks,
                 int64_t seq) {
  std::string user_id;

  // With a shared secret configured, the token must be an HS256 JWT with
  // an exp claim and the user in sub (same contract as the basic build).
  if (token_verifier != nullptr && token_verifier->enabled()) {
    std::string verify_err;
    if (!token_verifier->Verify(req.token(), chirp::chat::runtime::NowMs(),
                                &user_id, &verify_err)) {
      Logger::Instance().Warn("chat login rejected: " + verify_err);
      chirp::auth::LoginResponse deny;
      deny.set_code(chirp::common::AUTH_FAILED);
      deny.set_server_time(chirp::chat::runtime::NowMs());
      chirp::chat::runtime::SendPacket(session, chirp::gateway::LOGIN_RESP, seq,
                                       deny.SerializeAsString());
      return;
    }
  } else {
    // Scaffolding login: treat token as user_id.
    user_id = req.token();
  }

  chirp::auth::LoginResponse resp;
  if (!user_id.empty()) {
    resp.set_code(chirp::common::OK);
    resp.set_user_id(user_id);
    resp.set_session_id(state->instance_id + "_" + std::to_string(chirp::chat::runtime::NowMs()));

    state->AddSession(user_id, session);

    if (acks && req.supports_message_ack()) {
      acks->MarkCapable(session);
    }

    router->SubscribeUserChat(user_id, [session, state, acks, user_id](const std::string& msg_data) {
      chirp::chat::ChatMessage msg;
      if (msg.ParseFromArray(msg_data.data(), static_cast<int>(msg_data.size()))) {
        // Cross-instance deliveries are tracked like local ones - this
        // instance owns the receiving session, so the ack comes back here.
        if (acks && acks->IsCapable(session.get())) {
          acks->Track(msg.message_id(), user_id, msg_data);
        }
        chirp::chat::runtime::SendChatNotify(session, msg);
      }
    });

    Logger::Instance().Info("User logged in: " + user_id + " on instance " + state->instance_id);
  } else {
    resp.set_code(chirp::common::INVALID_PARAM);
  }
  resp.set_server_time(chirp::chat::runtime::NowMs());

  chirp::chat::runtime::SendPacket(session, chirp::gateway::LOGIN_RESP, seq, resp.SerializeAsString());

  // Deliver offline messages only after the LOGIN_RESP (same order as the
  // basic build): a refill notify sent first would be swallowed by clients
  // that read exactly one frame as "the login response".
  if (!user_id.empty()) {
    const auto offline_msgs = store->PopOffline(user_id);
    Logger::Instance().Info(
        "Delivering " + std::to_string(offline_msgs.size()) + " offline messages to " + user_id);
    for (const auto& msg_data : offline_msgs) {
      chirp::chat::ChatMessage msg;
      if (msg.ParseFromArray(msg_data.data(), static_cast<int>(msg_data.size()))) {
        // Refills are tracked like live deliveries (payload keeps the exact
        // bytes that were queued, so a late ack can remove them cleanly).
        if (acks && acks->IsCapable(session.get())) {
          acks->Track(msg.message_id(), user_id, msg_data);
        }
        chirp::chat::runtime::SendChatNotify(session, msg);
      }
    }
  }
}

void HandleGetHistory(const chirp::chat::GetHistoryRequest& req,
                      const std::shared_ptr<chirp::network::Session>& session,
                      const std::shared_ptr<DistributedMessageStore>& store,
                      int64_t seq) {
  chirp::chat::GetHistoryResponse resp;
  resp.set_code(chirp::common::OK);

  const auto history_data = store->GetHistory(req.channel_id(), req.limit());
  for (const auto& msg_data : history_data) {
    chirp::chat::ChatMessage* msg = resp.add_messages();
    if (!msg->ParseFromArray(msg_data.data(), static_cast<int>(msg_data.size()))) {
      resp.mutable_messages()->RemoveLast();
    }
  }

  resp.set_has_more(false);
  chirp::chat::runtime::SendPacket(session, chirp::gateway::GET_HISTORY_RESP, seq, resp.SerializeAsString());
}

}  // namespace

int main(int argc, char** argv) {
  Logger::Instance().SetLevel(Logger::Level::kInfo);

  const uint16_t port = chirp::chat::runtime::ParseU16Arg(argc, argv, "--port", 7000);
  const uint16_t ws_port = chirp::chat::runtime::ParseU16Arg(argc, argv, "--ws_port", static_cast<uint16_t>(port + 1));
  const std::string redis_host = chirp::chat::runtime::GetArg(argc, argv, "--redis_host", "127.0.0.1");
  const uint16_t redis_port = chirp::chat::runtime::ParseU16Arg(argc, argv, "--redis_port", 6379);
  const int offline_ttl = chirp::chat::runtime::ParseIntArg(argc, argv, "--offline_ttl", 604800);
  const std::string token_secret = chirp::chat::runtime::GetArg(argc, argv, "--token_secret", "");
  const std::string notification_host = chirp::chat::runtime::GetArg(argc, argv, "--notification_host", "");
  const uint16_t notification_port = chirp::chat::runtime::ParseU16Arg(argc, argv, "--notification_port", 5006);
  // 0 disables client delivery-ack tracking entirely (kill switch).
  const int64_t ack_timeout_ms = chirp::chat::runtime::ParseIntArg(argc, argv, "--ack_timeout_ms", 10000);

  std::string instance_id = chirp::chat::runtime::GetArg(argc, argv, "--instance_id", "");
  if (instance_id.empty()) {
    instance_id = "chat_" + chirp::chat::runtime::RandomHex(8);
  }

  Logger::Instance().Info("chirp_chat_distributed starting");
  Logger::Instance().Info("  instance_id: " + instance_id);
  Logger::Instance().Info("  tcp_port: " + std::to_string(port));
  Logger::Instance().Info("  ws_port: " + std::to_string(ws_port));
  Logger::Instance().Info("  redis: " + redis_host + ":" + std::to_string(redis_port));
  Logger::Instance().Info(
      "  auth=" + std::string(token_secret.empty() ? "scaffold" : "hmac-sha256") +
      (notification_host.empty()
           ? ""
           : " notification=" + notification_host + ":" + std::to_string(notification_port)));

  asio::io_context io;

  // With a shared secret, LOGIN tokens are verified locally as HS256 JWTs;
  // empty keeps the scaffolding login (token = user id).
  chirp::common::LoginTokenVerifier token_verifier(token_secret);

  // Offline pushes are wired only when a notification service is
  // configured; a null client makes the bridge a no-op.
  std::shared_ptr<chirp::app_notification::NotificationClient> notification;
  if (!notification_host.empty()) {
    notification = std::make_shared<chirp::app_notification::NotificationClient>(
        io, notification_host, notification_port);
  }
  chirp::chat::PushBridge push(notification);

  auto state = std::make_shared<DistributedChatState>();
  state->instance_id = instance_id;

  auto store = std::make_shared<DistributedMessageStore>();
  store->redis = std::make_shared<chirp::network::RedisClient>(redis_host, redis_port);
  store->offline_ttl_seconds = offline_ttl;

  // Client delivery-ack bookkeeping: live deliveries to ack-capable sessions
  // stay pending until MESSAGE_ACK; the timeout hands them back to the
  // offline queue (no push notification - the receiver was just online).
  chirp::chat::DeliveryAckManager::Config ack_config;
  ack_config.timeout_ms = ack_timeout_ms;
  auto acks = std::make_shared<chirp::chat::DeliveryAckManager>(
      io, ack_config,
      [store](const std::string& receiver_id, const std::string& payload) {
        store->AddOfflineBytes(receiver_id, payload);
      },
      [store](const std::string& receiver_id, const std::string& payload) {
        store->RemoveOffline(receiver_id, payload);
      });

  auto router = std::make_shared<chirp::network::MessageRouter>(io, redis_host, redis_port);
  if (!router->Start()) {
    Logger::Instance().Error("Failed to start message router");
    return 1;
  }

  chirp::chat::runtime::DistributedDispatchHandlers handlers;
  handlers.on_login = [state, store, router, &token_verifier, acks](
                          const std::shared_ptr<chirp::network::Session>& session,
                          const chirp::auth::LoginRequest& req,
                          int64_t seq) {
    HandleLogin(req, session, state, store, router, &token_verifier, acks.get(), seq);
  };
  handlers.on_send_message = [state, store, router, &push, acks](
                                 const std::shared_ptr<chirp::network::Session>& session,
                                 const chirp::chat::SendMessageRequest& req,
                                 int64_t seq) {
    HandleSendMessage(req, session, state, store, router, push, acks.get(), seq);
  };
  handlers.on_get_history = [store](const std::shared_ptr<chirp::network::Session>& session,
                                    const chirp::chat::GetHistoryRequest& req,
                                    int64_t seq) {
    HandleGetHistory(req, session, store, seq);
  };
  handlers.on_logout = [state, acks](const std::shared_ptr<chirp::network::Session>& session,
                                     const chirp::auth::LogoutRequest&,
                                     int64_t seq) {
    acks->ForgetSession(session.get());
    state->RemoveSession(session.get());
    chirp::auth::LogoutResponse resp;
    resp.set_code(chirp::common::OK);
    resp.set_server_time(chirp::chat::runtime::NowMs());
    chirp::chat::runtime::SendPacket(session, chirp::gateway::LOGOUT_RESP, seq, resp.SerializeAsString());
  };
  handlers.on_message_ack = [state, acks](
                                const std::shared_ptr<chirp::network::Session>& session,
                                const chirp::chat::MessageAck& req,
                                int64_t /*seq*/) {
    const std::string user_id = state->GetUserId(session.get());
    if (req.message_id().empty() || user_id.empty() ||
        (!req.user_id().empty() && req.user_id() != user_id)) {
      return;
    }
    if (acks->Acknowledge(req.message_id())) {
      Logger::Instance().Info("message acked id=" + req.message_id() + " user=" + user_id);
    }
  };

  auto on_packet = [handlers](const std::shared_ptr<chirp::network::Session>& session,
                              const chirp::gateway::Packet& pkt) {
    chirp::chat::runtime::DispatchDistributedPacket(session, pkt, handlers);
  };

  auto tcp_disconnect = [state, acks](const std::shared_ptr<chirp::network::Session>& session) {
    const std::string user_id = state->GetUserId(session.get());
    if (!user_id.empty()) {
      Logger::Instance().Info("User disconnected: " + user_id);
    }
    acks->ForgetSession(session.get());
    state->RemoveSession(session.get());
  };

  auto ws_disconnect = [state, acks](const std::shared_ptr<chirp::network::Session>& session) {
    acks->ForgetSession(session.get());
    state->RemoveSession(session.get());
  };

  // Acceptors bind in their constructors; a busy port must exit gracefully
  // (non-zero) instead of letting the system_error terminate the process.
  std::unique_ptr<chirp::network::TcpServer> server;
  std::unique_ptr<chirp::network::WebSocketServer> ws_server;
  try {
    server = chirp::chat::runtime::MakeDistributedTcpServer(io, port, on_packet, tcp_disconnect);
    ws_server = chirp::chat::runtime::MakeDistributedWsServer(io, ws_port, on_packet, ws_disconnect);
  } catch (const std::system_error& e) {
    Logger::Instance().Error(std::string("chat service failed to bind listen ports: ") + e.what());
    return 1;
  }

  server->Start();
  ws_server->Start();
  acks->Start();

  Logger::Instance().Info(
      "Chat service started, listening on TCP:" + std::to_string(port) + " WS:" + std::to_string(ws_port));

  chirp::chat::runtime::InstallSignalStop(io, [&]() {
    Logger::Instance().Info("Shutting down chat service...");
    acks->Stop();
    server->Stop();
    ws_server->Stop();
    router->Stop();
    io.stop();
  });
  io.run();
  Logger::Instance().Info("chirp_chat_distributed exited");
  return 0;
}
