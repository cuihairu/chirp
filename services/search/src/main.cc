#include <atomic>
#include <chrono>
#include <thread>
#include <iostream>
#include <memory>
#include <string>
#include <csignal>

#include <asio.hpp>

#include "message_search_service.h"
#include "network/tcp_server.h"
#include "logger.h"

using namespace chirp;

// Global flag for shutdown
std::atomic<bool> running{true};

void SignalHandler(int signal) {
  common::Logger::Instance().Info("Shutting down search service...");
  running = false;
}

int main(int argc, char* argv[]) {
  // Setup signal handling
  std::signal(SIGINT, SignalHandler);
  std::signal(SIGTERM, SignalHandler);

  // Initialize logger
  auto& logger = common::Logger::Instance();
  logger.SetLevel(common::Logger::Level::kInfo);

  logger.Info("Chirp Search Service starting...");

  // Parse command line arguments
  std::string host = "0.0.0.0";
  uint16_t port = 5007;
  size_t max_results = 100;

  for (int i = 1; i < argc; ++i) {
    std::string arg = argv[i];

    if (arg == "--host" && i + 1 < argc) {
      host = argv[++i];
    } else if (arg == "--port" && i + 1 < argc) {
      port = static_cast<uint16_t>(std::stoi(argv[++i]));
    } else if (arg == "--max-results" && i + 1 < argc) {
      max_results = static_cast<size_t>(std::stoi(argv[++i]));
    } else if (arg == "--help") {
      std::cout << "Usage: " << argv[0] << " [options]\n"
                << "Options:\n"
                << "  --host <address>       Server host (default: 0.0.0.0)\n"
                << "  --port <port>          Server port (default: 5007)\n"
                << "  --max-results <count>  Max search results (default: 100)\n"
                << "  --help                 Show this help\n";
      return 0;
    }
  }

  // Configure search service
  search::SearchConfig config;
  config.max_results = max_results;

  // Create search service
  auto search_service = std::make_shared<search::MessageSearchService>(config);

  // Create IO context
  asio::io_context io_context;

  // Setup periodic stats logging
  std::thread stats_thread([search_service]() {
    while (running) {
      std::this_thread::sleep_for(std::chrono::minutes(5));

      if (running) {
        auto& stats_logger = common::Logger::Instance();
        stats_logger.Info("Search service stats:");
        stats_logger.Info("  Documents indexed: " + std::to_string(search_service->GetDocumentCount()));
        stats_logger.Info("  Index size: " + std::to_string(search_service->GetIndexSizeBytes()) + " bytes");
      }
    }
  });

  logger.Info("Search service listening on " + host + ":" + std::to_string(port));
  logger.Info("Max results per query: " + std::to_string(max_results));

  // Run IO context
  asio::executor_work_guard<asio::io_context::executor_type> work(
      io_context.get_executor());

  while (running) {
    std::this_thread::sleep_for(std::chrono::milliseconds(100));
  }

  // Cleanup
  work.reset();
  stats_thread.join();

  logger.Info("Search service stopped.");

  return 0;
}
