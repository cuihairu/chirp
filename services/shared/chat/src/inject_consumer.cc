#include "inject_consumer.h"

#include "runtime_utils.h"

namespace chirp::chat {

InjectConsumer::InjectConsumer(InjectHooks hooks) : hooks_(std::move(hooks)) {}

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
  return outcome;
}

}  // namespace chirp::chat
