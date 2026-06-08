#include "distributed_runtime.h"

#include "logger.h"

namespace chirp::chat::runtime {

namespace {

void DispatchPacket(std::string&& payload,
                    const std::shared_ptr<network::Session>& session,
                    const PacketHandler& on_packet,
                    bool log_parse_failure) {
  gateway::Packet pkt;
  if (!pkt.ParseFromArray(payload.data(), static_cast<int>(payload.size()))) {
    if (log_parse_failure) {
      common::Logger::Instance().Warn("Failed to parse packet");
    }
    return;
  }
  on_packet(session, pkt);
}

}  // namespace

std::unique_ptr<network::TcpServer> MakeDistributedTcpServer(asio::io_context& io,
                                                             uint16_t port,
                                                             PacketHandler on_packet,
                                                             DisconnectHandler on_disconnect) {
  return std::make_unique<network::TcpServer>(
      io, port,
      [on_packet = std::move(on_packet)](std::shared_ptr<network::Session> session, std::string&& payload) {
        DispatchPacket(std::move(payload), session, on_packet, true);
      },
      [on_disconnect = std::move(on_disconnect)](std::shared_ptr<network::Session> session) {
        on_disconnect(session);
      });
}

std::unique_ptr<network::WebSocketServer> MakeDistributedWsServer(asio::io_context& io,
                                                                  uint16_t port,
                                                                  PacketHandler on_packet,
                                                                  DisconnectHandler on_disconnect) {
  return std::make_unique<network::WebSocketServer>(
      io, port,
      [on_packet = std::move(on_packet)](std::shared_ptr<network::Session> session, std::string&& payload) {
        DispatchPacket(std::move(payload), session, on_packet, false);
      },
      [on_disconnect = std::move(on_disconnect)](std::shared_ptr<network::Session> session) {
        on_disconnect(session);
      });
}

void InstallSignalStop(asio::io_context& io, ShutdownHandler on_shutdown) {
  auto signals = std::make_shared<asio::signal_set>(io, SIGINT, SIGTERM);
  signals->async_wait([signals, on_shutdown = std::move(on_shutdown)](const std::error_code&, int) mutable {
    on_shutdown();
  });
}

}  // namespace chirp::chat::runtime
