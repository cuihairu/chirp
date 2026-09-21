#include <chrono>
#include <functional>
#include <iostream>
#include <memory>
#include <string>
#include <utility>

#include <asio.hpp>

#include "logger.h"
#include "http_push_transport.h"
#include "network/protobuf_framing.h"
#include "network/tcp_server.h"
#include "network/websocket_server.h"
#include "notification_handlers.h"
#include "notification_service.h"
#include "proto/gateway.pb.h"

using namespace chirp;

namespace {

void SendPacket(const std::shared_ptr<network::Session>& session,
                chirp::gateway::Packet resp) {
  auto framed = network::ProtobufFraming::Encode(resp);
  session->Send(std::string(reinterpret_cast<const char*>(framed.data()), framed.size()));
}

}  // namespace

int main(int argc, char* argv[]) {
  auto& logger = common::Logger::Instance();
  logger.SetLevel(common::Logger::Level::kInfo);

  logger.Info("Chirp Notification Service starting...");

  // Parse command line arguments
  std::string host = "0.0.0.0";
  uint16_t port = 5006;
  uint16_t ws_port = 5016;  // 5007 is taken by the search service
  std::string fcm_server_key;
  std::string apns_key_path;
  std::string apns_key_id;
  std::string apns_team_id;
  std::string apns_bundle_id;
  bool apns_sandbox = false;

  for (int i = 1; i < argc; ++i) {
    std::string arg = argv[i];

    if (arg == "--host" && i + 1 < argc) {
      host = argv[++i];
    } else if (arg == "--port" && i + 1 < argc) {
      port = static_cast<uint16_t>(std::stoi(argv[++i]));
    } else if (arg == "--ws_port" && i + 1 < argc) {
      ws_port = static_cast<uint16_t>(std::stoi(argv[++i]));
    } else if (arg == "--fcm-key" && i + 1 < argc) {
      fcm_server_key = argv[++i];
    } else if (arg == "--apns-key" && i + 1 < argc) {
      apns_key_path = argv[++i];
    } else if (arg == "--apns-key-id" && i + 1 < argc) {
      apns_key_id = argv[++i];
    } else if (arg == "--apns-team-id" && i + 1 < argc) {
      apns_team_id = argv[++i];
    } else if (arg == "--apns-bundle-id" && i + 1 < argc) {
      apns_bundle_id = argv[++i];
    } else if (arg == "--apns-sandbox") {
      apns_sandbox = true;
    } else if (arg == "--help") {
      std::cout << "Usage: " << argv[0] << " [options]\n"
                << "Options:\n"
                << "  --host <address>    Server host (default: 0.0.0.0)\n"
                << "  --port <port>       TCP port (default: 5006)\n"
                << "  --ws_port <port>    WebSocket port (default: 5016)\n"
                << "  --fcm-key <key>     FCM server key\n"
                << "  --apns-key <path>   APNs private key path\n"
                << "  --apns-key-id <id>  APNs key ID\n"
                << "  --apns-team-id <id> APNs team ID\n"
                << "  --apns-bundle-id <id> APNs bundle ID\n"
                << "  --apns-sandbox      Use APNs sandbox\n"
                << "  --help              Show this help\n";
      return 0;
    }
  }

  // Configure notification service
  app_notification::FCMConfig fcm_config;
  fcm_config.server_key = fcm_server_key;

  app_notification::APNsConfig apns_config;
  apns_config.key_id = apns_key_id;
  apns_config.team_id = apns_team_id;
  apns_config.bundle_id = apns_bundle_id;
  apns_config.private_key_path = apns_key_path;
  apns_config.use_sandbox = apns_sandbox;

  // Transport selection: "logging" keeps the stub-era drop-and-log behavior;
  // "http" performs real HTTP/1.1 provider posts (plain TCP here — providers
  // require TLS/HTTP2 in front of or beyond this seam).
  std::string push_transport_flag = "logging";
  for (int i = 1; i < argc; ++i) {
    if (std::string(argv[i]) == "--push_transport" && i + 1 < argc) {
      push_transport_flag = argv[++i];
    }
  }
  std::shared_ptr<app_notification::PushTransport> transport =
      std::make_shared<app_notification::LoggingPushTransport>();
  if (push_transport_flag == "http") {
    transport = std::make_shared<app_notification::HttpPushTransport>(
        std::make_shared<app_notification::TcpHttpConnectionFactory>());
  } else if (push_transport_flag != "logging") {
    logger.Warn("unknown --push_transport '" + push_transport_flag +
                "', using logging");
  }

  auto service = std::make_shared<app_notification::NotificationService>(
      fcm_config, apns_config, transport);
  app_notification::NotificationHandlers handlers(*service);

  asio::io_context io;

  auto on_frame = [handlers, &logger](std::shared_ptr<network::Session> session,
                                      std::string&& payload) mutable {
    gateway::Packet pkt;
    if (!pkt.ParseFromArray(payload.data(), static_cast<int>(payload.size()))) {
      logger.Warn("failed to parse Packet on the notification plane");
      return;
    }
    gateway::Packet resp;
    if (handlers.HandlePacket(pkt, &resp)) {
      SendPacket(session, std::move(resp));
    }
  };

  network::TcpServer server(io, port, on_frame);
  network::WebSocketServer ws_server(io, ws_port, on_frame);
  server.Start();
  ws_server.Start();

  // Periodic device/cooldown cleanup on the io thread.
  asio::steady_timer cleanup_timer(io);
  std::function<void()> schedule_cleanup = [&]() {
    cleanup_timer.expires_after(std::chrono::minutes(5));
    cleanup_timer.async_wait([&](const asio::error_code& ec) {
      if (ec) {
        return;
      }
      service->CleanupInactiveDevices();
      service->CleanupExpiredCooldowns();
      schedule_cleanup();
    });
  };
  schedule_cleanup();

  asio::signal_set signals(io, SIGINT, SIGTERM);
  signals.async_wait([&](const std::error_code& /*ec*/, int /*sig*/) {
    logger.Info("shutdown requested");
    server.Stop();
    ws_server.Stop();
    io.stop();
  });

  logger.Info("Notification service listening on " + host + " tcp/" + std::to_string(port) +
              " ws/" + std::to_string(ws_port));
  logger.Info("FCM configured: " + std::string(fcm_server_key.empty() ? "no" : "yes"));
  logger.Info("APNs configured: " + std::string(apns_key_id.empty() ? "no" : "yes"));

  io.run();

  // Print stats
  const auto& stats = service->GetStats();
  logger.Info("Statistics:");
  logger.Info("  Notifications sent: " + std::to_string(stats.notifications_sent.load()));
  logger.Info("  Notifications failed: " + std::to_string(stats.notifications_failed.load()));
  logger.Info("  Devices registered: " + std::to_string(stats.devices_registered.load()));
  logger.Info("  FCM sent: " + std::to_string(stats.fcm_sent.load()));
  logger.Info("  APNs sent: " + std::to_string(stats.apns_sent.load()));

  logger.Info("Notification service stopped.");

  return 0;
}
