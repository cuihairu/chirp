// Direct-entry packet dispatch (basic in-memory/Redis build): extracted from
// main.cc so the entry only wires runtime options and lifecycle. The packet
// dispatcher, its file-local send/kick/session-id helpers and the shared
// session-tail helpers live here; main() owns assembly and lifecycle.
#include "basic_packet_handler.h"

#include <algorithm>
#include <atomic>
#include <chrono>
#include <cstdint>
#include <cstdlib>
#include <memory>
#include <mutex>
#include <optional>
#include <string>
#include <unordered_map>
#include <unordered_set>
#include <vector>

#include <asio.hpp>

#include "chat_rate_limiter.h"
#include "delivery_ack_manager.h"
#include "history_compression.h"
#include "network/device_presence.h"
#include "network/session_registry.h"
#include "login_token_verifier.h"
#include "chat_validation.h"
#include "group_handlers.h"
#include "inject_consumer.h"
#include "inject_idempotency.h"
#include "logger.h"
#include "common/metrics.h"
#include "common/metrics_http_server.h"
#include "common/crash_handler.h"
#include "message_handlers.h"
#include "network/protobuf_framing.h"
#include "network/redis_client.h"
#include "network/session.h"
#include "network/tcp_server.h"
#include "recall_tombstone.h"
#include "network/websocket_server.h"
#include "npc_uplink.h"
#include "player_directory.h"
#include "channel_pacer.h"
#include "delivery_prefs.h"
#include "friend_graph.h"
#include "repeat_guard.h"
#include "word_filter.h"
#include "word_filter_push.h"
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

namespace chirp::chat::basic {

std::string GenerateSessionId() {
  static std::atomic<uint64_t> counter{1};
  return "chat_session_" + std::to_string(chirp::chat::runtime::NowMs()) + "_" + std::to_string(counter.fetch_add(1));
}

}  // namespace chirp::chat::basic

namespace {

// request_id 缺省(=0)兜底生成器:直连入口的裸协议客户端不带 request_id,
// SDK 与网关会在发送侧生成;这里给每包一个进程级单调键,保证 chat 侧
// 日志/转发总有可关联值(协议 4.3「缺省=连接内生成」的进程级近似,
// 避免扩 per-session 状态;显式值一律不改写)。
std::atomic<int64_t> g_direct_request_id{1};

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


}  // namespace

namespace chirp::chat::basic {

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
// ack capability. Track is idempotent per delivery subject (delivery_id, or
// message_id for the first/live delivery), so one capable device is enough;
// the ack may arrive over a different one. slot 是超时回队要落的离线桶:
// 在线扇出是「任一端接受即送达」的用户级语义,回队回 default 共享桶,任何
// 端下次登录都可领;登录补投的认领端则记自己的 slot(见补投臂)。
void TrackAckIfCapable(chirp::chat::DeliveryAckManager* acks,
                       const std::vector<std::shared_ptr<chirp::network::Session>>& sessions,
                       const std::string& message_id,
                       const std::string& delivery_id,
                       const std::string& receiver_id,
                       const std::string& slot,
                       const std::string& payload) {
  if (!acks) {
    return;
  }
  for (const auto& recv : sessions) {
    if (acks->IsCapable(recv.get())) {
      acks->Track(message_id, delivery_id, receiver_id, slot, payload);
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
  std::string device_id;
  std::string platform;
  if (chirp::network::RemoveAuthenticatedSession(state, session, &user_id, &device_id,
                                                 &platform) &&
      !user_id.empty()) {
    CHIRP_GAUGE_DEC("chirp_chat_sessions");
    chirp::common::Logger::Instance().Info("User disconnected: " + user_id);
    // 多端在线（P0）：确认下线才广播清单变更（被顶的旧会话不广播——它没有
    // 赢得这个槽位）。
    chirp::network::BroadcastDevicePresence(state, user_id, platform, device_id,
                                            /*online=*/false);
  }
}

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
  CHIRP_COUNTER("chirp_chat_packets_total", 1);

  if (pkt.request_id() == 0) {
    pkt.set_request_id(g_direct_request_id.fetch_add(1, std::memory_order_relaxed));
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
      login_resp.mutable_kick()->set_reason(chirp::network::LoginKickReason(login_req.platform()));
    }
    login_resp.set_server_time(chirp::chat::runtime::NowMs());

    if (!user_id.empty()) {
      // 多端在线（P0）：同 (user, platform) 重登录顶掉旧会话（KICK_NOTIFY
      // 带新 platform 的顶号理由），跨 platform 共存；初始在线端清单随登录
      // 响应下发，其余在线端收到本端上线事件。
      auto old =
          chirp::network::BindAuthenticatedSession(state, user_id, login_resp.session_id(),
                                                   login_req.device_id(), session,
                                                   login_req.platform(),
                                                   login_req.level(),
                                                   login_req.supports_gzip());
      if (old && old.get() != session.get()) {
        KickSession(old, chirp::network::LoginKickReason(login_req.platform()));
      }
      // Slot accounting (same top-kick-safe caliber as the enhanced entry:
      // only a null displaced session admits a new (user, platform) slot).
      if (!old) {
        CHIRP_GAUGE_INC("chirp_chat_sessions");
      }
      CHIRP_COUNTER("chirp_chat_logins_total", 1);
      chirp::network::FillOnlineDevices(state, user_id, &login_resp, session.get());
      chirp::network::BroadcastDevicePresence(state, user_id, login_req.platform(),
                                              chirp::network::NormalizeDeviceId(login_req.device_id()),
                                              /*online=*/true, session.get());
      if (features.acks && login_req.supports_message_ack()) {
        features.acks->MarkCapable(session);
      }
    }

    chirp::chat::runtime::SendPacket(session, chirp::gateway::LOGIN_RESP, pkt.sequence(), login_resp.SerializeAsString());
    if (!user_id.empty()) {
      // 补投臂(P1-5 per-device 拆分):弹本端桶 ∪ default 共享桶。本端桶里
      // 是先前认领过、ack 超时回队的副本;default 桶是无主副本(入队时无任何
      // 在线端),任何端登录先到先得。
      const std::string refill_slot =
          chirp::network::NormalizePlatformId(login_req.platform());
      auto offline = store->PopOffline(user_id, refill_slot);
      for (const auto& m : offline) {
        // delivery_id(P1-5):补投副本要有自己的投递主语。空(首次补投)
        // 铸新值;ack 超时回队的副本保留原值——同一次投递的重投同 id,
        // 消费端按 delivery_id 丢弃重复(AT_MOST_ONCE dedup 位),
        // UI 层按 message_id 幂等。
        auto delivered = m;
        if (delivered.delivery_id().empty()) {
          delivered.set_delivery_id(chirp::chat::runtime::GenerateDeliveryId());
        }
        // Refills are tracked like live deliveries: if this session dies
        // before acking, the message returns to the offline queue instead of
        // being consumed by a zombie connection. 序列化在铸 id 之后,
        // 回队副本带着同一 delivery_id。Track 记认领端 slot:超时回队回本端
        // 桶,重投只找本端,不被其他端的登录截走。
        if (features.acks && features.acks->IsCapable(session.get())) {
          features.acks->Track(delivered.message_id(), delivered.delivery_id(),
                           user_id, refill_slot, delivered.SerializeAsString());
        }
        chirp::chat::runtime::SendChatNotify(session, delivered);
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

    // World-channel level gate (game_chat_features P1, 拍板形态① 登录声明):
    // the sender's LoginRequest-declared level must reach
    // --world_channel_min_level (0 = off). Sits with the auth/param class of
    // gates — a below-level send consumes neither send budget, pacing slot,
    // repeat-guard state nor filter work.
    if (features.world_channel_min_level > 0 && req.channel_type() == chirp::chat::WORLD &&
        authenticated.level < features.world_channel_min_level) {
      chirp::chat::SendMessageResponse resp;
      resp.set_code(chirp::common::LEVEL_REQUIRED);
      resp.set_server_timestamp(chirp::chat::runtime::NowMs());
      chirp::chat::runtime::SendPacket(session, chirp::gateway::SEND_MESSAGE_RESP,
                                       pkt.sequence(), resp.SerializeAsString());
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

    // Per-channel pacing (game_chat_features P0 发送频率限制): world 5s,
    // guild 2s, private 1s between sends by the same user, anchored to the
    // last allowed send. Sits after the abuse gate and before the filter —
    // a filter-rejected send still consumed its pacing slot.
    if (features.channel_pacer != nullptr &&
        !features.channel_pacer->Allow(authenticated_user_id, req.channel_type(),
                                       chirp::chat::runtime::NowMs())) {
      chirp::chat::SendMessageResponse resp;
      resp.set_code(chirp::common::RATE_LIMITED);
      resp.set_server_timestamp(chirp::chat::runtime::NowMs());
      chirp::chat::runtime::SendPacket(session, chirp::gateway::SEND_MESSAGE_RESP,
                                       pkt.sequence(), resp.SerializeAsString());
      return;
    }

    // Repeat-message mute (game_chat_features P0 重复消息检测): the third
    // consecutive identical send starts a 5-minute mute during which every
    // send is refused. Sits after pacing and before the filter.
    if (features.repeat_guard != nullptr &&
        !features.repeat_guard->Allow(authenticated_user_id, req.content(),
                                      chirp::chat::runtime::NowMs())) {
      chirp::chat::SendMessageResponse resp;
      resp.set_code(chirp::common::RATE_LIMITED);
      resp.set_server_timestamp(chirp::chat::runtime::NowMs());
      chirp::chat::runtime::SendPacket(session, chirp::gateway::SEND_MESSAGE_RESP,
                                       pkt.sequence(), resp.SerializeAsString());
      return;
    }

    // Lexicon content filter: sits after the rate limiter (a rejected send
    // still consumed budget) and before every delivery path — the cross-plane
    // intercept below included, so filtered content never reaches the game
    // plane either. kReplace filters the content in place; kReject refuses
    // with the dedicated WORD_FILTERED code.
    if (features.word_filter != nullptr && features.word_filter->enabled()) {
      std::string filtered = req.content();
      if (!features.word_filter->Filter(authenticated_user_id, &filtered)) {
        chirp::chat::SendMessageResponse resp;
        resp.set_code(chirp::common::WORD_FILTERED);
        resp.set_server_timestamp(chirp::chat::runtime::NowMs());
        chirp::chat::runtime::SendPacket(session, chirp::gateway::SEND_MESSAGE_RESP,
                                         pkt.sequence(), resp.SerializeAsString());
        return;
      }
      req.set_content(filtered);
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
    msg.set_metadata(req.metadata());
    msg.set_timestamp(chirp::chat::runtime::NowMs());
    if (req.channel_type() == chirp::chat::PRIVATE) {
      msg.set_channel_id(store->PrivateChannelId(req.sender_id(), req.receiver_id()));
    } else {
      msg.set_channel_id(req.channel_id());
    }

    // 消息引用（game_chat_features P1）：回复目标必须存在于同一会话的历史里，
    // 否则拒绝——悬空引用让客户端渲染不出被引用消息的摘要。放在 msg 构造完
    // 之后， PRIVATE 的会话键已归一化为 (sender, receiver) 有序对。
    if (!req.reply_to_message_id().empty() &&
        !store->HasMessage(msg.channel_type(), msg.channel_id(), req.reply_to_message_id())) {
      chirp::chat::SendMessageResponse resp;
      resp.set_code(chirp::common::INVALID_PARAM);
      resp.set_server_timestamp(chirp::chat::runtime::NowMs());
      chirp::chat::runtime::SendPacket(session, chirp::gateway::SEND_MESSAGE_RESP,
                                       pkt.sequence(), resp.SerializeAsString());
      return;
    }
    msg.set_reply_to_message_id(req.reply_to_message_id());

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

    // 群管理员禁言（game_chat_features P1）：群频道发送前查 GroupManager
    // 台账的禁言态，禁言中的成员回 MUTED、消息不落任何存储。判定与下方群
    // 扇出同构（非 PRIVATE 且 channel_id 命中台账 = 群频道发送，桌面侧用
    // GUILD 型、既有用例走 TEAM 型），非成员发送仍由群分支回 AUTH_FAILED，
    // 优先级不变。
    if (req.channel_type() != chirp::chat::PRIVATE &&
        features.groups.IsMember(req.channel_id(), req.sender_id()) &&
        features.groups.IsMuted(req.channel_id(), req.sender_id(),
                                chirp::chat::runtime::NowMs())) {
      chirp::chat::SendMessageResponse resp;
      resp.set_code(chirp::common::MUTED);
      resp.set_server_timestamp(chirp::chat::runtime::NowMs());
      chirp::chat::runtime::SendPacket(session, chirp::gateway::SEND_MESSAGE_RESP,
                                       pkt.sequence(), resp.SerializeAsString());
      return;
    }

    // 防骚扰私聊门（game_chat_features P2，2255-2258）：接收方开启私聊限制
    // 时，陌生人（非好友且无共同群）的私聊被拒 DM_RESTRICTED，消息不落任何
    // 存储。本人会话与 NPC 接收方不设限（对话面无门槛语义）；黑名单语义更
    // 强（静默丢弃不暴露拉黑态）且位于投递侧，两者可叠加。
    if (req.channel_type() == chirp::chat::PRIVATE &&
        req.sender_id() != req.receiver_id() &&
        !chirp::chat::npc::IsNpcReceiver(features.npc_prefix, req.receiver_id()) &&
        features.delivery_prefs->IsDmRestricted(req.receiver_id()) &&
        !(features.friend_graph != nullptr &&
          features.friend_graph->AreFriends(req.receiver_id(), req.sender_id())) &&
        !features.groups.ShareAnyGroup(req.sender_id(), req.receiver_id())) {
      chirp::chat::SendMessageResponse resp;
      resp.set_code(chirp::common::DM_RESTRICTED);
      resp.set_server_timestamp(chirp::chat::runtime::NowMs());
      chirp::chat::runtime::SendPacket(session, chirp::gateway::SEND_MESSAGE_RESP,
                                       pkt.sequence(), resp.SerializeAsString());
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
      // 黑名单：接收方拉黑了发送方时，消息整体消失——不投递也不入离线队列，
      // 但发送方仍看到 OK（不暴露拉黑态）。msg.sender_id 已经过发送校验，
      // 与认证身份等同。
      if (features.delivery_prefs->IsUserBlocked(req.receiver_id(), msg.sender_id())) {
        resp.set_code(chirp::common::OK);
        chirp::chat::runtime::SendPacket(session, chirp::gateway::SEND_MESSAGE_RESP,
                                         pkt.sequence(), resp.SerializeAsString());
        break;
      }
      auto receivers = HealthyUserSessions(state, req.receiver_id());
      // Deliver to every device of the receiver. A connection that already
      // sent FIN would "consume" the message without ever reading it, so
      // half-closed devices are not written to; the offline decision stays
      // user-level for the queue arm (no device is known when nobody is
      // online - the copy lands in the shared default bucket and any device's
      // login drains it first-come-first-served), meaning any healthy device
      // accepting the message counts as delivered. Per-device fidelity starts
      // at refill claim time: an unacked refill requeues to the claiming
      // device's bucket only. Ack-capable devices additionally hold the
      // delivery until MESSAGE_ACK - no ack before the timeout requeues it
      // instead (live deliveries requeue to the shared default bucket).
      if (!receivers.empty()) {
        resp.set_code(chirp::common::OK);
        TrackAckIfCapable(features.acks, receivers, msg.message_id(),
                          msg.delivery_id(), req.receiver_id(),
                          chirp::chat::kDefaultOfflineSlot,
                          msg.SerializeAsString());
        for (const auto& recv : receivers) {
          chirp::chat::runtime::SendChatNotify(recv, msg);
        }
      } else {
        resp.set_code(chirp::common::TARGET_OFFLINE);
        store->AddOffline(req.receiver_id(), chirp::chat::kDefaultOfflineSlot, msg);
        features.push.NotifyOffline(msg, req.receiver_id());
      }
      // Friend-DM relay into the game plane (游戏在线状态): the recipient's
      // bindings are the in-game assertion, and the per-player switch
      // (default on) gates the relay together with the friend-facing
      // status. Strictly best-effort — the response above already reflects
      // the chat delivery, so a missing spoke is a logged skip. NPC
      // receivers and blocked pairs never reach this point.
      if (features.directory && features.hub) {
        features.directory->RelayFriendMessage(
            authenticated_user_id, req.receiver_id(), msg.content(),
            /*client_msg_id=*/"",
            [hub = features.hub](const std::string& game_id) {
              return hub->service_id_for_game(game_id);
            },
            [hub = features.hub](const std::string& service_id,
                                 const chirp::gateway::PeerInjectMessageNotify& notify) {
              return hub->SendInject(service_id, notify);
            });
      }
    } else {
      // GROUP channel: fan the message out to every member via the group
      // handlers; members not online right now land in the offline queue.
      if (features.groups.IsMember(req.channel_id(), req.sender_id())) {
        resp.set_code(chirp::common::OK);
        auto offline = features.groups.BroadcastGroupMessage(req.channel_id(), req.sender_id(), msg);
        for (const auto& member_id : offline) {
          store->AddOffline(member_id, chirp::chat::kDefaultOfflineSlot, msg);
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
      if (features.acks &&
          features.acks->Acknowledge(ack.message_id(), ack.delivery_id())) {
        Logger::Instance().Info(
            "message acked id=" + ack.message_id() +
            (ack.delivery_id().empty()
                 ? " (first-delivery subject)"
                 : " dlv=" + ack.delivery_id()) +
            " user=" + authenticated_user_id);
      }
    }
    break;
  }
  case chirp::gateway::SET_CHANNEL_MUTE_REQ: {
    // Channel mute (game_chat_features P0 频道屏蔽): per-user push filter,
    // io-thread state. Non-muteable channel types (marquee, system, private)
    // answer INVALID_PARAM; the filter itself sits in the group notifier.
    chirp::chat::SetChannelMuteRequest req;
    chirp::chat::SetChannelMuteResponse resp;
    if (!req.ParseFromArray(pkt.body().data(), static_cast<int>(pkt.body().size()))) {
      resp.set_code(chirp::common::INVALID_PARAM);
    } else if (authenticated_user_id.empty()) {
      resp.set_code(chirp::common::AUTH_FAILED);
    } else if (!chirp::chat::IsMuteableChannel(req.channel_type())) {
      resp.set_code(chirp::common::INVALID_PARAM);
    } else {
      resp.set_code(chirp::common::OK);
      resp.set_muted(features.delivery_prefs->SetChannelMuted(
          authenticated_user_id, req.channel_type(), req.muted()));
    }
    resp.set_channel_type(req.channel_type());
    chirp::chat::runtime::SendPacket(session, chirp::gateway::SET_CHANNEL_MUTE_RESP,
                                     pkt.sequence(), resp.SerializeAsString());
    break;
  }
  case chirp::gateway::GET_CHANNEL_MUTES_REQ: {
    // Reports all three muteable channels with their current state.
    chirp::chat::GetChannelMutesResponse resp;
    if (authenticated_user_id.empty()) {
      resp.set_code(chirp::common::AUTH_FAILED);
    } else {
      resp.set_code(chirp::common::OK);
      for (const auto& [type, muted] : features.delivery_prefs->GetChannelMutes(authenticated_user_id)) {
        chirp::chat::ChannelMuteState* state_msg = resp.add_states();
        state_msg->set_channel_type(type);
        state_msg->set_muted(muted);
      }
    }
    chirp::chat::runtime::SendPacket(session, chirp::gateway::GET_CHANNEL_MUTES_RESP,
                                     pkt.sequence(), resp.SerializeAsString());
    break;
  }
  case chirp::gateway::BLOCK_MESSAGE_SENDER_REQ: {
    // 黑名单（game_chat_features P0）：拒绝空目标与自拉黑（INVALID_PARAM），
    // 其余记入 DeliveryPrefs。过滤本身挂在投递尾段（私聊静默丢弃、群播按
    // 成员过滤），不向发送方暴露拉黑态。
    chirp::chat::BlockMessageSenderRequest req;
    chirp::chat::BlockMessageSenderResponse resp;
    if (!req.ParseFromArray(pkt.body().data(), static_cast<int>(pkt.body().size()))) {
      resp.set_code(chirp::common::INVALID_PARAM);
    } else if (authenticated_user_id.empty()) {
      resp.set_code(chirp::common::AUTH_FAILED);
    } else if (!features.delivery_prefs->BlockUser(authenticated_user_id, req.target_user_id())) {
      resp.set_code(chirp::common::INVALID_PARAM);
    } else {
      resp.set_code(chirp::common::OK);
    }
    resp.set_target_user_id(req.target_user_id());
    chirp::chat::runtime::SendPacket(session, chirp::gateway::BLOCK_MESSAGE_SENDER_RESP,
                                     pkt.sequence(), resp.SerializeAsString());
    break;
  }
  case chirp::gateway::UNBLOCK_MESSAGE_SENDER_REQ: {
    // 解除拉黑是幂等的：解一个从未拉黑过的人也回 OK（状态本来就没变）。
    chirp::chat::UnblockMessageSenderRequest req;
    chirp::chat::UnblockMessageSenderResponse resp;
    if (!req.ParseFromArray(pkt.body().data(), static_cast<int>(pkt.body().size()))) {
      resp.set_code(chirp::common::INVALID_PARAM);
    } else if (authenticated_user_id.empty()) {
      resp.set_code(chirp::common::AUTH_FAILED);
    } else if (!features.delivery_prefs->UnblockUser(authenticated_user_id, req.target_user_id())) {
      resp.set_code(chirp::common::INVALID_PARAM);
    } else {
      resp.set_code(chirp::common::OK);
    }
    resp.set_target_user_id(req.target_user_id());
    chirp::chat::runtime::SendPacket(session, chirp::gateway::UNBLOCK_MESSAGE_SENDER_RESP,
                                     pkt.sequence(), resp.SerializeAsString());
    break;
  }
  case chirp::gateway::GET_BLOCKED_SENDERS_REQ: {
    chirp::chat::GetBlockedSendersResponse resp;
    if (authenticated_user_id.empty()) {
      resp.set_code(chirp::common::AUTH_FAILED);
    } else {
      resp.set_code(chirp::common::OK);
      for (const auto& target : features.delivery_prefs->GetBlockedUsers(authenticated_user_id)) {
        resp.add_target_user_ids(target);
      }
    }
    chirp::chat::runtime::SendPacket(session, chirp::gateway::GET_BLOCKED_SENDERS_RESP,
                                     pkt.sequence(), resp.SerializeAsString());
    break;
  }
  case chirp::gateway::WORD_FILTER_FETCH_REQ: {
    // 词库下发（docs/design-notes/word_filter.md「词库下发协议」）：登录后
    // 的客户端拉取服务端当前生效词库，发送侧预检据此镜像服务端裁决。分发
    // 逻辑（守卫/条件 GET）在两形态共用的 word_filter_push 单元。
    chirp::chat::HandleWordFilterFetch(pkt, session, features.word_filter,
                                       authenticated_user_id);
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
    // 消息压缩（game_chat_features P2）：登录声明 supports_gzip 的会话收
    // gzip 形态，其余保持明文 repeated 字段不变。
    chirp::chat::CompressHistoryResponse(
        &resp, chirp::network::SessionSupportsGzip(state, session.get()));
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
  case chirp::gateway::SET_MEMBER_ALIAS_REQ: {
    chirp::chat::SetMemberAliasRequest req;
    if (!req.ParseFromArray(pkt.body().data(), static_cast<int>(pkt.body().size()))) {
      chirp::chat::SetMemberAliasResponse resp;
      resp.set_code(chirp::common::INVALID_PARAM);
      chirp::chat::runtime::SendPacket(session, chirp::gateway::SET_MEMBER_ALIAS_RESP, pkt.sequence(), resp.SerializeAsString());
      return;
    }
    auto resp = features.groups.HandleSetMemberAlias(req, authenticated_user_id);
    chirp::chat::runtime::SendPacket(session, chirp::gateway::SET_MEMBER_ALIAS_RESP, pkt.sequence(), resp.SerializeAsString());
    break;
  }
  case chirp::gateway::SET_GROUP_MUTE_REQ: {
    chirp::chat::SetGroupMuteRequest req;
    if (!req.ParseFromArray(pkt.body().data(), static_cast<int>(pkt.body().size()))) {
      chirp::chat::SetGroupMuteResponse resp;
      resp.set_code(chirp::common::INVALID_PARAM);
      chirp::chat::runtime::SendPacket(session, chirp::gateway::SET_GROUP_MUTE_RESP, pkt.sequence(), resp.SerializeAsString());
      return;
    }
    auto resp = features.groups.HandleSetGroupMute(req, authenticated_user_id);
    chirp::chat::runtime::SendPacket(session, chirp::gateway::SET_GROUP_MUTE_RESP, pkt.sequence(), resp.SerializeAsString());
    break;
  }
  case chirp::gateway::SET_DM_RESTRICTION_REQ: {
    // 防骚扰开关（game_chat_features P2）：本人设置，回显生效后的状态。
    chirp::chat::SetDmRestrictionRequest req;
    chirp::chat::SetDmRestrictionResponse resp;
    if (!req.ParseFromArray(pkt.body().data(), static_cast<int>(pkt.body().size()))) {
      resp.set_code(chirp::common::INVALID_PARAM);
    } else if (authenticated_user_id.empty()) {
      resp.set_code(chirp::common::AUTH_FAILED);
    } else {
      features.delivery_prefs->SetDmRestricted(authenticated_user_id, req.enabled());
      resp.set_code(chirp::common::OK);
      resp.set_enabled(req.enabled());
    }
    chirp::chat::runtime::SendPacket(session, chirp::gateway::SET_DM_RESTRICTION_RESP,
                                     pkt.sequence(), resp.SerializeAsString());
    break;
  }
  case chirp::gateway::GET_DM_RESTRICTION_REQ: {
    chirp::chat::GetDmRestrictionResponse resp;
    if (authenticated_user_id.empty()) {
      resp.set_code(chirp::common::AUTH_FAILED);
    } else {
      resp.set_code(chirp::common::OK);
      resp.set_enabled(features.delivery_prefs->IsDmRestricted(authenticated_user_id));
    }
    chirp::chat::runtime::SendPacket(session, chirp::gateway::GET_DM_RESTRICTION_RESP,
                                     pkt.sequence(), resp.SerializeAsString());
    break;
  }
  case chirp::gateway::REPORT_MESSAGE_REQ: {
    chirp::chat::ReportMessageRequest req;
    if (!req.ParseFromArray(pkt.body().data(), static_cast<int>(pkt.body().size()))) {
      chirp::chat::ReportMessageResponse resp;
      resp.set_code(chirp::common::INVALID_PARAM);
      chirp::chat::runtime::SendPacket(session, chirp::gateway::REPORT_MESSAGE_RESP, pkt.sequence(), resp.SerializeAsString());
      return;
    }
    auto resp = features.reports.HandleReportMessage(req, authenticated_user_id);
    chirp::chat::runtime::SendPacket(session, chirp::gateway::REPORT_MESSAGE_RESP, pkt.sequence(), resp.SerializeAsString());
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

}  // namespace chirp::chat::basic
