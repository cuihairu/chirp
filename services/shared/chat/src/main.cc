#include <algorithm>
#include <atomic>
#include <chrono>
#include <cstdint>
#include <cstdlib>
#include <memory>
#include <mutex>
#include <string>
#include <unordered_map>
#include <unordered_set>
#include <vector>

#include <asio.hpp>

#include "chat_rate_limiter.h"
#include "delivery_ack_manager.h"
#include "network/session_registry.h"
#include "login_token_verifier.h"
#include "chat_validation.h"
#include "group_handlers.h"
#include "inject_consumer.h"
#include "logger.h"
#include "message_handlers.h"
#include "network/protobuf_framing.h"
#include "network/redis_client.h"
#include "network/session.h"
#include "network/tcp_server.h"
#include "network/websocket_server.h"
#include "npc_uplink.h"
#include "player_directory.h"
#include "push_bridge.h"
#include "network/chat_peer_hub.h"
#include "network/chat_peer_link.h"
#include "network/notification_client.h"
#include "proto/auth.pb.h"
#include "proto/chat.pb.h"
#include "proto/common.pb.h"
#include "proto/game_server_gateway.pb.h"
#include "runtime_utils.h"
#include "network/server_gateway_peer.h"

namespace {

std::string GenerateSessionId() {
  static std::atomic<uint64_t> counter{1};
  return "chat_session_" + std::to_string(chirp::chat::runtime::NowMs()) + "_" + std::to_string(counter.fetch_add(1));
}

struct MessageStore {
  static constexpr size_t kMaxHistory = 100;
  static constexpr size_t kMaxOfflineInMemory = 200;

  std::shared_ptr<chirp::network::RedisClient> redis;
  int offline_ttl_seconds{0};

  // channel_key -> messages
  std::unordered_map<std::string, std::vector<chirp::chat::ChatMessage>> history;
  // receiver_id -> pending messages (redis 不可用时兜底)
  std::unordered_map<std::string, std::vector<chirp::chat::ChatMessage>> offline_messages;

  explicit MessageStore(std::shared_ptr<chirp::network::RedisClient> redis_client, int ttl)
      : redis(std::move(redis_client)), offline_ttl_seconds(ttl) {}

  std::string HistoryKey(chirp::chat::ChannelType type, const std::string& channel_id) {
    return "chat:history:" + ChannelKey(type, channel_id);
  }

  std::string ChannelKey(chirp::chat::ChannelType type, const std::string& channel_id) {
    return std::to_string(static_cast<int>(type)) + ":" + channel_id;
  }

  std::string PrivateChannelId(const std::string& a, const std::string& b) {
    if (a <= b) {
      return a + "|" + b;
    }
    return b + "|" + a;
  }

  void AddMessage(const chirp::chat::ChatMessage& msg) {
    if (redis) {
      redis->RPush(HistoryKey(msg.channel_type(), msg.channel_id()), msg.SerializeAsString());
    }

    auto& msgs = history[ChannelKey(msg.channel_type(), msg.channel_id())];
    msgs.push_back(msg);

    // 只保留最近 100 条消息
    if (msgs.size() > kMaxHistory) {
      msgs.erase(msgs.begin());
    }
  }

  std::string OfflineKey(const std::string& user_id) { return "chat:offline:" + user_id; }

  void AddOffline(const std::string& receiver_id, const chirp::chat::ChatMessage& msg) {
    if (receiver_id.empty()) {
      return;
    }
    if (redis && redis->RPush(OfflineKey(receiver_id), msg.SerializeAsString())) {
      if (offline_ttl_seconds > 0) {
        redis->Expire(OfflineKey(receiver_id), offline_ttl_seconds);
      }
      return;
    }

    auto& pending = offline_messages[receiver_id];
    pending.push_back(msg);
    if (pending.size() > kMaxOfflineInMemory) {
      pending.erase(pending.begin());
    }
  }

  std::vector<chirp::chat::ChatMessage> PopOffline(const std::string& user_id) {
    std::vector<chirp::chat::ChatMessage> out;
    if (user_id.empty()) {
      return out;
    }

    if (redis) {
      auto raw = redis->LRange(OfflineKey(user_id), 0, -1);
      if (!raw.empty()) {
        out.reserve(raw.size());
        for (const auto& item : raw) {
          chirp::chat::ChatMessage m;
          if (m.ParseFromArray(item.data(), static_cast<int>(item.size()))) {
            out.push_back(std::move(m));
          }
        }
      }
      redis->Del(OfflineKey(user_id));
      if (!out.empty()) {
        return out;
      }
    }

    auto it = offline_messages.find(user_id);
    if (it == offline_messages.end()) {
      return out;
    }
    out = std::move(it->second);
    offline_messages.erase(it);
    return out;
  }

  // Delivery-ack requeue path: pushes the exact bytes that were tracked so a
  // late ack can remove the copy byte-for-byte (no parse/re-serialize).
  void AddOfflineBytes(const std::string& receiver_id, const std::string& bytes) {
    if (receiver_id.empty()) {
      return;
    }
    if (redis && redis->RPush(OfflineKey(receiver_id), bytes)) {
      if (offline_ttl_seconds > 0) {
        redis->Expire(OfflineKey(receiver_id), offline_ttl_seconds);
      }
      return;
    }
    chirp::chat::ChatMessage msg;
    if (msg.ParseFromArray(bytes.data(), static_cast<int>(bytes.size()))) {
      auto& pending = offline_messages[receiver_id];
      pending.push_back(std::move(msg));
      if (pending.size() > kMaxOfflineInMemory) {
        pending.erase(pending.begin());
      }
    }
  }

  // Late-ack cleanup: drop the offline copy the client confirmed after it had
  // already been requeued. The Redis entry matches byte-for-byte; the
  // in-memory fallback matches by message_id.
  bool RemoveOffline(const std::string& receiver_id, const std::string& bytes) {
    if (receiver_id.empty()) {
      return false;
    }
    bool removed = false;
    if (redis) {
      removed = redis->LRem(OfflineKey(receiver_id), 1, bytes) > 0;
    }
    chirp::chat::ChatMessage msg;
    if (msg.ParseFromArray(bytes.data(), static_cast<int>(bytes.size()))) {
      auto it = offline_messages.find(receiver_id);
      if (it != offline_messages.end()) {
        auto& pending = it->second;
        for (auto mit = pending.begin(); mit != pending.end(); ++mit) {
          if (mit->message_id() == msg.message_id()) {
            pending.erase(mit);
            removed = true;
            break;
          }
        }
      }
    }
    return removed;
  }

  std::vector<chirp::chat::ChatMessage> GetHistory(chirp::chat::ChannelType type,
                                                   const std::string& channel_id,
                                                   int64_t before_timestamp,
                                                   int32_t limit,
                                                   bool* has_more) {
    if (has_more) {
      *has_more = false;
    }

    int64_t before = before_timestamp;
    if (before <= 0) {
      before = chirp::chat::runtime::NowMs() + 1;
    }
    int32_t lim = limit;
    if (lim <= 0) {
      lim = 50;
    }

    if (redis) {
      auto raw = redis->LRange(HistoryKey(type, channel_id), 0, -1);
      if (!raw.empty()) {
        std::vector<chirp::chat::ChatMessage> result;
        result.reserve(raw.size());
        for (auto rit = raw.rbegin(); rit != raw.rend(); ++rit) {
          chirp::chat::ChatMessage msg;
          if (!msg.ParseFromArray(rit->data(), static_cast<int>(rit->size()))) {
            continue;
          }
          if (msg.timestamp() >= before) {
            continue;
          }
          result.push_back(std::move(msg));
          if (static_cast<int32_t>(result.size()) >= lim) {
            if (has_more) {
              *has_more = (rit + 1) != raw.rend();
            }
            break;
          }
        }
        std::reverse(result.begin(), result.end());
        return result;
      }
    }

    auto it = history.find(ChannelKey(type, channel_id));
    if (it == history.end()) {
      return {};
    }

    const auto& all_msgs = it->second;
    std::vector<chirp::chat::ChatMessage> result;
    for (auto rit = all_msgs.rbegin(); rit != all_msgs.rend(); ++rit) {
      if (rit->timestamp() >= before) {
        continue;
      }
      result.push_back(*rit);
      if (static_cast<int32_t>(result.size()) >= lim) {
        if (has_more) {
          *has_more = (rit + 1) != all_msgs.rend();
        }
        break;
      }
    }

    std::reverse(result.begin(), result.end());
    return result;
  }
};

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

  chirp::gateway::Packet pkt;
  pkt.set_msg_id(chirp::gateway::KICK_NOTIFY);
  pkt.set_sequence(0);
  pkt.set_body(kick.SerializeAsString());

  auto framed = chirp::network::ProtobufFraming::Encode(pkt);
  session->SendAndClose(std::string(reinterpret_cast<const char*>(framed.data()), framed.size()));
}

// All live sessions of a user whose connection has not half-closed - the
// healthy delivery targets across every device. Empty means the user has no
// connection worth writing to right now.
std::vector<std::shared_ptr<chirp::network::Session>> HealthyUserSessions(
    const std::shared_ptr<chirp::network::SessionRegistry>& state,
    const std::string& user_id) {
  std::vector<std::shared_ptr<chirp::network::Session>> healthy;
  for (const auto& recv : chirp::network::GetUserSessions(state, user_id)) {
    if (!recv->PeerHalfClosed()) {
      healthy.push_back(recv);
    }
  }
  return healthy;
}

// Holds a delivery until MESSAGE_ACK when any target device declared the
// ack capability. Track is idempotent per message id, so one capable device
// is enough; the ack may arrive over a different one.
void TrackAckIfCapable(chirp::chat::DeliveryAckManager* acks,
                       const std::vector<std::shared_ptr<chirp::network::Session>>& sessions,
                       const std::string& message_id,
                       const std::string& receiver_id,
                       const std::string& payload) {
  if (!acks) {
    return;
  }
  for (const auto& recv : sessions) {
    if (acks->IsCapable(recv.get())) {
      acks->Track(message_id, receiver_id, payload);
      return;
    }
  }
}

void HandleDisconnect(const std::shared_ptr<chirp::network::SessionRegistry>& state,
                      chirp::chat::DeliveryAckManager* acks,
                      const std::shared_ptr<std::unordered_set<const chirp::network::Session*>>& trusted_conns,
                      const std::shared_ptr<chirp::network::Session>& session) {
  if (acks) {
    acks->ForgetSession(session.get());
  }
  // Drop any trust-grant bound to this connection so a reused pointer cannot
  // inherit the previous connection's limiter bypass.
  if (trusted_conns) {
    trusted_conns->erase(session.get());
  }
  std::string user_id;
  if (chirp::network::RemoveAuthenticatedSession(state, session, &user_id) &&
      !user_id.empty()) {
    chirp::common::Logger::Instance().Info("User disconnected: " + user_id);
  }
}

// Aggregate of the per-feature request handlers wired into the dispatch.
struct FeatureHandlers {
  chirp::chat::GroupHandlers& groups;
  chirp::chat::ReadReceiptHandlers& receipts;
  chirp::chat::TypingHandlers& typing;
  chirp::chat::ReactionHandlers& reactions;
  chirp::chat::MessageEditHandlers& edits;
  chirp::chat::MentionHandlers& mentions;
  chirp::chat::PushBridge& push;
  // NPC uplink: peer is null unless --server_gateway_host is set; an empty
  // npc_service_id keeps the feature off even with a live peer.
  chirp::network::ServerGatewayPeer* hub_peer = nullptr;
  std::string npc_service_id;
  std::string npc_prefix = "npc:";
  // Direct-entry abuse gate: null keeps the legacy unthrottled behavior (not
  // expected in practice — main always installs one; it fails open without
  // Redis).
  chirp::chat::ChatRateLimiter* rate_limiter = nullptr;
  // Null or disabled() keeps the scaffold "token is user_id" login.
  const chirp::common::LoginTokenVerifier* token_verifier = nullptr;
  // Client delivery-ack bookkeeping; null (or a disabled manager) keeps the
  // send-and-forget delivery for every session.
  chirp::chat::DeliveryAckManager* acks = nullptr;
  // Trust gate for internal-plane dials (the gateway's per-client pipes).
  // Empty secret keeps SERVER_AUTH_REQ ignored — direct entry only, exactly
  // the historical behavior. Trusted connections skip the per-IP login
  // limiter only; token verification still applies.
  std::string gateway_secret;
  std::shared_ptr<std::unordered_set<const chirp::network::Session*>> trusted_conns;
  // Spoke-mode uplink to the app-plane chat hub (game_chat deployment).
  // Null = spoke disabled; uplink sends through it are best-effort and drop
  // while the link is unregistered — the game plane keeps running.
  std::shared_ptr<chirp::network::ChatPeerLink> spoke = nullptr;
  // Namespace the uplink stamps on CHANNEL_MESSAGE_NOTIFY (the hub prefixes
  // its channels with it); empty when the spoke is off.
  std::string spoke_game_id = {};
  // App-plane player directory (WP-8): identity bindings, subscriptions,
  // unread ledger and the hub fan-out tail. Main always installs one; the
  // RPC block on this port answers only trusted (SERVER_AUTH_REQ) dials.
  chirp::chat::PlayerDirectory* directory = nullptr;
  // The hub side of the peer protocol (app_chat deployment, --hub_mode);
  // null when hub mode is off. The cross-plane reply path resolves the
  // channel's "<game_id>:" prefix against its registrations.
  chirp::network::ChatPeerHub* hub = nullptr;
};

void HandlePacket(const std::shared_ptr<MessageStore>& store,
                  const std::shared_ptr<chirp::network::SessionRegistry>& state,
                  FeatureHandlers& features,
                  const std::shared_ptr<chirp::network::Session>& session,
                  std::string&& payload) {
  using chirp::common::Logger;

  chirp::gateway::Packet pkt;
  if (!pkt.ParseFromArray(payload.data(), static_cast<int>(payload.size()))) {
    Logger::Instance().Warn("failed to parse Packet from client");
    return;
  }

  const auto authenticated = chirp::network::GetAuthenticatedSession(state, session);
  const std::string& authenticated_user_id = authenticated.user_id;
  const std::string& authenticated_session_id = authenticated.session_id;

  // WP-8 player-directory block (5013-5030) rides this port behind the same
  // SERVER_AUTH_REQ trust gate as the gateway pipes: game backends assert
  // bindings/subscriptions and the app edge self-serves through it.
  if (features.directory &&
      chirp::chat::DispatchPlayerDirectoryPacket(pkt, session, *features.directory,
                                                  features.trusted_conns.get())) {
    return;
  }

  switch (pkt.msg_id()) {
  case chirp::gateway::SERVER_AUTH_REQ: {
    // Internal-plane trust gate: an edge gateway dials chat with the shared
    // service secret so its per-client pipes skip the per-IP login limiter
    // (a gateway fans many users out of one source address). Token
    // verification on the LOGIN_REQ that follows is untouched. With no
    // secret configured the frame is ignored - no response, exactly the
    // historical direct-entry-only behavior.
    if (features.gateway_secret.empty()) {
      break;
    }
    chirp::game_server_gateway::ServerAuthRequest auth_req;
    chirp::game_server_gateway::ServerAuthResponse auth_resp;
    auth_resp.set_server_time_ms(chirp::chat::runtime::NowMs());
    if (!auth_req.ParseFromArray(pkt.body().data(), static_cast<int>(pkt.body().size())) ||
        auth_req.secret() != features.gateway_secret) {
      auth_resp.set_code(chirp::common::AUTH_FAILED);
      SendPacketAndClose(session, chirp::gateway::SERVER_AUTH_RESP, pkt.sequence(),
                         auth_resp.SerializeAsString());
      break;
    }
    if (features.trusted_conns) {
      features.trusted_conns->insert(session.get());
    }
    auth_resp.set_code(chirp::common::OK);
    chirp::chat::runtime::SendPacket(session, chirp::gateway::SERVER_AUTH_RESP, pkt.sequence(),
                                      auth_resp.SerializeAsString());
    break;
  }
  case chirp::gateway::LOGIN_REQ: {
    // Direct-entry abuse gate: every LOGIN_REQ consumes budget before any
    // parsing, so malformed-packet floods are throttled too. Connections
    // that passed SERVER_AUTH_REQ (gateway pipes) are exempt: they came in
    // over the trusted internal plane, and fanning many users through one
    // gateway would otherwise exhaust this per-IP budget instantly.
    const bool trusted_conn =
        features.trusted_conns && features.trusted_conns->count(session.get()) > 0;
    if (features.rate_limiter && !trusted_conn) {
      const auto gate = features.rate_limiter->CheckLogin(session->RemoteAddress());
      if (!gate.allowed) {
        chirp::auth::LoginResponse deny;
        deny.set_code(chirp::common::RATE_LIMITED);
        deny.set_server_time(chirp::chat::runtime::NowMs());
        chirp::chat::runtime::SendPacket(session, chirp::gateway::LOGIN_RESP, pkt.sequence(),
                                         deny.SerializeAsString());
        return;
      }
    }
    // Scaffolding login: treat token as user_id.
    chirp::auth::LoginRequest login_req;
    if (!login_req.ParseFromArray(pkt.body().data(), static_cast<int>(pkt.body().size()))) {
      chirp::auth::LoginResponse resp;
      resp.set_code(chirp::common::INVALID_PARAM);
      resp.set_server_time(chirp::chat::runtime::NowMs());
      chirp::chat::runtime::SendPacket(session, chirp::gateway::LOGIN_RESP, pkt.sequence(), resp.SerializeAsString());
      return;
    }
    std::string user_id;
    if (features.token_verifier && features.token_verifier->enabled()) {
      // Real mode: the token must be an HS256 JWT signed with the shared
      // secret, unexpired, with the login user in "sub".
      std::string verify_err;
      if (!features.token_verifier->Verify(login_req.token(), chirp::chat::runtime::NowMs(),
                                           &user_id, &verify_err)) {
        Logger::Instance().Warn("chat login rejected: " + verify_err);
        chirp::auth::LoginResponse deny;
        deny.set_code(chirp::common::AUTH_FAILED);
        deny.set_server_time(chirp::chat::runtime::NowMs());
        chirp::chat::runtime::SendPacket(session, chirp::gateway::LOGIN_RESP, pkt.sequence(),
                                         deny.SerializeAsString());
        return;
      }
    } else {
      // Scaffolding login: treat token as user_id.
      user_id = login_req.token();
    }

    chirp::auth::LoginResponse login_resp;
    if (user_id.empty()) {
      login_resp.set_code(chirp::common::INVALID_PARAM);
    } else {
      login_resp.set_code(chirp::common::OK);
      login_resp.set_user_id(user_id);
      login_resp.set_session_id(GenerateSessionId());
      login_resp.set_kick_previous(true);
      login_resp.mutable_kick()->set_reason("login from another device");
    }
    login_resp.set_server_time(chirp::chat::runtime::NowMs());

    if (!user_id.empty()) {
      auto old =
          chirp::network::BindAuthenticatedSession(state, user_id, login_resp.session_id(),
                                                   chirp::network::NormalizeDeviceId(login_req.device_id()),
                                                   session);
      if (old && old.get() != session.get()) {
        KickSession(old, "login from another device");
      }
      if (features.acks && login_req.supports_message_ack()) {
        features.acks->MarkCapable(session);
      }
    }

    chirp::chat::runtime::SendPacket(session, chirp::gateway::LOGIN_RESP, pkt.sequence(), login_resp.SerializeAsString());
    if (!user_id.empty()) {
      auto offline = store->PopOffline(user_id);
      for (const auto& m : offline) {
        // Refills are tracked like live deliveries: if this session dies
        // before acking, the message returns to the offline queue instead of
        // being consumed by a zombie connection.
        if (features.acks && features.acks->IsCapable(session.get())) {
          features.acks->Track(m.message_id(), user_id, m.SerializeAsString());
        }
        chirp::chat::runtime::SendChatNotify(session, m);
      }
    }
    break;
  }
  case chirp::gateway::LOGOUT_REQ: {
    chirp::auth::LogoutRequest req;
    if (!req.ParseFromArray(pkt.body().data(), static_cast<int>(pkt.body().size()))) {
      chirp::auth::LogoutResponse resp;
      resp.set_code(chirp::common::INVALID_PARAM);
      resp.set_server_time(chirp::chat::runtime::NowMs());
      chirp::chat::runtime::SendPacket(session, chirp::gateway::LOGOUT_RESP, pkt.sequence(), resp.SerializeAsString());
      return;
    }

    chirp::auth::LogoutResponse resp;
    resp.set_code(chirp::chat::ValidateLogoutRequest(req, authenticated_user_id, authenticated_session_id));
    resp.set_server_time(chirp::chat::runtime::NowMs());
    if (resp.code() == chirp::common::OK) {
      HandleDisconnect(state, features.acks, features.trusted_conns, session);
      SendPacketAndClose(session, chirp::gateway::LOGOUT_RESP, pkt.sequence(), resp.SerializeAsString());
      break;
    }
    chirp::chat::runtime::SendPacket(session, chirp::gateway::LOGOUT_RESP, pkt.sequence(), resp.SerializeAsString());
    break;
  }
  case chirp::gateway::SEND_MESSAGE_REQ: {
    chirp::chat::SendMessageRequest req;
    if (!req.ParseFromArray(pkt.body().data(), static_cast<int>(pkt.body().size()))) {
      chirp::chat::SendMessageResponse resp;
      resp.set_code(chirp::common::INVALID_PARAM);
      resp.set_server_timestamp(chirp::chat::runtime::NowMs());
      chirp::chat::runtime::SendPacket(session, chirp::gateway::SEND_MESSAGE_RESP, pkt.sequence(), resp.SerializeAsString());
      return;
    }
    const auto validation = chirp::chat::ValidateSendMessageRequest(req, authenticated_user_id);
    if (validation != chirp::common::OK) {
      chirp::chat::SendMessageResponse resp;
      resp.set_code(validation);
      resp.set_server_timestamp(chirp::chat::runtime::NowMs());
      chirp::chat::runtime::SendPacket(session, chirp::gateway::SEND_MESSAGE_RESP, pkt.sequence(), resp.SerializeAsString());
      return;
    }

    // Per-user send window on the validated path: auth/param failures above
    // do not consume send budget.
    if (features.rate_limiter) {
      const auto gate = features.rate_limiter->CheckSend(authenticated_user_id);
      if (!gate.allowed) {
        chirp::chat::SendMessageResponse resp;
        resp.set_code(chirp::common::RATE_LIMITED);
        resp.set_server_timestamp(chirp::chat::runtime::NowMs());
        chirp::chat::runtime::SendPacket(session, chirp::gateway::SEND_MESSAGE_RESP,
                                         pkt.sequence(), resp.SerializeAsString());
        return;
      }
    }

    // Cross-plane reply (TODO 56): a channel_id prefixed "<game_id>:<bare>"
    // is a reply into the game plane — resolve the sender's game_user_id and
    // inject the message into the game_chat spoke that registered the game.
    // Sits after the rate limiter on purpose: a cross-plane send consumes
    // send budget like any other. Only hub mode has spokes to resolve
    // against; otherwise there is nothing to intercept.
    if (features.directory && features.hub) {
      const auto outcome = features.directory->RelayGameReply(
          authenticated_user_id, req.channel_id(), req.content(),
          /*client_msg_id=*/"",
          [hub = features.hub](const std::string& game_id) {
            return hub->service_id_for_game(game_id);
          },
          [hub = features.hub](const std::string& service_id,
                               const chirp::gateway::PeerInjectMessageNotify& notify) {
            return hub->SendInject(service_id, notify);
          });
      if (outcome != chirp::chat::PlayerDirectory::GameReplyOutcome::kNoGamePrefix) {
        chirp::chat::SendMessageResponse resp;
        switch (outcome) {
          case chirp::chat::PlayerDirectory::GameReplyOutcome::kSent:
            // The game plane mints its own message id; the App client gets
            // the acceptance only.
            resp.set_code(chirp::common::OK);
            break;
          case chirp::chat::PlayerDirectory::GameReplyOutcome::kUnboundPlayer:
            resp.set_code(chirp::common::INVALID_PARAM);
            break;
          case chirp::chat::PlayerDirectory::GameReplyOutcome::kUnknownGame:
          case chirp::chat::PlayerDirectory::GameReplyOutcome::kSendFailed:
            resp.set_code(chirp::common::SERVER_UNAVAILABLE);
            break;
          case chirp::chat::PlayerDirectory::GameReplyOutcome::kNoGamePrefix:
            break;  // handled above; unreachable
        }
        resp.set_server_timestamp(chirp::chat::runtime::NowMs());
        chirp::chat::runtime::SendPacket(session, chirp::gateway::SEND_MESSAGE_RESP,
                                         pkt.sequence(), resp.SerializeAsString());
        return;
      }
    }

    chirp::chat::ChatMessage msg;
    msg.set_message_id(chirp::chat::runtime::GenerateMessageId());
    msg.set_sender_id(req.sender_id());
    msg.set_receiver_id(req.receiver_id());
    msg.set_channel_type(req.channel_type());
    msg.set_msg_type(req.msg_type());
    msg.set_content(req.content());
    msg.set_timestamp(chirp::chat::runtime::NowMs());
    if (req.channel_type() == chirp::chat::PRIVATE) {
      msg.set_channel_id(store->PrivateChannelId(req.sender_id(), req.receiver_id()));
    } else {
      msg.set_channel_id(req.channel_id());
    }

    // Enforce mention permissions (@everyone/@here cooldown) before the
    // message becomes visible to anyone.
    chirp::common::ErrorCode mention_code = chirp::common::OK;
    if (!features.mentions.ProcessOutgoingMessage(msg, &mention_code)) {
      chirp::chat::SendMessageResponse resp;
      resp.set_code(mention_code);
      resp.set_server_timestamp(chirp::chat::runtime::NowMs());
      chirp::chat::runtime::SendPacket(session, chirp::gateway::SEND_MESSAGE_RESP, pkt.sequence(), resp.SerializeAsString());
      return;
    }

    store->AddMessage(msg);
    // Let the receipt/reaction/edit features resolve the channel of this
    // message later (their requests only carry a message_id).
    features.receipts.TrackMessage(msg.message_id(), msg.channel_type(), msg.channel_id());
    features.reactions.TrackMessage(msg.message_id(), msg.channel_type(), msg.channel_id());
    features.edits.TrackMessage(msg.message_id(), msg.channel_type(), msg.channel_id());
    features.edits.RegisterMessage(msg.message_id(), msg.sender_id(), msg.content());

    chirp::chat::SendMessageResponse resp;
    resp.set_message_id(msg.message_id());
    resp.set_server_timestamp(msg.timestamp());

    if (req.channel_type() == chirp::chat::PRIVATE) {
      // NPC-addressed private messages bypass player delivery: the receiver
      // is not a user, so there is no session to deliver to and nothing to
      // queue offline. The utterance is published to the NPC dialog service
      // (fire-and-forget: OK here means accepted, not that a reply will
      // come); the NPC answers through the injection path.
      if (features.hub_peer && !features.npc_service_id.empty() &&
          chirp::chat::npc::IsNpcReceiver(features.npc_prefix, req.receiver_id())) {
        features.hub_peer->SendEventPublish(
            chirp::chat::npc::MakeUtteranceEvent(msg, features.npc_prefix,
                                                 features.npc_service_id),
            [message_id = msg.message_id()](chirp::common::ErrorCode code) {
              if (code != chirp::common::OK) {
                Logger::Instance().Warn(
                    "npc utterance publish failed for message " + message_id +
                    " (code " + std::to_string(static_cast<int>(code)) + ")");
              }
            });
        resp.set_code(chirp::common::OK);
        chirp::chat::runtime::SendPacket(session, chirp::gateway::SEND_MESSAGE_RESP,
                                         pkt.sequence(), resp.SerializeAsString());
        break;
      }
      auto receivers = HealthyUserSessions(state, req.receiver_id());
      // Deliver to every device of the receiver. A connection that already
      // sent FIN would "consume" the message without ever reading it, so
      // half-closed devices are not written to; the offline decision stays
      // user-level (the offline queue has no per-device split yet), meaning
      // any healthy device accepting the message counts as delivered.
      // Ack-capable devices additionally hold the delivery until
      // MESSAGE_ACK - no ack before the timeout requeues it instead.
      if (!receivers.empty()) {
        resp.set_code(chirp::common::OK);
        TrackAckIfCapable(features.acks, receivers, msg.message_id(),
                          req.receiver_id(), msg.SerializeAsString());
        for (const auto& recv : receivers) {
          chirp::chat::runtime::SendChatNotify(recv, msg);
        }
      } else {
        resp.set_code(chirp::common::TARGET_OFFLINE);
        store->AddOffline(req.receiver_id(), msg);
        features.push.NotifyOffline(msg, req.receiver_id());
      }
    } else {
      // GROUP channel: fan the message out to every member via the group
      // handlers; members not online right now land in the offline queue.
      if (features.groups.IsMember(req.channel_id(), req.sender_id())) {
        resp.set_code(chirp::common::OK);
        auto offline = features.groups.BroadcastGroupMessage(req.channel_id(), req.sender_id(), msg);
        for (const auto& member_id : offline) {
          store->AddOffline(member_id, msg);
          features.push.NotifyOffline(msg, member_id);
        }
      } else {
        resp.set_code(chirp::common::AUTH_FAILED);
      }
    }
    // Spoke mode: relay non-private channels up to the app-plane hub.
    // Best-effort by design — an unregistered link drops the uplink and the
    // game plane keeps running.
    if (features.spoke && features.spoke->registered() &&
        resp.code() == chirp::common::OK &&
        req.channel_type() != chirp::chat::PRIVATE) {
      chirp::gateway::ChannelMessageNotify notify;
      notify.set_game_id(features.spoke_game_id);
      notify.set_channel_id(req.channel_id());
      *notify.mutable_message() = msg;
      features.spoke->SendChannelMessage(notify);
    }
    chirp::chat::runtime::SendPacket(session, chirp::gateway::SEND_MESSAGE_RESP, pkt.sequence(), resp.SerializeAsString());
    break;
  }
  case chirp::gateway::MESSAGE_ACK: {
    // The client confirms a live CHAT_MESSAGE_NOTIFY reached it; clear the
    // pending delivery so the ack timeout never requeues it. Acks for
    // messages nobody tracked (or that were already requeued) are handled
    // inside the manager; unauthenticated senders are ignored.
    chirp::chat::MessageAck ack;
    if (ack.ParseFromArray(pkt.body().data(), static_cast<int>(pkt.body().size())) &&
        !ack.message_id().empty() && !authenticated_user_id.empty() &&
        (ack.user_id().empty() || ack.user_id() == authenticated_user_id)) {
      if (features.acks && features.acks->Acknowledge(ack.message_id())) {
        Logger::Instance().Info("message acked id=" + ack.message_id() +
                                " user=" + authenticated_user_id);
      }
    }
    break;
  }
  case chirp::gateway::GET_HISTORY_REQ: {
    chirp::chat::GetHistoryRequest req;
    if (!req.ParseFromArray(pkt.body().data(), static_cast<int>(pkt.body().size()))) {
      chirp::chat::GetHistoryResponse resp;
      resp.set_code(chirp::common::INVALID_PARAM);
      resp.set_has_more(false);
      chirp::chat::runtime::SendPacket(session, chirp::gateway::GET_HISTORY_RESP, pkt.sequence(), resp.SerializeAsString());
      return;
    }
    const auto validation = chirp::chat::ValidateGetHistoryRequest(req, authenticated_user_id);
    if (validation != chirp::common::OK) {
      chirp::chat::GetHistoryResponse resp;
      resp.set_code(validation);
      resp.set_has_more(false);
      chirp::chat::runtime::SendPacket(session, chirp::gateway::GET_HISTORY_RESP, pkt.sequence(), resp.SerializeAsString());
      return;
    }

    bool has_more = false;
    auto msgs = store->GetHistory(req.channel_type(), req.channel_id(), req.before_timestamp(), req.limit(), &has_more);

    chirp::chat::GetHistoryResponse resp;
    resp.set_code(chirp::common::OK);
    resp.set_has_more(has_more);
    for (auto& m : msgs) {
      *resp.add_messages() = std::move(m);
    }
    chirp::chat::runtime::SendPacket(session, chirp::gateway::GET_HISTORY_RESP, pkt.sequence(), resp.SerializeAsString());
    break;
  }
  case chirp::gateway::CREATE_GROUP_REQ: {
    chirp::chat::CreateGroupRequest req;
    if (!req.ParseFromArray(pkt.body().data(), static_cast<int>(pkt.body().size()))) {
      chirp::chat::CreateGroupResponse resp;
      resp.set_code(chirp::common::INVALID_PARAM);
      chirp::chat::runtime::SendPacket(session, chirp::gateway::CREATE_GROUP_RESP, pkt.sequence(), resp.SerializeAsString());
      return;
    }
    auto resp = features.groups.HandleCreateGroup(req, authenticated_user_id);
    chirp::chat::runtime::SendPacket(session, chirp::gateway::CREATE_GROUP_RESP, pkt.sequence(), resp.SerializeAsString());
    break;
  }
  case chirp::gateway::JOIN_GROUP_REQ: {
    chirp::chat::JoinGroupRequest req;
    if (!req.ParseFromArray(pkt.body().data(), static_cast<int>(pkt.body().size()))) {
      chirp::chat::JoinGroupResponse resp;
      resp.set_code(chirp::common::INVALID_PARAM);
      chirp::chat::runtime::SendPacket(session, chirp::gateway::JOIN_GROUP_RESP, pkt.sequence(), resp.SerializeAsString());
      return;
    }
    auto resp = features.groups.HandleJoinGroup(req, authenticated_user_id);
    chirp::chat::runtime::SendPacket(session, chirp::gateway::JOIN_GROUP_RESP, pkt.sequence(), resp.SerializeAsString());
    break;
  }
  case chirp::gateway::LEAVE_GROUP_REQ: {
    chirp::chat::LeaveGroupRequest req;
    if (!req.ParseFromArray(pkt.body().data(), static_cast<int>(pkt.body().size()))) {
      chirp::chat::LeaveGroupResponse resp;
      resp.set_code(chirp::common::INVALID_PARAM);
      chirp::chat::runtime::SendPacket(session, chirp::gateway::LEAVE_GROUP_RESP, pkt.sequence(), resp.SerializeAsString());
      return;
    }
    auto resp = features.groups.HandleLeaveGroup(req, authenticated_user_id);
    chirp::chat::runtime::SendPacket(session, chirp::gateway::LEAVE_GROUP_RESP, pkt.sequence(), resp.SerializeAsString());
    break;
  }
  case chirp::gateway::KICK_MEMBER_REQ: {
    chirp::chat::KickMemberRequest req;
    if (!req.ParseFromArray(pkt.body().data(), static_cast<int>(pkt.body().size()))) {
      chirp::chat::KickMemberResponse resp;
      resp.set_code(chirp::common::INVALID_PARAM);
      chirp::chat::runtime::SendPacket(session, chirp::gateway::KICK_MEMBER_RESP, pkt.sequence(), resp.SerializeAsString());
      return;
    }
    auto resp = features.groups.HandleKickMember(req, authenticated_user_id);
    chirp::chat::runtime::SendPacket(session, chirp::gateway::KICK_MEMBER_RESP, pkt.sequence(), resp.SerializeAsString());
    break;
  }
  case chirp::gateway::GET_GROUP_INFO_REQ: {
    chirp::chat::GetGroupInfoRequest req;
    if (!req.ParseFromArray(pkt.body().data(), static_cast<int>(pkt.body().size()))) {
      chirp::chat::GetGroupInfoResponse resp;
      resp.set_code(chirp::common::INVALID_PARAM);
      chirp::chat::runtime::SendPacket(session, chirp::gateway::GET_GROUP_INFO_RESP, pkt.sequence(), resp.SerializeAsString());
      return;
    }
    auto resp = features.groups.HandleGetGroupInfo(req, authenticated_user_id);
    chirp::chat::runtime::SendPacket(session, chirp::gateway::GET_GROUP_INFO_RESP, pkt.sequence(), resp.SerializeAsString());
    break;
  }
  case chirp::gateway::GET_GROUP_MEMBERS_REQ: {
    chirp::chat::GetGroupMembersRequest req;
    if (!req.ParseFromArray(pkt.body().data(), static_cast<int>(pkt.body().size()))) {
      chirp::chat::GetGroupMembersResponse resp;
      resp.set_code(chirp::common::INVALID_PARAM);
      chirp::chat::runtime::SendPacket(session, chirp::gateway::GET_GROUP_MEMBERS_RESP, pkt.sequence(), resp.SerializeAsString());
      return;
    }
    auto resp = features.groups.HandleGetGroupMembers(req, authenticated_user_id);
    chirp::chat::runtime::SendPacket(session, chirp::gateway::GET_GROUP_MEMBERS_RESP, pkt.sequence(), resp.SerializeAsString());
    break;
  }
  case chirp::gateway::GET_USER_GROUPS_REQ: {
    chirp::chat::GetUserGroupsRequest req;
    if (!req.ParseFromArray(pkt.body().data(), static_cast<int>(pkt.body().size()))) {
      chirp::chat::GetUserGroupsResponse resp;
      resp.set_code(chirp::common::INVALID_PARAM);
      chirp::chat::runtime::SendPacket(session, chirp::gateway::GET_USER_GROUPS_RESP, pkt.sequence(), resp.SerializeAsString());
      return;
    }
    auto resp = features.groups.HandleGetUserGroups(req, authenticated_user_id);
    chirp::chat::runtime::SendPacket(session, chirp::gateway::GET_USER_GROUPS_RESP, pkt.sequence(), resp.SerializeAsString());
    break;
  }
  case chirp::gateway::INVITE_TO_GROUP_REQ: {
    chirp::chat::InviteToGroupRequest req;
    if (!req.ParseFromArray(pkt.body().data(), static_cast<int>(pkt.body().size()))) {
      chirp::chat::InviteToGroupResponse resp;
      resp.set_code(chirp::common::INVALID_PARAM);
      chirp::chat::runtime::SendPacket(session, chirp::gateway::INVITE_TO_GROUP_RESP, pkt.sequence(), resp.SerializeAsString());
      return;
    }
    auto resp = features.groups.HandleInviteToGroup(req, authenticated_user_id);
    chirp::chat::runtime::SendPacket(session, chirp::gateway::INVITE_TO_GROUP_RESP, pkt.sequence(), resp.SerializeAsString());
    break;
  }
  case chirp::gateway::MARK_READ_REQ: {
    chirp::chat::MarkReadRequest req;
    if (!req.ParseFromArray(pkt.body().data(), static_cast<int>(pkt.body().size()))) {
      chirp::chat::MarkReadResponse resp;
      resp.set_code(chirp::common::INVALID_PARAM);
      chirp::chat::runtime::SendPacket(session, chirp::gateway::MARK_READ_RESP, pkt.sequence(), resp.SerializeAsString());
      return;
    }
    auto resp = features.receipts.HandleMarkRead(req, authenticated_user_id);
    chirp::chat::runtime::SendPacket(session, chirp::gateway::MARK_READ_RESP, pkt.sequence(), resp.SerializeAsString());
    break;
  }
  case chirp::gateway::GET_READ_RECEIPTS_REQ: {
    chirp::chat::GetReadReceiptsRequest req;
    if (!req.ParseFromArray(pkt.body().data(), static_cast<int>(pkt.body().size()))) {
      chirp::chat::GetReadReceiptsResponse resp;
      resp.set_code(chirp::common::INVALID_PARAM);
      chirp::chat::runtime::SendPacket(session, chirp::gateway::GET_READ_RECEIPTS_RESP, pkt.sequence(), resp.SerializeAsString());
      return;
    }
    auto resp = features.receipts.HandleGetReadReceipts(req, authenticated_user_id);
    chirp::chat::runtime::SendPacket(session, chirp::gateway::GET_READ_RECEIPTS_RESP, pkt.sequence(), resp.SerializeAsString());
    break;
  }
  case chirp::gateway::GET_UNREAD_COUNT_REQ: {
    chirp::chat::GetUnreadCountRequest req;
    if (!req.ParseFromArray(pkt.body().data(), static_cast<int>(pkt.body().size()))) {
      chirp::chat::GetUnreadCountResponse resp;
      resp.set_code(chirp::common::INVALID_PARAM);
      chirp::chat::runtime::SendPacket(session, chirp::gateway::GET_UNREAD_COUNT_RESP, pkt.sequence(), resp.SerializeAsString());
      return;
    }
    auto resp = features.receipts.HandleGetUnreadCount(req, authenticated_user_id);
    chirp::chat::runtime::SendPacket(session, chirp::gateway::GET_UNREAD_COUNT_RESP, pkt.sequence(), resp.SerializeAsString());
    break;
  }
  case chirp::gateway::TYPING_INDICATOR_NOTIFY: {
    // Inbound from the client: start/stop typing and fan out to the other
    // channel members. Notify-class messages get no response frame.
    chirp::chat::TypingIndicator req;
    if (!req.ParseFromArray(pkt.body().data(), static_cast<int>(pkt.body().size()))) {
      Logger::Instance().Warn("failed to parse TypingIndicator body");
      return;
    }
    features.typing.HandleTypingIndicator(req, authenticated_user_id);
    break;
  }
  case chirp::gateway::GET_TYPING_USERS_REQ: {
    chirp::chat::GetTypingUsersRequest req;
    if (!req.ParseFromArray(pkt.body().data(), static_cast<int>(pkt.body().size()))) {
      chirp::chat::GetTypingUsersResponse resp;
      resp.set_code(chirp::common::INVALID_PARAM);
      chirp::chat::runtime::SendPacket(session, chirp::gateway::GET_TYPING_USERS_RESP, pkt.sequence(), resp.SerializeAsString());
      return;
    }
    auto resp = features.typing.HandleGetTypingUsers(req, authenticated_user_id);
    chirp::chat::runtime::SendPacket(session, chirp::gateway::GET_TYPING_USERS_RESP, pkt.sequence(), resp.SerializeAsString());
    break;
  }
  case chirp::gateway::ADD_REACTION_REQ: {
    chirp::chat::AddReactionRequest req;
    if (!req.ParseFromArray(pkt.body().data(), static_cast<int>(pkt.body().size()))) {
      chirp::chat::AddReactionResponse resp;
      resp.set_code(chirp::common::INVALID_PARAM);
      chirp::chat::runtime::SendPacket(session, chirp::gateway::ADD_REACTION_RESP, pkt.sequence(), resp.SerializeAsString());
      return;
    }
    auto resp = features.reactions.HandleAddReaction(req, authenticated_user_id);
    chirp::chat::runtime::SendPacket(session, chirp::gateway::ADD_REACTION_RESP, pkt.sequence(), resp.SerializeAsString());
    break;
  }
  case chirp::gateway::REMOVE_REACTION_REQ: {
    chirp::chat::RemoveReactionRequest req;
    if (!req.ParseFromArray(pkt.body().data(), static_cast<int>(pkt.body().size()))) {
      chirp::chat::RemoveReactionResponse resp;
      resp.set_code(chirp::common::INVALID_PARAM);
      chirp::chat::runtime::SendPacket(session, chirp::gateway::REMOVE_REACTION_RESP, pkt.sequence(), resp.SerializeAsString());
      return;
    }
    auto resp = features.reactions.HandleRemoveReaction(req, authenticated_user_id);
    chirp::chat::runtime::SendPacket(session, chirp::gateway::REMOVE_REACTION_RESP, pkt.sequence(), resp.SerializeAsString());
    break;
  }
  case chirp::gateway::GET_REACTIONS_REQ: {
    chirp::chat::GetReactionsRequest req;
    if (!req.ParseFromArray(pkt.body().data(), static_cast<int>(pkt.body().size()))) {
      chirp::chat::GetReactionsResponse resp;
      resp.set_code(chirp::common::INVALID_PARAM);
      chirp::chat::runtime::SendPacket(session, chirp::gateway::GET_REACTIONS_RESP, pkt.sequence(), resp.SerializeAsString());
      return;
    }
    auto resp = features.reactions.HandleGetReactions(req, authenticated_user_id);
    chirp::chat::runtime::SendPacket(session, chirp::gateway::GET_REACTIONS_RESP, pkt.sequence(), resp.SerializeAsString());
    break;
  }
  case chirp::gateway::EDIT_MESSAGE_REQ: {
    chirp::chat::EditMessageRequest req;
    if (!req.ParseFromArray(pkt.body().data(), static_cast<int>(pkt.body().size()))) {
      chirp::chat::EditMessageResponse resp;
      resp.set_code(chirp::common::INVALID_PARAM);
      chirp::chat::runtime::SendPacket(session, chirp::gateway::EDIT_MESSAGE_RESP, pkt.sequence(), resp.SerializeAsString());
      return;
    }
    auto resp = features.edits.HandleEditMessage(req, authenticated_user_id);
    chirp::chat::runtime::SendPacket(session, chirp::gateway::EDIT_MESSAGE_RESP, pkt.sequence(), resp.SerializeAsString());
    break;
  }
  case chirp::gateway::DELETE_MESSAGE_REQ: {
    chirp::chat::DeleteMessageRequest req;
    if (!req.ParseFromArray(pkt.body().data(), static_cast<int>(pkt.body().size()))) {
      chirp::chat::DeleteMessageResponse resp;
      resp.set_code(chirp::common::INVALID_PARAM);
      chirp::chat::runtime::SendPacket(session, chirp::gateway::DELETE_MESSAGE_RESP, pkt.sequence(), resp.SerializeAsString());
      return;
    }
    auto resp = features.edits.HandleDeleteMessage(req, authenticated_user_id);
    chirp::chat::runtime::SendPacket(session, chirp::gateway::DELETE_MESSAGE_RESP, pkt.sequence(), resp.SerializeAsString());
    break;
  }
  case chirp::gateway::BULK_DELETE_REQ: {
    chirp::chat::BulkDeleteRequest req;
    if (!req.ParseFromArray(pkt.body().data(), static_cast<int>(pkt.body().size()))) {
      chirp::chat::BulkDeleteResponse resp;
      resp.set_code(chirp::common::INVALID_PARAM);
      chirp::chat::runtime::SendPacket(session, chirp::gateway::BULK_DELETE_RESP, pkt.sequence(), resp.SerializeAsString());
      return;
    }
    auto resp = features.edits.HandleBulkDelete(req, authenticated_user_id);
    chirp::chat::runtime::SendPacket(session, chirp::gateway::BULK_DELETE_RESP, pkt.sequence(), resp.SerializeAsString());
    break;
  }
  case chirp::gateway::GET_MENTION_SUGGESTIONS_REQ: {
    chirp::chat::GetMentionSuggestionsRequest req;
    if (!req.ParseFromArray(pkt.body().data(), static_cast<int>(pkt.body().size()))) {
      chirp::chat::GetMentionSuggestionsResponse resp;
      resp.set_code(chirp::common::INVALID_PARAM);
      chirp::chat::runtime::SendPacket(session, chirp::gateway::GET_MENTION_SUGGESTIONS_RESP, pkt.sequence(), resp.SerializeAsString());
      return;
    }
    auto resp = features.mentions.HandleGetMentionSuggestions(req, authenticated_user_id);
    chirp::chat::runtime::SendPacket(session, chirp::gateway::GET_MENTION_SUGGESTIONS_RESP, pkt.sequence(), resp.SerializeAsString());
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
    pong.set_server_time(chirp::chat::runtime::NowMs());
    chirp::chat::runtime::SendPacket(session, chirp::gateway::HEARTBEAT_PONG, pkt.sequence(), pong.SerializeAsString());
    break;
  }
  default:
    break;
  }
}

} // namespace

int main(int argc, char** argv) {
  using chirp::common::Logger;

  Logger::Instance().SetLevel(Logger::Level::kInfo);
  const uint16_t port = chirp::chat::runtime::ParseU16Arg(argc, argv, "--port", 7000);
  const uint16_t ws_port = chirp::chat::runtime::ParseU16Arg(argc, argv, "--ws_port", static_cast<uint16_t>(port + 1));
  const std::string redis_host = chirp::chat::runtime::GetArg(argc, argv, "--redis_host", "");
  const uint16_t redis_port = chirp::chat::runtime::ParseU16Arg(argc, argv, "--redis_port", 6379);
  const int offline_ttl_seconds = chirp::chat::runtime::ParseIntArg(argc, argv, "--offline_ttl", 604800);
  const std::string notification_host = chirp::chat::runtime::GetArg(argc, argv, "--notification_host", "");
  const uint16_t notification_port = chirp::chat::runtime::ParseU16Arg(argc, argv, "--notification_port", 5006);
  // Direct-entry abuse controls: per-IP login and per-user send windows
  // (Redis-backed, fail-open). Without --redis_host the limiter is inert.
  const int login_rate_limit_per_min =
      chirp::chat::runtime::ParseIntArg(argc, argv, "--login_rate_limit_per_min", 30);
  const int send_rate_limit_per_min =
      chirp::chat::runtime::ParseIntArg(argc, argv, "--send_rate_limit_per_min", 120);
  // Empty keeps the scaffold "token is user_id" login; set to an HS256 secret
  // shared with the token issuer to require verifiable, unexpired JWTs.
  const std::string token_secret = chirp::chat::runtime::GetArg(argc, argv, "--token_secret", "");
  // Internal-plane trust gate: when set, edge gateways that dial in with this
  // secret (SERVER_AUTH_REQ) get per-client pipes that skip the per-IP login
  // limiter. Empty keeps SERVER_AUTH_REQ ignored (direct entry only).
  const std::string gateway_service_secret =
      chirp::chat::runtime::GetArg(argc, argv, "--gateway_service_secret", "");
  // Hub mode: accept game_chat peer registrations (app_chat deployment).
  // The peer link listens on its own port — the main client port keeps
  // serving the client protocol only.
  const bool hub_mode = chirp::chat::runtime::ParseIntArg(argc, argv, "--hub_mode", 0) != 0;
  const uint16_t hub_peer_port =
      chirp::chat::runtime::ParseU16Arg(argc, argv, "--hub_peer_port", 8200);
  const std::string allowed_peers_str =
      chirp::chat::runtime::GetArg(argc, argv, "--allowed_peers", "");
  const int min_peer_version =
      chirp::chat::runtime::ParseIntArg(argc, argv, "--min_peer_version", 1);
  const bool allow_unknown_peers =
      chirp::chat::runtime::ParseIntArg(argc, argv, "--allow_unknown_peers", 0) != 0;
  // Spoke mode: register into an app_chat hub (game_chat deployment).
  const std::string app_chat_host =
      chirp::chat::runtime::GetArg(argc, argv, "--app_chat_host", "");
  const uint16_t app_chat_port =
      chirp::chat::runtime::ParseU16Arg(argc, argv, "--app_chat_port", 8200);
  const std::string game_service_id =
      chirp::chat::runtime::GetArg(argc, argv, "--game_service_id", "");
  const std::string game_service_secret =
      chirp::chat::runtime::GetArg(argc, argv, "--game_service_secret", "");
  const std::string game_id =
      chirp::chat::runtime::GetArg(argc, argv, "--game_id", "");
  Logger::Instance().Info("chirp_chat starting tcp=" + std::to_string(port) + " ws=" + std::to_string(ws_port) +
                          (redis_host.empty()
                               ? ""
                               : (" redis=" + redis_host + ":" + std::to_string(redis_port) +
                                  " offline_ttl=" + std::to_string(offline_ttl_seconds) +
                                  " rate_limit(login/ip/min)=" + std::to_string(login_rate_limit_per_min) +
                                  " rate_limit(send/user/min)=" + std::to_string(send_rate_limit_per_min))) +
                          " auth=" + (token_secret.empty() ? "scaffold" : "hmac-sha256") +
                          (gateway_service_secret.empty() ? "" : " internal-trust=on") +
                          (notification_host.empty()
                               ? ""
                               : (" notification=" + notification_host + ":" + std::to_string(notification_port))));

  asio::io_context io;

  std::shared_ptr<chirp::network::RedisClient> redis;
  if (!redis_host.empty()) {
    redis = std::make_shared<chirp::network::RedisClient>(redis_host, redis_port);
  }

  auto store = std::make_shared<MessageStore>(redis, offline_ttl_seconds);
  auto state = std::make_shared<chirp::network::SessionRegistry>();

  // Client delivery-ack bookkeeping: live deliveries to ack-capable sessions
  // stay pending until MESSAGE_ACK; the timeout hands them back to the
  // offline queue. --ack_timeout_ms 0 disables the feature.
  const int64_t ack_timeout_ms = chirp::chat::runtime::ParseIntArg(argc, argv, "--ack_timeout_ms", 10000);
  chirp::chat::DeliveryAckManager::Config ack_config;
  ack_config.timeout_ms = ack_timeout_ms;
  auto acks = std::make_shared<chirp::chat::DeliveryAckManager>(
      io, ack_config,
      [&store](const std::string& receiver_id, const std::string& payload) {
        store->AddOfflineBytes(receiver_id, payload);
      },
      [&store](const std::string& receiver_id, const std::string& payload) {
        store->RemoveOffline(receiver_id, payload);
      });

  chirp::chat::ChatRateLimiter::Config rate_limit_config;
  rate_limit_config.max_logins_per_minute_per_ip = login_rate_limit_per_min;
  rate_limit_config.max_sends_per_minute_per_user = send_rate_limit_per_min;
  auto rate_limiter = std::make_shared<chirp::chat::ChatRateLimiter>(redis, rate_limit_config);
  chirp::common::LoginTokenVerifier token_verifier(token_secret);

  // Offline pushes are only wired when a notification service is configured;
  // a null client turns the bridge into a no-op.
  std::shared_ptr<chirp::app_notification::NotificationClient> notification;
  if (!notification_host.empty()) {
    notification = std::make_shared<chirp::app_notification::NotificationClient>(
        io, notification_host, notification_port);
  }
  chirp::chat::PushBridge push(notification);

  // Delivers group notifications to every live session of a member; false
  // means the member has no session right now.
  chirp::chat::GroupMemberNotifier notify_member =
      [state](const std::string& user_id, chirp::gateway::MsgID msg_id,
              const google::protobuf::Message& body) -> bool {
    const auto recvs = chirp::network::GetUserSessions(state, user_id);
    if (recvs.empty()) {
      return false;
    }
    const std::string payload = body.SerializeAsString();
    for (const auto& recv : recvs) {
      chirp::chat::runtime::SendPacket(recv, msg_id, 0, payload);
    }
    return true;
  };

  chirp::chat::GroupManager groups;
  chirp::chat::GroupHandlers group_handlers(groups, notify_member);

  // Resolves who should receive a channel broadcast: the other party for
  // private channels, the group membership otherwise.
  chirp::chat::ChannelMemberResolver resolve_members =
      [&groups](chirp::chat::ChannelType channel_type, const std::string& channel_id,
                const std::string& exclude_user_id) -> std::vector<std::string> {
    if (channel_type == chirp::chat::PRIVATE) {
      const size_t sep = channel_id.find('|');
      if (sep == std::string::npos || sep == 0 || sep + 1 >= channel_id.size()) {
        return {};
      }
      std::string left = channel_id.substr(0, sep);
      std::string right = channel_id.substr(sep + 1);
      const std::string& other = (left == exclude_user_id) ? right : left;
      if (other.empty() || other == exclude_user_id) {
        return {};
      }
      return {other};
    }
    std::vector<std::string> out;
    for (const auto& member : groups.GetMembers(channel_id)) {
      if (member.user_id() != exclude_user_id) {
        out.push_back(member.user_id());
      }
    }
    return out;
  };

  chirp::chat::ReadReceiptManager receipts;
  chirp::chat::TypingConfig typing_config;
  chirp::chat::TypingManager typing(typing_config);
  chirp::chat::ReactionManager reactions;
  chirp::chat::MessageEditManager edits;
  chirp::chat::MentionManager mentions;

  // Resolves moderator rights in a channel: group roles for group-style
  // channels, never for private channels.
  chirp::chat::ChannelModeratorChecker is_moderator =
      [&groups](chirp::chat::ChannelType channel_type, const std::string& channel_id,
                const std::string& user_id) -> bool {
    if (channel_type != chirp::chat::GUILD) {
      return false;
    }
    for (const auto& member : groups.GetMembers(channel_id)) {
      if (member.user_id() == user_id) {
        return member.role() >= chirp::chat::MODERATOR;
      }
    }
    return false;
  };

  chirp::chat::ReadReceiptHandlers receipt_handlers(receipts, resolve_members, notify_member);
  chirp::chat::TypingHandlers typing_handlers(typing, resolve_members, notify_member);
  chirp::chat::ReactionHandlers reaction_handlers(reactions, resolve_members, notify_member);
  chirp::chat::MessageEditHandlers edit_handlers(edits, resolve_members, is_moderator, notify_member);
  chirp::chat::MentionHandlers mention_handlers(mentions, is_moderator);

  FeatureHandlers features{.groups = group_handlers, .receipts = receipt_handlers,
                           .typing = typing_handlers, .reactions = reaction_handlers,
                           .edits = edit_handlers, .mentions = mention_handlers,
                           .push = push, .npc_service_id = {},
                           .gateway_secret = {}, .trusted_conns = nullptr};
  features.rate_limiter = rate_limiter.get();
  features.token_verifier = &token_verifier;
  features.acks = acks.get();
  features.gateway_secret = gateway_service_secret;
  features.trusted_conns =
      std::make_shared<std::unordered_set<const chirp::network::Session*>>();

  // App-plane player directory (WP-8): the identity/subscription/unread
  // registries relocated from game_server_gateway, plus the hub fan-out
  // tail. Optional per-registry Redis write-through keeps records across
  // app_chat restarts; empty hosts stay memory-only.
  const std::string binding_redis_host =
      chirp::chat::runtime::GetArg(argc, argv, "--binding_redis_host", "");
  const uint16_t binding_redis_port =
      chirp::chat::runtime::ParseU16Arg(argc, argv, "--binding_redis_port", 6379);
  const std::string subscription_redis_host =
      chirp::chat::runtime::GetArg(argc, argv, "--subscription_redis_host", "");
  const uint16_t subscription_redis_port =
      chirp::chat::runtime::ParseU16Arg(argc, argv, "--subscription_redis_port", 6379);
  const std::string unread_redis_host =
      chirp::chat::runtime::GetArg(argc, argv, "--unread_redis_host", "");
  const uint16_t unread_redis_port =
      chirp::chat::runtime::ParseU16Arg(argc, argv, "--unread_redis_port", 6379);
  chirp::chat::PlayerDirectory::Options directory_options;
  directory_options.max_fanout_per_message = static_cast<size_t>(chirp::chat::runtime::ParseIntArg(
      argc, argv, "--max_fanout_per_message", 10000));
  if (!binding_redis_host.empty()) {
    directory_options.identities_redis = [host = binding_redis_host, port = binding_redis_port] {
      return std::make_unique<chirp::network::RedisClient>(host, port);
    };
  }
  if (!subscription_redis_host.empty()) {
    directory_options.subscriptions_redis = [host = subscription_redis_host,
                                             port = subscription_redis_port] {
      return std::make_unique<chirp::network::RedisClient>(host, port);
    };
  }
  if (!unread_redis_host.empty()) {
    directory_options.unread_redis = [host = unread_redis_host, port = unread_redis_port] {
      return std::make_unique<chirp::network::RedisClient>(host, port);
    };
  }
  // One private copy per subscriber through the same delivery tail as an
  // ordinary private SEND_MESSAGE: canonical channel id, healthy-device
  // fan-out, ack tracking, offline queue + push when nobody is online.
  directory_options.deliver_copy =
      [&store, &state, &features](const std::string& player_id,
                                  const chirp::gateway::ChannelMessageNotify& notify) {
        chirp::chat::ChatMessage copy = notify.message();
        copy.set_message_id(chirp::chat::runtime::GenerateMessageId());
        // The App client answers to the namespaced game identity — the
        // same "{game_id}:" prefix the cross-plane reply path parses.
        copy.set_sender_id(notify.game_id() + ":" + notify.message().sender_id());
        copy.set_receiver_id(player_id);
        copy.set_channel_type(chirp::chat::PRIVATE);
        copy.set_channel_id(store->PrivateChannelId(copy.sender_id(), player_id));
        copy.set_sender_kind(chirp::chat::SENDER_USER);
        store->AddMessage(copy);
        auto receivers = HealthyUserSessions(state, player_id);
        if (!receivers.empty()) {
          TrackAckIfCapable(features.acks, receivers, copy.message_id(), player_id,
                            copy.SerializeAsString());
          for (const auto& recv : receivers) {
            chirp::chat::runtime::SendChatNotify(recv, copy);
          }
        } else {
          store->AddOffline(player_id, copy);
          features.push.NotifyOffline(copy, player_id);
        }
      };
  chirp::chat::PlayerDirectory directory(std::move(directory_options));
  directory.LoadAll();
  features.directory = &directory;

  // Hub mode: accept game_chat peer registrations on the dedicated peer
  // port (app_chat deployment). Registration runs on the hub's own
  // acceptor; the main client port keeps serving the client protocol only.
  std::shared_ptr<chirp::network::ChatPeerHub> chat_hub;
  if (hub_mode) {
    chirp::network::ChatPeerHub::Options hub_opts;
    hub_opts.port = hub_peer_port;
    hub_opts.min_peer_version = min_peer_version;
    hub_opts.allow_unknown_peers = allow_unknown_peers;
    // Parse allowed_peers: "id1:secret1,id2:secret2"
    if (!allowed_peers_str.empty()) {
      std::string s = allowed_peers_str;
      while (!s.empty()) {
        auto comma = s.find(',');
        std::string pair = s.substr(0, comma);
        auto colon = pair.find(':');
        if (colon != std::string::npos) {
          hub_opts.allowed_peers[pair.substr(0, colon)] = pair.substr(colon + 1);
        }
        if (comma == std::string::npos) break;
        s = s.substr(comma + 1);
      }
    }
    // Assign (not declare) so the signal handler below sees the hub and
    // stops it on shutdown — an inner `auto chat_hub = ...` would shadow
    // the outer declaration and leave Stop() a null no-op.
    chat_hub = chirp::network::ChatPeerHub::Create(
        io, std::move(hub_opts),
        [](const std::string& service_id, const std::string& peer_game_id,
           int32_t version,
           const std::vector<chirp::gateway::PeerCapability>&) {
          Logger::Instance().Info("peer registered: service_id=" + service_id +
                                  " game_id=" + peer_game_id +
                                  " version=" + std::to_string(version));
        },
        [](const std::string& service_id, const std::string& reason) {
          Logger::Instance().Info("peer dropped: service_id=" + service_id +
                                  " reason=" + reason);
        },
        [&directory](const std::string& service_id,
                     const chirp::gateway::ChannelMessageNotify& notify) {
          // Cross-plane fan-out (TODO 55): resolve the channel's
          // subscribers in the player directory and hand each one a
          // private copy (plus one unread badge increment per copy).
          const size_t copies = directory.FanoutChannelMessage(notify);
          Logger::Instance().Info(
              "channel message uplink: service_id=" + service_id +
              " game_id=" + notify.game_id() +
              " channel=" + notify.channel_id() +
              " copies=" + std::to_string(copies));
        });
    chat_hub->Start();
    features.hub = chat_hub.get();
    Logger::Instance().Info("hub mode enabled: peer_port=" +
                            std::to_string(chat_hub->port()) +
                            " min_version=" + std::to_string(min_peer_version));
  }

  // Spoke mode: register into an app_chat hub (game_chat deployment).
  // Needs host + service_id + game_id all set; anything missing keeps the
  // spoke fully off.
  if (!app_chat_host.empty() && !game_service_id.empty() && !game_id.empty()) {
    chirp::network::ChatPeerLink::Options link_opts;
    link_opts.host = app_chat_host;
    link_opts.port = app_chat_port;
    link_opts.service_id = game_service_id;
    link_opts.secret = game_service_secret;
    link_opts.game_id = game_id;
    link_opts.supported_features = {chirp::gateway::RELAY_READ_RECEIPTS,
                                    chirp::gateway::RELAY_TYPING,
                                    chirp::gateway::RELAY_PRESENCE};
    features.spoke = chirp::network::ChatPeerLink::Create(
        io, std::move(link_opts),
        [game_id](int32_t version,
                  const std::vector<chirp::gateway::PeerCapability>&) {
          Logger::Instance().Info("spoke registered to hub: game_id=" + game_id +
                                  " negotiated_version=" + std::to_string(version));
        },
        // A hub never sends CHANNEL_MESSAGE_NOTIFY downstream; leave the
        // handler unset (an unexpected frame is logged by the link).
        nullptr,
        // Hub -> spoke player reply: the hub already resolved
        // player_id -> game_user_id, the spoke never sees player ids.
        // Delivered through the same store + healthy-session tail as a
        // private message, offline queue included.
        [&store, &state](const chirp::gateway::PeerInjectMessageNotify& inject) {
          chirp::chat::ChatMessage msg;
          msg.set_message_id(chirp::chat::runtime::GenerateMessageId());
          msg.set_sender_id(inject.sender_id());
          msg.set_receiver_id(inject.channel_id());  // channel_id carries the target user
          msg.set_channel_type(chirp::chat::PRIVATE);
          msg.set_channel_id(inject.channel_id());
          msg.set_msg_type(chirp::chat::TEXT);
          msg.set_content(inject.content());
          msg.set_timestamp(chirp::chat::runtime::NowMs());
          msg.set_sender_kind(chirp::chat::SENDER_USER);
          store->AddMessage(msg);
          auto receivers = HealthyUserSessions(state, inject.channel_id());
          for (auto& s : receivers) {
            chirp::chat::runtime::SendPacket(
                s, chirp::gateway::CHAT_MESSAGE_NOTIFY, 0, msg.SerializeAsString());
          }
          if (receivers.empty()) {
            store->AddOffline(inject.channel_id(), msg);
          }
        },
        []() {
          Logger::Instance().Warn("spoke lost the hub connection, will retry");
        });
    features.spoke_game_id = game_id;
    features.spoke->Start();
    Logger::Instance().Info("spoke mode enabled: game_id=" + game_id +
                            " -> hub=" + app_chat_host + ":" +
                            std::to_string(app_chat_port));
  }

  // Server-plane injection: when --server_gateway_host is set, chat dials the
  // hub as an internal service and delivers forwarded injections through the
  // same store/deliver tail as SEND_MESSAGE (minus mention enforcement -
  // senders are non-user identities, never membership-checked).
  const std::string hub_host = chirp::chat::runtime::GetArg(argc, argv, "--server_gateway_host", "");
  // NPC dialog uplink: player -> NPC private messages become events published
  // to the NPC dialog service over the hub peer. Empty --npc_service_id keeps
  // the feature off entirely.
  const std::string npc_service_id =
      chirp::chat::runtime::GetArg(argc, argv, "--npc_service_id", "");
  const std::string npc_prefix =
      chirp::chat::runtime::GetArg(argc, argv, "--npc_prefix", "npc:");
  if (!npc_service_id.empty() && hub_host.empty()) {
    Logger::Instance().Warn(
        "--npc_service_id is set but --server_gateway_host is not; the NPC "
        "uplink stays disabled");
  }
  std::shared_ptr<chirp::network::ServerGatewayPeer> hub_peer;
  if (!hub_host.empty()) {
    const uint16_t hub_port = chirp::chat::runtime::ParseU16Arg(
        argc, argv, "--server_gateway_port", 8100);
    chirp::chat::InjectHooks hooks;
    hooks.private_channel_id =
        [&store](const std::string& a, const std::string& b) {
          return store->PrivateChannelId(a, b);
        };
    hooks.store_message =
        [&store, &features](const chirp::chat::ChatMessage& msg) {
          store->AddMessage(msg);
          features.receipts.TrackMessage(msg.message_id(), msg.channel_type(), msg.channel_id());
          features.reactions.TrackMessage(msg.message_id(), msg.channel_type(), msg.channel_id());
          features.edits.TrackMessage(msg.message_id(), msg.channel_type(), msg.channel_id());
          features.edits.RegisterMessage(msg.message_id(), msg.sender_id(), msg.content());
        };
    hooks.deliver_private =
        [state, &features](const std::string& receiver_id,
                const chirp::chat::ChatMessage& msg) -> bool {
      const auto receivers = HealthyUserSessions(state, receiver_id);
      if (receivers.empty()) {
        return false;
      }
      // Injected private replies are tracked like SEND_MESSAGE deliveries,
      // then fanned out to every device of the receiver.
      TrackAckIfCapable(features.acks, receivers, msg.message_id(),
                        receiver_id, msg.SerializeAsString());
      for (const auto& recv : receivers) {
        chirp::chat::runtime::SendChatNotify(recv, msg);
      }
      return true;
    };
    hooks.queue_offline =
        [&store, &features](const std::string& user_id, const chirp::chat::ChatMessage& msg) {
          store->AddOffline(user_id, msg);
          features.push.NotifyOffline(msg, user_id);
        };
    hooks.broadcast_channel =
        [&features](const std::string& channel_id,
                    const chirp::chat::ChatMessage& msg) {
          return features.groups.BroadcastGroupMessage(channel_id, msg.sender_id(), msg);
        };

    chirp::network::ServerGatewayPeer::Options hub_options;
    hub_options.host = hub_host;
    hub_options.port = hub_port;
    hub_options.service_id =
        chirp::chat::runtime::GetArg(argc, argv, "--server_gateway_service", "chat");
    hub_options.secret = chirp::chat::runtime::GetArg(argc, argv, "--server_gateway_secret", "");
    hub_options.reconnect_delay_seconds = chirp::chat::runtime::ParseIntArg(
        argc, argv, "--server_gateway_reconnect", 3);

    const std::string hub_service = hub_options.service_id;
    auto consumer = std::make_shared<chirp::chat::InjectConsumer>(std::move(hooks));
    hub_peer = chirp::network::ServerGatewayPeer::Create(
        io, std::move(hub_options),
        [consumer](const chirp::game_server_gateway::InjectMessageNotify& notify) {
          consumer->HandleInject(notify);
        });
    hub_peer->Start();
    features.hub_peer = hub_peer.get();
    features.npc_service_id = npc_service_id;
    features.npc_prefix = npc_prefix;
    Logger::Instance().Info("server-plane peer enabled hub=" + hub_host + ":" +
                            std::to_string(hub_port) + " service=" + hub_service +
                            (npc_service_id.empty()
                                 ? std::string()
                                 : " npc_service=" + npc_service_id));
  }

  chirp::network::TcpServer server(
      io, port,
      [store, state, &features](std::shared_ptr<chirp::network::Session> session, std::string&& payload) {
        HandlePacket(store, state, features, session, std::move(payload));
      },
      [state, &features](std::shared_ptr<chirp::network::Session> session) {
        HandleDisconnect(state, features.acks, features.trusted_conns, session);
      });

  chirp::network::WebSocketServer ws_server(
      io, ws_port,
      [store, state, &features](std::shared_ptr<chirp::network::Session> session, std::string&& payload) {
        HandlePacket(store, state, features, session, std::move(payload));
      },
      [state, &features](std::shared_ptr<chirp::network::Session> session) {
        HandleDisconnect(state, features.acks, features.trusted_conns, session);
      });

  server.Start();
  ws_server.Start();
  acks->Start();

  asio::signal_set signals(io, SIGINT, SIGTERM);
  signals.async_wait([&](const std::error_code& /*ec*/, int /*sig*/) {
    Logger::Instance().Info("shutdown requested");
    acks->Stop();
    if (features.spoke) {
      features.spoke->Stop();
    }
    if (chat_hub) {
      chat_hub->Stop();
    }
    server.Stop();
    ws_server.Stop();
    io.stop();
  });

  io.run();
  Logger::Instance().Info("chirp_chat exited");
  return 0;
}
