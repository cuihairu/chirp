#include "paginated_history_retriever.h"

#include <sstream>

#include "logger.h"

namespace chirp::chat {

using Logger = chirp::common::Logger;

PaginatedHistoryRetriever::PaginatedHistoryRetriever(std::shared_ptr<HybridMessageStore> store)
    : store_(std::move(store)) {}

std::string PaginatedHistoryRetriever::PageToken::Serialize() const {
  std::ostringstream oss;
  oss << cursor << "|" << timestamp << "|" << page_size;
  return oss.str();
}

PaginatedHistoryRetriever::PageToken
PaginatedHistoryRetriever::PageToken::Deserialize(const std::string& token) {
  PageToken result;

  size_t pos1 = token.find('|');
  size_t pos2 = token.find('|', pos1 + 1);

  if (pos1 != std::string::npos && pos2 != std::string::npos) {
    result.cursor = token.substr(0, pos1);
    result.timestamp = std::stoll(token.substr(pos1 + 1, pos2 - pos1 - 1));
    result.page_size = std::stoi(token.substr(pos2 + 1));
  }

  return result;
}

PaginatedHistoryRetriever::PageResult
PaginatedHistoryRetriever::GetFirstPage(const std::string& channel_id,
                                       int channel_type,
                                       int32_t page_size) {
  PageResult result;
  int64_t now = []() {
    using namespace std::chrono;
    return duration_cast<milliseconds>(system_clock::now().time_since_epoch()).count();
  }();

  auto messages = store_->GetHistory(channel_id, channel_type, now, page_size);

  result.messages = std::move(messages);
  result.has_more = !result.messages.empty();

  if (result.has_more && !result.messages.empty()) {
    result.next_page.cursor = channel_id;
    result.next_page.timestamp = result.messages.front().timestamp;
    result.next_page.page_size = page_size;
  }

  result.total_count = static_cast<int32_t>(result.messages.size());

  return result;
}

PaginatedHistoryRetriever::PageResult
PaginatedHistoryRetriever::GetNextPage(const PageToken& token) {
  PageResult result;

  if (!token.IsValid()) {
    return result;
  }

  auto messages = store_->GetHistory(token.cursor, 0, token.timestamp, token.page_size);

  result.messages = std::move(messages);
  result.has_more = !result.messages.empty();

  if (result.has_more && !result.messages.empty()) {
    result.next_page.cursor = token.cursor;
    result.next_page.timestamp = result.messages.front().timestamp;
    result.next_page.page_size = token.page_size;
  }

  result.total_count = static_cast<int32_t>(result.messages.size());

  return result;
}

PaginatedHistoryRetriever::PageResult
PaginatedHistoryRetriever::GetPageBefore(const std::string& channel_id,
                                        int channel_type,
                                        int64_t before_timestamp,
                                        int32_t page_size) {
  PageResult result;

  auto messages = store_->GetHistory(channel_id, channel_type, before_timestamp, page_size);

  result.messages = std::move(messages);
  result.has_more = !result.messages.empty();

  if (result.has_more && !result.messages.empty()) {
    result.next_page.cursor = channel_id;
    result.next_page.timestamp = result.messages.front().timestamp;
    result.next_page.page_size = page_size;
  }

  result.total_count = static_cast<int32_t>(result.messages.size());

  return result;
}

PaginatedHistoryRetriever::PageResult
PaginatedHistoryRetriever::GetPageAfter(const std::string& channel_id,
                                       int channel_type,
                                       int64_t after_timestamp,
                                       int32_t page_size) {
  // For "after" pagination, we need to get history and filter
  // This is less efficient and should be avoided when possible
  PageResult result;

  auto messages = store_->GetHistory(channel_id, channel_type, 0, page_size * 2);

  // Filter messages after the timestamp
  std::vector<MessageData> filtered;
  filtered.reserve(page_size);

  for (auto& msg : messages) {
    if (msg.timestamp > after_timestamp) {
      filtered.push_back(std::move(msg));
      if (static_cast<int>(filtered.size()) >= page_size) {
        break;
      }
    }
  }

  result.messages = std::move(filtered);
  result.has_more = !result.messages.empty();

  if (result.has_more && !result.messages.empty()) {
    result.next_page.cursor = channel_id;
    result.next_page.timestamp = result.messages.back().timestamp;
    result.next_page.page_size = page_size;
  }

  result.total_count = static_cast<int32_t>(result.messages.size());

  return result;
}

std::vector<MessageData>
PaginatedHistoryRetriever::Search(const std::string& channel_id,
                                 int channel_type,
                                 const std::string& query,
                                 int32_t limit) {
  // Search requires MySQL full-text search
  // For now, return empty as this requires MySQL implementation
  Logger::Instance().Debug("Search requested but not yet implemented: " + query);
  return {};
}

std::vector<MessageData>
PaginatedHistoryRetriever::GetTimeRange(const std::string& channel_id,
                                        int channel_type,
                                        int64_t start_timestamp,
                                        int64_t end_timestamp,
                                        int32_t limit) {
  // Get a larger set and filter by range
  auto messages = store_->GetHistory(channel_id, channel_type, end_timestamp, limit * 2);

  std::vector<MessageData> filtered;
  filtered.reserve(limit);

  for (auto& msg : messages) {
    if (msg.timestamp >= start_timestamp && msg.timestamp <= end_timestamp) {
      filtered.push_back(std::move(msg));
      if (static_cast<int>(filtered.size()) >= limit) {
        break;
      }
    }
  }

  return filtered;
}

} // namespace chirp::chat
