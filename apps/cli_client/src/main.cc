#include <chrono>
#include <iostream>
#include <memory>
#include <mutex>
#include <sstream>
#include <string>
#include <thread>
#include <vector>

#include <asio.hpp>

#include "common/logger.h"
#include "network/protobuf_framing.h"
#include "network/session.h"
#include "network/tcp_client.h"
#include "network/websocket_client.h"
#include "proto/auth.pb.h"
#include "proto/chat.pb.h"
#include "proto/common.pb.h"
#include "proto/gateway.pb.h"
#include "proto/social.pb.h"

namespace {

int64_t NowMs() {
  using namespace std::chrono;
  return duration_cast<milliseconds>(system_clock::now().time_since_epoch()).count();
}

int64_t g_sequence = 0;

class ChirpClient {
public:
  ChirpClient(const std::string& host, uint16_t port, bool use_ws)
      : use_ws_(use_ws), io_(), host_(host), port_(port) {
    if (use_ws) {
      ws_client_ = std::make_unique<chirp::network::WebSocketClient>(io_);
    } else {
      tcp_client_ = std::make_unique<chirp::network::TcpClient>(io_);
    }
  }

  bool Connect() {
    if (use_ws_) {
      if (!ws_client_->Connect(host_, port_)) {
        return false;
      }
      session_ = ws_client_->GetSession();
    } else {
      if (!tcp_client_->Connect(host_, port_)) {
        return false;
      }
      session_ = tcp_client_->GetSession();
    }

    // Start receive thread
    receive_thread_ = std::thread([this]() { ReceiveLoop(); });
    return true;
  }

  void Disconnect() {
    if (session_) {
      session_->Close();
    }
    if (receive_thread_.joinable()) {
      receive_thread_.join();
    }
  }

  bool Login(const std::string& user_id) {
    chirp::auth::LoginRequest req;
    req.set_token(user_id);
    req.set_device_id("cli_client");
    req.set_platform("pc");

    chirp::gateway::Packet pkt;
    pkt.set_msg_id(chirp::gateway::LOGIN_REQ);
    pkt.set_sequence(++g_sequence);
    pkt.set_body(req.SerializeAsString());

    if (!SendPacket(pkt)) {
      return false;
    }

    // Wait for response
    std::unique_lock<std::mutex> lock(response_mu_);
    if (response_cv_.wait_for(lock, std::chrono::seconds(5)) == std::cv_status::timeout) {
      return false;
    }

    return last_login_response_.code() == chirp::common::OK;
  }

  bool SendMessage(const std::string& to_user, const std::string& content) {
    chirp::chat::SendMessageRequest req;
    req.set_sender_id(current_user_id_);
    req.set_receiver_id(to_user);
    req.set_channel_type(chirp::chat::PRIVATE);
    req.set_msg_type(chirp::chat::TEXT);
    req.set_content(content);
    req.set_client_timestamp(NowMs());

    chirp::gateway::Packet pkt;
    pkt.set_msg_id(chirp::gateway::SEND_MESSAGE_REQ);
    pkt.set_sequence(++g_sequence);
    pkt.set_body(req.SerializeAsString());

    return SendPacket(pkt);
  }

  bool SetPresence(chirp::social::PresenceStatus status, const std::string& message = "") {
    chirp::social::SetPresenceRequest req;
    req.set_user_id(current_user_id_);
    req.set_status(status);
    req.set_status_message(message);

    chirp::gateway::Packet pkt;
    pkt.set_msg_id(chirp::gateway::SET_PRESENCE_REQ);
    pkt.set_sequence(++g_sequence);
    pkt.set_body(req.SerializeAsString());

    return SendPacket(pkt);
  }

  void SetCurrentUserId(const std::string& user_id) { current_user_id_ = user_id; }

  const std::string& GetCurrentUserId() const { return current_user_id_; }

private:
  bool SendPacket(const chirp::gateway::Packet& pkt) {
    if (!session_) {
      return false;
    }

    auto framed = chirp::network::ProtobufFraming::Encode(pkt);
    std::string data(reinterpret_cast<const char*>(framed.data()), framed.size());
    session_->Send(data);
    return true;
  }

  void ReceiveLoop() {
    while (session_ && !session_->IsClosed()) {
      auto data = session_->Receive();
      if (data.empty()) {
        break;
      }

      chirp::gateway::Packet pkt;
      if (!pkt.ParseFromArray(data.data(), static_cast<int>(data.size()))) {
        continue;
      }

      HandlePacket(pkt);
    }
  }

  void HandlePacket(const chirp::gateway::Packet& pkt) {
    switch (pkt.msg_id()) {
    case chirp::gateway::LOGIN_RESP: {
      chirp::auth::LoginResponse resp;
      if (resp.ParseFromArray(pkt.body().data(), static_cast<int>(pkt.body().size()))) {
        std::lock_guard<std::mutex> lock(response_mu_);
        last_login_response_ = resp;
        response_cv_.notify_all();

        if (resp.code() == chirp::common::OK) {
          std::cout << "[LOGIN] Success! User: " << resp.user_id()
                    << ", Session: " << resp.session_id() << std::endl;
        } else {
          std::cout << "[LOGIN] Failed with code: " << resp.code() << std::endl;
        }
      }
      break;
    }
    case chirp::gateway::CHAT_MESSAGE_NOTIFY: {
      chirp::chat::ChatMessage msg;
      if (msg.ParseFromArray(pkt.body().data(), static_cast<int>(pkt.body().size()))) {
        std::cout << "[MSG] " << msg.sender_id() << " -> " << msg.receiver_id() << ": "
                  << std::string(msg.content()) << std::endl;
      }
      break;
    }
    case chirp::gateway::PRESENCE_NOTIFY: {
      chirp::social::PresenceNotify notify;
      if (notify.ParseFromArray(pkt.body().data(), static_cast<int>(pkt.body().size()))) {
        std::cout << "[PRESENCE] " << notify.user_id() << " is now ";
        switch (notify.status()) {
        case chirp::social::ONLINE:
          std::cout << "ONLINE";
          break;
        case chirp::social::AWAY:
          std::cout << "AWAY";
          break;
        case chirp::social::IN_GAME:
          std::cout << "IN_GAME";
          break;
        case chirp::social::OFFLINE:
          std::cout << "OFFLINE";
          break;
        default:
          std::cout << "UNKNOWN";
        }
        if (!notify.status_message().empty()) {
          std::cout << " (" << notify.status_message() << ")";
        }
        std::cout << std::endl;
      }
      break;
    }
    case chirp::gateway::SEND_MESSAGE_RESP: {
      chirp::chat::SendMessageResponse resp;
      if (resp.ParseFromArray(pkt.body().data(), static_cast<int>(pkt.body().size()))) {
        if (resp.code() == chirp::common::OK) {
          std::cout << "[SENT] Message ID: " << resp.message_id() << std::endl;
        } else {
          std::cout << "[SENT] Failed with code: " << resp.code() << std::endl;
        }
      }
      break;
    }
    case chirp::gateway::KICK_NOTIFY: {
      chirp::auth::KickNotify notify;
      if (notify.ParseFromArray(pkt.body().data(), static_cast<int>(pkt.body().size()))) {
        std::cout << "[KICKED] Reason: " << notify.reason() << std::endl;
      }
      break;
    }
    default:
      // std::cout << "[PKT] Received message ID: " << pkt.msg_id() << std::endl;
      break;
    }
  }

  bool use_ws_;
  asio::io_context io_;
  std::string host_;
  uint16_t port_;

  std::unique_ptr<chirp::network::TcpClient> tcp_client_;
  std::unique_ptr<chirp::network::WebSocketClient> ws_client_;
  std::shared_ptr<chirp::network::Session> session_;

  std::string current_user_id_;
  std::thread receive_thread_;

  std::mutex response_mu_;
  std::condition_variable response_cv_;
  chirp::auth::LoginResponse last_login_response_;
};

void PrintHelp() {
  std::cout << "\nChirp CLI Client - Commands:\n"
            << "  connect <host> <port> [ws]  - Connect to server (add 'ws' for WebSocket)\n"
            << "  login <user_id>            - Login with user ID\n"
            << "  msg <to_user> <text>       - Send message\n"
            << "  presence <status> [msg]     - Set presence (online/away/game/offline)\n"
            << "  help                       - Show this help\n"
            << "  quit                       - Exit\n"
            << std::endl;
}

chirp::social::PresenceStatus ParsePresence(const std::string& status_str) {
  if (status_str == "online") return chirp::social::ONLINE;
  if (status_str == "away") return chirp::social::AWAY;
  if (status_str == "game" || status_str == "ingame") return chirp::social::IN_GAME;
  if (status_str == "offline") return chirp::social::OFFLINE;
  return chirp::social::ONLINE;
}

} // namespace

int main(int argc, char** argv) {
  std::string host = "localhost";
  uint16_t port = 5000;
  bool use_ws = false;

  if (argc > 1) {
    host = argv[1];
  }
  if (argc > 2) {
    port = static_cast<uint16_t>(std::atoi(argv[2]));
  }
  if (argc > 3 && std::string(argv[3]) == "ws") {
    use_ws = true;
  }

  std::cout << "Chirp CLI Client" << std::endl;
  std::cout << "Connecting to " << host << ":" << port << (use_ws ? " (WebSocket)" : " (TCP)") << std::endl;

  ChirpClient client(host, port, use_ws);

  std::string line;
  std::vector<std::string> history;

  PrintHelp();

  std::cout << "> ";
  while (std::getline(std::cin, line)) {
    if (line.empty()) {
      std::cout << "> ";
      continue;
    }

    history.push_back(line);

    // Parse command
    std::istringstream iss(line);
    std::string cmd;
    iss >> cmd;

    if (cmd == "quit" || cmd == "exit") {
      break;
    } else if (cmd == "help") {
      PrintHelp();
    } else if (cmd == "connect") {
      std::string new_host;
      uint16_t new_port;
      std::string ws_flag;
      iss >> new_host >> new_port >> ws_flag;

      client.Disconnect();
      ChirpClient new_client(new_host, new_port, ws_flag == "ws");
      client = std::move(new_client);

      if (client.Connect()) {
        std::cout << "Connected successfully!" << std::endl;
      } else {
        std::cout << "Connection failed!" << std::endl;
      }
    } else if (cmd == "login") {
      std::string user_id;
      iss >> user_id;

      client.SetCurrentUserId(user_id);
      if (client.Login(user_id)) {
        std::cout << "Login successful!" << std::endl;
      } else {
        std::cout << "Login failed!" << std::endl;
      }
    } else if (cmd == "msg") {
      std::string to_user, text;
      iss >> to_user;
      std::getline(iss >> std::ws, text);

      if (!to_user.empty() && !text.empty()) {
        client.SendMessage(to_user, text);
      }
    } else if (cmd == "presence") {
      std::string status_str, message;
      iss >> status_str;
      std::getline(iss >> std::ws, message);

      auto status = ParsePresence(status_str);
      client.SetPresence(status, message);
    } else {
      std::cout << "Unknown command: " << cmd << std::endl;
      std::cout << "Type 'help' for available commands." << std::endl;
    }

    std::cout << "> ";
  }

  client.Disconnect();
  return 0;
}
