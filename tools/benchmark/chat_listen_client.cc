#include <cstdint>
#include <cstdlib>
#include <iostream>
#include <string>

#include <asio.hpp>

#include "network/byte_order.h"
#include "network/protobuf_framing.h"
#include "proto/auth.pb.h"
#include "proto/chat.pb.h"
#include "proto/gateway.pb.h"

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

void SendPacket(asio::ip::tcp::socket& sock, chirp::gateway::MsgID msg_id, int64_t seq, const std::string& body) {
  chirp::gateway::Packet pkt;
  pkt.set_msg_id(msg_id);
  pkt.set_sequence(seq);
  pkt.set_body(body);
  auto out = chirp::network::ProtobufFraming::Encode(pkt);
  asio::write(sock, asio::buffer(out));
}

} // namespace

int main(int argc, char** argv) {
  const std::string host = GetArg(argc, argv, "--host", "127.0.0.1");
  const uint16_t port = static_cast<uint16_t>(std::atoi(GetArg(argc, argv, "--port", "7000").c_str()));
  const std::string user = GetArg(argc, argv, "--user", "user_2");
  const int max_msgs = std::atoi(GetArg(argc, argv, "--max", "1").c_str());

  asio::io_context io;
  asio::ip::tcp::resolver resolver(io);
  asio::ip::tcp::socket sock(io);
  asio::connect(sock, resolver.resolve(host, std::to_string(port)));

  // Login to register this connection.
  {
    chirp::auth::LoginRequest req;
    req.set_token(user);
    req.set_device_id("bench");
    req.set_platform("pc");
    SendPacket(sock, chirp::gateway::LOGIN_REQ, 1, req.SerializeAsString());

    std::string payload;
    if (!ReadFrame(sock, &payload)) {
      std::cerr << "failed to read login response\n";
      return 1;
    }
  }

  int got = 0;
  while (max_msgs <= 0 || got < max_msgs) {
    std::string payload;
    if (!ReadFrame(sock, &payload)) {
      std::cerr << "socket closed\n";
      return 1;
    }

    chirp::gateway::Packet pkt;
    if (!pkt.ParseFromArray(payload.data(), static_cast<int>(payload.size()))) {
      continue;
    }

    if (pkt.msg_id() == chirp::gateway::CHAT_MESSAGE_NOTIFY) {
      chirp::chat::ChatMessage msg;
      if (msg.ParseFromArray(pkt.body().data(), static_cast<int>(pkt.body().size()))) {
        std::cout << "notify ts=" << msg.timestamp() << " " << msg.sender_id() << " -> " << msg.receiver_id()
                  << " id=" << msg.message_id() << " bytes=" << msg.content().size() << "\n";
        got++;
      }
    }
  }

  return 0;
}

