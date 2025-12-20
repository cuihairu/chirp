#include <chrono>
#include <cstdint>
#include <cstdlib>
#include <mutex>
#include <optional>
#include <random>
#include <string>
#include <unordered_map>

#include <asio.hpp>

#include "common/logger.h"
#include "network/protobuf_framing.h"
#include "network/session.h"
#include "network/tcp_server.h"
#include "network/websocket_server.h"
#include "proto/auth.pb.h"
#include "proto/gateway.pb.h"

namespace {

int64_t NowMs() {
  using namespace std::chrono;
  return duration_cast<milliseconds>(system_clock::now().time_since_epoch()).count();
}

uint16_t ParsePort(int argc, char** argv) {
  uint16_t port = 5000;
  for (int i = 1; i < argc; i++) {
    std::string a = argv[i];
    if ((a == "--port" || a == "-p") && i + 1 < argc) {
      port = static_cast<uint16_t>(std::atoi(argv[i + 1]));
      i++;
    }
  }
  return port;
}

std::optional<uint16_t> ParseOptPort(int argc, char** argv, const std::string& key) {
  for (int i = 1; i < argc; i++) {
    if (argv[i] == key && i + 1 < argc) {
      return static_cast<uint16_t>(std::atoi(argv[i + 1]));
    }
  }
  return std::nullopt;
}

std::string RandomHex(size_t bytes) {
  static thread_local std::mt19937_64 rng{std::random_device{}()};
  std::uniform_int_distribution<uint32_t> dist(0, 255);
  static const char* kHex = "0123456789abcdef";

  std::string out;
  out.resize(bytes * 2);
  for (size_t i = 0; i < bytes; i++) {
    uint8_t b = static_cast<uint8_t>(dist(rng));
    out[i * 2] = kHex[(b >> 4) & 0xF];
    out[i * 2 + 1] = kHex[b & 0xF];
  }
  return out;
}

struct GatewayState {
  std::mutex mu;
  std::unordered_map<std::string, std::weak_ptr<chirp::network::Session>> user_to_session;
  std::unordered_map<void*, std::string> session_to_user;
};

void SendPacket(const std::shared_ptr<chirp::network::Session>& session,
                chirp::gateway::MsgID msg_id,
                int64_t seq,
                const std::string& body) {
  chirp::gateway::Packet pkt;
  pkt.set_msg_id(msg_id);
  pkt.set_sequence(seq);
  pkt.set_body(body);
  auto framed = chirp::network::ProtobufFraming::Encode(pkt);
  session->Send(std::string(reinterpret_cast<const char*>(framed.data()), framed.size()));
}

} // namespace

int main(int argc, char** argv) {
  using chirp::common::Logger;

  Logger::Instance().SetLevel(Logger::Level::kInfo);
  const uint16_t port = ParsePort(argc, argv);
  const uint16_t ws_port = ParseOptPort(argc, argv, "--ws_port").value_or(static_cast<uint16_t>(port + 1));
  Logger::Instance().Info("chirp_gateway starting tcp=" + std::to_string(port) + " ws=" + std::to_string(ws_port));

  asio::io_context io;

  auto state = std::make_shared<GatewayState>();

  chirp::network::TcpServer server(
      io, port,
      [state](std::shared_ptr<chirp::network::Session> session, std::string&& payload) {
        chirp::gateway::Packet pkt;
        if (!pkt.ParseFromArray(payload.data(), static_cast<int>(payload.size()))) {
          Logger::Instance().Warn("failed to parse Packet from client");
          return;
        }

        switch (pkt.msg_id()) {
        case chirp::gateway::LOGIN_REQ: {
          chirp::auth::LoginRequest req;
          if (!req.ParseFromArray(pkt.body().data(), static_cast<int>(pkt.body().size()))) {
            chirp::auth::LoginResponse resp;
            resp.set_code(chirp::common::INVALID_PARAM);
            resp.set_server_time(NowMs());
            SendPacket(session, chirp::gateway::LOGIN_RESP, pkt.sequence(), resp.SerializeAsString());
            return;
          }

          const std::string user_id = req.token(); // scaffolding: treat token as user id

          // Multi-device policy (scaffolding): last login wins; kick previous session.
          std::shared_ptr<chirp::network::Session> old;
          {
            std::lock_guard<std::mutex> lock(state->mu);
            auto it = state->user_to_session.find(user_id);
            if (it != state->user_to_session.end()) {
              old = it->second.lock();
            }
            state->user_to_session[user_id] = session;
            state->session_to_user[session.get()] = user_id;
          }

          if (old && old.get() != session.get()) {
            chirp::auth::KickNotify kick;
            kick.set_reason("login from another device");

            chirp::gateway::Packet kick_pkt;
            kick_pkt.set_msg_id(chirp::gateway::KICK_NOTIFY);
            kick_pkt.set_sequence(0);
            kick_pkt.set_body(kick.SerializeAsString());

            auto framed = chirp::network::ProtobufFraming::Encode(kick_pkt);
            old->SendAndClose(std::string(reinterpret_cast<const char*>(framed.data()), framed.size()));
          }

          chirp::auth::LoginResponse resp;
          if (user_id.empty()) {
            resp.set_code(chirp::common::INVALID_PARAM);
          } else {
            resp.set_code(chirp::common::OK);
            resp.set_session_id(RandomHex(16));
          }
          resp.set_server_time(NowMs());
          SendPacket(session, chirp::gateway::LOGIN_RESP, pkt.sequence(), resp.SerializeAsString());
          break;
        }
        case chirp::gateway::HEARTBEAT_PING: {
          chirp::gateway::HeartbeatPing ping;
          if (!ping.ParseFromArray(pkt.body().data(), static_cast<int>(pkt.body().size()))) {
            Logger::Instance().Warn("failed to parse HeartbeatPing body");
            return;
          }

          chirp::gateway::HeartbeatPong pong;
          pong.set_timestamp(ping.timestamp());
          pong.set_server_time(NowMs());

          chirp::gateway::Packet resp;
          resp.set_msg_id(chirp::gateway::HEARTBEAT_PONG);
          resp.set_sequence(pkt.sequence());
          resp.set_body(pong.SerializeAsString());

          auto framed = chirp::network::ProtobufFraming::Encode(resp);
          session->Send(std::string(reinterpret_cast<const char*>(framed.data()), framed.size()));
          break;
        }
        default:
          // For scaffolding: ignore unknown/unimplemented messages.
          break;
        }
      },
      [state](std::shared_ptr<chirp::network::Session> session) {
        std::lock_guard<std::mutex> lock(state->mu);
        auto it = state->session_to_user.find(session.get());
        if (it == state->session_to_user.end()) {
          return;
        }
        const std::string user_id = it->second;
        state->session_to_user.erase(it);

        auto it2 = state->user_to_session.find(user_id);
        if (it2 != state->user_to_session.end()) {
          auto cur = it2->second.lock();
          if (!cur || cur.get() == session.get()) {
            state->user_to_session.erase(it2);
          }
        }
      });

  chirp::network::WebSocketServer ws_server(
      io, ws_port,
      [state](std::shared_ptr<chirp::network::Session> session, std::string&& payload) {
        chirp::gateway::Packet pkt;
        if (!pkt.ParseFromArray(payload.data(), static_cast<int>(payload.size()))) {
          Logger::Instance().Warn("failed to parse Packet from ws client");
          return;
        }

        switch (pkt.msg_id()) {
        case chirp::gateway::LOGIN_REQ: {
          chirp::auth::LoginRequest req;
          if (!req.ParseFromArray(pkt.body().data(), static_cast<int>(pkt.body().size()))) {
            chirp::auth::LoginResponse resp;
            resp.set_code(chirp::common::INVALID_PARAM);
            resp.set_server_time(NowMs());
            SendPacket(session, chirp::gateway::LOGIN_RESP, pkt.sequence(), resp.SerializeAsString());
            return;
          }

          const std::string user_id = req.token(); // scaffolding: treat token as user id

          std::shared_ptr<chirp::network::Session> old;
          {
            std::lock_guard<std::mutex> lock(state->mu);
            auto it = state->user_to_session.find(user_id);
            if (it != state->user_to_session.end()) {
              old = it->second.lock();
            }
            state->user_to_session[user_id] = session;
            state->session_to_user[session.get()] = user_id;
          }

          if (old && old.get() != session.get()) {
            chirp::auth::KickNotify kick;
            kick.set_reason("login from another device");

            chirp::gateway::Packet kick_pkt;
            kick_pkt.set_msg_id(chirp::gateway::KICK_NOTIFY);
            kick_pkt.set_sequence(0);
            kick_pkt.set_body(kick.SerializeAsString());

            auto framed = chirp::network::ProtobufFraming::Encode(kick_pkt);
            old->SendAndClose(std::string(reinterpret_cast<const char*>(framed.data()), framed.size()));
          }

          chirp::auth::LoginResponse resp;
          if (user_id.empty()) {
            resp.set_code(chirp::common::INVALID_PARAM);
          } else {
            resp.set_code(chirp::common::OK);
            resp.set_session_id(RandomHex(16));
          }
          resp.set_server_time(NowMs());
          SendPacket(session, chirp::gateway::LOGIN_RESP, pkt.sequence(), resp.SerializeAsString());
          break;
        }
        case chirp::gateway::HEARTBEAT_PING: {
          chirp::gateway::HeartbeatPing ping;
          if (!ping.ParseFromArray(pkt.body().data(), static_cast<int>(pkt.body().size()))) {
            Logger::Instance().Warn("failed to parse HeartbeatPing body");
            return;
          }

          chirp::gateway::HeartbeatPong pong;
          pong.set_timestamp(ping.timestamp());
          pong.set_server_time(NowMs());

          chirp::gateway::Packet resp;
          resp.set_msg_id(chirp::gateway::HEARTBEAT_PONG);
          resp.set_sequence(pkt.sequence());
          resp.set_body(pong.SerializeAsString());

          auto framed = chirp::network::ProtobufFraming::Encode(resp);
          session->Send(std::string(reinterpret_cast<const char*>(framed.data()), framed.size()));
          break;
        }
        default:
          break;
        }
      },
      [state](std::shared_ptr<chirp::network::Session> session) {
        std::lock_guard<std::mutex> lock(state->mu);
        auto it = state->session_to_user.find(session.get());
        if (it == state->session_to_user.end()) {
          return;
        }
        const std::string user_id = it->second;
        state->session_to_user.erase(it);

        auto it2 = state->user_to_session.find(user_id);
        if (it2 != state->user_to_session.end()) {
          auto cur = it2->second.lock();
          if (!cur || cur.get() == session.get()) {
            state->user_to_session.erase(it2);
          }
        }
      });

  server.Start();
  ws_server.Start();

  asio::signal_set signals(io, SIGINT, SIGTERM);
  signals.async_wait([&](const std::error_code& /*ec*/, int /*sig*/) {
    Logger::Instance().Info("shutdown requested");
    server.Stop();
    ws_server.Stop();
    io.stop();
  });

  io.run();
  Logger::Instance().Info("chirp_gateway exited");
  return 0;
}
