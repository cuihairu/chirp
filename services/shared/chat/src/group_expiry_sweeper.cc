#include "group_expiry_sweeper.h"

#include <chrono>

#include "logger.h"

namespace chirp::chat {
namespace {

using Logger = chirp::common::Logger;

int64_t NowMs() {
  using namespace std::chrono;
  return duration_cast<milliseconds>(system_clock::now().time_since_epoch()).count();
}

}  // namespace

GroupExpirySweeper::GroupExpirySweeper(asio::io_context& io, GroupManager& groups,
                                       int64_t interval_ms)
    : timer_(io), io_(io), strand_(io.get_executor()), groups_(groups),
      interval_ms_(interval_ms) {}

GroupExpirySweeper::~GroupExpirySweeper() {
  Stop();
}

void GroupExpirySweeper::Start() {
  if (running_.load()) {
    return;
  }
  running_.store(true);
  Logger::Instance().Info("GroupExpirySweeper started (interval: " +
                          std::to_string(interval_ms_) + "ms)");

  // 所有 timer 变更走 strand：Start() 在调用方线程，到期处理在 io context。
  asio::post(strand_, [this] { ScheduleNextRun(); });
}

void GroupExpirySweeper::Stop() {
  if (!running_.load()) {
    return;
  }

  running_.store(false);
  // timer 变更走 strand：Stop() 在调用方线程，ScheduleNextRun() 在 io context。
  asio::post(strand_, [this] { timer_.cancel(); });
  Logger::Instance().Info("GroupExpirySweeper stopped");
}

std::vector<std::string> GroupExpirySweeper::SweepOnce(int64_t now_ms) {
  const auto removed = groups_.SweepExpired(now_ms);
  if (!removed.empty()) {
    Logger::Instance().Info("Group expiry sweep removed " +
                            std::to_string(removed.size()) + " groups");
  }
  return removed;
}

void GroupExpirySweeper::ScheduleNextRun() {
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

}  // namespace chirp::chat
