#ifndef CHIRP_CHAT_NPC_UPLINK_H_
#define CHIRP_CHAT_NPC_UPLINK_H_

#include <string>

#include "proto/chat.pb.h"
#include "proto/server_gateway.pb.h"

namespace chirp::chat::npc {

// True when the receiver id addresses an NPC under the configured prefix
// ("npc:blacksmith_01" with prefix "npc:"). An empty prefix never matches:
// there is no way to address an NPC without one, so it means the feature
// is effectively off.
bool IsNpcReceiver(const std::string& prefix, const std::string& receiver_id);

// Receiver id with the prefix stripped ("npc:blacksmith_01" ->
// "blacksmith_01"). Only meaningful when IsNpcReceiver returned true;
// otherwise returns the empty string.
std::string NpcIdFromReceiver(const std::string& prefix,
                              const std::string& receiver_id);

// Wraps a player -> NPC private message as an event for the NPC dialog
// service (event_type "npc.player_message"). The event id carries the chat
// message id so the service can correlate its replies and dedupe
// redeliveries; the payload is a serialized NpcPlayerUtterance.
chirp::server_gateway::EventPublishRequest MakeUtteranceEvent(
    const chirp::chat::ChatMessage& msg, const std::string& prefix,
    const std::string& target_service_id);

}  // namespace chirp::chat::npc

#endif  // CHIRP_CHAT_NPC_UPLINK_H_
