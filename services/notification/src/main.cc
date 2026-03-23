#include <atomic>
#include <chrono>
#include <thread>
#include <iostream>
#include <memory>
#include <string>
#include <csignal>

#include <asio.hpp>

#include "notification_service.h"
#include "network/tcp_server.h"
#include "logger.h"

using namespace chirp;

// Global flag for shutdown
std::atomic<bool> running{true};

void SignalHandler(int signal) {
  common::Logger::Instance().Info("Shutting down notification service...");
  running = false;
}

int main(int argc, char* argv[]) {
  // Setup signal handling
  std::signal(SIGINT, SignalHandler);
  std::signal(SIGTERM, SignalHandler);

  // Initialize logger
  auto& logger = common::Logger::Instance();
  logger.SetLevel(common::Logger::Level::kInfo);

  logger.Info("Chirp Notification Service starting...");

  // Parse command line arguments
  std::string host = "0.0.0.0";
  uint16_t port = 5006;
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
                << "  --port <port>       Server port (default: 5006)\n"
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
  notification::FCMConfig fcm_config;
  fcm_config.server_key = fcm_server_key;

  notification::APNsConfig apns_config;
  apns_config.key_id = apns_key_id;
  apns_config.team_id = apns_team_id;
  apns_config.bundle_id = apns_bundle_id;
  apns_config.private_key_path = apns_key_path;
  apns_config.use_sandbox = apns_sandbox;

  // Create notification service
  auto notification_service = std::make_shared<notification::NotificationService>(
      fcm_config, apns_config);

  // Create IO context
  asio::io_context io_context;

  // Setup cleanup timer
  std::thread cleanup_thread([&]() {
    while (running) {
      std::this_thread::sleep_for(std::chrono::minutes(5));

      if (running) {
        notification_service->CleanupInactiveDevices();
        notification_service->CleanupExpiredCooldowns();
      }
    }
  });

  logger.Info("Notification service listening on " + host + ":" + std::to_string(port));
  logger.Info("FCM configured: " + std::string(fcm_server_key.empty() ? "no" : "yes"));
  logger.Info("APNs configured: " + std::string(apns_key_id.empty() ? "no" : "yes"));

  // Run IO context
  asio::executor_work_guard<asio::io_context::executor_type> work(
      io_context.get_executor());

  while (running) {
    std::this_thread::sleep_for(std::chrono::milliseconds(100));
  }

  // Cleanup
  work.reset();
  cleanup_thread.join();

  // Print stats
  const auto& stats = notification_service->GetStats();
  logger.Info("Statistics:");
  logger.Info("  Notifications sent: " + std::to_string(stats.notifications_sent.load()));
  logger.Info("  Notifications failed: " + std::to_string(stats.notifications_failed.load()));
  logger.Info("  Devices registered: " + std::to_string(stats.devices_registered.load()));
  logger.Info("  FCM sent: " + std::to_string(stats.fcm_sent.load()));
  logger.Info("  APNs sent: " + std::to_string(stats.apns_sent.load()));

  logger.Info("Notification service stopped.");

  return 0;
}
