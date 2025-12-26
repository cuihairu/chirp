#include <chrono>
#include <cstdint>
#include <cstdlib>
#include <iostream>
#include <string>
#include <string_view>

#include <asio.hpp>

#include "network/byte_order.h"
#include "network/protobuf_framing.h"
#include "proto/auth.pb.h"
#include "proto/chat.pb.h"
#include "proto/gateway.pb.h"

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

std::string PrivateChannelId(std::string a, std::string b) {
  if (a <= b) {
    return a + "|" + b;
  }
  return b + "|" + a;
}

} // namespace

int main(int argc, char** argv) {
  const std::string host = GetArg(argc, argv, "--host", "127.0.0.1");
  const uint16_t port = static_cast<uint16_t>(std::atoi(GetArg(argc, argv, "--port", "7000").c_str()));

  const std::string sender = GetArg(argc, argv, "--sender", "user_1");
  const std::string receiver = GetArg(argc, argv, "--receiver", "user_2");
  const std::string text = GetArg(argc, argv, "--text", "hello from chirp_chat_send_client");

  asio::io_context io;
  asio::ip::tcp::resolver resolver(io);
  asio::ip::tcp::socket sock(io);
  asio::connect(sock, resolver.resolve(host, std::to_string(port)));

  // Login to register this connection as sender.
  {
    chirp::auth::LoginRequest req;
    req.set_token(sender);
    req.set_device_id("bench");
    req.set_platform("pc");

    chirp::gateway::Packet resp_pkt;
    if (!SendAndRead(sock, chirp::gateway::LOGIN_REQ, 1, req.SerializeAsString(), &resp_pkt) ||
        resp_pkt.msg_id() != chirp::gateway::LOGIN_RESP) {
      std::cerr << "login failed\n";
      return 1;
    }
  }

  chirp::chat::SendMessageRequest req;
  req.set_sender_id(sender);
  req.set_receiver_id(receiver);
  req.set_channel_type(chirp::chat::PRIVATE);
  req.set_channel_id(PrivateChannelId(sender, receiver));
  req.set_msg_type(chirp::chat::TEXT);
  req.set_content(text);
  req.set_client_timestamp(NowMs());

  chirp::gateway::Packet resp_pkt;
  if (!SendAndRead(sock, chirp::gateway::SEND_MESSAGE_REQ, 2, req.SerializeAsString(), &resp_pkt) ||
      resp_pkt.msg_id() != chirp::gateway::SEND_MESSAGE_RESP) {
    std::cerr << "send message failed\n";
    return 1;
  }

  chirp::chat::SendMessageResponse resp;
  if (!resp.ParseFromArray(resp_pkt.body().data(), static_cast<int>(resp_pkt.body().size()))) {
    std::cerr << "failed to parse SendMessageResponse\n";
    return 1;
  }

  std::cout << "code=" << resp.code() << " message_id=" << resp.message_id()
            << " server_ts=" << resp.server_timestamp() << "\n";
  return 0;
}

