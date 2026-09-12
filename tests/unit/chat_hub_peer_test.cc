// Loopback tests for the chat -> server_gateway peer: a real in-process TCP
// hub (framed chirp.gateway.Packet on the wire) drives connect, auth,
// heartbeat, injection delivery, bad-frame handling, and reconnect behavior.
// All hub socket work runs on its own io thread, mirroring fake_servers.h.
#include <gtest/gtest.h>

#include <asio.hpp>

#include <array>
#include <chrono>
#include <memory>
#include <mutex>
#include <string>
#include <thread>
#include <vector>

#include "logger.h"
#include "network/byte_order.h"
#include "network/protobuf_framing.h"
#include "proto/auth.pb.h"
#include "proto/chat.pb.h"
#include "proto/common.pb.h"
#include "proto/gateway.pb.h"
#include "proto/server_gateway.pb.h"
#include "server_gateway_peer.h"

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

// Minimal in-process hub: accepts connections, records every framed packet,
// answers auth according to the scripted code, and pongs heartbeats.
class FakeHubServer {
 public:
  explicit FakeHubServer(chirp::common::ErrorCode auth_code = chirp::common::OK,
                         int heartbeat_interval = 1)
      : auth_code_(auth_code),
        heartbeat_interval_(heartbeat_interval),
        acceptor_(io_, asio::ip::tcp::endpoint(asio::ip::tcp::v4(), 0)) {
    port_ = static_cast<uint16_t>(acceptor_.local_endpoint().port());
    DoAccept();
    thread_ = std::thread([this] { io_.run(); });
  }

  ~FakeHubServer() {
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

  // Pushes a framed packet to the most recent connection (the live one after
  // a reconnect - closed sockets stay in the vector).
  void SendToLatest(const Packet& pkt) {
    asio::post(io_, [this, pkt] {
      std::lock_guard<std::mutex> lock(mu_);
      if (!sockets_.empty()) {
        WriteTo(*sockets_.back(), pkt);
      }
    });
  }

  // Pushes raw (possibly malformed) bytes to the most recent connection.
  void SendRawToLatest(const std::string& bytes) {
    asio::post(io_, [this, bytes] {
      std::lock_guard<std::mutex> lock(mu_);
      if (!sockets_.empty()) {
        asio::error_code ec;
        asio::write(*sockets_.back(), asio::buffer(bytes), ec);
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

  // Accumulates the byte stream and emits one HandlePacket per full frame.
  void DoRead(const std::shared_ptr<asio::ip::tcp::socket>& sock, std::string buf) {
    auto chunk = std::make_shared<std::array<char, 4096>>();
    sock->async_read_some(
        asio::buffer(*chunk),
        [this, sock, buf, chunk](const std::error_code& ec, std::size_t n) mutable {
          if (ec) {
            return;
          }
          buf.append(chunk->data(), n);
          for (;;) {
            if (buf.size() < 4) {
              break;
            }
            const uint32_t size = chirp::network::ReadU32BE(reinterpret_cast<const uint8_t*>(buf.data()));
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
      std::lock_guard<std::mutex> lock(mu_);
      auth_service_ids_.push_back(req.service_id());
      chirp::server_gateway::ServerAuthResponse resp;
      resp.set_code(auth_code_);
      resp.set_heartbeat_interval_seconds(heartbeat_interval_);
      WriteTo(*sock, MakePacket(chirp::gateway::SERVER_AUTH_RESP, pkt.sequence(), resp));
      if (auth_code_ != chirp::common::OK) {
        asio::error_code ec;
        sock->close(ec);
      }
    } else if (pkt.msg_id() == chirp::gateway::SERVER_HEARTBEAT_PING) {
      chirp::server_gateway::ServerHeartbeatPong pong;
      pong.set_server_time_ms(0);
      WriteTo(*sock, MakePacket(chirp::gateway::SERVER_HEARTBEAT_PONG, pkt.sequence(), pong));
    }
  }

  // Only ever called from the hub io thread, so writes never interleave.
  void WriteTo(asio::ip::tcp::socket& sock, const Packet& pkt) {
    asio::error_code ec;
    asio::write(sock, asio::buffer(FramePacket(pkt)), ec);
  }

  chirp::common::ErrorCode auth_code_;
  int heartbeat_interval_;
  asio::io_context io_;
  asio::ip::tcp::acceptor acceptor_;
  uint16_t port_{0};
  std::thread thread_;
  std::mutex mu_;
  std::vector<std::shared_ptr<asio::ip::tcp::socket>> sockets_;
  std::vector<Packet> received_;
  std::vector<std::string> auth_service_ids_;
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

// Runs the peer's io_context on a helper thread with a hard watchdog; the
// watchdog stops the loop so a hung peer fails asserts instead of the test.
class PeerIoRunner {
 public:
  explicit PeerIoRunner(asio::io_context& io) : io_(io) {
    watchdog_ = std::make_shared<asio::steady_timer>(io);
    watchdog_->expires_after(std::chrono::seconds(30));
    watchdog_->async_wait([&](const std::error_code&) { io_.stop(); });
    thread_ = std::thread([this] { io_.run(); });
  }

  // Executes everything already queued (e.g. Stop's posted close and the
  // aborted-handler completions it triggers) without waiting for timers.
  void Drain() {
    while (io_.poll() > 0) {
    }
  }

  void Finish() {
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

chirp::chat::ServerGatewayPeer::Options HubOptions(const FakeHubServer& hub,
                                                   const std::string& secret = "s3cret") {
  chirp::chat::ServerGatewayPeer::Options opts;
  opts.host = "127.0.0.1";
  opts.port = hub.port();
  opts.service_id = "chat";
  opts.secret = secret;
  opts.reconnect_delay_seconds = 1;
  return opts;
}

TEST(ChatHubPeerTest, AuthenticatesSendsHeartbeatAndDeliversInjections) {
  chirp::common::Logger::Instance().SetLevel(chirp::common::Logger::Level::kError);
  FakeHubServer hub;
  asio::io_context io;
  PeerIoRunner runner(io);

  std::mutex mu;
  std::vector<chirp::server_gateway::InjectMessageNotify> injected;
  auto peer = chirp::chat::ServerGatewayPeer::Create(
      io, HubOptions(hub),
      [&](const chirp::server_gateway::InjectMessageNotify& n) {
        std::lock_guard<std::mutex> lock(mu);
        injected.push_back(n);
      });
  peer->Start();

  // Auth handshake with the configured identity.
  ASSERT_TRUE(WaitFor([&] { return hub.Count(chirp::gateway::SERVER_AUTH_REQ) > 0; },
                      std::chrono::seconds(5)));
  const auto auths = hub.All(chirp::gateway::SERVER_AUTH_REQ);
  ASSERT_FALSE(auths.empty());
  chirp::server_gateway::ServerAuthRequest auth_req;
  ASSERT_TRUE(auth_req.ParseFromString(auths[0].body()));
  EXPECT_EQ(auth_req.service_id(), "chat");
  EXPECT_EQ(auth_req.secret(), "s3cret");
  EXPECT_EQ(auth_req.protocol_version(), 1);

  // The assigned 1s cadence produces heartbeats; the hub pongs back.
  EXPECT_TRUE(WaitFor([&] { return hub.Count(chirp::gateway::SERVER_HEARTBEAT_PING) >= 2; },
                      std::chrono::seconds(6)));

  // A forwarded injection reaches the handler with its payload intact.
  chirp::server_gateway::InjectMessageNotify notify;
  auto* req = notify.mutable_message();
  req->set_sender_kind(chirp::server_gateway::SENDER_NPC);
  req->set_sender_id("npc:blacksmith_01");
  req->set_channel_type(static_cast<int32_t>(chirp::chat::PRIVATE));
  req->set_receiver_id("player_1");
  req->set_content("welcome to the forge");
  hub.SendToLatest(MakePacket(chirp::gateway::INJECT_MESSAGE_NOTIFY, 0, notify));
  EXPECT_TRUE(WaitFor([&] {
    std::lock_guard<std::mutex> lock(mu);
    for (const auto& n : injected) {
      if (n.message().sender_id() == "npc:blacksmith_01" &&
          n.message().content() == "welcome to the forge") {
        return true;
      }
    }
    return false;
  }, std::chrono::seconds(5)));

  // Frames the peer does not consume (client-plane and event ids) are ignored.
  chirp::auth::LoginRequest unrelated;
  unrelated.set_token("x");
  hub.SendToLatest(MakePacket(chirp::gateway::LOGIN_REQ, 0, unrelated));
  chirp::server_gateway::EventDeliverNotify event;
  event.set_event_id("evt-1");
  hub.SendToLatest(MakePacket(chirp::gateway::EVENT_DELIVER_NOTIFY, 0, event));
  std::this_thread::sleep_for(std::chrono::milliseconds(200));
  EXPECT_EQ(hub.Count(chirp::gateway::SERVER_AUTH_REQ), 1u);  // connection stayed up

  peer->Stop();
  runner.Drain();
  runner.Finish();
}

TEST(ChatHubPeerTest, ReconnectsAfterConnectionLoss) {
  chirp::common::Logger::Instance().SetLevel(chirp::common::Logger::Level::kError);
  // interval=0 exercises the fallback to the configured cadence.
  FakeHubServer hub(chirp::common::OK, 0);
  asio::io_context io;
  PeerIoRunner runner(io);

  auto peer = chirp::chat::ServerGatewayPeer::Create(
      io, HubOptions(hub), [](const chirp::server_gateway::InjectMessageNotify&) {});
  peer->Start();

  ASSERT_TRUE(WaitFor([&] { return hub.Count(chirp::gateway::SERVER_AUTH_REQ) >= 1; },
                      std::chrono::seconds(5)));
  hub.CloseLatest();
  EXPECT_TRUE(WaitFor([&] { return hub.Count(chirp::gateway::SERVER_AUTH_REQ) >= 2; },
                      std::chrono::seconds(6)));

  peer->Stop();
  runner.Drain();
  runner.Finish();
}

TEST(ChatHubPeerTest, RejectedAuthRetries) {
  chirp::common::Logger::Instance().SetLevel(chirp::common::Logger::Level::kError);
  FakeHubServer hub(chirp::common::AUTH_FAILED);
  asio::io_context io;
  PeerIoRunner runner(io);

  auto peer = chirp::chat::ServerGatewayPeer::Create(
      io, HubOptions(hub, "wrong"), [](const chirp::server_gateway::InjectMessageNotify&) {});
  peer->Start();

  // Every attempt is rejected and retried after the reconnect delay.
  EXPECT_TRUE(WaitFor([&] { return hub.Count(chirp::gateway::SERVER_AUTH_REQ) >= 3; },
                      std::chrono::seconds(8)));

  peer->Stop();
  runner.Drain();
  runner.Finish();
}

TEST(ChatHubPeerTest, MalformedAuthResponseTriggersReconnect) {
  chirp::common::Logger::Instance().SetLevel(chirp::common::Logger::Level::kError);
  FakeHubServer hub;
  asio::io_context io;
  PeerIoRunner runner(io);

  auto peer = chirp::chat::ServerGatewayPeer::Create(
      io, HubOptions(hub), [](const chirp::server_gateway::InjectMessageNotify&) {});
  peer->Start();

  ASSERT_TRUE(WaitFor([&] { return hub.Count(chirp::gateway::SERVER_AUTH_REQ) >= 1; },
                      std::chrono::seconds(5)));
  hub.SendToLatest(MakeRawPacket(chirp::gateway::SERVER_AUTH_RESP, 0, "not-a-proto"));
  EXPECT_TRUE(WaitFor([&] { return hub.Count(chirp::gateway::SERVER_AUTH_REQ) >= 2; },
                      std::chrono::seconds(6)));

  peer->Stop();
  runner.Drain();
  runner.Finish();
}

TEST(ChatHubPeerTest, InvalidFrameSizeTriggersReconnect) {
  chirp::common::Logger::Instance().SetLevel(chirp::common::Logger::Level::kError);
  FakeHubServer hub;
  asio::io_context io;
  PeerIoRunner runner(io);

  auto peer = chirp::chat::ServerGatewayPeer::Create(
      io, HubOptions(hub), [](const chirp::server_gateway::InjectMessageNotify&) {});
  peer->Start();

  ASSERT_TRUE(WaitFor([&] { return hub.Count(chirp::gateway::SERVER_AUTH_REQ) >= 1; },
                      std::chrono::seconds(5)));
  // A length prefix past the frame cap is a protocol violation: the peer
  // drops the connection and reconnects.
  const uint32_t huge = 0xffffffffu;
  hub.SendRawToLatest(std::string(reinterpret_cast<const char*>(&huge), 4));
  EXPECT_TRUE(WaitFor([&] { return hub.Count(chirp::gateway::SERVER_AUTH_REQ) >= 2; },
                      std::chrono::seconds(6)));

  peer->Stop();
  runner.Drain();
  runner.Finish();
}

// Writes a big-endian length prefix matching the wire format.
std::string FrameHeader(uint32_t size) {
  std::string out(4, '\0');
  chirp::network::WriteU32BE(reinterpret_cast<uint8_t*>(out.data()), size);
  return out;
}

TEST(ChatHubPeerTest, GarbageBodyTriggersReconnect) {
  chirp::common::Logger::Instance().SetLevel(chirp::common::Logger::Level::kError);
  FakeHubServer hub;
  asio::io_context io;
  PeerIoRunner runner(io);

  auto peer = chirp::chat::ServerGatewayPeer::Create(
      io, HubOptions(hub), [](const chirp::server_gateway::InjectMessageNotify&) {});
  peer->Start();

  ASSERT_TRUE(WaitFor([&] { return hub.Count(chirp::gateway::SERVER_AUTH_REQ) >= 1; },
                      std::chrono::seconds(5)));
  // A well-sized frame whose payload is not a valid Packet.
  const std::string body = "not-a-proto";
  hub.SendRawToLatest(FrameHeader(static_cast<uint32_t>(body.size())) + body);
  EXPECT_TRUE(WaitFor([&] { return hub.Count(chirp::gateway::SERVER_AUTH_REQ) >= 2; },
                      std::chrono::seconds(6)));

  peer->Stop();
  runner.Drain();
  runner.Finish();
}

TEST(ChatHubPeerTest, MalformedInjectBodyIsIgnored) {
  chirp::common::Logger::Instance().SetLevel(chirp::common::Logger::Level::kError);
  FakeHubServer hub;
  asio::io_context io;
  PeerIoRunner runner(io);

  std::mutex mu;
  size_t injected = 0;
  auto peer = chirp::chat::ServerGatewayPeer::Create(
      io, HubOptions(hub), [&](const chirp::server_gateway::InjectMessageNotify&) {
        std::lock_guard<std::mutex> lock(mu);
        injected++;
      });
  peer->Start();

  ASSERT_TRUE(WaitFor([&] { return hub.Count(chirp::gateway::SERVER_AUTH_REQ) >= 1; },
                      std::chrono::seconds(5)));
  // A malformed injection payload is logged and skipped: the handler is not
  // called and the connection stays up.
  hub.SendToLatest(MakeRawPacket(chirp::gateway::INJECT_MESSAGE_NOTIFY, 0, "not-a-proto"));
  std::this_thread::sleep_for(std::chrono::milliseconds(200));
  EXPECT_EQ(injected, 0u);
  EXPECT_EQ(hub.Count(chirp::gateway::SERVER_AUTH_REQ), 1u);  // connection stayed up

  peer->Stop();
  runner.Drain();
  runner.Finish();
}

TEST(ChatHubPeerTest, MidBodyDisconnectTriggersReconnect) {
  chirp::common::Logger::Instance().SetLevel(chirp::common::Logger::Level::kError);
  FakeHubServer hub;
  asio::io_context io;
  PeerIoRunner runner(io);

  auto peer = chirp::chat::ServerGatewayPeer::Create(
      io, HubOptions(hub), [](const chirp::server_gateway::InjectMessageNotify&) {});
  peer->Start();

  ASSERT_TRUE(WaitFor([&] { return hub.Count(chirp::gateway::SERVER_AUTH_REQ) >= 1; },
                      std::chrono::seconds(5)));
  // Announce a 64-byte body but close after only a few payload bytes: the
  // partial-body read fails and must funnel into the reconnect path.
  hub.SendRawToLatest(FrameHeader(64) + std::string(3, '\0'));
  hub.CloseLatest();
  EXPECT_TRUE(WaitFor([&] { return hub.Count(chirp::gateway::SERVER_AUTH_REQ) >= 2; },
                      std::chrono::seconds(6)));

  peer->Stop();
  runner.Drain();
  runner.Finish();
}

TEST(ChatHubPeerTest, ConnectFailureRetries) {
  chirp::common::Logger::Instance().SetLevel(chirp::common::Logger::Level::kError);
  asio::io_context io;
  PeerIoRunner runner(io);

  // Port 1 on loopback refuses connections; the peer must keep retrying
  // (reconnect scheduling) without hanging or crashing.
  chirp::chat::ServerGatewayPeer::Options opts;
  opts.host = "127.0.0.1";
  opts.port = 1;
  opts.service_id = "chat";
  opts.secret = "s3cret";
  opts.reconnect_delay_seconds = 1;
  auto peer = chirp::chat::ServerGatewayPeer::Create(
      io, opts, [](const chirp::server_gateway::InjectMessageNotify&) {});
  peer->Start();

  std::this_thread::sleep_for(std::chrono::milliseconds(2500));

  peer->Stop();
  runner.Drain();
  runner.Finish();
  SUCCEED();
}

TEST(ChatHubPeerTest, ResolveFailureRetries) {
  chirp::common::Logger::Instance().SetLevel(chirp::common::Logger::Level::kError);
  asio::io_context io;
  PeerIoRunner runner(io);

  // A host whose label exceeds the 63-byte DNS limit is rejected by
  // getaddrinfo locally, without any DNS round-trip.
  chirp::chat::ServerGatewayPeer::Options opts;
  opts.host = std::string(300, 'a');
  opts.port = 8100;
  opts.reconnect_delay_seconds = 1;
  auto peer = chirp::chat::ServerGatewayPeer::Create(
      io, opts, [](const chirp::server_gateway::InjectMessageNotify&) {});
  peer->Start();

  std::this_thread::sleep_for(std::chrono::milliseconds(2500));

  peer->Stop();
  runner.Drain();
  runner.Finish();
  SUCCEED();
}

TEST(ChatHubPeerTest, StopDuringConnectAttemptIsClean) {
  chirp::common::Logger::Instance().SetLevel(chirp::common::Logger::Level::kError);
  asio::io_context io;
  PeerIoRunner runner(io);

  chirp::chat::ServerGatewayPeer::Options opts;
  opts.host = "10.255.255.1";  // blackhole: connect stays pending
  opts.port = 81;
  opts.reconnect_delay_seconds = 1;
  auto peer = chirp::chat::ServerGatewayPeer::Create(
      io, opts, [](const chirp::server_gateway::InjectMessageNotify&) {});
  peer->Start();

  std::this_thread::sleep_for(std::chrono::milliseconds(100));
  peer->Stop();
  runner.Drain();
  runner.Finish();
  SUCCEED();
}

TEST(ChatHubPeerTest, StartAfterStopDoesNothing) {
  chirp::common::Logger::Instance().SetLevel(chirp::common::Logger::Level::kError);
  asio::io_context io;
  auto peer = chirp::chat::ServerGatewayPeer::Create(
      io, chirp::chat::ServerGatewayPeer::Options{},
      [](const chirp::server_gateway::InjectMessageNotify&) {});
  peer->Stop();   // stopped before ever starting
  peer->Start();  // must be a no-op
  while (io.poll() > 0) {
  }
  SUCCEED();
}

}  // namespace
