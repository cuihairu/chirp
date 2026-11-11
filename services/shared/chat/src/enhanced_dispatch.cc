// Enhanced (MySQL) build packet dispatch and runtime state: extracted from
// main_enhanced.cc so the entry only wires runtime options and lifecycle.
// DistributedChatState and the packet/history handlers live here; main() in
// main_enhanced.cc owns assembly. chat_enhanced_session_tests drives these
// directly by including main_enhanced.cc (main renamed).
#include "enhanced_dispatch.h"

// Enhanced Distributed Chat Service with Hybrid Message Store
// Features: Redis+MySQL dual-write, message delivery tracking, pagination

#include <algorithm>
#include <chrono>
#include <cstdint>
#include <cstdlib>
#include <memory>
#include <mutex>
#include <optional>
#include <string>
#include <unordered_map>
#include <unordered_set>
#include <atomic>
#include <functional>
#include <random>

#include <asio.hpp>

#include "hybrid_message_store.h"
#include "chat_rate_limiter.h"
#include "channel_pacer.h"
#include "chat_validation.h"
#include "delivery_ack_manager.h"
#include "history_compression.h"
#include "delivery_prefs.h"
#include "friend_graph.h"
#include "repeat_guard.h"
#include "inject_consumer.h"
#include "inject_idempotency.h"
#include "login_token_verifier.h"
#include "message_delivery_tracker.h"
#include "group_expiry_sweeper.h"
#include "group_handlers.h"
#include "history_retention_sweeper.h"
#include "message_edit_manager.h"
#include "message_handlers.h"
#include "message_migration_worker.h"
#include "network/device_presence.h"
#include "npc_uplink.h"
#include "paginated_history_retriever.h"
#include "player_directory.h"
#include "push_bridge.h"
#include "word_filter.h"
#include "word_filter_push.h"

#include "common/metrics.h"
#include "common/metrics_http_server.h"
#include "common/crash_handler.h"

#include "network/chat_peer_hub.h"
#include "network/chat_peer_link.h"
#include "network/server_gateway_peer.h"
#include "distributed_dispatch.h"
#include "distributed_runtime.h"
#include "logger.h"
#include "network/message_router.h"
#include "network/notification_client.h"
#include "network/protobuf_framing.h"
#include "network/redis_client.h"
#include "network/session.h"
#include "network/session_registry.h"
#include "network/tcp_server.h"
#include "network/websocket_server.h"
#include "proto/auth.pb.h"
#include "proto/chat.pb.h"
#include "proto/common.pb.h"
#include "proto/game_server_gateway.pb.h"
#include "runtime_utils.h"

namespace chirp::chat::enhanced {


using chirp::chat::HybridMessageStore;
using chirp::chat::MessageDeliveryTracker;
using chirp::chat::MessageMigrationWorker;
using chirp::chat::PaginatedHistoryRetriever;
using chirp::chat::MessageStoreConfig;
using chirp::common::Logger;

namespace {
/// @brief Tell a session it was displaced and close it (same contract as the
/// basic build's KickSession).
void KickSession(const std::shared_ptr<chirp::network::Session>& session,
                 const std::string& reason) {
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
}  // namespace

std::vector<std::shared_ptr<chirp::network::Session>> HealthyLocalSessions(
    const std::shared_ptr<DistributedChatState>& state, const std::string& user_id) {
  std::vector<std::shared_ptr<chirp::network::Session>> healthy;
  for (const auto& recv : chirp::network::GetUserSessions(state->registry, user_id)) {
    if (!recv->PeerHalfClosed()) {
      healthy.push_back(recv);
    }
  }
  return healthy;
}

// Holds a delivery until MESSAGE_ACK when any target device declared the
// ack capability. Track is idempotent per delivery subject (delivery_id, or
// message_id for the first/live delivery), so one capable device is enough;
// the ack may arrive over a different one. Returns whether the delivery is
// now held for an ack. slot 是超时回队要落的离线桶:在线扇出(任一端接受即
// 送达的用户级语义)回 default 共享桶;登录补投的认领端记自己的 slot。
bool TrackAckIfCapable(chirp::chat::DeliveryAckManager* acks,
                       const std::vector<std::shared_ptr<chirp::network::Session>>& sessions,
                       const std::string& message_id,
                       const std::string& delivery_id,
                       const std::string& receiver_id,
                       const std::string& slot,
                       const std::string& payload) {
  if (!acks) {
    return false;
  }
  for (const auto& recv : sessions) {
    if (acks->IsCapable(recv.get())) {
      acks->Track(message_id, delivery_id, receiver_id, slot, payload);
      return true;
    }
  }
  return false;
}

/// @brief Convert a protocol ChatMessage into the store's MessageData.
chirp::chat::MessageData ToMessageData(const chirp::chat::ChatMessage& msg) {
  chirp::chat::MessageData data;
  data.message_id = msg.message_id();
  data.sender_id = msg.sender_id();
  data.receiver_id = msg.receiver_id();
  data.channel_id = msg.channel_id();
  data.channel_type = msg.channel_type();
  data.msg_type = msg.msg_type();
  data.content = msg.content();
  data.timestamp = msg.timestamp();
  data.reply_to_message_id = msg.reply_to_message_id();
  data.metadata = msg.metadata();
  data.sender_kind = static_cast<int>(msg.sender_kind());
  data.created_at = chirp::chat::runtime::NowMs();
  return data;
}

/// @brief Handle send message with hybrid storage
void HandleSendMessage(const chirp::chat::SendMessageRequest& req,
                      const std::shared_ptr<chirp::network::Session>& sender_session,
                      const std::shared_ptr<DistributedChatState>& state,
                      const std::shared_ptr<HybridMessageStore>& store,
                      const std::shared_ptr<MessageDeliveryTracker>& delivery_tracker,
                      chirp::chat::DeliveryAckManager* acks,
                      const chirp::chat::DeliveryPrefs* delivery_prefs,
                      const std::shared_ptr<chirp::network::MessageRouter>& router,
                      chirp::network::ServerGatewayPeer* hub_peer,
                      const std::string& npc_service_id,
                      const std::string& npc_prefix,
                      chirp::network::ChatPeerLink* spoke_link,
                      const std::string& spoke_game_id,
                      chirp::chat::MessageEditHandlers* edit_handlers,
                      chirp::chat::PlayerDirectory* directory,
                      chirp::network::ChatPeerHub* hub,
                      chirp::chat::GroupHandlers* group_handlers,
                      chirp::chat::ReactionHandlers* reaction_handlers,
                      chirp::chat::FriendGraph* friend_graph,
                      int64_t seq) {
  chirp::chat::ChatMessage msg;
  msg.set_message_id(chirp::chat::runtime::GenerateMessageId());
  msg.set_sender_id(req.sender_id());
  msg.set_receiver_id(req.receiver_id());
  msg.set_channel_type(req.channel_type());
  msg.set_msg_type(req.msg_type());
  msg.set_content(req.content());
  msg.set_timestamp(chirp::chat::runtime::NowMs());

  std::string channel_id;
  if (req.channel_type() == chirp::chat::PRIVATE) {
    if (req.receiver_id().empty()) {
      chirp::chat::SendMessageResponse resp;
      resp.set_code(chirp::common::INVALID_PARAM);
      resp.set_server_timestamp(chirp::chat::runtime::NowMs());
      chirp::chat::runtime::SendPacket(sender_session, chirp::gateway::SEND_MESSAGE_RESP, seq, resp.SerializeAsString());
      return;
    }
    channel_id = HybridMessageStore::PrivateChannelId(req.sender_id(), req.receiver_id());
  } else {
    channel_id = req.channel_id();
  }
  msg.set_channel_id(channel_id);

  // 消息引用（game_chat_features P1）：回复目标必须存在于同一会话——先查
  // Redis 热层（同步写，刚发出的消息必可见），未命中再落 MySQL 冷层。悬空
  // 引用拒绝，避免客户端渲染不出被引用消息的摘要。
  if (!req.reply_to_message_id().empty() &&
      !store->HasMessage(channel_id, req.reply_to_message_id())) {
    chirp::chat::SendMessageResponse resp;
    resp.set_code(chirp::common::INVALID_PARAM);
    resp.set_server_timestamp(chirp::chat::runtime::NowMs());
    chirp::chat::runtime::SendPacket(sender_session, chirp::gateway::SEND_MESSAGE_RESP,
                                     seq, resp.SerializeAsString());
    return;
  }
  msg.set_reply_to_message_id(req.reply_to_message_id());

  // 坐标链接（P1 COORD_LINK）：metadata 与回复目标同层校验——解析不出或
  // map_id 为空的坐标不落任何存储（basic 形态经 ValidateSendMessageRequest）。
  if (const chirp::common::ErrorCode coord_code = chirp::chat::ValidateCoordMetadata(req);
      coord_code != chirp::common::OK) {
    chirp::chat::SendMessageResponse resp;
    resp.set_code(coord_code);
    resp.set_server_timestamp(chirp::chat::runtime::NowMs());
    chirp::chat::runtime::SendPacket(sender_session, chirp::gateway::SEND_MESSAGE_RESP,
                                     seq, resp.SerializeAsString());
    return;
  }
  // 成就分享（P1 ACHIEVEMENT）：与坐标/物品链接同层同口径——解析不出或
  // achievement_id 为空的不落任何存储（basic 形态经 ValidateSendMessageRequest）。
  if (const chirp::common::ErrorCode achievement_code = chirp::chat::ValidateAchievementMetadata(req);
      achievement_code != chirp::common::OK) {
    chirp::chat::SendMessageResponse resp;
    resp.set_code(achievement_code);
    resp.set_server_timestamp(chirp::chat::runtime::NowMs());
    chirp::chat::runtime::SendPacket(sender_session, chirp::gateway::SEND_MESSAGE_RESP,
                                     seq, resp.SerializeAsString());
    return;
  }
  // 物品链接（P0 ITEM_LINK）：与坐标链接同层同口径——解析不出或 item_id
  // 为空的不落任何存储（basic 形态经 ValidateSendMessageRequest）。
  if (const chirp::common::ErrorCode item_code = chirp::chat::ValidateItemMetadata(req);
      item_code != chirp::common::OK) {
    chirp::chat::SendMessageResponse resp;
    resp.set_code(item_code);
    resp.set_server_timestamp(chirp::chat::runtime::NowMs());
    chirp::chat::runtime::SendPacket(sender_session, chirp::gateway::SEND_MESSAGE_RESP,
                                     seq, resp.SerializeAsString());
    return;
  }
  // 交易状态（P1 TRADE_STATUS）：与坐标/物品/成就链接同层同口径——解析不出
  // 或 trade_id 为空的不落任何存储（basic 形态经 ValidateSendMessageRequest）。
  if (const chirp::common::ErrorCode trade_code = chirp::chat::ValidateTradeMetadata(req);
      trade_code != chirp::common::OK) {
    chirp::chat::SendMessageResponse resp;
    resp.set_code(trade_code);
    resp.set_server_timestamp(chirp::chat::runtime::NowMs());
    chirp::chat::runtime::SendPacket(sender_session, chirp::gateway::SEND_MESSAGE_RESP,
                                     seq, resp.SerializeAsString());
    return;
  }
  // 招募卡片（P1 GUILD_RECRUIT/PARTY_RECRUIT）：与交易/成就链接同层同口径——
  // 解析不出或 guild_id/party_id 为空的不落任何存储（basic 形态经
  // ValidateSendMessageRequest）。
  if (const chirp::common::ErrorCode recruit_code = chirp::chat::ValidateRecruitMetadata(req);
      recruit_code != chirp::common::OK) {
    chirp::chat::SendMessageResponse resp;
    resp.set_code(recruit_code);
    resp.set_server_timestamp(chirp::chat::runtime::NowMs());
    chirp::chat::runtime::SendPacket(sender_session, chirp::gateway::SEND_MESSAGE_RESP,
                                     seq, resp.SerializeAsString());
    return;
  }
  // 技能链接（P1 SKILL_LINK）：与招募卡片同层同口径——解析不出或 skill_id
  // 为空的不落任何存储（basic 形态经 ValidateSendMessageRequest）。
  if (const chirp::common::ErrorCode skill_code = chirp::chat::ValidateSkillMetadata(req);
      skill_code != chirp::common::OK) {
    chirp::chat::SendMessageResponse resp;
    resp.set_code(skill_code);
    resp.set_server_timestamp(chirp::chat::runtime::NowMs());
    chirp::chat::runtime::SendPacket(sender_session, chirp::gateway::SEND_MESSAGE_RESP,
                                     seq, resp.SerializeAsString());
    return;
  }
  msg.set_metadata(req.metadata());

  // 群频道成员门（web e2e 权限回码契约，与 basic 群分支同语义）：频道命中
  // 群台账而发送者不在名册，回 AUTH_FAILED、不落任何存储。台账外频道
  // （Redis 扇出路径）不经此门——分布式形态承接游戏平面自管频道的扇出，
  // 不能一刀切。放在禁言门之前：禁言门只看成员，非成员在此先拦。
  if (group_handlers != nullptr && req.channel_type() != chirp::chat::PRIVATE &&
      group_handlers->HasGroup(channel_id) &&
      !group_handlers->IsMember(channel_id, req.sender_id())) {
    chirp::chat::SendMessageResponse resp;
    resp.set_code(chirp::common::AUTH_FAILED);
    resp.set_server_timestamp(chirp::chat::runtime::NowMs());
    chirp::chat::runtime::SendPacket(sender_session, chirp::gateway::SEND_MESSAGE_RESP,
                                     seq, resp.SerializeAsString());
    return;
  }

  // 群管理员禁言（game_chat_features P1，与 basic 同一门）：群频道发送前查
  // GroupManager 台账，禁言中的成员回 MUTED、消息不进 Redis/MySQL 任何一层。
  // 判定与投递侧群扇出同构（非 PRIVATE 且 channel_id 命中台账）——台账外
  // 频道（Redis 扇出路径）与群外成员不走此门。
  if (group_handlers != nullptr && req.channel_type() != chirp::chat::PRIVATE &&
      group_handlers->IsMember(channel_id, req.sender_id()) &&
      group_handlers->IsMuted(channel_id, req.sender_id(),
                              chirp::chat::runtime::NowMs())) {
    chirp::chat::SendMessageResponse resp;
    resp.set_code(chirp::common::MUTED);
    resp.set_server_timestamp(chirp::chat::runtime::NowMs());
    chirp::chat::runtime::SendPacket(sender_session, chirp::gateway::SEND_MESSAGE_RESP,
                                     seq, resp.SerializeAsString());
    return;
  }

  // 防骚扰私聊门（game_chat_features P2，2255-2258，与 basic 同一门）：接收方
  // 开启私聊限制时，陌生人（非好友且无共同群）的私聊被拒 DM_RESTRICTED，
  // 消息不进 Redis/MySQL 任何一层。本人会话与 NPC 接收方不设限；黑名单在
  // 投递侧（语义更强，静默丢弃），两者可叠加。
  if (req.channel_type() == chirp::chat::PRIVATE &&
      req.sender_id() != req.receiver_id() &&
      !chirp::chat::npc::IsNpcReceiver(npc_prefix, req.receiver_id()) &&
      delivery_prefs != nullptr &&
      delivery_prefs->IsDmRestricted(req.receiver_id()) &&
      !(friend_graph != nullptr &&
        friend_graph->AreFriends(req.receiver_id(), req.sender_id())) &&
      (group_handlers == nullptr ||
       !group_handlers->ShareAnyGroup(req.sender_id(), req.receiver_id()))) {
    chirp::chat::SendMessageResponse resp;
    resp.set_code(chirp::common::DM_RESTRICTED);
    resp.set_server_timestamp(chirp::chat::runtime::NowMs());
    chirp::chat::runtime::SendPacket(sender_session, chirp::gateway::SEND_MESSAGE_RESP,
                                     seq, resp.SerializeAsString());
    return;
  }

  // Store in hybrid store (Redis + MySQL)
  chirp::chat::MessageData msg_data = ToMessageData(msg);

  // Store asynchronously for better performance. The RESP has already been
  // sent as 受理 (OK) by the time this callback fires; retries happened inside
  // the store, so a failure here means the message is accepted-but-not-
  // persisted (hot tier + counter are what remain of it) — Error, not Warn
  // (评审 P1-3).
  store->StoreMessageAsync(msg_data, [msg](bool success) {
    if (!success) {
      Logger::Instance().Error("Send " + msg.message_id() +
                               " accepted but not persisted after cold-tier retries");
    }
  });

  // Respond to sender
  chirp::chat::SendMessageResponse resp;
  resp.set_code(chirp::common::OK);
  resp.set_message_id(msg.message_id());
  resp.set_server_timestamp(msg.timestamp());
  chirp::chat::runtime::SendPacket(sender_session, chirp::gateway::SEND_MESSAGE_RESP, seq, resp.SerializeAsString());

  // 撤回/编辑台账（game_chat_features P0）：DELETE_MESSAGE 请求只带
  // message_id，频道要从 TrackMessage 的台账反查；RegisterMessage 记下发送者
  // 与内容，「非发送者」判定和撤回窗口起点都靠它。与 basic 的 accept 同一时机。
  if (edit_handlers != nullptr) {
    edit_handlers->TrackMessage(msg.message_id(), msg.channel_type(), channel_id);
    edit_handlers->RegisterMessage(msg.message_id(), msg.sender_id(), msg.content());
  }
  // 表情回应与撤回同款：ADD/REMOVE_REACTION 请求只带 message_id，频道要从
  // 发送时的登记反查。与 basic 的 TrackMessage 同一时机（接受之后）。
  if (reaction_handlers != nullptr) {
    reaction_handlers->TrackMessage(msg.message_id(), msg.channel_type(), channel_id);
  }

  // Track delivery for private messages
  if (req.channel_type() == chirp::chat::PRIVATE && !req.receiver_id().empty()) {
    delivery_tracker->TrackMessage(msg.message_id(), req.receiver_id(),
                                  chirp::chat::runtime::NowMs() + 300000);  // 5 min expiry
  }

  // Route to receiver
  if (req.channel_type() == chirp::chat::PRIVATE) {
    // NPC-addressed private messages bypass player delivery: the receiver is
    // not a user, so there is nothing to route and nothing to queue offline.
    // The utterance is published to the NPC dialog service (fire-and-forget;
    // OK means accepted, not that a reply will come) and the NPC answers
    // through the injection path.
    if (hub_peer != nullptr && !npc_service_id.empty() &&
        chirp::chat::npc::IsNpcReceiver(npc_prefix, req.receiver_id())) {
      hub_peer->SendEventPublish(
          chirp::chat::npc::MakeUtteranceEvent(msg, npc_prefix, npc_service_id),
          [message_id = msg.message_id()](chirp::common::ErrorCode code) {
            if (code != chirp::common::OK) {
              Logger::Instance().Warn(
                  "npc utterance publish failed for message " + message_id +
                  " (code " + std::to_string(static_cast<int>(code)) + ")");
            } else {
              Logger::Instance().Info("npc utterance published for message " + message_id);
            }
          });
      return;
    }

    // One serialization feeds both the router fan-out and the ack pending
    // entry, so a late-ack offline removal matches byte-for-byte.
    const std::string msg_bytes = msg.SerializeAsString();
    const int64_t receivers = router->SendChatMessageCount(req.receiver_id(), msg_bytes,
      [&](const std::string& user_id) -> bool {
        // 黑名单：接收方拉黑了发送方时消息整体消失——报告"已投递"阻止调用
        // 方入离线队列，实际两端都不送（不暴露拉黑态）。
        if (delivery_prefs->IsUserBlocked(user_id, msg.sender_id())) {
          return true;
        }
        // A receiver whose connections already sent FIN would "consume" the
        // message without ever reading it; report not-delivered so the
        // caller queues it offline.
        auto healthy = HealthyLocalSessions(state, user_id);
        if (healthy.empty()) {
          return false;
        }
        // Ack-capable sessions hold the delivery until MESSAGE_ACK; only
        // legacy sessions keep the write-means-delivered self answer.
        if (!TrackAckIfCapable(acks, healthy, msg.message_id(), msg.delivery_id(),
                               user_id, chirp::chat::kDefaultOfflineSlot,
                               msg_bytes)) {
          delivery_tracker->Acknowledge(msg.message_id(), user_id);
        }
        for (const auto& recv_session : healthy) {
          chirp::chat::runtime::SendChatNotify(recv_session, msg);
        }
        Logger::Instance().Info("Message delivered locally to " + user_id + " (" +
                                std::to_string(healthy.size()) + " session(s))");
        return true;
      });

    // Nobody received it live (offline here, or on another instance with no
    // Redis pub/sub): enqueue an offline copy. AddOfflineMessage falls back
    // to an in-memory queue when Redis is unavailable, so single-node
    // deployments without Redis still refill on login. 入队臂:无任何在线端,
    // 目标端未知 → default 共享桶(任何端登录先到先得)。
    if (receivers <= 0) {
      store->AddOfflineMessage(req.receiver_id(), chirp::chat::kDefaultOfflineSlot,
                               msg_data.SerializeAsString());
      Logger::Instance().Info("Message stored offline for " + req.receiver_id());
    }

    // Friend-DM relay into the game plane (游戏在线状态): the recipient's
    // bindings are the in-game assertion, and the per-player switch (default
    // on) gates the relay together with the friend-facing status. Strictly
    // best-effort — the sender above already got OK, so a missing spoke is a
    // logged skip. Blocked pairs die inside the delivery lambda; re-check
    // here so a blocked send never reaches the game plane either. NPC
    // receivers returned long before this point.
    if (directory != nullptr && hub != nullptr &&
        !delivery_prefs->IsUserBlocked(req.receiver_id(), msg.sender_id())) {
      directory->RelayFriendMessage(
          msg.sender_id(), req.receiver_id(), msg.content(),
          /*client_msg_id=*/"",
          [hub](const std::string& game_id) { return hub->service_id_for_game(game_id); },
          [hub](const std::string& service_id,
                const chirp::gateway::PeerInjectMessageNotify& notify) {
            return hub->SendInject(service_id, notify);
          });
    }
  } else {
    // 群组会话（GroupManager 台账内的频道，桌面侧 GUILD 型 channel_id=群 id）
    // 按成员名单扇出：在线成员直达 CHAT_MESSAGE_NOTIFY，离线成员入离线队列
    // 补投（与 basic 的群发契约一致）。台账外的频道（world/team 等游戏平面
    // 频道没有群台账）保持既有 Redis 扇出路径，行为不变。
    if (group_handlers != nullptr &&
        group_handlers->IsMember(channel_id, msg.sender_id())) {
      for (const auto& member_id :
           group_handlers->BroadcastGroupMessage(channel_id, msg.sender_id(), msg)) {
        store->AddOfflineMessage(member_id, chirp::chat::kDefaultOfflineSlot,
                                 msg.SerializeAsString());
      }
    } else {
      router->BroadcastToGroup(channel_id, msg.SerializeAsString());
    }
    // Spoke mode: relay non-private channels up to the app-plane hub.
    // Best-effort by design — an unregistered link drops the uplink and
    // the game plane keeps running.
    if (spoke_link != nullptr && spoke_link->registered()) {
      chirp::gateway::ChannelMessageNotify notify;
      notify.set_game_id(spoke_game_id);
      notify.set_channel_id(req.channel_id());
      *notify.mutable_message() = msg;
      spoke_link->SendChannelMessage(notify);
    }
  }
}


EnhancedRecallRuntime MakeEnhancedRecallRuntime(
    const std::shared_ptr<DistributedChatState>& state,
    const std::shared_ptr<HybridMessageStore>& store,
    const chirp::chat::EditConfig& edit_config,
    chirp::chat::GroupManager* groups) {
  EnhancedRecallRuntime rt;
  rt.edits = std::make_unique<chirp::chat::MessageEditManager>(edit_config);

  chirp::chat::UserNotifier notify =
      [state](const std::string& user_id, chirp::gateway::MsgID msg_id,
              const google::protobuf::Message& body) -> bool {
    const auto sessions = chirp::network::GetUserSessions(state->registry, user_id);
    if (sessions.empty()) {
      return false;
    }
    const std::string payload = body.SerializeAsString();
    for (const auto& session : sessions) {
      chirp::chat::runtime::SendPacket(session, msg_id, 0, payload);
    }
    return true;
  };

  chirp::chat::ChannelMemberResolver members =
      [groups](chirp::chat::ChannelType channel_type, const std::string& channel_id,
               const std::string& exclude_user_id) -> std::vector<std::string> {
    if (channel_type != chirp::chat::PRIVATE) {
      // 群频道（桌面侧 GUILD 型 channel_id=群 id）：有群台账时按成员名单
      // 广播；台账外频道（游戏平面）返回空。
      std::vector<std::string> out;
      if (groups != nullptr) {
        for (const auto& member : groups->GetMembers(channel_id)) {
          if (member.user_id() != exclude_user_id) {
            out.push_back(member.user_id());
          }
        }
      }
      return out;
    }
    const size_t sep = channel_id.find('|');
    if (sep == std::string::npos || sep == 0 || sep + 1 >= channel_id.size()) {
      return {};
    }
    const std::string left = channel_id.substr(0, sep);
    const std::string right = channel_id.substr(sep + 1);
    const std::string& other = (left == exclude_user_id) ? right : left;
    if (other.empty() || other == exclude_user_id) {
      return {};
    }
    return {other};
  };

  chirp::chat::ChannelModeratorChecker moderator =
      [](chirp::chat::ChannelType, const std::string&, const std::string&) { return false; };

  chirp::chat::OfflineMessagePurger purger =
      [store](const std::string& message_id, const std::string& receiver_id) {
        store->PurgeOfflineByMessageId(receiver_id, message_id);
      };
  chirp::chat::RecallTombstoneMarker marker =
      [store](chirp::chat::ChannelType, const std::string& channel_id,
              const std::string& message_id) {
        store->MarkMessageRecalled(channel_id, message_id);
      };

  rt.handlers = std::make_unique<chirp::chat::MessageEditHandlers>(
      *rt.edits, members, moderator, notify, purger, marker);
  return rt;
}

/// @brief DELETE_MESSAGE 的 enhanced 分发体：以注册表里的认证身份跑
/// MessageEditHandlers（撤回回码语义与 basic 完全一致），并把响应帧回给请求者。
/// 解析失败由 DispatchDistributedPacket 按 INVALID_PARAM 拒绝，不进到这里。
void HandleDeleteMessage(const chirp::chat::DeleteMessageRequest& req,
                         const std::shared_ptr<chirp::network::Session>& session,
                         const std::shared_ptr<DistributedChatState>& state,
                         chirp::chat::MessageEditHandlers& edit_handlers, int64_t seq) {
  const chirp::chat::DeleteMessageResponse resp =
      edit_handlers.HandleDeleteMessage(req, state->GetUserId(session));
  chirp::chat::runtime::SendPacket(session, chirp::gateway::DELETE_MESSAGE_RESP, seq,
                                   resp.SerializeAsString());
}

/// @brief 群组/输入中/表情/未读/编辑客户端面 opcode 的 enhanced 分发体（TODO
/// 2026-10-02 更正注补齐项：这批此前只在 basic 分发表，enhanced 侧请求静默
/// 悬死）。DispatchDistributedPacket 是槽位制，这批不进槽位，与 MARK_READ/
/// WORD_FILTER_FETCH 同一拦截位就地解析+处理+回帧；逐 case 的回码契约与
/// basic 分发表一致（解析失败 INVALID_PARAM，typing notify 族无回帧）。
/// 认证身份取 registry 绑定（与槽位 handler 同源），组内成员/权限判定都在
/// 共享 handlers 单元里。返回 true 表示帧已消费。
bool DispatchClientFeaturePacket(
    const chirp::gateway::Packet& pkt,
    const std::shared_ptr<chirp::network::Session>& session,
    const std::string& authenticated_user, chirp::chat::GroupHandlers* group_handlers,
    chirp::chat::TypingHandlers* typing_handlers,
    chirp::chat::ReactionHandlers* reaction_handlers,
    chirp::chat::ReadReceiptHandlers* receipt_handlers,
    chirp::chat::MessageEditHandlers* edit_handlers,
    chirp::chat::ReportHandlers* report_handlers,
    chirp::chat::DeliveryPrefs* delivery_prefs) {
  switch (pkt.msg_id()) {
    case chirp::gateway::CREATE_GROUP_REQ: {
      chirp::chat::CreateGroupRequest req;
      chirp::chat::CreateGroupResponse resp;
      if (!req.ParseFromArray(pkt.body().data(), static_cast<int>(pkt.body().size()))) {
        resp.set_code(chirp::common::INVALID_PARAM);
      } else {
        resp = group_handlers->HandleCreateGroup(req, authenticated_user);
      }
      chirp::chat::runtime::SendPacket(session, chirp::gateway::CREATE_GROUP_RESP,
                                       pkt.sequence(), resp.SerializeAsString());
      break;
    }
    case chirp::gateway::JOIN_GROUP_REQ: {
      chirp::chat::JoinGroupRequest req;
      chirp::chat::JoinGroupResponse resp;
      if (!req.ParseFromArray(pkt.body().data(), static_cast<int>(pkt.body().size()))) {
        resp.set_code(chirp::common::INVALID_PARAM);
      } else {
        resp = group_handlers->HandleJoinGroup(req, authenticated_user);
      }
      chirp::chat::runtime::SendPacket(session, chirp::gateway::JOIN_GROUP_RESP,
                                       pkt.sequence(), resp.SerializeAsString());
      break;
    }
    case chirp::gateway::LEAVE_GROUP_REQ: {
      chirp::chat::LeaveGroupRequest req;
      chirp::chat::LeaveGroupResponse resp;
      if (!req.ParseFromArray(pkt.body().data(), static_cast<int>(pkt.body().size()))) {
        resp.set_code(chirp::common::INVALID_PARAM);
      } else {
        resp = group_handlers->HandleLeaveGroup(req, authenticated_user);
      }
      chirp::chat::runtime::SendPacket(session, chirp::gateway::LEAVE_GROUP_RESP,
                                       pkt.sequence(), resp.SerializeAsString());
      break;
    }
    case chirp::gateway::KICK_MEMBER_REQ: {
      chirp::chat::KickMemberRequest req;
      chirp::chat::KickMemberResponse resp;
      if (!req.ParseFromArray(pkt.body().data(), static_cast<int>(pkt.body().size()))) {
        resp.set_code(chirp::common::INVALID_PARAM);
      } else {
        resp = group_handlers->HandleKickMember(req, authenticated_user);
      }
      chirp::chat::runtime::SendPacket(session, chirp::gateway::KICK_MEMBER_RESP,
                                       pkt.sequence(), resp.SerializeAsString());
      break;
    }
    case chirp::gateway::GET_GROUP_INFO_REQ: {
      chirp::chat::GetGroupInfoRequest req;
      chirp::chat::GetGroupInfoResponse resp;
      if (!req.ParseFromArray(pkt.body().data(), static_cast<int>(pkt.body().size()))) {
        resp.set_code(chirp::common::INVALID_PARAM);
      } else {
        resp = group_handlers->HandleGetGroupInfo(req, authenticated_user);
      }
      chirp::chat::runtime::SendPacket(session, chirp::gateway::GET_GROUP_INFO_RESP,
                                       pkt.sequence(), resp.SerializeAsString());
      break;
    }
    case chirp::gateway::GET_GROUP_MEMBERS_REQ: {
      chirp::chat::GetGroupMembersRequest req;
      chirp::chat::GetGroupMembersResponse resp;
      if (!req.ParseFromArray(pkt.body().data(), static_cast<int>(pkt.body().size()))) {
        resp.set_code(chirp::common::INVALID_PARAM);
      } else {
        resp = group_handlers->HandleGetGroupMembers(req, authenticated_user);
      }
      chirp::chat::runtime::SendPacket(session, chirp::gateway::GET_GROUP_MEMBERS_RESP,
                                       pkt.sequence(), resp.SerializeAsString());
      break;
    }
    case chirp::gateway::GET_USER_GROUPS_REQ: {
      chirp::chat::GetUserGroupsRequest req;
      chirp::chat::GetUserGroupsResponse resp;
      if (!req.ParseFromArray(pkt.body().data(), static_cast<int>(pkt.body().size()))) {
        resp.set_code(chirp::common::INVALID_PARAM);
      } else {
        resp = group_handlers->HandleGetUserGroups(req, authenticated_user);
      }
      chirp::chat::runtime::SendPacket(session, chirp::gateway::GET_USER_GROUPS_RESP,
                                       pkt.sequence(), resp.SerializeAsString());
      break;
    }
    case chirp::gateway::INVITE_TO_GROUP_REQ: {
      chirp::chat::InviteToGroupRequest req;
      chirp::chat::InviteToGroupResponse resp;
      if (!req.ParseFromArray(pkt.body().data(), static_cast<int>(pkt.body().size()))) {
        resp.set_code(chirp::common::INVALID_PARAM);
      } else {
        resp = group_handlers->HandleInviteToGroup(req, authenticated_user);
      }
      chirp::chat::runtime::SendPacket(session, chirp::gateway::INVITE_TO_GROUP_RESP,
                                       pkt.sequence(), resp.SerializeAsString());
      break;
    }
    case chirp::gateway::SET_MEMBER_ALIAS_REQ: {
      chirp::chat::SetMemberAliasRequest req;
      chirp::chat::SetMemberAliasResponse resp;
      if (!req.ParseFromArray(pkt.body().data(), static_cast<int>(pkt.body().size()))) {
        resp.set_code(chirp::common::INVALID_PARAM);
      } else {
        resp = group_handlers->HandleSetMemberAlias(req, authenticated_user);
      }
      chirp::chat::runtime::SendPacket(session, chirp::gateway::SET_MEMBER_ALIAS_RESP,
                                       pkt.sequence(), resp.SerializeAsString());
      break;
    }
    case chirp::gateway::SET_GROUP_MUTE_REQ: {
      chirp::chat::SetGroupMuteRequest req;
      chirp::chat::SetGroupMuteResponse resp;
      if (!req.ParseFromArray(pkt.body().data(), static_cast<int>(pkt.body().size()))) {
        resp.set_code(chirp::common::INVALID_PARAM);
      } else {
        resp = group_handlers->HandleSetGroupMute(req, authenticated_user);
      }
      chirp::chat::runtime::SendPacket(session, chirp::gateway::SET_GROUP_MUTE_RESP,
                                       pkt.sequence(), resp.SerializeAsString());
      break;
    }
    case chirp::gateway::SET_DM_RESTRICTION_REQ: {
      // 防骚扰开关（game_chat_features P2，2255）：本人设置，回显生效后的
      // 状态；delivery_prefs 为空按未装配拒绝（同 report_handlers 先例）。
      chirp::chat::SetDmRestrictionRequest req;
      chirp::chat::SetDmRestrictionResponse resp;
      if (delivery_prefs == nullptr) {
        resp.set_code(chirp::common::AUTH_FAILED);
      } else if (!req.ParseFromArray(pkt.body().data(), static_cast<int>(pkt.body().size()))) {
        resp.set_code(chirp::common::INVALID_PARAM);
      } else if (authenticated_user.empty()) {
        resp.set_code(chirp::common::AUTH_FAILED);
      } else {
        delivery_prefs->SetDmRestricted(authenticated_user, req.enabled());
        resp.set_code(chirp::common::OK);
        resp.set_enabled(req.enabled());
      }
      chirp::chat::runtime::SendPacket(session, chirp::gateway::SET_DM_RESTRICTION_RESP,
                                       pkt.sequence(), resp.SerializeAsString());
      break;
    }
    case chirp::gateway::GET_DM_RESTRICTION_REQ: {
      chirp::chat::GetDmRestrictionResponse resp;
      if (delivery_prefs == nullptr || authenticated_user.empty()) {
        resp.set_code(chirp::common::AUTH_FAILED);
      } else {
        resp.set_code(chirp::common::OK);
        resp.set_enabled(delivery_prefs->IsDmRestricted(authenticated_user));
      }
      chirp::chat::runtime::SendPacket(session, chirp::gateway::GET_DM_RESTRICTION_RESP,
                                       pkt.sequence(), resp.SerializeAsString());
      break;
    }
    case chirp::gateway::REPORT_MESSAGE_REQ: {
      // 举报（game_chat_features P1）：存在性校验借撤回管道的编辑台账，与
      // basic 分发表同一套回码契约。report_handlers 为空时按未装配拒绝。
      chirp::chat::ReportMessageRequest req;
      chirp::chat::ReportMessageResponse resp;
      if (report_handlers == nullptr) {
        resp.set_code(chirp::common::AUTH_FAILED);
      } else if (!req.ParseFromArray(pkt.body().data(), static_cast<int>(pkt.body().size()))) {
        resp.set_code(chirp::common::INVALID_PARAM);
      } else {
        resp = report_handlers->HandleReportMessage(req, authenticated_user);
      }
      chirp::chat::runtime::SendPacket(session, chirp::gateway::REPORT_MESSAGE_RESP,
                                       pkt.sequence(), resp.SerializeAsString());
      break;
    }
    case chirp::gateway::GET_UNREAD_COUNT_REQ: {
      chirp::chat::GetUnreadCountRequest req;
      chirp::chat::GetUnreadCountResponse resp;
      if (!req.ParseFromArray(pkt.body().data(), static_cast<int>(pkt.body().size()))) {
        resp.set_code(chirp::common::INVALID_PARAM);
      } else {
        resp = receipt_handlers->HandleGetUnreadCount(req, authenticated_user);
      }
      chirp::chat::runtime::SendPacket(session, chirp::gateway::GET_UNREAD_COUNT_RESP,
                                       pkt.sequence(), resp.SerializeAsString());
      break;
    }
    case chirp::gateway::TYPING_INDICATOR_NOTIFY: {
      // notify 族：无回帧，解析失败仅告警（basic 同契约）。
      chirp::chat::TypingIndicator req;
      if (!req.ParseFromArray(pkt.body().data(), static_cast<int>(pkt.body().size()))) {
        Logger::Instance().Warn("failed to parse TypingIndicator body");
        break;
      }
      typing_handlers->HandleTypingIndicator(req, authenticated_user);
      break;
    }
    case chirp::gateway::GET_TYPING_USERS_REQ: {
      chirp::chat::GetTypingUsersRequest req;
      chirp::chat::GetTypingUsersResponse resp;
      if (!req.ParseFromArray(pkt.body().data(), static_cast<int>(pkt.body().size()))) {
        resp.set_code(chirp::common::INVALID_PARAM);
      } else {
        resp = typing_handlers->HandleGetTypingUsers(req, authenticated_user);
      }
      chirp::chat::runtime::SendPacket(session, chirp::gateway::GET_TYPING_USERS_RESP,
                                       pkt.sequence(), resp.SerializeAsString());
      break;
    }
    case chirp::gateway::ADD_REACTION_REQ: {
      chirp::chat::AddReactionRequest req;
      chirp::chat::AddReactionResponse resp;
      if (!req.ParseFromArray(pkt.body().data(), static_cast<int>(pkt.body().size()))) {
        resp.set_code(chirp::common::INVALID_PARAM);
      } else {
        resp = reaction_handlers->HandleAddReaction(req, authenticated_user);
      }
      chirp::chat::runtime::SendPacket(session, chirp::gateway::ADD_REACTION_RESP,
                                       pkt.sequence(), resp.SerializeAsString());
      break;
    }
    case chirp::gateway::REMOVE_REACTION_REQ: {
      chirp::chat::RemoveReactionRequest req;
      chirp::chat::RemoveReactionResponse resp;
      if (!req.ParseFromArray(pkt.body().data(), static_cast<int>(pkt.body().size()))) {
        resp.set_code(chirp::common::INVALID_PARAM);
      } else {
        resp = reaction_handlers->HandleRemoveReaction(req, authenticated_user);
      }
      chirp::chat::runtime::SendPacket(session, chirp::gateway::REMOVE_REACTION_RESP,
                                       pkt.sequence(), resp.SerializeAsString());
      break;
    }
    case chirp::gateway::GET_REACTIONS_REQ: {
      chirp::chat::GetReactionsRequest req;
      chirp::chat::GetReactionsResponse resp;
      if (!req.ParseFromArray(pkt.body().data(), static_cast<int>(pkt.body().size()))) {
        resp.set_code(chirp::common::INVALID_PARAM);
      } else {
        resp = reaction_handlers->HandleGetReactions(req, authenticated_user);
      }
      chirp::chat::runtime::SendPacket(session, chirp::gateway::GET_REACTIONS_RESP,
                                       pkt.sequence(), resp.SerializeAsString());
      break;
    }
    case chirp::gateway::EDIT_MESSAGE_REQ: {
      chirp::chat::EditMessageRequest req;
      chirp::chat::EditMessageResponse resp;
      if (!req.ParseFromArray(pkt.body().data(), static_cast<int>(pkt.body().size()))) {
        resp.set_code(chirp::common::INVALID_PARAM);
      } else {
        resp = edit_handlers->HandleEditMessage(req, authenticated_user);
      }
      chirp::chat::runtime::SendPacket(session, chirp::gateway::EDIT_MESSAGE_RESP,
                                       pkt.sequence(), resp.SerializeAsString());
      break;
    }
    default:
      return false;
  }
  return true;
}

/// @brief Internal-plane trust gate (parity with the basic build's main.cc):
/// an edge gateway dials in with SERVER_AUTH_REQ carrying the shared service
/// secret, and its per-client pipes then skip the per-IP login limiter (a
/// gateway fans many users out of one source address). Token verification on
/// the LOGIN_REQ that follows is untouched. With no secret configured the
/// frame is ignored - direct entry only, the historical enhanced behavior.
/// Returns true when the frame was consumed (response sent).
bool HandleServerAuth(const chirp::gateway::Packet& pkt,
                      const std::shared_ptr<chirp::network::Session>& session,
                      const std::string& secret,
                      std::unordered_set<const chirp::network::Session*>* trusted) {
  if (secret.empty()) {
    return false;
  }
  chirp::game_server_gateway::ServerAuthRequest auth_req;
  chirp::game_server_gateway::ServerAuthResponse auth_resp;
  auth_resp.set_server_time_ms(chirp::chat::runtime::NowMs());
  if (!auth_req.ParseFromArray(pkt.body().data(), static_cast<int>(pkt.body().size())) ||
      auth_req.secret() != secret) {
    auth_resp.set_code(chirp::common::AUTH_FAILED);
    // Reply-then-close: the gateway pipe must not linger after a rejected
    // secret (the basic build's SendPacketAndClose semantics).
    const auto framed = chirp::network::ProtobufFraming::Encode(
        [&] {
          chirp::gateway::Packet p;
          p.set_msg_id(chirp::gateway::SERVER_AUTH_RESP);
          p.set_sequence(pkt.sequence());
          p.set_body(auth_resp.SerializeAsString());
          return p;
        }());
    session->SendAndClose(std::string(reinterpret_cast<const char*>(framed.data()), framed.size()));
    return true;
  }
  if (trusted != nullptr) {
    trusted->insert(session.get());
  }
  auth_resp.set_code(chirp::common::OK);
  chirp::chat::runtime::SendPacket(session, chirp::gateway::SERVER_AUTH_RESP, pkt.sequence(),
                                   auth_resp.SerializeAsString());
  return true;
}

/// @brief Handle user login
void HandleLogin(const chirp::auth::LoginRequest& req,
                const std::shared_ptr<chirp::network::Session>& session,
                const std::shared_ptr<DistributedChatState>& state,
                const std::shared_ptr<HybridMessageStore>& store,
                const std::shared_ptr<chirp::network::MessageRouter>& router,
                const chirp::common::LoginTokenVerifier* token_verifier,
                chirp::chat::DeliveryAckManager* acks,
                const chirp::chat::DeliveryPrefs* delivery_prefs,
                int64_t seq) {
  std::string user_id;

  // With a shared secret configured, the token must be an HS256 JWT with
  // an exp claim and the user in sub (same contract as the basic build).
  if (token_verifier != nullptr && token_verifier->enabled()) {
    std::string verify_err;
    if (!token_verifier->Verify(req.token(), chirp::chat::runtime::NowMs(),
                                &user_id, &verify_err)) {
      Logger::Instance().Warn("chat login rejected: " + verify_err);
      chirp::auth::LoginResponse deny;
      deny.set_code(chirp::common::AUTH_FAILED);
      deny.set_server_time(chirp::chat::runtime::NowMs());
      chirp::chat::runtime::SendPacket(session, chirp::gateway::LOGIN_RESP, seq,
                                       deny.SerializeAsString());
      return;
    }
  } else {
    // Scaffolding login: treat token as user_id.
    user_id = req.token();
  }

  chirp::auth::LoginResponse resp;
  if (!user_id.empty()) {
    resp.set_code(chirp::common::OK);
    resp.set_user_id(user_id);
    resp.set_session_id(state->instance_id + "_" + std::to_string(chirp::chat::runtime::NowMs()));
    resp.set_kick_previous(true);
    resp.mutable_kick()->set_reason(chirp::network::LoginKickReason(req.platform()));

    // 多端在线（P0）：同 (user, platform) 重登录 displaces the previous
    // session, which gets a KICK_NOTIFY instead of silently rotting; another
    // platform of the same user keeps its session. The initial online-device
    // listing (self excluded) rides in the response, and the user's other
    // devices learn this device came online.
    auto old = state->AddSession(user_id, req.device_id(), resp.session_id(), session,
                                 req.platform(), req.level(), req.supports_gzip());
    if (old && old.get() != session.get()) {
      KickSession(old, chirp::network::LoginKickReason(req.platform()));
    }
    // Slot accounting: a null displaced session means BindAuthenticatedSession
    // added a new (user, platform) slot; a non-null one means the slot was
    // replaced (count unchanged - the old session's later release is a no-op
    // for the gauge). Same-connection relogin keeps the slot, so the single
    // release at disconnect balances the one inc that admitted it.
    if (!old) {
      CHIRP_GAUGE_INC("chirp_chat_sessions");
    }
    CHIRP_COUNTER("chirp_chat_logins_total", 1);
    chirp::network::FillOnlineDevices(state->registry, user_id, &resp, session.get());
    chirp::network::BroadcastDevicePresence(state->registry, user_id, req.platform(),
                                            chirp::network::NormalizeDeviceId(req.device_id()),
                                            /*online=*/true, session.get());

    if (acks && req.supports_message_ack()) {
      acks->MarkCapable(session);
    }

    // Cross-instance deliveries fan to every live local session of the user
    // (one per device) through the registry - the callback outlives any
    // single connection, so it must not capture one.
    router->SubscribeUserChat(user_id, [state, acks, user_id, delivery_prefs](
                                           const std::string& msg_data) {
      chirp::chat::ChatMessage msg;
      if (!msg.ParseFromArray(msg_data.data(), static_cast<int>(msg_data.size()))) {
        return;
      }
      // 黑名单：本实例用户拉黑了发送方，跨实例投递到此为止（静默丢弃，
      // 不暴露拉黑态）。
      if (delivery_prefs->IsUserBlocked(user_id, msg.sender_id())) {
        return;
      }
      auto healthy = HealthyLocalSessions(state, user_id);
      if (healthy.empty()) {
        return;
      }
      // Cross-instance deliveries are tracked like local ones - this
      // instance owns the receiving sessions, so the ack comes back here.
      // 在线首投 → 回队回 default 共享桶。
      TrackAckIfCapable(acks, healthy, msg.message_id(), msg.delivery_id(),
                        user_id, chirp::chat::kDefaultOfflineSlot, msg_data);
      for (const auto& recv : healthy) {
        chirp::chat::runtime::SendChatNotify(recv, msg);
      }
    });

    Logger::Instance().Info("User logged in: " + user_id);
  } else {
    resp.set_code(chirp::common::INVALID_PARAM);
  }
  resp.set_server_time(chirp::chat::runtime::NowMs());

  chirp::chat::runtime::SendPacket(session, chirp::gateway::LOGIN_RESP, seq, resp.SerializeAsString());

  // Deliver offline messages only after the LOGIN_RESP (same order as the
  // basic build): a refill notify sent first would be swallowed by clients
  // that read exactly one frame as "the login response".
  if (!user_id.empty()) {
    // 补投臂(per-device 拆分):弹本端桶 ∪ default 共享桶;Track 记认领端
    // slot——超时回队回本端桶,重投不被其他端的登录截走。
    const std::string refill_slot = chirp::network::NormalizePlatformId(req.platform());
    auto offline_msgs = store->PopOfflineMessages(user_id, refill_slot);
    Logger::Instance().Info("Delivering " + std::to_string(offline_msgs.size()) +
                           " offline messages to " + user_id);
    for (const auto& msg_data : offline_msgs) {
      chirp::chat::ChatMessage msg;
      msg.set_message_id(msg_data.message_id);
      msg.set_sender_id(msg_data.sender_id);
      msg.set_receiver_id(msg_data.receiver_id);
      msg.set_channel_id(msg_data.channel_id);
      msg.set_channel_type(static_cast<chirp::chat::ChannelType>(msg_data.channel_type));
      msg.set_msg_type(static_cast<chirp::chat::MsgType>(msg_data.msg_type));
      msg.set_content(msg_data.content);
      msg.set_timestamp(msg_data.timestamp);
      msg.set_sender_kind(static_cast<chirp::chat::SenderKind>(msg_data.sender_kind));
      msg.set_metadata(msg_data.metadata);
      // Refills are tracked like live deliveries: an unacked refill returns
      // to the offline queue instead of dying with the connection.
      if (acks && acks->IsCapable(session.get())) {
        acks->Track(msg.message_id(), msg.delivery_id(), user_id, refill_slot,
                    msg.SerializeAsString());
      }
      chirp::chat::runtime::SendChatNotify(session, msg);
    }
  }
}

/// @brief Handle get history with pagination. Audit 2026-11-11: the request
/// now runs the same validator as the basic build (login identity,
/// private-channel membership) and the page size is clamped to a sane window,
/// so a hostile limit no longer fans out into an unbounded page.
void HandleGetHistory(const chirp::chat::GetHistoryRequest& req,
                    const std::shared_ptr<chirp::network::Session>& session,
                    const std::shared_ptr<PaginatedHistoryRetriever>& retriever,
                    bool supports_gzip,
                    int64_t seq,
                    const std::string& authenticated_user_id) {
  chirp::chat::GetHistoryResponse resp;
  const chirp::common::ErrorCode validation =
      chirp::chat::ValidateGetHistoryRequest(req, authenticated_user_id);
  if (validation != chirp::common::OK) {
    resp.set_code(validation);
    resp.set_has_more(false);
    chirp::chat::runtime::SendPacket(session, chirp::gateway::GET_HISTORY_RESP, seq,
                                     resp.SerializeAsString());
    return;
  }

  // Same page window as the basic default: non-positive falls back to 50,
  // oversized requests clamp instead of reaching the store raw.
  int32_t limit = req.limit();
  if (limit <= 0) {
    limit = 50;
  }
  constexpr int32_t kMaxHistoryPageSize = 200;
  limit = std::min(limit, kMaxHistoryPageSize);

  std::string channel_id = req.channel_id();

  auto page = retriever->GetPageBefore(channel_id, req.channel_type(),
                                      req.before_timestamp(), limit);

  resp.set_code(chirp::common::OK);
  resp.set_has_more(page.has_more);

  for (const auto& msg_data : page.messages) {
    chirp::chat::ChatMessage* msg = resp.add_messages();
    msg->set_message_id(msg_data.message_id);
    msg->set_sender_id(msg_data.sender_id);
    msg->set_receiver_id(msg_data.receiver_id);
    msg->set_channel_id(msg_data.channel_id);
    msg->set_channel_type(static_cast<chirp::chat::ChannelType>(msg_data.channel_type));
    msg->set_msg_type(static_cast<chirp::chat::MsgType>(msg_data.msg_type));
    msg->set_content(msg_data.content);
    msg->set_timestamp(msg_data.timestamp);
    msg->set_sender_kind(static_cast<chirp::chat::SenderKind>(msg_data.sender_kind));
    msg->set_reply_to_message_id(msg_data.reply_to_message_id);
    msg->set_is_recalled(msg_data.is_recalled);
    msg->set_metadata(msg_data.metadata);
  }

  // 消息压缩（game_chat_features P2）：登录声明 supports_gzip 的会话收
  // gzip 形态，其余保持明文 repeated 字段不变。
  chirp::chat::CompressHistoryResponse(&resp, supports_gzip);
  chirp::chat::runtime::SendPacket(session, chirp::gateway::GET_HISTORY_RESP, seq, resp.SerializeAsString());
}

/// @brief Handle get history V2 with cursor pagination
void HandleGetHistoryV2(const std::string& request_body,
                       const std::shared_ptr<chirp::network::Session>& session,
                       int64_t seq) {
  (void)request_body;

  chirp::chat::GetHistoryResponse resp;
  resp.set_code(chirp::common::INVALID_PARAM);
  resp.set_has_more(false);
  chirp::chat::runtime::SendPacket(session, chirp::gateway::GET_HISTORY_V2_RESP, seq, resp.SerializeAsString());
}

}  // namespace chirp::chat::enhanced
