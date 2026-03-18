#include "voice_module_impl.h"

#include <chrono>

namespace chirp {
namespace core {
namespace modules {
namespace voice {

int64_t NowMs() {
  return std::chrono::duration_cast<std::chrono::milliseconds>(
      std::chrono::system_clock::now().time_since_epoch()).count();
}

VoiceModuleImpl::VoiceModuleImpl(asio::io_context& io,
                                 const std::string& host,
                                 uint16_t port,
                                 bool use_websocket)
    : io_(io),
      host_(host),
      port_(port),
      use_websocket_(use_websocket),
      connected_(false),
      receiving_(false),
      sequence_(0),
      is_muted_(false),
      is_deafened_(false),
      audio_input_enabled_(true),
      audio_output_enabled_(true) {
  if (use_websocket) {
    ws_client_ = std::make_unique<chirp::network::WebSocketClient>(io_);
  } else {
    tcp_client_ = std::make_unique<chirp::network::TcpClient>(io_);
  }
}

VoiceModuleImpl::~VoiceModuleImpl() {
  Disconnect();
  webrtc_client_.reset();
}

bool VoiceModuleImpl::InitializeWebRTC(const WebRTCConfig& config) {
  if (webrtc_initialized_) {
    return true;
  }

  webrtc_client_ = WebRTCClientFactory::Create();
  if (!webrtc_client_) {
    return false;
  }

  if (!webrtc_client_->Initialize(config)) {
    webrtc_client_.reset();
    return false;
  }

  webrtc_initialized_ = true;
  return true;
}

bool VoiceModuleImpl::Connect() {
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

void VoiceModuleImpl::Disconnect() {
  if (!connected_) {
    return;
  }

  // Leave room if in one
  if (!current_room_id_.empty()) {
    LeaveRoom(current_room_id_, [](bool) {});
  }

  connected_ = false;
  receiving_ = false;

  if (session_) {
    session_->Close();
  }

  if (receive_thread_.joinable()) {
    receive_thread_.join();
  }

  current_room_id_.clear();
}

void VoiceModuleImpl::CreateRoom(RoomType type,
                                const std::string& name,
                                int32_t max_participants,
                                CreateRoomCallback callback) {
  chirp::voice::CreateRoomRequest req;
  req.set_user_id(user_id_);
  req.set_room_type(ConvertRoomType(type));
  req.set_room_name(name);
  req.set_max_participants(max_participants);

  chirp::gateway::Packet pkt;
  pkt.set_msg_id(chirp::gateway::CREATE_ROOM_REQ);
  pkt.set_sequence(++sequence_);
  pkt.set_body(req.SerializeAsString());

  SendPacket(pkt);

  // Simplified: call callback with generated ID
  callback(true, "room_" + std::to_string(sequence_));
}

void VoiceModuleImpl::JoinRoom(const std::string& room_id,
                              const std::string& sdp_offer,
                              JoinRoomCallback callback) {
  chirp::voice::JoinRoomRequest req;
  req.set_user_id(user_id_);
  req.set_room_id(room_id);
  req.set_sdp_offer(sdp_offer);

  chirp::gateway::Packet pkt;
  pkt.set_msg_id(chirp::gateway::JOIN_ROOM_REQ);
  pkt.set_sequence(++sequence_);
  pkt.set_body(req.SerializeAsString());

  SendPacket(pkt);

  current_room_id_ = room_id;

  // Simplified: call callback
  callback(true, room_id, {});
}

void VoiceModuleImpl::LeaveRoom(const std::string& room_id,
                               SimpleCallback callback) {
  chirp::voice::LeaveRoomRequest req;
  req.set_user_id(user_id_);
  req.set_room_id(room_id);

  chirp::gateway::Packet pkt;
  pkt.set_msg_id(chirp::gateway::LEAVE_ROOM_REQ);
  pkt.set_sequence(++sequence_);
  pkt.set_body(req.SerializeAsString());

  SendPacket(pkt);

  if (current_room_id_ == room_id) {
    current_room_id_.clear();
  }

  callback(true);
}

void VoiceModuleImpl::GetRoomInfo(const std::string& room_id,
                                 RoomInfoCallback callback) {
  chirp::voice::GetRoomInfoRequest req;
  req.set_room_id(room_id);

  chirp::gateway::Packet pkt;
  pkt.set_msg_id(chirp::gateway::GET_ROOM_INFO_REQ);
  pkt.set_sequence(++sequence_);
  pkt.set_body(req.SerializeAsString());

  SendPacket(pkt);

  // Return dummy info for now
  RoomInfo info;
  info.room_id = room_id;
  info.room_name = "Room";
  info.room_type = RoomType::GROUP;
  info.max_participants = 10;
  info.current_participants = 0;
  callback(info);
}

void VoiceModuleImpl::GetCurrentRoom(RoomInfoCallback callback) {
  if (current_room_id_.empty()) {
    // No current room
    RoomInfo info;
    info.room_id = "";
    callback(info);
    return;
  }

  GetRoomInfo(current_room_id_, callback);
}

void VoiceModuleImpl::GetParticipants(const std::string& room_id,
                                     ParticipantsCallback callback) {
  // Simplified: return empty list
  std::vector<Participant> participants;
  callback(participants);
}

void VoiceModuleImpl::SendIceCandidate(const std::string& room_id,
                                     const std::string& to_user_id,
                                     const std::string& candidate,
                                     const std::string& sdp_mid,
                                     int32_t sdp_mline_index) {
  chirp::voice::IceCandidateMessage msg;
  msg.set_room_id(room_id);
  msg.set_from_user_id(user_id_);
  msg.set_to_user_id(to_user_id);
  msg.mutable_candidate()->set_candidate(candidate);
  msg.mutable_candidate()->set_sdp_mid(sdp_mid);
  msg.mutable_candidate()->set_sdp_mline_index(sdp_mline_index);

  chirp::gateway::Packet pkt;
  pkt.set_msg_id(chirp::gateway::ICE_CANDIDATE_MSG);
  pkt.set_sequence(0);
  pkt.set_body(msg.SerializeAsString());

  SendPacket(pkt);
}

void VoiceModuleImpl::SendPacket(const chirp::gateway::Packet& pkt) {
  if (!session_ || session_->IsClosed()) {
    return;
  }

  auto framed = chirp::network::ProtobufFraming::Encode(pkt);
  std::string data(reinterpret_cast<const char*>(framed.data()), framed.size());
  session_->Send(data);
}

void VoiceModuleImpl::ReceiveLoop() {
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
    case chirp::gateway::PARTICIPANT_JOINED_NOTIFY:
      if (participant_joined_callback_) {
        chirp::voice::ParticipantJoinedNotify notify;
        if (notify.ParseFromArray(pkt.body().data(), static_cast<int>(pkt.body().size()))) {
          Participant p = ConvertParticipant(notify.participant());
          participant_joined_callback_(notify.room_id(), p);
        }
      }
      break;
    case chirp::gateway::PARTICIPANT_LEFT_NOTIFY:
      if (participant_left_callback_) {
        chirp::voice::ParticipantLeftNotify notify;
        if (notify.ParseFromArray(pkt.body().data(), static_cast<int>(pkt.body().size()))) {
          participant_left_callback_(notify.room_id(), notify.user_id());
        }
      }
      break;
    case chirp::gateway::PARTICIPANT_STATE_CHANGED_NOTIFY:
      if (participant_state_changed_callback_) {
        chirp::voice::ParticipantStateChangedNotify notify;
        if (notify.ParseFromArray(pkt.body().data(), static_cast<int>(pkt.body().size()))) {
          participant_state_changed_callback_(notify.room_id(), notify.user_id(),
                                            ConvertParticipantState(notify.state()));
        }
      }
      break;
    case chirp::gateway::SPEAKING_NOTIFY:
      if (speaking_callback_) {
        chirp::voice::SpeakingNotify notify;
        if (notify.ParseFromArray(pkt.body().data(), static_cast<int>(pkt.body().size()))) {
          speaking_callback_(notify.room_id(), notify.user_id(), notify.speaking());
        }
      }
      break;
    case chirp::gateway::ICE_CANDIDATE_MSG:
      // Handle ICE candidate (would be processed by WebRTC layer)
      break;
    case chirp::gateway::SDP_OFFER_MSG:
    case chirp::gateway::SDP_ANSWER_MSG:
      // Handle SDP messages (would be processed by WebRTC layer)
      break;
    default:
      break;
    }
  }
}

chirp::voice::RoomType VoiceModuleImpl::ConvertRoomType(RoomType type) {
  switch (type) {
  case RoomType::PEER_TO_PEER: return chirp::voice::PEER_TO_PEER;
  case RoomType::GROUP: return chirp::voice::GROUP;
  case RoomType::CHANNEL: return chirp::voice::CHANNEL;
  default: return chirp::voice::GROUP;
  }
}

RoomType VoiceModuleImpl::ConvertRoomType(chirp::voice::RoomType type) {
  switch (type) {
  case chirp::voice::PEER_TO_PEER: return RoomType::PEER_TO_PEER;
  case chirp::voice::GROUP: return RoomType::GROUP;
  case chirp::voice::CHANNEL: return RoomType::CHANNEL;
  default: return RoomType::GROUP;
  }
}

chirp::voice::ParticipantState VoiceModuleImpl::ConvertParticipantState(ParticipantState state) {
  switch (state) {
  case ParticipantState::JOINING: return chirp::voice::JOINING;
  case ParticipantState::CONNECTED: return chirp::voice::CONNECTED;
  case ParticipantState::MUTED: return chirp::voice::MUTED;
  case ParticipantState::DEAFENED: return chirp::voice::DEAFENED;
  case ParticipantState::DISCONNECTED: return chirp::voice::DISCONNECTED;
  default: return chirp::voice::CONNECTED;
  }
}

ParticipantState VoiceModuleImpl::ConvertParticipantState(chirp::voice::ParticipantState state) {
  switch (state) {
  case chirp::voice::JOINING: return ParticipantState::JOINING;
  case chirp::voice::CONNECTED: return ParticipantState::CONNECTED;
  case chirp::voice::MUTED: return ParticipantState::MUTED;
  case chirp::voice::DEAFENED: return ParticipantState::DEAFENED;
  case chirp::voice::DISCONNECTED: return ParticipantState::DISCONNECTED;
  default: return ParticipantState::CONNECTED;
  }
}

RoomInfo VoiceModuleImpl::ConvertRoomInfo(const chirp::voice::ParticipantInfo& info) {
  RoomInfo room;
  room.room_id = "";
  room.room_name = "";
  room.room_type = RoomType::GROUP;
  room.max_participants = 0;
  room.current_participants = 0;
  return room;
}

Participant VoiceModuleImpl::ConvertParticipant(const chirp::voice::ParticipantInfo& info) {
  Participant p;
  p.user_id = info.user_id();
  p.username = info.username();
  p.state = ConvertParticipantState(info.state());
  p.joined_at = info.joined_at();
  p.is_speaking = info.is_speaking();
  return p;
}

} // namespace voice
} // namespace modules
} // namespace core
} // namespace chirp
