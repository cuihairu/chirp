#include "common/config.h"

#include <cerrno>
#include <charconv>
#include <fstream>
#include <string>

namespace chirp::common {

static inline void TrimInPlace(std::string& s) {
  while (!s.empty() && (s.back() == ' ' || s.back() == '\t' || s.back() == '\r' || s.back() == '\n')) {
    s.pop_back();
  }
  size_t i = 0;
  while (i < s.size() && (s[i] == ' ' || s[i] == '\t')) {
    ++i;
  }
  if (i > 0) {
    s.erase(0, i);
  }
}

bool Config::LoadFromFile(const std::string& path) {
  std::ifstream in(path);
  if (!in.is_open()) {
    return false;
  }

  std::string line;
  while (std::getline(in, line)) {
    TrimInPlace(line);
    if (line.empty() || line[0] == '#') {
      continue;
    }
    auto eq = line.find('=');
    if (eq == std::string::npos) {
      continue;
    }
    std::string key = line.substr(0, eq);
    std::string val = line.substr(eq + 1);
    TrimInPlace(key);
    TrimInPlace(val);
    if (!key.empty()) {
      kv_[std::move(key)] = std::move(val);
    }
  }
  return true;
}

std::optional<std::string> Config::GetString(std::string_view key) const {
  auto it = kv_.find(std::string(key));
  if (it == kv_.end()) {
    return std::nullopt;
  }
  return it->second;
}

std::string Config::GetStringOr(std::string_view key, std::string default_value) const {
  auto v = GetString(key);
  return v ? *v : std::move(default_value);
}

std::optional<int> Config::GetInt(std::string_view key) const {
  auto v = GetString(key);
  if (!v) {
    return std::nullopt;
  }
  int out = 0;
  auto sv = std::string_view(*v);
  auto res = std::from_chars(sv.data(), sv.data() + sv.size(), out);
  if (res.ec != std::errc() || res.ptr != sv.data() + sv.size()) {
    return std::nullopt;
  }
  return out;
}

int Config::GetIntOr(std::string_view key, int default_value) const {
  auto v = GetInt(key);
  return v ? *v : default_value;
}

} // namespace chirp::common

