#include <chrono>
#include <functional>
#include <iostream>
#include <memory>
#include <optional>
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

#include "common/metrics.h"
#include "common/metrics_http_server.h"
#include "common/crash_handler.h"

using namespace chirp;

namespace {

void SendPacket(const std::shared_ptr<network::Session>& session,
                chirp::gateway::Packet resp) {
  auto framed = network::ProtobufFraming::Encode(resp);
  session->Send(std::string(reinterpret_cast<const char*>(framed.data()), framed.size()));
}

// Dispatch body of the on_frame callback wired into both listeners below,
// hoisted verbatim (the only change: the logger reference becomes the
// singleton it aliases) so tests can drive the notification plane directly.
void HandleNotificationPacket(app_notification::NotificationHandlers& handlers,
                              const std::shared_ptr<network::Session>& session,
                              std::string&& payload) {
  gateway::Packet pkt;
  if (!pkt.ParseFromArray(payload.data(), static_cast<int>(payload.size()))) {
    chirp::common::Logger::Instance().Warn(
        "failed to parse Packet on the notification plane");
    return;
  }
  CHIRP_COUNTER("chirp_app_notification_packets_total", 1);
  gateway::Packet resp;
  if (handlers.HandlePacket(pkt, &resp)) {
    SendPacket(session, std::move(resp));
  }
}

}  // namespace

int main(int argc, char* argv[]) {
  // 崩溃采集:main 首条语句,早于一切 flag/日志初始化(见 CRASH_COLLECTION.md)。
  chirp::common::crash::Initialize(argc, argv);
  auto& logger = common::Logger::Instance();
  logger.SetLevel(common::Logger::Level::kInfo);

  logger.Info("Chirp Notification Service starting...");

  // Parse command line arguments
  std::string host = "0.0.0.0";
  uint16_t port = 5006;
  uint16_t ws_port = 5016;  // 5007 is taken by the search service
  uint16_t metrics_port = 0;  // 0 = no metrics listener (opt-in)
  std::string fcm_server_key;
  std::string apns_key_path;
  std::string apns_key_id;
  std::string apns_team_id;
  std::string apns_bundle_id;
  bool apns_sandbox = false;
  std::string fcm_endpoint;
  std::string apns_endpoint;
  std::string push_ca_file;
  bool push_verify_tls = true;

  for (int i = 1; i < argc; ++i) {
    std::string arg = argv[i];

    if (arg == "--host" && i + 1 < argc) {
      host = argv[++i];
    } else if (arg == "--port" && i + 1 < argc) {
      port = static_cast<uint16_t>(std::stoi(argv[++i]));
    } else if (arg == "--ws_port" && i + 1 < argc) {
      ws_port = static_cast<uint16_t>(std::stoi(argv[++i]));
    } else if (arg == "--metrics_port" && i + 1 < argc) {
      metrics_port = static_cast<uint16_t>(std::stoi(argv[++i]));
    } else if (arg == "--fcm-key" && i + 1 < argc) {
      fcm_server_key = argv[++i];
    } else if (arg == "--fcm-endpoint" && i + 1 < argc) {
      fcm_endpoint = argv[++i];
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
    } else if (arg == "--apns-endpoint" && i + 1 < argc) {
      apns_endpoint = argv[++i];
    } else if (arg == "--push_ca_file" && i + 1 < argc) {
      push_ca_file = argv[++i];
    } else if (arg == "--push_verify_tls" && i + 1 < argc) {
      push_verify_tls = std::string(argv[++i]) != "off";
    } else if (arg == "--help") {
      std::cout << "Usage: " << argv[0] << " [options]\n"
                << "Options:\n"
                << "  --host <address>    Server host (default: 0.0.0.0)\n"
                << "  --port <port>       TCP port (default: 5006)\n"
                << "  --ws_port <port>    WebSocket port (default: 5016)\n"
                << "  --fcm-key <key>     FCM server key\n"
                << "  --fcm-endpoint <url>   FCM provider endpoint\n"
                << "                         (default: https://fcm.googleapis.com/fcm/send)\n"
                << "  --apns-key <path>   APNs private key path\n"
                << "  --apns-key-id <id>  APNs key ID\n"
                << "  --apns-team-id <id> APNs team ID\n"
                << "  --apns-bundle-id <id> APNs bundle ID\n"
                << "  --apns-sandbox      Use APNs sandbox\n"
                << "  --apns-endpoint <url>  APNs provider endpoint\n"
                << "                         (default: production, or the sandbox\n"
                << "                         host when --apns-sandbox is set)\n"
                << "  --push_transport <m>  push backend: logging (default) or http\n"
                << "  --push_ca_file <path> CA bundle for the http push transport\n"
                << "                        (default: system trust store)\n"
                << "  --push_verify_tls on|off  certificate verification for the http\n"
                << "                        push transport (default: on)\n"
                << "  --help              Show this help\n";
      return 0;
    }
  }

  // Configure notification service
  app_notification::FCMConfig fcm_config;
  fcm_config.server_key = fcm_server_key;
  if (!fcm_endpoint.empty()) {
    fcm_config.endpoint = fcm_endpoint;
  }

  app_notification::APNsConfig apns_config;
  apns_config.key_id = apns_key_id;
  apns_config.team_id = apns_team_id;
  apns_config.bundle_id = apns_bundle_id;
  apns_config.private_key_path = apns_key_path;
  apns_config.use_sandbox = apns_sandbox;
  // The service posts to the endpoint as-is; sandbox selection lives here.
  if (!apns_endpoint.empty()) {
    apns_config.endpoint = apns_endpoint;
  } else if (apns_sandbox) {
    apns_config.endpoint = "https://api.development.push.apple.com:443";
  }

  // Transport selection: "logging" keeps the stub-era drop-and-log behavior;
  // "http" performs real HTTP/1.1 provider posts, TLS-secured for https
  // endpoints (providers require HTTP/2 beyond this seam, fronted by a
  // translation proxy or the provider's HTTP/1.1-compatible API).
  std::string push_transport_flag = "logging";
  for (int i = 1; i < argc; ++i) {
    if (std::string(argv[i]) == "--push_transport" && i + 1 < argc) {
      push_transport_flag = argv[++i];
    }
  }
  std::shared_ptr<app_notification::PushTransport> transport =
      std::make_shared<app_notification::LoggingPushTransport>();
  if (push_transport_flag == "http") {
    app_notification::SslHttpConnectionFactory::Config factory_config;
    factory_config.ca_file = push_ca_file;
    factory_config.verify_certificates = push_verify_tls;
    transport = std::make_shared<app_notification::HttpPushTransport>(
        std::make_shared<app_notification::SslHttpConnectionFactory>(
            factory_config));
  } else if (push_transport_flag != "logging") {
    logger.Warn("unknown --push_transport '" + push_transport_flag +
                "', using logging");
  }

  auto service = std::make_shared<app_notification::NotificationService>(
      fcm_config, apns_config, transport);
  app_notification::NotificationHandlers handlers(*service);

  asio::io_context io;

  auto on_frame = [handlers](std::shared_ptr<network::Session> session,
                             std::string&& payload) mutable {
    HandleNotificationPacket(handlers, session, std::move(payload));
  };

  network::TcpServer server(io, port, on_frame);
  network::WebSocketServer ws_server(io, ws_port, on_frame);
  server.Start();
  ws_server.Start();

  // Minimal metrics endpoint, opt-in via --metrics_port (0 = off). Declared
  // after io so it is destroyed first; a failed bind logs and the service
  // continues without metrics.
  std::optional<common::MetricsHttpServer> metrics_server;
  if (metrics_port > 0) {
    metrics_server.emplace(io, metrics_port);
    if (metrics_server->Start()) {
      logger.Info("Metrics endpoint listening on TCP:" + std::to_string(metrics_port) +
                  " (/metrics)");
    } else {
      logger.Warn("Metrics endpoint failed to bind TCP:" + std::to_string(metrics_port) +
                  "; continuing without metrics");
      metrics_server.reset();
    }
  }

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
    if (metrics_server) {
      metrics_server->Stop();
    }
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
