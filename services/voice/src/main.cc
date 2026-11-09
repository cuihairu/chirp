#include <array>
#include <chrono>
#include <cstdint>
#include <cstdlib>
#include <memory>
#include <mutex>
#include <optional>
#include <random>
#include <string>
#include <unordered_map>
#include <unordered_set>
#include <vector>

#include <asio.hpp>

#include "common/login_token_verifier.h"
#include "common/metrics.h"
#include "common/metrics_http_server.h"
#include "common/crash_handler.h"
#include "logger.h"
#include "network/protobuf_framing.h"
#include "network/session.h"
#include "network/tcp_server.h"
#include "network/websocket_server.h"
#include "proto/auth.pb.h"
#include "proto/common.pb.h"
#include "proto/gateway.pb.h"
#include "proto/voice.pb.h"

#include "voice_dispatch.h"


namespace {

std::string GetArg(int argc, char** argv, const std::string& key, const std::string& def) {
  for (int i = 1; i < argc; i++) {
    if (argv[i] == key && i + 1 < argc) {
      return argv[i + 1];
    }
  }
  return def;
}

uint16_t ParseU16Arg(int argc, char** argv, const std::string& key, uint16_t def) {
  return static_cast<uint16_t>(std::atoi(GetArg(argc, argv, key, std::to_string(def)).c_str()));
}

// 拆分:包处理与房间/心跳状态在 voice_dispatch.{h,cc}(chirp::voice::direct);
// 这里 re-export 供 main 与直驱单测使用。
using chirp::voice::direct::HandleDisconnect;
using chirp::voice::direct::HandlePacket;
using chirp::voice::direct::Base64Encode;
using chirp::voice::direct::HmacSha1;
using chirp::voice::direct::MakeTurnCredential;
using chirp::voice::direct::MakeTurnUsername;
using chirp::voice::direct::NowMs;
using chirp::voice::direct::Sha1;
using chirp::voice::direct::ScheduleHeartbeatSweep;
using chirp::voice::direct::SweepHeartbeatTimeout;
using chirp::voice::direct::VoiceState;

}  // namespace

int main(int argc, char** argv) {
  // 崩溃采集:main 首条语句,早于一切 flag/日志初始化(见 CRASH_COLLECTION.md)。
  chirp::common::crash::Initialize(argc, argv);
  using chirp::common::Logger;

  Logger::Instance().SetLevel(Logger::Level::kInfo);
  const uint16_t port = ParseU16Arg(argc, argv, "--port", 9000);
  const std::string host = GetArg(argc, argv, "--host", "0.0.0.0");

  const uint16_t ws_port = ParseU16Arg(argc, argv, "--ws_port", static_cast<uint16_t>(port + 1));
  const std::string token_secret = GetArg(argc, argv, "--token_secret", "");
  const std::string turn_uri = GetArg(argc, argv, "--turn_uri", "");
  const std::string turn_secret = GetArg(argc, argv, "--turn_static_secret", "");
  const int64_t turn_ttl =
      std::atoll(GetArg(argc, argv, "--turn_credential_ttl_seconds", "86400").c_str());
  const int64_t heartbeat_timeout =
      std::atoll(GetArg(argc, argv, "--heartbeat_timeout_ms", "75000").c_str());
  const uint16_t metrics_port = ParseU16Arg(argc, argv, "--metrics_port", 0);

  if (!turn_uri.empty() && turn_secret.empty()) {
    Logger::Instance().Warn(
        "--turn_uri is set without --turn_static_secret: only STUN access will "
        "be advertised (TURN relays need credentials)");
  }

  const chirp::common::LoginTokenVerifier token_verifier(token_secret);

  Logger::Instance().Info(
      "chirp_voice starting tcp=" + std::to_string(port) + " ws=" + std::to_string(ws_port) +
      " auth=" + (token_verifier.enabled() ? "hmac-sha256" : "scaffold") +
      " turn=" + (turn_uri.empty() ? "off" : "on") +
      " heartbeat_timeout=" + std::to_string(heartbeat_timeout) + "ms");

  asio::io_context io;

  auto state = std::make_shared<VoiceState>();
  state->cfg.verifier = &token_verifier;
  state->cfg.turn_uri = turn_uri;
  state->cfg.turn_secret = turn_secret;
  state->cfg.turn_credential_ttl_seconds = turn_ttl;
  state->cfg.heartbeat_timeout_ms = heartbeat_timeout;

  chirp::network::TcpServer server(
      io, port,
      [state](std::shared_ptr<chirp::network::Session> session, std::string&& payload) {
        HandlePacket(state, session, std::move(payload));
      },
      [state](std::shared_ptr<chirp::network::Session> session) { HandleDisconnect(state, session); },
      host);

  chirp::network::WebSocketServer ws_server(
      io, ws_port,
      [state](std::shared_ptr<chirp::network::Session> session, std::string&& payload) {
        HandlePacket(state, session, std::move(payload));
      },
      [state](std::shared_ptr<chirp::network::Session> session) { HandleDisconnect(state, session); },
      host);

  server.Start();
  ws_server.Start();

  // Minimal metrics endpoint, opt-in via --metrics_port (0 = off; default
  // keeps the listening surface unchanged). Declared after io so it dies
  // first; a failed bind logs and the service continues without metrics.
  std::optional<chirp::common::MetricsHttpServer> metrics_server;
  if (metrics_port > 0) {
    metrics_server.emplace(io, metrics_port);
    if (metrics_server->Start()) {
      Logger::Instance().Info("Metrics endpoint listening on TCP:" +
                              std::to_string(metrics_port) + " (/metrics)");
    } else {
      Logger::Instance().Warn("Metrics endpoint failed to bind TCP:" +
                              std::to_string(metrics_port) + "; continuing without metrics");
      metrics_server.reset();
    }
  }

  if (state->cfg.heartbeat_timeout_ms > 0) {
    auto sweep_timer = std::make_shared<asio::steady_timer>(io);
    ScheduleHeartbeatSweep(sweep_timer, state);
  }

  asio::signal_set signals(io, SIGINT, SIGTERM);
  signals.async_wait([&](const std::error_code& /*ec*/, int /*sig*/) {
    Logger::Instance().Info("shutdown requested");
    server.Stop();
    ws_server.Stop();
    if (metrics_server) {
      metrics_server->Stop();
    }
    io.stop();
  });

  io.run();
  Logger::Instance().Info("chirp_voice exited");
  return 0;
}
