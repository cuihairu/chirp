#include <chrono>
#include <optional>
#include <iostream>
#include <memory>
#include <string>
#include <thread>
#include <vector>
#include <functional>

#include <asio.hpp>

#include "logger.h"
#include "network/protobuf_framing.h"
#include "network/tcp_client.h"
#include "network/websocket_client.h"
#include "proto/auth.pb.h"
#include "proto/chat.pb.h"
#include "proto/common.pb.h"
#include "proto/gateway.pb.h"
#include "proto/social.pb.h"

using chirp::common::Logger;

namespace chirp {
namespace test {

int64_t NowMs() {
  return std::chrono::duration_cast<std::chrono::milliseconds>(
      std::chrono::system_clock::now().time_since_epoch()).count();
}

// Test client for integration testing
class TestClient {
public:
  TestClient(const std::string& host, uint16_t port, bool use_ws = false)
      : host_(host), port_(port), use_ws_(use_ws), io_() {
    if (use_ws) {
      ws_client_ = std::make_unique<network::WebSocketClient>(io_);
    } else {
      tcp_client_ = std::make_unique<network::TcpClient>(io_);
    }
  }

  ~TestClient() {
    Disconnect();
  }

  bool Connect() {
    // Set up session callbacks before connecting
    if (use_ws_) {
      ws_client_->SetCallbacks(
          [this](std::shared_ptr<network::Session> session, std::string&& payload) {
            OnFrame(session, std::move(payload));
          },
          [this](std::shared_ptr<network::Session> session) {
            OnClose(session);
          }
      );
      if (!ws_client_->Connect(host_, port_)) {
        return false;
      }
      session_ = ws_client_->GetSession();
    } else {
      tcp_client_->SetCallbacks(
          [this](std::shared_ptr<network::Session> session, std::string&& payload) {
            OnFrame(session, std::move(payload));
          },
          [this](std::shared_ptr<network::Session> session) {
            OnClose(session);
          }
      );
      if (!tcp_client_->Connect(host_, port_)) {
        return false;
      }
      session_ = tcp_client_->GetSession();
    }

    // Start receive thread
    receiving_ = true;
    receive_thread_ = std::thread([this]() {
      auto work_guard = asio::make_work_guard(io_);
      while (receiving_) {
        try {
          io_.run();
          break;
        } catch (const std::exception& e) {
          Logger::Instance().Error(std::string("IO thread error: ") + e.what());
        }
      }
    });

    return true;
  }

  void Disconnect() {
    receiving_ = false;
    io_.stop();
    if (session_) {
      session_->Close();
    }
    if (receive_thread_.joinable()) {
      receive_thread_.join();
    }
  }

  bool Login(const std::string& user_id) {
    user_id_ = user_id;

    chirp::auth::LoginRequest req;
    req.set_token(user_id);
    req.set_device_id("test_client");
    req.set_platform("pc");

    chirp::gateway::Packet pkt;
    pkt.set_msg_id(chirp::gateway::LOGIN_REQ);
    pkt.set_sequence(++sequence_);
    pkt.set_body(req.SerializeAsString());

    SendPacket(pkt);

    // Wait for response with timeout
    auto start = NowMs();
    while (!logged_in_ && (NowMs() - start) < 5000) {
      std::this_thread::sleep_for(std::chrono::milliseconds(100));
    }

    return logged_in_;
  }

  bool SendMessage(const std::string& to_user, const std::string& content) {
    chirp::chat::SendMessageRequest req;
    req.set_sender_id(user_id_);
    req.set_receiver_id(to_user);
    req.set_channel_type(chirp::chat::PRIVATE);
    req.set_msg_type(chirp::chat::TEXT);
    req.set_content(content);
    req.set_client_timestamp(NowMs());

    chirp::gateway::Packet pkt;
    pkt.set_msg_id(chirp::gateway::SEND_MESSAGE_REQ);
    pkt.set_sequence(++sequence_);
    pkt.set_body(req.SerializeAsString());

    SendPacket(pkt);

    // Wait for response with timeout
    auto start = NowMs();
    while (!last_send_success_.has_value() && (NowMs() - start) < 5000) {
      std::this_thread::sleep_for(std::chrono::milliseconds(100));
    }

    return last_send_success_.value_or(false);
  }

  bool SetPresence(chirp::social::PresenceStatus status, const std::string& status_msg) {
    chirp::social::SetPresenceRequest req;
    req.set_user_id(user_id_);
    req.set_status(status);
    req.set_status_message(status_msg);

    chirp::gateway::Packet pkt;
    pkt.set_msg_id(chirp::gateway::SET_PRESENCE_REQ);
    pkt.set_sequence(++sequence_);
    pkt.set_body(req.SerializeAsString());

    SendPacket(pkt);

    // Wait for response with timeout
    auto start = NowMs();
    while (!presence_set_success_.has_value() && (NowMs() - start) < 5000) {
      std::this_thread::sleep_for(std::chrono::milliseconds(100));
    }

    return presence_set_success_.value_or(false);
  }

  std::vector<std::string> GetReceivedMessages() const {
    std::lock_guard<std::mutex> lock(mutex_);
    return received_messages_;
  }

  int GetReceivedMessageCount() const {
    std::lock_guard<std::mutex> lock(mutex_);
    return static_cast<int>(received_messages_.size());
  }

  void ClearReceivedMessages() {
    std::lock_guard<std::mutex> lock(mutex_);
    received_messages_.clear();
  }

private:
  void OnFrame(std::shared_ptr<network::Session> session, std::string&& payload) {
    chirp::gateway::Packet pkt;
    if (!pkt.ParseFromString(payload)) {
      Logger::Instance().Error("Failed to parse gateway packet");
      return;
    }

    switch (pkt.msg_id()) {
    case chirp::gateway::LOGIN_RESP: {
      chirp::auth::LoginResponse resp;
      if (resp.ParseFromString(pkt.body())) {
        logged_in_ = (resp.code() == chirp::common::OK);
        Logger::Instance().Info(std::string("Login result: ") + (logged_in_ ? "success" : "failed"));
      }
      break;
    }
    case chirp::gateway::SEND_MESSAGE_RESP: {
      chirp::chat::SendMessageResponse resp;
      if (resp.ParseFromString(pkt.body())) {
        last_send_success_ = (resp.code() == chirp::common::OK);
        Logger::Instance().Info(std::string("Send message result: ") + (last_send_success_.value() ? "success" : "failed"));
      }
      break;
    }
    case chirp::gateway::CHAT_MESSAGE_NOTIFY: {
      chirp::chat::ChatMessage msg;
      if (msg.ParseFromString(pkt.body())) {
        std::lock_guard<std::mutex> lock(mutex_);
        received_messages_.push_back(std::string(msg.content()));
        Logger::Instance().Info(std::string("Received message: ") + msg.content());
      }
      break;
    }
    case chirp::gateway::SET_PRESENCE_RESP: {
      chirp::social::SetPresenceResponse resp;
      if (resp.ParseFromString(pkt.body())) {
        presence_set_success_ = (resp.code() == chirp::common::OK);
        Logger::Instance().Info(std::string("Set presence result: ") + (presence_set_success_.value() ? "success" : "failed"));
      }
      break;
    }
    default:
      Logger::Instance().Error(std::string("Unknown message ID: ") + std::to_string(pkt.msg_id()));
      break;
    }
  }

  void OnClose(std::shared_ptr<network::Session> session) {
    Logger::Instance().Info("Session closed");
    receiving_ = false;
  }

  void SendPacket(const chirp::gateway::Packet& pkt) {
    if (!session_) {
      Logger::Instance().Error("No active session");
      return;
    }

    auto framed = network::ProtobufFraming::Encode(pkt);
    std::string data(reinterpret_cast<const char*>(framed.data()), framed.size());
    session_->Send(data);
  }

  std::string host_;
  uint16_t port_;
  bool use_ws_;
  asio::io_context io_;

  std::unique_ptr<network::TcpClient> tcp_client_;
  std::unique_ptr<network::WebSocketClient> ws_client_;
  std::shared_ptr<network::Session> session_;

  std::string user_id_;
  uint64_t sequence_{0};
  bool logged_in_{false};
  std::optional<bool> last_send_success_;
  std::optional<bool> presence_set_success_;

  std::vector<std::string> received_messages_;
  mutable std::mutex mutex_;

  bool receiving_{false};
  std::thread receive_thread_;
};

// Simple tests
bool TestBasicConnection(const std::string& gateway_host, uint16_t gateway_port) {
  std::cout << "\n=== Test: Basic Connection ===" << std::endl;
  std::cout << "Gateway: " << gateway_host << ":" << gateway_port << std::endl;

  TestClient client(gateway_host, gateway_port);
  if (!client.Connect()) {
    std::cout << "✗ Failed to connect to gateway" << std::endl;
    return false;
  }

  const std::string user_id = "integration_user";
  if (!client.Login(user_id)) {
    std::cout << "✗ Failed to login through gateway" << std::endl;
    return false;
  }

  std::cout << "✓ Gateway connection test PASSED" << std::endl;
  return true;
}

void TestProtobufEncoding() {
  std::cout << "\n=== Test: Protobuf Encoding ===" << std::endl;

  // Create a test packet
  chirp::gateway::Packet pkt;
  pkt.set_msg_id(chirp::gateway::LOGIN_REQ);
  pkt.set_sequence(1);

  chirp::auth::LoginRequest req;
  req.set_token("test_user");
  req.set_device_id("test_device");
  req.set_platform("pc");
  pkt.set_body(req.SerializeAsString());

  // Encode
  auto encoded = network::ProtobufFraming::Encode(pkt);
  std::cout << "Encoded packet size: " << encoded.size() << " bytes" << std::endl;

  // Decode
  chirp::gateway::Packet decoded;
  std::string payload(encoded.begin() + 4, encoded.end()); // Skip length prefix
  if (decoded.ParseFromString(payload)) {
    std::cout << "Decoded successfully" << std::endl;
    std::cout << "Message ID: " << decoded.msg_id() << std::endl;
    std::cout << "Sequence: " << decoded.sequence() << std::endl;

    chirp::auth::LoginRequest decoded_req;
    if (decoded_req.ParseFromString(decoded.body())) {
      std::cout << "User ID: " << decoded_req.token() << std::endl;
      std::cout << "Device ID: " << decoded_req.device_id() << std::endl;
      std::cout << "Platform: " << decoded_req.platform() << std::endl;
    }
    std::cout << "✓ Protobuf encoding test PASSED" << std::endl;
  } else {
    std::cout << "✗ Protobuf encoding test FAILED" << std::endl;
  }
}

} // namespace test
} // namespace chirp

int main(int argc, char* argv[]) {
  std::cout << "Chirp Integration Tests" << std::endl;
  std::cout << "=========================" << std::endl;

  // Parse command line arguments
  std::string gateway_host = "localhost";
  uint16_t gateway_port = 5000;
  bool run_connection_tests = false;

  for (int i = 1; i < argc; i++) {
    std::string arg = argv[i];
    if (arg == "--gateway-host" && i + 1 < argc) {
      gateway_host = argv[++i];
    } else if (arg == "--gateway-port" && i + 1 < argc) {
      gateway_port = static_cast<uint16_t>(std::stoi(argv[++i]));
    } else if (arg == "--connect" || arg == "-c") {
      run_connection_tests = true;
    } else if (arg == "--help" || arg == "-h") {
      std::cout << "Usage: " << argv[0] << " [options]" << std::endl;
      std::cout << "Options:" << std::endl;
      std::cout << "  --gateway-host HOST  Gateway host (default: localhost)" << std::endl;
      std::cout << "  --gateway-port PORT  Gateway port (default: 5000)" << std::endl;
      std::cout << "  --connect, -c        Run connection tests" << std::endl;
      std::cout << "  --help, -h           Show this help" << std::endl;
      return 0;
    }
  }

  bool all_passed = true;

  // Always run protobuf encoding test (doesn't require services)
  chirp::test::TestProtobufEncoding();

  // Optionally run connection tests (requires services)
  if (run_connection_tests) {
    all_passed = chirp::test::TestBasicConnection(gateway_host, gateway_port) && all_passed;
  }

  std::cout << "\n=== Tests Complete ===" << std::endl;
  if (!run_connection_tests) {
    std::cout << "\nNote: Live connection smoke requires running services." << std::endl;
    std::cout << "Start services with: docker compose up -d redis auth gateway chat social" << std::endl;
    std::cout << "Then run with: bash tests/run_integration_tests.sh --docker --connect" << std::endl;
    std::cout << "Or without Docker: bash tests/run_integration_tests.sh --local-services --gateway-port 5500 --auth-port 6500" << std::endl;
  }

  return all_passed ? 0 : 1;
}
