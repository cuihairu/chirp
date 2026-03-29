#include <chrono>
#include <cstdint>
#include <cstdlib>
#include <memory>
#include <mutex>
#include <random>
#include <string>
#include <unordered_map>
#include <unordered_set>
#include <vector>

#include <asio.hpp>

#include "logger.h"
#include "network/protobuf_framing.h"
#include "network/redis_client.h"
#include "network/session.h"
#include "network/tcp_server.h"
#include "network/websocket_server.h"
#include "proto/auth.pb.h"
#include "proto/chat.pb.h"
#include "proto/common.pb.h"
#include "proto/gateway.pb.h"
#include "proto/social.pb.h"

namespace {

int64_t NowMs() {
  using namespace std::chrono;
  return duration_cast<milliseconds>(system_clock::now().time_since_epoch()).count();
}

std::string GetArg(int argc, char** argv, const std::string& key, const std::string& def) {
  for (int i = 1; i < argc; i++) {
    if (argv[i] == key && i + 1 < argc) {
      return argv[i + 1];
    }
  }
  return def;
}

uint16_t ParseU16Arg(int argc, char** argv, const std::string& key, uint16_t def) {
  return static_cast<uint16_t>(std::atoi(GetArg(argc, argv, key, std::to_string(def)).c_str()));
}

std::string RandomHex(size_t bytes) {
  static thread_local std::mt19937_64 rng{std::random_device{}()};
  std::uniform_int_distribution<uint32_t> dist(0, 255);
  static const char* kHex = "0123456789abcdef";

  std::string out;
  out.resize(bytes * 2);
  for (size_t i = 0; i < bytes; i++) {
    uint8_t b = static_cast<uint8_t>(dist(rng));
    out[i * 2] = kHex[(b >> 4) & 0xF];
    out[i * 2 + 1] = kHex[b & 0xF];
  }
  return out;
}

// Social service state
struct SocialState {
  std::mutex mu;

  // Friend relationships: user_id -> set of friend_ids
  std::unordered_map<std::string, std::unordered_set<std::string>> friends;

  // Pending friend requests: request_id -> request
  std::unordered_map<std::string, chirp::social::FriendRequest> pending_requests;

  // Blocked users: user_id -> set of blocked_user_ids
  std::unordered_map<std::string, std::unordered_set<std::string>> blocked;

  // Presence: user_id -> presence info
  std::unordered_map<std::string, chirp::social::PresenceInfo> presence;

  // Session tracking for notifications
  std::unordered_map<std::string, std::weak_ptr<chirp::network::Session>> user_to_session;
  std::unordered_map<void*, std::string> session_to_user;
};

void SendPacket(const std::shared_ptr<chirp::network::Session>& session,
                chirp::gateway::MsgID msg_id,
                int64_t seq,
                const std::string& body) {
  chirp::gateway::Packet pkt;
  pkt.set_msg_id(msg_id);
  pkt.set_sequence(seq);
  pkt.set_body(body);
  auto framed = chirp::network::ProtobufFraming::Encode(pkt);
  session->Send(std::string(reinterpret_cast<const char*>(framed.data()), framed.size()));
}

void BroadcastToFriends(const std::shared_ptr<SocialState>& state,
                        const std::string& user_id,
                        chirp::gateway::MsgID msg_id,
                        const std::string& body) {
  std::vector<std::shared_ptr<chirp::network::Session>> targets;
  {
    std::lock_guard<std::mutex> lock(state->mu);
    auto it = state->friends.find(user_id);
    if (it != state->friends.end()) {
      for (const auto& friend_id : it->second) {
        auto sess_it = state->user_to_session.find(friend_id);
        if (sess_it != state->user_to_session.end()) {
          auto sess = sess_it->second.lock();
          if (sess) {
            targets.push_back(sess);
          }
        }
      }
    }
  }

  for (auto& sess : targets) {
    SendPacket(sess, msg_id, 0, body);
  }
}

void HandleAddFriend(const std::shared_ptr<SocialState>& state,
                     const std::shared_ptr<chirp::network::Session>& session,
                     const chirp::gateway::Packet& pkt) {
  chirp::social::AddFriendRequest req;
  if (!req.ParseFromArray(pkt.body().data(), static_cast<int>(pkt.body().size()))) {
    chirp::social::AddFriendResponse resp;
    resp.set_code(chirp::common::INVALID_PARAM);
    resp.set_server_time(NowMs());
    SendPacket(session, chirp::gateway::ADD_FRIEND_RESP, pkt.sequence(), resp.SerializeAsString());
    return;
  }

  if (req.user_id().empty() || req.target_user_id().empty() || req.user_id() == req.target_user_id()) {
    chirp::social::AddFriendResponse resp;
    resp.set_code(chirp::common::INVALID_PARAM);
    resp.set_server_time(NowMs());
    SendPacket(session, chirp::gateway::ADD_FRIEND_RESP, pkt.sequence(), resp.SerializeAsString());
    return;
  }

  std::string request_id = RandomHex(16);
  {
    std::lock_guard<std::mutex> lock(state->mu);

    // Check if already friends
    auto it = state->friends.find(req.user_id());
    if (it != state->friends.end() && it->second.count(req.target_user_id())) {
      chirp::social::AddFriendResponse resp;
      resp.set_code(chirp::common::INTERNAL_ERROR);  // Already friends
      resp.set_server_time(NowMs());
      SendPacket(session, chirp::gateway::ADD_FRIEND_RESP, pkt.sequence(), resp.SerializeAsString());
      return;
    }

    // Check if blocked
    auto blocked_it = state->blocked.find(req.target_user_id());
    if (blocked_it != state->blocked.end() && blocked_it->second.count(req.user_id())) {
      chirp::social::AddFriendResponse resp;
      resp.set_code(chirp::common::INTERNAL_ERROR);  // Blocked
      resp.set_server_time(NowMs());
      SendPacket(session, chirp::gateway::ADD_FRIEND_RESP, pkt.sequence(), resp.SerializeAsString());
      return;
    }

    // Create pending request
    chirp::social::FriendRequest friend_req;
    friend_req.set_from_user_id(req.user_id());
    friend_req.set_to_user_id(req.target_user_id());
    friend_req.set_message(req.message());
    friend_req.set_timestamp(NowMs());
    state->pending_requests[request_id] = friend_req;
  }

  // Send notification to target user
  auto target_session_it = state->user_to_session.find(req.target_user_id());
  if (target_session_it != state->user_to_session.end()) {
    auto target_sess = target_session_it->second.lock();
    if (target_sess) {
      chirp::social::FriendRequestNotify notify;
      notify.set_request_id(request_id);
      notify.set_from_user_id(req.user_id());
      notify.set_message(req.message());
      notify.set_timestamp(NowMs());
      SendPacket(target_sess, chirp::gateway::FRIEND_REQUEST_NOTIFY, 0, notify.SerializeAsString());
    }
  }

  chirp::social::AddFriendResponse resp;
  resp.set_code(chirp::common::OK);
  resp.set_request_id(request_id);
  resp.set_server_time(NowMs());
  SendPacket(session, chirp::gateway::ADD_FRIEND_RESP, pkt.sequence(), resp.SerializeAsString());
}

void HandleFriendRequestAction(const std::shared_ptr<SocialState>& state,
                               const std::shared_ptr<chirp::network::Session>& session,
                               const chirp::gateway::Packet& pkt) {
  chirp::social::FriendRequestAction req;
  if (!req.ParseFromArray(pkt.body().data(), static_cast<int>(pkt.body().size()))) {
    chirp::social::FriendRequestActionResponse resp;
    resp.set_code(chirp::common::INVALID_PARAM);
    resp.set_server_time(NowMs());
    SendPacket(session, chirp::gateway::FRIEND_REQUEST_ACTION_RESP, pkt.sequence(), resp.SerializeAsString());
    return;
  }

  std::string from_user_id;
  {
    std::lock_guard<std::mutex> lock(state->mu);
    auto it = state->pending_requests.find(req.request_id());
    if (it == state->pending_requests.end()) {
      chirp::social::FriendRequestActionResponse resp;
      resp.set_code(chirp::common::INVALID_PARAM);
      resp.set_server_time(NowMs());
      SendPacket(session, chirp::gateway::FRIEND_REQUEST_ACTION_RESP, pkt.sequence(), resp.SerializeAsString());
      return;
    }

    from_user_id = it->second.from_user_id();

    if (req.accept()) {
      // Add bidirectional friendship
      state->friends[it->second.from_user_id()].insert(it->second.to_user_id());
      state->friends[it->second.to_user_id()].insert(it->second.from_user_id());
    }

    state->pending_requests.erase(it);
  }

  if (req.accept()) {
    // Notify both users
    chirp::social::FriendAcceptedNotify notify;
    notify.set_user_id(from_user_id);
    notify.set_timestamp(NowMs());

    auto it1 = state->user_to_session.find(from_user_id);
    if (it1 != state->user_to_session.end()) {
      auto sess = it1->second.lock();
      if (sess) {
        SendPacket(sess, chirp::gateway::FRIEND_ACCEPTED_NOTIFY, 0, notify.SerializeAsString());
      }
    }
  }

  chirp::social::FriendRequestActionResponse resp;
  resp.set_code(chirp::common::OK);
  resp.set_server_time(NowMs());
  SendPacket(session, chirp::gateway::FRIEND_REQUEST_ACTION_RESP, pkt.sequence(), resp.SerializeAsString());
}

void HandleSetPresence(const std::shared_ptr<SocialState>& state,
                       const std::shared_ptr<chirp::network::Session>& session,
                       const chirp::gateway::Packet& pkt,
                       std::shared_ptr<chirp::network::RedisClient> redis) {
  chirp::social::SetPresenceRequest req;
  if (!req.ParseFromArray(pkt.body().data(), static_cast<int>(pkt.body().size()))) {
    chirp::social::SetPresenceResponse resp;
    resp.set_code(chirp::common::INVALID_PARAM);
    resp.set_server_time(NowMs());
    SendPacket(session, chirp::gateway::SET_PRESENCE_RESP, pkt.sequence(), resp.SerializeAsString());
    return;
  }

  chirp::social::PresenceNotify notify;
  notify.set_user_id(req.user_id());
  notify.set_status(req.status());
  notify.set_status_message(req.status_message());
  notify.set_timestamp(NowMs());
  for (const auto& kv : req.metadata()) {
    (*notify.mutable_metadata())[kv.first] = kv.second;
  }

  {
    std::lock_guard<std::mutex> lock(state->mu);
    chirp::social::PresenceInfo info;
    info.set_user_id(req.user_id());
    info.set_status(req.status());
    info.set_status_message(req.status_message());
    info.set_last_seen(NowMs());
    for (const auto& kv : req.metadata()) {
      (*info.mutable_metadata())[kv.first] = kv.second;
    }
    state->presence[req.user_id()] = info;
  }

  // Store in Redis for cross-instance presence
  if (redis) {
    std::string key = "chirp:social:presence:" + req.user_id();
    redis->SetEx(key, notify.SerializeAsString(), 3600);  // 1 hour TTL
  }

  // Broadcast to friends
  BroadcastToFriends(state, req.user_id(), chirp::gateway::PRESENCE_NOTIFY, notify.SerializeAsString());

  chirp::social::SetPresenceResponse resp;
  resp.set_code(chirp::common::OK);
  resp.set_server_time(NowMs());
  SendPacket(session, chirp::gateway::SET_PRESENCE_RESP, pkt.sequence(), resp.SerializeAsString());
}

void HandleGetPresence(const std::shared_ptr<SocialState>& state,
                       const std::shared_ptr<chirp::network::Session>& session,
                       const chirp::gateway::Packet& pkt) {
  chirp::social::GetPresenceRequest req;
  if (!req.ParseFromArray(pkt.body().data(), static_cast<int>(pkt.body().size()))) {
    chirp::social::GetPresenceResponse resp;
    resp.set_code(chirp::common::INVALID_PARAM);
    SendPacket(session, chirp::gateway::GET_PRESENCE_RESP, pkt.sequence(), resp.SerializeAsString());
    return;
  }

  chirp::social::GetPresenceResponse resp;
  resp.set_code(chirp::common::OK);

  std::lock_guard<std::mutex> lock(state->mu);
  for (const auto& user_id : req.user_ids()) {
    auto it = state->presence.find(user_id);
    if (it != state->presence.end()) {
      *resp.add_presences() = it->second;
    } else {
      // Return offline presence
      chirp::social::PresenceInfo info;
      info.set_user_id(user_id);
      info.set_status(chirp::social::OFFLINE);
      resp.add_presences()->CopyFrom(info);
    }
  }

  SendPacket(session, chirp::gateway::GET_PRESENCE_RESP, pkt.sequence(), resp.SerializeAsString());
}

void HandleDisconnect(const std::shared_ptr<SocialState>& state,
                     const std::shared_ptr<chirp::network::Session>& session) {
  std::lock_guard<std::mutex> lock(state->mu);
  auto it = state->session_to_user.find(session.get());
  if (it == state->session_to_user.end()) {
    return;
  }
  const std::string user_id = it->second;
  state->session_to_user.erase(it);

  auto it2 = state->user_to_session.find(user_id);
  if (it2 != state->user_to_session.end()) {
    auto cur = it2->second.lock();
    if (!cur || cur.get() == session.get()) {
      state->user_to_session.erase(it2);
    }
  }

  // Set presence to offline
  auto presence_it = state->presence.find(user_id);
  if (presence_it != state->presence.end()) {
    presence_it->second.set_status(chirp::social::OFFLINE);
    presence_it->second.set_last_seen(NowMs());
  }
}

void HandlePacket(const std::shared_ptr<SocialState>& state,
                  const std::shared_ptr<chirp::network::RedisClient>& redis,
                  const std::shared_ptr<chirp::network::Session>& session,
                  std::string&& payload) {
  using chirp::common::Logger;

  chirp::gateway::Packet pkt;
  if (!pkt.ParseFromArray(payload.data(), static_cast<int>(payload.size()))) {
    Logger::Instance().Warn("failed to parse Packet from client");
    return;
  }

  switch (pkt.msg_id()) {
  case chirp::gateway::LOGIN_REQ: {
    // Scaffolding login
    chirp::auth::LoginRequest login_req;
    if (!login_req.ParseFromArray(pkt.body().data(), static_cast<int>(pkt.body().size()))) {
      chirp::auth::LoginResponse resp;
      resp.set_code(chirp::common::INVALID_PARAM);
      resp.set_server_time(NowMs());
      SendPacket(session, chirp::gateway::LOGIN_RESP, pkt.sequence(), resp.SerializeAsString());
      return;
    }
    const std::string user_id = login_req.token();

    chirp::auth::LoginResponse login_resp;
    if (user_id.empty()) {
      login_resp.set_code(chirp::common::INVALID_PARAM);
    } else {
      login_resp.set_code(chirp::common::OK);
      login_resp.set_user_id(user_id);
      login_resp.set_session_id("social_session_" + user_id);
    }
    login_resp.set_server_time(NowMs());

    {
      std::lock_guard<std::mutex> lock(state->mu);
      state->user_to_session[user_id] = session;
      state->session_to_user[session.get()] = user_id;
    }

    SendPacket(session, chirp::gateway::LOGIN_RESP, pkt.sequence(), login_resp.SerializeAsString());
    break;
  }
  case chirp::gateway::ADD_FRIEND_REQ:
    HandleAddFriend(state, session, pkt);
    break;
  case chirp::gateway::FRIEND_REQUEST_ACTION_REQ:
    HandleFriendRequestAction(state, session, pkt);
    break;
  case chirp::gateway::SET_PRESENCE_REQ:
    HandleSetPresence(state, session, pkt, redis);
    break;
  case chirp::gateway::GET_PRESENCE_REQ:
    HandleGetPresence(state, session, pkt);
    break;
  case chirp::gateway::HEARTBEAT_PING: {
    chirp::gateway::HeartbeatPing ping;
    if (!ping.ParseFromArray(pkt.body().data(), static_cast<int>(pkt.body().size()))) {
      Logger::Instance().Warn("failed to parse HeartbeatPing body");
      return;
    }

    chirp::gateway::HeartbeatPong pong;
    pong.set_timestamp(ping.timestamp());
    pong.set_server_time(NowMs());
    SendPacket(session, chirp::gateway::HEARTBEAT_PONG, pkt.sequence(), pong.SerializeAsString());
    break;
  }
  default:
    break;
  }
}

} // namespace

int main(int argc, char** argv) {
  using chirp::common::Logger;

  Logger::Instance().SetLevel(Logger::Level::kInfo);
  const uint16_t port = ParseU16Arg(argc, argv, "--port", 8000);
  const uint16_t ws_port = ParseU16Arg(argc, argv, "--ws_port", static_cast<uint16_t>(port + 1));
  const std::string redis_host = GetArg(argc, argv, "--redis_host", "");
  const uint16_t redis_port = ParseU16Arg(argc, argv, "--redis_port", 6379);

  Logger::Instance().Info("chirp_social starting tcp=" + std::to_string(port) + " ws=" + std::to_string(ws_port) +
                          (redis_host.empty() ? "" : (" redis=" + redis_host + ":" + std::to_string(redis_port))));

  asio::io_context io;

  std::shared_ptr<chirp::network::RedisClient> redis;
  if (!redis_host.empty()) {
    redis = std::make_shared<chirp::network::RedisClient>(redis_host, redis_port);
  }

  auto state = std::make_shared<SocialState>();

  chirp::network::TcpServer server(
      io, port,
      [state, redis](std::shared_ptr<chirp::network::Session> session, std::string&& payload) {
        HandlePacket(state, redis, session, std::move(payload));
      },
      [state](std::shared_ptr<chirp::network::Session> session) { HandleDisconnect(state, session); });

  chirp::network::WebSocketServer ws_server(
      io, ws_port,
      [state, redis](std::shared_ptr<chirp::network::Session> session, std::string&& payload) {
        HandlePacket(state, redis, session, std::move(payload));
      },
      [state](std::shared_ptr<chirp::network::Session> session) { HandleDisconnect(state, session); });

  server.Start();
  ws_server.Start();

  asio::signal_set signals(io, SIGINT, SIGTERM);
  signals.async_wait([&](const std::error_code& /*ec*/, int /*sig*/) {
    Logger::Instance().Info("shutdown requested");
    server.Stop();
    ws_server.Stop();
    io.stop();
  });

  io.run();
  Logger::Instance().Info("chirp_social exited");
  return 0;
}
