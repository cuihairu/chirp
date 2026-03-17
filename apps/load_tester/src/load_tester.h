#pragma once

#include <atomic>
#include <cstdint>
#include <functional>
#include <memory>
#include <string>
#include <thread>
#include <vector>

#include <asio.hpp>

namespace chirp::load_test {

/// @brief Load testing configuration
struct LoadTestConfig {
  // Connection settings
  std::string host = "127.0.0.1";
  uint16_t port = 7000;
  uint16_t ws_port = 7001;
  bool use_websocket = false;

  // Load parameters
  int concurrent_connections = 100;
  int connections_per_second = 10;
  int messages_per_second = 100;
  int test_duration_seconds = 300;

  // Message settings
  std::string message_content = "Load test message";
  bool enable_ping_pong = true;

  // Reporting
  int report_interval_seconds = 5;
};

/// @brief Statistics for load testing
struct LoadTestStats {
  std::atomic<uint64_t> connections_succeeded{0};
  std::atomic<uint64_t> connections_failed{0};
  std::atomic<uint64_t> messages_sent{0};
  std::atomic<uint64_t> messages_received{0};
  std::atomic<uint64_t> current_connections{0};
  std::atomic<uint64_t> total_latency_ms{0};
  std::atomic<uint64_t> min_latency_ms{UINT64_MAX};
  std::atomic<uint64_t> max_latency_ms{0};

  double GetAverageLatencyMs() const {
    uint64_t sent = messages_sent.load();
    return sent > 0 ? static_cast<double>(total_latency_ms.load()) / sent : 0.0;
  }

  void Reset() {
    connections_succeeded = 0;
    connections_failed = 0;
    messages_sent = 0;
    messages_received = 0;
    current_connections = 0;
    total_latency_ms = 0;
    min_latency_ms = UINT64_MAX;
    max_latency_ms = 0;
  }
};

/// @brief Load tester for Chirp services
/// Supports TCP and WebSocket protocols
class LoadTester {
public:
  using ReportCallback = std::function<void(const LoadTestStats&)>;

  explicit LoadTester(const LoadTestConfig& config);
  ~LoadTester();

  /// @brief Start the load test
  void Start();

  /// @brief Stop the load test
  void Stop();

  /// @brief Get current statistics
  LoadTestStats GetStats() const;

  /// @brief Set report callback
  void SetReportCallback(ReportCallback cb) { report_callback_ = std::move(cb); }

  /// @brief Run a specific test scenario
  void RunConnectionFlood();
  void RunMessageStorm();
  void RunMixedLoad();

private:
  struct ClientSession {
    int id;
    std::string user_id;
    std::string session_id;
    bool connected{false};
    int64_t connect_time_ms{0};
    int64_t last_ping_ms{0};

    // WebSocket specific
    std::shared_ptr<asio::ip::tcp::socket> tcp_socket;
    std::shared_ptr<void> ws_stream;  // Opaque WebSocket stream pointer
  };

  void WorkerThread(int thread_id, int connections_per_thread);
  void ReporterThread();

  bool ConnectClient(ClientSession& session, asio::io_context& io);
  void DisconnectClient(ClientSession& session);
  bool SendMessage(ClientSession& session, const std::string& content);
  bool SendPing(ClientSession& session);
  void HandlePong(ClientSession& session);

  LoadTestConfig config_;
  LoadTestStats stats_;
  ReportCallback report_callback_;

  std::vector<std::thread> worker_threads_;
  std::thread reporter_thread_;

  std::atomic<bool> running_{false};
  std::atomic<bool> stop_requested_{false};

  std::vector<ClientSession> sessions_;
  std::mutex sessions_mutex_;
};

/// @brief Test scenarios
enum class TestScenario {
  kConnectionFlood,   // Rapidly open many connections
  kMessageStorm,      // High message throughput
  kMixedLoad,         // Realistic usage pattern
  kFailureRecovery    // Test reconnect behavior
};

/// @brief Run a specific test scenario
void RunScenario(TestScenario scenario, const LoadTestConfig& config);

/// @brief Print statistics report
void PrintStatsReport(const LoadTestStats& stats);

} // namespace chirp::load_test
