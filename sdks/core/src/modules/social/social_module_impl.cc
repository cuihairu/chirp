#include "social_module_impl.h"

#include <chrono>

namespace chirp {
namespace core {
namespace modules {
namespace social {

int64_t NowMs() {
  return std::chrono::duration_cast<std::chrono::milliseconds>(
      std::chrono::system_clock::now().time_since_epoch()).count();
}

SocialModuleImpl::SocialModuleImpl(asio::io_context& io,
                                   const std::string& host,
                                   uint16_t port,
                                   bool use_websocket)
    : io_(io),
      host_(host),
      port_(port),
      use_websocket_(use_websocket),
      connected_(false),
      receiving_(false),
      sequence_(0) {
  if (use_websocket) {
    ws_client_ = std::make_unique<chirp::network::WebSocketClient>(io_);
  } else {
    tcp_client_ = std::make_unique<chirp::network::TcpClient>(io_);
  }
}

SocialModuleImpl::~SocialModuleImpl() {
  Disconnect();
}

bool SocialModuleImpl::Connect() {
  if (connected_) {
    return true;
  }

  if (use_websocket_) {
    if (!ws_client_->Connect(host_, port_)) {
      return false;
    }
    session_ = ws_client_->GetSession();
  } else {
    if (!tcp_client_->Connect(host_, port_)) {
      return false;
    }
    session_ = tcp_client_->GetSession();
  }

  connected_ = true;
  receiving_ = true;

  receive_thread_ = std::thread([this]() { ReceiveLoop(); });

  return true;
}

void SocialModuleImpl::Disconnect() {
  if (!connected_) {
    return;
  }

  connected_ = false;
  receiving_ = false;

  if (session_) {
    session_->Close();
  }

  if (receive_thread_.joinable()) {
    receive_thread_.join();
  }
}

bool SocialModuleImpl::Login(const std::string& user_id, const std::string& token) {
  chirp::auth::LoginRequest req;
  req.set_token(token.empty() ? user_id : token);
  req.set_device_id("sdk_social_client");
  req.set_platform("pc");

  chirp::gateway::Packet pkt;
  pkt.set_msg_id(chirp::gateway::LOGIN_REQ);
  pkt.set_sequence(++sequence_);
  pkt.set_body(req.SerializeAsString());

  SendPacket(pkt);

  // Wait for response (simplified)
  std::this_thread::sleep_for(std::chrono::milliseconds(100));

  user_id_ = user_id;
  return true;
}

void SocialModuleImpl::AddFriend(const std::string& user_id,
                                const std::string& message,
                                AddFriendCallback callback) {
  chirp::social::AddFriendRequest req;
  req.set_user_id(user_id_);
  req.set_target_user_id(user_id);
  req.set_message(message);

  chirp::gateway::Packet pkt;
  pkt.set_msg_id(chirp::gateway::ADD_FRIEND_REQ);
  pkt.set_sequence(++sequence_);
  pkt.set_body(req.SerializeAsString());

  SendPacket(pkt);

  // For now, call callback immediately (in production, would wait for response)
  callback(true, "pending_req_" + std::to_string(sequence_));
}

void SocialModuleImpl::AcceptFriendRequest(const std::string& request_id,
                                          SimpleCallback callback) {
  chirp::social::FriendRequestAction req;
  req.set_user_id(user_id_);
  req.set_request_id(request_id);
  req.set_accept(true);

  chirp::gateway::Packet pkt;
  pkt.set_msg_id(chirp::gateway::FRIEND_REQUEST_ACTION_REQ);
  pkt.set_sequence(++sequence_);
  pkt.set_body(req.SerializeAsString());

  SendPacket(pkt);
  callback(true);
}

void SocialModuleImpl::DeclineFriendRequest(const std::string& request_id,
                                            SimpleCallback callback) {
  chirp::social::FriendRequestAction req;
  req.set_user_id(user_id_);
  req.set_request_id(request_id);
  req.set_accept(false);

  chirp::gateway::Packet pkt;
  pkt.set_msg_id(chirp::gateway::FRIEND_REQUEST_ACTION_REQ);
  pkt.set_sequence(++sequence_);
  pkt.set_body(req.SerializeAsString());

  SendPacket(pkt);
  callback(true);
}

void SocialModuleImpl::RemoveFriend(const std::string& user_id,
                                   SimpleCallback callback) {
  chirp::social::RemoveFriendRequest req;
  req.set_user_id(user_id_);
  req.set_friend_user_id(user_id);

  chirp::gateway::Packet pkt;
  pkt.set_msg_id(chirp::gateway::REMOVE_FRIEND_REQ);
  pkt.set_sequence(++sequence_);
  pkt.set_body(req.SerializeAsString());

  SendPacket(pkt);
  callback(true);
}

void SocialModuleImpl::GetFriendList(int32_t limit,
                                     int32_t offset,
                                     FriendListCallback callback) {
  chirp::social::GetFriendListRequest req;
  req.set_user_id(user_id_);
  req.set_limit(limit);
  req.set_offset(offset);

  chirp::gateway::Packet pkt;
  pkt.set_msg_id(chirp::gateway::GET_FRIEND_LIST_REQ);
  pkt.set_sequence(++sequence_);
  pkt.set_body(req.SerializeAsString());

  SendPacket(pkt);

  // Return empty list for now (in production, would wait for response)
  std::vector<Friend> friends;
  callback(friends);
}

void SocialModuleImpl::GetPendingRequests(std::function<void(const std::vector<FriendRequest>&)> callback) {
  chirp::social::GetPendingRequestsRequest req;
  req.set_user_id(user_id_);

  chirp::gateway::Packet pkt;
  pkt.set_msg_id(chirp::gateway::GET_PENDING_REQUESTS_REQ);
  pkt.set_sequence(++sequence_);
  pkt.set_body(req.SerializeAsString());

  SendPacket(pkt);

  // Return empty list for now
  std::vector<FriendRequest> requests;
  callback(requests);
}

void SocialModuleImpl::BlockUser(const std::string& user_id,
                                SimpleCallback callback) {
  chirp::social::BlockUserRequest req;
  req.set_user_id(user_id_);
  req.set_target_user_id(user_id);

  chirp::gateway::Packet pkt;
  pkt.set_msg_id(chirp::gateway::BLOCK_USER_REQ);
  pkt.set_sequence(++sequence_);
  pkt.set_body(req.SerializeAsString());

  SendPacket(pkt);
  callback(true);
}

void SocialModuleImpl::UnblockUser(const std::string& user_id,
                                  SimpleCallback callback) {
  chirp::social::UnblockUserRequest req;
  req.set_user_id(user_id_);
  req.set_target_user_id(user_id);

  chirp::gateway::Packet pkt;
  pkt.set_msg_id(chirp::gateway::UNBLOCK_USER_REQ);
  pkt.set_sequence(++sequence_);
  pkt.set_body(req.SerializeAsString());

  SendPacket(pkt);
  callback(true);
}

void SocialModuleImpl::GetBlockedList(std::function<void(const std::vector<std::string>&)> callback) {
  chirp::social::GetBlockedListRequest req;
  req.set_user_id(user_id_);

  chirp::gateway::Packet pkt;
  pkt.set_msg_id(chirp::gateway::GET_BLOCKED_LIST_REQ);
  pkt.set_sequence(++sequence_);
  pkt.set_body(req.SerializeAsString());

  SendPacket(pkt);

  // Return empty list for now
  std::vector<std::string> blocked;
  callback(blocked);
}

void SocialModuleImpl::SetPresence(PresenceStatus status,
                                  const std::string& status_message,
                                  const std::string& game_name) {
  chirp::social::SetPresenceRequest req;
  req.set_user_id(user_id_);
  req.set_status(ConvertPresenceStatus(status));
  req.set_status_message(status_message);
  if (!game_name.empty()) {
    (*req.mutable_metadata())["game"] = game_name;
  }

  chirp::gateway::Packet pkt;
  pkt.set_msg_id(chirp::gateway::SET_PRESENCE_REQ);
  pkt.set_sequence(++sequence_);
  pkt.set_body(req.SerializeAsString());

  SendPacket(pkt);
}

void SocialModuleImpl::GetPresence(const std::vector<std::string>& user_ids,
                                  PresenceCallback callback) {
  chirp::social::GetPresenceRequest req;
  for (const auto& user_id : user_ids) {
    req.add_user_ids(user_id);
  }

  chirp::gateway::Packet pkt;
  pkt.set_msg_id(chirp::gateway::GET_PRESENCE_REQ);
  pkt.set_sequence(++sequence_);
  pkt.set_body(req.SerializeAsString());

  SendPacket(pkt);

  // Return empty list for now
  std::vector<Presence> presences;
  callback(presences);
}

void SocialModuleImpl::SubscribePresence(const std::vector<std::string>& user_ids) {
  // In production, would subscribe to presence updates for these users
  // For now, this is a no-op as presence updates are broadcast to friends
}

void SocialModuleImpl::UnsubscribePresence(const std::vector<std::string>& user_ids) {
  // Unsubscribe from presence updates
}

void SocialModuleImpl::SendPacket(const chirp::gateway::Packet& pkt) {
  if (!session_ || session_->IsClosed()) {
    return;
  }

  auto framed = chirp::network::ProtobufFraming::Encode(pkt);
  std::string data(reinterpret_cast<const char*>(framed.data()), framed.size());
  session_->Send(data);
}

void SocialModuleImpl::ReceiveLoop() {
  while (receiving_ && session_ && !session_->IsClosed()) {
    auto data = session_->Receive();
    if (data.empty()) {
      break;
    }

    chirp::gateway::Packet pkt;
    if (!pkt.ParseFromArray(data.data(), static_cast<int>(data.size()))) {
      continue;
    }

    switch (pkt.msg_id()) {
    case chirp::gateway::PRESENCE_NOTIFY:
      if (presence_callback_) {
        chirp::social::PresenceNotify notify;
        if (notify.ParseFromArray(pkt.body().data(), static_cast<int>(pkt.body().size()))) {
          presence_callback_(notify.user_id(),
                            ConvertPresenceStatus(notify.status()),
                            notify.status_message());
        }
      }
      break;
    case chirp::gateway::FRIEND_REQUEST_NOTIFY:
      if (friend_request_callback_) {
        chirp::social::FriendRequestNotify notify;
        if (notify.ParseFromArray(pkt.body().data(), static_cast<int>(pkt.body().size()))) {
          friend_request_callback_(ConvertFriendRequest(notify));
        }
      }
      break;
    case chirp::gateway::FRIEND_ACCEPTED_NOTIFY:
      if (friend_accepted_callback_) {
        chirp::social::FriendAcceptedNotify notify;
        if (notify.ParseFromArray(pkt.body().data(), static_cast<int>(pkt.body().size()))) {
          friend_accepted_callback_(notify.user_id(), notify.username());
        }
      }
      break;
    case chirp::gateway::FRIEND_REMOVED_NOTIFY:
      if (friend_removed_callback_) {
        chirp::social::FriendRemovedNotify notify;
        if (notify.ParseFromArray(pkt.body().data(), static_cast<int>(pkt.body().size()))) {
          friend_removed_callback_(notify.user_id());
        }
      }
      break;
    case chirp::gateway::LOGIN_RESP: {
      chirp::auth::LoginResponse resp;
      if (resp.ParseFromArray(pkt.body().data(), static_cast<int>(pkt.body().size()))) {
        if (resp.code() == chirp::common::OK) {
          user_id_ = resp.user_id();
          session_id_ = resp.session_id();
        }
      }
      break;
    }
    default:
      break;
    }
  }
}

chirp::social::PresenceStatus SocialModuleImpl::ConvertPresenceStatus(PresenceStatus status) {
  switch (status) {
  case PresenceStatus::OFFLINE: return chirp::social::OFFLINE;
  case PresenceStatus::ONLINE: return chirp::social::ONLINE;
  case PresenceStatus::AWAY: return chirp::social::AWAY;
  case PresenceStatus::DND: return chirp::social::DND;
  case PresenceStatus::IN_GAME: return chirp::social::IN_GAME;
  case PresenceStatus::IN_BATTLE: return chirp::social::IN_BATTLE;
  default: return chirp::social::ONLINE;
  }
}

PresenceStatus SocialModuleImpl::ConvertPresenceStatus(chirp::social::PresenceStatus status) {
  switch (status) {
  case chirp::social::OFFLINE: return PresenceStatus::OFFLINE;
  case chirp::social::ONLINE: return PresenceStatus::ONLINE;
  case chirp::social::AWAY: return PresenceStatus::AWAY;
  case chirp::social::DND: return PresenceStatus::DND;
  case chirp::social::IN_GAME: return PresenceStatus::IN_GAME;
  case chirp::social::IN_BATTLE: return PresenceStatus::IN_BATTLE;
  default: return PresenceStatus::ONLINE;
  }
}

Friend SocialModuleImpl::ConvertFriend(const chirp::social::FriendInfo& info) {
  Friend f;
  f.user_id = info.user_id();
  f.username = info.username();
  f.avatar_url = info.avatar_url();
  switch (info.status()) {
  case chirp::social::ACCEPTED: f.status = FriendStatus::ACCEPTED; break;
  case chirp::social::PENDING: f.status = FriendStatus::PENDING; break;
  case chirp::social::BLOCKED: f.status = FriendStatus::BLOCKED; break;
  default: f.status = FriendStatus::NONE;
  }
  f.added_at = info.added_at();
  return f;
}

FriendRequest SocialModuleImpl::ConvertFriendRequest(const chirp::social::FriendRequest& req) {
  FriendRequest r;
  r.request_id = "req_" + std::to_string(NowMs());
  r.from_user_id = req.from_user_id();
  r.from_username = "";  // Would come from user service
  r.message = req.message();
  r.timestamp = req.timestamp();
  return r;
}

Presence SocialModuleImpl::ConvertPresence(const chirp::social::PresenceInfo& info) {
  Presence p;
  p.user_id = info.user_id();
  p.status = ConvertPresenceStatus(info.status());
  p.status_message = info.status_message();
  p.last_seen = info.last_seen();
  auto it = info.metadata().find("game");
  if (it != info.metadata().end()) {
    p.game_name = it->second;
  }
  return p;
}

} // namespace social
} // namespace modules
} // namespace core
} // namespace chirp
