#pragma once

#include <optional>
#include <string>
#include <string_view>
#include <unordered_map>

namespace chirp::common {

class Config {
public:
  bool LoadFromFile(const std::string& path);

  std::optional<std::string> GetString(std::string_view key) const;
  std::string GetStringOr(std::string_view key, std::string default_value) const;

  std::optional<int> GetInt(std::string_view key) const;
  int GetIntOr(std::string_view key, int default_value) const;

private:
  std::unordered_map<std::string, std::string> kv_;
};

} // namespace chirp::common

