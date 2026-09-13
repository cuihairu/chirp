#include "npc_responder.h"

#include "logger.h"

namespace chirp::npc {

namespace {

constexpr char kEventType[] = "npc.player_message";
constexpr char kNpcSenderPrefix[] = "npc:";

}  // namespace

NpcResponder::NpcResponder(NpcEngine& engine, InjectSender send_inject,
                           AckSender send_ack)
    : engine_(engine),
      send_inject_(std::move(send_inject)),
      send_ack_(std::move(send_ack)) {}

void NpcResponder::OnEvent(const chirp::server_gateway::EventDeliverNotify& event) {
  // Not our event type: ack immediately, or it would redeliver forever.
  if (event.event_type() != kEventType) {
    Ack(event.event_id());
    return;
  }

  chirp::chat::NpcPlayerUtterance utterance;
  if (!utterance.ParseFromString(event.payload())) {
    Ack(event.event_id());
    return;
  }
  // An utterance without sender or NPC identity cannot be answered.
  if (utterance.message_id().empty() || utterance.sender_id().empty() ||
      utterance.npc_id().empty()) {
    Ack(event.event_id());
    return;
  }

  chirp::server_gateway::MessageInjectRequest reply;
  reply.set_inject_id(event.event_id());  // caller idempotency key = event id
  reply.set_sender_kind(chirp::server_gateway::SENDER_NPC);
  reply.set_sender_id(kNpcSenderPrefix + utterance.npc_id());
  reply.set_channel_type(chirp::chat::PRIVATE);
  reply.set_receiver_id(utterance.sender_id());
  reply.set_content(engine_.Reply(utterance.npc_id(), utterance.content()));

  send_inject_(reply, [this, event_id = event.event_id()](
                          chirp::common::ErrorCode code) {
    if (code == chirp::common::OK) {
      Ack(event_id);
    }
    // Anything else: stay unacked; the hub redelivers the event and the
    // reply is attempted again (at-least-once).
  });
}

void NpcResponder::Ack(const std::string& event_id) {
  chirp::server_gateway::EventAckRequest req;
  req.add_event_ids(event_id);
  send_ack_(req, [event_id](chirp::common::ErrorCode code) {
    if (code != chirp::common::OK) {
      chirp::common::Logger::Instance().Warn("event ack failed for " + event_id +
                                             "; the hub will redeliver");
    }
  });
}

}  // namespace chirp::npc
