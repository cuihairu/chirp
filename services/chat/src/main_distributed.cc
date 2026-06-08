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
#include "distributed_dispatch.h"
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
    local_sessions.erase(it->second);
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

  std::shared_ptr<chirp::network::RedisClient> redis;
  int offline_ttl_seconds{0};
};

void HandleSendMessage(const chirp::chat::SendMessageRequest& req,
                       const std::shared_ptr<chirp::network::Session>& sender_session,
                       const std::shared_ptr<DistributedChatState>& state,
                       const std::shared_ptr<DistributedMessageStore>& store,
                       const std::shared_ptr<chirp::network::MessageRouter>& router,
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

  store->AddToHistory(channel_id, msg.SerializeAsString());

  chirp::chat::SendMessageResponse resp;
  resp.set_code(chirp::common::OK);
  resp.set_message_id(msg.message_id());
  resp.set_server_timestamp(msg.timestamp());
  chirp::chat::runtime::SendPacket(sender_session, chirp::gateway::SEND_MESSAGE_RESP, seq, resp.SerializeAsString());

  if (req.channel_type() == chirp::chat::PRIVATE) {
    router->SendChatMessage(
        req.receiver_id(), msg.SerializeAsString(), [&](const std::string& user_id) -> bool {
          auto recv_session = state->GetLocalSession(user_id);
          if (!recv_session) {
            return false;
          }
          chirp::chat::runtime::SendChatNotify(recv_session, msg);
          Logger::Instance().Info("Message delivered locally to " + user_id);
          return true;
        });

    if (!state->IsUserLocal(req.receiver_id())) {
      store->AddOffline(req.receiver_id(), msg.SerializeAsString());
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
                 int64_t seq) {
  const std::string user_id = req.token();

  chirp::auth::LoginResponse resp;
  if (!user_id.empty()) {
    resp.set_code(chirp::common::OK);
    resp.set_user_id(user_id);
    resp.set_session_id(state->instance_id + "_" + std::to_string(chirp::chat::runtime::NowMs()));

    state->AddSession(user_id, session);

    router->SubscribeUserChat(user_id, [session](const std::string& msg_data) {
      chirp::chat::ChatMessage msg;
      if (msg.ParseFromArray(msg_data.data(), static_cast<int>(msg_data.size()))) {
        chirp::chat::runtime::SendChatNotify(session, msg);
      }
    });

    Logger::Instance().Info("User logged in: " + user_id + " on instance " + state->instance_id);

    const auto offline_msgs = store->PopOffline(user_id);
    Logger::Instance().Info(
        "Delivering " + std::to_string(offline_msgs.size()) + " offline messages to " + user_id);
    for (const auto& msg_data : offline_msgs) {
      chirp::chat::ChatMessage msg;
      if (msg.ParseFromArray(msg_data.data(), static_cast<int>(msg_data.size()))) {
        chirp::chat::runtime::SendChatNotify(session, msg);
      }
    }
  } else {
    resp.set_code(chirp::common::INVALID_PARAM);
  }
  resp.set_server_time(chirp::chat::runtime::NowMs());

  chirp::chat::runtime::SendPacket(session, chirp::gateway::LOGIN_RESP, seq, resp.SerializeAsString());
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

  std::string instance_id = chirp::chat::runtime::GetArg(argc, argv, "--instance_id", "");
  if (instance_id.empty()) {
    instance_id = "chat_" + chirp::chat::runtime::RandomHex(8);
  }

  Logger::Instance().Info("chirp_chat_distributed starting");
  Logger::Instance().Info("  instance_id: " + instance_id);
  Logger::Instance().Info("  tcp_port: " + std::to_string(port));
  Logger::Instance().Info("  ws_port: " + std::to_string(ws_port));
  Logger::Instance().Info("  redis: " + redis_host + ":" + std::to_string(redis_port));

  asio::io_context io;

  auto state = std::make_shared<DistributedChatState>();
  state->instance_id = instance_id;

  auto store = std::make_shared<DistributedMessageStore>();
  store->redis = std::make_shared<chirp::network::RedisClient>(redis_host, redis_port);
  store->offline_ttl_seconds = offline_ttl;

  auto router = std::make_shared<chirp::network::MessageRouter>(io, redis_host, redis_port);
  if (!router->Start()) {
    Logger::Instance().Error("Failed to start message router");
    return 1;
  }

  chirp::chat::runtime::DistributedDispatchHandlers handlers;
  handlers.on_login = [state, store, router](const std::shared_ptr<chirp::network::Session>& session,
                                             const chirp::auth::LoginRequest& req,
                                             int64_t seq) {
    HandleLogin(req, session, state, store, router, seq);
  };
  handlers.on_send_message = [state, store, router](const std::shared_ptr<chirp::network::Session>& session,
                                                    const chirp::chat::SendMessageRequest& req,
                                                    int64_t seq) {
    HandleSendMessage(req, session, state, store, router, seq);
  };
  handlers.on_get_history = [store](const std::shared_ptr<chirp::network::Session>& session,
                                    const chirp::chat::GetHistoryRequest& req,
                                    int64_t seq) {
    HandleGetHistory(req, session, store, seq);
  };
  handlers.on_logout = [state](const std::shared_ptr<chirp::network::Session>& session,
                               const chirp::auth::LogoutRequest&,
                               int64_t seq) {
    state->RemoveSession(session.get());
    chirp::auth::LogoutResponse resp;
    resp.set_code(chirp::common::OK);
    resp.set_server_time(chirp::chat::runtime::NowMs());
    chirp::chat::runtime::SendPacket(session, chirp::gateway::LOGOUT_RESP, seq, resp.SerializeAsString());
  };

  auto on_packet = [handlers](const std::shared_ptr<chirp::network::Session>& session,
                              const chirp::gateway::Packet& pkt) {
    chirp::chat::runtime::DispatchDistributedPacket(session, pkt, handlers);
  };

  auto tcp_disconnect = [state](const std::shared_ptr<chirp::network::Session>& session) {
    const std::string user_id = state->GetUserId(session.get());
    if (!user_id.empty()) {
      Logger::Instance().Info("User disconnected: " + user_id);
    }
    state->RemoveSession(session.get());
  };

  auto ws_disconnect = [state](const std::shared_ptr<chirp::network::Session>& session) {
    state->RemoveSession(session.get());
  };

  auto server = chirp::chat::runtime::MakeDistributedTcpServer(io, port, on_packet, tcp_disconnect);
  auto ws_server = chirp::chat::runtime::MakeDistributedWsServer(io, ws_port, on_packet, ws_disconnect);

  server->Start();
  ws_server->Start();

  Logger::Instance().Info(
      "Chat service started, listening on TCP:" + std::to_string(port) + " WS:" + std::to_string(ws_port));

  chirp::chat::runtime::InstallSignalStop(io, [&]() {
    Logger::Instance().Info("Shutting down chat service...");
    server->Stop();
    ws_server->Stop();
    router->Stop();
    io.stop();
  });
  io.run();
  Logger::Instance().Info("chirp_chat_distributed exited");
  return 0;
}
