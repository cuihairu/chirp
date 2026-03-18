#ifndef CHIRP_SERVICES_SEARCH_MESSAGE_SEARCH_SERVICE_H_
#define CHIRP_SERVICES_SEARCH_MESSAGE_SEARCH_SERVICE_H_

#include <chrono>
#include <functional>
#include <memory>
#include <mutex>
#include <string>
#include <unordered_map>
#include <unordered_set>
#include <vector>

#include "proto/chat.pb.h"

namespace chirp {
namespace search {

// Search query
struct SearchQuery {
  std::string query;              // Search text
  std::string user_id;            // User performing search
  std::string channel_id;         // Optional: limit to channel
  std::vector<std::string> channel_ids;  // Search multiple channels
  std::string sender_id;          // Optional: limit to sender
  std::string mentioned_user_id;  // Optional: messages mentioning user

  int64_t before_timestamp = 0;   // Only messages before this time
  int64_t after_timestamp = 0;    // Only messages after this time

  MsgType msg_type = MsgType::TEXT;  // Filter by message type
  bool has_attachments = false;   // Only messages with files

  int32_t limit = 50;             // Max results
  int32_t offset = 0;             // Pagination offset
};

// Search result with context
struct SearchResult {
  std::string message_id;
  std::string channel_id;
  std::string channel_name;
  std::string sender_id;
  std::string sender_username;
  std::string content;
  std::string snippet;            // Highlighted snippet
  int64_t timestamp = 0;

  // Relevance score
  double score = 0.0;

  // Match info
  std::vector<std::pair<size_t, size_t>> matches;  // start, length
};

// Search response
struct SearchResponse {
  std::vector<SearchResult> results;
  int32_t total_count = 0;
  int32_t offset = 0;
  bool has_more = false;
  double search_time_ms = 0.0;
};

// Document for indexing
struct IndexedDocument {
  std::string doc_id;             // message_id
  std::string content;
  std::string sender_id;
  std::string channel_id;
  int64_t timestamp = 0;
  MsgType msg_type = MsgType::TEXT;

  // For ranking
  int32_t reply_count = 0;
  int32_t reaction_count = 0;

  // For filtering
  std::vector<std::string> mentioned_user_ids;
  bool has_attachments = false;
};

// Configuration
struct SearchConfig {
  size_t max_results = 100;
  size_t max_snippet_length = 200;
  size_t max_context_length = 50;
  bool enable_fuzzy_search = true;
  int32_t min_word_length = 2;
  std::vector<std::string> stop_words;
};

// Simple in-memory full-text search
class MessageSearchService {
public:
  explicit MessageSearchService(const SearchConfig& config = SearchConfig());
  ~MessageSearchService() = default;

  // Index management
  bool IndexDocument(const IndexedDocument& doc);
  bool UpdateDocument(const std::string& doc_id, const IndexedDocument& doc);
  bool DeleteDocument(const std::string& doc_id);

  // Search
  SearchResponse Search(const SearchQuery& query);

  // Channel management
  bool AddChannel(const std::string& channel_id, const std::string& name);
  bool RemoveChannel(const std::string& channel_id);
  void SetChannelName(const std::string& channel_id, const std::string& name);

  // User management (for username lookup)
  void SetUsername(const std::string& user_id, const std::string& username);
  std::string GetUsername(const std::string& user_id);

  // Suggestions/autocomplete
  struct Suggestion {
    std::string text;
    std::string type;  // "query", "user", "channel"
    int32_t frequency = 0;
  };
  std::vector<Suggestion> GetSuggestions(const std::string& prefix, size_t limit = 10);

  // Statistics
  size_t GetDocumentCount() const;
  size_t GetIndexSizeBytes() const;
  void Reset();

private:
  // Tokenize text into words
  std::vector<std::string> Tokenize(const std::string& text);

  // Normalize word (lowercase, remove punctuation)
  std::string NormalizeWord(const std::string& word);

  // Check if word is a stop word
  bool IsStopWord(const std::string& word) const;

  // Calculate relevance score
  double CalculateScore(const SearchResult& result, const SearchQuery& query);

  // Generate snippet with highlighted matches
  std::string GenerateSnippet(const std::string& content,
                             const std::vector<std::pair<size_t, size_t>>& matches);

  // Inverted index: word -> set of doc_ids
  std::unordered_map<std::string, std::unordered_set<std::string>> inverted_index_;

  // Document storage
  std::unordered_map<std::string, IndexedDocument> documents_;

  // Channel names
  std::unordered_map<std::string, std::string> channel_names_;

  // Usernames
  std::unordered_map<std::string, std::string> usernames_;

  // Query frequency for suggestions
  std::unordered_map<std::string, int32_t> query_frequency_;

  SearchConfig config_;
  mutable std::mutex mu_;
};

} // namespace search
} // namespace chirp

#endif // CHIRP_SERVICES_SEARCH_MESSAGE_SEARCH_SERVICE_H_
