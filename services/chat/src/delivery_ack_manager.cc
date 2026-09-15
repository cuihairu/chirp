#include "delivery_ack_manager.h"

#include <chrono>
#include <vector>

#include "logger.h"

namespace chirp::chat {
namespace {

using Logger = chirp::common::Logger;

int64_t NowMs() {
  using namespace std::chrono;
  return duration_cast<milliseconds>(system_clock::now().time_since_epoch()).count();
}

} // namespace

DeliveryAckManager::DeliveryAckManager(asio::io_context& io, Config config,
                                       PayloadCallback on_requeue,
                                       PayloadCallback on_late_ack)
    : config_(config),
      on_requeue_(std::move(on_requeue)),
      on_late_ack_(std::move(on_late_ack)),
      timer_(io) {}

DeliveryAckManager::~DeliveryAckManager() {
  Stop();
}

void DeliveryAckManager::Start() {
  if (running_.load()) {
    return;
  }
  if (config_.timeout_ms <= 0) {
    Logger::Instance().Info("DeliveryAckManager disabled (ack timeout is 0)");
    return;
  }

  running_.store(true);
  Logger::Instance().Info("DeliveryAckManager started timeout_ms=" +
                          std::to_string(config_.timeout_ms));

  timer_.expires_after(std::chrono::milliseconds(config_.scan_interval_ms));
  timer_.async_wait([this](const std::error_code& ec) {
    if (!ec) {
      RunCheck();
    }
  });
}

void DeliveryAckManager::Stop() {
  if (!running_.exchange(false)) {
    return;
  }
  timer_.cancel();
  Logger::Instance().Info("DeliveryAckManager stopped");
}

void DeliveryAckManager::MarkCapable(const std::shared_ptr<network::Session>& session) {
  if (config_.timeout_ms <= 0 || !session) {
    return;
  }
  std::lock_guard<std::mutex> lock(mu_);
  capable_[session.get()] = session;
}

bool DeliveryAckManager::IsCapable(const network::Session* session) {
  if (config_.timeout_ms <= 0 || session == nullptr) {
    return false;
  }
  std::lock_guard<std::mutex> lock(mu_);
  const auto it = capable_.find(session);
  if (it == capable_.end()) {
    return false;
  }
  // An expired weak_ptr means the connection is gone; drop the stale entry so
  // the map cannot outgrow the set of live sessions.
  if (it->second.expired()) {
    capable_.erase(it);
    return false;
  }
  return true;
}

void DeliveryAckManager::ForgetSession(const network::Session* session) {
  if (session == nullptr) {
    return;
  }
  std::lock_guard<std::mutex> lock(mu_);
  capable_.erase(session);
}

void DeliveryAckManager::Track(const std::string& message_id,
                               const std::string& receiver_id,
                               const std::string& payload) {
  if (config_.timeout_ms <= 0 || message_id.empty() || receiver_id.empty()) {
    return;
  }
  std::lock_guard<std::mutex> lock(mu_);
  pending_[message_id] = Pending{receiver_id, payload, NowMs() + config_.timeout_ms};
}

bool DeliveryAckManager::Acknowledge(const std::string& message_id) {
  if (config_.timeout_ms <= 0 || message_id.empty()) {
    return false;
  }

  std::pair<std::string, std::string> late;
  bool was_requeued = false;
  {
    std::lock_guard<std::mutex> lock(mu_);
    if (pending_.erase(message_id) > 0) {
      return true;
    }
    const auto it = requeued_.find(message_id);
    if (it != requeued_.end()) {
      late = {it->second.receiver_id, it->second.payload};
      requeued_.erase(it);
      was_requeued = true;
    }
  }

  if (was_requeued) {
    // The client did receive the message (its ack just beat the offline
    // refill); drop the copy that already timed out into the offline queue.
    Logger::Instance().Info("late message ack after requeue: " + message_id +
                            " user=" + late.first);
    if (on_late_ack_) {
      on_late_ack_(late.first, late.second);
    }
    return true;
  }
  return false;
}

size_t DeliveryAckManager::pending_count() const {
  std::lock_guard<std::mutex> lock(mu_);
  return pending_.size();
}

void DeliveryAckManager::RunCheck() {
  if (!running_.load()) {
    return;
  }

  const int64_t now = NowMs();

  struct Expired {
    std::string message_id;
    std::string receiver_id;
    std::string payload;
  };
  std::vector<Expired> expired;

  {
    std::lock_guard<std::mutex> lock(mu_);
    for (auto it = pending_.begin(); it != pending_.end();) {
      if (it->second.deadline_ms <= now) {
        expired.push_back(Expired{it->first, it->second.receiver_id, it->second.payload});
        requeued_[it->first] =
            Requeued{it->second.receiver_id, it->second.payload, now};
        it = pending_.erase(it);
      } else {
        ++it;
      }
    }

    for (auto it = requeued_.begin(); it != requeued_.end();) {
      if (now - it->second.requeued_at_ms > config_.requeued_retention_ms) {
        // Past the retention window the offline copy is treated as the
        // message of record; a still later ack just leaves the refill as is.
        Logger::Instance().Info("late ack window closed for requeued message: " +
                                it->first);
        it = requeued_.erase(it);
      } else {
        ++it;
      }
    }
  }

  for (const auto& e : expired) {
    Logger::Instance().Warn("message ack timeout, requeued offline: " + e.message_id +
                            " -> " + e.receiver_id);
    if (on_requeue_) {
      on_requeue_(e.receiver_id, e.payload);
    }
  }

  if (running_.load()) {
    timer_.expires_after(std::chrono::milliseconds(config_.scan_interval_ms));
    timer_.async_wait([this](const std::error_code& ec) {
      if (!ec) {
        RunCheck();
      }
    });
  }
}

} // namespace chirp::chat
