#pragma once

// Shared loopback fake chat for tests that drive the ChatBridge pipeline:
// a real in-process TCP server speaking framed chirp.gateway.Packet, with a
// scripted service-auth + login handshake. Own io thread, mirroring the
// other loopback test doubles (fake_servers.h, chat_hub_peer_test.cc).

#include <asio.hpp>

#include <array>
#include <cstdint>
#include <memory>
#include <mutex>
#include <string>
#include <thread>
#include <utility>
#include <vector>

#include "network/byte_order.h"
#include "network/protobuf_framing.h"
#include "proto/auth.pb.h"
#include "proto/common.pb.h"
#include "proto/gateway.pb.h"
#include "proto/game_server_gateway.pb.h"

namespace chirp_test {

using chirp::gateway::Packet;
using chirp::gateway::MsgID;

inline std::string FramePacket(const Packet& pkt) {
  const auto framed = chirp::network::ProtobufFraming::Encode(pkt);
  return std::string(reinterpret_cast<const char*>(framed.data()), framed.size());
}

inline Packet MakePacket(MsgID msg_id, int64_t seq,
                         const google::protobuf::Message& body) {
  Packet pkt;
  pkt.set_msg_id(msg_id);
  pkt.set_sequence(seq);
  pkt.set_body(body.SerializeAsString());
  return pkt;
}

inline Packet MakeRawPacket(MsgID msg_id, int64_t seq, const std::string& body) {
  Packet pkt;
  pkt.set_msg_id(msg_id);
  pkt.set_sequence(seq);
  pkt.set_body(body);
  return pkt;
}

inline Packet ParseFrame(const std::string& framed) {
  Packet pkt;
  if (framed.size() > 4u) {
    pkt.ParseFromArray(framed.data() + 4, static_cast<int>(framed.size() - 4));
  }
  return pkt;
}

// Minimal in-process chat: accepts the bridge's per-client connections,
// records every framed packet, and drives the handshake according to the
// scripted auth/login codes. hold_handshake() withholds the auth response so
// tests can queue packets before the relay is ready.
class FakeChatServer {
 public:
  FakeChatServer(chirp::common::ErrorCode auth_code, chirp::common::ErrorCode login_code)
      : auth_code_(auth_code),
        login_code_(login_code),
        acceptor_(io_, asio::ip::tcp::endpoint(asio::ip::tcp::v4(), 0)) {
    port_ = static_cast<uint16_t>(acceptor_.local_endpoint().port());
    DoAccept();
    thread_ = std::thread([this] { io_.run(); });
  }

  ~FakeChatServer() {
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

  void hold_handshake() {
    std::lock_guard<std::mutex> lock(mu_);
    hold_ = true;
  }
  void release_handshake() {
    {
      std::lock_guard<std::mutex> lock(mu_);
      hold_ = false;
    }
    asio::post(io_, [this] { DrainHeld(); });
  }

  size_t Count(MsgID msg_id) {
    std::lock_guard<std::mutex> lock(mu_);
    size_t n = 0;
    for (const auto& pkt : received_) {
      if (pkt.msg_id() == msg_id) {
        n++;
      }
    }
    return n;
  }

  std::vector<Packet> All(MsgID msg_id) {
    std::lock_guard<std::mutex> lock(mu_);
    std::vector<Packet> out;
    for (const auto& pkt : received_) {
      if (pkt.msg_id() == msg_id) {
        out.push_back(pkt);
      }
    }
    return out;
  }

  size_t EofCount() {
    std::lock_guard<std::mutex> lock(mu_);
    return eof_count_;
  }

  // Overrides the msg_id the fake answers the handshake with: the bridge must
  // treat a non-reply frame during auth/login as a broken chat and kick.
  void set_auth_reply_id(MsgID id) {
    std::lock_guard<std::mutex> lock(mu_);
    auth_reply_id_ = id;
  }
  void set_login_reply_id(MsgID id) {
    std::lock_guard<std::mutex> lock(mu_);
    login_reply_id_ = id;
  }

  // Writes arbitrary bytes (malformed frames) to the most recent connection.
  void SendRawToLatest(std::string bytes) {
    asio::post(io_, [this, bytes = std::move(bytes)] {
      std::lock_guard<std::mutex> lock(mu_);
      if (!sockets_.empty()) {
        asio::error_code ec;
        asio::write(*sockets_.back(), asio::buffer(bytes), ec);
      }
    });
  }

  // Pushes a framed packet to the most recent (live) connection.
  void SendToLatest(const Packet& pkt) {
    asio::post(io_, [this, pkt] {
      std::lock_guard<std::mutex> lock(mu_);
      if (!sockets_.empty()) {
        asio::error_code ec;
        asio::write(*sockets_.back(), asio::buffer(FramePacket(pkt)), ec);
      }
    });
  }

  void CloseLatest() {
    asio::post(io_, [this] {
      std::lock_guard<std::mutex> lock(mu_);
      if (!sockets_.empty()) {
        asio::error_code ec;
        sockets_.back()->close(ec);
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
      DoAccept();
      DoRead(sock, std::string());
    });
  }

  void DoRead(const std::shared_ptr<asio::ip::tcp::socket>& sock, std::string buf) {
    auto chunk = std::make_shared<std::array<char, 4096>>();
    sock->async_read_some(
        asio::buffer(*chunk),
        [this, sock, buf, chunk](const std::error_code& ec, std::size_t n) mutable {
          if (ec) {
            std::lock_guard<std::mutex> lock(mu_);
            eof_count_++;
            return;
          }
          buf.append(chunk->data(), n);
          for (;;) {
            if (buf.size() < 4) {
              break;
            }
            const uint32_t size =
                chirp::network::ReadU32BE(reinterpret_cast<const uint8_t*>(buf.data()));
            if (size > 64u * 1024u * 1024u || buf.size() < 4 + size) {
              break;
            }
            Packet pkt;
            if (pkt.ParseFromArray(buf.data() + 4, static_cast<int>(size))) {
              HandlePacket(pkt, sock);
            }
            buf.erase(0, 4 + size);
          }
          DoRead(sock, std::move(buf));
        });
  }

  void HandlePacket(const Packet& pkt, const std::shared_ptr<asio::ip::tcp::socket>& sock) {
    {
      std::lock_guard<std::mutex> lock(mu_);
      received_.push_back(pkt);
    }
    if (pkt.msg_id() == chirp::gateway::SERVER_AUTH_REQ) {
      chirp::game_server_gateway::ServerAuthRequest req;
      req.ParseFromString(pkt.body());
      chirp::game_server_gateway::ServerAuthResponse resp;
      resp.set_code(auth_code_);
      resp.set_server_time_ms(0);
      MsgID reply_id;
      {
        std::lock_guard<std::mutex> lock(mu_);
        auth_requests_.push_back(req);
        reply_id = auth_reply_id_;
      }
      if (hold_) {
        std::lock_guard<std::mutex> lock(mu_);
        held_.push_back({sock, MakePacket(reply_id, pkt.sequence(), resp)});
        return;
      }
      WriteTo(*sock, MakePacket(reply_id, pkt.sequence(), resp));
      if (auth_code_ != chirp::common::OK) {
        asio::error_code ec;
        sock->close(ec);
      }
    } else if (pkt.msg_id() == chirp::gateway::LOGIN_REQ) {
      chirp::auth::LoginRequest req;
      req.ParseFromString(pkt.body());
      chirp::auth::LoginResponse resp;
      resp.set_code(login_code_);
      resp.set_user_id(req.token());
      resp.set_server_time(0);
      MsgID reply_id;
      {
        std::lock_guard<std::mutex> lock(mu_);
        login_requests_.push_back(req);
        reply_id = login_reply_id_;
      }
      if (hold_) {
        std::lock_guard<std::mutex> lock(mu_);
        held_.push_back({sock, MakePacket(reply_id, pkt.sequence(), resp)});
        return;
      }
      WriteTo(*sock, MakePacket(reply_id, pkt.sequence(), resp));
    }
  }

  // Only ever called from the chat io thread, so writes never interleave.
  void WriteTo(asio::ip::tcp::socket& sock, const Packet& pkt) {
    asio::error_code ec;
    asio::write(sock, asio::buffer(FramePacket(pkt)), ec);
  }

  void DrainHeld() {
    std::vector<std::pair<std::shared_ptr<asio::ip::tcp::socket>, Packet>> held;
    {
      std::lock_guard<std::mutex> lock(mu_);
      held.swap(held_);
    }
    for (auto& [sock, pkt] : held) {
      WriteTo(*sock, pkt);
    }
  }

  chirp::common::ErrorCode auth_code_;
  chirp::common::ErrorCode login_code_;
  asio::io_context io_;
  asio::ip::tcp::acceptor acceptor_;
  uint16_t port_{0};
  std::thread thread_;
  mutable std::mutex mu_;
  bool hold_{false};
  std::vector<std::pair<std::shared_ptr<asio::ip::tcp::socket>, Packet>> held_;
  std::vector<std::shared_ptr<asio::ip::tcp::socket>> sockets_;
  std::vector<Packet> received_;
  std::vector<chirp::game_server_gateway::ServerAuthRequest> auth_requests_;
  std::vector<chirp::auth::LoginRequest> login_requests_;
  MsgID auth_reply_id_{chirp::gateway::SERVER_AUTH_RESP};
  MsgID login_reply_id_{chirp::gateway::LOGIN_RESP};
  size_t eof_count_{0};
};

}  // namespace chirp_test
