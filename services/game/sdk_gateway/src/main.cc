#include <chrono>
#include <cstdint>
#include <cstdlib>
#include <mutex>
#include <optional>
#include <random>
#include <string>
#include <unordered_map>

#include <asio.hpp>

#include "network/auth_client.h"
#include "network/session_registry.h"
#include "network/chat_bridge.h"
#include "logger.h"
#include "network/protobuf_framing.h"
#include "network/redis_session_manager.h"
#include "network/session.h"
#include "network/tcp_server.h"
#include "network/websocket_server.h"
#include "proto/auth.pb.h"
#include "proto/common.pb.h"
#include "proto/gateway.pb.h"

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

void SendPacketAndClose(const std::shared_ptr<chirp::network::Session>& session,
                        chirp::gateway::MsgID msg_id,
                        int64_t seq,
                        const std::string& body) {
  chirp::gateway::Packet pkt;
  pkt.set_msg_id(msg_id);
  pkt.set_sequence(seq);
  pkt.set_body(body);
  auto framed = chirp::network::ProtobufFraming::Encode(pkt);
  session->SendAndClose(std::string(reinterpret_cast<const char*>(framed.data()), framed.size()));
}

void KickSession(const std::shared_ptr<chirp::network::Session>& session, const std::string& reason) {
  chirp::auth::KickNotify kick;
  kick.set_reason(reason.empty() ? "kicked" : reason);

  chirp::gateway::Packet kick_pkt;
  kick_pkt.set_msg_id(chirp::gateway::KICK_NOTIFY);
  kick_pkt.set_sequence(0);
  kick_pkt.set_body(kick.SerializeAsString());

  auto framed = chirp::network::ProtobufFraming::Encode(kick_pkt);
  session->SendAndClose(std::string(reinterpret_cast<const char*>(framed.data()), framed.size()));
}

void HandleLogin(const std::shared_ptr<chirp::network::Session>& session,
                 const chirp::gateway::Packet& pkt,
                 const chirp::auth::LoginRequest& req,
                 const std::shared_ptr<chirp::network::SessionRegistry>& state,
                 const std::shared_ptr<chirp::gateway::AuthClient>& auth,
                 const std::shared_ptr<chirp::gateway::RedisSessionManager>& redis_mgr,
                 chirp::gateway::ChatBridge* bridge) {
  const int64_t seq = pkt.sequence();
  auto send_err = [session, seq](chirp::common::ErrorCode code) {
    chirp::auth::LoginResponse resp;
    resp.set_code(code);
    resp.set_server_time(NowMs());
    SendPacket(session, chirp::gateway::LOGIN_RESP, seq, resp.SerializeAsString());
  };

  if (!auth) {
    chirp::auth::LoginResponse resp;
    resp.set_code(req.token().empty() ? chirp::common::INVALID_PARAM : chirp::common::OK);
    resp.set_server_time(NowMs());
    resp.set_user_id(req.token());
    resp.set_session_id(RandomHex(16));
    resp.set_kick_previous(true);
    resp.mutable_kick()->set_reason("login from another device");
    SendPacket(session, chirp::gateway::LOGIN_RESP, seq, resp.SerializeAsString());
    if (bridge) {
      bridge->Attach(session, req.token(), req.device_id());
    }
    return;
  }

  auth->AsyncLogin(req, seq,
                   [session, seq, req, state, redis_mgr, bridge, send_err](const chirp::auth::LoginResponse& auth_resp) {
    chirp::auth::LoginResponse resp = auth_resp;
    if (resp.code() != chirp::common::OK) {
      SendPacket(session, chirp::gateway::LOGIN_RESP, seq, resp.SerializeAsString());
      return;
    }

    const std::string user_id = resp.user_id().empty() ? req.token() : resp.user_id();
    if (user_id.empty()) {
      send_err(chirp::common::INVALID_PARAM);
      return;
    }

    auto old = chirp::network::BindAuthenticatedSession(state, user_id, resp.session_id(),
                                                        chirp::network::NormalizeDeviceId(req.device_id()),
                                                        session);

    if (old && old.get() != session.get()) {
      const std::string reason = resp.has_kick() ? resp.kick().reason() : "login from another device";
      KickSession(old, reason);
    }

    if (redis_mgr) {
      redis_mgr->AsyncClaim(user_id, req.device_id(),
                            [session, seq, resp, bridge, req](std::optional<std::string> /*prev_owner*/) mutable {
          SendPacket(session, chirp::gateway::LOGIN_RESP, seq, resp.SerializeAsString());
          if (bridge) {
            bridge->Attach(session, req.token(), req.device_id());
          }
        });
    } else {
      SendPacket(session, chirp::gateway::LOGIN_RESP, seq, resp.SerializeAsString());
      if (bridge) {
        bridge->Attach(session, req.token(), req.device_id());
      }
    }
  });
}

void HandleLogout(const std::shared_ptr<chirp::network::Session>& session,
                  const chirp::gateway::Packet& pkt,
                  const chirp::auth::LogoutRequest& req,
                  const std::shared_ptr<chirp::network::SessionRegistry>& state,
                  const std::shared_ptr<chirp::gateway::AuthClient>& auth,
                  const std::shared_ptr<chirp::gateway::RedisSessionManager>& redis_mgr) {
  const int64_t seq = pkt.sequence();
  auto send = [session, seq](chirp::common::ErrorCode code, bool close) {
    chirp::auth::LogoutResponse resp;
    resp.set_code(code);
    resp.set_server_time(NowMs());
    if (close) {
      SendPacketAndClose(session, chirp::gateway::LOGOUT_RESP, seq, resp.SerializeAsString());
    } else {
      SendPacket(session, chirp::gateway::LOGOUT_RESP, seq, resp.SerializeAsString());
    }
  };

  if (req.user_id().empty()) {
    send(chirp::common::INVALID_PARAM, false);
    return;
  }

  const auto current = chirp::network::GetAuthenticatedSession(state, session);
  const std::string& cur_user = current.user_id;
  const std::string& cur_session_id = current.session_id;

  if (cur_user.empty() || cur_user != req.user_id()) {
    send(chirp::common::AUTH_FAILED, false);
    return;
  }
  if (!req.session_id().empty() && !cur_session_id.empty() && req.session_id() != cur_session_id) {
    send(chirp::common::SESSION_EXPIRED, false);
    return;
  }

  auto finalize = [session, seq, req, state, redis_mgr, send](const chirp::auth::LogoutResponse& auth_resp) mutable {
    chirp::auth::LogoutResponse resp = auth_resp;
    if (resp.code() == chirp::common::OK) {
      bool should_release = false;
      std::string removed_user_id;
      std::string removed_device_id;
      should_release = chirp::network::RemoveAuthenticatedSession(state, session, &removed_user_id,
                                                                  &removed_device_id);
      if (should_release && redis_mgr) {
        redis_mgr->AsyncRelease(removed_user_id.empty() ? req.user_id() : removed_user_id,
                                removed_device_id);
      }
      SendPacketAndClose(session, chirp::gateway::LOGOUT_RESP, seq, resp.SerializeAsString());
      return;
    }
    SendPacket(session, chirp::gateway::LOGOUT_RESP, seq, resp.SerializeAsString());
  };

  if (auth) {
    auth->AsyncLogout(req, seq, finalize);
  } else {
    chirp::auth::LogoutResponse resp;
    resp.set_code(chirp::common::OK);
    resp.set_server_time(NowMs());
    finalize(resp);
  }
}

// Single dispatch shared by the TCP and WS entry points (they used to be two
// copy-pasted switches). Chat business packets (2xxx) are piped to chat
// through the bridge when one is configured; unauthenticated 2xxx and every
// other unknown msg_id keep the historical silent-drop behavior.
void HandleClientPacket(const std::shared_ptr<chirp::network::SessionRegistry>& state,
                        const std::shared_ptr<chirp::gateway::AuthClient>& auth,
                        const std::shared_ptr<chirp::gateway::RedisSessionManager>& redis_mgr,
                        chirp::gateway::ChatBridge* bridge,
                        const std::shared_ptr<chirp::network::Session>& session,
                        const chirp::gateway::Packet& pkt) {
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
    HandleLogin(session, pkt, req, state, auth, redis_mgr, bridge);
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
    HandleLogout(session, pkt, req, state, auth, redis_mgr);
    break;
  }
  case chirp::gateway::HEARTBEAT_PING: {
    chirp::gateway::HeartbeatPing ping;
    if (!ping.ParseFromArray(pkt.body().data(), static_cast<int>(pkt.body().size()))) {
      chirp::common::Logger::Instance().Warn("failed to parse HeartbeatPing body");
      return;
    }

    chirp::gateway::HeartbeatPong pong;
    pong.set_timestamp(ping.timestamp());
    pong.set_server_time(NowMs());

    SendPacket(session, chirp::gateway::HEARTBEAT_PONG, pkt.sequence(), pong.SerializeAsString());
    break;
  }
  default: {
    const auto id = static_cast<int>(pkt.msg_id());
    if (bridge != nullptr && id >= 2001 && id <= 2999) {
      if (!chirp::network::GetAuthenticatedSession(state, session).user_id.empty()) {
        bridge->ForwardToChat(session.get(), pkt);
      }
      break;
    }
    // For scaffolding: ignore unknown/unimplemented messages.
    break;
  }
  }
}

} // namespace

int main(int argc, char** argv) {
  using chirp::common::Logger;

  Logger::Instance().SetLevel(Logger::Level::kInfo);
  const uint16_t port = ParseU16Arg(argc, argv, "--port", 5000);
  const uint16_t ws_port = ParseU16Arg(argc, argv, "--ws_port", static_cast<uint16_t>(port + 1));
  const std::string auth_host = GetArg(argc, argv, "--auth_host", "");
  const uint16_t auth_port = ParseU16Arg(argc, argv, "--auth_port", 6000);

  const std::string redis_host = GetArg(argc, argv, "--redis_host", "");
  const uint16_t redis_port = ParseU16Arg(argc, argv, "--redis_port", 6379);
  const int redis_ttl_seconds = std::atoi(GetArg(argc, argv, "--redis_ttl", "3600").c_str());
  std::string instance_id = GetArg(argc, argv, "--instance_id", "");
  if (instance_id.empty()) {
    instance_id = RandomHex(8);
  }

  const std::string chat_host = GetArg(argc, argv, "--chat_host", "");
  const uint16_t chat_port = ParseU16Arg(argc, argv, "--chat_port", 7000);
  const std::string chat_service_id = GetArg(argc, argv, "--chat_service_id", "gateway");
  const std::string chat_service_secret = GetArg(argc, argv, "--chat_service_secret", "");

  Logger::Instance().Info("chirp_game_sdk_gateway starting tcp=" + std::to_string(port) + " ws=" + std::to_string(ws_port) +
                          (auth_host.empty() ? "" : (" auth=" + auth_host + ":" + std::to_string(auth_port))) +
                          (redis_host.empty() ? "" : (" redis=" + redis_host + ":" + std::to_string(redis_port) +
                                                      " instance=" + instance_id)) +
                          (chat_host.empty() ? "" : (" chat=" + chat_host + ":" + std::to_string(chat_port))));

  asio::io_context io;

  auto state = std::make_shared<chirp::network::SessionRegistry>();
  std::shared_ptr<chirp::gateway::AuthClient> auth;
  if (!auth_host.empty()) {
    auth = std::make_shared<chirp::gateway::AuthClient>(io, auth_host, auth_port);
  }

  std::unique_ptr<chirp::gateway::ChatBridge> bridge;
  if (!chat_host.empty()) {
    bridge = std::make_unique<chirp::gateway::ChatBridge>(io, chat_host, chat_port,
                                                          chat_service_id, chat_service_secret);
  }

  std::shared_ptr<chirp::gateway::RedisSessionManager> redis_mgr;
  if (!redis_host.empty()) {
    redis_mgr = std::make_shared<chirp::gateway::RedisSessionManager>(
        io, redis_host, redis_port, instance_id, redis_ttl_seconds,
        // The payload carries the normalized device: kick exactly the
        // session logged in from the same device, other devices coexist.
        [state](const std::string& user_id, const std::string& device_id) {
          if (auto s = chirp::network::GetSession(state, user_id, device_id)) {
            KickSession(s, "login from another gateway instance");
          }
        });
  }

  // One dispatch pair shared by TCP and WS (the two on_frame switches used
  // to be copy-pasted copies of each other).
  auto on_frame = [state, auth, redis_mgr,
                   bridge_raw = bridge.get()](std::shared_ptr<chirp::network::Session> session,
                                              std::string&& payload) {
    chirp::gateway::Packet pkt;
    if (!pkt.ParseFromArray(payload.data(), static_cast<int>(payload.size()))) {
      Logger::Instance().Warn("failed to parse Packet from client");
      return;
    }
    HandleClientPacket(state, auth, redis_mgr, bridge_raw, session, pkt);
  };
  auto on_close = [state, redis_mgr,
                   bridge_raw = bridge.get()](std::shared_ptr<chirp::network::Session> session) {
    std::string user_id;
    std::string device_id;
    const bool should_release =
        chirp::network::RemoveAuthenticatedSession(state, session, &user_id, &device_id);
    if (should_release && redis_mgr) {
      redis_mgr->AsyncRelease(user_id, device_id);
    }
    if (bridge_raw) {
      bridge_raw->Detach(session.get());
    }
  };

  chirp::network::TcpServer server(io, port, on_frame, on_close);

  chirp::network::WebSocketServer ws_server(io, ws_port, on_frame, on_close);

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
  Logger::Instance().Info("chirp_game_sdk_gateway exited");
  return 0;
}
