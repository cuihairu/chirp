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

bool SendAndRead(asio::ip::tcp::socket& sock,
                 chirp::gateway::MsgID msg_id,
                 int64_t seq,
                 const std::string& body,
                 chirp::gateway::Packet* out_pkt) {
  chirp::gateway::Packet pkt;
  pkt.set_msg_id(msg_id);
  pkt.set_sequence(seq);
  pkt.set_body(body);
  auto out = chirp::network::ProtobufFraming::Encode(pkt);
  asio::write(sock, asio::buffer(out));

  std::string payload;
  if (!ReadFrame(sock, &payload)) {
    return false;
  }
  return out_pkt->ParseFromArray(payload.data(), static_cast<int>(payload.size()));
}

} // namespace

int main(int argc, char** argv) {
  const std::string host = GetArg(argc, argv, "--host", "127.0.0.1");
  const uint16_t port = static_cast<uint16_t>(std::atoi(GetArg(argc, argv, "--port", "7000").c_str()));
  const std::string user = GetArg(argc, argv, "--user", "user_1");

  const int channel_type = std::atoi(GetArg(argc, argv, "--channel_type", "0").c_str());
  const std::string channel_id = GetArg(argc, argv, "--channel_id", "");
  const int32_t limit = static_cast<int32_t>(std::atoi(GetArg(argc, argv, "--limit", "50").c_str()));
  const int64_t before = std::atoll(GetArg(argc, argv, "--before", "0").c_str());

  if (channel_id.empty()) {
    std::cerr << "--channel_id is required (for PRIVATE use 'a|b')\n";
    return 2;
  }

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

    chirp::gateway::Packet resp_pkt;
    if (!SendAndRead(sock, chirp::gateway::LOGIN_REQ, 1, req.SerializeAsString(), &resp_pkt) ||
        resp_pkt.msg_id() != chirp::gateway::LOGIN_RESP) {
      std::cerr << "login failed\n";
      return 1;
    }
  }

  chirp::chat::GetHistoryRequest req;
  req.set_user_id(user);
  req.set_channel_type(static_cast<chirp::chat::ChannelType>(channel_type));
  req.set_channel_id(channel_id);
  req.set_before_timestamp(before);
  req.set_limit(limit);

  chirp::gateway::Packet resp_pkt;
  if (!SendAndRead(sock, chirp::gateway::GET_HISTORY_REQ, 2, req.SerializeAsString(), &resp_pkt) ||
      resp_pkt.msg_id() != chirp::gateway::GET_HISTORY_RESP) {
    std::cerr << "get history failed\n";
    return 1;
  }

  chirp::chat::GetHistoryResponse resp;
  if (!resp.ParseFromArray(resp_pkt.body().data(), static_cast<int>(resp_pkt.body().size()))) {
    std::cerr << "failed to parse GetHistoryResponse\n";
    return 1;
  }

  std::cout << "code=" << resp.code() << " messages=" << resp.messages_size() << " has_more=" << resp.has_more()
            << "\n";
  for (const auto& m : resp.messages()) {
    std::cout << m.timestamp() << " " << m.sender_id() << " -> " << m.receiver_id() << " id=" << m.message_id()
              << " bytes=" << m.content().size() << "\n";
  }
  return 0;
}

