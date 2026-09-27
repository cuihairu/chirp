// One-shot voice-plane smoke client: drives the full room lifecycle over a
// real chirp_voice connection (scaffold login -> create -> join -> room info
// -> mute -> heartbeat -> leave) and asserts every response code on the way.
// Exits 0 only when the whole round-trip checks out; test_services.sh
// --smoke-voice fails loudly otherwise.

#include <chrono>
#include <cstdint>
#include <cstdlib>
#include <iostream>
#include <string>

#include <asio.hpp>

#include "network/byte_order.h"
#include "network/protobuf_framing.h"
#include "proto/auth.pb.h"
#include "proto/common.pb.h"
#include "proto/gateway.pb.h"
#include "proto/voice.pb.h"

namespace {

std::string GetArg(int argc, char** argv, const std::string& key, const std::string& def) {
  for (int i = 1; i < argc; i++) {
    if (argv[i] == key && i + 1 < argc) {
      return argv[i + 1];
    }
  }
  return def;
}

bool ReadFrame(asio::ip::tcp::socket& sock, std::string* payload) {
  uint8_t len_be[4];
  asio::error_code ec;
  asio::read(sock, asio::buffer(len_be, 4), ec);
  if (ec) {
    return false;
  }
  const uint32_t len = chirp::network::ReadU32BE(len_be);
  payload->resize(len);
  asio::read(sock, asio::buffer(payload->data(), payload->size()), ec);
  return !ec;
}

void SendPacket(asio::ip::tcp::socket& sock, chirp::gateway::MsgID msg_id, int64_t seq,
                const std::string& body) {
  chirp::gateway::Packet pkt;
  pkt.set_msg_id(msg_id);
  pkt.set_sequence(seq);
  pkt.set_body(body);
  auto out = chirp::network::ProtobufFraming::Encode(pkt);
  asio::write(sock, asio::buffer(out));
}

// Sends a request and reads frames until the matching RESP arrives; notify
// frames that interleave (e.g. from other participants) are skipped.
template <typename RespT>
bool RoundTrip(asio::ip::tcp::socket& sock, chirp::gateway::MsgID req_id,
               chirp::gateway::MsgID resp_id, int64_t seq, const std::string& body,
               RespT* resp) {
  SendPacket(sock, req_id, seq, body);
  for (;;) {
    std::string payload;
    if (!ReadFrame(sock, &payload)) {
      std::cerr << "connection closed while waiting for msg_id=" << resp_id << "\n";
      return false;
    }
    chirp::gateway::Packet pkt;
    if (!pkt.ParseFromArray(payload.data(), static_cast<int>(payload.size()))) {
      std::cerr << "unparseable Packet frame\n";
      return false;
    }
    if (pkt.msg_id() != resp_id) {
      continue;
    }
    if (!resp->ParseFromArray(pkt.body().data(), static_cast<int>(pkt.body().size()))) {
      std::cerr << "unparseable body for msg_id=" << resp_id << "\n";
      return false;
    }
    return true;
  }
}

#define CHECK(cond)                                                          \
  do {                                                                       \
    if (!(cond)) {                                                           \
      std::cerr << "voice smoke failed at line " << __LINE__ << ": " #cond "\n"; \
      return 1;                                                              \
    }                                                                        \
  } while (0)

int64_t NowMs() {
  using namespace std::chrono;
  return duration_cast<milliseconds>(system_clock::now().time_since_epoch()).count();
}

}  // namespace

int main(int argc, char** argv) {
  const std::string host = GetArg(argc, argv, "--host", "127.0.0.1");
  const std::string port = GetArg(argc, argv, "--port", "9000");
  const std::string user = GetArg(argc, argv, "--user", "voice-smoke-user");

  asio::io_context io;
  asio::ip::tcp::socket sock(io);
  asio::error_code ec;
  sock.connect(asio::ip::tcp::endpoint(asio::ip::make_address_v4(host, ec),
                                       static_cast<uint16_t>(std::atoi(port.c_str()))), ec);
  if (ec) {
    std::cerr << "voice smoke: cannot connect to " << host << ":" << port << ": " << ec.message()
              << "\n";
    return 1;
  }

  // Scaffold login: the token doubles as the user id.
  chirp::auth::LoginRequest login;
  login.set_token(user);
  chirp::auth::LoginResponse login_resp;
  CHECK((RoundTrip(sock, chirp::gateway::LOGIN_REQ, chirp::gateway::LOGIN_RESP, 1,
                   login.SerializeAsString(), &login_resp)));
  CHECK(login_resp.code() == chirp::common::OK);
  CHECK(login_resp.user_id() == user);

  // Create room.
  chirp::voice::CreateRoomRequest create;
  create.set_user_id(user);
  create.set_room_type(chirp::voice::GROUP);
  create.set_room_name("smoke-room");
  create.set_max_participants(5);
  chirp::voice::CreateRoomResponse create_resp;
  CHECK((RoundTrip(sock, chirp::gateway::CREATE_ROOM_REQ, chirp::gateway::CREATE_ROOM_RESP, 2,
                   create.SerializeAsString(), &create_resp)));
  CHECK(create_resp.code() == chirp::common::OK);
  CHECK(!create_resp.room_id().empty());

  // Join: we are the first participant, and the echoed sdp_answer mirrors the
  // scaffold offer (production would answer with real SDP).
  chirp::voice::JoinRoomRequest join;
  join.set_user_id(user);
  join.set_room_id(create_resp.room_id());
  join.set_sdp_offer("smoke-offer");
  chirp::voice::JoinRoomResponse join_resp;
  CHECK((RoundTrip(sock, chirp::gateway::JOIN_ROOM_REQ, chirp::gateway::JOIN_ROOM_RESP, 3,
                   join.SerializeAsString(), &join_resp)));
  CHECK(join_resp.code() == chirp::common::OK);
  CHECK(join_resp.room_id() == create_resp.room_id());
  CHECK(join_resp.sdp_answer() == "smoke-offer");
  bool joined_seen = false;
  for (const auto& pid : join_resp.participant_ids()) {
    joined_seen = joined_seen || pid == user;
  }
  CHECK(joined_seen);

  // Room info: self listed as CONNECTED.
  chirp::voice::GetRoomInfoRequest info;
  info.set_room_id(create_resp.room_id());
  chirp::voice::GetRoomInfoResponse info_resp;
  CHECK((RoundTrip(sock, chirp::gateway::GET_ROOM_INFO_REQ, chirp::gateway::GET_ROOM_INFO_RESP, 4,
                   info.SerializeAsString(), &info_resp)));
  CHECK(info_resp.code() == chirp::common::OK);
  CHECK(info_resp.participants_size() == 1);
  CHECK(info_resp.participants(0).user_id() == user);
  CHECK(info_resp.participants(0).state() == chirp::voice::CONNECTED);

  // Mute: derived participant state flips to MUTED.
  chirp::voice::SetMuteRequest mute;
  mute.set_user_id(user);
  mute.set_room_id(create_resp.room_id());
  mute.set_muted(true);
  chirp::voice::SetMuteResponse mute_resp;
  CHECK((RoundTrip(sock, chirp::gateway::SET_MUTE_REQ, chirp::gateway::SET_MUTE_RESP, 5,
                   mute.SerializeAsString(), &mute_resp)));
  CHECK(mute_resp.code() == chirp::common::OK);
  CHECK((RoundTrip(sock, chirp::gateway::GET_ROOM_INFO_REQ, chirp::gateway::GET_ROOM_INFO_RESP, 6,
                   info.SerializeAsString(), &info_resp)));
  CHECK(info_resp.participants(0).muted());
  CHECK(info_resp.participants(0).state() == chirp::voice::MUTED);

  // Heartbeat: pong echoes our timestamp.
  chirp::gateway::HeartbeatPing ping;
  ping.set_timestamp(NowMs());
  chirp::gateway::HeartbeatPong pong;
  CHECK((RoundTrip(sock, chirp::gateway::HEARTBEAT_PING, chirp::gateway::HEARTBEAT_PONG, 7,
                   ping.SerializeAsString(), &pong)));
  CHECK(pong.timestamp() == ping.timestamp());

  // Leave: idempotent-clean exit.
  chirp::voice::LeaveRoomRequest leave;
  leave.set_user_id(user);
  leave.set_room_id(create_resp.room_id());
  chirp::voice::LeaveRoomResponse leave_resp;
  CHECK((RoundTrip(sock, chirp::gateway::LEAVE_ROOM_REQ, chirp::gateway::LEAVE_ROOM_RESP, 8,
                   leave.SerializeAsString(), &leave_resp)));
  CHECK(leave_resp.code() == chirp::common::OK);

  std::cout << "voice smoke: full lifecycle OK (user=" << user << ")\n";
  return 0;
}
