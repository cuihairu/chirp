#include "chat_module_impl.h"

#include <chrono>

namespace chirp {
namespace core {
namespace modules {
namespace chat {

int64_t NowMs() {
  return std::chrono::duration_cast<std::chrono::milliseconds>(
      std::chrono::system_clock::now().time_since_epoch()).count();
}

ChatModuleImpl::ChatModuleImpl(asio::io_context& io,
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

ChatModuleImpl::~ChatModuleImpl() {
  Disconnect();
}

bool ChatModuleImpl::Connect() {
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
  request_thread_ = std::thread([this]() { ProcessPendingRequests(); });

  return true;
}

void ChatModuleImpl::Disconnect() {
  if (!connected_) {
    return;
  }

  connected_ = false;
  receiving_ = false;

  if (session_) {
    session_->Close();
  }

  request_cv_.notify_all();

  if (receive_thread_.joinable()) {
    receive_thread_.join();
  }
  if (request_thread_.joinable()) {
    request_thread_.join();
  }

  {
    std::lock_guard<std::mutex> lock(request_mu_);
    pending_requests_.clear();
  }
}

bool ChatModuleImpl::Login(const std::string& user_id, const std::string& token) {
  chirp::auth::LoginRequest req;
  req.set_token(token.empty() ? user_id : token);
  req.set_device_id("sdk_client");
  req.set_platform("pc");

  chirp::gateway::Packet pkt;
  pkt.set_msg_id(chirp::gateway::LOGIN_REQ);
  pkt.set_sequence(++sequence_);
  pkt.set_body(req.SerializeAsString());

  SendPacket(pkt);

  // Wait for response (simplified - should use callback)
  std::this_thread::sleep_for(std::chrono::milliseconds(100));

  user_id_ = user_id;
  return true;
}

void ChatModuleImpl::Logout() {
  chirp::auth::LogoutRequest req;
  req.set_user_id(user_id_);
  req.set_session_id(session_id_);

  chirp::gateway::Packet pkt;
  pkt.set_msg_id(chirp::gateway::LOGOUT_REQ);
  pkt.set_sequence(++sequence_);
  pkt.set_body(req.SerializeAsString());

  SendPacket(pkt);
  user_id_.clear();
  session_id_.clear();
}

void ChatModuleImpl::SendMessage(const std::string& to_user,
                                MessageType type,
                                const std::string& content,
                                SendMessageCallback callback) {
  chirp::chat::SendMessageRequest req;
  req.set_sender_id(user_id_);
  req.set_receiver_id(to_user);
  req.set_channel_type(chirp::chat::PRIVATE);
  req.set_msg_type(ConvertMessageType(type));
  req.set_content(content);
  req.set_client_timestamp(NowMs());

  chirp::gateway::Packet pkt;
  pkt.set_msg_id(chirp::gateway::SEND_MESSAGE_REQ);
  pkt.set_sequence(++sequence_);
  pkt.set_body(req.SerializeAsString());

  int64_t seq = sequence_;
  {
    std::lock_guard<std::mutex> lock(request_mu_);
    PendingRequest pending;
    pending.sequence = seq;
    pending.timeout_ms = 10000;
    pending.created_at = NowMs();
    pending.callback = [this, callback](const std::string& body) {
      chirp::chat::SendMessageResponse resp;
      if (resp.ParseFromArray(body.data(), body.size())) {
        if (resp.code() == chirp::common::OK) {
          callback(SendResult::SUCCESS, resp.message_id());
        } else {
          callback(SendResult::FAILED, "");
        }
      } else {
        callback(SendResult::FAILED, "");
      }
    };
    pending_requests_[seq] = std::move(pending);
  }

  SendPacket(pkt);
}

void ChatModuleImpl::SendChannelMessage(const std::string& channel_id,
                                       ChannelType channel_type,
                                       MessageType type,
                                       const std::string& content,
                                       SendMessageCallback callback) {
  chirp::chat::SendMessageRequest req;
  req.set_sender_id(user_id_);
  req.set_channel_id(channel_id);
  req.set_channel_type(ConvertChannelType(channel_type));
  req.set_msg_type(ConvertMessageType(type));
  req.set_content(content);
  req.set_client_timestamp(NowMs());

  chirp::gateway::Packet pkt;
  pkt.set_msg_id(chirp::gateway::SEND_MESSAGE_REQ);
  pkt.set_sequence(++sequence_);
  pkt.set_body(req.SerializeAsString());

  int64_t seq = sequence_;
  {
    std::lock_guard<std::mutex> lock(request_mu_);
    PendingRequest pending;
    pending.sequence = seq;
    pending.timeout_ms = 10000;
    pending.created_at = NowMs();
    pending.callback = [this, callback](const std::string& body) {
      chirp::chat::SendMessageResponse resp;
      if (resp.ParseFromArray(body.data(), body.size())) {
        if (resp.code() == chirp::common::OK) {
          callback(SendResult::SUCCESS, resp.message_id());
        } else {
          callback(SendResult::FAILED, "");
        }
      }
    };
    pending_requests_[seq] = std::move(pending);
  }

  SendPacket(pkt);
}

void ChatModuleImpl::GetHistory(const std::string& channel_id,
                               ChannelType channel_type,
                               int64_t before_timestamp,
                               int32_t limit,
                               GetHistoryCallback callback) {
  chirp::chat::GetHistoryRequest req;
  req.set_user_id(user_id_);
  req.set_channel_id(channel_id);
  req.set_channel_type(ConvertChannelType(channel_type));
  req.set_before_timestamp(before_timestamp);
  req.set_limit(limit);

  chirp::gateway::Packet pkt;
  pkt.set_msg_id(chirp::gateway::GET_HISTORY_REQ);
  pkt.set_sequence(++sequence_);
  pkt.set_body(req.SerializeAsString());

  int64_t seq = sequence_;
  {
    std::lock_guard<std::mutex> lock(request_mu_);
    PendingRequest pending;
    pending.sequence = seq;
    pending.timeout_ms = 10000;
    pending.created_at = NowMs();
    pending.callback = [this, callback](const std::string& body) {
      chirp::chat::GetHistoryResponse resp;
      if (resp.ParseFromArray(body.data(), body.size())) {
        std::vector<Message> messages;
        for (const auto& msg : resp.messages()) {
          messages.push_back(ConvertMessage(msg));
        }
        callback(resp.code() == chirp::common::OK, messages, resp.has_more());
      } else {
        callback(false, {}, false);
      }
    };
    pending_requests_[seq] = std::move(pending);
  }

  SendPacket(pkt);
}

void ChatModuleImpl::MarkRead(const std::string& channel_id,
                             ChannelType channel_type,
                             const std::string& message_id) {
  chirp::chat::MarkReadRequest req;
  req.set_user_id(user_id_);
  req.set_channel_id(channel_id);
  req.set_channel_type(ConvertChannelType(channel_type));
  req.set_message_id(message_id);
  req.set_read_timestamp(NowMs());

  chirp::gateway::Packet pkt;
  pkt.set_msg_id(chirp::gateway::MARK_READ_REQ);
  pkt.set_sequence(++sequence_);
  pkt.set_body(req.SerializeAsString());

  SendPacket(pkt);
}

void ChatModuleImpl::GetUnreadCount(std::function<void(int32_t count)> callback) {
  chirp::chat::GetUnreadCountRequest req;
  req.set_user_id(user_id_);

  chirp::gateway::Packet pkt;
  pkt.set_msg_id(chirp::gateway::GET_UNREAD_COUNT_REQ);
  pkt.set_sequence(++sequence_);
  pkt.set_body(req.SerializeAsString());

  int64_t seq = sequence_;
  {
    std::lock_guard<std::mutex> lock(request_mu_);
    PendingRequest pending;
    pending.sequence = seq;
    pending.timeout_ms = 10000;
    pending.created_at = NowMs();
    pending.callback = [callback](const std::string& body) {
      chirp::chat::GetUnreadCountResponse resp;
      if (resp.ParseFromArray(body.data(), body.size())) {
        callback(resp.total_unread());
      }
    };
    pending_requests_[seq] = std::move(pending);
  }

  SendPacket(pkt);
}

void ChatModuleImpl::CreateGroup(const std::string& name,
                                const std::vector<std::string>& members,
                                std::function<void(const std::string& group_id)> callback) {
  chirp::chat::CreateGroupRequest req;
  req.set_creator_id(user_id_);
  req.set_group_name(name);
  req.set_max_members(0);  // Unlimited
  for (const auto& member : members) {
    req.add_initial_members(member);
  }

  chirp::gateway::Packet pkt;
  pkt.set_msg_id(chirp::gateway::CREATE_GROUP_REQ);
  pkt.set_sequence(++sequence_);
  pkt.set_body(req.SerializeAsString());

  int64_t seq = sequence_;
  {
    std::lock_guard<std::mutex> lock(request_mu_);
    PendingRequest pending;
    pending.sequence = seq;
    pending.timeout_ms = 10000;
    pending.created_at = NowMs();
    pending.callback = [callback](const std::string& body) {
      chirp::chat::CreateGroupResponse resp;
      if (resp.ParseFromArray(body.data(), body.size())) {
        callback(resp.group_id());
      }
    };
    pending_requests_[seq] = std::move(pending);
  }

  SendPacket(pkt);
}

void ChatModuleImpl::JoinGroup(const std::string& group_id,
                              std::function<void(bool success)> callback) {
  chirp::chat::JoinGroupRequest req;
  req.set_user_id(user_id_);
  req.set_group_id(group_id);

  chirp::gateway::Packet pkt;
  pkt.set_msg_id(chirp::gateway::JOIN_GROUP_REQ);
  pkt.set_sequence(++sequence_);
  pkt.set_body(req.SerializeAsString());

  int64_t seq = sequence_;
  {
    std::lock_guard<std::mutex> lock(request_mu_);
    PendingRequest pending;
    pending.sequence = seq;
    pending.timeout_ms = 10000;
    pending.created_at = NowMs();
    pending.callback = [callback](const std::string& body) {
      chirp::chat::JoinGroupResponse resp;
      if (resp.ParseFromArray(body.data(), body.size())) {
        callback(resp.code() == chirp::common::OK);
      }
    };
    pending_requests_[seq] = std::move(pending);
  }

  SendPacket(pkt);
}

void ChatModuleImpl::LeaveGroup(const std::string& group_id,
                               std::function<void(bool success)> callback) {
  chirp::chat::LeaveGroupRequest req;
  req.set_user_id(user_id_);
  req.set_group_id(group_id);

  chirp::gateway::Packet pkt;
  pkt.set_msg_id(chirp::gateway::LEAVE_GROUP_REQ);
  pkt.set_sequence(++sequence_);
  pkt.set_body(req.SerializeAsString());

  int64_t seq = sequence_;
  {
    std::lock_guard<std::mutex> lock(request_mu_);
    PendingRequest pending;
    pending.sequence = seq;
    pending.timeout_ms = 10000;
    pending.created_at = NowMs();
    pending.callback = [callback](const std::string& body) {
      chirp::chat::LeaveGroupResponse resp;
      if (resp.ParseFromArray(body.data(), body.size())) {
        callback(resp.code() == chirp::common::OK);
      }
    };
    pending_requests_[seq] = std::move(pending);
  }

  SendPacket(pkt);
}

void ChatModuleImpl::GetGroupMembers(const std::string& group_id,
                                    std::function<void(const std::vector<GroupMember>&)> callback) {
  chirp::chat::GetGroupMembersRequest req;
  req.set_group_id(group_id);

  chirp::gateway::Packet pkt;
  pkt.set_msg_id(chirp::gateway::GET_GROUP_MEMBERS_REQ);
  pkt.set_sequence(++sequence_);
  pkt.set_body(req.SerializeAsString());

  int64_t seq = sequence_;
  {
    std::lock_guard<std::mutex> lock(request_mu_);
    PendingRequest pending;
    pending.sequence = seq;
    pending.timeout_ms = 10000;
    pending.created_at = NowMs();
    pending.callback = [this, callback](const std::string& body) {
      chirp::chat::GetGroupMembersResponse resp;
      if (resp.ParseFromArray(body.data(), body.size())) {
        std::vector<GroupMember> members;
        for (const auto& m : resp.members()) {
          members.push_back(ConvertGroupMember(m));
        }
        callback(members);
      }
    };
    pending_requests_[seq] = std::move(pending);
  }

  SendPacket(pkt);
}

void ChatModuleImpl::SendTypingIndicator(const std::string& channel_id,
                                        ChannelType channel_type,
                                        bool is_typing) {
  chirp::chat::TypingIndicator indicator;
  indicator.set_channel_id(channel_id);
  indicator.set_channel_type(ConvertChannelType(channel_type));
  indicator.set_user_id(user_id_);
  indicator.set_is_typing(is_typing);
  indicator.set_timestamp(NowMs());

  chirp::gateway::Packet pkt;
  pkt.set_msg_id(chirp::gateway::TYPING_INDICATOR_NOTIFY);
  pkt.set_sequence(0);
  pkt.set_body(indicator.SerializeAsString());

  SendPacket(pkt);
}

void ChatModuleImpl::SendPacket(const chirp::gateway::Packet& pkt) {
  if (!session_ || session_->IsClosed()) {
    return;
  }

  auto framed = chirp::network::ProtobufFraming::Encode(pkt);
  std::string data(reinterpret_cast<const char*>(framed.data()), framed.size());
  session_->Send(data);
}

void ChatModuleImpl::ReceiveLoop() {
  while (receiving_ && session_ && !session_->IsClosed()) {
    auto data = session_->Receive();
    if (data.empty()) {
      break;
    }

    chirp::gateway::Packet pkt;
    if (!pkt.ParseFromArray(data.data(), static_cast<int>(data.size()))) {
      continue;
    }

    // Handle response
    if (pkt.sequence() > 0) {
      std::lock_guard<std::mutex> lock(request_mu_);
      auto it = pending_requests_.find(pkt.sequence());
      if (it != pending_requests_.end()) {
        if (it->second.callback) {
          it->second.callback(pkt.body());
        }
        pending_requests_.erase(it);
        request_cv_.notify_all();
      }
    }

    // Handle notifications
    switch (pkt.msg_id()) {
    case chirp::gateway::CHAT_MESSAGE_NOTIFY:
      if (message_callback_) {
        chirp::chat::ChatMessage msg;
        if (msg.ParseFromArray(pkt.body().data(), static_cast<int>(pkt.body().size()))) {
          message_callback_(ConvertMessage(msg));
        }
      }
      break;
    case chirp::gateway::MESSAGE_READ_NOTIFY:
      if (read_callback_) {
        // Parse and invoke callback
      }
      break;
    case chirp::gateway::TYPING_INDICATOR_NOTIFY:
      if (typing_callback_) {
        chirp::chat::TypingIndicator indicator;
        if (indicator.ParseFromArray(pkt.body().data(), static_cast<int>(pkt.body().size()))) {
          typing_callback_(indicator.channel_id(),
                          ConvertChannelType(indicator.channel_type()),
                          indicator.user_id(),
                          indicator.is_typing());
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

void ChatModuleImpl::ProcessPendingRequests() {
  while (connected_) {
    std::unique_lock<std::mutex> lock(request_mu_);
    request_cv_.wait_for(lock, std::chrono::seconds(1));

    int64_t now = NowMs();
    auto it = pending_requests_.begin();
    while (it != pending_requests_.end()) {
      if (now - it->second.created_at > it->second.timeout_ms) {
        // Timeout
        if (it->second.callback) {
          it->second.callback("");  // Empty response for timeout
        }
        it = pending_requests_.erase(it);
      } else {
        ++it;
      }
    }
  }
}

chirp::chat::MessageType ChatModuleImpl::ConvertMessageType(MessageType type) {
  switch (type) {
  case MessageType::TEXT: return chirp::chat::TEXT;
  case MessageType::EMOJI: return chirp::chat::EMOJI;
  case MessageType::VOICE: return chirp::chat::VOICE;
  case MessageType::IMAGE: return chirp::chat::IMAGE;
  case MessageType::SYSTEM: return chirp::chat::SYSTEM;
  default: return chirp::chat::TEXT;
  }
}

MessageType ChatModuleImpl::ConvertMessageType(chirp::chat::MessageType type) {
  switch (type) {
  case chirp::chat::TEXT: return MessageType::TEXT;
  case chirp::chat::EMOJI: return MessageType::EMOJI;
  case chirp::chat::VOICE: return MessageType::VOICE;
  case chirp::chat::IMAGE: return MessageType::IMAGE;
  case chirp::chat::SYSTEM: return MessageType::SYSTEM;
  default: return MessageType::TEXT;
  }
}

chirp::chat::ChannelType ChatModuleImpl::ConvertChannelType(ChannelType type) {
  switch (type) {
  case ChannelType::PRIVATE: return chirp::chat::PRIVATE;
  case ChannelType::TEAM: return chirp::chat::TEAM;
  case ChannelType::GUILD: return chirp::chat::GUILD;
  case ChannelType::WORLD: return chirp::chat::WORLD;
  case ChannelType::GROUP: return chirp::chat::PRIVATE;  // Use PRIVATE for GROUP
  default: return chirp::chat::PRIVATE;
  }
}

ChannelType ChatModuleImpl::ConvertChannelType(chirp::chat::ChannelType type) {
  switch (type) {
  case chirp::chat::PRIVATE: return ChannelType::PRIVATE;
  case chirp::chat::TEAM: return ChannelType::TEAM;
  case chirp::chat::GUILD: return ChannelType::GUILD;
  case chirp::chat::WORLD: return ChannelType::WORLD;
  default: return ChannelType::PRIVATE;
  }
}

Message ChatModuleImpl::ConvertMessage(const chirp::chat::ChatMessage& msg) {
  Message m;
  m.message_id = msg.message_id();
  m.sender_id = msg.sender_id();
  m.receiver_id = msg.receiver_id();
  m.channel_id = msg.channel_id();
  m.channel_type = ConvertChannelType(msg.channel_type());
  m.msg_type = ConvertMessageType(msg.msg_type());
  m.content = std::string(msg.content());
  m.timestamp = msg.timestamp();
  return m;
}

GroupMember ChatModuleImpl::ConvertGroupMember(const chirp::chat::GroupMember& member) {
  GroupMember m;
  m.user_id = member.user_id();
  m.username = member.username();
  m.avatar_url = member.avatar_url();
  m.role = member.role();
  m.joined_at = member.joined_at();
  m.last_read_at = member.last_read_at();
  return m;
}

} // namespace chat
} // namespace modules
} // namespace core
} // namespace chirp
