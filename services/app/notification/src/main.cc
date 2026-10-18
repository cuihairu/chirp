#include <chrono>
#include <functional>
#include <iostream>
#include <memory>
#include <optional>
#include <string>
#include <unordered_set>
#include <utility>

#include <asio.hpp>

#include "logger.h"
#include "http_push_transport.h"
#include "device_store.h"
#include "caller_auth.h"
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

// Dispatch body of the on_frame callback wired into both listeners below.
// Caller authentication gate (audit 表一 P0 half): with a non-empty caller
// secret every connection must complete the SERVER_AUTH_REQ handshake before
// any 6xxx packet is served. A rejected handshake is answered AUTH_FAILED and
// closes the connection, mirroring chat's HandleServerAuth contract; a
// non-handshake packet on an unauthenticated connection is dropped with the
// connection closed. Without a secret the plane stays open as before.
void HandleNotificationPacket(app_notification::NotificationHandlers& handlers,
                              const std::shared_ptr<network::Session>& session,
                              const std::string& caller_secret,
                              std::unordered_set<const network::Session*>& trusted_conns,
                              std::string&& payload) {
  gateway::Packet pkt;
  if (!pkt.ParseFromArray(payload.data(), static_cast<int>(payload.size()))) {
    chirp::common::Logger::Instance().Warn(
        "failed to parse Packet on the notification plane");
    return;
  }
  const auto verdict = app_notification::GateCallerPacket(
      trusted_conns.count(session.get()) != 0, caller_secret, pkt);
  if (verdict != app_notification::CallerAuthVerdict::kServe) {
    if (verdict == app_notification::CallerAuthVerdict::kHandshake) {
      gateway::Packet auth_resp;
      if (app_notification::VerifyCallerSecret(pkt, caller_secret, &auth_resp)) {
        trusted_conns.insert(session.get());
        SendPacket(session, std::move(auth_resp));
      } else {
        chirp::common::Logger::Instance().Warn(
            "notification plane rejected a caller secret on a new connection");
        auto framed = network::ProtobufFraming::Encode(auth_resp);
        session->SendAndClose(
            std::string(reinterpret_cast<const char*>(framed.data()), framed.size()));
      }
      return;
    }
    chirp::common::Logger::Instance().Warn(
        "notification plane dropping a packet from an unauthenticated connection");
    session->Close();
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
  // Empty = persistence off: the registry stays memory-only (the previous
  // behavior), so integration scripts that start several instances in one
  // working directory do not fight over one file.
  std::string device_store_file;
  // Empty = open plane (the pre-auth behavior): the dev compose stack and
  // integration scripts keep working unchanged.
  std::string service_secret;

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
    } else if (arg == "--device_store_file" && i + 1 < argc) {
      device_store_file = argv[++i];
    } else if (arg == "--service_secret" && i + 1 < argc) {
      service_secret = argv[++i];
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
                << "  --device_store_file <path>  persist device registrations across\n"
                << "                        restarts (default: off, memory-only)\n"
                << "  --service_secret <secret>  require the SERVER_AUTH_REQ handshake\n"
                << "                        from every caller (default: off, open plane)\n"
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

  std::shared_ptr<app_notification::DeviceStore> device_store;
  if (!device_store_file.empty()) {
    device_store = std::make_shared<app_notification::DeviceStore>(device_store_file);
  }
  auto service = std::make_shared<app_notification::NotificationService>(
      fcm_config, apns_config, transport, device_store);
  if (device_store) {
    // Before the listeners go live: a registration saved by the previous
    // process keeps receiving pushes without waiting for a re-register.
    const int restored = service->RestorePersisted();
    logger.Info("restored " + std::to_string(restored) +
                " device registrations from " + device_store_file);
  }
  app_notification::NotificationHandlers handlers(*service);

  asio::io_context io;

  // Connections that completed the SERVER_AUTH_REQ handshake (audit 表一 P0
  // half). Both listeners share the set; a disconnect erases its entry so the
  // memory stays bounded and a recycled pointer can never authenticate.
  std::unordered_set<const network::Session*> trusted_conns;

  auto on_frame = [&trusted_conns, service_secret,
                   handlers](std::shared_ptr<network::Session> session,
                             std::string&& payload) mutable {
    HandleNotificationPacket(handlers, session, service_secret, trusted_conns,
                             std::move(payload));
  };

  auto on_disconnect = [&trusted_conns](const std::shared_ptr<network::Session>& session) {
    trusted_conns.erase(session.get());
  };

  // --host controls the actual bind address of both listeners (literal IP).
  if (!service_secret.empty()) {
    logger.Info("notification plane caller authentication enabled (SERVER_AUTH_REQ)");
  } else {
    logger.Info("notification plane caller authentication disabled: open plane");
  }
  network::TcpServer server(io, port, on_frame, on_disconnect, host);
  network::WebSocketServer ws_server(io, ws_port, on_frame, on_disconnect, host);
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

  // Log the actually bound endpoint, not the flag: the bind can only differ
  // from --host when the kernel picked an ephemeral port.
  logger.Info("Notification service listening on " + server.BoundAddress().to_string() + " tcp/" +
              std::to_string(server.Port()) + " ws/" + std::to_string(ws_server.Port()));
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
