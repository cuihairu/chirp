#include "chirp/voice_client.h"

#include <utility>

#include "chirp/sdk_client.h"
#include "proto/gateway.pb.h"

namespace chirp {
namespace sdk {

namespace {

using chirp::gateway::MsgID;

// 通用请求的解包壳:ec 原样透传;RespT 解析失败按协议异常报 BadResponse。
// 业务结果一律由调用方读 resp.code()(与 ChatClient 便捷 API 同约定)。
// RespT 在参数表里推不出来,调用点必须显式给出。
template <typename RespT, typename RespCb>
void RequestAndParse(ChatClient& client, MsgID req_id, MsgID resp_id,
                     const std::string& body, RespCb cb) {
  client.Request(static_cast<uint32_t>(req_id), static_cast<uint32_t>(resp_id), body,
                 [cb = std::move(cb)](const std::error_code& ec, const std::string& resp_body) {
                   if (ec) {
                     cb(ec, RespT{});
                     return;
                   }
                   RespT resp;
                   if (!resp.ParseFromString(resp_body)) {
                     cb(make_error_code(ChatError::BadResponse), RespT{});
                     return;
                   }
                   cb(std::error_code{}, std::move(resp));
                 });
}

// code-only 响应(SetMuteResponse/MicQueueResponse 等只回 code)适配成
// VoiceOpCallback:业务结果取 resp.code(),传输层错误传 OK 占位。
template <typename RespT>
std::function<void(const std::error_code&, const RespT&)> ToCodeOnly(VoiceOpCallback cb) {
  return [cb = std::move(cb)](const std::error_code& ec, const RespT& resp) {
    cb(ec, ec ? chirp::common::OK : resp.code());
  };
}

} // namespace

VoiceClient::VoiceClient(const ChatConfig& config) : client_(std::make_unique<ChatClient>(config)) {
  using chirp::gateway::PARTICIPANT_JOINED_NOTIFY;
  using chirp::gateway::PARTICIPANT_LEFT_NOTIFY;
  using chirp::gateway::PARTICIPANT_STATE_CHANGED_NOTIFY;
  using chirp::gateway::SPEAKING_NOTIFY;
  using chirp::gateway::MIC_QUEUE_STATE_NOTIFY;

  // 解包 + 派发:notify 语义由服务端保证(sequence==0),这里只按消息类型
  // 拆 body 并交给用户回调(io 线程)。
  speaking_handle_ = client_->OnNotify(static_cast<uint32_t>(SPEAKING_NOTIFY),
                                       [this](const std::string& body) {
                                         if (!on_speaking_) return;
                                         chirp::voice::SpeakingNotify n;
                                         if (n.ParseFromString(body)) on_speaking_(n);
                                       });
  joined_handle_ = client_->OnNotify(
      static_cast<uint32_t>(PARTICIPANT_JOINED_NOTIFY), [this](const std::string& body) {
        if (!on_joined_) return;
        chirp::voice::ParticipantJoinedNotify n;
        if (n.ParseFromString(body)) on_joined_(n);
      });
  left_handle_ = client_->OnNotify(
      static_cast<uint32_t>(PARTICIPANT_LEFT_NOTIFY), [this](const std::string& body) {
        if (!on_left_) return;
        chirp::voice::ParticipantLeftNotify n;
        if (n.ParseFromString(body)) on_left_(n);
      });
  state_handle_ = client_->OnNotify(
      static_cast<uint32_t>(PARTICIPANT_STATE_CHANGED_NOTIFY), [this](const std::string& body) {
        if (!on_state_) return;
        chirp::voice::ParticipantStateChangedNotify n;
        if (n.ParseFromString(body)) on_state_(n);
      });
  mic_queue_handle_ = client_->OnNotify(
      static_cast<uint32_t>(MIC_QUEUE_STATE_NOTIFY), [this](const std::string& body) {
        if (!on_mic_queue_) return;
        chirp::voice::MicQueueStateNotify n;
        if (n.ParseFromString(body)) on_mic_queue_(n);
      });
}

VoiceClient::~VoiceClient() {
  client_->OffNotify(speaking_handle_);
  client_->OffNotify(joined_handle_);
  client_->OffNotify(left_handle_);
  client_->OffNotify(state_handle_);
  client_->OffNotify(mic_queue_handle_);
}

void VoiceClient::Connect() { client_->Connect(); }
void VoiceClient::Disconnect() { client_->Disconnect(); }
ConnectionState VoiceClient::GetState() const { return client_->GetState(); }

std::string VoiceClient::CurrentUserId() const {
  std::lock_guard<std::mutex> lock(user_mu_);
  return user_id_;
}

void VoiceClient::Login(const std::string& token, LoginCallback cb) {
  // 记住服务端确认的身份:voice 的业务请求体要带 user_id,且必须与绑定
  // 身份一致(AuthorizeActor 顶回不一致的 claim)。
  client_->Login(token, [this, cb](const std::error_code& ec, const std::string& user_id) {
    if (!ec) {
      std::lock_guard<std::mutex> lock(user_mu_);
      user_id_ = user_id;
    }
    if (cb) {
      cb(ec, user_id);
    }
  });
}

void VoiceClient::CreateRoom(chirp::voice::RoomType type, const std::string& name,
                             int32_t max_participants, VoiceCreateRoomCallback cb) {
  chirp::voice::CreateRoomRequest req;
  req.set_user_id(CurrentUserId());
  req.set_room_type(type);
  req.set_room_name(name);
  req.set_max_participants(max_participants);
  RequestAndParse<chirp::voice::CreateRoomResponse>(
      *client_, MsgID::CREATE_ROOM_REQ, MsgID::CREATE_ROOM_RESP, req.SerializeAsString(),
      std::move(cb));
}

void VoiceClient::JoinRoom(const std::string& room_id, const std::string& sdp_offer,
                           VoiceJoinCallback cb) {
  chirp::voice::JoinRoomRequest req;
  req.set_user_id(CurrentUserId());
  req.set_room_id(room_id);
  req.set_sdp_offer(sdp_offer);
  RequestAndParse<chirp::voice::JoinRoomResponse>(*client_, MsgID::JOIN_ROOM_REQ,
                                                  MsgID::JOIN_ROOM_RESP,
                                                  req.SerializeAsString(), std::move(cb));
}

void VoiceClient::LeaveRoom(const std::string& room_id, VoiceOpCallback cb) {
  chirp::voice::LeaveRoomRequest req;
  req.set_user_id(CurrentUserId());
  req.set_room_id(room_id);
  RequestAndParse<chirp::voice::LeaveRoomResponse>(
      *client_, MsgID::LEAVE_ROOM_REQ, MsgID::LEAVE_ROOM_RESP, req.SerializeAsString(),
      ToCodeOnly<chirp::voice::LeaveRoomResponse>(std::move(cb)));
}

void VoiceClient::FetchRoomInfo(const std::string& room_id, VoiceRoomInfoCallback cb) {
  chirp::voice::GetRoomInfoRequest req;
  req.set_room_id(room_id);
  RequestAndParse<chirp::voice::GetRoomInfoResponse>(
      *client_, MsgID::GET_ROOM_INFO_REQ, MsgID::GET_ROOM_INFO_RESP, req.SerializeAsString(),
      std::move(cb));
}

void VoiceClient::SetMute(const std::string& room_id, bool muted, VoiceOpCallback cb) {
  chirp::voice::SetMuteRequest req;
  req.set_user_id(CurrentUserId());
  req.set_room_id(room_id);
  req.set_muted(muted);
  RequestAndParse<chirp::voice::SetMuteResponse>(
      *client_, MsgID::SET_MUTE_REQ, MsgID::SET_MUTE_RESP, req.SerializeAsString(),
      ToCodeOnly<chirp::voice::SetMuteResponse>(std::move(cb)));
}

void VoiceClient::SetDeafen(const std::string& room_id, bool deafened, VoiceOpCallback cb) {
  chirp::voice::SetDeafenRequest req;
  req.set_user_id(CurrentUserId());
  req.set_room_id(room_id);
  req.set_deafened(deafened);
  RequestAndParse<chirp::voice::SetDeafenResponse>(
      *client_, MsgID::SET_DEAFEN_REQ, MsgID::SET_DEAFEN_RESP, req.SerializeAsString(),
      ToCodeOnly<chirp::voice::SetDeafenResponse>(std::move(cb)));
}

void VoiceClient::SetMicMode(const std::string& room_id, chirp::voice::MicMode mode,
                             VoiceOpCallback cb) {
  chirp::voice::SetMicModeRequest req;
  req.set_user_id(CurrentUserId());
  req.set_room_id(room_id);
  req.set_mode(mode);
  RequestAndParse<chirp::voice::MicQueueResponse>(
      *client_, MsgID::SET_MIC_MODE_REQ, MsgID::SET_MIC_MODE_RESP, req.SerializeAsString(),
      ToCodeOnly<chirp::voice::MicQueueResponse>(std::move(cb)));
}

void VoiceClient::ApplyForMic(const std::string& room_id, VoiceOpCallback cb) {
  chirp::voice::MicQueueApplyRequest req;
  req.set_user_id(CurrentUserId());
  req.set_room_id(room_id);
  RequestAndParse<chirp::voice::MicQueueResponse>(
      *client_, MsgID::MIC_QUEUE_APPLY_REQ, MsgID::MIC_QUEUE_APPLY_RESP,
      req.SerializeAsString(), ToCodeOnly<chirp::voice::MicQueueResponse>(std::move(cb)));
}

void VoiceClient::ApproveMic(const std::string& room_id, const std::string& target_user_id,
                             VoiceOpCallback cb) {
  chirp::voice::MicQueueApproveRequest req;
  req.set_user_id(CurrentUserId());
  req.set_room_id(room_id);
  req.set_target_user_id(target_user_id);
  RequestAndParse<chirp::voice::MicQueueResponse>(
      *client_, MsgID::MIC_QUEUE_APPROVE_REQ, MsgID::MIC_QUEUE_APPROVE_RESP,
      req.SerializeAsString(), ToCodeOnly<chirp::voice::MicQueueResponse>(std::move(cb)));
}

void VoiceClient::ReleaseMic(const std::string& room_id, VoiceOpCallback cb) {
  chirp::voice::MicQueueReleaseRequest req;
  req.set_user_id(CurrentUserId());
  req.set_room_id(room_id);
  RequestAndParse<chirp::voice::MicQueueResponse>(
      *client_, MsgID::MIC_QUEUE_RELEASE_REQ, MsgID::MIC_QUEUE_RELEASE_RESP,
      req.SerializeAsString(), ToCodeOnly<chirp::voice::MicQueueResponse>(std::move(cb)));
}

void VoiceClient::GrantMic(const std::string& room_id, const std::string& target_user_id,
                           VoiceOpCallback cb) {
  chirp::voice::MicQueueGrantRequest req;
  req.set_user_id(CurrentUserId());
  req.set_room_id(room_id);
  req.set_target_user_id(target_user_id);
  RequestAndParse<chirp::voice::MicQueueResponse>(
      *client_, MsgID::MIC_QUEUE_GRANT_REQ, MsgID::MIC_QUEUE_GRANT_RESP,
      req.SerializeAsString(), ToCodeOnly<chirp::voice::MicQueueResponse>(std::move(cb)));
}

void VoiceClient::SetSpeakingCallback(SpeakingCallback cb) { on_speaking_ = std::move(cb); }
void VoiceClient::SetParticipantJoinedCallback(ParticipantJoinedCallback cb) {
  on_joined_ = std::move(cb);
}
void VoiceClient::SetParticipantLeftCallback(ParticipantLeftCallback cb) {
  on_left_ = std::move(cb);
}
void VoiceClient::SetParticipantStateChangedCallback(ParticipantStateChangedCallback cb) {
  on_state_ = std::move(cb);
}
void VoiceClient::SetMicQueueStateCallback(MicQueueStateCallback cb) {
  on_mic_queue_ = std::move(cb);
}

} // namespace sdk
} // namespace chirp
