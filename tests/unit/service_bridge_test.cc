// Loopback tests for the gateway service bridge: a real in-process TCP fake
// backend (framed chirp.gateway.Packet on the wire) drives the service-auth +
// login-replay handshake, the verbatim relay, the pre-ready queue, every kick
// path (chat instance), and the degrade semantics (search instance: 2248 ->
// SERVER_UNAVAILABLE without touching the client, auto re-dial on the next
// query). All backend-side socket work runs on its own io thread, mirroring
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

#include "network/service_bridge.h"
#include "fake_chat_server.h"
#include "logger.h"
#include "proto/auth.pb.h"
#include "proto/chat.pb.h"
#include "proto/common.pb.h"
#include "proto/gateway.pb.h"
#include "proto/game_server_gateway.pb.h"

namespace {

using chirp::gateway::Packet;
using chirp::gateway::MsgID;
using chirp_test::FakeChatServer;
using chirp_test::FramePacket;
using chirp_test::MakePacket;
using chirp_test::MakeRawPacket;
using chirp_test::ParseFrame;

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

TEST(ServiceBridgeTest, AttachAuthenticatesThenLoginWithTokenAndDevice) {
  FakeChatServer chat(chirp::common::OK, chirp::common::OK);
  asio::io_context io;
  chirp::gateway::ServiceBridge bridge(io, "127.0.0.1", chat.port(), "edge-1", "s3cret");
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
  chirp::game_server_gateway::ServerAuthRequest auth_req;
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
  asio::post(io, [&] { bridge.ForwardPacket(client.get(), fwd, chirp::gateway::SEND_MESSAGE_REQ); });
  ASSERT_TRUE(WaitFor([&] { return chat.Count(chirp::gateway::SEND_MESSAGE_REQ) > 0; },
                      std::chrono::seconds(5)));
  const auto sent = chat.All(chirp::gateway::SEND_MESSAGE_REQ);
  EXPECT_EQ(sent.back().sequence(), 42);
  EXPECT_EQ(sent.back().body(), "body-bytes");
}

TEST(ServiceBridgeTest, AuthFailureKicksClient) {
  FakeChatServer chat(chirp::common::AUTH_FAILED, chirp::common::OK);
  asio::io_context io;
  chirp::gateway::ServiceBridge bridge(io, "127.0.0.1", chat.port(), "gateway", "");
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

TEST(ServiceBridgeTest, LoginRejectedKicksClient) {
  FakeChatServer chat(chirp::common::OK, chirp::common::AUTH_FAILED);
  asio::io_context io;
  chirp::gateway::ServiceBridge bridge(io, "127.0.0.1", chat.port(), "gateway", "");
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

TEST(ServiceBridgeTest, InternalDisconnectKicksClient) {
  FakeChatServer chat(chirp::common::OK, chirp::common::OK);
  asio::io_context io;
  chirp::gateway::ServiceBridge bridge(io, "127.0.0.1", chat.port(), "gateway", "");
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

TEST(ServiceBridgeTest, DetachClosesInternalConnection) {
  FakeChatServer chat(chirp::common::OK, chirp::common::OK);
  asio::io_context io;
  chirp::gateway::ServiceBridge bridge(io, "127.0.0.1", chat.port(), "gateway", "");
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

TEST(ServiceBridgeTest, QueuedPacketsFlushOnReadyAndOverflowDrops) {
  FakeChatServer chat(chirp::common::OK, chirp::common::OK);
  asio::io_context io;
  chirp::gateway::ServiceBridge bridge(io, "127.0.0.1", chat.port(), "gateway", "");
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
      bridge.ForwardPacket(client.get(),
                           MakeRawPacket(chirp::gateway::SEND_MESSAGE_REQ, i, "m"),
                           chirp::gateway::SEND_MESSAGE_REQ);
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

TEST(ServiceBridgeTest, InternalFramesForwardedToClientExceptHandshake) {
  FakeChatServer chat(chirp::common::OK, chirp::common::OK);
  asio::io_context io;
  chirp::gateway::ServiceBridge bridge(io, "127.0.0.1", chat.port(), "gateway", "");
  BridgeIoRunner runner(io);

  auto client = std::make_shared<MockClientSession>();
  asio::post(io, [&] { bridge.Attach(client, "tok", "dev"); });
  // Prove readiness by round-tripping one business packet.
  asio::post(io, [&] {
    bridge.ForwardPacket(client.get(),
                         MakeRawPacket(chirp::gateway::SEND_MESSAGE_REQ, 1, "m"),
                         chirp::gateway::SEND_MESSAGE_REQ);
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

TEST(ServiceBridgeTest, DisabledBridgeIsNoOp) {
  asio::io_context io;
  chirp::gateway::ServiceBridge bridge(io, "", 0, "gateway", "");
  BridgeIoRunner runner(io);
  EXPECT_FALSE(bridge.enabled());

  auto client = std::make_shared<MockClientSession>();
  asio::post(io, [&] { bridge.Attach(client, "tok", "dev"); });
  asio::post(io, [&] {
    bridge.ForwardPacket(client.get(),
                         MakeRawPacket(chirp::gateway::SEND_MESSAGE_REQ, 1, "m"),
                         chirp::gateway::SEND_MESSAGE_REQ);
  });
  asio::post(io, [&] { bridge.Detach(client.get()); });
  // Nothing dialed, nothing sent: the historical silent-drop behavior.
  std::this_thread::sleep_for(std::chrono::milliseconds(50));
  EXPECT_EQ(client->SentCount(), 0u);
}

namespace {

// Grabs a free loopback port and releases it again: nothing listens there,
// so a dial to it is refused deterministically (real RST on loopback).
uint16_t ClosedLoopbackPort() {
  asio::io_context io;
  asio::ip::tcp::acceptor acceptor(io, asio::ip::tcp::endpoint(asio::ip::tcp::v4(), 0));
  const uint16_t port = acceptor.local_endpoint().port();
  asio::error_code ec;
  acceptor.close(ec);
  return port;
}

} // namespace

TEST(ServiceBridgeTest, ConnectFailureKicksClient) {
  asio::io_context io;
  const uint16_t dead_port = ClosedLoopbackPort();
  chirp::gateway::ServiceBridge bridge(io, "127.0.0.1", dead_port, "gateway", "");
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

TEST(ServiceBridgeTest, ResolveFailureKicksClient) {
  asio::io_context io;
  chirp::gateway::ServiceBridge bridge(io, "no such host for chirp test", 7000,
                                   "gateway", "");
  BridgeIoRunner runner(io);

  auto client = std::make_shared<MockClientSession>();
  asio::post(io, [&] { bridge.Attach(client, "tok", "dev"); });

  ASSERT_TRUE(WaitFor([&] { return client->SentCount() > 0; }, std::chrono::seconds(10)));
  const auto frames = client->SentPackets();
  ASSERT_FALSE(frames.empty());
  EXPECT_EQ(frames.back().msg_id(), chirp::gateway::KICK_NOTIFY);
  EXPECT_EQ(AsKick(frames.back()).reason(), "chat unavailable");
  EXPECT_TRUE(client->CloseAfterSend());
}

TEST(ServiceBridgeTest, DetachDuringResolveIsQuiet) {
  asio::io_context io;
  chirp::gateway::ServiceBridge bridge(io, "no such host for chirp test", 7000,
                                   "gateway", "");
  BridgeIoRunner runner(io);

  auto client = std::make_shared<MockClientSession>();
  // Detach runs while the resolver is still out (DNS is the slow path): the
  // late resolve completion must find a closed connection and do nothing -
  // no kick after the client already left, no use-after-free.
  asio::post(io, [&] { bridge.Attach(client, "tok", "dev"); });
  asio::post(io, [&] { bridge.Detach(client.get()); });
  std::this_thread::sleep_for(std::chrono::milliseconds(300));
  EXPECT_EQ(client->SentCount(), 0u);
}

TEST(ServiceBridgeTest, HandshakeTimeoutKicksClient) {
  FakeChatServer chat(chirp::common::OK, chirp::common::OK);
  asio::io_context io;
  chirp::gateway::ServiceBridge bridge(io, "127.0.0.1", chat.port(), "gateway", "");
  BridgeIoRunner runner(io);

  chat.hold_handshake();
  auto client = std::make_shared<MockClientSession>();
  asio::post(io, [&] { bridge.Attach(client, "tok", "dev"); });
  ASSERT_TRUE(WaitFor([&] { return chat.Count(chirp::gateway::SERVER_AUTH_REQ) > 0; },
                      std::chrono::seconds(5)));

  // Chat never answers: the bounded handshake kicks the client with a clear
  // reason instead of stranding the pipe.
  ASSERT_TRUE(WaitFor([&] { return client->SentCount() > 0; }, std::chrono::seconds(10)));
  const auto frames = client->SentPackets();
  ASSERT_FALSE(frames.empty());
  EXPECT_EQ(frames.back().msg_id(), chirp::gateway::KICK_NOTIFY);
  EXPECT_EQ(AsKick(frames.back()).reason(), "chat unavailable");
  EXPECT_TRUE(client->CloseAfterSend());
}

TEST(ServiceBridgeTest, InvalidFrameSizeDropsConnection) {
  FakeChatServer chat(chirp::common::OK, chirp::common::OK);
  asio::io_context io;
  chirp::gateway::ServiceBridge bridge(io, "127.0.0.1", chat.port(), "gateway", "");
  BridgeIoRunner runner(io);

  chat.hold_handshake();
  auto client = std::make_shared<MockClientSession>();
  asio::post(io, [&] { bridge.Attach(client, "tok", "dev"); });
  ASSERT_TRUE(WaitFor([&] { return chat.Count(chirp::gateway::SERVER_AUTH_REQ) > 0; },
                      std::chrono::seconds(5)));

  // A zero-length frame header is never valid: the bridge drops the pipe and
  // kicks the client instead of looping on it.
  chat.SendRawToLatest(std::string(4, '\0'));
  ASSERT_TRUE(WaitFor([&] { return client->SentCount() > 0; }, std::chrono::seconds(5)));
  const auto frames = client->SentPackets();
  ASSERT_FALSE(frames.empty());
  EXPECT_EQ(frames.back().msg_id(), chirp::gateway::KICK_NOTIFY);
  EXPECT_EQ(AsKick(frames.back()).reason(), "chat unavailable");
}

TEST(ServiceBridgeTest, MalformedFrameBodyDropsConnection) {
  FakeChatServer chat(chirp::common::OK, chirp::common::OK);
  asio::io_context io;
  chirp::gateway::ServiceBridge bridge(io, "127.0.0.1", chat.port(), "gateway", "");
  BridgeIoRunner runner(io);

  chat.hold_handshake();
  auto client = std::make_shared<MockClientSession>();
  asio::post(io, [&] { bridge.Attach(client, "tok", "dev"); });
  ASSERT_TRUE(WaitFor([&] { return chat.Count(chirp::gateway::SERVER_AUTH_REQ) > 0; },
                      std::chrono::seconds(5)));

  // A well-formed header carrying a body that is not a Packet: the bridge
  // drops the pipe rather than relaying garbage.
  chat.SendRawToLatest(std::string("\x00\x00\x00\x04\xff\xff\xff\xff", 8));
  ASSERT_TRUE(WaitFor([&] { return client->SentCount() > 0; }, std::chrono::seconds(5)));
  const auto frames = client->SentPackets();
  ASSERT_FALSE(frames.empty());
  EXPECT_EQ(frames.back().msg_id(), chirp::gateway::KICK_NOTIFY);
  EXPECT_EQ(AsKick(frames.back()).reason(), "chat unavailable");
}

TEST(ServiceBridgeTest, PartialFrameBodyDisconnectKicksClient) {
  FakeChatServer chat(chirp::common::OK, chirp::common::OK);
  asio::io_context io;
  chirp::gateway::ServiceBridge bridge(io, "127.0.0.1", chat.port(), "gateway", "");
  BridgeIoRunner runner(io);

  chat.hold_handshake();
  auto client = std::make_shared<MockClientSession>();
  asio::post(io, [&] { bridge.Attach(client, "tok", "dev"); });
  ASSERT_TRUE(WaitFor([&] { return chat.Count(chirp::gateway::SERVER_AUTH_REQ) > 0; },
                      std::chrono::seconds(5)));

  // A header announcing a body that never arrives: the bridge treats the
  // truncated frame as a lost chat and kicks.
  chat.SendRawToLatest(std::string("\x00\x00\x00\x64", 4));
  chat.CloseLatest();
  ASSERT_TRUE(WaitFor([&] { return client->SentCount() > 0; }, std::chrono::seconds(5)));
  const auto frames = client->SentPackets();
  ASSERT_FALSE(frames.empty());
  EXPECT_EQ(frames.back().msg_id(), chirp::gateway::KICK_NOTIFY);
  EXPECT_EQ(AsKick(frames.back()).reason(), "chat unavailable");
}

TEST(ServiceBridgeTest, UnexpectedFrameDuringAuthKicksClient) {
  FakeChatServer chat(chirp::common::OK, chirp::common::OK);
  chat.set_auth_reply_id(chirp::gateway::CHAT_MESSAGE_NOTIFY);
  asio::io_context io;
  chirp::gateway::ServiceBridge bridge(io, "127.0.0.1", chat.port(), "gateway", "");
  BridgeIoRunner runner(io);

  auto client = std::make_shared<MockClientSession>();
  asio::post(io, [&] { bridge.Attach(client, "tok", "dev"); });

  ASSERT_TRUE(WaitFor([&] { return client->SentCount() > 0; }, std::chrono::seconds(5)));
  const auto frames = client->SentPackets();
  ASSERT_FALSE(frames.empty());
  EXPECT_EQ(frames.back().msg_id(), chirp::gateway::KICK_NOTIFY);
  EXPECT_EQ(AsKick(frames.back()).reason(), "chat unavailable");
}

TEST(ServiceBridgeTest, UnexpectedFrameDuringLoginKicksClient) {
  FakeChatServer chat(chirp::common::OK, chirp::common::OK);
  chat.set_login_reply_id(chirp::gateway::CHAT_MESSAGE_NOTIFY);
  asio::io_context io;
  chirp::gateway::ServiceBridge bridge(io, "127.0.0.1", chat.port(), "gateway", "");
  BridgeIoRunner runner(io);

  auto client = std::make_shared<MockClientSession>();
  asio::post(io, [&] { bridge.Attach(client, "tok", "dev"); });

  ASSERT_TRUE(WaitFor([&] { return client->SentCount() > 0; }, std::chrono::seconds(5)));
  const auto frames = client->SentPackets();
  ASSERT_FALSE(frames.empty());
  EXPECT_EQ(frames.back().msg_id(), chirp::gateway::KICK_NOTIFY);
  // An unexpected frame mid-handshake means the chat side misbehaves (same
  // class as the auth-phase surprise frame): the bridge reports it as
  // unavailable, reserving "chat session rejected" for an explicit
  // LOGIN_RESP rejection.
  EXPECT_EQ(AsKick(frames.back()).reason(), "chat unavailable");
  EXPECT_TRUE(client->CloseAfterSend());
}

TEST(ServiceBridgeTest, ForwardAfterHandshakeFailureIsDropped) {
  FakeChatServer chat(chirp::common::AUTH_FAILED, chirp::common::OK);
  asio::io_context io;
  chirp::gateway::ServiceBridge bridge(io, "127.0.0.1", chat.port(), "gateway", "");
  BridgeIoRunner runner(io);

  auto client = std::make_shared<MockClientSession>();
  asio::post(io, [&] { bridge.Attach(client, "tok", "dev"); });
  ASSERT_TRUE(WaitFor([&] { return client->SentCount() > 0; }, std::chrono::seconds(5)));

  // The mock client lingers after the kick (the gateway closes it on its own
  // schedule): business packets that still arrive find no connection and are
  // dropped silently - no second kick, no crash.
  asio::post(io, [&] {
    bridge.ForwardPacket(client.get(),
                         MakeRawPacket(chirp::gateway::SEND_MESSAGE_REQ, 1, "m"),
                         chirp::gateway::SEND_MESSAGE_REQ);
  });
  std::this_thread::sleep_for(std::chrono::milliseconds(100));
  EXPECT_EQ(client->SentCount(), 1u);  // only the original kick
}

TEST(ServiceBridgeTest, SecondAttachReplacesOldPipe) {
  FakeChatServer chat(chirp::common::OK, chirp::common::OK);
  asio::io_context io;
  chirp::gateway::ServiceBridge bridge(io, "127.0.0.1", chat.port(), "gateway", "");
  BridgeIoRunner runner(io);

  auto client = std::make_shared<MockClientSession>();
  asio::post(io, [&] { bridge.Attach(client, "tok", "dev"); });
  ASSERT_TRUE(WaitFor([&] { return chat.Count(chirp::gateway::SERVER_AUTH_REQ) > 0; },
                      std::chrono::seconds(5)));

  // A defensive path: a second Attach for a live client closes the old pipe
  // (chat observes the EOF) and dials a fresh one - no kick, no leak.
  asio::post(io, [&] { bridge.Attach(client, "tok", "dev"); });
  EXPECT_TRUE(WaitFor([&] { return chat.Count(chirp::gateway::SERVER_AUTH_REQ) >= 2; },
                      std::chrono::seconds(5)));
  EXPECT_TRUE(WaitFor([&] { return chat.EofCount() >= 1; }, std::chrono::seconds(5)));
  EXPECT_EQ(client->SentCount(), 0u);
}

TEST(ServiceBridgeTest, GarbageAuthResponseBodyKicksClient) {
  FakeChatServer chat(chirp::common::OK, chirp::common::OK);
  chat.set_auth_reply_body("not-a-proto");
  asio::io_context io;
  chirp::gateway::ServiceBridge bridge(io, "127.0.0.1", chat.port(), "gateway", "");
  BridgeIoRunner runner(io);

  auto client = std::make_shared<MockClientSession>();
  asio::post(io, [&] { bridge.Attach(client, "tok", "dev"); });
  ASSERT_TRUE(WaitFor([&] { return client->SentCount() > 0; }, std::chrono::seconds(5)));
  const auto frames = client->SentPackets();
  ASSERT_FALSE(frames.empty());
  EXPECT_EQ(frames.back().msg_id(), chirp::gateway::KICK_NOTIFY);
  EXPECT_EQ(AsKick(frames.back()).reason(), "chat unavailable");
}

TEST(ServiceBridgeTest, GarbageLoginResponseBodyKicksClient) {
  FakeChatServer chat(chirp::common::OK, chirp::common::OK);
  chat.set_login_reply_body("not-a-login");
  asio::io_context io;
  chirp::gateway::ServiceBridge bridge(io, "127.0.0.1", chat.port(), "gateway", "");
  BridgeIoRunner runner(io);

  auto client = std::make_shared<MockClientSession>();
  asio::post(io, [&] { bridge.Attach(client, "tok", "dev"); });
  ASSERT_TRUE(WaitFor([&] { return client->SentCount() > 0; }, std::chrono::seconds(5)));
  const auto frames = client->SentPackets();
  ASSERT_FALSE(frames.empty());
  EXPECT_EQ(frames.back().msg_id(), chirp::gateway::KICK_NOTIFY);
  EXPECT_EQ(AsKick(frames.back()).reason(), "chat session rejected");
}

TEST(ServiceBridgeTest, ClientDestroyedBeforeFailClientSkipsKick) {
  FakeChatServer chat(chirp::common::AUTH_FAILED, chirp::common::OK);
  chat.hold_handshake();
  asio::io_context io;
  chirp::gateway::ServiceBridge bridge(io, "127.0.0.1", chat.port(), "gateway", "");
  BridgeIoRunner runner(io);

  auto client = std::make_shared<MockClientSession>();
  auto* raw = client.get();
  asio::post(io, [&] { bridge.Attach(client, "tok", "dev"); });
  ASSERT_TRUE(WaitFor([&] { return chat.Count(chirp::gateway::SERVER_AUTH_REQ) > 0; },
                      std::chrono::seconds(5)));
  // Drop the gateway's last strong ref while the auth reply is still held:
  // FailClient's weak_ptr expires (no kick erase), and a late ForwardPacket
  // still finds the map entry with closing=true.
  client.reset();
  chat.release_handshake();
  std::this_thread::sleep_for(std::chrono::milliseconds(300));
  asio::post(io, [&] {
    bridge.ForwardPacket(raw, MakeRawPacket(chirp::gateway::SEND_MESSAGE_REQ, 1, "m"), chirp::gateway::SEND_MESSAGE_REQ);
  });
  std::this_thread::sleep_for(std::chrono::milliseconds(100));
  EXPECT_EQ(client, nullptr);
}

TEST(ServiceBridgeTest, ChatPushAfterClientDestroyedSkipsSend) {
  FakeChatServer chat(chirp::common::OK, chirp::common::OK);
  asio::io_context io;
  chirp::gateway::ServiceBridge bridge(io, "127.0.0.1", chat.port(), "gateway", "");
  BridgeIoRunner runner(io);

  auto client = std::make_shared<MockClientSession>();
  auto* raw = client.get();
  asio::post(io, [&] { bridge.Attach(client, "tok", "dev"); });
  ASSERT_TRUE(WaitFor([&] { return chat.Count(chirp::gateway::SERVER_AUTH_REQ) > 0; },
                      std::chrono::seconds(5)));
  ASSERT_TRUE(WaitFor([&] { return chat.Count(chirp::gateway::LOGIN_REQ) > 0; },
                      std::chrono::seconds(5)));
  // Ready path: destroy the client, then a chat push hits weak lock failure.
  ASSERT_TRUE(WaitFor([&] { return chat.Count(chirp::gateway::LOGIN_REQ) > 0; },
                      std::chrono::seconds(5)));
  client.reset();
  chirp::chat::ChatMessage msg;
  msg.set_sender_id("u2");
  msg.set_content("late");
  chat.SendToLatest(MakePacket(chirp::gateway::CHAT_MESSAGE_NOTIFY, 0, msg));
  std::this_thread::sleep_for(std::chrono::milliseconds(200));
  EXPECT_EQ(client, nullptr);
  (void)raw;
}

TEST(ServiceBridgeTest, ForwardPacketFillsMissingRequestIdMonotonicAndPassesExplicit) {
  FakeChatServer chat(chirp::common::OK, chirp::common::OK);
  asio::io_context io;
  chirp::gateway::ServiceBridge bridge(io, "127.0.0.1", chat.port(), "edge-1", "s3cret");
  BridgeIoRunner runner(io);
  auto client = std::make_shared<MockClientSession>();
  asio::post(io, [&] { bridge.Attach(client, "tok", "dev"); });
  ASSERT_TRUE(WaitFor([&] { return chat.Count(chirp::gateway::LOGIN_REQ) > 0; },
                      std::chrono::seconds(5)));

  // 未带 request_id 的出站包:桥按连接内单调值兜底(缺省=连接内生成);
  // 显式 request_id 的原样透传(不改写)。三包依序进同一条内部连接。
  asio::post(io, [&] {
    bridge.ForwardPacket(client.get(),
                         MakeRawPacket(chirp::gateway::SEND_MESSAGE_REQ, 1, "a"),
                         chirp::gateway::SEND_MESSAGE_REQ);
    auto carried = MakeRawPacket(chirp::gateway::SEND_MESSAGE_REQ, 2, "b");
    carried.set_request_id(7);
    bridge.ForwardPacket(client.get(), carried, chirp::gateway::SEND_MESSAGE_REQ);
    bridge.ForwardPacket(client.get(),
                         MakeRawPacket(chirp::gateway::SEND_MESSAGE_REQ, 3, "c"),
                         chirp::gateway::SEND_MESSAGE_REQ);
  });
  ASSERT_TRUE(WaitFor([&] { return chat.Count(chirp::gateway::SEND_MESSAGE_REQ) >= 3; },
                      std::chrono::seconds(5)));
  const auto got = chat.All(chirp::gateway::SEND_MESSAGE_REQ);
  ASSERT_EQ(got.size(), 3u);
  ASSERT_NE(got[0].request_id(), 0);          // 缺省→生成
  EXPECT_EQ(got[1].request_id(), 7);          // 显式→透传
  ASSERT_NE(got[2].request_id(), 0);
  EXPECT_GT(got[2].request_id(), got[0].request_id());  // 连接内单调
  EXPECT_EQ(got[0].sequence(), 1);
  EXPECT_EQ(got[2].sequence(), 3);            // sequence 不受影响
}

// ---- search 降级语义（search_degrade=true 的第二实例）：断管不踢、2248
// 以请求 sequence 回 SERVER_UNAVAILABLE、凭据留存驱动下一条查询自动重拨。----

chirp::chat::SearchMessageRequest MakeSearchReq(const std::string& keyword) {
  chirp::chat::SearchMessageRequest req;
  req.set_keyword(keyword);
  return req;
}

TEST(ServiceBridgeTest, SearchDegradedAnswersServerUnavailableWhenUnreachable) {
  asio::io_context io;
  const uint16_t dead_port = ClosedLoopbackPort();
  chirp::gateway::ServiceBridge bridge(io, "127.0.0.1", dead_port, "gateway", "",
                                       /*search_degrade=*/true);
  BridgeIoRunner runner(io);

  auto client = std::make_shared<MockClientSession>();
  asio::post(io, [&] { bridge.Attach(client, "tok", "dev"); });
  // 拨号失败进降级态：客户端不被踢、不收任何帧（对比 chat 实例同路径的
  // ConnectFailureKicksClient）。
  std::this_thread::sleep_for(std::chrono::milliseconds(300));
  EXPECT_EQ(client->SentCount(), 0u);
  EXPECT_FALSE(client->CloseAfterSend());

  // 无管道的 2248：合成 SEARCH_MESSAGE_RESP(SERVER_UNAVAILABLE)，sequence
  // 沿用请求，客户端连接原样保留。
  asio::post(io, [&] {
    bridge.ForwardPacket(client.get(), MakePacket(chirp::gateway::SEARCH_MESSAGE_REQ, 77,
                                                  MakeSearchReq("hi")),
                         chirp::gateway::SEARCH_MESSAGE_REQ);
  });
  ASSERT_TRUE(WaitFor([&] { return client->SentCount() > 0; }, std::chrono::seconds(5)));
  const auto frames = client->SentPackets();
  ASSERT_EQ(frames.size(), 1u);
  EXPECT_EQ(frames[0].msg_id(), chirp::gateway::SEARCH_MESSAGE_RESP);
  EXPECT_EQ(frames[0].sequence(), 77);
  chirp::chat::SearchMessageResponse resp;
  ASSERT_TRUE(resp.ParseFromString(frames[0].body()));
  EXPECT_EQ(resp.code(), chirp::common::SERVER_UNAVAILABLE);
  EXPECT_FALSE(client->CloseAfterSend());
}

TEST(ServiceBridgeTest, SearchPendingOverflowDegradesWholePipe) {
  // 握手被扣住，灌满挂起队列再溢出一条：第 65 条触发整管降级——已挂起的
  // 64 条逐条补 SERVER_UNAVAILABLE，溢出的这条也当场回码，客户端零踢零断。
  FakeChatServer chat(chirp::common::OK, chirp::common::OK);
  asio::io_context io;
  chirp::gateway::ServiceBridge bridge(io, "127.0.0.1", chat.port(), "gateway", "",
                                       /*search_degrade=*/true);
  BridgeIoRunner runner(io);

  auto client = std::make_shared<MockClientSession>();
  chat.hold_handshake();
  asio::post(io, [&] { bridge.Attach(client, "tok", "dev"); });
  ASSERT_TRUE(WaitFor([&] { return chat.Count(chirp::gateway::SERVER_AUTH_REQ) > 0; },
                      std::chrono::seconds(5)));

  const int kOverflowTotal = 65;  // kMaxPendingPackets(64) + 1
  asio::post(io, [&] {
    for (int i = 0; i < kOverflowTotal; i++) {
      bridge.ForwardPacket(client.get(),
                           MakePacket(chirp::gateway::SEARCH_MESSAGE_REQ, i, MakeSearchReq("q")),
                           chirp::gateway::SEARCH_MESSAGE_REQ);
    }
  });
  ASSERT_TRUE(WaitFor([&] { return client->SentCount() >= kOverflowTotal; },
                      std::chrono::seconds(5)));
  const auto frames = client->SentPackets();
  ASSERT_EQ(frames.size(), static_cast<size_t>(kOverflowTotal));
  for (int i = 0; i < kOverflowTotal; i++) {
    EXPECT_EQ(frames[i].msg_id(), chirp::gateway::SEARCH_MESSAGE_RESP);
    EXPECT_EQ(frames[i].sequence(), i);
    chirp::chat::SearchMessageResponse resp;
    ASSERT_TRUE(resp.ParseFromString(frames[i].body()));
    EXPECT_EQ(resp.code(), chirp::common::SERVER_UNAVAILABLE);
  }
  EXPECT_FALSE(client->CloseAfterSend());
  EXPECT_FALSE(client->IsClosed());
}

TEST(ServiceBridgeTest, SearchQueryFromUnattachedClientStillGetsDegradeAnswer) {
  // 真实竞态窗口：redis claim 异步回调里才 Attach，客户端在 LOGIN_RESP
  // 之后立刻流水线一条 2248 就会先于 Attach 到达。降级语义对未建管的
  // 查询同样回码（无凭据则不重拨），不踢不崩。
  asio::io_context io;
  const uint16_t dead_port = ClosedLoopbackPort();
  chirp::gateway::ServiceBridge bridge(io, "127.0.0.1", dead_port, "gateway", "",
                                       /*search_degrade=*/true);
  BridgeIoRunner runner(io);

  auto client = std::make_shared<MockClientSession>();
  asio::post(io, [&] {
    bridge.ForwardPacket(client.get(),
                         MakePacket(chirp::gateway::SEARCH_MESSAGE_REQ, 5, MakeSearchReq("q")),
                         chirp::gateway::SEARCH_MESSAGE_REQ);
  });
  ASSERT_TRUE(WaitFor([&] { return client->SentCount() > 0; }, std::chrono::seconds(5)));
  const auto frames = client->SentPackets();
  ASSERT_EQ(frames.size(), 1u);
  EXPECT_EQ(frames[0].msg_id(), chirp::gateway::SEARCH_MESSAGE_RESP);
  EXPECT_EQ(frames[0].sequence(), 5);
  chirp::chat::SearchMessageResponse resp;
  ASSERT_TRUE(resp.ParseFromString(frames[0].body()));
  EXPECT_EQ(resp.code(), chirp::common::SERVER_UNAVAILABLE);
  EXPECT_FALSE(client->CloseAfterSend());
}

TEST(ServiceBridgeTest, SearchPipeLossDegradesPendingQueriesAndRedials) {
  FakeChatServer chat(chirp::common::OK, chirp::common::OK);
  asio::io_context io;
  chirp::gateway::ServiceBridge bridge(io, "127.0.0.1", chat.port(), "gateway", "",
                                       /*search_degrade=*/true);
  BridgeIoRunner runner(io);

  auto client = std::make_shared<MockClientSession>();
  chat.hold_handshake();
  asio::post(io, [&] { bridge.Attach(client, "tok", "dev"); });
  ASSERT_TRUE(WaitFor([&] { return chat.Count(chirp::gateway::SERVER_AUTH_REQ) > 0; },
                      std::chrono::seconds(5)));

  // 握手在飞，两条 2248 进挂起队列；此时管道被掐断：挂起查询逐条补
  // SERVER_UNAVAILABLE（顺序保持），客户端连接不被触碰。
  asio::post(io, [&] {
    bridge.ForwardPacket(client.get(),
                         MakePacket(chirp::gateway::SEARCH_MESSAGE_REQ, 1, MakeSearchReq("a")),
                         chirp::gateway::SEARCH_MESSAGE_REQ);
    bridge.ForwardPacket(client.get(),
                         MakePacket(chirp::gateway::SEARCH_MESSAGE_REQ, 2, MakeSearchReq("b")),
                         chirp::gateway::SEARCH_MESSAGE_REQ);
  });
  std::this_thread::sleep_for(std::chrono::milliseconds(100));
  chat.CloseLatest();
  ASSERT_TRUE(WaitFor([&] { return client->SentCount() >= 2; }, std::chrono::seconds(5)));
  const auto frames = client->SentPackets();
  ASSERT_EQ(frames.size(), 2u);
  for (int i = 0; i < 2; ++i) {
    EXPECT_EQ(frames[i].msg_id(), chirp::gateway::SEARCH_MESSAGE_RESP);
    EXPECT_EQ(frames[i].sequence(), i + 1);
    chirp::chat::SearchMessageResponse resp;
    ASSERT_TRUE(resp.ParseFromString(frames[i].body()));
    EXPECT_EQ(resp.code(), chirp::common::SERVER_UNAVAILABLE);
  }
  EXPECT_FALSE(client->CloseAfterSend());
  EXPECT_FALSE(client->IsClosed());

  // 下一条 2248 触发凭据重拨（Attach 留存的 token/device 原样重放），
  // search 侧重新见到完整握手。
  asio::post(io, [&] {
    bridge.ForwardPacket(client.get(),
                         MakePacket(chirp::gateway::SEARCH_MESSAGE_REQ, 3, MakeSearchReq("c")),
                         chirp::gateway::SEARCH_MESSAGE_REQ);
  });
  EXPECT_TRUE(WaitFor([&] { return chat.Count(chirp::gateway::SERVER_AUTH_REQ) >= 2; },
                      std::chrono::seconds(5)));
  chat.release_handshake();
  ASSERT_TRUE(WaitFor([&] { return chat.Count(chirp::gateway::LOGIN_REQ) >= 2; },
                      std::chrono::seconds(5)));
  const auto logins = chat.All(chirp::gateway::LOGIN_REQ);
  chirp::auth::LoginRequest replay;
  ASSERT_TRUE(replay.ParseFromString(logins.back().body()));
  EXPECT_EQ(replay.token(), "tok");
  EXPECT_EQ(replay.device_id(), "dev");

  // 新管道就绪后，再一条 2248 原样进后端（健康路径透传，sequence 不改写）。
  asio::post(io, [&] {
    bridge.ForwardPacket(client.get(),
                         MakePacket(chirp::gateway::SEARCH_MESSAGE_REQ, 4, MakeSearchReq("d")),
                         chirp::gateway::SEARCH_MESSAGE_REQ);
  });
  ASSERT_TRUE(WaitFor([&] { return chat.Count(chirp::gateway::SEARCH_MESSAGE_REQ) >= 1; },
                      std::chrono::seconds(5)));
  const auto searched = chat.All(chirp::gateway::SEARCH_MESSAGE_REQ);
  EXPECT_EQ(searched.back().sequence(), 4);
  EXPECT_EQ(searched.back().body(), MakeSearchReq("d").SerializeAsString());
}

TEST(ServiceBridgeTest, SearchDegradeDoesNotKickOnHandshakeRejection) {
  // 对照组：同样的 auth 拒绝，chat 实例踢客户端（AuthFailureKicksClient），
  // search 实例静默进降级态——secret 配错也只影响检索可用性，不波及会话；
  // 后续 2248 照样拿到 SERVER_UNAVAILABLE。
  FakeChatServer chat(chirp::common::AUTH_FAILED, chirp::common::OK);
  asio::io_context io;
  chirp::gateway::ServiceBridge bridge(io, "127.0.0.1", chat.port(), "gateway", "",
                                       /*search_degrade=*/true);
  BridgeIoRunner runner(io);

  auto client = std::make_shared<MockClientSession>();
  asio::post(io, [&] { bridge.Attach(client, "tok", "dev"); });
  // 握手被拒：降级抹掉管道，客户端零帧、连接保留。
  ASSERT_TRUE(WaitFor([&] { return chat.Count(chirp::gateway::SERVER_AUTH_REQ) > 0; },
                      std::chrono::seconds(5)));
  std::this_thread::sleep_for(std::chrono::milliseconds(300));
  EXPECT_EQ(client->SentCount(), 0u);
  EXPECT_FALSE(client->CloseAfterSend());

  asio::post(io, [&] {
    bridge.ForwardPacket(client.get(),
                         MakePacket(chirp::gateway::SEARCH_MESSAGE_REQ, 9, MakeSearchReq("q")),
                         chirp::gateway::SEARCH_MESSAGE_REQ);
  });
  ASSERT_TRUE(WaitFor([&] { return client->SentCount() > 0; }, std::chrono::seconds(5)));
  const auto frames = client->SentPackets();
  ASSERT_EQ(frames.size(), 1u);
  EXPECT_EQ(frames[0].msg_id(), chirp::gateway::SEARCH_MESSAGE_RESP);
  EXPECT_EQ(frames[0].sequence(), 9);
  chirp::chat::SearchMessageResponse resp;
  ASSERT_TRUE(resp.ParseFromString(frames[0].body()));
  EXPECT_EQ(resp.code(), chirp::common::SERVER_UNAVAILABLE);
}
