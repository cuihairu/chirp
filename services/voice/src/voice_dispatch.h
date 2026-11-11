#ifndef CHIRP_VOICE_DISPATCH_H_
#define CHIRP_VOICE_DISPATCH_H_

// Voice service packet handlers and room state (chirp_voice); the
// entry point in main.cc only wires options and lifecycle.

#include <array>
#include <cstdint>
#include <string_view>
#include <mutex>
#include <optional>
#include <string>
#include <unordered_map>
#include <unordered_set>
#include <vector>

#include <asio.hpp>

#include "common/login_token_verifier.h"
#include "network/session.h"
#include "proto/gateway.pb.h"
#include "proto/voice.pb.h"

namespace chirp::voice::asr {
class AsrFilter;
}  // namespace chirp::voice::asr

namespace chirp::voice::direct {

int64_t NowMs();
std::string RandomHex(size_t bytes);

// TURN REST credential primitives (coturn "use-auth-secret"): pure crypto
// with external linkage so the direct-drive unit tests pin known-answer
// vectors (SHA-1/HMAC-SHA1/padded base64; see the .cc for rationale).
std::array<uint8_t, 20> Sha1(std::string_view msg);
std::array<uint8_t, 20> HmacSha1(std::string_view key, std::string_view msg);
std::string Base64Encode(const uint8_t* data, size_t len);
std::string MakeTurnUsername(int64_t expiry_unix, const std::string& user_id);
std::string MakeTurnCredential(const std::string& secret, const std::string& username);

// Voice room state
struct VoiceRoom {
  std::string room_id;
  chirp::voice::RoomType room_type;
  std::string room_name;
  int32_t max_participants;
  int64_t created_at;

  std::unordered_map<std::string, chirp::voice::ParticipantInfo> participants;
  std::mutex mu;
};

// Startup configuration: written once in main(), read-only afterwards.
struct VoiceConfig {
  // Null or disabled() => scaffold mode: identity is whatever the client
  // reports. With a secret, business packets require a prior LOGIN.
  const chirp::common::LoginTokenVerifier* verifier = nullptr;
  // Empty => no ice_servers in join responses. URLs are comma-separated;
  // credentials are minted only when turn_secret is also set.
  std::string turn_uri;
  std::string turn_secret;
  int64_t turn_credential_ttl_seconds = 86400;
  // Idle connection sweep; 0 disables. 75000ms = three 25s heartbeat windows.
  int64_t heartbeat_timeout_ms = 75000;
};

// Voice service state
struct VoiceState {
  std::mutex mu;

  // room_id -> room
  std::unordered_map<std::string, std::shared_ptr<VoiceRoom>> rooms;

  // user_id -> room_id (current room for each user)
  std::unordered_map<std::string, std::string> user_to_room;

  // Session tracking
  std::unordered_map<std::string, std::weak_ptr<chirp::network::Session>> user_to_session;
  std::unordered_map<void*, std::string> session_to_user;

  // Sessions that completed LOGIN (checked by the auth gate when a secret is
  // configured) and the last-seen stamp refreshed on every parsed packet
  // (drives the idle-connection sweep).
  std::unordered_set<void*> authenticated_sessions;
  std::unordered_map<void*, int64_t> session_to_last_seen_ms;

  VoiceConfig cfg;

  // Voice moderation chain (VOICE-ASR-FILTER), built in main() when
  // --asr_lexicon is set: null = chain off. The media path consults it per
  // transcribed chunk; the ASR result never leaves the filter untouched.
  std::shared_ptr<chirp::voice::asr::AsrFilter> asr_filter;
};

void SendPacket(const std::shared_ptr<chirp::network::Session>& session,
                chirp::gateway::MsgID msg_id,
                int64_t seq,
                const std::string& body);
void BroadcastToRoom(const std::shared_ptr<VoiceRoom>& room,
                     chirp::gateway::MsgID msg_id,
                     const std::string& body,
                     const std::shared_ptr<VoiceState>& state,
                     const std::string& exclude_user = "");
void RelayToUser(const std::shared_ptr<VoiceRoom>& room,
                 const std::string& user_id,
                 chirp::gateway::MsgID msg_id,
                 const std::string& body,
                 const std::shared_ptr<VoiceState>& state);
void BindSession(const std::shared_ptr<VoiceState>& state,
                 const std::shared_ptr<chirp::network::Session>& session,
                 const std::string& user_id);
template <typename RespT>
std::optional<std::string> AuthorizeActor(const std::shared_ptr<VoiceState>& state,
                           const std::shared_ptr<chirp::network::Session>& session,
                           const chirp::gateway::Packet& pkt,
                           chirp::gateway::MsgID resp_id,
                           const std::string& claimed_user,
                           RespT* resp);
void HandleDisconnect(const std::shared_ptr<VoiceState>& state,
                      const std::shared_ptr<chirp::network::Session>& session);

// LOGIN_REQ: verifies the token (scaffold mode when no secret is configured),
// binds the session (kicking the previous session of the same user) and marks
// it authenticated so the business handlers' gate lets it through.
void HandleLogin(const std::shared_ptr<VoiceState>& state,
                 const std::shared_ptr<chirp::network::Session>& session,
                 const chirp::gateway::Packet& pkt);
void AppendIceServers(const std::shared_ptr<VoiceState>& state,
                      const std::string& user_id,
                      chirp::voice::JoinRoomResponse* resp);
void HandleCreateRoom(const std::shared_ptr<VoiceState>& state,
                      const std::shared_ptr<chirp::network::Session>& session,
                      const chirp::gateway::Packet& pkt);
void HandleJoinRoom(const std::shared_ptr<VoiceState>& state,
                    const std::shared_ptr<chirp::network::Session>& session,
                    const chirp::gateway::Packet& pkt);
void HandleLeaveRoom(const std::shared_ptr<VoiceState>& state,
                     const std::shared_ptr<chirp::network::Session>& session,
                     const chirp::gateway::Packet& pkt);
std::shared_ptr<VoiceRoom> FindRoom(const std::shared_ptr<VoiceState>& state,
                                    const std::string& room_id);
chirp::voice::ParticipantState DeriveParticipantState(bool muted, bool deafened);
template <typename ReqT, typename RespT>
void SetParticipantFlag(const std::shared_ptr<VoiceState>& state,
                        const std::shared_ptr<chirp::network::Session>& session,
                        const chirp::gateway::Packet& pkt,
                        chirp::gateway::MsgID resp_id,
                        bool is_deafen);
void HandleSetMute(const std::shared_ptr<VoiceState>& state,
                   const std::shared_ptr<chirp::network::Session>& session,
                   const chirp::gateway::Packet& pkt);
void HandleSetDeafen(const std::shared_ptr<VoiceState>& state,
                     const std::shared_ptr<chirp::network::Session>& session,
                     const chirp::gateway::Packet& pkt);
void HandleGetRoomInfo(const std::shared_ptr<VoiceState>& state,
                       const std::shared_ptr<chirp::network::Session>& session,
                       const chirp::gateway::Packet& pkt);
void HandleGetUserRoom(const std::shared_ptr<VoiceState>& state,
                       const std::shared_ptr<chirp::network::Session>& session,
                       const chirp::gateway::Packet& pkt);
void HandleIceCandidate(const std::shared_ptr<VoiceState>& state,
                        [[maybe_unused]] const std::shared_ptr<chirp::network::Session>& session,
                        const chirp::gateway::Packet& pkt);
void HandleSdpOffer(const std::shared_ptr<VoiceState>& state,
                    [[maybe_unused]] const std::shared_ptr<chirp::network::Session>& session,
                    const chirp::gateway::Packet& pkt);
void HandleSdpAnswer(const std::shared_ptr<VoiceState>& state,
                     [[maybe_unused]] const std::shared_ptr<chirp::network::Session>& session,
                     const chirp::gateway::Packet& pkt);
std::vector<std::pair<std::shared_ptr<chirp::network::Session>, std::string>>
CollectTimedOutSessions(const std::shared_ptr<VoiceState>& state, int64_t now_ms);
void SweepHeartbeatTimeout(const std::shared_ptr<VoiceState>& state, int64_t now_ms);
void ScheduleHeartbeatSweep(const std::shared_ptr<asio::steady_timer>& timer,
                            const std::shared_ptr<VoiceState>& state);
void HandlePacket(const std::shared_ptr<VoiceState>& state,
                  const std::shared_ptr<chirp::network::Session>& session,
                  std::string&& payload);

}  // namespace chirp::voice::direct

#endif  // CHIRP_VOICE_DISPATCH_H_
