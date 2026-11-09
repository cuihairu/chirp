// NOTE: never include presence_manager.h from this TU. It declares its own
// chirp::social::PresenceStatus enum that collides with the proto-generated
// one used below.

#include <algorithm>
#include <chrono>
#include <cstdint>
#include <cstdlib>
#include <memory>
#include <mutex>
#include <optional>
#include <random>
#include <set>
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
#include "network/redis_client.h"
#include "network/session.h"
#include "network/device_presence.h"
#include "network/session_registry.h"
#include "network/tcp_server.h"
#include "network/websocket_server.h"
#include "proto/auth.pb.h"
#include "proto/chat.pb.h"
#include "proto/common.pb.h"
#include "proto/gateway.pb.h"
#include "proto/game_server_gateway.pb.h"
#include "proto/social.pb.h"

#include "social_dispatch.h"

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

}  // namespace
namespace {
using chirp::social::direct::ConsumeGamePresenceEvent;
using chirp::social::direct::HandleDisconnect;
using chirp::social::direct::HandlePacket;
using chirp::social::direct::LoadSocialState;
using chirp::social::direct::SocialState;
}  // namespace

int main(int argc, char** argv) {
  // 崩溃采集:main 首条语句,早于一切 flag/日志初始化(见 CRASH_COLLECTION.md)。
  chirp::common::crash::Initialize(argc, argv);
  using chirp::common::Logger;

  Logger::Instance().SetLevel(Logger::Level::kInfo);
  const uint16_t port = ParseU16Arg(argc, argv, "--port", 8000);
  const std::string host = GetArg(argc, argv, "--host", "0.0.0.0");

  const uint16_t ws_port = ParseU16Arg(argc, argv, "--ws_port", static_cast<uint16_t>(port + 1));
  const std::string redis_host = GetArg(argc, argv, "--redis_host", "");
  const uint16_t redis_port = ParseU16Arg(argc, argv, "--redis_port", 6379);
  const std::string token_secret = GetArg(argc, argv, "--token_secret", "");
  const uint16_t metrics_port = ParseU16Arg(argc, argv, "--metrics_port", 0);

  Logger::Instance().Info("chirp_social starting tcp=" + std::to_string(port) + " ws=" + std::to_string(ws_port) +
                          (redis_host.empty() ? "" : (" redis=" + redis_host + ":" + std::to_string(redis_port))) +
                          (token_secret.empty() ? " login=scaffold" : " login=jwt"));

  asio::io_context io;

  std::shared_ptr<chirp::network::RedisClient> redis;
  if (!redis_host.empty()) {
    redis = std::make_shared<chirp::network::RedisClient>(redis_host, redis_port);
  }

  const chirp::common::LoginTokenVerifier token_verifier(token_secret);

  auto state = std::make_shared<SocialState>();
  LoadSocialState(redis, state);

  // 游戏在线状态 (chat plane -> friends)：订阅 chat 派生的上下线事件，把
  // IN_GAME / 回落状态推给好友。SUBSCRIBE 必须在连接建立后发（Start 之前
  // socket 未开，是静默 no-op，见 RedisSubscriber 契约），所以挂 connect
  // 回调。没配 Redis 时 overlay 永远为空，GET_PRESENCE 退化为基础状态。
  std::unique_ptr<chirp::network::RedisSubscriber> presence_subscriber;
  if (redis) {
    presence_subscriber = std::make_unique<chirp::network::RedisSubscriber>(redis_host, redis_port);
    presence_subscriber->SetConnectCallback([sub = presence_subscriber.get()] {
      if (sub->Subscribe("chirp:game_presence:events")) {
        Logger::Instance().Info("social: subscribed to game presence events");
      }
    });
    presence_subscriber->SetMessageCallback(
        [state](const std::string& channel, const std::string& payload) {
          ConsumeGamePresenceEvent(state, channel, payload);
        });
    presence_subscriber->Start();
  }

  auto on_packet = [state, redis, &token_verifier](std::shared_ptr<chirp::network::Session> session,
                                                   std::string&& payload) {
    HandlePacket(state, redis, &token_verifier, session, std::move(payload));
  };
  auto on_disconnect = [state, redis](std::shared_ptr<chirp::network::Session> session) {
    HandleDisconnect(state, redis, session);
  };

  chirp::network::TcpServer server(io, port, on_packet, on_disconnect, host);
  chirp::network::WebSocketServer ws_server(io, ws_port, on_packet, on_disconnect, host);

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
  Logger::Instance().Info("chirp_social exited");
  return 0;
}
