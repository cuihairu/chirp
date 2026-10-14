#include "history_retention_sweeper.h"

#include "logger.h"

namespace chirp::chat {
namespace {

using Logger = chirp::common::Logger;

int64_t NowMs() {
  using namespace std::chrono;
  return duration_cast<milliseconds>(system_clock::now().time_since_epoch()).count();
}

} // namespace

HistoryRetentionSweeper::HistoryRetentionSweeper(asio::io_context& io,
                                                 std::shared_ptr<HybridMessageStore> store,
                                                 int64_t other_retention_ms,
                                                 int64_t private_retention_ms,
                                                 int64_t interval_ms)
    : timer_(io), io_(io), strand_(io.get_executor()),
      store_(std::move(store)), other_retention_ms_(other_retention_ms),
      private_retention_ms_(private_retention_ms), interval_ms_(interval_ms) {}

HistoryRetentionSweeper::~HistoryRetentionSweeper() {
  Stop();
}

void HistoryRetentionSweeper::Start() {
  if (running_.load()) {
    return;
  }
  if (other_retention_ms_ <= 0 && private_retention_ms_ <= 0) {
    // 保留窗双 0 = 关（缺省部署零 I/O，连定时器都不挂）。
    Logger::Instance().Info("HistoryRetentionSweeper disabled by configuration");
    return;
  }

  running_.store(true);
  Logger::Instance().Info(
      "HistoryRetentionSweeper started (other: " + std::to_string(other_retention_ms_) +
      "ms, private: " + std::to_string(private_retention_ms_) + "ms, interval: " +
      std::to_string(interval_ms_) + "ms)");

  // 所有 timer 变更走 strand：Start() 在调用方线程，到期处理在 io context。
  asio::post(strand_, [this] { ScheduleNextRun(); });
}

void HistoryRetentionSweeper::Stop() {
  if (!running_.load()) {
    return;
  }

  running_.store(false);
  // timer 变更走 strand：Stop() 在调用方线程，ScheduleNextRun() 在 io context。
  asio::post(strand_, [this] { timer_.cancel(); });
  Logger::Instance().Info("HistoryRetentionSweeper stopped");
}

size_t HistoryRetentionSweeper::SweepOnce(int64_t now_ms) {
  if (other_retention_ms_ <= 0 && private_retention_ms_ <= 0) {
    return 0;
  }
  const int64_t other_cutoff =
      other_retention_ms_ > 0 ? now_ms - other_retention_ms_ : 0;
  const int64_t private_cutoff =
      private_retention_ms_ > 0 ? now_ms - private_retention_ms_ : 0;
  const size_t removed = store_->ExpireBefore(private_cutoff, other_cutoff);
  if (removed > 0) {
    Logger::Instance().Info("History retention sweep removed " +
                            std::to_string(removed) + " entries");
  }
  return removed;
}

void HistoryRetentionSweeper::ScheduleNextRun() {
  if (!running_.load()) {
    return;
  }

  // 只从 strand 调度（Start/到期尾），timer 不会撞并发 expires_after()/cancel()。
  timer_.expires_after(std::chrono::milliseconds(interval_ms_));
  timer_.async_wait(asio::bind_executor(strand_, [this](const std::error_code& ec) {
    if (ec || !running_.load()) {
      return;
    }
    SweepOnce(NowMs());
    ScheduleNextRun();
  }));
}

} // namespace chirp::chat
