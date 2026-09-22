// chirp_game_server_gateway: the trusted service-plane hub. Game backends (and
// internal services such as chat) dial out to this listener, authenticate
// with a service_id + secret, and exchange injections and events. There are
// no user-session semantics here; see docs/architecture.md (server plane).
#include <algorithm>
#include <chrono>
#include <cstdlib>
#include <ctime>
#include <memory>
#include <string>
#include <unordered_map>
#include <utility>
#include <vector>

#include <unistd.h>

#include <asio.hpp>

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
    pkt.set_body(body.SerializeAsString());
    auto framed = chirp::network::ProtobufFraming::Encode(pkt);
    session->Send(std::string(reinterpret_cast<const char*>(framed.data()), framed.size()));
    return true;
  }

 private:
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

}  // namespace

int main(int argc, char** argv) {
  using chirp::common::Logger;
  namespace sg = chirp::game_server_gateway;

  Logger::Instance().SetLevel(Logger::Level::kInfo);

  const uint16_t port = ParseU16Arg(argc, argv, "--port", 8100);
  const int auth_timeout = std::atoi(GetArg(argc, argv, "--auth_timeout", "10").c_str());

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
  std::unordered_map<const Session*, PeerContext> peers;

  const auto on_disconnect = [&](const Session* key) {
    auto it = peers.find(key);
    if (it == peers.end()) {
      return;
    }
    if (!it->second.service_id.empty()) {
      handlers.OnPeerDisconnected(it->second.service_id, it->second.sender.get());
    }
    if (it->second.deadline) {
      it->second.deadline->cancel();
    }
    peers.erase(it);
  };

  const auto arm_deadline = [&](const Session* key, bool authenticated) {
    auto it = peers.find(key);
    if (it == peers.end()) {
      return;
    }
    if (!it->second.deadline) {
      it->second.deadline = std::make_unique<asio::steady_timer>(io);
    }
    auto& timer = *it->second.deadline;
    timer.cancel();
    const int ttl = authenticated
                        ? std::max(2, 2 * config.heartbeat_interval_seconds)
                        : std::max(1, auth_timeout);
    timer.expires_after(std::chrono::seconds(ttl));
    timer.async_wait([&, key](const std::error_code& ec) {
      if (ec) {
        return;  // refreshed, removed, or the peer went away on its own
      }
      Logger::Instance().Warn("server-plane peer timed out; closing");
      std::shared_ptr<Session> session;
      auto it = peers.find(key);
      if (it != peers.end()) {
        session = it->second.session;
      }
      on_disconnect(key);
      if (session) {
        session->Close();
      }
    });
  };

  const auto on_frame = [&](std::shared_ptr<Session> session, std::string&& payload) {
    chirp::gateway::Packet pkt;
    if (!pkt.ParseFromArray(payload.data(), static_cast<int>(payload.size()))) {
      Logger::Instance().Warn("failed to parse Packet from server-plane peer");
      return;
    }

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
        on_disconnect(key);
        session->Close();
        return;
      }

      sg::ServerAuthRequest req;
      sg::ServerAuthResponse resp;
      if (!ParseBody(pkt, &req)) {
        resp.set_code(chirp::common::INVALID_PARAM);
        SendPacket(session, chirp::gateway::SERVER_AUTH_RESP, pkt.sequence(), resp);
        on_disconnect(key);
        session->Close();
        return;
      }

      const auto out = handlers.HandleAuth(req, ctx.sender);
      resp.set_code(out.code);
      resp.set_server_time_ms(NowMs());
      resp.set_heartbeat_interval_seconds(out.heartbeat_interval_seconds);
      SendPacket(session, chirp::gateway::SERVER_AUTH_RESP, pkt.sequence(), resp);

      if (out.code != chirp::common::OK) {
        Logger::Instance().Warn("server-plane auth failed for service_id=" + req.service_id());
        on_disconnect(key);
        session->Close();
        return;
      }

      ctx.service_id = out.service_id;
      ctx.authenticated = true;
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
          on_disconnect(stale);
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
        const auto pong = handlers.HandleHeartbeat(ping);
        SendPacket(session, chirp::gateway::SERVER_HEARTBEAT_PONG, pkt.sequence(), pong);
        break;
      }
      case chirp::gateway::INJECT_MESSAGE_REQ: {
        sg::MessageInjectRequest req;
        sg::MessageInjectResponse resp;
        if (!ParseBody(pkt, &req)) {
          resp.set_code(chirp::common::INVALID_PARAM);
        } else {
          resp = handlers.HandleInject(req);
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
          resp = handlers.HandleEventPublish(req);
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
          resp = handlers.HandleEventAck(req, ctx.service_id);
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

    arm_deadline(key, ctx.authenticated);
  };

  const auto on_close = [&](std::shared_ptr<Session> session) { on_disconnect(session.get()); };

  chirp::network::TcpServer server(io, port, on_frame, on_close);
  server.Start();

  asio::signal_set signals(io, SIGINT, SIGTERM);
  signals.async_wait([&](const std::error_code& /*ec*/, int /*sig*/) {
    Logger::Instance().Info("shutdown requested");
    if (broker) {
      broker->Stop();
    }
    server.Stop();
    io.stop();
  });

  io.run();
  Logger::Instance().Info("chirp_game_server_gateway exited");
  return 0;
}
