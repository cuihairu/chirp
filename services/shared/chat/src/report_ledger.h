#ifndef CHIRP_SERVICES_CHAT_REPORT_LEDGER_H_
#define CHIRP_SERVICES_CHAT_REPORT_LEDGER_H_

#include <cstdint>
#include <mutex>
#include <string>
#include <unordered_map>
#include <unordered_set>
#include <vector>

namespace chirp {
namespace chat {

// 举报台账（game_chat_features P1「举报」，REPORT_MESSAGE 2253/2254）：按
// message_id 记一条举报记录——举报人集合（去重键 + 计数）与封顶留存的理由
// 列表。进程内存态，与 GroupManager 的禁言台账同一生命周期类：不持久化、不
// 跨实例扇出；理由列表给未来的管理后台审核队列留料。
struct ReportRecord {
  std::unordered_set<std::string> reporter_ids;
  std::vector<std::string> reasons;  // 封顶 kMaxReasonsPerMessage 条
};

// 单次举报的结果。
struct ReportOutcome {
  bool first_report = false;  // false = 该举报人已举报过（幂等，不再计数）
  int32_t report_count = 0;   // 本次举报后去重的累计举报数
};

class ReportLedger {
 public:
  // 记一次举报：按 (message_id, reporter_id) 幂等——重复举报不重复计数，回显
  // 当前累计数。理由只在首次举报时留一条（封顶后丢弃多余）。
  ReportOutcome Report(const std::string& message_id,
                       const std::string& reporter_id,
                       const std::string& reason);

  int32_t ReportCount(const std::string& message_id) const;
  size_t TrackedMessageCount() const;

 private:
  static constexpr size_t kMaxReasonsPerMessage = 100;

  mutable std::mutex mu_;
  std::unordered_map<std::string, ReportRecord> reports_;
};

}  // namespace chat
}  // namespace chirp

#endif  // CHIRP_SERVICES_CHAT_REPORT_LEDGER_H_
