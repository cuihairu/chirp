#include "inject_consumer.h"

#include "runtime_utils.h"

namespace chirp::chat {

InjectConsumer::InjectConsumer(InjectHooks hooks, std::shared_ptr<InjectIdempotency> idempotency)
    : hooks_(std::move(hooks)), idempotency_(std::move(idempotency)) {}

InjectOutcome InjectConsumer::HandleInject(
    const chirp::game_server_gateway::InjectMessageNotify& notify) {
  const chirp::game_server_gateway::MessageInjectRequest& req = notify.message();

  InjectOutcome outcome;
  const bool valid_channel_type =
      req.channel_type() >= static_cast<int32_t>(chirp::chat::PRIVATE) &&
      req.channel_type() <= static_cast<int32_t>(chirp::chat::WORLD);
  const bool has_target =
      req.channel_type() == static_cast<int32_t>(chirp::chat::PRIVATE)
          ? !req.receiver_id().empty()
          : !req.channel_id().empty();
  if (req.content().empty() || req.sender_id().empty() ||
      req.sender_kind() == chirp::game_server_gateway::SENDER_UNKNOWN ||
      !valid_channel_type || !has_target) {
    outcome.code = chirp::common::INVALID_PARAM;
    return outcome;
  }

  // inject_id 是契约上的幂等键（game_server_gateway.proto）：hub 在收到 ack
  // 之前重投同一个 INJECT_MESSAGE_NOTIFY 时，第二次进来直接回吐首次落库的
  // message_id，不再重复落库/投递。空 inject_id 视为不带幂等语义（旧游戏侧
  // 调用零感知），不参与去重。
  const std::string& inject_id = req.inject_id();
  if (idempotency_ && !inject_id.empty()) {
    const std::optional<std::string> replay = idempotency_->Lookup(inject_id);
    if (replay) {
      outcome.message_id = *replay;
      return outcome;
    }
  }

  chirp::chat::ChatMessage msg;
  msg.set_message_id(chirp::chat::runtime::GenerateMessageId());
  msg.set_sender_id(req.sender_id());
  msg.set_receiver_id(req.receiver_id());
  msg.set_channel_type(static_cast<chirp::chat::ChannelType>(req.channel_type()));
  msg.set_content(req.content());
  msg.set_timestamp(chirp::chat::runtime::NowMs());
  if (req.channel_type() == static_cast<int32_t>(chirp::chat::PRIVATE)) {
    msg.set_channel_id(hooks_.private_channel_id(req.sender_id(), req.receiver_id()));
  } else {
    msg.set_channel_id(req.channel_id());
  }
  // SenderKind 网关→chat 按契约直传：两侧枚举数值对齐（gateway UNKNOWN=0 /
  // SYSTEM=1 / NPC=2 / SERVICE=3 ↔ chat 同值），UNKNOWN 已在上方拒绝。值域
  // 契约见 chat.proto / game_server_gateway.proto 两侧 SenderKind 注释。
  msg.set_sender_kind(static_cast<chirp::chat::SenderKind>(req.sender_kind()));
  // 展示面元数据同款直传（int32 值域与 chat.Priority 对齐；channel_type 同
  // 口径）：priority/ttl_seconds 是走马灯（URGENT+ttl>0）与系统弹窗（HIGH）
  // 的判定二要素，缺省 0 = 普通消息，旧游戏侧注入零感知。枚举外值客户端
  // 回落普通气泡，不做服务端拦截（与「不越权校验」同口径）。
  msg.set_priority(static_cast<chirp::chat::Priority>(req.priority()));
  msg.set_ttl_seconds(req.ttl_seconds());

  hooks_.store_message(msg);

  if (req.channel_type() == static_cast<int32_t>(chirp::chat::PRIVATE)) {
    if (!hooks_.deliver_private(req.receiver_id(), msg)) {
      hooks_.queue_offline(req.receiver_id(), msg);
    }
  } else {
    for (const auto& member_id : hooks_.broadcast_channel(msg.channel_id(), msg)) {
      hooks_.queue_offline(member_id, msg);
    }
  }

  outcome.message_id = msg.message_id();
  if (idempotency_ && !inject_id.empty()) {
    idempotency_->Remember(inject_id, outcome.message_id);
  }
  return outcome;
}

}  // namespace chirp::chat
