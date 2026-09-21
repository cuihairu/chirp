#include "service_registry.h"

namespace chirp::game_server_gateway {

std::shared_ptr<PeerSender> ServiceRegistry::Register(const std::string& service_id,
                                                      std::shared_ptr<PeerSender> peer) {
  std::lock_guard<std::mutex> lock(mu_);
  std::shared_ptr<PeerSender> previous;
  auto it = peers_.find(service_id);
  if (it != peers_.end()) {
    previous = std::move(it->second);
  }
  peers_[service_id] = std::move(peer);
  return previous;
}

bool ServiceRegistry::Unregister(const std::string& service_id, const PeerSender* peer) {
  std::lock_guard<std::mutex> lock(mu_);
  auto it = peers_.find(service_id);
  if (it == peers_.end() || it->second.get() != peer) {
    return false;
  }
  peers_.erase(it);
  return true;
}

std::shared_ptr<PeerSender> ServiceRegistry::Get(const std::string& service_id) const {
  std::lock_guard<std::mutex> lock(mu_);
  auto it = peers_.find(service_id);
  return it == peers_.end() ? nullptr : it->second;
}

bool ServiceRegistry::IsOnline(const std::string& service_id) const {
  return Get(service_id) != nullptr;
}

size_t ServiceRegistry::Size() const {
  std::lock_guard<std::mutex> lock(mu_);
  return peers_.size();
}

}  // namespace chirp::game_server_gateway
