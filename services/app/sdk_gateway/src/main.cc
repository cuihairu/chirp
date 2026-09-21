// App gateway: the companion-app edge. Same scaffolding as the game gateway
// (dual TCP/WS listeners, auth delegation, session registry) plus forwarding
// of the notification-plane device messages (6xxx) to the notification
// service. When --chat_host is configured, chat business packets (2xxx) are
// relayed verbatim through a per-client ChatBridge pipeline to chirp_chat;
// with it empty the edge ignores them, matching the pre-bridge behavior.

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

#include "network/auth_client.h"
#include "network/chat_bridge.h"
#include "network/session_registry.h"
#include "logger.h"
#include "network/protobuf_framing.h"
#include "network/session.h"
#include "network/ssl_context.h"
#include "network/tcp_server.h"
#include "network/tls_server.h"
#include "network/websocket_server.h"
#include "network/notification_client.h"
#include "network/server_gateway_peer.h"
#include "proto/auth.pb.h"
#include "proto/common.pb.h"
#include "proto/gateway.pb.h"
#include "proto/app_notification.pb.h"
#include "proto/game_server_gateway.pb.h"
#include "network/redis_session_manager.h"

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
    // Unlike the game gateway's scaffolding fallback, the app edge must bind
    // the session: device messages below require an authenticated session.
    if (!resp.user_id().empty()) {
      auto old = chirp::network::BindAuthenticatedSession(state, resp.user_id(), resp.session_id(),
                                                          chirp::network::NormalizeDeviceId(req.device_id()),
                                                          session);
      if (old && old.get() != session.get()) {
        KickSession(old, "login from another device");
      }
      SendPacket(session, chirp::gateway::LOGIN_RESP, seq, resp.SerializeAsString());
      if (bridge) {
        bridge->Attach(session, req.token(), req.device_id());
      }
    } else {
      SendPacket(session, chirp::gateway::LOGIN_RESP, seq, resp.SerializeAsString());
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
                         chirp::app_notification::NotificationClient* notification,
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

// Shared subscription-message path: parse, require an authenticated
// session, pin player_id to the authenticated user, then forward the RPC to
// the server-plane hub. The same message ids serve game backends directly;
// the app edge only overwrites the player identity. The hub's response body
// is relayed verbatim (it carries the minted subscription_id), so the parser
// does the relaying and the SendRpc callback only reports transport
// failures.
template <typename Req, typename Resp>
void ForwardSubscriptionPacket(const std::shared_ptr<chirp::network::Session>& session,
                               const chirp::gateway::Packet& pkt,
                               chirp::gateway::MsgID req_id,
                               chirp::gateway::MsgID resp_id,
                               const std::string& authenticated_user,
                               chirp::network::ServerGatewayPeer* sg) {
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
  if (!sg) {
    send_code(chirp::common::SERVER_UNAVAILABLE);
    return;
  }

  req.set_player_id(authenticated_user);  // clients may only act as themselves
  sg->SendRpc(req_id, resp_id, req,
              // Success path: relay the hub's body verbatim, then return OK
              // so the callback below stays silent - the real code travelled
              // inside the relayed body.
              [session, seq, resp_id](const std::string& body) {
                Resp check;
                if (!check.ParseFromString(body)) {
                  return chirp::common::INTERNAL_ERROR;
                }
                SendPacket(session, resp_id, seq, body);
                return chirp::common::OK;
              },
              // Failure path only: an OK means the parser already relayed
              // the hub's own response verbatim. SERVER_UNAVAILABLE (sent
              // while disconnected, or lost to a connection drop) and
              // INTERNAL_ERROR (unparseable hub reply) reach the client as
              // a bare code - nothing was relayed yet.
              [session, seq, resp_id](chirp::common::ErrorCode code) {
                if (code == chirp::common::OK) {
                  return;
                }
                Resp resp;
                resp.set_code(code);
                SendPacket(session, resp_id, seq, resp.SerializeAsString());
              });
}

void HandleClientPacket(const std::shared_ptr<chirp::network::Session>& session,
                        std::string&& payload,
                        const std::shared_ptr<chirp::network::SessionRegistry>& state,
                        const std::shared_ptr<chirp::gateway::AuthClient>& auth,
                        const std::shared_ptr<chirp::gateway::RedisSessionManager>& redis_mgr,
                        chirp::app_notification::NotificationClient* notification,
                        chirp::network::ServerGatewayPeer* sg,
                        chirp::gateway::ChatBridge* bridge) {
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
  case chirp::gateway::REGISTER_DEVICE_REQ: {
    ForwardDevicePacket<chirp::app_notification::RegisterDeviceRequest,
                        chirp::app_notification::RegisterDeviceResponse>(
        session, pkt, chirp::gateway::REGISTER_DEVICE_RESP,
        chirp::network::GetAuthenticatedSession(state, session).user_id, notification,
        &chirp::app_notification::NotificationClient::AsyncRegisterDevice);
    break;
  }
  case chirp::gateway::UNREGISTER_DEVICE_REQ: {
    ForwardDevicePacket<chirp::app_notification::UnregisterDeviceRequest,
                        chirp::app_notification::UnregisterDeviceResponse>(
        session, pkt, chirp::gateway::UNREGISTER_DEVICE_RESP,
        chirp::network::GetAuthenticatedSession(state, session).user_id, notification,
        &chirp::app_notification::NotificationClient::AsyncUnregisterDevice);
    break;
  }
  case chirp::gateway::UPDATE_DEVICE_TOKEN_REQ: {
    ForwardDevicePacket<chirp::app_notification::UpdateDeviceTokenRequest,
                        chirp::app_notification::UpdateDeviceTokenResponse>(
        session, pkt, chirp::gateway::UPDATE_DEVICE_TOKEN_RESP,
        chirp::network::GetAuthenticatedSession(state, session).user_id, notification,
        &chirp::app_notification::NotificationClient::AsyncUpdateDeviceToken);
    break;
  }
  case chirp::gateway::GET_USER_DEVICES_REQ: {
    ForwardDevicePacket<chirp::app_notification::GetUserDevicesRequest,
                        chirp::app_notification::GetUserDevicesResponse>(
        session, pkt, chirp::gateway::GET_USER_DEVICES_RESP,
        chirp::network::GetAuthenticatedSession(state, session).user_id, notification,
        &chirp::app_notification::NotificationClient::AsyncGetUserDevices);
    break;
  }
  case chirp::gateway::SUBSCRIBE_PLAYER_CHANNEL_REQ: {
    ForwardSubscriptionPacket<chirp::game_server_gateway::SubscribePlayerChannelRequest,
                              chirp::game_server_gateway::SubscribePlayerChannelResponse>(
        session, pkt, chirp::gateway::SUBSCRIBE_PLAYER_CHANNEL_REQ,
        chirp::gateway::SUBSCRIBE_PLAYER_CHANNEL_RESP,
        chirp::network::GetAuthenticatedSession(state, session).user_id, sg);
    break;
  }
  case chirp::gateway::UNSUBSCRIBE_PLAYER_CHANNEL_REQ: {
    ForwardSubscriptionPacket<chirp::game_server_gateway::UnsubscribePlayerChannelRequest,
                              chirp::game_server_gateway::UnsubscribePlayerChannelResponse>(
        session, pkt, chirp::gateway::UNSUBSCRIBE_PLAYER_CHANNEL_REQ,
        chirp::gateway::UNSUBSCRIBE_PLAYER_CHANNEL_RESP,
        chirp::network::GetAuthenticatedSession(state, session).user_id, sg);
    break;
  }
  case chirp::gateway::GET_PLAYER_SUBSCRIPTIONS_REQ: {
    ForwardSubscriptionPacket<chirp::game_server_gateway::GetPlayerSubscriptionsRequest,
                              chirp::game_server_gateway::GetPlayerSubscriptionsResponse>(
        session, pkt, chirp::gateway::GET_PLAYER_SUBSCRIPTIONS_REQ,
        chirp::gateway::GET_PLAYER_SUBSCRIPTIONS_RESP,
        chirp::network::GetAuthenticatedSession(state, session).user_id, sg);
    break;
  }
  case chirp::gateway::MARK_CHANNELS_READ_REQ: {
    ForwardSubscriptionPacket<chirp::game_server_gateway::MarkChannelsReadRequest,
                              chirp::game_server_gateway::MarkChannelsReadResponse>(
        session, pkt, chirp::gateway::MARK_CHANNELS_READ_REQ,
        chirp::gateway::MARK_CHANNELS_READ_RESP,
        chirp::network::GetAuthenticatedSession(state, session).user_id, sg);
    break;
  }
  case chirp::gateway::GET_UNREAD_SUMMARY_REQ: {
    ForwardSubscriptionPacket<chirp::game_server_gateway::GetUnreadSummaryRequest,
                              chirp::game_server_gateway::GetUnreadSummaryResponse>(
        session, pkt, chirp::gateway::GET_UNREAD_SUMMARY_REQ,
        chirp::gateway::GET_UNREAD_SUMMARY_RESP,
        chirp::network::GetAuthenticatedSession(state, session).user_id, sg);
    break;
  }
  default: {
    // Chat business packets relay through the per-client pipeline once the
    // client is authenticated; chat answers on the same connection, so the
    // edge never synthesizes responses for them. Without a bridge (or before
    // login) they stay ignored, matching the pre-bridge edge behavior.
    const auto id = static_cast<int>(pkt.msg_id());
    if (bridge != nullptr && id >= 2001 && id <= 2999) {
      if (!chirp::network::GetAuthenticatedSession(state, session).user_id.empty()) {
        bridge->ForwardToChat(session.get(), pkt);
      }
      break;
    }
    break;
  }
  }
}

void HandleDisconnect(const std::shared_ptr<chirp::network::Session>& session,
                      const std::shared_ptr<chirp::network::SessionRegistry>& state,
                      const std::shared_ptr<chirp::gateway::RedisSessionManager>& redis_mgr,
                      chirp::gateway::ChatBridge* bridge) {
  std::string user_id;
  std::string device_id;
  const bool should_release =
      chirp::network::RemoveAuthenticatedSession(state, session, &user_id, &device_id);
  if (should_release && redis_mgr) {
    redis_mgr->AsyncRelease(user_id, device_id);
  }
  // Single detach point, mirroring the game gateway's on_close: logout,
  // kick and plain disconnects all close the socket, which funnels here.
  if (bridge) {
    bridge->Detach(session.get());
  }
}

}  // namespace

int main(int argc, char** argv) {
  using chirp::common::Logger;

  Logger::Instance().SetLevel(Logger::Level::kInfo);
  const uint16_t port = ParseU16Arg(argc, argv, "--port", 5200);
  const uint16_t ws_port = ParseU16Arg(argc, argv, "--ws_port", 5201);
  // Optional TLS/wss edges: 0 = off. Enabling either requires the PEM pair.
  const uint16_t tls_port = ParseU16Arg(argc, argv, "--tls_port", 0);
  const uint16_t ws_tls_port = ParseU16Arg(argc, argv, "--ws_tls_port", 0);
  const std::string tls_cert = GetArg(argc, argv, "--tls_cert", "");
  const std::string tls_key = GetArg(argc, argv, "--tls_key", "");
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

  // Server-plane hub (WP-8 player channel subscriptions): empty --sg_host
  // leaves the subscription self-service path disabled.
  const std::string sg_host = GetArg(argc, argv, "--sg_host", "");
  const uint16_t sg_port = ParseU16Arg(argc, argv, "--sg_port", 8100);
  const std::string sg_service_id = GetArg(argc, argv, "--sg_service_id", "app_gateway");
  const std::string sg_secret = GetArg(argc, argv, "--sg_secret", "");

  // Chat pipeline: empty --chat_host keeps the edge byte-for-byte compatible
  // with the pre-bridge behavior (2xxx ignored). The secret must match the
  // chat service's --gateway_service_secret or every login is kicked by the
  // 5s handshake timeout.
  const std::string chat_host = GetArg(argc, argv, "--chat_host", "");
  const uint16_t chat_port = ParseU16Arg(argc, argv, "--chat_port", 7000);
  const std::string chat_service_id = GetArg(argc, argv, "--chat_service_id", "app_gateway");
  const std::string chat_service_secret = GetArg(argc, argv, "--chat_service_secret", "");

  Logger::Instance().Info("chirp_app_sdk_gateway starting tcp=" + std::to_string(port) +
                          " ws=" + std::to_string(ws_port) +
                          (tls_port != 0 ? (" tls=" + std::to_string(tls_port)) : "") +
                          (ws_tls_port != 0 ? (" wss=" + std::to_string(ws_tls_port)) : "") +
                          (auth_host.empty() ? "" : (" auth=" + auth_host + ":" + std::to_string(auth_port))) +
                          (redis_host.empty() ? "" : (" redis=" + redis_host + ":" + std::to_string(redis_port) +
                                                      " instance=" + instance_id)) +
                          (notification_host.empty()
                               ? " device-forward=disabled"
                               : (" notification=" + notification_host + ":" + std::to_string(notification_port))) +
                          (sg_host.empty()
                               ? " server-plane=disabled"
                               : (" sg=" + sg_host + ":" + std::to_string(sg_port) +
                                  " service=" + sg_service_id)) +
                          (chat_host.empty()
                               ? " chat-pipeline=disabled"
                               : (" chat=" + chat_host + ":" + std::to_string(chat_port) +
                                  " service=" + chat_service_id)));

  // TLS edges: load the shared context before anything is bound, so a bad
  // cert/key pair is a clean fatal startup error.
  std::shared_ptr<asio::ssl::context> ssl;
  if (tls_port != 0 || ws_tls_port != 0) {
    if (tls_cert.empty() || tls_key.empty()) {
      Logger::Instance().Error("TLS port requested but --tls_cert/--tls_key missing");
      return 1;
    }
    std::string error;
    ssl = chirp::network::MakeServerSslContext(tls_cert, tls_key, &error);
    if (!ssl) {
      Logger::Instance().Error("failed to load TLS cert/key: " + error);
      return 1;
    }
  }

  asio::io_context io;

  auto state = std::make_shared<chirp::network::SessionRegistry>();
  std::shared_ptr<chirp::gateway::AuthClient> auth;
  if (!auth_host.empty()) {
    auth = std::make_shared<chirp::gateway::AuthClient>(io, auth_host, auth_port);
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

  std::shared_ptr<chirp::app_notification::NotificationClient> notification;
  if (!notification_host.empty()) {
    notification =
        std::make_shared<chirp::app_notification::NotificationClient>(io, notification_host, notification_port);
  }

  // One long-lived authenticated connection: the hub allows exactly one live
  // connection per service id, so per-request dials would displace each
  // other. Runs on the same single io thread as the edge sessions, so RPC
  // callbacks can write back to a session directly.
  std::shared_ptr<chirp::network::ServerGatewayPeer> sg;
  if (!sg_host.empty()) {
    chirp::network::ServerGatewayPeer::Options sg_opts;
    sg_opts.host = sg_host;
    sg_opts.port = sg_port;
    sg_opts.service_id = sg_service_id;
    sg_opts.secret = sg_secret;
    sg = chirp::network::ServerGatewayPeer::Create(io, sg_opts, nullptr, nullptr);
    sg->Start();
  }

  // Per-client chat pipeline (game gateway's ChatBridge). Lives on the same
  // single io thread as the edge sessions, so relay callbacks can write back
  // to a session directly.
  std::unique_ptr<chirp::gateway::ChatBridge> bridge;
  if (!chat_host.empty()) {
    bridge = std::make_unique<chirp::gateway::ChatBridge>(io, chat_host, chat_port,
                                                          chat_service_id, chat_service_secret);
  }

  auto on_frame = [state, auth, redis_mgr, notification, sg,
                   bridge_raw = bridge.get()](std::shared_ptr<chirp::network::Session> session,
                                              std::string&& payload) {
    HandleClientPacket(session, std::move(payload), state, auth, redis_mgr, notification.get(),
                       sg.get(), bridge_raw);
  };
  auto on_close = [state, redis_mgr,
                   bridge_raw = bridge.get()](std::shared_ptr<chirp::network::Session> session) {
    HandleDisconnect(session, state, redis_mgr, bridge_raw);
  };

  chirp::network::TcpServer server(io, port, on_frame, on_close);
  chirp::network::WebSocketServer ws_server(io, ws_port, on_frame, on_close);
  server.Start();
  ws_server.Start();

  // TLS twins of the same edge: the registry/auth/forwarding paths are
  // stream-agnostic, so the callbacks are shared verbatim.
  std::unique_ptr<chirp::network::TlsTcpServer> tls_server;
  std::unique_ptr<chirp::network::TlsWebSocketServer> wss_server;
  if (ssl) {
    if (tls_port != 0) {
      tls_server = std::make_unique<chirp::network::TlsTcpServer>(io, tls_port, ssl, on_frame, on_close);
      tls_server->Start();
    }
    if (ws_tls_port != 0) {
      wss_server =
          std::make_unique<chirp::network::TlsWebSocketServer>(io, ws_tls_port, ssl, on_frame, on_close);
      wss_server->Start();
    }
  }

  asio::signal_set signals(io, SIGINT, SIGTERM);
  signals.async_wait([&](const std::error_code& /*ec*/, int /*sig*/) {
    Logger::Instance().Info("shutdown requested");
    if (sg) {
      sg->Stop();
    }
    server.Stop();
    ws_server.Stop();
    if (tls_server) {
      tls_server->Stop();
    }
    if (wss_server) {
      wss_server->Stop();
    }
    io.stop();
  });

  io.run();
  Logger::Instance().Info("chirp_app_sdk_gateway exited");
  return 0;
}
