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

#include "network/chat_bridge.h"
#include "fake_chat_server.h"
#include "logger.h"
#include "proto/auth.pb.h"
#include "proto/chat.pb.h"
#include "proto/common.pb.h"
#include "proto/gateway.pb.h"
#include "proto/server_gateway.pb.h"

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

TEST(ChatBridgeTest, ConnectFailureKicksClient) {
  asio::io_context io;
  const uint16_t dead_port = ClosedLoopbackPort();
  chirp::gateway::ChatBridge bridge(io, "127.0.0.1", dead_port, "gateway", "");
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

TEST(ChatBridgeTest, ResolveFailureKicksClient) {
  asio::io_context io;
  chirp::gateway::ChatBridge bridge(io, "no-such-host-for-chirp-test.invalid", 7000,
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

TEST(ChatBridgeTest, DetachDuringResolveIsQuiet) {
  asio::io_context io;
  chirp::gateway::ChatBridge bridge(io, "no-such-host-for-chirp-test.invalid", 7000,
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

TEST(ChatBridgeTest, HandshakeTimeoutKicksClient) {
  FakeChatServer chat(chirp::common::OK, chirp::common::OK);
  asio::io_context io;
  chirp::gateway::ChatBridge bridge(io, "127.0.0.1", chat.port(), "gateway", "");
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

TEST(ChatBridgeTest, InvalidFrameSizeDropsConnection) {
  FakeChatServer chat(chirp::common::OK, chirp::common::OK);
  asio::io_context io;
  chirp::gateway::ChatBridge bridge(io, "127.0.0.1", chat.port(), "gateway", "");
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

TEST(ChatBridgeTest, MalformedFrameBodyDropsConnection) {
  FakeChatServer chat(chirp::common::OK, chirp::common::OK);
  asio::io_context io;
  chirp::gateway::ChatBridge bridge(io, "127.0.0.1", chat.port(), "gateway", "");
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

TEST(ChatBridgeTest, PartialFrameBodyDisconnectKicksClient) {
  FakeChatServer chat(chirp::common::OK, chirp::common::OK);
  asio::io_context io;
  chirp::gateway::ChatBridge bridge(io, "127.0.0.1", chat.port(), "gateway", "");
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

TEST(ChatBridgeTest, UnexpectedFrameDuringAuthKicksClient) {
  FakeChatServer chat(chirp::common::OK, chirp::common::OK);
  chat.set_auth_reply_id(chirp::gateway::CHAT_MESSAGE_NOTIFY);
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
}

TEST(ChatBridgeTest, UnexpectedFrameDuringLoginKicksClient) {
  FakeChatServer chat(chirp::common::OK, chirp::common::OK);
  chat.set_login_reply_id(chirp::gateway::CHAT_MESSAGE_NOTIFY);
  asio::io_context io;
  chirp::gateway::ChatBridge bridge(io, "127.0.0.1", chat.port(), "gateway", "");
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

TEST(ChatBridgeTest, ForwardAfterHandshakeFailureIsDropped) {
  FakeChatServer chat(chirp::common::AUTH_FAILED, chirp::common::OK);
  asio::io_context io;
  chirp::gateway::ChatBridge bridge(io, "127.0.0.1", chat.port(), "gateway", "");
  BridgeIoRunner runner(io);

  auto client = std::make_shared<MockClientSession>();
  asio::post(io, [&] { bridge.Attach(client, "tok", "dev"); });
  ASSERT_TRUE(WaitFor([&] { return client->SentCount() > 0; }, std::chrono::seconds(5)));

  // The mock client lingers after the kick (the gateway closes it on its own
  // schedule): business packets that still arrive find no connection and are
  // dropped silently - no second kick, no crash.
  asio::post(io, [&] {
    bridge.ForwardToChat(client.get(),
                         MakeRawPacket(chirp::gateway::SEND_MESSAGE_REQ, 1, "m"));
  });
  std::this_thread::sleep_for(std::chrono::milliseconds(100));
  EXPECT_EQ(client->SentCount(), 1u);  // only the original kick
}

TEST(ChatBridgeTest, SecondAttachReplacesOldPipe) {
  FakeChatServer chat(chirp::common::OK, chirp::common::OK);
  asio::io_context io;
  chirp::gateway::ChatBridge bridge(io, "127.0.0.1", chat.port(), "gateway", "");
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
