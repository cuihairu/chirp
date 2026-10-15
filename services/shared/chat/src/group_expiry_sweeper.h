#pragma once

#include <atomic>
#include <cstdint>
#include <string>
#include <vector>

#include <asio.hpp>

#include "group_manager.h"

namespace chirp::chat {

/// @brief 临时频道（P1，2026-10-15）：周期回收到点的临时群
/// （game_chat_features P1「活动结束自动清理」）。
///
/// 读路径的惰性摘除（GroupManager::LookupLocked）只清「被读到」的到点群；
/// 没人访问的过期群靠这条周期清扫回收内存。
class GroupExpirySweeper {
 public:
  /// 每 5 分钟一遍：清理是后台尽力而为，ttl 精度为秒级，不需要更紧的节奏。
  static constexpr int64_t kDefaultIntervalMs = 5LL * 60 * 1000;

  GroupExpirySweeper(asio::io_context& io, GroupManager& groups,
                     int64_t interval_ms = kDefaultIntervalMs);
  ~GroupExpirySweeper();

  void Start();
  void Stop();

  /// 一趟同步清扫，返回被摘除的群 id。now_ms 显式传入（定时臂传墙钟，
  /// 测试传确定值钉裁决）。
  std::vector<std::string> SweepOnce(int64_t now_ms);

 private:
  void ScheduleNextRun();

  asio::steady_timer timer_;
  asio::io_context& io_;
  // 串行化对 timer_ 的所有触碰（与 HistoryRetentionSweeper 同款注释）：
  // Start()/Stop() 在调用方线程，到期处理跑在 io context 上，asio 定时器
  // 不抗并发 expires_after()/cancel()。
  asio::strand<asio::io_context::executor_type> strand_;
  GroupManager& groups_;
  int64_t interval_ms_;
  std::atomic<bool> running_{false};
};

}  // namespace chirp::chat
