#include <chrono>
#include <cstdlib>
#include <iostream>
#include <string>

#include "load_tester.h"

using namespace chirp::load_test;

void PrintUsage(const char* program) {
  std::cout << "Usage: " << program << " [options]\n"
            << "Options:\n"
            << "  --host <host>           Server host (default: 127.0.0.1)\n"
            << "  --port <port>           Server port (default: 7000)\n"
            << "  --ws                    Use WebSocket instead of TCP\n"
            << "  --connections <n>      Concurrent connections (default: 100)\n"
            << "  --conns-per-sec <n>     Connections per second (default: 10)\n"
            << "  --msg-per-sec <n>       Messages per second (default: 100)\n"
            << "  --duration <seconds>    Test duration (default: 300)\n"
            << "  --scenario <type>       Test scenario:\n"
            << "                         flood - Connection flood\n"
            << "                         storm - Message storm\n"
            << "                         mixed - Mixed load (default)\n"
            << "  --report-interval <s>   Stats report interval (default: 5)\n"
            << "  --help                 Show this help\n";
}

uint16_t ParseU16(const std::string& s, uint16_t def) {
  try {
    return static_cast<uint16_t>(std::stoul(s));
  } catch (...) {
    return def;
  }
}

int ParseInt(const std::string& s, int def) {
  try {
    return std::stoi(s);
  } catch (...) {
    return def;
  }
}

int main(int argc, char** argv) {
  LoadTestConfig config;

  // Parse command line arguments
  for (int i = 1; i < argc; ++i) {
    std::string arg = argv[i];

    if (arg == "--help" || arg == "-h") {
      PrintUsage(argv[0]);
      return 0;
    }
    else if (arg == "--host" && i + 1 < argc) {
      config.host = argv[++i];
    }
    else if (arg == "--port" && i + 1 < argc) {
      config.port = ParseU16(argv[++i], 7000);
    }
    else if (arg == "--ws") {
      config.use_websocket = true;
    }
    else if (arg == "--connections" && i + 1 < argc) {
      config.concurrent_connections = ParseInt(argv[++i], 100);
    }
    else if (arg == "--conns-per-sec" && i + 1 < argc) {
      config.connections_per_second = ParseInt(argv[++i], 10);
    }
    else if (arg == "--msg-per-sec" && i + 1 < argc) {
      config.messages_per_second = ParseInt(argv[++i], 100);
    }
    else if (arg == "--duration" && i + 1 < argc) {
      config.test_duration_seconds = ParseInt(argv[++i], 300);
    }
    else if (arg == "--scenario" && i + 1 < argc) {
      std::string scenario = argv[++i];
    }
    else if (arg == "--report-interval" && i + 1 < argc) {
      config.report_interval_seconds = ParseInt(argv[++i], 5);
    }
  }

  // Determine scenario
  TestScenario scenario = TestScenario::kMixedLoad;

  std::cout << "Chirp Load Tester" << std::endl;
  std::cout << "==================" << std::endl;
  std::cout << "Host: " << config.host << ":" << config.port << std::endl;
  std::cout << "Connections: " << config.concurrent_connections << std::endl;
  std::cout << "Duration: " << config.test_duration_seconds << " seconds" << std::endl;
  std::cout << std::endl;

  // Run the test
  RunScenario(scenario, config);

  return 0;
}
