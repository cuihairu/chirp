#include "message_search_service.h"

#include <algorithm>
#include <cctype>
#include <sstream>
#include <unordered_set>

namespace chirp {
namespace search {

namespace {

// Default stop words
const std::vector<std::string> kDefaultStopWords = {
  "a", "an", "and", "are", "as", "at", "be", "by", "for", "from",
  "has", "he", "in", "is", "it", "its", "of", "on", "that", "the",
  "to", "was", "were", "will", "with"
};

std::vector<std::string> TokenizeForIndex(const std::string& text) {
  std::vector<std::string> tokens;
  std::string current;

  for (unsigned char c : text) {
    if (std::isalnum(c) || c == '_' || c == '@' || c == '#') {
      current += static_cast<char>(std::tolower(c));
    } else if (!current.empty()) {
      tokens.push_back(current);
      current.clear();
    }
  }

  if (!current.empty()) {
    tokens.push_back(current);
  }

  return tokens;
}

void RemoveDocumentFromIndex(
    const chirp::search::IndexedDocument& doc,
    std::unordered_map<std::string, std::unordered_set<std::string>>* inverted_index) {
  auto tokens = TokenizeForIndex(doc.content);
  for (const auto& token : tokens) {
    auto index_it = inverted_index->find(token);
    if (index_it == inverted_index->end()) {
      continue;
    }
    index_it->second.erase(doc.doc_id);
    if (index_it->second.empty()) {
      inverted_index->erase(index_it);
    }
  }
}

} // namespace

MessageSearchService::MessageSearchService(const SearchConfig& config)
    : config_(config) {
  if (config_.stop_words.empty()) {
    config_.stop_words = kDefaultStopWords;
  }
}

std::vector<std::string> MessageSearchService::Tokenize(const std::string& text) {
  std::vector<std::string> tokens;
  std::string current;

  for (char c : text) {
    if (std::isalnum(c) || c == '_' || c == '@' || c == '#') {
      current += c;
    } else if (!current.empty()) {
      tokens.push_back(NormalizeWord(current));
      current.clear();
    }
  }

  if (!current.empty()) {
    tokens.push_back(NormalizeWord(current));
  }

  return tokens;
}

std::string MessageSearchService::NormalizeWord(const std::string& word) {
  std::string result;
  for (char c : word) {
    if (std::isalnum(c) || c == '_' || c == '@' || c == '#') {
      result += std::tolower(c);
    }
  }
  return result;
}

bool MessageSearchService::IsStopWord(const std::string& word) const {
  return std::find(config_.stop_words.begin(), config_.stop_words.end(), word)
      != config_.stop_words.end();
}

bool MessageSearchService::IndexDocument(const IndexedDocument& doc) {
  std::lock_guard<std::mutex> lock(mu_);

  auto existing = documents_.find(doc.doc_id);
  if (existing != documents_.end()) {
    RemoveDocumentFromIndex(existing->second, &inverted_index_);
    documents_.erase(existing);
  }

  // Store document
  documents_[doc.doc_id] = doc;

  // Tokenize and index
  auto tokens = Tokenize(doc.content);
  for (const auto& token : tokens) {
    if (token.length() < static_cast<size_t>(config_.min_word_length)) {
      continue;
    }
    if (IsStopWord(token)) {
      continue;
    }
    inverted_index_[token].insert(doc.doc_id);
  }

  return true;
}

bool MessageSearchService::UpdateDocument(const std::string& doc_id,
                                        const IndexedDocument& doc) {
  return IndexDocument(doc);
}

bool MessageSearchService::DeleteDocument(const std::string& doc_id) {
  std::lock_guard<std::mutex> lock(mu_);

  auto it = documents_.find(doc_id);
  if (it == documents_.end()) {
    return false;
  }

  RemoveDocumentFromIndex(it->second, &inverted_index_);

  documents_.erase(it);
  return true;
}

SearchResponse MessageSearchService::Search(const SearchQuery& query) {
  auto start_time = std::chrono::steady_clock::now();
  SearchResponse response;

  std::lock_guard<std::mutex> lock(mu_);

  // Tokenize query
  auto query_tokens = Tokenize(query.query);

  // Find matching documents
  std::unordered_map<std::string, int32_t> doc_matches;  // doc_id -> match count
  std::unordered_map<std::string, std::vector<std::pair<size_t, size_t>>> doc_matches_positions;

  for (const auto& token : query_tokens) {
    if (token.length() < static_cast<size_t>(config_.min_word_length)) {
      continue;
    }

    auto it = inverted_index_.find(token);
    if (it == inverted_index_.end()) {
      continue;
    }

    for (const auto& doc_id : it->second) {
      doc_matches[doc_id]++;
    }
  }

  // Filter and score results
  std::vector<SearchResult> results;

  for (const auto& [doc_id, match_count] : doc_matches) {
    auto doc_it = documents_.find(doc_id);
    if (doc_it == documents_.end()) {
      continue;
    }

    const auto& doc = doc_it->second;

    // Apply filters
    if (!query.channel_id.empty() && doc.channel_id != query.channel_id) {
      continue;
    }

    if (!query.channel_ids.empty()) {
      if (std::find(query.channel_ids.begin(), query.channel_ids.end(),
                    doc.channel_id) == query.channel_ids.end()) {
        continue;
      }
    }

    if (!query.sender_id.empty() && doc.sender_id != query.sender_id) {
      continue;
    }

    if (!query.mentioned_user_id.empty()) {
      if (std::find(doc.mentioned_user_ids.begin(), doc.mentioned_user_ids.end(),
                    query.mentioned_user_id) == doc.mentioned_user_ids.end()) {
        continue;
      }
    }

    if (query.before_timestamp > 0 && doc.timestamp >= query.before_timestamp) {
      continue;
    }

    if (query.after_timestamp > 0 && doc.timestamp <= query.after_timestamp) {
      continue;
    }

    if (query.msg_type != ::chirp::chat::TEXT && doc.msg_type != query.msg_type) {
      continue;
    }

    if (query.has_attachments && !doc.has_attachments) {
      continue;
    }

    // Build result
    SearchResult result;
    result.message_id = doc.doc_id;
    result.channel_id = doc.channel_id;
    result.sender_id = doc.sender_id;
    result.content = doc.content;
    result.timestamp = doc.timestamp;

    // Look up names
    auto channel_it = channel_names_.find(doc.channel_id);
    if (channel_it != channel_names_.end()) {
      result.channel_name = channel_it->second;
    }

    auto user_it = usernames_.find(doc.sender_id);
    if (user_it != usernames_.end()) {
      result.sender_username = user_it->second;
    }

    // Find matches for snippet
    std::string lower_content = doc.content;
    std::transform(lower_content.begin(), lower_content.end(),
                   lower_content.begin(), ::tolower);

    std::string lower_query = query.query;
    std::transform(lower_query.begin(), lower_query.end(),
                   lower_query.begin(), ::tolower);

    size_t pos = 0;
    while ((pos = lower_content.find(lower_query, pos)) != std::string::npos) {
      result.matches.emplace_back(pos, lower_query.length());
      pos += lower_query.length();
    }

    result.snippet = GenerateSnippet(doc.content, result.matches);
    result.score = CalculateScore(result, query);

    results.push_back(std::move(result));
  }

  // Sort by score (descending)
  std::sort(results.begin(), results.end(),
    [](const SearchResult& a, const SearchResult& b) {
      return a.score > b.score;
    });

  // Apply pagination
  size_t total_count = results.size();
  size_t start = std::min(static_cast<size_t>(query.offset), total_count);
  size_t end = std::min(start + static_cast<size_t>(query.limit), total_count);

  if (start >= total_count) {
    response.results = {};
  } else {
    response.results.assign(results.begin() + start, results.begin() + end);
  }

  response.total_count = static_cast<int32_t>(total_count);
  response.offset = query.offset;
  response.has_more = end < total_count;

  // Calculate search time
  auto end_time = std::chrono::steady_clock::now();
  response.search_time_ms = std::chrono::duration<double, std::milli>(
      end_time - start_time).count();

  // Update query frequency for suggestions
  if (!query.query.empty()) {
    query_frequency_[query.query]++;
  }

  return response;
}

double MessageSearchService::CalculateScore(const SearchResult& result,
                                           const SearchQuery& query) {
  double score = 0.0;

  // Base score for matches
  score += result.matches.size() * 10.0;

  // Boost for exact phrase match
  if (result.content.find(query.query) != std::string::npos) {
    score += 50.0;
  }

  // Recency boost (messages in last 24 hours)
  int64_t now = std::chrono::duration_cast<std::chrono::milliseconds>(
      std::chrono::system_clock::now().time_since_epoch()).count();
  int64_t day_ms = 24 * 3600 * 1000;

  if (result.timestamp > 0) {
    int64_t age_ms = now - result.timestamp;
    if (age_ms < day_ms) {
      score += 20.0;
    } else if (age_ms < 7 * day_ms) {
      score += 10.0;
    }
  }

  // Document-specific boosts
  auto doc_it = documents_.find(result.message_id);
  if (doc_it != documents_.end()) {
    const auto& doc = doc_it->second;
    score += doc.reply_count * 2.0;
    score += doc.reaction_count * 1.0;
  }

  return score;
}

std::string MessageSearchService::GenerateSnippet(
    const std::string& content,
    const std::vector<std::pair<size_t, size_t>>& matches) {

  if (matches.empty()) {
    size_t len = std::min(content.length(), config_.max_snippet_length);
    return content.substr(0, len) + (len < content.length() ? "..." : "");
  }

  // Find the best match (first one for now)
  auto [start, length] = matches[0];

  // Add context around the match
  size_t context_start = std::max(
      static_cast<size_t>(0),
      start - config_.max_context_length);

  size_t context_end = std::min(
      content.length(),
      start + length + config_.max_context_length);

  std::string snippet = content.substr(context_start, context_end - context_start);

  // Add ellipsis if truncated
  if (context_start > 0) {
    snippet = "..." + snippet;
  }
  if (context_end < content.length()) {
    snippet += "...";
  }

  // Truncate to max length
  if (snippet.length() > config_.max_snippet_length) {
    snippet = snippet.substr(0, config_.max_snippet_length) + "...";
  }

  return snippet;
}

bool MessageSearchService::AddChannel(const std::string& channel_id,
                                     const std::string& name) {
  std::lock_guard<std::mutex> lock(mu_);
  channel_names_[channel_id] = name;
  return true;
}

bool MessageSearchService::RemoveChannel(const std::string& channel_id) {
  std::lock_guard<std::mutex> lock(mu_);
  return channel_names_.erase(channel_id) > 0;
}

void MessageSearchService::SetChannelName(const std::string& channel_id,
                                         const std::string& name) {
  std::lock_guard<std::mutex> lock(mu_);
  channel_names_[channel_id] = name;
}

void MessageSearchService::SetUsername(const std::string& user_id,
                                      const std::string& username) {
  std::lock_guard<std::mutex> lock(mu_);
  usernames_[user_id] = username;
}

std::string MessageSearchService::GetUsername(const std::string& user_id) {
  std::lock_guard<std::mutex> lock(mu_);
  auto it = usernames_.find(user_id);
  return it != usernames_.end() ? it->second : "";
}

std::vector<MessageSearchService::Suggestion> MessageSearchService::GetSuggestions(
    const std::string& prefix,
    size_t limit) {

  std::vector<Suggestion> suggestions;
  std::string lower_prefix = NormalizeWord(prefix);

  std::lock_guard<std::mutex> lock(mu_);

  // Query suggestions
  for (const auto& [query, freq] : query_frequency_) {
    std::string lower_query = NormalizeWord(query);
    if (lower_query.find(lower_prefix) == 0) {
      Suggestion sugg;
      sugg.text = query;
      sugg.type = "query";
      sugg.frequency = freq;
      suggestions.push_back(sugg);
    }
  }

  // User suggestions
  for (const auto& [user_id, username] : usernames_) {
    std::string lower_username = NormalizeWord(username);
    if (lower_username.find(lower_prefix) == 0) {
      Suggestion sugg;
      sugg.text = username;
      sugg.type = "user";
      suggestions.push_back(sugg);
    }
  }

  // Channel suggestions
  for (const auto& [channel_id, name] : channel_names_) {
    std::string lower_name = NormalizeWord(name);
    if (lower_name.find(lower_prefix) == 0) {
      Suggestion sugg;
      sugg.text = name;
      sugg.type = "channel";
      suggestions.push_back(sugg);
    }
  }

  // Sort by frequency/relevance and limit
  std::sort(suggestions.begin(), suggestions.end(),
    [](const Suggestion& a, const Suggestion& b) {
      return a.frequency > b.frequency;
    });

  if (suggestions.size() > limit) {
    suggestions.resize(limit);
  }

  return suggestions;
}

size_t MessageSearchService::GetDocumentCount() const {
  std::lock_guard<std::mutex> lock(mu_);
  return documents_.size();
}

size_t MessageSearchService::GetIndexSizeBytes() const {
  std::lock_guard<std::mutex> lock(mu_);

  size_t size = 0;
  for (const auto& [word, docs] : inverted_index_) {
    size += word.size();
    size += docs.size() * sizeof(std::string);  // Approximate
  }
  for (const auto& [id, doc] : documents_) {
    size += id.size() + doc.content.size();
  }
  return size;
}

void MessageSearchService::Reset() {
  std::lock_guard<std::mutex> lock(mu_);
  inverted_index_.clear();
  documents_.clear();
  query_frequency_.clear();
}

} // namespace search
} // namespace chirp
