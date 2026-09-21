#include "event_queue.h"

namespace chirp::game_server_gateway {

EventQueue::EventQueue(size_t max_pending_per_service) : max_pending_(max_pending_per_service) {}

bool EventQueue::Enqueue(const std::string& service_id, PendingEvent event) {
  std::lock_guard<std::mutex> lock(mu_);
  auto& q = queues_[service_id];
  if (q.events.size() >= max_pending_) {
    return false;
  }
  q.events.push_back(std::move(event));
  return true;
}

std::vector<PendingEvent> EventQueue::ClaimDeliverable(const std::string& service_id) {
  std::lock_guard<std::mutex> lock(mu_);
  auto it = queues_.find(service_id);
  std::vector<PendingEvent> batch;
  if (it == queues_.end()) {
    return batch;
  }
  for (auto& event : it->second.events) {
    if (it->second.in_flight.count(event.event_id) > 0) {
      continue;
    }
    it->second.in_flight.insert(event.event_id);
    ++event.attempt;
    batch.push_back(event);
  }
  return batch;
}

void EventQueue::Ack(const std::string& service_id, const std::vector<std::string>& event_ids) {
  std::lock_guard<std::mutex> lock(mu_);
  auto it = queues_.find(service_id);
  if (it == queues_.end()) {
    return;
  }
  std::unordered_set<std::string> acked(event_ids.begin(), event_ids.end());
  auto& q = it->second;
  for (auto vit = q.events.begin(); vit != q.events.end();) {
    if (acked.count(vit->event_id) > 0) {
      q.in_flight.erase(vit->event_id);
      vit = q.events.erase(vit);
    } else {
      ++vit;
    }
  }
}

void EventQueue::ResetInFlight(const std::string& service_id) {
  std::lock_guard<std::mutex> lock(mu_);
  auto it = queues_.find(service_id);
  if (it != queues_.end()) {
    it->second.in_flight.clear();
  }
}

size_t EventQueue::UnackedCount(const std::string& service_id) const {
  std::lock_guard<std::mutex> lock(mu_);
  auto it = queues_.find(service_id);
  return it == queues_.end() ? 0 : it->second.events.size();
}

}  // namespace chirp::game_server_gateway
