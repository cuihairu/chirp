#pragma once

#include <atomic>
#include <cstdint>
#include <functional>
#include <memory>
#include <mutex>
#include <string>
#include <unordered_map>
#include <utility>

#include <asio.hpp>

#include "network/session.h"

namespace chirp::chat {

/// @brief Tracks live private-message deliveries until the receiving client
/// acknowledges them (MESSAGE_ACK). A delivery that is not acknowledged within
/// the timeout is handed back to the owner via on_requeue so it lands in the
/// offline queue and is refilled on the next login - the recovery path for
/// messages pushed into a zombie connection.
///
/// Only sessions that declared the capability at login (supports_message_ack)
/// are tracked; legacy clients keep the send-and-forget behavior because they
/// would never acknowledge anything.
///
/// Store interaction goes through callbacks so the manager stays independent
/// of any MessageStore implementation. All callbacks run on the io_context
/// thread, like every other chat dispatch path.
class DeliveryAckManager {
public:
  struct Config {
    // 0 disables the feature entirely: Track becomes a no-op and no session
    // is ever marked capable (kill switch for incidents).
    int64_t timeout_ms = 10000;
    int64_t scan_interval_ms = 500;
    // How long a requeued message stays remembered for late-ack cleanup
    // (removing the offline copy the client actually acknowledged late).
    int64_t requeued_retention_ms = 600000;
  };

  // (receiver_id, payload bytes): payload is exactly the byte string that was
  // tracked, so offline-queue removal can match it byte for byte.
  using PayloadCallback = std::function<void(const std::string& receiver_id,
                                             const std::string& payload)>;

  DeliveryAckManager(asio::io_context& io, Config config,
                     PayloadCallback on_requeue, PayloadCallback on_late_ack);
  ~DeliveryAckManager();

  DeliveryAckManager(const DeliveryAckManager&) = delete;
  DeliveryAckManager& operator=(const DeliveryAckManager&) = delete;

  void Start();
  void Stop();

  // Capability bookkeeping -------------------------------------------------
  // Sessions are remembered weakly: an expired entry proves the session is
  // gone even if its pointer was reused by a new connection.
  void MarkCapable(const std::shared_ptr<network::Session>& session);
  bool IsCapable(const network::Session* session);
  void ForgetSession(const network::Session* session);

  // Pending bookkeeping ----------------------------------------------------
  // Idempotent: tracking the same message_id again just refreshes it.
  void Track(const std::string& message_id,
             const std::string& receiver_id,
             const std::string& payload);
  // Returns true when the ack meant something: it cleared a pending entry, or
  // it arrived late and on_late_ack was invoked to clean the offline copy.
  bool Acknowledge(const std::string& message_id);

  size_t pending_count() const;

private:
  void RunCheck();

  Config config_;
  PayloadCallback on_requeue_;
  PayloadCallback on_late_ack_;

  struct Pending {
    std::string receiver_id;
    std::string payload;
    int64_t deadline_ms;
  };
  struct Requeued {
    std::string receiver_id;
    std::string payload;
    int64_t requeued_at_ms;
  };
  std::unordered_map<std::string, Pending> pending_;
  // Messages already handed back to the offline queue, remembered so a late
  // ack can still remove the offline copy; swept on requeued_retention_ms.
  std::unordered_map<std::string, Requeued> requeued_;
  std::unordered_map<const void*, std::weak_ptr<network::Session>> capable_;

  mutable std::mutex mu_;
  asio::steady_timer timer_;
  std::atomic<bool> running_{false};
};

} // namespace chirp::chat
