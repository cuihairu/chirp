#pragma once

#include "sdk.h"
#include "proto/common.pb.h"
#include "proto/voice.pb.h"

#include <functional>
#include <memory>
#include <mutex>
#include <string>
#include <vector>

namespace chirp {
namespace sdk {

class ChatClient;

// ---- 语音面回调(VOICE-RT SDK 语音面)。ec 只覆盖传输层(Timeout/Closed/
// Kicked)与协议异常(BadResponse);业务结果(房间不存在、非房主等)读
// resp/code。所有回调在 SDK 内部 io 线程触发,引擎适配层负责派发回游戏线程。
using VoiceJoinCallback =
    std::function<void(const std::error_code& ec, const chirp::voice::JoinRoomResponse&)>;
using VoiceCreateRoomCallback =
    std::function<void(const std::error_code& ec, const chirp::voice::CreateRoomResponse&)>;
using VoiceRoomInfoCallback =
    std::function<void(const std::error_code& ec, const chirp::voice::GetRoomInfoResponse&)>;
// mute/deafen/麦序等只回 code 的操作。
using VoiceOpCallback = std::function<void(const std::error_code& ec, chirp::common::ErrorCode code)>;

// 服务端推送(均为 sequence==0 的 notify 帧,经 OnNotify 订阅解包)。
using SpeakingCallback = std::function<void(const chirp::voice::SpeakingNotify&)>;
using ParticipantJoinedCallback =
    std::function<void(const chirp::voice::ParticipantJoinedNotify&)>;
using ParticipantLeftCallback = std::function<void(const chirp::voice::ParticipantLeftNotify&)>;
using ParticipantStateChangedCallback =
    std::function<void(const chirp::voice::ParticipantStateChangedNotify&)>;
using MicQueueStateCallback = std::function<void(const chirp::voice::MicQueueStateNotify&)>;

// 语音客户端:包装 ChatClient 指向 voice 服务端口。voice 与聊天网关共用
// [u32_be len][Packet] 帧与 LOGIN_REQ 认证(chirp::auth::LoginRequest;
// scaffold 模式 token 即 user_id),所以连接/心跳/重连语义全部复用
// ChatClient,这里只补语音语义的请求封装与 notify 解包(VOICE-RT)。
class VoiceClient {
public:
  explicit VoiceClient(const ChatConfig& config);
  ~VoiceClient();

  VoiceClient(const VoiceClient&) = delete;
  VoiceClient& operator=(const VoiceClient&) = delete;
  VoiceClient(VoiceClient&&) = delete;
  VoiceClient& operator=(VoiceClient&&) = delete;

  // 连接与认证(直通 ChatClient;连接语义/心跳/重连见 sdk.h)。
  void Connect();
  void Disconnect();
  ConnectionState GetState() const;
  void Login(const std::string& token, LoginCallback cb);

  // 房间:建/进/出/查。JoinRoom 的响应带服务端分级(FULL_MIX/FORWARD_BYPASS
  // 与 rtcp_feedback_ms,VOICE-RT 旁路分级),引擎据此配置混音或旁路。
  void CreateRoom(chirp::voice::RoomType type, const std::string& name,
                  int32_t max_participants, VoiceCreateRoomCallback cb);
  void JoinRoom(const std::string& room_id, const std::string& sdp_offer,
                VoiceJoinCallback cb);
  void LeaveRoom(const std::string& room_id, VoiceOpCallback cb);
  void FetchRoomInfo(const std::string& room_id, VoiceRoomInfoCallback cb);

  // 自身状态:静音麦/静音扬声器。
  void SetMute(const std::string& room_id, bool muted, VoiceOpCallback cb);
  void SetDeafen(const std::string& room_id, bool deafened, VoiceOpCallback cb);

  // 麦序模式(VOICE-RT):host 切模式/批准/切麦,成员申请/下麦。
  void SetMicMode(const std::string& room_id, chirp::voice::MicMode mode, VoiceOpCallback cb);
  void ApplyForMic(const std::string& room_id, VoiceOpCallback cb);
  // target_user_id 空 = 批准队首。
  void ApproveMic(const std::string& room_id, const std::string& target_user_id,
                  VoiceOpCallback cb);
  void ReleaseMic(const std::string& room_id, VoiceOpCallback cb);
  // host 直接把麦交给 target(绕过队列)。
  void GrantMic(const std::string& room_id, const std::string& target_user_id,
                VoiceOpCallback cb);

  // 服务端推送订阅(同一时刻只有一个回调;重复 Set 覆盖)。
  void SetSpeakingCallback(SpeakingCallback cb);
  void SetParticipantJoinedCallback(ParticipantJoinedCallback cb);
  void SetParticipantLeftCallback(ParticipantLeftCallback cb);
  void SetParticipantStateChangedCallback(ParticipantStateChangedCallback cb);
  void SetMicQueueStateCallback(MicQueueStateCallback cb);

private:
  // 登录身份(服务端把 user_id 与会话绑定,请求体里的 user_id 必须一致;
  // 未登录时空串,请求按未认证被拒)。
  std::string CurrentUserId() const;

  std::unique_ptr<ChatClient> client_;
  mutable std::mutex user_mu_;
  std::string user_id_;
  // ctor 里注册、dtor 里退订的 notify 句柄。
  uint64_t speaking_handle_ = 0;
  uint64_t joined_handle_ = 0;
  uint64_t left_handle_ = 0;
  uint64_t state_handle_ = 0;
  uint64_t mic_queue_handle_ = 0;
  // 回调体(std::function 赋值任意线程可调)。
  SpeakingCallback on_speaking_;
  ParticipantJoinedCallback on_joined_;
  ParticipantLeftCallback on_left_;
  ParticipantStateChangedCallback on_state_;
  MicQueueStateCallback on_mic_queue_;
};

} // namespace sdk
} // namespace chirp
