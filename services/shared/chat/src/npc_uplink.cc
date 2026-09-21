#include "npc_uplink.h"

namespace chirp::chat::npc {

namespace {
constexpr char kEventType[] = "npc.player_message";
}

bool IsNpcReceiver(const std::string& prefix, const std::string& receiver_id) {
  return !prefix.empty() && receiver_id.rfind(prefix, 0) == 0;
}

std::string NpcIdFromReceiver(const std::string& prefix,
                              const std::string& receiver_id) {
  if (!IsNpcReceiver(prefix, receiver_id)) {
    return "";
  }
  return receiver_id.substr(prefix.size());
}

chirp::game_server_gateway::EventPublishRequest MakeUtteranceEvent(
    const chirp::chat::ChatMessage& msg, const std::string& prefix,
    const std::string& target_service_id) {
  chirp::chat::NpcPlayerUtterance utterance;
  utterance.set_message_id(msg.message_id());
  utterance.set_sender_id(msg.sender_id());
  utterance.set_npc_id(NpcIdFromReceiver(prefix, msg.receiver_id()));
  utterance.set_content(msg.content());
  utterance.set_timestamp(msg.timestamp());

  chirp::game_server_gateway::EventPublishRequest event;
  event.set_event_id(msg.message_id());
  event.set_target_service_id(target_service_id);
  event.set_event_type(kEventType);
  event.set_payload(utterance.SerializeAsString());
  return event;
}

}  // namespace chirp::chat::npc
