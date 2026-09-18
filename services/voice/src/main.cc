#include <array>
#include <chrono>
#include <cstdint>
#include <cstdlib>
#include <memory>
#include <mutex>
#include <optional>
#include <random>
#include <string>
#include <unordered_map>
#include <unordered_set>
#include <vector>

#include <asio.hpp>

#include "common/login_token_verifier.h"
#include "logger.h"
#include "network/protobuf_framing.h"
#include "network/session.h"
#include "network/tcp_server.h"
#include "network/websocket_server.h"
#include "proto/auth.pb.h"
#include "proto/common.pb.h"
#include "proto/gateway.pb.h"
#include "proto/voice.pb.h"

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

// ---------------------------------------------------------------------------
// TURN REST credential primitives (coturn "use-auth-secret").
//
// coturn's REST API only accepts HMAC-SHA1 (SHA-256 support exists solely as
// an unmerged fork patch), and the repo carries no crypto library providing
// SHA-1 (libsodium has none, abseil has none, OpenSSL is not a target), so
// SHA-1 / HMAC-SHA1 / padded standard base64 live here as pure functions.
// The sole consumer is the TURN credential below; if a second consumer ever
// appears, hoist these into libs/common (mirroring sha256.* there).
// ---------------------------------------------------------------------------

constexpr size_t kSha1BlockSize = 64;  // bytes, per FIPS 180-4

std::array<uint8_t, 20> Sha1(std::string_view msg) {
  uint32_t h[5] = {0x67452301u, 0xEFCDAB89u, 0x98BADCFEu, 0x10325476u,
                   0xC3D2E1F0u};

  // Pad: 0x80, zeros to 56 mod 64, then the 64-bit big-endian bit length.
  std::string padded(msg);
  const uint64_t bit_len = static_cast<uint64_t>(msg.size()) * 8;
  padded.push_back('\x80');
  while (padded.size() % kSha1BlockSize != 56) {
    padded.push_back('\0');
  }
  for (int shift = 56; shift >= 0; shift -= 8) {
    padded.push_back(static_cast<char>((bit_len >> shift) & 0xFF));
  }

  for (size_t off = 0; off < padded.size(); off += kSha1BlockSize) {
    uint32_t w[80];
    for (int i = 0; i < 16; i++) {
      w[i] = (static_cast<uint32_t>(static_cast<uint8_t>(padded[off + i * 4])) << 24) |
             (static_cast<uint32_t>(static_cast<uint8_t>(padded[off + i * 4 + 1])) << 16) |
             (static_cast<uint32_t>(static_cast<uint8_t>(padded[off + i * 4 + 2])) << 8) |
             static_cast<uint32_t>(static_cast<uint8_t>(padded[off + i * 4 + 3]));
    }
    for (int i = 16; i < 80; i++) {
      const uint32_t v = w[i - 3] ^ w[i - 8] ^ w[i - 14] ^ w[i - 16];
      w[i] = (v << 1) | (v >> 31);
    }

    uint32_t a = h[0], b = h[1], c = h[2], d = h[3], e = h[4];
    for (int i = 0; i < 80; i++) {
      uint32_t f, k;
      if (i < 20) {
        f = (b & c) | ((~b) & d);
        k = 0x5A827999u;
      } else if (i < 40) {
        f = b ^ c ^ d;
        k = 0x6ED9EBA1u;
      } else if (i < 60) {
        f = (b & c) | (b & d) | (c & d);
        k = 0x8F1BBCDCu;
      } else {
        f = b ^ c ^ d;
        k = 0xCA62C1D6u;
      }
      const uint32_t tmp = ((a << 5) | (a >> 27)) + f + e + k + w[i];
      e = d;
      d = c;
      c = (b << 30) | (b >> 2);
      b = a;
      a = tmp;
    }
    h[0] += a;
    h[1] += b;
    h[2] += c;
    h[3] += d;
    h[4] += e;
  }

  std::array<uint8_t, 20> out{};
  for (int i = 0; i < 5; i++) {
    out[i * 4] = static_cast<uint8_t>(h[i] >> 24);
    out[i * 4 + 1] = static_cast<uint8_t>(h[i] >> 16);
    out[i * 4 + 2] = static_cast<uint8_t>(h[i] >> 8);
    out[i * 4 + 3] = static_cast<uint8_t>(h[i]);
  }
  return out;
}

std::array<uint8_t, 20> HmacSha1(std::string_view key, std::string_view msg) {
  std::string block(kSha1BlockSize, '\0');
  if (key.size() > kSha1BlockSize) {
    const auto keyed = Sha1(key);
    std::copy(keyed.begin(), keyed.end(), block.begin());
  } else {
    std::copy(key.begin(), key.end(), block.begin());
  }

  std::string inner(kSha1BlockSize, '\0');
  std::string outer(kSha1BlockSize, '\0');
  for (size_t i = 0; i < kSha1BlockSize; i++) {
    inner[i] = block[i] ^ '\x36';
    outer[i] = block[i] ^ '\x5c';
  }
  inner.append(msg);
  const auto inner_hash = Sha1(inner);
  outer.append(reinterpret_cast<const char*>(inner_hash.data()), inner_hash.size());
  return Sha1(outer);
}

// Standard-alphabet base64 with '=' padding (coturn decodes the credential
// with a standard decoder; the URL-safe unpadded variant in libs/common does
// not match it).
std::string Base64Encode(const uint8_t* data, size_t len) {
  static const char* kAlphabet =
      "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/";
  std::string out;
  out.reserve(((len + 2) / 3) * 4);
  for (size_t i = 0; i < len; i += 3) {
    const uint32_t b0 = data[i];
    const uint32_t b1 = i + 1 < len ? data[i + 1] : 0;
    const uint32_t b2 = i + 2 < len ? data[i + 2] : 0;
    const uint32_t triple = (b0 << 16) | (b1 << 8) | b2;
    out.push_back(kAlphabet[(triple >> 18) & 0x3F]);
    out.push_back(kAlphabet[(triple >> 12) & 0x3F]);
    out.push_back(i + 1 < len ? kAlphabet[(triple >> 6) & 0x3F] : '=');
    out.push_back(i + 2 < len ? kAlphabet[triple & 0x3F] : '=');
  }
  return out;
}

// Mints a coturn REST short-term credential: the username leads with the
// unix expiry (coturn parses the leading integer timestamp and tolerates the
// ":user_id" suffix, which then shows up in its logs for auditing), and the
// password is base64(HMAC-SHA1(secret, username)).
std::string MakeTurnUsername(int64_t expiry_unix, const std::string& user_id) {
  return std::to_string(expiry_unix) + ":" + user_id;
}

std::string MakeTurnCredential(const std::string& secret, const std::string& username) {
  const auto mac = HmacSha1(secret, username);
  return Base64Encode(mac.data(), mac.size());
}

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

void BroadcastToRoom(const std::shared_ptr<VoiceRoom>& room,
                     chirp::gateway::MsgID msg_id,
                     const std::string& body,
                     const std::shared_ptr<VoiceState>& state,
                     const std::string& exclude_user = "") {
  std::vector<std::shared_ptr<chirp::network::Session>> targets;
  {
    std::lock_guard<std::mutex> lock(room->mu);
    for (const auto& kv : room->participants) {
      if (!exclude_user.empty() && kv.first == exclude_user) {
        continue;
      }
      auto it = state->user_to_session.find(kv.first);
      if (it != state->user_to_session.end()) {
        auto sess = it->second.lock();
        if (sess) {
          targets.push_back(sess);
        }
      }
    }
  }

  for (auto& sess : targets) {
    SendPacket(sess, msg_id, 0, body);
  }
}

// Delivers a signaling message to one participant only; dropped when the
// target is not in the room or has no bound session (no signaling leaks
// across rooms).
void RelayToUser(const std::shared_ptr<VoiceRoom>& room,
                 const std::string& user_id,
                 chirp::gateway::MsgID msg_id,
                 const std::string& body,
                 const std::shared_ptr<VoiceState>& state) {
  {
    std::lock_guard<std::mutex> lock(room->mu);
    if (room->participants.find(user_id) == room->participants.end()) {
      return;
    }
  }

  std::shared_ptr<chirp::network::Session> target;
  {
    std::lock_guard<std::mutex> lock(state->mu);
    auto it = state->user_to_session.find(user_id);
    if (it != state->user_to_session.end()) {
      target = it->second.lock();
    }
  }
  if (target) {
    SendPacket(target, msg_id, 0, body);
  }
}

// Records which session belongs to which user so broadcasts can reach them
// and disconnect cleanup can find the room to leave.
void BindSession(const std::shared_ptr<VoiceState>& state,
                 const std::shared_ptr<chirp::network::Session>& session,
                 const std::string& user_id) {
  std::lock_guard<std::mutex> lock(state->mu);
  state->session_to_user[session.get()] = user_id;
  state->user_to_session[user_id] = session;
}

// Auth gate for every business handler. Returns the effective user_id (the
// login-bound identity when the session has LOGINed, else the self-reported
// one in scaffold mode) — possibly empty, for queries that carry no user_id
// and legitimately authorize an anonymous scaffold session; on failure it
// sends the error response and returns nullopt.
//   - no secret configured (scaffold): unauthenticated packets keep the
//     legacy self-reported identity, so existing clients and the scaffold
//     smokes keep working untouched;
//   - secret configured: a business packet before LOGIN is AUTH_FAILED;
//   - either mode: a packet claiming a different user_id than the bound
//     identity is INVALID_PARAM (identity forgery).
template <typename RespT>
std::optional<std::string> AuthorizeActor(const std::shared_ptr<VoiceState>& state,
                           const std::shared_ptr<chirp::network::Session>& session,
                           const chirp::gateway::Packet& pkt,
                           chirp::gateway::MsgID resp_id,
                           const std::string& claimed_user,
                           RespT* resp) {
  const bool requires_login = state->cfg.verifier && state->cfg.verifier->enabled();
  const auto stamp = [&] {
    if constexpr (requires { resp->set_server_time(NowMs()); }) {
      resp->set_server_time(NowMs());
    }
  };

  std::string bound_user;
  bool authenticated = false;
  {
    std::lock_guard<std::mutex> lock(state->mu);
    authenticated = state->authenticated_sessions.count(session.get()) > 0;
    if (authenticated) {
      auto it = state->session_to_user.find(session.get());
      if (it != state->session_to_user.end()) {
        bound_user = it->second;
      }
    }
  }

  if (!authenticated && requires_login) {
    resp->set_code(chirp::common::AUTH_FAILED);
    stamp();
    SendPacket(session, resp_id, pkt.sequence(), resp->SerializeAsString());
    return {};
  }
  if (authenticated) {
    if (!claimed_user.empty() && claimed_user != bound_user) {
      resp->set_code(chirp::common::INVALID_PARAM);
      stamp();
      SendPacket(session, resp_id, pkt.sequence(), resp->SerializeAsString());
      return {};
    }
    return bound_user;
  }
  return claimed_user;  // scaffold, unauthenticated: legacy self-reported id
}

void HandleDisconnect(const std::shared_ptr<VoiceState>& state,
                      const std::shared_ptr<chirp::network::Session>& session);

// LOGIN_REQ: verifies the token (scaffold mode when no secret is configured),
// binds the session (kicking the previous session of the same user) and marks
// it authenticated so the business handlers' gate lets it through.
void HandleLogin(const std::shared_ptr<VoiceState>& state,
                 const std::shared_ptr<chirp::network::Session>& session,
                 const chirp::gateway::Packet& pkt) {
  using chirp::common::Logger;

  chirp::auth::LoginRequest login_req;
  if (!login_req.ParseFromArray(pkt.body().data(), static_cast<int>(pkt.body().size()))) {
    chirp::auth::LoginResponse resp;
    resp.set_code(chirp::common::INVALID_PARAM);
    resp.set_server_time(NowMs());
    SendPacket(session, chirp::gateway::LOGIN_RESP, pkt.sequence(), resp.SerializeAsString());
    return;
  }

  std::string user_id;
  if (state->cfg.verifier && state->cfg.verifier->enabled()) {
    // Real mode: the token must be an HS256 JWT signed with the shared
    // secret, unexpired, with the login user in "sub".
    std::string verify_err;
    if (!state->cfg.verifier->Verify(login_req.token(), NowMs(), &user_id, &verify_err)) {
      Logger::Instance().Warn("voice login rejected: " + verify_err);
      chirp::auth::LoginResponse resp;
      resp.set_code(chirp::common::AUTH_FAILED);
      resp.set_server_time(NowMs());
      SendPacket(session, chirp::gateway::LOGIN_RESP, pkt.sequence(), resp.SerializeAsString());
      return;
    }
  } else {
    // Scaffolding login: treat token as user_id.
    user_id = login_req.token();
  }

  if (user_id.empty()) {
    chirp::auth::LoginResponse resp;
    resp.set_code(chirp::common::INVALID_PARAM);
    resp.set_server_time(NowMs());
    SendPacket(session, chirp::gateway::LOGIN_RESP, pkt.sequence(), resp.SerializeAsString());
    return;
  }

  // Kick the previous session before rebinding: voice is one connection per
  // user, so the old session must leave its room (with the LEFT broadcast)
  // right away instead of waiting for the kernel to notice the dead socket.
  std::shared_ptr<chirp::network::Session> old;
  {
    std::lock_guard<std::mutex> lock(state->mu);
    auto it = state->user_to_session.find(user_id);
    if (it != state->user_to_session.end()) {
      old = it->second.lock();
    }
  }
  if (old && old.get() != session.get()) {
    chirp::auth::KickNotify kick;
    kick.set_reason("login from another device");
    SendPacket(old, chirp::gateway::KICK_NOTIFY, 0, kick.SerializeAsString());
    HandleDisconnect(state, old);  // idempotent: session_to_user is erased first
    old->Close();
  }

  BindSession(state, session, user_id);
  {
    std::lock_guard<std::mutex> lock(state->mu);
    state->authenticated_sessions.insert(session.get());
  }

  chirp::auth::LoginResponse resp;
  resp.set_code(chirp::common::OK);
  resp.set_user_id(user_id);
  resp.set_session_id("voice_session_" + RandomHex(8));
  resp.set_kick_previous(static_cast<bool>(old));
  resp.mutable_kick()->set_reason("login from another device");
  resp.set_server_time(NowMs());
  SendPacket(session, chirp::gateway::LOGIN_RESP, pkt.sequence(), resp.SerializeAsString());
}

// Fills a join response with the configured TURN/STUN access: one IceServer
// carrying the comma-separated urls, plus coturn REST short-term credentials
// when a static secret is set (STUN-only otherwise, empty username/
// credential).
void AppendIceServers(const std::shared_ptr<VoiceState>& state,
                      const std::string& user_id,
                      chirp::voice::JoinRoomResponse* resp) {
  if (state->cfg.turn_uri.empty()) {
    return;
  }
  chirp::voice::IceServer* ice = resp->add_ice_servers();
  for (size_t pos = 0; pos < state->cfg.turn_uri.size();) {
    const size_t comma = state->cfg.turn_uri.find(',', pos);
    const size_t end = comma == std::string::npos ? state->cfg.turn_uri.size() : comma;
    std::string url = state->cfg.turn_uri.substr(pos, end - pos);
    while (!url.empty() && url.front() == ' ') {
      url.erase(url.begin());
    }
    while (!url.empty() && url.back() == ' ') {
      url.pop_back();
    }
    if (!url.empty()) {
      ice->add_urls(url);
    }
    if (comma == std::string::npos) {
      break;
    }
    pos = end + 1;
  }
  if (!state->cfg.turn_secret.empty()) {
    const int64_t expiry_unix = NowMs() / 1000 + state->cfg.turn_credential_ttl_seconds;
    const std::string username = MakeTurnUsername(expiry_unix, user_id);
    ice->set_username(username);
    ice->set_credential(MakeTurnCredential(state->cfg.turn_secret, username));
  }
}

void HandleCreateRoom(const std::shared_ptr<VoiceState>& state,
                      const std::shared_ptr<chirp::network::Session>& session,
                      const chirp::gateway::Packet& pkt) {
  chirp::voice::CreateRoomRequest req;
  if (!req.ParseFromArray(pkt.body().data(), static_cast<int>(pkt.body().size()))) {
    chirp::voice::CreateRoomResponse resp;
    resp.set_code(chirp::common::INVALID_PARAM);
    resp.set_server_time(NowMs());
    SendPacket(session, chirp::gateway::CREATE_ROOM_RESP, pkt.sequence(), resp.SerializeAsString());
    return;
  }

  std::string room_id = "room_" + RandomHex(8);
  auto room = std::make_shared<VoiceRoom>();
  room->room_id = room_id;
  room->room_type = req.room_type();
  room->room_name = req.room_name();
  room->max_participants = req.max_participants();
  room->created_at = NowMs();

  {
    std::lock_guard<std::mutex> lock(state->mu);
    state->rooms[room_id] = room;
  }

  chirp::voice::CreateRoomResponse resp;
  resp.set_code(chirp::common::OK);
  resp.set_room_id(room_id);
  resp.set_server_time(NowMs());
  SendPacket(session, chirp::gateway::CREATE_ROOM_RESP, pkt.sequence(), resp.SerializeAsString());
}

void HandleJoinRoom(const std::shared_ptr<VoiceState>& state,
                    const std::shared_ptr<chirp::network::Session>& session,
                    const chirp::gateway::Packet& pkt) {
  chirp::voice::JoinRoomRequest req;
  if (!req.ParseFromArray(pkt.body().data(), static_cast<int>(pkt.body().size()))) {
    chirp::voice::JoinRoomResponse resp;
    resp.set_code(chirp::common::INVALID_PARAM);
    resp.set_server_time(NowMs());
    SendPacket(session, chirp::gateway::JOIN_ROOM_RESP, pkt.sequence(), resp.SerializeAsString());
    return;
  }

  chirp::voice::JoinRoomResponse auth_resp;
  const std::optional<std::string> authorized = AuthorizeActor(state, session, pkt,
                                                               chirp::gateway::JOIN_ROOM_RESP,
                                                               req.user_id(), &auth_resp);
  if (!authorized.has_value()) {
    return;
  }
  const std::string actor = *authorized;

  std::shared_ptr<VoiceRoom> room;
  {
    std::lock_guard<std::mutex> lock(state->mu);
    auto it = state->rooms.find(req.room_id());
    if (it == state->rooms.end()) {
      chirp::voice::JoinRoomResponse resp;
      resp.set_code(chirp::common::USER_NOT_FOUND);  // Room not found
      resp.set_server_time(NowMs());
      SendPacket(session, chirp::gateway::JOIN_ROOM_RESP, pkt.sequence(), resp.SerializeAsString());
      return;
    }
    room = it->second;
  }

  // Capacity check before any state mutation: a rejected join must leave the
  // user's previous room membership untouched.
  {
    std::lock_guard<std::mutex> lock(room->mu);
    if (room->max_participants > 0 && static_cast<int32_t>(room->participants.size()) >= room->max_participants) {
      chirp::voice::JoinRoomResponse resp;
      resp.set_code(chirp::common::INTERNAL_ERROR);  // Room full
      resp.set_server_time(NowMs());
      SendPacket(session, chirp::gateway::JOIN_ROOM_RESP, pkt.sequence(), resp.SerializeAsString());
      return;
    }
  }

  {
    std::lock_guard<std::mutex> lock(state->mu);
    // Remove from previous room if any
    auto prev_room_it = state->user_to_room.find(actor);
    if (prev_room_it != state->user_to_room.end()) {
      auto prev_room_it2 = state->rooms.find(prev_room_it->second);
      if (prev_room_it2 != state->rooms.end()) {
        std::lock_guard<std::mutex> room_lock(prev_room_it2->second->mu);
        prev_room_it2->second->participants.erase(actor);
      }
    }
    state->user_to_room[actor] = req.room_id();
  }

  // Add participant to room
  std::vector<std::string> existing_participants;
  {
    std::lock_guard<std::mutex> lock(room->mu);
    chirp::voice::ParticipantInfo participant;
    participant.set_user_id(actor);
    participant.set_state(chirp::voice::CONNECTED);
    participant.set_joined_at(NowMs());
    room->participants[actor] = participant;

    for (const auto& kv : room->participants) {
      existing_participants.push_back(kv.first);
    }
  }

  BindSession(state, session, actor);

  // Notify existing participants
  chirp::voice::ParticipantJoinedNotify joined_notify;
  joined_notify.set_room_id(req.room_id());
  joined_notify.mutable_participant()->set_user_id(actor);
  joined_notify.mutable_participant()->set_state(chirp::voice::CONNECTED);
  joined_notify.mutable_participant()->set_joined_at(NowMs());
  joined_notify.set_timestamp(NowMs());
  BroadcastToRoom(room, chirp::gateway::PARTICIPANT_JOINED_NOTIFY, joined_notify.SerializeAsString(), state, actor);

  chirp::voice::JoinRoomResponse resp;
  resp.set_code(chirp::common::OK);
  resp.set_room_id(req.room_id());
  resp.set_sdp_answer(req.sdp_offer());  // In production, this would be actual SDP answer
  for (const auto& pid : existing_participants) {
    resp.add_participant_ids(pid);
  }
  resp.set_server_time(NowMs());
  AppendIceServers(state, actor, &resp);
  SendPacket(session, chirp::gateway::JOIN_ROOM_RESP, pkt.sequence(), resp.SerializeAsString());
}

void HandleLeaveRoom(const std::shared_ptr<VoiceState>& state,
                     const std::shared_ptr<chirp::network::Session>& session,
                     const chirp::gateway::Packet& pkt) {
  chirp::voice::LeaveRoomRequest req;
  if (!req.ParseFromArray(pkt.body().data(), static_cast<int>(pkt.body().size()))) {
    chirp::voice::LeaveRoomResponse resp;
    resp.set_code(chirp::common::INVALID_PARAM);
    resp.set_server_time(NowMs());
    SendPacket(session, chirp::gateway::LEAVE_ROOM_RESP, pkt.sequence(), resp.SerializeAsString());
    return;
  }

  std::shared_ptr<VoiceRoom> room;
  {
    std::lock_guard<std::mutex> lock(state->mu);
    auto it = state->rooms.find(req.room_id());
    if (it == state->rooms.end()) {
      chirp::voice::LeaveRoomResponse resp;
      resp.set_code(chirp::common::OK);  // Already left or room doesn't exist
      resp.set_server_time(NowMs());
      SendPacket(session, chirp::gateway::LEAVE_ROOM_RESP, pkt.sequence(), resp.SerializeAsString());
      return;
    }
    room = it->second;
    state->user_to_room.erase(req.user_id());
  }

  {
    std::lock_guard<std::mutex> lock(room->mu);
    room->participants.erase(req.user_id());
  }

  // Notify other participants
  chirp::voice::ParticipantLeftNotify left_notify;
  left_notify.set_room_id(req.room_id());
  left_notify.set_user_id(req.user_id());
  left_notify.set_timestamp(NowMs());
  BroadcastToRoom(room, chirp::gateway::PARTICIPANT_LEFT_NOTIFY, left_notify.SerializeAsString(), state, req.user_id());

  chirp::voice::LeaveRoomResponse resp;
  resp.set_code(chirp::common::OK);
  resp.set_server_time(NowMs());
  SendPacket(session, chirp::gateway::LEAVE_ROOM_RESP, pkt.sequence(), resp.SerializeAsString());
}

// Shared lookup for the signaling relays (duplicates the same block that used
// to sit inside each ICE/SDP handler).
std::shared_ptr<VoiceRoom> FindRoom(const std::shared_ptr<VoiceState>& state,
                                    const std::string& room_id) {
  std::lock_guard<std::mutex> lock(state->mu);
  auto it = state->rooms.find(room_id);
  return it == state->rooms.end() ? nullptr : it->second;
}

// Derived presence state: deafened wins over muted wins over plain connected,
// so unmuting the mic while deafened keeps the participant DEAFENED.
chirp::voice::ParticipantState DeriveParticipantState(bool muted, bool deafened) {
  if (deafened) {
    return chirp::voice::DEAFENED;
  }
  if (muted) {
    return chirp::voice::MUTED;
  }
  return chirp::voice::CONNECTED;
}

// Shared kernel for SET_MUTE/SET_DEAFEN: validates, flips the flag on the
// participant, derives the state enum and notifies the other participants.
// The requester's own confirmation is the RESP, not the broadcast.
template <typename ReqT, typename RespT>
void SetParticipantFlag(const std::shared_ptr<VoiceState>& state,
                        const std::shared_ptr<chirp::network::Session>& session,
                        const chirp::gateway::Packet& pkt,
                        chirp::gateway::MsgID resp_id,
                        bool is_deafen) {
  RespT resp;
  ReqT req;
  if (!req.ParseFromArray(pkt.body().data(), static_cast<int>(pkt.body().size()))) {
    resp.set_code(chirp::common::INVALID_PARAM);
    resp.set_server_time(NowMs());
    SendPacket(session, resp_id, pkt.sequence(), resp.SerializeAsString());
    return;
  }

  if (!AuthorizeActor(state, session, pkt, resp_id, req.user_id(), &resp).has_value()) {
    return;
  }

  // SetMuteRequest carries `muted`, SetDeafenRequest carries `deafened`;
  // pick the field the concrete request type actually has.
  const bool requested = [&] {
    if constexpr (requires { req.muted(); }) {
      return req.muted();
    } else {
      return req.deafened();
    }
  }();

  const auto room = FindRoom(state, req.room_id());
  if (!room) {
    resp.set_code(chirp::common::USER_NOT_FOUND);  // room not found
    resp.set_server_time(NowMs());
    SendPacket(session, resp_id, pkt.sequence(), resp.SerializeAsString());
    return;
  }

  bool broadcast_needed = false;
  {
    std::lock_guard<std::mutex> lock(room->mu);
    auto it = room->participants.find(req.user_id());
    if (it == room->participants.end()) {
      broadcast_needed = false;
    } else {
      if (is_deafen) {
        it->second.set_deafened(requested);
      } else {
        it->second.set_muted(requested);
      }
      it->second.set_state(DeriveParticipantState(it->second.muted(), it->second.deafened()));
      broadcast_needed = true;
    }
  }
  if (!broadcast_needed) {
    resp.set_code(chirp::common::USER_NOT_FOUND);  // not a participant
    resp.set_server_time(NowMs());
    SendPacket(session, resp_id, pkt.sequence(), resp.SerializeAsString());
    return;
  }

  resp.set_code(chirp::common::OK);
  resp.set_server_time(NowMs());
  SendPacket(session, resp_id, pkt.sequence(), resp.SerializeAsString());

  chirp::voice::ParticipantStateChangedNotify notify;
  notify.set_room_id(req.room_id());
  notify.set_user_id(req.user_id());
  {
    std::lock_guard<std::mutex> lock(room->mu);
    auto it = room->participants.find(req.user_id());
    if (it != room->participants.end()) {
      notify.set_state(it->second.state());
    }
  }
  notify.set_timestamp(NowMs());
  BroadcastToRoom(room, chirp::gateway::PARTICIPANT_STATE_CHANGED_NOTIFY,
                  notify.SerializeAsString(), state, req.user_id());
}

void HandleSetMute(const std::shared_ptr<VoiceState>& state,
                   const std::shared_ptr<chirp::network::Session>& session,
                   const chirp::gateway::Packet& pkt) {
  SetParticipantFlag<chirp::voice::SetMuteRequest, chirp::voice::SetMuteResponse>(
      state, session, pkt, chirp::gateway::SET_MUTE_RESP, false);
}

void HandleSetDeafen(const std::shared_ptr<VoiceState>& state,
                     const std::shared_ptr<chirp::network::Session>& session,
                     const chirp::gateway::Packet& pkt) {
  SetParticipantFlag<chirp::voice::SetDeafenRequest, chirp::voice::SetDeafenResponse>(
      state, session, pkt, chirp::gateway::SET_DEAFEN_RESP, true);
}

void HandleGetRoomInfo(const std::shared_ptr<VoiceState>& state,
                       const std::shared_ptr<chirp::network::Session>& session,
                       const chirp::gateway::Packet& pkt) {
  chirp::voice::GetRoomInfoRequest req;
  chirp::voice::GetRoomInfoResponse resp;
  // GetRoomInfoRequest carries no user_id: the gate only checks that a
  // secret-configured session has LOGINed.
  if (!req.ParseFromArray(pkt.body().data(), static_cast<int>(pkt.body().size()))) {
    resp.set_code(chirp::common::INVALID_PARAM);
    SendPacket(session, chirp::gateway::GET_ROOM_INFO_RESP, pkt.sequence(), resp.SerializeAsString());
    return;
  }
  if (!AuthorizeActor(state, session, pkt, chirp::gateway::GET_ROOM_INFO_RESP, "", &resp).has_value()) {
    return;
  }

  const auto room = FindRoom(state, req.room_id());
  if (!room) {
    resp.set_code(chirp::common::USER_NOT_FOUND);  // room not found
    SendPacket(session, chirp::gateway::GET_ROOM_INFO_RESP, pkt.sequence(), resp.SerializeAsString());
    return;
  }

  resp.set_code(chirp::common::OK);
  resp.set_room_id(room->room_id);
  resp.set_max_participants(room->max_participants);
  {
    std::lock_guard<std::mutex> lock(room->mu);
    resp.set_room_name(room->room_name);
    resp.set_room_type(room->room_type);
    for (const auto& kv : room->participants) {
      *resp.add_participants() = kv.second;
    }
  }
  SendPacket(session, chirp::gateway::GET_ROOM_INFO_RESP, pkt.sequence(), resp.SerializeAsString());
}

void HandleGetUserRoom(const std::shared_ptr<VoiceState>& state,
                       const std::shared_ptr<chirp::network::Session>& session,
                       const chirp::gateway::Packet& pkt) {
  chirp::voice::GetUserRoomRequest req;
  chirp::voice::GetUserRoomResponse resp;
  if (!req.ParseFromArray(pkt.body().data(), static_cast<int>(pkt.body().size()))) {
    resp.set_code(chirp::common::INVALID_PARAM);
    SendPacket(session, chirp::gateway::GET_USER_ROOM_RESP, pkt.sequence(), resp.SerializeAsString());
    return;
  }
  if (!AuthorizeActor(state, session, pkt, chirp::gateway::GET_USER_ROOM_RESP, req.user_id(), &resp).has_value()) {
    return;
  }

  std::string room_id;
  {
    std::lock_guard<std::mutex> lock(state->mu);
    auto it = state->user_to_room.find(req.user_id());
    if (it != state->user_to_room.end()) {
      room_id = it->second;
    }
  }
  if (room_id.empty()) {
    resp.set_code(chirp::common::USER_NOT_FOUND);  // user not in any room
    SendPacket(session, chirp::gateway::GET_USER_ROOM_RESP, pkt.sequence(), resp.SerializeAsString());
    return;
  }

  const auto room = FindRoom(state, room_id);
  if (!room) {
    resp.set_code(chirp::common::USER_NOT_FOUND);
    SendPacket(session, chirp::gateway::GET_USER_ROOM_RESP, pkt.sequence(), resp.SerializeAsString());
    return;
  }

  resp.set_code(chirp::common::OK);
  resp.set_room_id(room_id);
  {
    std::lock_guard<std::mutex> lock(room->mu);
    auto it = room->participants.find(req.user_id());
    if (it != room->participants.end()) {
      *resp.mutable_participant() = it->second;
    }
  }
  SendPacket(session, chirp::gateway::GET_USER_ROOM_RESP, pkt.sequence(), resp.SerializeAsString());
}

void HandleIceCandidate(const std::shared_ptr<VoiceState>& state,
                        [[maybe_unused]] const std::shared_ptr<chirp::network::Session>& session,
                        const chirp::gateway::Packet& pkt) {
  chirp::voice::IceCandidateMessage msg;
  if (!msg.ParseFromArray(pkt.body().data(), static_cast<int>(pkt.body().size()))) {
    return;
  }

  const auto room = FindRoom(state, msg.room_id());
  if (!room) {
    return;
  }

  // Relay the ICE candidate to the targeted participant, or broadcast to all
  // when no target is set (proto: "Empty for broadcast to all").
  if (msg.to_user_id().empty()) {
    BroadcastToRoom(room, chirp::gateway::ICE_CANDIDATE_MSG, pkt.body(), state);
  } else {
    RelayToUser(room, msg.to_user_id(), chirp::gateway::ICE_CANDIDATE_MSG, pkt.body(), state);
  }
}

void HandleSdpOffer(const std::shared_ptr<VoiceState>& state,
                    [[maybe_unused]] const std::shared_ptr<chirp::network::Session>& session,
                    const chirp::gateway::Packet& pkt) {
  chirp::voice::SdpOfferMessage msg;
  if (!msg.ParseFromArray(pkt.body().data(), static_cast<int>(pkt.body().size()))) {
    return;
  }

  const auto room = FindRoom(state, msg.room_id());
  if (!room) {
    return;
  }

  // Relay the SDP offer to the targeted participant, or broadcast to all when
  // no target is set (proto: "Empty for broadcast to all").
  if (msg.to_user_id().empty()) {
    BroadcastToRoom(room, chirp::gateway::SDP_OFFER_MSG, pkt.body(), state);
  } else {
    RelayToUser(room, msg.to_user_id(), chirp::gateway::SDP_OFFER_MSG, pkt.body(), state);
  }
}

// SDP answers relay exactly like offers (targeted via to_user_id, broadcast
// when it is empty); without this the answer side of the mesh handshake had
// no server path at all.
void HandleSdpAnswer(const std::shared_ptr<VoiceState>& state,
                     [[maybe_unused]] const std::shared_ptr<chirp::network::Session>& session,
                     const chirp::gateway::Packet& pkt) {
  chirp::voice::SdpAnswerMessage msg;
  if (!msg.ParseFromArray(pkt.body().data(), static_cast<int>(pkt.body().size()))) {
    return;
  }

  const auto room = FindRoom(state, msg.room_id());
  if (!room) {
    return;
  }

  if (msg.to_user_id().empty()) {
    BroadcastToRoom(room, chirp::gateway::SDP_ANSWER_MSG, pkt.body(), state);
  } else {
    RelayToUser(room, msg.to_user_id(), chirp::gateway::SDP_ANSWER_MSG, pkt.body(), state);
  }
}

void HandleDisconnect(const std::shared_ptr<VoiceState>& state,
                     const std::shared_ptr<chirp::network::Session>& session) {
  std::string user_id;
  std::string room_id;
  {
    std::lock_guard<std::mutex> lock(state->mu);
    auto it = state->session_to_user.find(session.get());
    if (it == state->session_to_user.end()) {
      return;
    }
    user_id = it->second;
    state->session_to_user.erase(it);
    state->authenticated_sessions.erase(session.get());
    state->session_to_last_seen_ms.erase(session.get());

    auto it2 = state->user_to_session.find(user_id);
    if (it2 != state->user_to_session.end()) {
      auto cur = it2->second.lock();
      if (!cur || cur.get() == session.get()) {
        state->user_to_session.erase(it2);
      }
    }

    auto it3 = state->user_to_room.find(user_id);
    if (it3 != state->user_to_room.end()) {
      room_id = it3->second;
      state->user_to_room.erase(it3);
    }
  }

  if (!room_id.empty()) {
    std::shared_ptr<VoiceRoom> room;
    {
      std::lock_guard<std::mutex> lock(state->mu);
      auto it = state->rooms.find(room_id);
      if (it != state->rooms.end()) {
        room = it->second;
      }
    }

    if (room) {
      {
        std::lock_guard<std::mutex> lock(room->mu);
        room->participants.erase(user_id);
      }

      chirp::voice::ParticipantLeftNotify left_notify;
      left_notify.set_room_id(room_id);
      left_notify.set_user_id(user_id);
      left_notify.set_timestamp(NowMs());
      BroadcastToRoom(room, chirp::gateway::PARTICIPANT_LEFT_NOTIFY, left_notify.SerializeAsString(), state, user_id);
    }
  }
}

// Snapshots the idle sessions in one critical section: expired last_seen
// entries (plus any whose weak session already died) are collected/cleaned
// under state->mu, while HandleDisconnect/Close run strictly outside the
// lock. VoiceState is driven from a single io_context today, so the mutexes
// guard against future threading more than current races - keep it that way.
std::vector<std::pair<std::shared_ptr<chirp::network::Session>, std::string>>
CollectTimedOutSessions(const std::shared_ptr<VoiceState>& state, int64_t now_ms) {
  std::vector<std::pair<std::shared_ptr<chirp::network::Session>, std::string>> expired;
  std::lock_guard<std::mutex> lock(state->mu);
  for (auto it = state->session_to_last_seen_ms.begin();
       it != state->session_to_last_seen_ms.end();) {
    const bool timed_out = now_ms - it->second > state->cfg.heartbeat_timeout_ms;
    auto sess_it = state->user_to_session.end();
    // Resolve the session through the bound user (session pointer keys the
    // last_seen map; the strong ref lives in user_to_session).
    const auto user_it = state->session_to_user.find(it->first);
    if (user_it != state->session_to_user.end()) {
      sess_it = state->user_to_session.find(user_it->second);
    }
    std::shared_ptr<chirp::network::Session> sess =
        sess_it != state->user_to_session.end() ? sess_it->second.lock() : nullptr;
    if (!sess) {
      it = state->session_to_last_seen_ms.erase(it);  // session already gone
      continue;
    }
    if (timed_out) {
      expired.emplace_back(std::move(sess), user_it->second);
      it = state->session_to_last_seen_ms.erase(it);
    } else {
      ++it;
    }
  }
  return expired;
}

// Kicks connections that stopped sending anything (heartbeat included) for
// heartbeat_timeout_ms: same path as a socket disconnect (room removal +
// LEFT broadcast), then the session is closed.
void SweepHeartbeatTimeout(const std::shared_ptr<VoiceState>& state, int64_t now_ms) {
  if (state->cfg.heartbeat_timeout_ms <= 0) {
    return;
  }
  using chirp::common::Logger;
  for (auto& expired : CollectTimedOutSessions(state, now_ms)) {
    Logger::Instance().Info("voice session idle timeout: " + expired.second);
    HandleDisconnect(state, expired.first);
    expired.first->Close();
  }
}

void ScheduleHeartbeatSweep(const std::shared_ptr<asio::steady_timer>& timer,
                            const std::shared_ptr<VoiceState>& state) {
  timer->expires_after(std::chrono::seconds(5));
  timer->async_wait([timer, state](const std::error_code& ec) {
    if (ec) {
      return;  // timer cancelled (shutdown) - do not reschedule
    }
    SweepHeartbeatTimeout(state, NowMs());
    ScheduleHeartbeatSweep(timer, state);
  });
}

void HandlePacket(const std::shared_ptr<VoiceState>& state,
                  const std::shared_ptr<chirp::network::Session>& session,
                  std::string&& payload) {
  using chirp::common::Logger;

  chirp::gateway::Packet pkt;
  if (!pkt.ParseFromArray(payload.data(), static_cast<int>(payload.size()))) {
    Logger::Instance().Warn("failed to parse Packet from client");
    return;
  }

  // Any traffic counts as liveness for the idle sweep (heartbeats are the
  // expected cadence, but a client busy with signaling should never be
  // reaped either).
  {
    std::lock_guard<std::mutex> lock(state->mu);
    state->session_to_last_seen_ms[session.get()] = NowMs();
  }

  switch (pkt.msg_id()) {
  case chirp::gateway::LOGIN_REQ:
    HandleLogin(state, session, pkt);
    break;
  case chirp::gateway::CREATE_ROOM_REQ:
    HandleCreateRoom(state, session, pkt);
    break;
  case chirp::gateway::JOIN_ROOM_REQ:
    HandleJoinRoom(state, session, pkt);
    break;
  case chirp::gateway::LEAVE_ROOM_REQ:
    HandleLeaveRoom(state, session, pkt);
    break;
  case chirp::gateway::ICE_CANDIDATE_MSG:
    HandleIceCandidate(state, session, pkt);
    break;
  case chirp::gateway::SDP_OFFER_MSG:
    HandleSdpOffer(state, session, pkt);
    break;
  case chirp::gateway::SDP_ANSWER_MSG:
    HandleSdpAnswer(state, session, pkt);
    break;
  case chirp::gateway::GET_ROOM_INFO_REQ:
    HandleGetRoomInfo(state, session, pkt);
    break;
  case chirp::gateway::GET_USER_ROOM_REQ:
    HandleGetUserRoom(state, session, pkt);
    break;
  case chirp::gateway::SET_MUTE_REQ:
    HandleSetMute(state, session, pkt);
    break;
  case chirp::gateway::SET_DEAFEN_REQ:
    HandleSetDeafen(state, session, pkt);
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
  const uint16_t port = ParseU16Arg(argc, argv, "--port", 9000);
  const uint16_t ws_port = ParseU16Arg(argc, argv, "--ws_port", static_cast<uint16_t>(port + 1));
  const std::string token_secret = GetArg(argc, argv, "--token_secret", "");
  const std::string turn_uri = GetArg(argc, argv, "--turn_uri", "");
  const std::string turn_secret = GetArg(argc, argv, "--turn_static_secret", "");
  const int64_t turn_ttl =
      std::atoll(GetArg(argc, argv, "--turn_credential_ttl_seconds", "86400").c_str());
  const int64_t heartbeat_timeout =
      std::atoll(GetArg(argc, argv, "--heartbeat_timeout_ms", "75000").c_str());

  if (!turn_uri.empty() && turn_secret.empty()) {
    Logger::Instance().Warn(
        "--turn_uri is set without --turn_static_secret: only STUN access will "
        "be advertised (TURN relays need credentials)");
  }

  const chirp::common::LoginTokenVerifier token_verifier(token_secret);

  Logger::Instance().Info(
      "chirp_voice starting tcp=" + std::to_string(port) + " ws=" + std::to_string(ws_port) +
      " auth=" + (token_verifier.enabled() ? "hmac-sha256" : "scaffold") +
      " turn=" + (turn_uri.empty() ? "off" : "on") +
      " heartbeat_timeout=" + std::to_string(heartbeat_timeout) + "ms");

  asio::io_context io;

  auto state = std::make_shared<VoiceState>();
  state->cfg.verifier = &token_verifier;
  state->cfg.turn_uri = turn_uri;
  state->cfg.turn_secret = turn_secret;
  state->cfg.turn_credential_ttl_seconds = turn_ttl;
  state->cfg.heartbeat_timeout_ms = heartbeat_timeout;

  chirp::network::TcpServer server(
      io, port,
      [state](std::shared_ptr<chirp::network::Session> session, std::string&& payload) {
        HandlePacket(state, session, std::move(payload));
      },
      [state](std::shared_ptr<chirp::network::Session> session) { HandleDisconnect(state, session); });

  chirp::network::WebSocketServer ws_server(
      io, ws_port,
      [state](std::shared_ptr<chirp::network::Session> session, std::string&& payload) {
        HandlePacket(state, session, std::move(payload));
      },
      [state](std::shared_ptr<chirp::network::Session> session) { HandleDisconnect(state, session); });

  server.Start();
  ws_server.Start();

  if (state->cfg.heartbeat_timeout_ms > 0) {
    auto sweep_timer = std::make_shared<asio::steady_timer>(io);
    ScheduleHeartbeatSweep(sweep_timer, state);
  }

  asio::signal_set signals(io, SIGINT, SIGTERM);
  signals.async_wait([&](const std::error_code& /*ec*/, int /*sig*/) {
    Logger::Instance().Info("shutdown requested");
    server.Stop();
    ws_server.Stop();
    io.stop();
  });

  io.run();
  Logger::Instance().Info("chirp_voice exited");
  return 0;
}
