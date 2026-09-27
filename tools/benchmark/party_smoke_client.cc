// One-shot party-plane smoke client: scaffold login -> create party -> get
// my party -> invite an offline target -> leave, asserting every response
// code. Runs against a real chirp_party (scaffold mode, in-memory state, no
// Redis needed); test_services.sh --smoke-party fails loudly if any step
// breaks.

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
#include "proto/party.pb.h"

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
      std::cerr << "party smoke failed at line " << __LINE__ << ": " #cond "\n"; \
      return 1;                                                              \
    }                                                                        \
  } while (0)

}  // namespace

int main(int argc, char** argv) {
  const std::string host = GetArg(argc, argv, "--host", "127.0.0.1");
  const std::string port = GetArg(argc, argv, "--port", "7500");
  const std::string user = GetArg(argc, argv, "--user", "party-smoke-user");

  asio::io_context io;
  asio::ip::tcp::socket sock(io);
  asio::error_code ec;
  sock.connect(asio::ip::tcp::endpoint(asio::ip::make_address_v4(host, ec),
                                       static_cast<uint16_t>(std::atoi(port.c_str()))), ec);
  if (ec) {
    std::cerr << "party smoke: cannot connect to " << host << ":" << port << ": " << ec.message()
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

  // Create: we lead a fresh party.
  chirp::party::CreatePartyRequest create;
  create.set_user_id(user);
  create.set_max_members(5);
  chirp::party::CreatePartyResponse create_resp;
  CHECK((RoundTrip(sock, chirp::gateway::CREATE_PARTY_REQ, chirp::gateway::CREATE_PARTY_RESP, 2,
                   create.SerializeAsString(), &create_resp)));
  CHECK(create_resp.code() == chirp::common::OK);
  CHECK(create_resp.party().party_id() != "");
  CHECK(create_resp.party().leader_id() == user);
  const std::string party_id = create_resp.party().party_id();

  // Snapshot query echoes the same membership.
  chirp::party::GetMyPartyRequest mine;
  mine.set_user_id(user);
  chirp::party::GetMyPartyResponse mine_resp;
  CHECK((RoundTrip(sock, chirp::gateway::GET_MY_PARTY_REQ, chirp::gateway::GET_MY_PARTY_RESP, 3,
                   mine.SerializeAsString(), &mine_resp)));
  CHECK(mine_resp.code() == chirp::common::OK);
  CHECK(mine_resp.party().party_id() == party_id);
  bool member_seen = false;
  for (const auto& member : mine_resp.party().members()) {
    member_seen = member_seen || member.user_id() == user;
  }
  CHECK(member_seen);

  // Invite an offline target: the invite is recorded (id comes back), the
  // notify simply has no session to land on.
  chirp::party::InviteToPartyRequest invite;
  invite.set_user_id(user);
  invite.set_party_id(party_id);
  invite.set_target_user_id("party-smoke-guest");
  chirp::party::InviteToPartyResponse invite_resp;
  CHECK((RoundTrip(sock, chirp::gateway::INVITE_TO_PARTY_REQ, chirp::gateway::INVITE_TO_PARTY_RESP,
                   4, invite.SerializeAsString(), &invite_resp)));
  CHECK(invite_resp.code() == chirp::common::OK);
  CHECK(!invite_resp.invite_id().empty());

  // Leave: last member leaving disbands silently.
  chirp::party::LeavePartyRequest leave;
  leave.set_user_id(user);
  leave.set_party_id(party_id);
  chirp::party::LeavePartyResponse leave_resp;
  CHECK((RoundTrip(sock, chirp::gateway::LEAVE_PARTY_REQ, chirp::gateway::LEAVE_PARTY_RESP, 5,
                   leave.SerializeAsString(), &leave_resp)));
  CHECK(leave_resp.code() == chirp::common::OK);

  std::cout << "party smoke: full lifecycle OK (user=" << user << " party=" << party_id << ")\n";
  return 0;
}
