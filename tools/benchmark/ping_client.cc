#include <chrono>
#include <cstdint>
#include <cstdlib>
#include <iostream>
#include <string>

#include <asio.hpp>

#include "network/byte_order.h"
#include "network/protobuf_framing.h"
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

} // namespace

int main(int argc, char** argv) {
  const std::string host = GetArg(argc, argv, "--host", "127.0.0.1");
  const uint16_t port = static_cast<uint16_t>(std::atoi(GetArg(argc, argv, "--port", "5000").c_str()));

  asio::io_context io;
  asio::ip::tcp::resolver resolver(io);
  asio::ip::tcp::socket sock(io);

  auto endpoints = resolver.resolve(host, std::to_string(port));
  asio::connect(sock, endpoints);

  chirp::gateway::HeartbeatPing ping;
  ping.set_timestamp(NowMs());

  chirp::gateway::Packet pkt;
  pkt.set_msg_id(chirp::gateway::HEARTBEAT_PING);
  pkt.set_sequence(1);
  pkt.set_body(ping.SerializeAsString());

  auto out = chirp::network::ProtobufFraming::Encode(pkt);
  asio::write(sock, asio::buffer(out));

  uint8_t len_be[4];
  asio::read(sock, asio::buffer(len_be, 4));
  const uint32_t len = chirp::network::ReadU32BE(len_be);

  std::string payload;
  payload.resize(len);
  asio::read(sock, asio::buffer(payload.data(), payload.size()));

  chirp::gateway::Packet resp;
  if (!resp.ParseFromArray(payload.data(), static_cast<int>(payload.size()))) {
    std::cerr << "failed to parse response Packet\n";
    return 1;
  }

  std::cout << "resp msg_id=" << resp.msg_id() << " seq=" << resp.sequence() << "\n";
  if (resp.msg_id() == chirp::gateway::HEARTBEAT_PONG) {
    chirp::gateway::HeartbeatPong pong;
    if (pong.ParseFromArray(resp.body().data(), static_cast<int>(resp.body().size()))) {
      std::cout << "pong timestamp=" << pong.timestamp() << " server_time=" << pong.server_time() << "\n";
    }
  }

  return 0;
}

