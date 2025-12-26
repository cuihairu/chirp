#include <chrono>
#include <cstdint>
#include <cstdlib>
#include <optional>
#include <random>
#include <string>

#include <asio.hpp>

#include "common/jwt.h"
#include "common/logger.h"
#include "network/protobuf_framing.h"
#include "network/session.h"
#include "network/tcp_server.h"
#include "proto/auth.pb.h"
#include "proto/common.pb.h"
#include "proto/gateway.pb.h"

namespace {

int64_t NowMs() {
  using namespace std::chrono;
  return std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::system_clock::now().time_since_epoch())
      .count();
}

uint16_t ParsePort(int argc, char** argv) {
  uint16_t port = 6000;
  for (int i = 1; i < argc; i++) {
    std::string a = argv[i];
    if ((a == "--port" || a == "-p") && i + 1 < argc) {
      port = static_cast<uint16_t>(std::atoi(argv[i + 1]));
      i++;
    }
  }
  return port;
}

std::string GetArg(int argc, char** argv, const std::string& key, const std::string& def) {
  for (int i = 1; i < argc; i++) {
    if (argv[i] == key && i + 1 < argc) {
      return argv[i + 1];
    }
  }
  return def;
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

bool LooksLikeJwt(std::string_view token) {
  const size_t dot1 = token.find('.');
  if (dot1 == std::string_view::npos) {
    return false;
  }
  const size_t dot2 = token.find('.', dot1 + 1);
  return dot2 != std::string_view::npos;
}

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
  const std::string jwt_secret = GetArg(argc, argv, "--jwt_secret", "dev_secret");
  Logger::Instance().Info("chirp_auth starting on port " + std::to_string(port));

  asio::io_context io;

  chirp::network::TcpServer server(
      io, port,
      [jwt_secret](std::shared_ptr<chirp::network::Session> session, std::string&& payload) {
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

          std::string user_id;
          if (LooksLikeJwt(req.token())) {
            chirp::common::JwtClaims claims;
            std::string err;
            if (!chirp::common::JwtVerifyHS256(req.token(), jwt_secret, &claims, &err)) {
              chirp::auth::LoginResponse resp;
              resp.set_code(chirp::common::AUTH_FAILED);
              resp.set_server_time(NowMs());
              SendPacket(session, chirp::gateway::LOGIN_RESP, pkt.sequence(), resp.SerializeAsString());
              return;
            }
            user_id = claims.subject;
          } else {
            // Scaffolding fallback: treat token as user id.
            user_id = req.token();
          }

          chirp::auth::LoginResponse resp;
          if (user_id.empty()) {
            resp.set_code(chirp::common::INVALID_PARAM);
          } else {
            resp.set_code(chirp::common::OK);
            resp.set_user_id(user_id);
            resp.set_session_id(RandomHex(16));
            resp.set_kick_previous(true);
            resp.mutable_kick()->set_reason("login from another device");
          }
          resp.set_server_time(NowMs());
          SendPacket(session, chirp::gateway::LOGIN_RESP, pkt.sequence(), resp.SerializeAsString());
          break;
        }
        case chirp::gateway::LOGOUT_REQ: {
          chirp::auth::LogoutRequest req;
          if (!req.ParseFromArray(pkt.body().data(), static_cast<int>(pkt.body().size()))) {
            chirp::auth::LogoutResponse resp;
            resp.set_code(chirp::common::INVALID_PARAM);
            resp.set_server_time(NowMs());
            SendPacket(session, chirp::gateway::LOGOUT_RESP, pkt.sequence(), resp.SerializeAsString());
            return;
          }

          chirp::auth::LogoutResponse resp;
          if (req.user_id().empty() || req.session_id().empty()) {
            resp.set_code(chirp::common::INVALID_PARAM);
          } else {
            resp.set_code(chirp::common::OK);
          }
          resp.set_server_time(NowMs());
          SendPacket(session, chirp::gateway::LOGOUT_RESP, pkt.sequence(), resp.SerializeAsString());
          break;
        }
        default:
          break;
        }
      });

  server.Start();

  asio::signal_set signals(io, SIGINT, SIGTERM);
  signals.async_wait([&](const std::error_code& /*ec*/, int /*sig*/) {
    Logger::Instance().Info("shutdown requested");
    server.Stop();
    io.stop();
  });

  io.run();
  Logger::Instance().Info("chirp_auth exited");
  return 0;
}

