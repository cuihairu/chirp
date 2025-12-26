#include "network/redis_client.h"

#include <array>
#include <atomic>
#include <cstdint>
#include <memory>
#include <mutex>
#include <string>

#include <asio.hpp>

#include "network/redis_protocol.h"

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

RedisSubscriber::RedisSubscriber(std::string host, uint16_t port) : host_(std::move(host)), port_(port) {}

RedisSubscriber::~RedisSubscriber() { Stop(); }

void RedisSubscriber::Start(const std::string& channel, MessageCallback cb) {
  Stop();
  channel_ = channel;
  cb_ = std::move(cb);
  stop_.store(false);
  th_ = std::thread([this] { Run(); });
}

void RedisSubscriber::Stop() {
  stop_.store(true);
  {
    std::lock_guard<std::mutex> lock(sock_mu_);
    if (sock_holder_) {
      // Stored as void to avoid including asio in header; we know it's a tcp::socket.
      auto* sock = static_cast<asio::ip::tcp::socket*>(sock_holder_.get());
      asio::error_code ec;
      sock->close(ec);
      sock_holder_.reset();
    }
  }
  if (th_.joinable()) {
    th_.join();
  }
}

void RedisSubscriber::Run() {
  try {
    asio::io_context io;
    asio::ip::tcp::resolver resolver(io);
    auto sock = std::make_shared<asio::ip::tcp::socket>(io);
    {
      std::lock_guard<std::mutex> lock(sock_mu_);
      sock_holder_ = sock;
    }

    auto endpoints = resolver.resolve(host_, std::to_string(port_));
    asio::connect(*sock, endpoints);

    const std::string subscribe_cmd = BuildRedisCommand({"SUBSCRIBE", channel_});
    asio::write(*sock, asio::buffer(subscribe_cmd));

    RedisRespParser parser;
    std::array<uint8_t, 4096> buf{};
    while (!stop_.load()) {
      auto v = parser.Pop();
      if (!v) {
        asio::error_code ec;
        const size_t n = sock->read_some(asio::buffer(buf), ec);
        if (ec) {
          break;
        }
        parser.Append(buf.data(), n);
        continue;
      }

      if (v->type != RedisResp::Type::kArray || v->array.size() < 3) {
        continue;
      }
      const auto& kind = v->array[0];
      const auto& ch = v->array[1];
      const auto& payload = v->array[2];
      if ((kind.type == RedisResp::Type::kBulkString || kind.type == RedisResp::Type::kSimpleString) &&
          kind.str == "message") {
        if (cb_ && (ch.type == RedisResp::Type::kBulkString || ch.type == RedisResp::Type::kSimpleString) &&
            payload.type == RedisResp::Type::kBulkString) {
          cb_(ch.str, payload.str);
        }
      }
    }
  } catch (...) {
    // best-effort
  }
}

} // namespace chirp::network

