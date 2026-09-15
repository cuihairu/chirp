#include "npc_responder.h"

#include "logger.h"

namespace chirp::npc {

namespace {

constexpr char kEventType[] = "npc.player_message";
constexpr char kNpcSenderPrefix[] = "npc:";

}  // namespace

NpcResponder::NpcResponder(NpcEngine& engine, InjectSender send_inject,
                           AckSender send_ack, std::size_t dedupe_capacity)
    : engine_(engine),
      send_inject_(std::move(send_inject)),
      send_ack_(std::move(send_ack)),
      dedupe_capacity_(dedupe_capacity) {}

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

  // Redelivery of an event whose reply was already accepted: stop the hub's
  // retry loop without replying again.
  if (AlreadyAnswered(event.event_id())) {
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

  chirp::common::Logger::Instance().Info(
      "utterance event=" + event.event_id() + " npc=" + utterance.npc_id() +
      " from=" + utterance.sender_id() + " -> replying as " + reply.sender_id());

  send_inject_(reply, [this, event_id = event.event_id()](
                          chirp::common::ErrorCode code) {
    if (code == chirp::common::OK) {
      chirp::common::Logger::Instance().Info("reply inject accepted for " + event_id);
      RememberAnswered(event_id);
      Ack(event_id);
    } else {
      // Anything else: stay unacked (and un-remembered), so the hub redelivers
      // the event and the reply is attempted again (at-least-once).
      chirp::common::Logger::Instance().Warn(
          "reply inject failed for " + event_id + " (code " +
          std::to_string(static_cast<int>(code)) + "); waiting for redelivery");
    }
  });
}

bool NpcResponder::AlreadyAnswered(const std::string& event_id) const {
  return answered_.count(event_id) > 0;
}

void NpcResponder::RememberAnswered(const std::string& event_id) {
  if (!answered_.insert(event_id).second) {
    return;  // already tracked; keep its original eviction position
  }
  answered_order_.push_back(event_id);
  while (answered_order_.size() > dedupe_capacity_) {
    answered_.erase(answered_order_.front());
    answered_order_.pop_front();
  }
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
