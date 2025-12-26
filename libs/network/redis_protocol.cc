#include "network/redis_protocol.h"

#include <cstddef>
#include <cstdlib>

namespace chirp::network {

void RedisRespParser::Append(const uint8_t* data, size_t len) {
  buf_.append(reinterpret_cast<const char*>(data), len);
}

std::optional<std::pair<std::string_view, size_t>> RedisRespParser::ReadLine(size_t off) const {
  const size_t pos = buf_.find("\r\n", off);
  if (pos == std::string::npos) {
    return std::nullopt;
  }
  return std::make_pair(std::string_view(buf_).substr(off, pos - off), pos + 2);
}

std::optional<std::pair<RedisResp, size_t>> RedisRespParser::ParseAt(size_t off) const {
  if (off >= buf_.size()) {
    return std::nullopt;
  }

  const char t = buf_[off];
  if (t == '+' || t == '-' || t == ':') {
    auto line = ReadLine(off + 1);
    if (!line) {
      return std::nullopt;
    }
    RedisResp r;
    if (t == '+') {
      r.type = RedisResp::Type::kSimpleString;
      r.str.assign(line->first.data(), line->first.size());
    } else if (t == '-') {
      r.type = RedisResp::Type::kError;
      r.str.assign(line->first.data(), line->first.size());
    } else {
      r.type = RedisResp::Type::kInteger;
      r.integer = std::strtoll(std::string(line->first).c_str(), nullptr, 10);
    }
    return std::make_pair(std::move(r), line->second);
  }

  if (t == '$') {
    auto line = ReadLine(off + 1);
    if (!line) {
      return std::nullopt;
    }
    const int64_t n = std::strtoll(std::string(line->first).c_str(), nullptr, 10);
    if (n < 0) {
      RedisResp r;
      r.type = RedisResp::Type::kNull;
      return std::make_pair(std::move(r), line->second);
    }
    const size_t need = line->second + static_cast<size_t>(n) + 2;
    if (buf_.size() < need) {
      return std::nullopt;
    }
    RedisResp r;
    r.type = RedisResp::Type::kBulkString;
    r.str.assign(buf_.data() + line->second, static_cast<size_t>(n));
    return std::make_pair(std::move(r), need);
  }

  if (t == '*') {
    auto line = ReadLine(off + 1);
    if (!line) {
      return std::nullopt;
    }
    const int64_t n = std::strtoll(std::string(line->first).c_str(), nullptr, 10);
    if (n < 0) {
      RedisResp r;
      r.type = RedisResp::Type::kNull;
      return std::make_pair(std::move(r), line->second);
    }
    RedisResp r;
    r.type = RedisResp::Type::kArray;
    r.array.reserve(static_cast<size_t>(n));
    size_t cur = line->second;
    for (int64_t i = 0; i < n; ++i) {
      auto child = ParseAt(cur);
      if (!child) {
        return std::nullopt;
      }
      r.array.push_back(std::move(child->first));
      cur = child->second;
    }
    return std::make_pair(std::move(r), cur);
  }

  return std::nullopt;
}

std::optional<RedisResp> RedisRespParser::Pop() {
  auto parsed = ParseAt(0);
  if (!parsed) {
    return std::nullopt;
  }
  RedisResp out = std::move(parsed->first);
  buf_.erase(0, parsed->second);
  return out;
}

std::string BuildRedisCommand(const std::vector<std::string>& args) {
  std::string out;
  out.reserve(64);
  out += "*";
  out += std::to_string(args.size());
  out += "\r\n";
  for (const auto& a : args) {
    out += "$";
    out += std::to_string(a.size());
    out += "\r\n";
    out += a;
    out += "\r\n";
  }
  return out;
}

} // namespace chirp::network

