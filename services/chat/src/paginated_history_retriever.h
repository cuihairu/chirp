#pragma once

#include <cstdint>
#include <memory>
#include <optional>
#include <string>
#include <vector>

#include "chat/src/hybrid_message_store.h"

namespace chirp::chat {

/// @brief Cursor-based pagination for message history
/// Provides efficient pagination across Redis and MySQL sources
class PaginatedHistoryRetriever {
public:
  /// @brief Pagination token
  struct PageToken {
    std::string cursor;
    int64_t timestamp{0};
    int32_t page_size{50};

    std::string Serialize() const;
    static PageToken Deserialize(const std::string& token);
    bool IsValid() const { return !cursor.empty(); }
  };

  /// @brief Page result
  struct PageResult {
    std::vector<MessageData> messages;
    PageToken next_page;
    bool has_more{false};
    int32_t total_count{0};
  };

  explicit PaginatedHistoryRetriever(std::shared_ptr<HybridMessageStore> store);

  /// @brief Get first page of history
  PageResult GetFirstPage(const std::string& channel_id,
                         int channel_type,
                         int32_t page_size);

  /// @brief Get next page from token
  PageResult GetNextPage(const PageToken& token);

  /// @brief Get page before timestamp
  PageResult GetPageBefore(const std::string& channel_id,
                          int channel_type,
                          int64_t before_timestamp,
                          int32_t page_size);

  /// @brief Get page after timestamp
  PageResult GetPageAfter(const std::string& channel_id,
                         int channel_type,
                         int64_t after_timestamp,
                         int32_t page_size);

  /// @brief Search messages by content (MySQL only)
  std::vector<MessageData> Search(const std::string& channel_id,
                                 int channel_type,
                                 const std::string& query,
                                 int32_t limit);

  /// @brief Get messages in time range
  std::vector<MessageData> GetTimeRange(const std::string& channel_id,
                                       int channel_type,
                                       int64_t start_timestamp,
                                       int64_t end_timestamp,
                                       int32_t limit);

private:
  std::shared_ptr<HybridMessageStore> store_;
};

} // namespace chirp::chat
