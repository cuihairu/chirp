#pragma once

#include <cstddef>
#include <cstdint>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

namespace chirp::network {

struct RedisResp {
  enum class Type { kSimpleString, kError, kInteger, kBulkString, kArray, kNull };
  Type type{Type::kNull};
  std::string str;
  int64_t integer{0};
  std::vector<RedisResp> array;
};

class RedisRespParser {
public:
  void Append(const uint8_t* data, size_t len);
  std::optional<RedisResp> Pop();
  void Clear() { buf_.clear(); }

private:
  std::optional<std::pair<RedisResp, size_t>> ParseAt(size_t off) const;
  std::optional<std::pair<std::string_view, size_t>> ReadLine(size_t off) const;

  std::string buf_;
};

std::string BuildRedisCommand(const std::vector<std::string>& args);

} // namespace chirp::network
