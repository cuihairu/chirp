#ifndef CHIRP_NPC_DIALOG_NPC_RESPONDER_H_
#define CHIRP_NPC_DIALOG_NPC_RESPONDER_H_

#include <functional>
#include <string>

#include "npc_engine.h"
#include "proto/chat.pb.h"
#include "proto/common.pb.h"
#include "proto/server_gateway.pb.h"

namespace chirp::npc {

// Consumes npc.player_message events delivered by the hub, answers with an
// NPC injection over the same connection, and acknowledges.
//
// Ack policy: a malformed or non-NPC event can never become valid by
// retrying, so it is acknowledged immediately (poison-pill: ack it or the
// hub would redeliver forever). A valid event is acknowledged only after its
// reply injection was accepted by the hub (OK); any other outcome stays
// unacked, so the hub redelivers - at-least-once. A redelivery may produce a
// duplicate reply, which the MVP accepts.
class NpcResponder {
 public:
  using InjectSender = std::function<void(
      const chirp::server_gateway::MessageInjectRequest& req,
      std::function<void(chirp::common::ErrorCode code)> cb)>;
  using AckSender = std::function<void(
      const chirp::server_gateway::EventAckRequest& req,
      std::function<void(chirp::common::ErrorCode code)> cb)>;

  NpcResponder(NpcEngine& engine, InjectSender send_inject, AckSender send_ack);

  void OnEvent(const chirp::server_gateway::EventDeliverNotify& event);

 private:
  // Acknowledges one event; a failed ack is only logged - the hub's
  // redelivery gives the next attempt another chance.
  void Ack(const std::string& event_id);

  NpcEngine& engine_;
  InjectSender send_inject_;
  AckSender send_ack_;
};

}  // namespace chirp::npc

#endif  // CHIRP_NPC_DIALOG_NPC_RESPONDER_H_
