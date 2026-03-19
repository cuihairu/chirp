#include "message_delivery_tracker.h"

#include "logger.h"

namespace chirp::chat {
namespace {

int64_t NowMs() {
  using namespace std::chrono;
  return duration_cast<milliseconds>(system_clock::now().time_since_epoch()).count();
}

} // namespace

MessageDeliveryTracker::MessageDeliveryTracker(asio::io_context& io,
                                              std::shared_ptr<HybridMessageStore> store,
                                              const Config& config)
    : timer_(io), io_(io), store_(std::move(store)), config_(config) {}

MessageDeliveryTracker::~MessageDeliveryTracker() {
  Stop();
}

void MessageDeliveryTracker::Start() {
  if (running_.load()) {
    return;
  }

  running_.store(true);
  Logger::Instance().Info("MessageDeliveryTracker started");

  // Schedule first check
  timer_.expires_after(std::chrono::seconds(config_.check_interval_seconds));
  timer_.async_wait([this](const std::error_code& ec) {
    if (!ec) {
      RunCheck();
    }
  });
}

void MessageDeliveryTracker::Stop() {
  if (!running_.load()) {
    return;
  }

  running_.store(false);
  timer_.cancel();
  Logger::Instance().Info("MessageDeliveryTracker stopped");
}

void MessageDeliveryTracker::TrackMessage(const std::string& message_id,
                                         const std::string& receiver_id,
                                         int64_t expires_at) {
  store_->TrackMessage(message_id, receiver_id, expires_at);
  total_tracked_.fetch_add(1);
}

void MessageDeliveryTracker::Acknowledge(const std::string& message_id,
                                        const std::string& user_id) {
  store_->AcknowledgeMessage(message_id, user_id);
  successful_count_.fetch_add(1);

  if (delivery_callback_) {
    delivery_callback_(message_id, user_id, DeliveryStatus::kDelivered, "");
  }
}

void MessageDeliveryTracker::Fail(const std::string& message_id,
                                 const std::string& user_id,
                                 const std::string& error) {
  store_->FailMessage(message_id, user_id, error);
  failed_count_.fetch_add(1);

  if (delivery_callback_) {
    delivery_callback_(message_id, user_id, DeliveryStatus::kFailed, error);
  }
}

std::optional<DeliveryInfo> MessageDeliveryTracker::GetStatus(const std::string& message_id,
                                                            const std::string& receiver_id) {
  return store_->GetDeliveryStatus(message_id, receiver_id);
}

MessageDeliveryTracker::Stats MessageDeliveryTracker::GetStats() const {
  Stats stats;
  stats.successful_deliveries = successful_count_.load();
  stats.failed_deliveries = failed_count_.load();
  stats.total_tracked = total_tracked_.load();

  // Get pending count from store
  int64_t check_before = NowMs() + (config_.delivery_timeout_seconds * 1000);
  auto pending = store_->GetPendingDeliveries(check_before);
  stats.pending_deliveries = pending.size();

  return stats;
}

void MessageDeliveryTracker::RunCheck() {
  if (!running_.load()) {
    return;
  }

  int64_t timeout_before = NowMs() - (config_.delivery_timeout_seconds * 1000);

  // Get pending deliveries that have timed out
  auto pending = store_->GetPendingDeliveries(timeout_before);

  for (const auto& info : pending) {
    // Check current status
    auto status = store_->GetDeliveryStatus(info.message_id, info.receiver_id);

    if (status && status->status == DeliveryStatus::kPending) {
      // Mark as failed if timeout exceeded
      Fail(info.message_id, info.receiver_id, "Delivery timeout");
      Logger::Instance().Warn("Message delivery timeout: " + info.message_id +
                             " to " + info.receiver_id);
    }
  }

  // Schedule next check
  if (running_.load()) {
    timer_.expires_after(std::chrono::seconds(config_.check_interval_seconds));
    timer_.async_wait([this](const std::error_code& ec) {
      if (!ec) {
        RunCheck();
      }
    });
  }
}

void MessageDeliveryTracker::ProcessPendingDelivery(const std::string& message_id,
                                                   const std::string& receiver_id,
                                                   int64_t expires_at) {
  // This would trigger a retry logic
  // For now, just log
  Logger::Instance().Debug("Processing pending delivery: " + message_id +
                          " to " + receiver_id);
}

} // namespace chirp::chat
