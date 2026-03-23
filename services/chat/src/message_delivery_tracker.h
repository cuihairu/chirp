#pragma once

#include <cstdint>
#include <functional>
#include <memory>
#include <string>
#include <unordered_map>

#include <asio.hpp>

#include "hybrid_message_store.h"

namespace chirp::chat {

/// @brief Tracks message delivery status and handles retries
class MessageDeliveryTracker {
public:
  using DeliveryCallback = std::function<void(const std::string& message_id,
                                              const std::string& receiver_id,
                                              DeliveryStatus status,
                                              const std::string& error)>;

  /// @brief Configuration
  struct Config {
    int check_interval_seconds = 5;
    int max_retries = 5;
    int64_t delivery_timeout_seconds = 300;  // 5 minutes
    int64_t pending_expiry_seconds = 86400;  // 24 hours
  };

  explicit MessageDeliveryTracker(asio::io_context& io,
                                 std::shared_ptr<HybridMessageStore> store,
                                 const Config& config = Config{});
  ~MessageDeliveryTracker();

  /// @brief Start the tracker
  void Start();

  /// @brief Stop the tracker
  void Stop();

  /// @brief Set callback for delivery status changes
  void SetDeliveryCallback(DeliveryCallback cb) { delivery_callback_ = std::move(cb); }

  /// @brief Track a message for delivery
  void TrackMessage(const std::string& message_id,
                   const std::string& receiver_id,
                   int64_t expires_at);

  /// @brief Acknowledge message delivery
  void Acknowledge(const std::string& message_id, const std::string& user_id);

  /// @brief Mark message as failed
  void Fail(const std::string& message_id,
           const std::string& user_id,
           const std::string& error);

  /// @brief Get delivery status
  std::optional<DeliveryInfo> GetStatus(const std::string& message_id,
                                       const std::string& receiver_id);

  /// @brief Get statistics
  struct Stats {
    uint64_t pending_deliveries{0};
    uint64_t successful_deliveries{0};
    uint64_t failed_deliveries{0};
    uint64_t total_tracked{0};
  };
  Stats GetStats() const;

private:
  void RunCheck();
  void ProcessPendingDelivery(const std::string& message_id,
                            const std::string& receiver_id,
                            int64_t expires_at);

  asio::steady_timer timer_;
  asio::io_context& io_;
  std::shared_ptr<HybridMessageStore> store_;
  Config config_;
  DeliveryCallback delivery_callback_;

  std::atomic<bool> running_{false};
  std::atomic<uint64_t> successful_count_{0};
  std::atomic<uint64_t> failed_count_{0};
  std::atomic<uint64_t> total_tracked_{0};
};

} // namespace chirp::chat
