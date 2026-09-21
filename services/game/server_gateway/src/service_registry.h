#pragma once

#include <cstddef>
#include <memory>
#include <mutex>
#include <string>
#include <unordered_map>

#include <google/protobuf/message.h>

#include "proto/gateway.pb.h"

namespace chirp::game_server_gateway {

// A connected, authenticated peer. Implemented by the connection layer;
// tests use recording fakes.
class PeerSender {
 public:
  virtual ~PeerSender() = default;

  // Returns false when the underlying connection refused the write.
  virtual bool Send(chirp::gateway::MsgID msg_id, const google::protobuf::Message& body) = 0;
};

// Thread-safe service_id -> live connection registry. A service is connected
// through exactly one connection at a time: a newer login replaces the older
// one, and the displaced peer is handed back so the caller can close it.
class ServiceRegistry {
 public:
  // Registers `peer` for `service_id` and returns the previously registered
  // peer (nullptr when the service was offline).
  std::shared_ptr<PeerSender> Register(const std::string& service_id,
                                       std::shared_ptr<PeerSender> peer);

  // Removes the mapping only when it still points at `peer`. Returns false
  // when a newer peer has already replaced this one (e.g. the displaced
  // connection closing late); callers must not treat that as an outage.
  bool Unregister(const std::string& service_id, const PeerSender* peer);

  std::shared_ptr<PeerSender> Get(const std::string& service_id) const;
  bool IsOnline(const std::string& service_id) const;
  size_t Size() const;

 private:
  mutable std::mutex mu_;
  std::unordered_map<std::string, std::shared_ptr<PeerSender>> peers_;
};

}  // namespace chirp::game_server_gateway
