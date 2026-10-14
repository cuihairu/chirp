#pragma once

#include <atomic>
#include <cstddef>
#include <cstdint>
#include <memory>

#include <asio.hpp>

#include "hybrid_message_store.h"

namespace chirp::chat {

/// @brief 消息过期（game_chat_features P2）：按保留窗周期清理历史。
///
/// 形态：热层（Redis history 列表）+ 冷层（MySQL 档案）各按频道类型用各自
/// cutoff（私聊窗独立可配，落点 HybridMessageStore::ExpireBefore）；本类只管
/// 「何时清」——按 interval 周期把 now - retention 换算成 cutoff 交给存储。
/// 保留窗 <= 0 = 该类不清理（缺省 0 0 = 全关，app/companion 部署零感知；
/// 调用方把「私聊未配」解析成「跟随普通窗」，即「私聊保留更久」的旋钮）。
class HistoryRetentionSweeper {
public:
  /// 每小时一遍：清理是后台尽力而为，不需要更紧的节奏。
  static constexpr int64_t kDefaultIntervalMs = 60LL * 60 * 1000;

  HistoryRetentionSweeper(asio::io_context& io,
                          std::shared_ptr<HybridMessageStore> store,
                          int64_t other_retention_ms,
                          int64_t private_retention_ms,
                          int64_t interval_ms = kDefaultIntervalMs);
  ~HistoryRetentionSweeper();

  void Start();
  void Stop();

  /// 一趟同步清理，返回删除条目数。now_ms 显式传入（定时臂传墙钟，测试传
  /// 确定值钉 cutoff 裁决）。保留窗双 0 时直接返回，不发任何 I/O。
  size_t SweepOnce(int64_t now_ms);

private:
  void ScheduleNextRun();

  asio::steady_timer timer_;
  asio::io_context& io_;
  // 串行化对 timer_ 的所有触碰（与 MessageMigrationWorker 同款注释）：
  // Start()/Stop() 在调用方线程，到期处理跑在 io context 上，asio 定时器
  // 不抗并发 expires_after()/cancel()。
  asio::strand<asio::io_context::executor_type> strand_;
  std::shared_ptr<HybridMessageStore> store_;
  int64_t other_retention_ms_;
  int64_t private_retention_ms_;
  int64_t interval_ms_;
  std::atomic<bool> running_{false};
};

} // namespace chirp::chat
