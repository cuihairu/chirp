#include "load_tester.h"

#include <chrono>
#include <iostream>
#include <random>
#include <sstream>

#include "common/logger.h"
#include "network/protobuf_framing.h"
#include "network/session.h"
#include "network/tcp_server.h"
#include "network/websocket_server.h"
#include "proto/auth.pb.h"
#include "proto/chat.pb.h"
#include "proto/common.pb.h"
#include "proto/gateway.pb.h"

namespace chirp::load_test {
namespace {

int64_t NowMs() {
  using namespace std::chrono;
  return duration_cast<milliseconds>(system_clock::now().time_since_epoch()).count();
}

std::string RandomUserId(int client_id) {
  return "load_test_user_" + std::to_string(client_id);
}

std::string RandomMessageId() {
  static std::atomic<uint64_t> counter{1};
  return "msg_" + std::to_string(NowMs()) + "_" + std::to_string(counter.fetch_add(1));
}

// Encode gateway packet
std::vector<uint8_t> EncodePacket(uint32_t msg_id, uint64_t seq, const std::string& body) {
  chirp::gateway::Packet pkt;
  pkt.set_msg_id(static_cast<chirp::gateway::MsgID>(msg_id));
  pkt.set_sequence(seq);
  pkt.set_body(body);

  std::string serialized = pkt.SerializeAsString();
  auto framed = chirp::network::ProtobufFraming::Encode(pkt);

  return std::vector<uint8_t>(framed.begin(), framed.end());
}

} // namespace

LoadTester::LoadTester(const LoadTestConfig& config)
    : config_(config) {}

LoadTester::~LoadTester() {
  Stop();
}

void LoadTester::Start() {
  if (running_.load()) {
    return;
  }

  running_.store(true);
  stop_requested_.store(false);
  stats_.Reset();

  Logger::Instance().Info("Load test starting: " +
                         std::to_string(config_.concurrent_connections) + " connections, " +
                         std::to_string(config_.messages_per_second) + " msg/sec");

  // Calculate threads needed
  int total_threads = (config_.concurrent_connections + config_.connections_per_second - 1) /
                      config_.connections_per_second;

  // Start worker threads
  for (int i = 0; i < total_threads; ++i) {
    worker_threads_.emplace_back([this, i]() {
      WorkerThread(i, config_.connections_per_second);
    });
  }

  // Start reporter thread
  reporter_thread_ = std::thread([this]() { ReporterThread(); });
}

void LoadTester::Stop() {
  if (!running_.load()) {
    return;
  }

  stop_requested_.store(true);
  running_.store(false);

  // Wait for threads
  for (auto& thread : worker_threads_) {
    if (thread.joinable()) {
      thread.join();
    }
  }
  worker_threads_.clear();

  if (reporter_thread_.joinable()) {
    reporter_thread_.join();
  }

  Logger::Instance().Info("Load test stopped");
  PrintStatsReport(stats_);
}

LoadTestStats LoadTester::GetStats() const {
  return stats_;
}

void LoadTester::RunConnectionFlood() {
  Start();

  // Run for specified duration
  std::this_thread::sleep_for(std::chrono::seconds(config_.test_duration_seconds));

  Stop();
}

void LoadTester::RunMessageStorm() {
  stats_.messages_per_second = config_.messages_per_second;

  Start();

  // Wait for connections to establish
  std::this_thread::sleep_for(std::chrono::seconds(5));

  // Send messages as fast as possible
  while (running_.load() && !stop_requested_.load()) {
    std::this_thread::sleep_for(std::chrono::milliseconds(100));
  }

  Stop();
}

void LoadTester::RunMixedLoad() {
  // Realistic usage pattern
  stats_.messages_per_second = config_.messages_per_second / 2;  // 50% of max

  Start();

  // Run for specified duration
  std::this_thread::sleep_for(std::chrono::seconds(config_.test_duration_seconds));

  Stop();
}

void LoadTester::WorkerThread(int thread_id, int connections_per_thread) {
  asio::io_context io;
  asio::executor_work_guard<asio::io_context::executor_type> work_guard(io.get_executor());

  std::vector<ClientSession> thread_sessions;
  thread_sessions.reserve(connections_per_thread);

  // Establish connections
  for (int i = 0; i < connections_per_thread && !stop_requested_.load(); ++i) {
    ClientSession session;
    session.id = thread_id * connections_per_thread + i;
    session.user_id = RandomUserId(session.id);

    if (ConnectClient(session, io)) {
      thread_sessions.push_back(std::move(session));
      stats_.connections_succeeded++;
      stats_.current_connections++;

      // Ramp up delay
      std::this_thread::sleep_for(std::chrono::milliseconds(
          1000 / config_.connections_per_second));
    } else {
      stats_.connections_failed++;
    }
  }

  // Add to global sessions
  {
    std::lock_guard<std::mutex> lock(sessions_mutex_);
    for (auto& session : thread_sessions) {
      sessions_.push_back(std::move(session));
    }
  }

  // Run io_context for message handling
  io.run();

  // Cleanup
  {
    std::lock_guard<std::mutex> lock(sessions_mutex_);
    for (auto& session : thread_sessions) {
      DisconnectClient(session);
    }
    stats_.current_connections -= static_cast<int>(thread_sessions.size());
  }
}

void LoadTester::ReporterThread() {
  while (running_.load() && !stop_requested_.load()) {
    std::this_thread::sleep_for(std::chrono::seconds(config_.report_interval_seconds));

    PrintStatsReport(stats_);

    if (report_callback_) {
      report_callback_(stats_);
    }
  }
}

bool LoadTester::ConnectClient(ClientSession& session, asio::io_context& io) {
  try {
    asio::ip::tcp::socket socket(io);
    asio::ip::tcp::endpoint endpoint(
        asio::ip::make_address(config_.host),
        config_.port
    );

    auto start = NowMs();
    asio::error_code ec;
    socket.connect(endpoint, ec);

    if (ec) {
      return false;
    }

    session.connect_time_ms = NowMs() - start;
    session.connected = true;

    // Send login request
    chirp::auth::LoginRequest login_req;
    login_req.set_token(session.user_id);
    login_req.set_device_id("load_test_device");
    login_req.set_platform("pc");

    std::vector<uint8_t> login_pkt = EncodePacket(
        static_cast<uint32_t>(chirp::gateway::LOGIN_REQ),
        0,
        login_req.SerializeAsString()
    );

    asio::write(socket, asio::buffer(login_pkt), ec);

    // Read login response
    std::array<uint8_t, 4096> read_buf;
    size_t bytes_read = socket.read_some(asio::buffer(read_buf), ec);

    if (!ec && bytes_read > 0) {
      chirp::gateway::Packet pkt;
      std::string pkt_data(read_buf.begin(), read_buf.begin() + bytes_read);

      if (pkt.ParseFromArray(pkt_data.data(), static_cast<int>(pkt_data.size()))) {
        if (pkt.msg_id() == chirp::gateway::LOGIN_RESP) {
          chirp::auth::LoginResponse resp;
          if (resp.ParseFromArray(pkt.body().data(), static_cast<int>(pkt.body().size()))) {
            session.session_id = resp.session_id();
            return true;
          }
        }
      }
    }

    return false;
  } catch (...) {
    return false;
  }
}

void LoadTester::DisconnectClient(ClientSession& session) {
  session.connected = false;
  // Cleanup socket
}

bool LoadTester::SendMessage(ClientSession& session, const std::string& content) {
  if (!session.connected) {
    return false;
  }

  chirp::chat::SendMessageRequest req;
  req.set_sender_id(session.user_id);
  req.set_receiver_id("load_test_receiver");
  req.set_channel_type(chirp::chat::PRIVATE);
  req.set_msg_type(chirp::chat::TEXT);
  req.set_content(content);

  std::vector<uint8_t> pkt = EncodePacket(
      static_cast<uint32_t>(chirp::gateway::SEND_MESSAGE_REQ),
      0,
      req.SerializeAsString()
  );

  // Send via socket (simplified - should store socket in session)
  stats_.messages_sent++;

  return true;
}

bool LoadTester::SendPing(ClientSession& session) {
  if (!session.connected) {
    return false;
  }

  auto now = NowMs();

  // Limit ping rate
  if (now - session.last_ping_ms < 30000) {  // 30 seconds
    return true;
  }

  chirp::gateway::HeartbeatPing ping;
  ping.set_timestamp(now);

  std::vector<uint8_t> pkt = EncodePacket(
      static_cast<uint32_t>(chirp::gateway::HEARTBEAT_PING),
      0,
      ping.SerializeAsString()
  );

  session.last_ping_ms = now;

  // Send via socket (simplified)
  return true;
}

void LoadTester::HandlePong(ClientSession& session) {
  // Calculate latency
  // Update stats
}

// ============================================================================
// Helper Functions
// ============================================================================

void RunScenario(TestScenario scenario, const LoadTestConfig& config) {
  LoadTester tester(config);

  switch (scenario) {
  case TestScenario::kConnectionFlood:
    tester.RunConnectionFlood();
    break;
  case TestScenario::kMessageStorm:
    tester.RunMessageStorm();
    break;
  case TestScenario::kMixedLoad:
    tester.RunMixedLoad();
    break;
  case TestScenario::kFailureRecovery:
    // TODO: Implement
    break;
  }
}

void PrintStatsReport(const LoadTestStats& stats) {
  std::cout << "\n=== Load Test Statistics ===" << std::endl;
  std::cout << "Connections Succeeded: " << stats.connections_succeeded << std::endl;
  std::cout << "Connections Failed:    " << stats.connections_failed << std::endl;
  std::cout << "Current Connections:   " << stats.current_connections << std::endl;
  std::cout << "Messages Sent:           " << stats.messages_sent << std::endl;
  std::cout << "Messages Received:       " << stats.messages_received << std::endl;
  std::cout << "Average Latency:         " << stats.GetAverageLatencyMs() << " ms" << std::endl;
  std::cout << "Min Latency:             " << stats.min_latency_ms.load() << " ms" << std::endl;
  std::cout << "Max Latency:             " << stats.max_latency_ms.load() << " ms" << std::endl;
  std::cout << "============================\n" << std::endl;
}

} // namespace chirp::load_test
