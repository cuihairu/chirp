// Loopback tests for the gateway -> chat bridge: a real in-process TCP chat
// (framed chirp.gateway.Packet on the wire) drives the service-auth +
// login-replay handshake, the verbatim relay, the pre-ready queue, and every
// kick path. All chat-side socket work runs on its own io thread, mirroring
// chat_hub_peer_test.cc.
#include <gtest/gtest.h>

#include <asio.hpp>

#include <array>
#include <chrono>
#include <memory>
#include <mutex>
#include <string>
#include <thread>
#include <vector>

#include "chat_bridge.h"
#include "logger.h"
#include "network/byte_order.h"
#include "network/protobuf_framing.h"
#include "proto/auth.pb.h"
#include "proto/chat.pb.h"
#include "proto/common.pb.h"
#include "proto/gateway.pb.h"
#include "proto/server_gateway.pb.h"

namespace {

using chirp::gateway::Packet;
using chirp::gateway::MsgID;

std::string FramePacket(const Packet& pkt) {
  const auto framed = chirp::network::ProtobufFraming::Encode(pkt);
  return std::string(reinterpret_cast<const char*>(framed.data()), framed.size());
}

Packet MakePacket(MsgID msg_id, int64_t seq, const google::protobuf::Message& body) {
  Packet pkt;
  pkt.set_msg_id(msg_id);
  pkt.set_sequence(seq);
  pkt.set_body(body.SerializeAsString());
  return pkt;
}

Packet MakeRawPacket(MsgID msg_id, int64_t seq, const std::string& body) {
  Packet pkt;
  pkt.set_msg_id(msg_id);
  pkt.set_sequence(seq);
  pkt.set_body(body);
  return pkt;
}

Packet ParseFrame(const std::string& framed) {
  Packet pkt;
  if (framed.size() > 4u) {
    pkt.ParseFromArray(framed.data() + 4, static_cast<int>(framed.size() - 4));
  }
  return pkt;
}

// Pure in-memory Session mock standing in for the real client edge: records
// every frame the bridge pushes and the close transitions. Mutex-guarded so
// the bridge io thread and the test thread can share it safely.
class MockClientSession : public chirp::network::Session {
 public:
  void Send(std::string bytes) override {
    std::lock_guard<std::mutex> lock(mu_);
    sent.push_back(std::move(bytes));
  }
  void SendAndClose(std::string bytes) override {
    std::lock_guard<std::mutex> lock(mu_);
    sent.push_back(std::move(bytes));
    close_after_send = true;
  }
  void Close() override {
    std::lock_guard<std::mutex> lock(mu_);
    closed = true;
  }
  bool IsClosed() const override {
    std::lock_guard<std::mutex> lock(mu_);
    return closed;
  }
  std::string RemoteAddress() const override { return "127.0.0.1"; }

  size_t SentCount() const {
    std::lock_guard<std::mutex> lock(mu_);
    return sent.size();
  }
  std::vector<Packet> SentPackets() const {
    std::lock_guard<std::mutex> lock(mu_);
    std::vector<Packet> out;
    for (const auto& frame : sent) {
      out.push_back(ParseFrame(frame));
    }
    return out;
  }
  bool CloseAfterSend() const {
    std::lock_guard<std::mutex> lock(mu_);
    return close_after_send;
  }

 private:
  mutable std::mutex mu_;
  std::vector<std::string> sent;
  bool closed = false;
  bool close_after_send = false;
};

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
      chirp::server_gateway::ServerAuthRequest req;
      req.ParseFromString(pkt.body());
      chirp::server_gateway::ServerAuthResponse resp;
      resp.set_code(auth_code_);
      resp.set_server_time_ms(0);
      std::lock_guard<std::mutex> lock(mu_);
      auth_requests_.push_back(req);
      if (hold_) {
        held_.push_back({sock, MakePacket(chirp::gateway::SERVER_AUTH_RESP, pkt.sequence(), resp)});
        return;
      }
      WriteTo(*sock, MakePacket(chirp::gateway::SERVER_AUTH_RESP, pkt.sequence(), resp));
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
      std::lock_guard<std::mutex> lock(mu_);
      login_requests_.push_back(req);
      if (hold_) {
        held_.push_back({sock, MakePacket(chirp::gateway::LOGIN_RESP, pkt.sequence(), resp)});
        return;
      }
      WriteTo(*sock, MakePacket(chirp::gateway::LOGIN_RESP, pkt.sequence(), resp));
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
  std::vector<chirp::server_gateway::ServerAuthRequest> auth_requests_;
  std::vector<chirp::auth::LoginRequest> login_requests_;
  size_t eof_count_{0};
};

template <typename Pred>
bool WaitFor(Pred pred, std::chrono::milliseconds timeout) {
  const auto deadline = std::chrono::steady_clock::now() + timeout;
  while (std::chrono::steady_clock::now() < deadline) {
    if (pred()) {
      return true;
    }
    std::this_thread::sleep_for(std::chrono::milliseconds(10));
  }
  return pred();
}

// Runs the bridge's io_context on a helper thread with a hard watchdog; the
// watchdog stops the loop so a hung bridge fails asserts instead of the test.
class BridgeIoRunner {
 public:
  explicit BridgeIoRunner(asio::io_context& io) : io_(io) {
    watchdog_ = std::make_shared<asio::steady_timer>(io);
    watchdog_->expires_after(std::chrono::seconds(30));
    watchdog_->async_wait([&](const std::error_code&) { io_.stop(); });
    thread_ = std::thread([this] { io_.run(); });
  }

  ~BridgeIoRunner() {
    watchdog_->cancel();
    io_.stop();
    if (thread_.joinable()) {
      thread_.join();
    }
  }

 private:
  asio::io_context& io_;
  std::shared_ptr<asio::steady_timer> watchdog_;
  std::thread thread_;
};

chirp::auth::KickNotify AsKick(const Packet& pkt) {
  chirp::auth::KickNotify kick;
  kick.ParseFromString(pkt.body());
  return kick;
}

} // namespace

TEST(ChatBridgeTest, AttachAuthenticatesThenLoginWithTokenAndDevice) {
  FakeChatServer chat(chirp::common::OK, chirp::common::OK);
  asio::io_context io;
  chirp::gateway::ChatBridge bridge(io, "127.0.0.1", chat.port(), "edge-1", "s3cret");
  BridgeIoRunner runner(io);

  auto client = std::make_shared<MockClientSession>();
  // All bridge entry points are invoked through the bridge's io_context,
  // mirroring the gateway: its packet handlers and async completions all run
  // on the single main-io thread, which is the class's threading contract.
  asio::post(io, [&] { bridge.Attach(client, "user_1-token", "dev_9"); });

  ASSERT_TRUE(WaitFor([&] { return chat.Count(chirp::gateway::SERVER_AUTH_REQ) > 0; },
                      std::chrono::seconds(5)));
  const auto auths = chat.All(chirp::gateway::SERVER_AUTH_REQ);
  ASSERT_FALSE(auths.empty());
  chirp::server_gateway::ServerAuthRequest auth_req;
  ASSERT_TRUE(auth_req.ParseFromString(auths.front().body()));
  EXPECT_EQ(auth_req.service_id(), "edge-1");
  EXPECT_EQ(auth_req.secret(), "s3cret");

  ASSERT_TRUE(WaitFor([&] { return chat.Count(chirp::gateway::LOGIN_REQ) > 0; },
                      std::chrono::seconds(5)));
  const auto logins = chat.All(chirp::gateway::LOGIN_REQ);
  ASSERT_FALSE(logins.empty());
  chirp::auth::LoginRequest login_req;
  ASSERT_TRUE(login_req.ParseFromString(logins.front().body()));
  EXPECT_EQ(login_req.token(), "user_1-token");
  EXPECT_EQ(login_req.device_id(), "dev_9");

  // Once ready, a forwarded 2xxx packet reaches chat with its identity
  // intact (msg_id, sequence, body - the bridge never rewrites them).
  Packet fwd = MakeRawPacket(chirp::gateway::SEND_MESSAGE_REQ, 42, "body-bytes");
  asio::post(io, [&] { bridge.ForwardToChat(client.get(), fwd); });
  ASSERT_TRUE(WaitFor([&] { return chat.Count(chirp::gateway::SEND_MESSAGE_REQ) > 0; },
                      std::chrono::seconds(5)));
  const auto sent = chat.All(chirp::gateway::SEND_MESSAGE_REQ);
  EXPECT_EQ(sent.back().sequence(), 42);
  EXPECT_EQ(sent.back().body(), "body-bytes");
}

TEST(ChatBridgeTest, AuthFailureKicksClient) {
  FakeChatServer chat(chirp::common::AUTH_FAILED, chirp::common::OK);
  asio::io_context io;
  chirp::gateway::ChatBridge bridge(io, "127.0.0.1", chat.port(), "gateway", "");
  BridgeIoRunner runner(io);

  auto client = std::make_shared<MockClientSession>();
  asio::post(io, [&] { bridge.Attach(client, "tok", "dev"); });

  ASSERT_TRUE(WaitFor([&] { return client->SentCount() > 0; }, std::chrono::seconds(5)));
  const auto frames = client->SentPackets();
  ASSERT_FALSE(frames.empty());
  EXPECT_EQ(frames.back().msg_id(), chirp::gateway::KICK_NOTIFY);
  EXPECT_EQ(AsKick(frames.back()).reason(), "chat unavailable");
  EXPECT_TRUE(client->CloseAfterSend());
}

TEST(ChatBridgeTest, LoginRejectedKicksClient) {
  FakeChatServer chat(chirp::common::OK, chirp::common::AUTH_FAILED);
  asio::io_context io;
  chirp::gateway::ChatBridge bridge(io, "127.0.0.1", chat.port(), "gateway", "");
  BridgeIoRunner runner(io);

  auto client = std::make_shared<MockClientSession>();
  asio::post(io, [&] { bridge.Attach(client, "bad-token", "dev"); });

  ASSERT_TRUE(WaitFor([&] { return client->SentCount() > 0; }, std::chrono::seconds(5)));
  const auto frames = client->SentPackets();
  ASSERT_FALSE(frames.empty());
  EXPECT_EQ(frames.back().msg_id(), chirp::gateway::KICK_NOTIFY);
  EXPECT_EQ(AsKick(frames.back()).reason(), "chat session rejected");
  EXPECT_TRUE(client->CloseAfterSend());
}

TEST(ChatBridgeTest, InternalDisconnectKicksClient) {
  FakeChatServer chat(chirp::common::OK, chirp::common::OK);
  asio::io_context io;
  chirp::gateway::ChatBridge bridge(io, "127.0.0.1", chat.port(), "gateway", "");
  BridgeIoRunner runner(io);

  auto client = std::make_shared<MockClientSession>();
  asio::post(io, [&] { bridge.Attach(client, "tok", "dev"); });
  // Ready: the handshake ends with chat answering the login replay.
  ASSERT_TRUE(WaitFor([&] { return chat.Count(chirp::gateway::LOGIN_REQ) > 0; },
                      std::chrono::seconds(5)));

  chat.CloseLatest();
  ASSERT_TRUE(WaitFor([&] { return client->SentCount() > 0; }, std::chrono::seconds(5)));
  const auto frames = client->SentPackets();
  ASSERT_FALSE(frames.empty());
  EXPECT_EQ(frames.back().msg_id(), chirp::gateway::KICK_NOTIFY);
  EXPECT_EQ(AsKick(frames.back()).reason(), "chat session lost");
  EXPECT_TRUE(client->CloseAfterSend());
}

TEST(ChatBridgeTest, DetachClosesInternalConnection) {
  FakeChatServer chat(chirp::common::OK, chirp::common::OK);
  asio::io_context io;
  chirp::gateway::ChatBridge bridge(io, "127.0.0.1", chat.port(), "gateway", "");
  BridgeIoRunner runner(io);

  auto client = std::make_shared<MockClientSession>();
  asio::post(io, [&] { bridge.Attach(client, "tok", "dev"); });
  ASSERT_TRUE(WaitFor([&] { return chat.Count(chirp::gateway::LOGIN_REQ) > 0; },
                      std::chrono::seconds(5)));

  // Detach closes the internal side; chat observes the EOF and the client
  // is NOT kicked.
  asio::post(io, [&] { bridge.Detach(client.get()); });
  EXPECT_TRUE(WaitFor([&] { return chat.EofCount() > 0; }, std::chrono::seconds(5)));
  EXPECT_EQ(client->SentCount(), 0u);

  // A fresh Attach dials a new connection and re-runs the handshake.
  asio::post(io, [&] { bridge.Attach(client, "tok", "dev"); });
  EXPECT_TRUE(WaitFor([&] { return chat.Count(chirp::gateway::SERVER_AUTH_REQ) >= 2; },
                      std::chrono::seconds(5)));
}

TEST(ChatBridgeTest, QueuedPacketsFlushOnReadyAndOverflowDrops) {
  FakeChatServer chat(chirp::common::OK, chirp::common::OK);
  asio::io_context io;
  chirp::gateway::ChatBridge bridge(io, "127.0.0.1", chat.port(), "gateway", "");
  BridgeIoRunner runner(io);

  auto client = std::make_shared<MockClientSession>();
  asio::post(io, [&] { bridge.Attach(client, "tok", "dev"); });

  // Withhold the handshake and stuff more packets than the pending queue
  // holds: the excess is dropped (with a warning), nothing crashes.
  chat.hold_handshake();
  ASSERT_TRUE(WaitFor([&] { return chat.Count(chirp::gateway::SERVER_AUTH_REQ) > 0; },
                      std::chrono::seconds(5)));
  const int kOverflowTotal = 70;
  asio::post(io, [&] {
    for (int i = 0; i < kOverflowTotal; i++) {
      bridge.ForwardToChat(client.get(),
                           MakeRawPacket(chirp::gateway::SEND_MESSAGE_REQ, i, "m"));
    }
  });

  chat.release_handshake();
  // The bridge completes auth + login, turns ready, and flushes the bounded
  // queue in order.
  ASSERT_TRUE(WaitFor([&] { return chat.Count(chirp::gateway::SEND_MESSAGE_REQ) >= 64; },
                      std::chrono::seconds(5)));
  const auto sent = chat.All(chirp::gateway::SEND_MESSAGE_REQ);
  EXPECT_EQ(sent.size(), 64u);
  for (size_t i = 0; i < sent.size(); i++) {
    EXPECT_EQ(sent[i].sequence(), static_cast<int64_t>(i));
  }
}

TEST(ChatBridgeTest, InternalFramesForwardedToClientExceptHandshake) {
  FakeChatServer chat(chirp::common::OK, chirp::common::OK);
  asio::io_context io;
  chirp::gateway::ChatBridge bridge(io, "127.0.0.1", chat.port(), "gateway", "");
  BridgeIoRunner runner(io);

  auto client = std::make_shared<MockClientSession>();
  asio::post(io, [&] { bridge.Attach(client, "tok", "dev"); });
  // Prove readiness by round-tripping one business packet.
  asio::post(io, [&] {
    bridge.ForwardToChat(client.get(),
                         MakeRawPacket(chirp::gateway::SEND_MESSAGE_REQ, 1, "m"));
  });
  ASSERT_TRUE(WaitFor([&] { return chat.Count(chirp::gateway::SEND_MESSAGE_REQ) > 0; },
                      std::chrono::seconds(5)));

  // A chat push for this user rides the same pipe back to the one client.
  chirp::chat::ChatMessage msg;
  msg.set_sender_id("user_2");
  msg.set_content("hello");
  chat.SendToLatest(MakePacket(chirp::gateway::CHAT_MESSAGE_NOTIFY, 0, msg));
  ASSERT_TRUE(WaitFor([&] { return client->SentCount() > 0; }, std::chrono::seconds(5)));

  const auto frames = client->SentPackets();
  ASSERT_FALSE(frames.empty());
  bool saw_notify = false;
  for (const auto& f : frames) {
    // Handshake replies are consumed by the bridge, never relayed.
    EXPECT_NE(f.msg_id(), chirp::gateway::SERVER_AUTH_RESP);
    EXPECT_NE(f.msg_id(), chirp::gateway::LOGIN_RESP);
    if (f.msg_id() == chirp::gateway::CHAT_MESSAGE_NOTIFY) {
      chirp::chat::ChatMessage got;
      ASSERT_TRUE(got.ParseFromString(f.body()));
      EXPECT_EQ(got.sender_id(), "user_2");
      EXPECT_EQ(got.content(), "hello");
      saw_notify = true;
    }
  }
  EXPECT_TRUE(saw_notify);
}

TEST(ChatBridgeTest, DisabledBridgeIsNoOp) {
  asio::io_context io;
  chirp::gateway::ChatBridge bridge(io, "", 0, "gateway", "");
  BridgeIoRunner runner(io);
  EXPECT_FALSE(bridge.enabled());

  auto client = std::make_shared<MockClientSession>();
  asio::post(io, [&] { bridge.Attach(client, "tok", "dev"); });
  asio::post(io, [&] {
    bridge.ForwardToChat(client.get(),
                         MakeRawPacket(chirp::gateway::SEND_MESSAGE_REQ, 1, "m"));
  });
  asio::post(io, [&] { bridge.Detach(client.get()); });
  // Nothing dialed, nothing sent: the historical silent-drop behavior.
  std::this_thread::sleep_for(std::chrono::milliseconds(50));
  EXPECT_EQ(client->SentCount(), 0u);
}
