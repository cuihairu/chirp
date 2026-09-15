// An in-memory RESP command handler for FakeRedisServer: enough Redis
// semantics (strings, lists, expiry-less TTL bookkeeping, KEYS globbing) to
// drive RedisClient-based production code (hybrid store, auth store) in
// unit tests without a real server.

#pragma once

#include <mutex>
#include <string>
#include <unordered_map>
#include <vector>

#include "fake_servers.h"

namespace chirp_test {

class InMemoryRedis {
 public:
  std::string Handle(const std::vector<std::string>& args) {
    if (args.empty()) {
      return Simple("OK");
    }
    std::lock_guard<std::mutex> lock(mu_);
    const std::string& cmd = args[0];
    if (cmd == "GET") {
      auto it = kv_.find(args[1]);
      return it == kv_.end() ? "$-1\r\n" : Bulk(it->second);
    }
    if (cmd == "SET" || cmd == "SETEX") {
      // SETEX key ttl value / SET key value
      if (cmd == "SETEX") {
        kv_[args[1]] = args[3];
      } else {
        kv_[args[1]] = args[2];
      }
      return Simple("OK");
    }
    if (cmd == "DEL") {
      size_t removed = 0;
      for (size_t i = 1; i < args.size(); ++i) {
        removed += kv_.erase(args[i]) + lists_.erase(args[i]);
      }
      return Int(static_cast<int64_t>(removed));
    }
    if (cmd == "EXPIRE") {
      return kv_.count(args[1]) || lists_.count(args[1]) ? Int(1) : Int(0);
    }
    if (cmd == "RPUSH") {
    auto& list = lists_[args[1]];
      for (size_t i = 2; i < args.size(); ++i) {
        list.push_back(args[i]);
      }
      return Int(static_cast<int64_t>(list.size()));
    }
    if (cmd == "LPUSH") {
      auto& list = lists_[args[1]];
      for (size_t i = 2; i < args.size(); ++i) {
        list.insert(list.begin(), args[i]);
      }
      return Int(static_cast<int64_t>(list.size()));
    }
    if (cmd == "LRANGE") {
      static const std::vector<std::string> kEmpty;
      auto it = lists_.find(args[1]);
      const auto& list = it == lists_.end() ? kEmpty : it->second;
      const int64_t n = static_cast<int64_t>(list.size());
      int64_t start = std::stoll(args[2]);
      int64_t stop = std::stoll(args[3]);
      if (start < 0) start += n;
      if (stop < 0) stop += n;
      start = std::max<int64_t>(start, 0);
      stop = std::min<int64_t>(stop, n - 1);
      std::vector<std::string> items;
      for (int64_t i = start; i <= stop; ++i) {
        items.push_back(list[static_cast<size_t>(i)]);
      }
      return Array(items);
    }
    if (cmd == "LREM") {
      // Only the count=1 (head-to-tail) form is exercised by production code.
      auto it = lists_.find(args[1]);
      if (it == lists_.end()) {
        return Int(0);
      }
      int64_t removed = 0;
      const std::string& value = args[3];
      for (auto elem = it->second.begin(); elem != it->second.end(); ++elem) {
        if (*elem == value) {
          it->second.erase(elem);
          ++removed;
          break;
        }
      }
      if (it->second.empty()) {
        lists_.erase(it);
      }
      return Int(removed);
    }
    if (cmd == "KEYS") {
      std::vector<std::string> keys;
      for (const auto& [k, v] : kv_) {
        if (GlobMatch(args[1], k)) {
          keys.push_back(k);
        }
      }
      for (const auto& [k, v] : lists_) {
        if (GlobMatch(args[1], k)) {
          keys.push_back(k);
        }
      }
      return Array(keys);
    }
    if (cmd == "PING") {
      return Simple("PONG");
    }
    if (cmd == "INCR") {
      auto& value = kv_[args[1]];
      value = std::to_string((value.empty() || !IsNumeric(value) ? 0 : std::stoll(value)) + 1);
      return Int(std::stoll(value));
    }
    if (cmd == "TTL") {
      return kv_.count(args[1]) || lists_.count(args[1]) ? Int(60) : Int(-2);
    }
    return Simple("OK");
  }

  // Direct (thread-unsafe) accessors for arranging/verifying state in tests.
  std::string GetDirect(const std::string& key) {
    std::lock_guard<std::mutex> lock(mu_);
    auto it = kv_.find(key);
    return it == kv_.end() ? std::string() : it->second;
  }

  void SetDirect(const std::string& key, const std::string& value) {
    std::lock_guard<std::mutex> lock(mu_);
    kv_[key] = value;
  }

  std::vector<std::string> ListDirect(const std::string& key) {
    std::lock_guard<std::mutex> lock(mu_);
    auto it = lists_.find(key);
    return it == lists_.end() ? std::vector<std::string>() : it->second;
  }

  void PushDirect(const std::string& key, const std::string& value) {
    std::lock_guard<std::mutex> lock(mu_);
    lists_[key].push_back(value);
  }

  void Clear() {
    std::lock_guard<std::mutex> lock(mu_);
    kv_.clear();
    lists_.clear();
  }

 private:
  static bool IsNumeric(const std::string& s) {
    if (s.empty()) return false;
    for (char c : s) {
      if (!std::isdigit(static_cast<unsigned char>(c))) return false;
    }
    return true;
  }

  static bool GlobMatch(const std::string& pattern, const std::string& key) {
    // Supports the '*' wildcard used by the production KEYS patterns.
    size_t p = 0;
    size_t k = 0;
    size_t star = std::string::npos;
    size_t mark = 0;
    while (k < key.size()) {
      if (p < pattern.size() && (pattern[p] == key[k])) {
        ++p;
        ++k;
      } else if (p < pattern.size() && pattern[p] == '*') {
        star = p++;
        mark = k;
      } else if (star != std::string::npos) {
        p = star + 1;
        k = ++mark;
      } else {
        return false;
      }
    }
    while (p < pattern.size() && pattern[p] == '*') {
      ++p;
    }
    return p == pattern.size();
  }

  std::mutex mu_;
  std::unordered_map<std::string, std::string> kv_;
  std::unordered_map<std::string, std::vector<std::string>> lists_;
};

}  // namespace chirp_test
