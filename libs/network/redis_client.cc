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

// One command, one reply, on the caller's thread. No timeout — same blocking
// posture the per-command short connections always had; the pool changes
// *where the round-trip cost goes*, not how long a hung peer can block.
std::optional<RedisResp> SendOne(asio::ip::tcp::socket& sock, const std::string& cmd) {
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

} // namespace

RedisClient::RedisClient(std::string host, uint16_t port, size_t max_idle)
    : host_(std::move(host)), port_(port), max_idle_(max_idle) {}

std::optional<RedisResp> RedisClient::Command(const std::vector<std::string>& args) {
  std::unique_ptr<Connection> conn = Checkout();
  if (!conn) {
    return std::nullopt;
  }
  std::optional<RedisResp> reply;
  // 传输异常（write 抛出）与读失败同型：reply 保持空 → 弃用该连接。
  try {
    reply = SendOne(conn->socket, BuildRedisCommand(args));
  } catch (...) {
  }
  if (!reply) {
    // 失败即弃，不重试：写类命令（RPUSH 等）盲重试可能二次生效，保持
    // 与短连接时代一致的至多一次语义。
    CloseConnection(std::move(conn));
    return std::nullopt;
  }
  Retire(std::move(conn));
  return reply;
}

std::unique_ptr<RedisClient::Connection> RedisClient::Connect() {
  try {
    auto conn = std::make_unique<Connection>();
    asio::ip::tcp::resolver resolver(conn->io);
    auto endpoints = resolver.resolve(host_, std::to_string(port_));
    asio::connect(conn->socket, endpoints);
    return conn;
  } catch (...) {
    return nullptr;
  }
}

bool RedisClient::IsAlive(Connection& conn) {
  asio::error_code ec;
  conn.socket.non_blocking(true, ec);
  size_t n = 0;
  uint8_t probe;
  asio::error_code read_ec;
  if (!ec) {
    n = conn.socket.read_some(asio::buffer(&probe, 1), read_ec);
  }
  asio::error_code clear_ec;
  conn.socket.non_blocking(false, clear_ec);
  if (read_ec == asio::error::would_block) {
    return n == 0; // 无待读数据且连接健在
  }
  return false; // EOF/RST、非阻塞切换失败，或对端多塞了字节（协议失步）——一律不复用
}

std::unique_ptr<RedisClient::Connection> RedisClient::Checkout() {
  for (;;) {
    std::unique_ptr<Connection> idle_conn;
    {
      std::lock_guard<std::mutex> lock(pool_mu_);
      if (idle_.empty()) {
        break;
      }
      idle_conn = std::move(idle_.back());
      idle_.pop_back();
    }
    if (IsAlive(*idle_conn)) {
      return idle_conn;
    }
    CloseConnection(std::move(idle_conn));
  }
  return Connect();
}

void RedisClient::Retire(std::unique_ptr<Connection> conn) {
  bool pooled = false;
  if (conn->socket.is_open()) {
    std::lock_guard<std::mutex> lock(pool_mu_);
    if (idle_.size() < max_idle_) {
      idle_.push_back(std::move(conn));
      pooled = true;
    }
  }
  if (!pooled) {
    CloseConnection(std::move(conn));
  }
}

void RedisClient::CloseConnection(std::unique_ptr<Connection> conn) {
  asio::error_code ec;
  conn->socket.close(ec);
}

std::optional<std::string> RedisClient::Get(const std::string& key) {
  auto r = Command({"GET", key});
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
  auto r = Command({"SET", key, value, "EX", std::to_string(ttl_seconds)});
  return r && r->type == RedisResp::Type::kSimpleString && r->str == "OK";
}

bool RedisClient::Set(const std::string& key, const std::string& value) {
  auto r = Command({"SET", key, value});
  return r && r->type == RedisResp::Type::kSimpleString && r->str == "OK";
}

bool RedisClient::Del(const std::string& key) {
  auto r = Command({"DEL", key});
  return r && r->type == RedisResp::Type::kInteger;
}

bool RedisClient::Publish(const std::string& channel, const std::string& message) {
  return PublishCount(channel, message) >= 0;
}

int64_t RedisClient::PublishCount(const std::string& channel, const std::string& message) {
  auto r = Command({"PUBLISH", channel, message});
  return (r && r->type == RedisResp::Type::kInteger) ? r->integer : -1;
}

bool RedisClient::RPush(const std::string& key, const std::string& value) {
  auto r = Command({"RPUSH", key, value});
  return r && r->type == RedisResp::Type::kInteger;
}

int64_t RedisClient::LRem(const std::string& key, int64_t count, const std::string& value) {
  auto r = Command({"LREM", key, std::to_string(count), value});
  return (r && r->type == RedisResp::Type::kInteger) ? r->integer : -1;
}

bool RedisClient::LSet(const std::string& key, int64_t index, const std::string& value) {
  auto r = Command({"LSET", key, std::to_string(index), value});
  return r && r->type == RedisResp::Type::kSimpleString && r->str == "OK";
}

bool RedisClient::Expire(const std::string& key, int ttl_seconds) {
  auto r = Command({"EXPIRE", key, std::to_string(ttl_seconds)});
  return r && r->type == RedisResp::Type::kInteger && r->integer > 0;
}

std::vector<std::string> RedisClient::LRange(const std::string& key, int64_t start, int64_t stop) {
  std::vector<std::string> out;
  auto r = Command({"LRANGE", key, std::to_string(start), std::to_string(stop)});
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
  auto r = Command({"KEYS", pattern});
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

      // A new RESP array header resets pending accumulation. Without this,
      // leftover bulk items from a previous reply (e.g. the SUBSCRIBE ack
      // array) would shift the frame alignment and drop subsequent
      // ["message", channel, payload] pushes.
      if (line.empty()) {
        continue;
      }
      if (line[0] == '*') {
        current_array_.clear();
        continue;
      }

      if (line[0] != '$') {
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
          pending_messages_.push_back({
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
      // shutdown() only, and under sock_mu_ so it cannot race the reader
      // thread's connect: it wakes a blocking read_some on Linux (a plain
      // close() does not), letting Run() exit and the join() below finish.
      socket_->shutdown(asio::ip::tcp::socket::shutdown_both, ec);
    }
  }
  if (io_) {
    io_->stop();
  }
  if (th_.joinable()) {
    th_.join();
  }
  // Only close after join(): the reader thread is gone, so closing here can
  // never race an in-flight read or reconnect, and the fd cannot be reused
  // under a still-blocked syscall.
  std::lock_guard<std::mutex> lock(sock_mu_);
  if (socket_ && socket_->is_open()) {
    asio::error_code ec;
    socket_->close(ec);
  }
}

void RedisSubscriber::Run() {
  using chirp::common::Logger;

  try {
    asio::ip::tcp::resolver resolver(*io_);
    auto endpoints = resolver.resolve(host_, std::to_string(port_));
    {
      // The open-state transition must hold sock_mu_ or it races Stop()'s
      // shutdown/close on the same implementation. Trade-off: a hung connect
      // delays Stop() until the kernel gives up; the read loop below stays
      // unlocked so Stop() can still wake it instantly via shutdown().
      std::lock_guard<std::mutex> lock(sock_mu_);
      if (stop_.load()) {
        return;
      }
      asio::connect(*socket_, endpoints);
    }

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
