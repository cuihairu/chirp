#include <chrono>
#include <cstdint>
#include <cstdlib>
#include <iostream>
#include <string>

#include <asio.hpp>

#include "network/byte_order.h"
#include "network/length_prefixed_framer.h"
#include "network/protobuf_framing.h"
#include "network/websocket_frame.h"
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

bool ReadUntilHttpEnd(asio::ip::tcp::socket& sock, std::string* out, std::string* leftover) {
  out->clear();
  leftover->clear();
  std::array<char, 4096> buf{};
  while (true) {
    auto pos = out->find("\r\n\r\n");
    if (pos != std::string::npos) {
      const size_t end = pos + 4;
      *leftover = out->substr(end);
      out->resize(end);
      return true;
    }

    asio::error_code ec;
    const size_t n = sock.read_some(asio::buffer(buf), ec);
    if (ec) {
      return false;
    }
    out->append(buf.data(), n);
  }
}

} // namespace

int main(int argc, char** argv) {
  const std::string host = GetArg(argc, argv, "--host", "127.0.0.1");
  const uint16_t port = static_cast<uint16_t>(std::atoi(GetArg(argc, argv, "--port", "5001").c_str()));

  asio::io_context io;
  asio::ip::tcp::resolver resolver(io);
  asio::ip::tcp::socket sock(io);

  auto endpoints = resolver.resolve(host, std::to_string(port));
  asio::connect(sock, endpoints);

  const std::string req =
      "GET / HTTP/1.1\r\n"
      "Host: " +
      host + ":" + std::to_string(port) +
      "\r\n"
      "Upgrade: websocket\r\n"
      "Connection: Upgrade\r\n"
      "Sec-WebSocket-Key: dGhlIHNhbXBsZSBub25jZQ==\r\n"
      "Sec-WebSocket-Version: 13\r\n\r\n";

  asio::write(sock, asio::buffer(req));

  std::string resp_headers;
  std::string leftover;
  if (!ReadUntilHttpEnd(sock, &resp_headers, &leftover)) {
    std::cerr << "failed to read ws handshake response\n";
    return 1;
  }

  if (resp_headers.find("101") == std::string::npos) {
    std::cerr << "handshake failed:\n" << resp_headers << "\n";
    return 1;
  }

  chirp::gateway::HeartbeatPing ping;
  ping.set_timestamp(NowMs());

  chirp::gateway::Packet pkt;
  pkt.set_msg_id(chirp::gateway::HEARTBEAT_PING);
  pkt.set_sequence(1);
  pkt.set_body(ping.SerializeAsString());

  auto framed = chirp::network::ProtobufFraming::Encode(pkt);
  const std::string payload(reinterpret_cast<const char*>(framed.data()), framed.size());
  const std::string ws_msg = chirp::network::BuildWebSocketFrame(/*opcode=*/0x2, payload, /*mask=*/true);
  asio::write(sock, asio::buffer(ws_msg));

  chirp::network::WebSocketFrameParser ws_parser;
  if (!leftover.empty()) {
    ws_parser.Append(reinterpret_cast<const uint8_t*>(leftover.data()), leftover.size());
  }

  chirp::network::LengthPrefixedFramer framer;
  std::array<uint8_t, 4096> buf{};

  while (true) {
    while (true) {
      auto f = ws_parser.PopFrame();
      if (!f) {
        break;
      }
      if (f->opcode != 0x2) {
        continue;
      }
      framer.Append(reinterpret_cast<const uint8_t*>(f->payload.data()), f->payload.size());
      auto frame = framer.PopFrame();
      if (!frame) {
        continue;
      }

      chirp::gateway::Packet resp;
      if (!resp.ParseFromArray(frame->data(), static_cast<int>(frame->size()))) {
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

    asio::error_code ec;
    const size_t n = sock.read_some(asio::buffer(buf), ec);
    if (ec) {
      std::cerr << "socket read error\n";
      return 1;
    }
    ws_parser.Append(buf.data(), n);
  }
}

