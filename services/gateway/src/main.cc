#include <chrono>
#include <cstdint>
#include <cstdlib>
#include <string>

#include <asio.hpp>

#include "common/logger.h"
#include "network/protobuf_framing.h"
#include "network/tcp_server.h"
#include "proto/gateway.pb.h"

namespace {

int64_t NowMs() {
  using namespace std::chrono;
  return duration_cast<milliseconds>(system_clock::now().time_since_epoch()).count();
}

uint16_t ParsePort(int argc, char** argv) {
  uint16_t port = 5000;
  for (int i = 1; i < argc; i++) {
    std::string a = argv[i];
    if ((a == "--port" || a == "-p") && i + 1 < argc) {
      port = static_cast<uint16_t>(std::atoi(argv[i + 1]));
      i++;
    }
  }
  return port;
}

} // namespace

int main(int argc, char** argv) {
  using chirp::common::Logger;

  Logger::Instance().SetLevel(Logger::Level::kInfo);
  const uint16_t port = ParsePort(argc, argv);
  Logger::Instance().Info("chirp_gateway starting on port " + std::to_string(port));

  asio::io_context io;

  chirp::network::TcpServer server(
      io, port,
      [](std::shared_ptr<chirp::network::TcpSession> session, std::string&& payload) {
        chirp::gateway::Packet pkt;
        if (!pkt.ParseFromArray(payload.data(), static_cast<int>(payload.size()))) {
          Logger::Instance().Warn("failed to parse Packet from client");
          return;
        }

        switch (pkt.msg_id()) {
        case chirp::gateway::HEARTBEAT_PING: {
          chirp::gateway::HeartbeatPing ping;
          if (!ping.ParseFromArray(pkt.body().data(), static_cast<int>(pkt.body().size()))) {
            Logger::Instance().Warn("failed to parse HeartbeatPing body");
            return;
          }

          chirp::gateway::HeartbeatPong pong;
          pong.set_timestamp(ping.timestamp());
          pong.set_server_time(NowMs());

          chirp::gateway::Packet resp;
          resp.set_msg_id(chirp::gateway::HEARTBEAT_PONG);
          resp.set_sequence(pkt.sequence());
          resp.set_body(pong.SerializeAsString());

          auto framed = chirp::network::ProtobufFraming::Encode(resp);
          session->Send(std::string(reinterpret_cast<const char*>(framed.data()), framed.size()));
          break;
        }
        default:
          // For scaffolding: ignore unknown/unimplemented messages.
          break;
        }
      });

  server.Start();

  asio::signal_set signals(io, SIGINT, SIGTERM);
  signals.async_wait([&](const std::error_code& /*ec*/, int /*sig*/) {
    Logger::Instance().Info("shutdown requested");
    server.Stop();
    io.stop();
  });

  io.run();
  Logger::Instance().Info("chirp_gateway exited");
  return 0;
}
