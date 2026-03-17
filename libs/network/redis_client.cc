#include "network/redis_client.h"

#include <array>
#include <atomic>
#include <cstdint>
#include <memory>
#include <mutex>
#include <string>

#include <asio.hpp>

#include "network/redis_protocol.h"
#include "common/logger.h"

namespace chirp::network {
namespace {

std::optional<RedisResp> SendAndReadOne(asio::ip::tcp::socket& sock, const std::string& cmd) {
  asio::write(sock, asio::buffer(cmd));

  RedisRespParser parser;
  std::array<uint8_t, 4096> buf{};
  while (true) {
    auto v = parser.Pop();
    if (v) {
      return v;
    }
    asio::error_code ec;
    const size_t n = sock.read_some(asio::buffer(buf), ec);
    if (ec) {
      return std::nullopt;
    }
    parser.Append(buf.data(), n);
  }
}

std::optional<RedisResp> SendCmd(const std::string& host, uint16_t port, const std::vector<std::string>& args) {
  asio::io_context io;
  asio::ip::tcp::resolver resolver(io);
  asio::ip::tcp::socket sock(io);
  auto endpoints = resolver.resolve(host, std::to_string(port));
  asio::connect(sock, endpoints);
  const std::string cmd = BuildRedisCommand(args);
  return SendAndReadOne(sock, cmd);
}

} // namespace

RedisClient::RedisClient(std::string host, uint16_t port) : host_(std::move(host)), port_(port) {}

std::optional<std::string> RedisClient::Get(const std::string& key) {
  auto r = SendCmd(host_, port_, {"GET", key});
  if (!r) {
    return std::nullopt;
  }
  if (r->type == RedisResp::Type::kBulkString) {
    return r->str;
  }
  if (r->type == RedisResp::Type::kNull) {
    return std::nullopt;
  }
  return std::nullopt;
}

bool RedisClient::SetEx(const std::string& key, const std::string& value, int ttl_seconds) {
  auto r = SendCmd(host_, port_, {"SET", key, value, "EX", std::to_string(ttl_seconds)});
  return r && r->type == RedisResp::Type::kSimpleString && r->str == "OK";
}

bool RedisClient::Del(const std::string& key) {
  auto r = SendCmd(host_, port_, {"DEL", key});
  return r && r->type == RedisResp::Type::kInteger;
}

bool RedisClient::Publish(const std::string& channel, const std::string& message) {
  auto r = SendCmd(host_, port_, {"PUBLISH", channel, message});
  return r && r->type == RedisResp::Type::kInteger;
}

bool RedisClient::RPush(const std::string& key, const std::string& value) {
  auto r = SendCmd(host_, port_, {"RPUSH", key, value});
  return r && r->type == RedisResp::Type::kInteger;
}

bool RedisClient::Expire(const std::string& key, int ttl_seconds) {
  auto r = SendCmd(host_, port_, {"EXPIRE", key, std::to_string(ttl_seconds)});
  return r && r->type == RedisResp::Type::kInteger && r->integer > 0;
}

std::vector<std::string> RedisClient::LRange(const std::string& key, int64_t start, int64_t stop) {
  std::vector<std::string> out;
  auto r = SendCmd(host_, port_, {"LRANGE", key, std::to_string(start), std::to_string(stop)});
  if (!r || r->type != RedisResp::Type::kArray) {
    return out;
  }
  out.reserve(r->array.size());
  for (const auto& e : r->array) {
    if (e.type == RedisResp::Type::kBulkString || e.type == RedisResp::Type::kSimpleString) {
      out.push_back(e.str);
    }
  }
  return out;
}

std::vector<std::string> RedisClient::Keys(const std::string& pattern) {
  std::vector<std::string> out;
  auto r = SendCmd(host_, port_, {"KEYS", pattern});
  if (!r || r->type != RedisResp::Type::kArray) {
    return out;
  }
  out.reserve(r->array.size());
  for (const auto& e : r->array) {
    if (e.type == RedisResp::Type::kBulkString || e.type == RedisResp::Type::kSimpleString) {
      out.push_back(e.str);
    }
  }
  return out;
}

// ============================================================================
// RedisSubscriber - Enhanced version with multi-channel support
// ============================================================================

namespace {

class SubscribeParser {
public:
  void Append(const char* data, size_t size) {
    buffer_.append(data, size);
    Process();
  }

  void Process() {
    while (!buffer_.empty()) {
      // Find \r\n
      size_t pos = buffer_.find("\r\n");
      if (pos == std::string::npos) {
        return;  // Incomplete line
      }

      std::string line = buffer_.substr(0, pos);
      buffer_.erase(0, pos + 2);

      if (line.empty() || line[0] != '$') {
        continue;
      }

      // Parse bulk string size: $<size>\r\n<data>\r\n
      size_t size;
      try {
        size = std::stoul(line.substr(1));
      } catch (...) {
        continue;
      }

      if (size == static_cast<size_t>(-1)) {
        // Null bulk string
        current_array_.emplace_back();
        current_array_.back().type = RedisResp::Type::kNull;
        continue;
      }

      // Read the data
      if (buffer_.size() < size + 2) {
        return;  // Incomplete data
      }

      std::string data = buffer_.substr(0, size);
      buffer_.erase(0, size + 2);  // +2 for \r\n

      RedisResp resp;
      resp.type = RedisResp::Type::kBulkString;
      resp.str = data;
      current_array_.push_back(resp);

      // Subscribe messages come in arrays of 3: ["message", "channel", "payload"]
      if (current_array_.size() == 3) {
        // We have a complete subscribe message
        if (current_array_[0].type == RedisResp::Type::kBulkString &&
            current_array_[0].str == "message") {
          // Valid subscribe message
          pending_messages_.push({
            current_array_[1].str,  // channel
            current_array_[2].str   // payload
          });
        }
        current_array_.clear();
      }
    }
  }

  std::vector<std::pair<std::string, std::string>> PopMessages() {
    std::vector<std::pair<std::string, std::string>> result;
    result.swap(pending_messages_);
    return result;
  }

private:
  std::string buffer_;
  std::vector<RedisResp> current_array_;
  std::vector<std::pair<std::string, std::string>> pending_messages_;
};

}  // namespace

RedisSubscriber::RedisSubscriber(std::string host, uint16_t port)
    : host_(std::move(host)),
      port_(port),
      io_(std::make_unique<asio::io_context>()),
      socket_(std::make_unique<asio::ip::tcp::socket>(*io_)) {
  socket_ptr_ = socket_.get();
}

RedisSubscriber::~RedisSubscriber() {
  Stop();
}

bool RedisSubscriber::Subscribe(const std::string& channel) {
  std::string cmd = BuildRedisCommand({"SUBSCRIBE", channel});
  return SendCommand(cmd);
}

bool RedisSubscriber::Unsubscribe(const std::string& channel) {
  std::string cmd = BuildRedisCommand({"UNSUBSCRIBE", channel});
  return SendCommand(cmd);
}

bool RedisSubscriber::SendCommand(const std::string& cmd) {
  std::lock_guard<std::mutex> lock(sock_mu_);
  if (!socket_ || !socket_->is_open()) {
    return false;
  }
  try {
    asio::write(*socket_, asio::buffer(cmd));
    return true;
  } catch (const std::exception& e) {
    if (error_cb_) {
      error_cb_(std::string("SendCommand failed: ") + e.what());
    }
    return false;
  }
}

void RedisSubscriber::Start() {
  Stop();
  stop_.store(false);
  connected_.store(false);
  th_ = std::thread([this] { Run(); });
}

void RedisSubscriber::Stop() {
  stop_.store(true);
  {
    std::lock_guard<std::mutex> lock(sock_mu_);
    if (socket_ && socket_->is_open()) {
      asio::error_code ec;
      socket_->close(ec);
    }
  }
  if (io_) {
    io_->stop();
  }
  if (th_.joinable()) {
    th_.join();
  }
}

void RedisSubscriber::Run() {
  using chirp::common::Logger;

  try {
    asio::ip::tcp::resolver resolver(*io_);
    auto endpoints = resolver.resolve(host_, std::to_string(port_));
    asio::connect(*socket_, endpoints);

    connected_.store(true);
    if (connect_cb_) {
      connect_cb_();
    }

    SubscribeParser parser;
    std::array<uint8_t, 8192> buf{};

    while (!stop_.load()) {
      asio::error_code ec;
      const size_t n = socket_->read_some(asio::buffer(buf), ec);

      if (ec) {
        if (error_cb_ && ec != asio::error::eof) {
          error_cb_("Read error: " + ec.message());
        }
        break;
      }

      parser.Append(reinterpret_cast<const char*>(buf.data()), n);

      // Process all pending messages
      auto messages = parser.PopMessages();
      for (const auto& [channel, payload] : messages) {
        if (msg_cb_) {
          msg_cb_(channel, payload);
        }
      }
    }
  } catch (const std::exception& e) {
    if (error_cb_) {
      error_cb_(std::string("Subscriber exception: ") + e.what());
    }
  }

  connected_.store(false);
}

} // namespace chirp::network
