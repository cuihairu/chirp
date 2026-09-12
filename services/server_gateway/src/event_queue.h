#pragma once

#include <cstddef>
#include <cstdint>
#include <deque>
#include <mutex>
#include <string>
#include <unordered_map>
#include <unordered_set>
#include <vector>

namespace chirp::server_gateway {

// One event awaiting acknowledgement from its target service.
struct PendingEvent {
  std::string event_id;
  std::string event_type;
  std::string payload;  // opaque; EventPublishRequest.payload passthrough
  int64_t published_at_ms = 0;
  int attempt = 0;
};

// Per-service FIFO of unacknowledged events. Delivery is at-least-once:
// ClaimDeliverable marks events in flight, and ResetInFlight (invoked when
// the peer disconnects) makes unacknowledged in-flight events deliverable
// again. Queues are bounded; a full queue rejects new publishes instead of
// silently dropping older events.
class EventQueue {
 public:
  explicit EventQueue(size_t max_pending_per_service);

  // Returns false when the service already holds the maximum number of
  // pending events.
  bool Enqueue(const std::string& service_id, PendingEvent event);

  // Returns the ordered events that are not currently in flight and marks
  // them in flight with attempt incremented.
  std::vector<PendingEvent> ClaimDeliverable(const std::string& service_id);

  // Removes acknowledged events; unknown ids are ignored (acks are
  // idempotent).
  void Ack(const std::string& service_id, const std::vector<std::string>& event_ids);

  // Clears the in-flight markers of the service so every unacknowledged
  // event can be redelivered.
  void ResetInFlight(const std::string& service_id);

  size_t UnackedCount(const std::string& service_id) const;

 private:
  struct ServiceQueue {
    std::deque<PendingEvent> events;
    std::unordered_set<std::string> in_flight;
  };

  size_t max_pending_;
  mutable std::mutex mu_;
  std::unordered_map<std::string, ServiceQueue> queues_;
};

}  // namespace chirp::server_gateway
