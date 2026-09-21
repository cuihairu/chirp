#ifndef CHIRP_NPC_DIALOG_NPC_RESPONDER_H_
#define CHIRP_NPC_DIALOG_NPC_RESPONDER_H_

#include <cstddef>
#include <deque>
#include <functional>
#include <string>
#include <unordered_set>

#include "npc_engine.h"
#include "proto/chat.pb.h"
#include "proto/common.pb.h"
#include "proto/game_server_gateway.pb.h"

namespace chirp::npc {

// Consumes npc.player_message events delivered by the hub, answers with an
// NPC injection over the same connection, and acknowledges.
//
// Ack policy: a malformed or non-NPC event can never become valid by
// retrying, so it is acknowledged immediately (poison-pill: ack it or the
// hub would redeliver forever). A valid event is acknowledged only after its
// reply injection was accepted by the hub (OK); any other outcome stays
// unacked, so the hub redelivers - at-least-once.
//
// Redelivery dedupe: once an event's reply was accepted, its event id is
// remembered in a bounded most-recent window. A redelivery of an answered
// event is acknowledged without replying again (the ack alone stops the
// hub's retry loop). Events whose injection did not succeed are NOT
// remembered, so a redelivery retries the reply. All state lives on the
// responder's single dispatch thread; no locking.
class NpcResponder {
 public:
  static constexpr std::size_t kDefaultDedupeCapacity = 1024;
  using InjectSender = std::function<void(
      const chirp::game_server_gateway::MessageInjectRequest& req,
      std::function<void(chirp::common::ErrorCode code)> cb)>;
  using AckSender = std::function<void(
      const chirp::game_server_gateway::EventAckRequest& req,
      std::function<void(chirp::common::ErrorCode code)> cb)>;

  NpcResponder(NpcEngine& engine, InjectSender send_inject, AckSender send_ack,
               std::size_t dedupe_capacity = kDefaultDedupeCapacity);

  void OnEvent(const chirp::game_server_gateway::EventDeliverNotify& event);

 private:
  // Acknowledges one event; a failed ack is only logged - the hub's
  // redelivery gives the next attempt another chance.
  void Ack(const std::string& event_id);

  bool AlreadyAnswered(const std::string& event_id) const;
  void RememberAnswered(const std::string& event_id);

  NpcEngine& engine_;
  InjectSender send_inject_;
  AckSender send_ack_;
  std::size_t dedupe_capacity_;
  std::deque<std::string> answered_order_;
  std::unordered_set<std::string> answered_;
};

}  // namespace chirp::npc

#endif  // CHIRP_NPC_DIALOG_NPC_RESPONDER_H_
