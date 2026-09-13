// App gateway: the companion-app edge. Same scaffolding as the game gateway
// (dual TCP/WS listeners, auth delegation, session registry) plus forwarding
// of the notification-plane device messages (6xxx) to the notification
// service. Chat business packets are NOT accepted here.

#include <chrono>
#include <cstdint>
#include <cstdlib>
#include <memory>
#include <mutex>
#include <optional>
#include <random>
#include <string>
#include <type_traits>
#include <utility>

#include <asio.hpp>

#include "auth_client.h"
#include "gateway_session_registry.h"
#include "logger.h"
#include "network/protobuf_framing.h"
#include "network/session.h"
#include "network/tcp_server.h"
#include "network/websocket_server.h"
#include "notification_client.h"
#include "proto/auth.pb.h"
#include "proto/common.pb.h"
#include "proto/gateway.pb.h"
#include "proto/notification.pb.h"
#include "redis_session_manager.h"

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
                 const std::shared_ptr<chirp::gateway::GatewayState>& state,
                 const std::shared_ptr<chirp::gateway::AuthClient>& auth,
                 const std::shared_ptr<chirp::gateway::RedisSessionManager>& redis_mgr) {
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
    // Unlike the game gateway's scaffolding fallback, the app edge must bind
    // the session: device messages below require an authenticated session.
    if (!resp.user_id().empty()) {
      auto old = chirp::gateway::BindAuthenticatedSession(state, resp.user_id(), resp.session_id(), session);
      if (old && old.get() != session.get()) {
        KickSession(old, "login from another device");
      }
    }
    SendPacket(session, chirp::gateway::LOGIN_RESP, seq, resp.SerializeAsString());
    return;
  }

  auth->AsyncLogin(req, seq,
                   [session, seq, req, state, redis_mgr, send_err](const chirp::auth::LoginResponse& auth_resp) {
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

    auto old = chirp::gateway::BindAuthenticatedSession(state, user_id, resp.session_id(), session);

    if (old && old.get() != session.get()) {
      const std::string reason = resp.has_kick() ? resp.kick().reason() : "login from another device";
      KickSession(old, reason);
    }

    if (redis_mgr) {
      redis_mgr->AsyncClaim(user_id, [session, seq, resp](std::optional<std::string> /*prev_owner*/) mutable {
        SendPacket(session, chirp::gateway::LOGIN_RESP, seq, resp.SerializeAsString());
      });
    } else {
      SendPacket(session, chirp::gateway::LOGIN_RESP, seq, resp.SerializeAsString());
    }
  });
}

void HandleLogout(const std::shared_ptr<chirp::network::Session>& session,
                  const chirp::gateway::Packet& pkt,
                  const chirp::auth::LogoutRequest& req,
                  const std::shared_ptr<chirp::gateway::GatewayState>& state,
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

  const auto current = chirp::gateway::GetAuthenticatedSession(state, session);
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
      should_release = chirp::gateway::RemoveAuthenticatedSession(state, session, &removed_user_id);
      if (should_release && redis_mgr) {
        redis_mgr->AsyncRelease(removed_user_id.empty() ? req.user_id() : removed_user_id);
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

// Detects whether a device request carries a user_id field; the token update
// message addresses devices by device_id only.
template <typename T, typename = void>
struct HasUserId : std::false_type {};
template <typename T>
struct HasUserId<T, std::void_t<decltype(std::declval<T&>().set_user_id(std::string{}))>>
    : std::true_type {};

// Shared device-message path: parse, require an authenticated session, pin
// user_id to the authenticated user, then forward to the notification plane.
template <typename Req, typename Resp, typename ClientFn>
void ForwardDevicePacket(const std::shared_ptr<chirp::network::Session>& session,
                         const chirp::gateway::Packet& pkt,
                         chirp::gateway::MsgID resp_id,
                         const std::string& authenticated_user,
                         chirp::notification::NotificationClient* notification,
                         ClientFn client_fn) {
  const int64_t seq = pkt.sequence();
  auto send_code = [session, seq, resp_id](chirp::common::ErrorCode code) {
    Resp resp;
    resp.set_code(code);
    SendPacket(session, resp_id, seq, resp.SerializeAsString());
  };

  Req req;
  if (!req.ParseFromArray(pkt.body().data(), static_cast<int>(pkt.body().size()))) {
    send_code(chirp::common::INVALID_PARAM);
    return;
  }
  if (authenticated_user.empty()) {
    send_code(chirp::common::AUTH_FAILED);
    return;
  }
  if (!notification) {
    send_code(chirp::common::SERVER_UNAVAILABLE);
    return;
  }

  if constexpr (HasUserId<Req>::value) {
    req.set_user_id(authenticated_user);  // clients may only act as themselves
  }
  (notification->*client_fn)(req, seq, [session, seq, resp_id](const Resp& resp) {
    SendPacket(session, resp_id, seq, resp.SerializeAsString());
  });
}

void HandleClientPacket(const std::shared_ptr<chirp::network::Session>& session,
                        std::string&& payload,
                        const std::shared_ptr<chirp::gateway::GatewayState>& state,
                        const std::shared_ptr<chirp::gateway::AuthClient>& auth,
                        const std::shared_ptr<chirp::gateway::RedisSessionManager>& redis_mgr,
                        chirp::notification::NotificationClient* notification) {
  chirp::gateway::Packet pkt;
  if (!pkt.ParseFromArray(payload.data(), static_cast<int>(payload.size()))) {
    chirp::common::Logger::Instance().Warn("failed to parse Packet from app client");
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
    HandleLogin(session, pkt, req, state, auth, redis_mgr);
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
  case chirp::gateway::REGISTER_DEVICE_REQ: {
    ForwardDevicePacket<chirp::notification::RegisterDeviceRequest,
                        chirp::notification::RegisterDeviceResponse>(
        session, pkt, chirp::gateway::REGISTER_DEVICE_RESP,
        chirp::gateway::GetAuthenticatedSession(state, session).user_id, notification,
        &chirp::notification::NotificationClient::AsyncRegisterDevice);
    break;
  }
  case chirp::gateway::UNREGISTER_DEVICE_REQ: {
    ForwardDevicePacket<chirp::notification::UnregisterDeviceRequest,
                        chirp::notification::UnregisterDeviceResponse>(
        session, pkt, chirp::gateway::UNREGISTER_DEVICE_RESP,
        chirp::gateway::GetAuthenticatedSession(state, session).user_id, notification,
        &chirp::notification::NotificationClient::AsyncUnregisterDevice);
    break;
  }
  case chirp::gateway::UPDATE_DEVICE_TOKEN_REQ: {
    ForwardDevicePacket<chirp::notification::UpdateDeviceTokenRequest,
                        chirp::notification::UpdateDeviceTokenResponse>(
        session, pkt, chirp::gateway::UPDATE_DEVICE_TOKEN_RESP,
        chirp::gateway::GetAuthenticatedSession(state, session).user_id, notification,
        &chirp::notification::NotificationClient::AsyncUpdateDeviceToken);
    break;
  }
  case chirp::gateway::GET_USER_DEVICES_REQ: {
    ForwardDevicePacket<chirp::notification::GetUserDevicesRequest,
                        chirp::notification::GetUserDevicesResponse>(
        session, pkt, chirp::gateway::GET_USER_DEVICES_RESP,
        chirp::gateway::GetAuthenticatedSession(state, session).user_id, notification,
        &chirp::notification::NotificationClient::AsyncGetUserDevices);
    break;
  }
  default:
    // Companion-app edge: chat/business packets belong to their own services.
    break;
  }
}

void HandleDisconnect(const std::shared_ptr<chirp::network::Session>& session,
                      const std::shared_ptr<chirp::gateway::GatewayState>& state,
                      const std::shared_ptr<chirp::gateway::RedisSessionManager>& redis_mgr) {
  std::string user_id;
  bool should_release = false;
  {
    std::lock_guard<std::mutex> lock(state->mu);
    auto it = state->session_to_user.find(session.get());
    if (it == state->session_to_user.end()) {
      return;
    }
    user_id = it->second;
    state->session_to_user.erase(it);
    state->session_to_session_id.erase(session.get());

    auto it2 = state->user_to_session.find(user_id);
    if (it2 != state->user_to_session.end()) {
      auto cur = it2->second.lock();
      if (!cur || cur.get() == session.get()) {
        state->user_to_session.erase(it2);
        should_release = true;
      }
    }
  }
  if (should_release && redis_mgr) {
    redis_mgr->AsyncRelease(user_id);
  }
}

}  // namespace

int main(int argc, char** argv) {
  using chirp::common::Logger;

  Logger::Instance().SetLevel(Logger::Level::kInfo);
  const uint16_t port = ParseU16Arg(argc, argv, "--port", 5200);
  const uint16_t ws_port = ParseU16Arg(argc, argv, "--ws_port", 5201);
  const std::string auth_host = GetArg(argc, argv, "--auth_host", "");
  const uint16_t auth_port = ParseU16Arg(argc, argv, "--auth_port", 6000);

  const std::string redis_host = GetArg(argc, argv, "--redis_host", "");
  const uint16_t redis_port = ParseU16Arg(argc, argv, "--redis_port", 6379);
  const int redis_ttl_seconds = std::atoi(GetArg(argc, argv, "--redis_ttl", "3600").c_str());
  std::string instance_id = GetArg(argc, argv, "--instance_id", "");
  if (instance_id.empty()) {
    instance_id = RandomHex(8);
  }

  const std::string notification_host = GetArg(argc, argv, "--notification_host", "");
  const uint16_t notification_port = ParseU16Arg(argc, argv, "--notification_port", 5006);

  Logger::Instance().Info("chirp_app_gateway starting tcp=" + std::to_string(port) +
                          " ws=" + std::to_string(ws_port) +
                          (auth_host.empty() ? "" : (" auth=" + auth_host + ":" + std::to_string(auth_port))) +
                          (redis_host.empty() ? "" : (" redis=" + redis_host + ":" + std::to_string(redis_port) +
                                                      " instance=" + instance_id)) +
                          (notification_host.empty()
                               ? " device-forward=disabled"
                               : (" notification=" + notification_host + ":" + std::to_string(notification_port))));

  asio::io_context io;

  auto state = std::make_shared<chirp::gateway::GatewayState>();
  std::shared_ptr<chirp::gateway::AuthClient> auth;
  if (!auth_host.empty()) {
    auth = std::make_shared<chirp::gateway::AuthClient>(io, auth_host, auth_port);
  }

  std::shared_ptr<chirp::gateway::RedisSessionManager> redis_mgr;
  if (!redis_host.empty()) {
    redis_mgr = std::make_shared<chirp::gateway::RedisSessionManager>(
        io, redis_host, redis_port, instance_id, redis_ttl_seconds,
        [state](const std::string& user_id) {
          std::shared_ptr<chirp::network::Session> s;
          {
            std::lock_guard<std::mutex> lock(state->mu);
            auto it = state->user_to_session.find(user_id);
            if (it != state->user_to_session.end()) {
              s = it->second.lock();
            }
          }
          if (s) {
            KickSession(s, "login from another gateway instance");
          }
        });
  }

  std::shared_ptr<chirp::notification::NotificationClient> notification;
  if (!notification_host.empty()) {
    notification =
        std::make_shared<chirp::notification::NotificationClient>(io, notification_host, notification_port);
  }

  auto on_frame = [state, auth, redis_mgr, notification](std::shared_ptr<chirp::network::Session> session,
                                                         std::string&& payload) {
    HandleClientPacket(session, std::move(payload), state, auth, redis_mgr, notification.get());
  };
  auto on_close = [state, redis_mgr](std::shared_ptr<chirp::network::Session> session) {
    HandleDisconnect(session, state, redis_mgr);
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
  Logger::Instance().Info("chirp_app_gateway exited");
  return 0;
}
