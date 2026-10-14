#include "report_ledger.h"

namespace chirp {
namespace chat {

ReportOutcome ReportLedger::Report(const std::string& message_id,
                                   const std::string& reporter_id,
                                   const std::string& reason) {
  ReportOutcome out;
  std::lock_guard<std::mutex> lock(mu_);
  ReportRecord& record = reports_[message_id];
  const bool inserted = record.reporter_ids.insert(reporter_id).second;
  if (inserted && record.reasons.size() < kMaxReasonsPerMessage) {
    record.reasons.push_back(reason);
  }
  out.first_report = inserted;
  out.report_count = static_cast<int32_t>(record.reporter_ids.size());
  return out;
}

int32_t ReportLedger::ReportCount(const std::string& message_id) const {
  std::lock_guard<std::mutex> lock(mu_);
  const auto it = reports_.find(message_id);
  if (it == reports_.end()) {
    return 0;
  }
  return static_cast<int32_t>(it->second.reporter_ids.size());
}

size_t ReportLedger::TrackedMessageCount() const {
  std::lock_guard<std::mutex> lock(mu_);
  return reports_.size();
}

}  // namespace chat
}  // namespace chirp
