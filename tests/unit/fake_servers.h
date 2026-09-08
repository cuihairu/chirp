#pragma once

// Shared loopback test doubles for unit tests.

#include <asio.hpp>

#include <algorithm>
#include <array>
#include <atomic>
#include <functional>
#include <memory>
#include <mutex>
#include <thread>
#include <unordered_map>
#include <vector>

#include "network/redis_protocol.h"

namespace chirp_test {

// A minimal loopback Redis: parses RESP command arrays, routes them to a
// user-supplied handler, and can push pub/sub message arrays.
class FakeRedisServer {
 public:
  using CmdHandler =
      std::function<std::string(const std::vector<std::string>&)>;

  explicit FakeRedisServer(CmdHandler handler)
      : handler_(std::move(handler)),
        acceptor_(io_, asio::ip::tcp::endpoint(asio::ip::tcp::v4(), 0)) {
    port_ = static_cast<uint16_t>(acceptor_.local_endpoint().port());
    DoAccept();
    thread_ = std::thread([this] { io_.run(); });
  }

  ~FakeRedisServer() {
    asio::post(io_, [this] {
      asio::error_code ec;
      acceptor_.close(ec);
      std::lock_guard<std::mutex> lock(mu_);
      for (auto& s : sockets_) {
        s->close(ec);
      }
    });
    io_.stop();
    if (thread_.joinable()) {
      thread_.join();
    }
  }

  uint16_t port() const { return port_; }

  // Pushes a pub/sub message array on every open connection. Posted to the
  // server io thread so writes never interleave with command replies.
  void Publish(const std::string& channel, const std::string& payload) {
    std::string msg = "*3\r\n$7\r\nmessage\r\n$" + std::to_string(channel.size()) +
                      "\r\n" + channel + "\r\n$" + std::to_string(payload.size()) +
                      "\r\n" + payload + "\r\n";
    asio::post(io_, [this, msg = std::move(msg)] {
      std::lock_guard<std::mutex> lock(mu_);
      for (auto& s : sockets_) {
        asio::error_code ec;
        asio::write(*s, asio::buffer(msg), ec);
      }
    });
  }

 private:
  void DoAccept() {
    auto sock = std::make_shared<asio::ip::tcp::socket>(io_);
    acceptor_.async_accept(*sock, [this, sock](const std::error_code& ec) {
      if (ec) {
        return;
      }
      {
        std::lock_guard<std::mutex> lock(mu_);
        sockets_.push_back(sock);
      }
      DoRead(sock);
      DoAccept();
    });
  }

  void DoRead(std::shared_ptr<asio::ip::tcp::socket> sock) {
    auto buf = std::make_shared<std::array<uint8_t, 4096>>();
    sock->async_read_some(
        asio::buffer(*buf),
        [this, sock, buf](const std::error_code& ec, std::size_t n) {
          if (ec) {
            std::lock_guard<std::mutex> lock(mu_);
            sockets_.erase(std::remove(sockets_.begin(), sockets_.end(), sock),
                           sockets_.end());
            return;
          }
          parsers_[sock].Append(buf->data(), n);
          while (auto cmd = parsers_[sock].Pop()) {
            if (cmd->type == chirp::network::RedisResp::Type::kArray) {
              std::vector<std::string> args;
              for (const auto& a : cmd->array) {
                args.push_back(a.str);
              }
              std::string reply = handler_(args);
              if (!reply.empty()) {
                asio::error_code wec;
                asio::write(*sock, asio::buffer(reply), wec);
              }
            }
          }
          DoRead(sock);
        });
  }

  CmdHandler handler_;
  asio::io_context io_;
  asio::ip::tcp::acceptor acceptor_;
  uint16_t port_{0};
  std::thread thread_;
  std::mutex mu_;
  std::vector<std::shared_ptr<asio::ip::tcp::socket>> sockets_;
  std::unordered_map<std::shared_ptr<asio::ip::tcp::socket>,
                     chirp::network::RedisRespParser>
      parsers_;
};

// RESP reply helpers.
inline std::string Bulk(const std::string& s) {
  return "$" + std::to_string(s.size()) + "\r\n" + s + "\r\n";
}
inline std::string Int(int64_t v) { return ":" + std::to_string(v) + "\r\n"; }
inline std::string Simple(const std::string& s) { return "+" + s + "\r\n"; }
inline std::string Array(const std::vector<std::string>& items) {
  std::string out = "*" + std::to_string(items.size()) + "\r\n";
  for (const auto& i : items) {
    out += Bulk(i);
  }
  return out;
}

}  // namespace chirp_test
