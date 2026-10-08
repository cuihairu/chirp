// chirp_game_server_gateway: the trusted service-plane hub. Game backends (and
// internal services such as chat) dial out to this listener, authenticate
// with a service_id + secret, and exchange injections and events. There are
// no user-session semantics here; see docs/architecture.md (server plane).
#include <algorithm>
#include <chrono>
#include <cstdlib>
#include <ctime>
#include <memory>
#include <optional>
#include <string>
#include <unordered_map>
#include <utility>
#include <vector>

#ifdef _WIN32
#include <process.h>
#define getpid _getpid  // MSVC 的 unistd.h 等价物是 process.h 的 _getpid
#else
#include <unistd.h>
#endif

#include <asio.hpp>

#include "common/metrics.h"
#include "common/metrics_http_server.h"
#include "common/crash_handler.h"
#include "event_queue.h"
#include "logger.h"
#include "network/protobuf_framing.h"
#include "network/session.h"
#include "network/tcp_server.h"
#include "proto/common.pb.h"
#include "proto/gateway.pb.h"
#include "proto/game_server_gateway.pb.h"
#include "server_gateway_handlers.h"
#include "service_registry.h"
#include "stream_broker.h"

namespace {

int64_t NowMs() {
  using namespace std::chrono;
  return duration_cast<milliseconds>(system_clock::now().time_since_epoch()).count();
}

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

using Session = chirp::network::Session;
namespace sg = chirp::game_server_gateway;
using chirp::common::Logger;

// Bridges the abstract PeerSender to a live session. The session is held
// weakly: ownership stays with the server's callback chain.
class SessionPeerSender : public chirp::game_server_gateway::PeerSender {
 public:
  explicit SessionPeerSender(std::weak_ptr<Session> session) : session_(std::move(session)) {}

  bool Send(chirp::gateway::MsgID msg_id, const google::protobuf::Message& body) override {
    auto session = session_.lock();
    if (!session || session->IsClosed()) {
      return false;
    }
    chirp::gateway::Packet pkt;
    pkt.set_msg_id(msg_id);
    pkt.set_sequence(0);  // server-initiated frames carry no request sequence
    // request_id:「连接内生成」兜底(Packet 缺省=0)——服务面出站包没有上
    // 游请求可透传,按连接自增填单调值,供接收侧跨进程日志关联;业务侧
    // 请求若自带 request_id,应在构造出站包时显式 set(见 HandleInject)。
    const int64_t request_id = next_request_id_++;
    pkt.set_request_id(request_id);
    pkt.set_body(body.SerializeAsString());
    auto framed = chirp::network::ProtobufFraming::Encode(pkt);
    // 注入出站记 req,与 chat 侧「inject received ... req=" 日志互相
    // 关联,跨进程追踪请求出自哪条服务连接。
    if (msg_id == chirp::gateway::INJECT_MESSAGE_NOTIFY) {
      chirp::common::Logger::Instance().Info(
          "inject forwarded req=" + std::to_string(request_id) + " to chat");
    }
    session->Send(std::string(reinterpret_cast<const char*>(framed.data()), framed.size()));
    return true;
  }

 private:
  // 单连接内单调(仅主 io 线程回调链访问,无需原子)。
  int64_t next_request_id_{1};
  std::weak_ptr<Session> session_;
};

struct PeerContext {
  std::shared_ptr<Session> session;
  std::shared_ptr<SessionPeerSender> sender;
  std::string service_id;  // empty until authenticated
  bool authenticated = false;
  std::unique_ptr<asio::steady_timer> deadline;
};

void SendPacket(const std::shared_ptr<Session>& session, chirp::gateway::MsgID msg_id, int64_t seq,
                const google::protobuf::Message& body) {
  chirp::gateway::Packet pkt;
  pkt.set_msg_id(msg_id);
  pkt.set_sequence(seq);
  pkt.set_body(body.SerializeAsString());
  auto framed = chirp::network::ProtobufFraming::Encode(pkt);
  session->Send(std::string(reinterpret_cast<const char*>(framed.data()), framed.size()));
}

template <typename T>
bool ParseBody(const chirp::gateway::Packet& pkt, T* out) {
  return out->ParseFromArray(pkt.body().data(), static_cast<int>(pkt.body().size()));
}

// Per-run peer table plus the frame/disconnect/deadline handlers. This used
// to be main()'s lambda graph; hoisted verbatim (batch 15, behavior-
// preserving) so the unit tests can drive it with in-memory sessions. The
// lifetime matches the old captures: the runtime is constructed on main()'s
// stack after the config/io/handlers it references, and the server callbacks
// (bound later) are destroyed before it.
struct GatewayRuntime {
  GatewayRuntime(asio::io_context& io, const sg::ServerGatewayConfig& config,
                 sg::ServerGatewayHandlers& handlers, int auth_timeout)
      : io_(io), config_(config), handlers_(handlers), auth_timeout_(auth_timeout) {}

  void OnDisconnect(const Session* key) {
    auto it = peers.find(key);
    if (it == peers.end()) {
      return;
    }
    if (!it->second.service_id.empty()) {
      handlers_.OnPeerDisconnected(it->second.service_id, it->second.sender.get());
      // Pair with the inc at auth success; the early return above keeps the
      // disconnect paths (close/deadline/replace) from double-decrementing.
      CHIRP_GAUGE_DEC("chirp_server_gateway_sessions");
    }
    if (it->second.deadline) {
      it->second.deadline->cancel();
    }
    peers.erase(it);
  }

  void ArmDeadline(const Session* key, bool authenticated) {
    auto it = peers.find(key);
    if (it == peers.end()) {
      return;
    }
    if (!it->second.deadline) {
      it->second.deadline = std::make_unique<asio::steady_timer>(io_);
    }
    auto& timer = *it->second.deadline;
    timer.cancel();
    const int ttl = authenticated
                        ? std::max(2, 2 * config_.heartbeat_interval_seconds)
                        : std::max(1, auth_timeout_);
    timer.expires_after(std::chrono::seconds(ttl));
    timer.async_wait([this, key](const std::error_code& ec) {
      if (ec) {
        return;  // refreshed, removed, or the peer went away on its own
      }
      Logger::Instance().Warn("server-plane peer timed out; closing");
      std::shared_ptr<Session> session;
      auto it = peers.find(key);
      if (it != peers.end()) {
        session = it->second.session;
      }
      OnDisconnect(key);
      if (session) {
        session->Close();
      }
    });
  }

  void OnFrame(std::shared_ptr<Session> session, std::string&& payload) {
    chirp::gateway::Packet pkt;
    if (!pkt.ParseFromArray(payload.data(), static_cast<int>(payload.size()))) {
      Logger::Instance().Warn("failed to parse Packet from server-plane peer");
      return;
    }
    CHIRP_COUNTER("chirp_server_gateway_packets_total", 1);

    const Session* key = session.get();
    if (peers.find(key) == peers.end()) {
      PeerContext ctx;
      ctx.session = session;
      ctx.sender = std::make_shared<SessionPeerSender>(session);
      peers.emplace(key, std::move(ctx));
    }
    auto& ctx = peers.at(key);

    if (!ctx.authenticated) {
      if (pkt.msg_id() != chirp::gateway::SERVER_AUTH_REQ) {
        Logger::Instance().Warn("first frame from a peer was not SERVER_AUTH_REQ; closing");
        OnDisconnect(key);
        session->Close();
        return;
      }

      sg::ServerAuthRequest req;
      sg::ServerAuthResponse resp;
      if (!ParseBody(pkt, &req)) {
        resp.set_code(chirp::common::INVALID_PARAM);
        SendPacket(session, chirp::gateway::SERVER_AUTH_RESP, pkt.sequence(), resp);
        OnDisconnect(key);
        session->Close();
        return;
      }

      const auto out = handlers_.HandleAuth(req, ctx.sender);
      resp.set_code(out.code);
      resp.set_server_time_ms(NowMs());
      resp.set_heartbeat_interval_seconds(out.heartbeat_interval_seconds);
      SendPacket(session, chirp::gateway::SERVER_AUTH_RESP, pkt.sequence(), resp);

      if (out.code != chirp::common::OK) {
        Logger::Instance().Warn("server-plane auth failed for service_id=" + req.service_id());
        OnDisconnect(key);
        session->Close();
        return;
      }

      ctx.service_id = out.service_id;
      ctx.authenticated = true;
      // Authenticated service-plane connections gauge: service_id is set
      // exactly once here and cleared by nobody - OnDisconnect is the only
      // place it reads non-empty (see the dec below), so inc/dec pair up.
      CHIRP_GAUGE_INC("chirp_server_gateway_sessions");
      Logger::Instance().Info("service authenticated: " + out.service_id);

      if (out.replaced_peer) {
        // The service reconnected; find and drop the displaced connection.
        const Session* stale = nullptr;
        std::shared_ptr<Session> stale_session;
        for (const auto& [other_key, other] : peers) {
          if (other.sender.get() == out.replaced_peer.get()) {
            stale = other_key;
            stale_session = other.session;
            break;
          }
        }
        if (stale != nullptr) {
          Logger::Instance().Info("closing displaced connection of service " + out.service_id);
          OnDisconnect(stale);
          if (stale_session) {
            stale_session->Close();
          }
        }
      }
    } else {
      switch (pkt.msg_id()) {
      case chirp::gateway::SERVER_HEARTBEAT_PING: {
        sg::ServerHeartbeatPing ping;
        if (!ParseBody(pkt, &ping)) {
          Logger::Instance().Warn("failed to parse ServerHeartbeatPing body");
          break;
        }
        const auto pong = handlers_.HandleHeartbeat(ping);
        SendPacket(session, chirp::gateway::SERVER_HEARTBEAT_PONG, pkt.sequence(), pong);
        break;
      }
      case chirp::gateway::INJECT_MESSAGE_REQ: {
        sg::MessageInjectRequest req;
        sg::MessageInjectResponse resp;
        if (!ParseBody(pkt, &req)) {
          resp.set_code(chirp::common::INVALID_PARAM);
        } else {
          resp = handlers_.HandleInject(req);
        }
        SendPacket(session, chirp::gateway::INJECT_MESSAGE_RESP, pkt.sequence(), resp);
        break;
      }
      case chirp::gateway::EVENT_PUBLISH_REQ: {
        sg::EventPublishRequest req;
        sg::EventPublishResponse resp;
        if (!ParseBody(pkt, &req)) {
          resp.set_code(chirp::common::INVALID_PARAM);
        } else {
          resp = handlers_.HandleEventPublish(req);
        }
        SendPacket(session, chirp::gateway::EVENT_PUBLISH_RESP, pkt.sequence(), resp);
        break;
      }
      case chirp::gateway::EVENT_ACK_REQ: {
        sg::EventAckRequest req;
        sg::EventAckResponse resp;
        if (!ParseBody(pkt, &req)) {
          resp.set_code(chirp::common::INVALID_PARAM);
        } else {
          resp = handlers_.HandleEventAck(req, ctx.service_id);
        }
        SendPacket(session, chirp::gateway::EVENT_ACK_RESP, pkt.sequence(), resp);
        break;
      }
      default:
        // Unknown/unimplemented server-plane messages are ignored. The
        // WP-8 player-directory block (5013-5030) is served by app_chat's
        // main port now, not this hub.
        break;
      }
    }

    ArmDeadline(key, ctx.authenticated);
  }

  void OnClose(const std::shared_ptr<Session>& session) { OnDisconnect(session.get()); }

  asio::io_context& io_;
  const sg::ServerGatewayConfig& config_;
  sg::ServerGatewayHandlers& handlers_;
  const int auth_timeout_;
  // Everything runs on the single io thread; the peers map needs no lock.
  std::unordered_map<const Session*, PeerContext> peers;
};

}  // namespace

int main(int argc, char** argv) {
  // 崩溃采集:main 首条语句,早于一切 flag/日志初始化(见 CRASH_COLLECTION.md)。
  chirp::common::crash::Initialize(argc, argv);
  Logger::Instance().SetLevel(Logger::Level::kInfo);

  const uint16_t port = ParseU16Arg(argc, argv, "--port", 8100);
  const int auth_timeout = std::atoi(GetArg(argc, argv, "--auth_timeout", "10").c_str());
  const uint16_t metrics_port = ParseU16Arg(argc, argv, "--metrics_port", 0);

  sg::ServerGatewayConfig config;
  config.chat_service_id = GetArg(argc, argv, "--chat_service_id", "chat");
  config.heartbeat_interval_seconds =
      std::atoi(GetArg(argc, argv, "--heartbeat_interval", "30").c_str());
  config.max_pending_events_per_service =
      static_cast<size_t>(std::atoi(GetArg(argc, argv, "--max_pending", "1000").c_str()));
  // Repeatable: --service <service_id>=<secret>
  for (int i = 1; i < argc; i++) {
    if (argv[i] == std::string("--service") && i + 1 < argc) {
      const std::string pair = argv[i + 1];
      const auto sep = pair.find('=');
      if (sep != std::string::npos) {
        config.service_secrets[pair.substr(0, sep)] = pair.substr(sep + 1);
      } else {
        Logger::Instance().Warn("ignoring malformed --service entry: " + pair);
      }
    }
  }
  if (config.service_secrets.empty()) {
    Logger::Instance().Warn("no --service id=secret configured; every auth attempt will be rejected");
  }

  Logger::Instance().Info("chirp_game_server_gateway starting port=" + std::to_string(port) +
                          " services=" + std::to_string(config.service_secrets.size()) +
                          " chat_service=" + config.chat_service_id);

  asio::io_context io;

  sg::ServiceRegistry registry;
  sg::EventQueue queue(config.max_pending_events_per_service);
  sg::ServerGatewayHandlers handlers(config, registry, queue);

  // Optional Redis Streams intake for game backends that cannot host a
  // long-connection client (empty --broker_redis_host disables it).
  sg::StreamBrokerConfig broker_config;
  broker_config.redis_host = GetArg(argc, argv, "--broker_redis_host", "");
  broker_config.redis_port = ParseU16Arg(argc, argv, "--broker_redis_port", 6379);
  broker_config.stream = GetArg(argc, argv, "--broker_stream", "chirp:server_plane:inject");
  broker_config.group = GetArg(argc, argv, "--broker_group", "chirp-plane");
  broker_config.consumer = GetArg(argc, argv, "--broker_consumer", "");
  broker_config.claim_min_idle_ms =
      std::atoi(GetArg(argc, argv, "--broker_claim_min_idle_ms", "30000").c_str());
  broker_config.service_secrets = config.service_secrets;
  if (broker_config.consumer.empty()) {
    char hostname[256] = {0};
    if (gethostname(hostname, sizeof(hostname) - 1) != 0) {
      std::strncpy(hostname, "unknown", sizeof(hostname) - 1);
    }
    broker_config.consumer = std::string(hostname) + ":" + std::to_string(getpid());
  }

  std::unique_ptr<sg::StreamBrokerConsumer> broker;
  if (!broker_config.redis_host.empty()) {
    broker = std::make_unique<sg::StreamBrokerConsumer>(
        broker_config, [&handlers](const sg::MessageInjectRequest& req) {
          return handlers.HandleInject(req);
        });
    broker->Start();
    Logger::Instance().Info("stream broker enabled: stream=" + broker_config.stream +
                            " group=" + broker_config.group +
                            " consumer=" + broker_config.consumer);
  }

  // Everything runs on the single io thread; the peers map needs no lock.
  GatewayRuntime rt(io, config, handlers, auth_timeout);

  chirp::network::TcpServer server(
      io, port,
      [&rt](std::shared_ptr<Session> session, std::string&& payload) {
        rt.OnFrame(std::move(session), std::move(payload));
      },
      [&rt](std::shared_ptr<Session> session) { rt.OnClose(session); });
  server.Start();

  // Minimal metrics endpoint, opt-in via --metrics_port (0 = off; default
  // keeps the listening surface unchanged). Declared after io so it dies
  // first; a failed bind logs and the hub continues without metrics.
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
    if (broker) {
      broker->Stop();
    }
    server.Stop();
    if (metrics_server) {
      metrics_server->Stop();
    }
    io.stop();
  });

  io.run();
  Logger::Instance().Info("chirp_game_server_gateway exited");
  return 0;
}
