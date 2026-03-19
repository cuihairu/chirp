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
#include "network/protobuf_framing.h"
#include "network/session.h"
#include "network/tcp_server.h"
#include "network/websocket_server.h"
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

    // Remove from previous room if any
    auto prev_room_it = state->user_to_room.find(req.user_id());
    if (prev_room_it != state->user_to_room.end()) {
      auto prev_room_it2 = state->rooms.find(prev_room_it->second);
      if (prev_room_it2 != state->rooms.end()) {
        std::lock_guard<std::mutex> room_lock(prev_room_it2->second->mu);
        prev_room_it2->second->participants.erase(req.user_id());
      }
    }
    state->user_to_room[req.user_id()] = req.room_id();
  }

  // Add participant to room
  std::vector<std::string> existing_participants;
  {
    std::lock_guard<std::mutex> lock(room->mu);
    if (room->max_participants > 0 && static_cast<int32_t>(room->participants.size()) >= room->max_participants) {
      chirp::voice::JoinRoomResponse resp;
      resp.set_code(chirp::common::INTERNAL_ERROR);  // Room full
      resp.set_server_time(NowMs());
      SendPacket(session, chirp::gateway::JOIN_ROOM_RESP, pkt.sequence(), resp.SerializeAsString());
      return;
    }

    chirp::voice::ParticipantInfo participant;
    participant.set_user_id(req.user_id());
    participant.set_state(chirp::voice::CONNECTED);
    participant.set_joined_at(NowMs());
    room->participants[req.user_id()] = participant;

    for (const auto& kv : room->participants) {
      existing_participants.push_back(kv.first);
    }
  }

  // Notify existing participants
  chirp::voice::ParticipantJoinedNotify joined_notify;
  joined_notify.set_room_id(req.room_id());
  joined_notify.mutable_participant()->set_user_id(req.user_id());
  joined_notify.mutable_participant()->set_state(chirp::voice::CONNECTED);
  joined_notify.mutable_participant()->set_joined_at(NowMs());
  joined_notify.set_timestamp(NowMs());
  BroadcastToRoom(room, chirp::gateway::PARTICIPANT_JOINED_NOTIFY, joined_notify.SerializeAsString(), state, req.user_id());

  chirp::voice::JoinRoomResponse resp;
  resp.set_code(chirp::common::OK);
  resp.set_room_id(req.room_id());
  resp.set_sdp_answer(req.sdp_offer());  // In production, this would be actual SDP answer
  for (const auto& pid : existing_participants) {
    resp.add_participant_ids(pid);
  }
  resp.set_server_time(NowMs());
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

void HandleIceCandidate(const std::shared_ptr<VoiceState>& state,
                        const std::shared_ptr<chirp::network::Session>& session,
                        const chirp::gateway::Packet& pkt) {
  chirp::voice::IceCandidateMessage msg;
  if (!msg.ParseFromArray(pkt.body().data(), static_cast<int>(pkt.body().size()))) {
    return;
  }

  std::shared_ptr<VoiceRoom> room;
  {
    std::lock_guard<std::mutex> lock(state->mu);
    auto it = state->rooms.find(msg.room_id());
    if (it == state->rooms.end()) {
      return;
    }
    room = it->second;
  }

  // Relay ICE candidate to target or all participants
  BroadcastToRoom(room, chirp::gateway::ICE_CANDIDATE_MSG, pkt.body(), state,
                  msg.to_user_id().empty() ? "" : msg.from_user_id());
}

void HandleSdpOffer(const std::shared_ptr<VoiceState>& state,
                    const std::shared_ptr<chirp::network::Session>& session,
                    const chirp::gateway::Packet& pkt) {
  chirp::voice::SdpOfferMessage msg;
  if (!msg.ParseFromArray(pkt.body().data(), static_cast<int>(pkt.body().size()))) {
    return;
  }

  std::shared_ptr<VoiceRoom> room;
  {
    std::lock_guard<std::mutex> lock(state->mu);
    auto it = state->rooms.find(msg.room_id());
    if (it == state->rooms.end()) {
      return;
    }
    room = it->second;
  }

  // Relay SDP offer to target user
  BroadcastToRoom(room, chirp::gateway::SDP_OFFER_MSG, pkt.body(), state, msg.from_user_id());
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

void HandlePacket(const std::shared_ptr<VoiceState>& state,
                  const std::shared_ptr<chirp::network::Session>& session,
                  std::string&& payload) {
  using chirp::common::Logger;

  chirp::gateway::Packet pkt;
  if (!pkt.ParseFromArray(payload.data(), static_cast<int>(payload.size()))) {
    Logger::Instance().Warn("failed to parse Packet from client");
    return;
  }

  switch (pkt.msg_id()) {
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

  Logger::Instance().Info("chirp_voice starting tcp=" + std::to_string(port) + " ws=" + std::to_string(ws_port));

  asio::io_context io;

  auto state = std::make_shared<VoiceState>();

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
