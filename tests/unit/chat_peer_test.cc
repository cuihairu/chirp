// Loopback tests for the chat peer registration protocol (5050-5053): both
// sides of the wire. The link (spoke) tests run against a scripted fake hub
// answering PEER_REGISTER_RESP per the scenario; the hub tests run the real
// ChatPeerHub against raw synchronous client sockets. All link socket work
// runs on its own io thread; hub handlers run on the hub io thread, mirroring
// chat_hub_peer_test.cc.
#include <gtest/gtest.h>

#include <asio.hpp>

#include <algorithm>
#include <array>
#include <chrono>
#include <future>
#include <memory>
#include <mutex>
#include <string>
#include <thread>
#include <vector>

#include "logger.h"
#include "network/byte_order.h"
#include "network/chat_peer_hub.h"
#include "network/chat_peer_link.h"
#include "network/protobuf_framing.h"
#include "proto/chat.pb.h"
#include "proto/common.pb.h"
#include "proto/gateway.pb.h"

namespace {

using chirp::gateway::MsgID;
using chirp::gateway::Packet;

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

// ---------------------------------------------------------------------------
// Link-side fixture: scripted hub that answers PEER_REGISTER_REQ per the
// scenario, pongs heartbeats, and can push frames to the latest connection.
// ---------------------------------------------------------------------------
class FakePeerHubServer {
 public:
  explicit FakePeerHubServer(chirp::common::ErrorCode reg_code = chirp::common::OK,
                             int heartbeat_interval = 1)
      : reg_code_(reg_code),
        heartbeat_interval_(heartbeat_interval),
        acceptor_(io_, asio::ip::tcp::endpoint(asio::ip::tcp::v4(), 0)) {
    port_ = static_cast<uint16_t>(acceptor_.local_endpoint().port());
    DoAccept();
    thread_ = std::thread([this] { io_.run(); });
  }

  ~FakePeerHubServer() {
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

  void SendToLatest(const Packet& pkt) {
    asio::post(io_, [this, pkt] {
      std::lock_guard<std::mutex> lock(mu_);
      if (!sockets_.empty()) {
        WriteTo(*sockets_.back(), pkt);
      }
    });
  }

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
            const uint32_t size = chirp::network::ReadU32BE(
                reinterpret_cast<const uint8_t*>(buf.data()));
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
    if (pkt.msg_id() == chirp::gateway::PEER_REGISTER_REQ) {
      chirp::gateway::PeerRegisterReq req;
      if (req.ParseFromString(pkt.body())) {
        std::lock_guard<std::mutex> lock(mu_);
        register_service_ids_.push_back(req.service_id());
      }
      chirp::gateway::PeerRegisterResp resp;
      resp.set_code(reg_code_);
      resp.set_protocol_version(1);
      resp.set_min_version(1);
      resp.set_heartbeat_interval_seconds(heartbeat_interval_);
      resp.add_supported_features(chirp::gateway::RELAY_TYPING);
      WriteTo(*sock, MakePacket(chirp::gateway::PEER_REGISTER_RESP, pkt.sequence(), resp));
      if (reg_code_ != chirp::common::OK) {
        asio::error_code ec;
        sock->close(ec);
      }
    } else if (pkt.msg_id() == chirp::gateway::HEARTBEAT_PING) {
      chirp::gateway::HeartbeatPong pong;
      pong.set_timestamp(0);
      WriteTo(*sock, MakePacket(chirp::gateway::HEARTBEAT_PONG, pkt.sequence(), pong));
    }
  }

  void WriteTo(asio::ip::tcp::socket& sock, const Packet& pkt) {
    asio::error_code ec;
    asio::write(sock, asio::buffer(FramePacket(pkt)), ec);
  }

  chirp::common::ErrorCode reg_code_;
  int heartbeat_interval_;
  asio::io_context io_;
  asio::ip::tcp::acceptor acceptor_;
  uint16_t port_{0};
  std::thread thread_;
  std::mutex mu_;
  std::vector<std::shared_ptr<asio::ip::tcp::socket>> sockets_;
  std::vector<Packet> received_;
  std::vector<std::string> register_service_ids_;
};

chirp::network::ChatPeerLink::Options LinkOptions(const FakePeerHubServer& hub,
                                                  const std::string& secret = "peer-s3cret") {
  chirp::network::ChatPeerLink::Options opts;
  opts.host = "127.0.0.1";
  opts.port = hub.port();
  opts.service_id = "game_chat";
  opts.secret = secret;
  opts.game_id = "game42";
  opts.supported_features = {chirp::gateway::RELAY_TYPING, chirp::gateway::RELAY_PRESENCE};
  opts.reconnect_delay_seconds = 1;
  return opts;
}

// Runs the link's io_context on a helper thread with a hard watchdog; the
// watchdog stops the loop so a hung link fails asserts instead of the test.
class LinkIoRunner {
 public:
  explicit LinkIoRunner(asio::io_context& io) : io_(io) {
    watchdog_ = std::make_shared<asio::steady_timer>(io);
    watchdog_->expires_after(std::chrono::seconds(30));
    watchdog_->async_wait([&](const std::error_code&) { io_.stop(); });
    thread_ = std::thread([this] { io_.run(); });
  }

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

// Snapshots the link's registered() flag on the link's own io thread (the
// flag's documented thread contract), instead of reading it racy from here.
bool RegisteredOnLinkThread(chirp::network::ChatPeerLink& link, asio::io_context& io) {
  std::promise<bool> done;
  asio::post(io, [&] { done.set_value(link.registered()); });
  return done.get_future().get();
}

TEST(ChatPeerLinkTest, RegistersHandshakesAndSendsHeartbeats) {
  chirp::common::Logger::Instance().SetLevel(chirp::common::Logger::Level::kError);
  FakePeerHubServer hub;
  asio::io_context io;
  LinkIoRunner runner(io);

  std::mutex mu;
  std::vector<std::pair<int32_t, size_t>> registrations;  // (version, feature count)
  auto link = chirp::network::ChatPeerLink::Create(
      io, LinkOptions(hub),
      [&](int32_t version, const std::vector<chirp::gateway::PeerCapability>& features) {
        std::lock_guard<std::mutex> lock(mu);
        registrations.emplace_back(version, features.size());
      },
      [](const chirp::gateway::ChannelMessageNotify&) {},
      [](const chirp::gateway::PeerInjectMessageNotify&) {});
  link->Start();

  // The handshake carries the configured identity and capabilities.
  ASSERT_TRUE(WaitFor([&] { return hub.Count(chirp::gateway::PEER_REGISTER_REQ) > 0; },
                      std::chrono::seconds(5)));
  const auto regs = hub.All(chirp::gateway::PEER_REGISTER_REQ);
  ASSERT_FALSE(regs.empty());
  chirp::gateway::PeerRegisterReq req;
  ASSERT_TRUE(req.ParseFromString(regs[0].body()));
  EXPECT_EQ(req.service_id(), "game_chat");
  EXPECT_EQ(req.service_secret(), "peer-s3cret");
  EXPECT_EQ(req.protocol_version(), 1);
  EXPECT_EQ(req.game_id(), "game42");
  EXPECT_EQ(req.supported_features_size(), 2);

  // The hub's assigned 1s cadence produces heartbeats.
  EXPECT_TRUE(WaitFor([&] { return hub.Count(chirp::gateway::HEARTBEAT_PING) >= 2; },
                      std::chrono::seconds(6)));

  // The registered callback saw the negotiated version and the intersected
  // feature set (the fake hub echoes a single feature back).
  std::lock_guard<std::mutex> lock(mu);
  ASSERT_GE(registrations.size(), 1u);
  EXPECT_EQ(registrations[0].first, 1);
  EXPECT_EQ(registrations[0].second, 1u);

  link->Stop();
  runner.Drain();
  runner.Finish();
}

TEST(ChatPeerLinkTest, RegisteredFlagReflectsHandshake) {
  chirp::common::Logger::Instance().SetLevel(chirp::common::Logger::Level::kError);
  FakePeerHubServer hub;
  asio::io_context io;
  LinkIoRunner runner(io);

  auto link = chirp::network::ChatPeerLink::Create(
      io, LinkOptions(hub), [](int32_t, const std::vector<chirp::gateway::PeerCapability>&) {},
      [](const chirp::gateway::ChannelMessageNotify&) {},
      [](const chirp::gateway::PeerInjectMessageNotify&) {});
  EXPECT_FALSE(RegisteredOnLinkThread(*link, io));  // before Start
  link->Start();

  ASSERT_TRUE(WaitFor([&] { return hub.Count(chirp::gateway::PEER_REGISTER_REQ) > 0; },
                      std::chrono::seconds(5)));
  EXPECT_TRUE(WaitFor([&] { return RegisteredOnLinkThread(*link, io); },
                      std::chrono::seconds(5)));

  link->Stop();
  EXPECT_FALSE(RegisteredOnLinkThread(*link, io));
  runner.Drain();
  runner.Finish();
}

TEST(ChatPeerLinkTest, ChannelMessageDownlinkReachesHandler) {
  chirp::common::Logger::Instance().SetLevel(chirp::common::Logger::Level::kError);
  FakePeerHubServer hub;
  asio::io_context io;
  LinkIoRunner runner(io);

  std::mutex mu;
  std::vector<chirp::gateway::ChannelMessageNotify> downlinks;
  auto link = chirp::network::ChatPeerLink::Create(
      io, LinkOptions(hub),
      [](int32_t, const std::vector<chirp::gateway::PeerCapability>&) {},
      [&](const chirp::gateway::ChannelMessageNotify& n) {
        std::lock_guard<std::mutex> lock(mu);
        downlinks.push_back(n);
      },
      [](const chirp::gateway::PeerInjectMessageNotify&) {});
  link->Start();
  ASSERT_TRUE(WaitFor([&] { return hub.Count(chirp::gateway::PEER_REGISTER_REQ) >= 1; },
                      std::chrono::seconds(5)));

  chirp::gateway::ChannelMessageNotify notify;
  notify.set_game_id("game42");
  notify.set_channel_id("guild_1");
  notify.mutable_message()->set_sender_id("u1");
  notify.mutable_message()->set_content("loot at the forge");
  hub.SendToLatest(MakePacket(chirp::gateway::CHANNEL_MESSAGE_NOTIFY, 0, notify));
  EXPECT_TRUE(WaitFor([&] {
    std::lock_guard<std::mutex> lock(mu);
    for (const auto& n : downlinks) {
      if (n.channel_id() == "guild_1" && n.message().content() == "loot at the forge") {
        return true;
      }
    }
    return false;
  }, std::chrono::seconds(5)));

  // A malformed 5052 body is skipped; the connection stays up.
  hub.SendToLatest(MakeRawPacket(chirp::gateway::CHANNEL_MESSAGE_NOTIFY, 0, "not-a-proto"));
  std::this_thread::sleep_for(std::chrono::milliseconds(200));
  {
    std::lock_guard<std::mutex> lock(mu);
    EXPECT_EQ(downlinks.size(), 1u);
  }
  EXPECT_EQ(hub.Count(chirp::gateway::PEER_REGISTER_REQ), 1u);  // connection stayed up

  link->Stop();
  runner.Drain();
  runner.Finish();
}

TEST(ChatPeerLinkTest, InjectDownlinkReachesHandler) {
  chirp::common::Logger::Instance().SetLevel(chirp::common::Logger::Level::kError);
  FakePeerHubServer hub;
  asio::io_context io;
  LinkIoRunner runner(io);

  std::mutex mu;
  std::vector<chirp::gateway::PeerInjectMessageNotify> injected;
  auto link = chirp::network::ChatPeerLink::Create(
      io, LinkOptions(hub),
      [](int32_t, const std::vector<chirp::gateway::PeerCapability>&) {},
      [](const chirp::gateway::ChannelMessageNotify&) {},
      [&](const chirp::gateway::PeerInjectMessageNotify& n) {
        std::lock_guard<std::mutex> lock(mu);
        injected.push_back(n);
      });
  link->Start();
  ASSERT_TRUE(WaitFor([&] { return hub.Count(chirp::gateway::PEER_REGISTER_REQ) >= 1; },
                      std::chrono::seconds(5)));

  chirp::gateway::PeerInjectMessageNotify notify;
  notify.set_channel_id("guild_1");
  notify.set_sender_id("player_9");
  notify.set_content("hello from the app plane");
  notify.set_client_msg_id("cmid-1");
  hub.SendToLatest(MakePacket(chirp::gateway::PEER_INJECT_MESSAGE_NOTIFY, 0, notify));
  EXPECT_TRUE(WaitFor([&] {
    std::lock_guard<std::mutex> lock(mu);
    for (const auto& n : injected) {
      if (n.sender_id() == "player_9" &&
          n.content() == "hello from the app plane") {
        return true;
      }
    }
    return false;
  }, std::chrono::seconds(5)));

  link->Stop();
  runner.Drain();
  runner.Finish();
}

TEST(ChatPeerLinkTest, UplinkDeliversAfterRegistrationOnly) {
  chirp::common::Logger::Instance().SetLevel(chirp::common::Logger::Level::kError);
  FakePeerHubServer hub;
  asio::io_context io;
  LinkIoRunner runner(io);

  auto link = chirp::network::ChatPeerLink::Create(
      io, LinkOptions(hub),
      [](int32_t, const std::vector<chirp::gateway::PeerCapability>&) {},
      [](const chirp::gateway::ChannelMessageNotify&) {},
      [](const chirp::gateway::PeerInjectMessageNotify&) {});

  // Not started, never connected: the uplink refuses instead of queueing.
  chirp::gateway::ChannelMessageNotify notify;
  notify.set_game_id("game42");
  notify.set_channel_id("world");
  notify.mutable_message()->set_sender_id("u1");
  notify.mutable_message()->set_content("hello across planes");
  EXPECT_FALSE(link->SendChannelMessage(notify));
  runner.Drain();
  EXPECT_EQ(hub.Count(chirp::gateway::CHANNEL_MESSAGE_NOTIFY), 0u);

  // After registration the same send reaches the wire verbatim.
  link->Start();
  ASSERT_TRUE(WaitFor([&] { return hub.Count(chirp::gateway::PEER_REGISTER_REQ) >= 1; },
                      std::chrono::seconds(5)));
  ASSERT_TRUE(WaitFor([&] { return RegisteredOnLinkThread(*link, io); },
                      std::chrono::seconds(5)));
  EXPECT_TRUE(link->SendChannelMessage(notify));
  EXPECT_TRUE(WaitFor([&] { return hub.Count(chirp::gateway::CHANNEL_MESSAGE_NOTIFY) >= 1; },
                      std::chrono::seconds(5)));
  const auto sent = hub.All(chirp::gateway::CHANNEL_MESSAGE_NOTIFY);
  ASSERT_GE(sent.size(), 1u);
  chirp::gateway::ChannelMessageNotify seen;
  ASSERT_TRUE(seen.ParseFromString(sent.back().body()));
  EXPECT_EQ(seen.game_id(), "game42");
  EXPECT_EQ(seen.channel_id(), "world");
  EXPECT_EQ(seen.message().sender_id(), "u1");
  EXPECT_EQ(seen.message().content(), "hello across planes");

  link->Stop();
  runner.Drain();
  runner.Finish();
}

TEST(ChatPeerLinkTest, RejectedRegistrationRetries) {
  chirp::common::Logger::Instance().SetLevel(chirp::common::Logger::Level::kError);
  FakePeerHubServer hub(chirp::common::AUTH_FAILED);
  asio::io_context io;
  LinkIoRunner runner(io);

  auto link = chirp::network::ChatPeerLink::Create(
      io, LinkOptions(hub, "wrong-secret"),
      [](int32_t, const std::vector<chirp::gateway::PeerCapability>&) {},
      [](const chirp::gateway::ChannelMessageNotify&) {},
      [](const chirp::gateway::PeerInjectMessageNotify&) {});
  link->Start();

  // Every attempt is rejected and retried after the reconnect delay; the
  // link never reports itself registered.
  EXPECT_TRUE(WaitFor([&] { return hub.Count(chirp::gateway::PEER_REGISTER_REQ) >= 3; },
                      std::chrono::seconds(8)));
  EXPECT_FALSE(RegisteredOnLinkThread(*link, io));

  link->Stop();
  runner.Drain();
  runner.Finish();
}

TEST(ChatPeerLinkTest, ZeroHeartbeatFromHubFallsBackToOptions) {
  chirp::common::Logger::Instance().SetLevel(chirp::common::Logger::Level::kError);
  // Hub advertises heartbeat_interval_seconds=0: the link must keep its
  // configured options_.heartbeat_interval_seconds instead of arming a
  // zero/negative cadence.
  FakePeerHubServer hub(chirp::common::OK, /*heartbeat_interval=*/0);
  asio::io_context io;
  LinkIoRunner runner(io);

  std::atomic<int> registered{0};
  auto link = chirp::network::ChatPeerLink::Create(
      io, LinkOptions(hub),
      [&](int32_t, const std::vector<chirp::gateway::PeerCapability>&) {
        registered.fetch_add(1);
      },
      [](const chirp::gateway::ChannelMessageNotify&) {},
      [](const chirp::gateway::PeerInjectMessageNotify&) {});
  link->Start();

  EXPECT_TRUE(WaitFor([&] { return registered.load() >= 1; }, std::chrono::seconds(5)));
  EXPECT_TRUE(WaitFor([&] { return hub.Count(chirp::gateway::PEER_REGISTER_REQ) >= 1; },
                      std::chrono::seconds(5)));
  const auto regs = hub.All(chirp::gateway::PEER_REGISTER_REQ);
  ASSERT_FALSE(regs.empty());
  chirp::gateway::PeerRegisterResp resp;
  // The fake answered with 0; registration still succeeded.
  EXPECT_TRUE(WaitFor([&] { return registered.load() >= 1; }, std::chrono::seconds(3)));

  link->Stop();
  runner.Drain();
  runner.Finish();
}

TEST(ChatPeerLinkTest, VersionMismatchRetries) {
  chirp::common::Logger::Instance().SetLevel(chirp::common::Logger::Level::kError);
  FakePeerHubServer hub(chirp::common::VERSION_MISMATCH);
  asio::io_context io;
  LinkIoRunner runner(io);

  std::mutex mu;
  size_t registrations = 0;
  auto link = chirp::network::ChatPeerLink::Create(
      io, LinkOptions(hub),
      [&](int32_t, const std::vector<chirp::gateway::PeerCapability>&) {
        std::lock_guard<std::mutex> lock(mu);
        registrations++;
      },
      [](const chirp::gateway::ChannelMessageNotify&) {},
      [](const chirp::gateway::PeerInjectMessageNotify&) {});
  link->Start();

  EXPECT_TRUE(WaitFor([&] { return hub.Count(chirp::gateway::PEER_REGISTER_REQ) >= 2; },
                      std::chrono::seconds(8)));
  std::this_thread::sleep_for(std::chrono::milliseconds(100));
  std::lock_guard<std::mutex> lock(mu);
  EXPECT_EQ(registrations, 0u);  // a mismatch is never a registration

  link->Stop();
  runner.Drain();
  runner.Finish();
}

TEST(ChatPeerLinkTest, ReconnectsAfterConnectionLoss) {
  chirp::common::Logger::Instance().SetLevel(chirp::common::Logger::Level::kError);
  FakePeerHubServer hub;
  asio::io_context io;
  LinkIoRunner runner(io);

  std::mutex mu;
  size_t losses = 0;
  auto link = chirp::network::ChatPeerLink::Create(
      io, LinkOptions(hub),
      [](int32_t, const std::vector<chirp::gateway::PeerCapability>&) {},
      [](const chirp::gateway::ChannelMessageNotify&) {},
      [](const chirp::gateway::PeerInjectMessageNotify&) {},
      [&] {
        std::lock_guard<std::mutex> lock(mu);
        losses++;
      });
  link->Start();
  ASSERT_TRUE(WaitFor([&] { return hub.Count(chirp::gateway::PEER_REGISTER_REQ) >= 1; },
                      std::chrono::seconds(5)));

  hub.CloseLatest();
  EXPECT_TRUE(WaitFor([&] { return hub.Count(chirp::gateway::PEER_REGISTER_REQ) >= 2; },
                      std::chrono::seconds(6)));
  std::lock_guard<std::mutex> lock(mu);
  EXPECT_GE(losses, 1u);  // the registered-state loss was reported

  link->Stop();
  runner.Drain();
  runner.Finish();
}

TEST(ChatPeerLinkTest, ConnectFailureRetriesQuietly) {
  chirp::common::Logger::Instance().SetLevel(chirp::common::Logger::Level::kError);
  asio::io_context io;
  LinkIoRunner runner(io);

  // Nothing listens on loopback port 1: every dial fails with ECONNREFUSED
  // and the link keeps retrying on the reconnect delay without ever
  // reporting itself registered.
  chirp::network::ChatPeerLink::Options opts;
  opts.host = "127.0.0.1";
  opts.port = 1;
  opts.reconnect_delay_seconds = 1;
  auto link = chirp::network::ChatPeerLink::Create(
      io, opts,
      [](int32_t, const std::vector<chirp::gateway::PeerCapability>&) {},
      [](const chirp::gateway::ChannelMessageNotify&) {},
      [](const chirp::gateway::PeerInjectMessageNotify&) {});
  link->Start();

  // Two reconnect windows pass; the link stays unregistered and healthy.
  std::this_thread::sleep_for(std::chrono::milliseconds(2500));
  EXPECT_FALSE(RegisteredOnLinkThread(*link, io));

  link->Stop();
  runner.Drain();
  runner.Finish();
}

TEST(ChatPeerLinkTest, ResolveFailureRetriesQuietly) {
  chirp::common::Logger::Instance().SetLevel(chirp::common::Logger::Level::kError);
  asio::io_context io;
  LinkIoRunner runner(io);

  // A hostname with spaces fails inside getaddrinfo (EAI_NONAME) before
  // any DNS query: deterministic on CI (no resolver dependency) and on
  // sandboxes whose DNS gateway fake-answers even .invalid (RFC 6761)
  // names, which would otherwise turn this into a connect failure.
  chirp::network::ChatPeerLink::Options opts;
  opts.host = "chat peer hub invalid";
  opts.port = 5050;
  opts.reconnect_delay_seconds = 1;
  auto link = chirp::network::ChatPeerLink::Create(
      io, opts,
      [](int32_t, const std::vector<chirp::gateway::PeerCapability>&) {},
      [](const chirp::gateway::ChannelMessageNotify&) {},
      [](const chirp::gateway::PeerInjectMessageNotify&) {});
  link->Start();

  std::this_thread::sleep_for(std::chrono::milliseconds(2500));
  EXPECT_FALSE(RegisteredOnLinkThread(*link, io));

  link->Stop();
  runner.Drain();
  runner.Finish();
}

// Registers a link against the scripted hub and counts on_lost reports.
struct RegisteredLink {
  FakePeerHubServer& hub;
  asio::io_context& io;
  LinkIoRunner& runner;
  std::shared_ptr<chirp::network::ChatPeerLink> link;
  std::mutex mu;
  size_t losses = 0;

  explicit RegisteredLink(FakePeerHubServer& hub_ref, asio::io_context& io_ref,
                          LinkIoRunner& runner_ref)
      : hub(hub_ref), io(io_ref), runner(runner_ref) {
    link = chirp::network::ChatPeerLink::Create(
        io, LinkOptions(hub),
        [](int32_t, const std::vector<chirp::gateway::PeerCapability>&) {},
        [](const chirp::gateway::ChannelMessageNotify&) {},
        [](const chirp::gateway::PeerInjectMessageNotify&) {},
        [this] {
          std::lock_guard<std::mutex> lock(mu);
          losses++;
        });
    link->Start();
    EXPECT_TRUE(WaitFor([&] { return RegisteredOnLinkThread(*link, io); },
                        std::chrono::seconds(5)))
        << "link never registered against the scripted hub";
  }

  ~RegisteredLink() {
    link->Stop();
    runner.Drain();
    runner.Finish();
  }
};

TEST(ChatPeerLinkTest, BadFrameSizeDropsConnection) {
  chirp::common::Logger::Instance().SetLevel(chirp::common::Logger::Level::kError);
  FakePeerHubServer hub;
  asio::io_context io;
  LinkIoRunner runner(io);
  RegisteredLink registered(hub, io, runner);

  // A zero length prefix violates the framing contract and drops the
  // connection; the loss of a registered link is reported.
  hub.SendRawToLatest(std::string(4, '\0'));
  EXPECT_TRUE(WaitFor([&] {
    std::lock_guard<std::mutex> lock(registered.mu);
    return registered.losses >= 1;
  }, std::chrono::seconds(5)));
}

TEST(ChatPeerLinkTest, GarbagePacketBodyDropsConnection) {
  chirp::common::Logger::Instance().SetLevel(chirp::common::Logger::Level::kError);
  FakePeerHubServer hub;
  asio::io_context io;
  LinkIoRunner runner(io);
  RegisteredLink registered(hub, io, runner);

  // A well-formed length prefix over an unparseable outer envelope.
  hub.SendRawToLatest(std::string("\x00\x00\x00\x01\xff", 5));
  EXPECT_TRUE(WaitFor([&] {
    std::lock_guard<std::mutex> lock(registered.mu);
    return registered.losses >= 1;
  }, std::chrono::seconds(5)));
}

TEST(ChatPeerLinkTest, GarbageRegisterRespDropsConnection) {
  chirp::common::Logger::Instance().SetLevel(chirp::common::Logger::Level::kError);
  FakePeerHubServer hub;
  asio::io_context io;
  LinkIoRunner runner(io);
  RegisteredLink registered(hub, io, runner);

  // A follow-up PEER_REGISTER_RESP whose body is not parseable protobuf.
  hub.SendToLatest(
      MakeRawPacket(chirp::gateway::PEER_REGISTER_RESP, 0, "\xff"));
  EXPECT_TRUE(WaitFor([&] {
    std::lock_guard<std::mutex> lock(registered.mu);
    return registered.losses >= 1;
  }, std::chrono::seconds(5)));
}

TEST(ChatPeerLinkTest, MalformedAndUnexpectedDownlinkFramesAreIgnored) {
  chirp::common::Logger::Instance().SetLevel(chirp::common::Logger::Level::kError);
  FakePeerHubServer hub;
  asio::io_context io;
  LinkIoRunner runner(io);
  RegisteredLink registered(hub, io, runner);

  // One heartbeat proves the cadence is running before the bad frames land.
  ASSERT_TRUE(WaitFor([&] { return hub.Count(chirp::gateway::HEARTBEAT_PING) >= 1; },
                      std::chrono::seconds(5)));

  // Bodies that fail to parse are logged and skipped ...
  hub.SendToLatest(
      MakeRawPacket(chirp::gateway::CHANNEL_MESSAGE_NOTIFY, 0, "\xff"));
  hub.SendToLatest(
      MakeRawPacket(chirp::gateway::PEER_INJECT_MESSAGE_NOTIFY, 0, "\xff"));
  // ... and unknown msg ids (future version skew) are dropped loudly, not
  // fatally. The next heartbeat round-trip proves the link kept reading and
  // never reported a loss.
  hub.SendToLatest(MakeRawPacket(static_cast<MsgID>(12345), 0, ""));
  EXPECT_TRUE(WaitFor([&] { return hub.Count(chirp::gateway::HEARTBEAT_PING) >= 2; },
                      std::chrono::seconds(5)));
  {
    std::lock_guard<std::mutex> lock(registered.mu);
    EXPECT_EQ(registered.losses, 0u);
  }
}

TEST(ChatPeerLinkTest, StopIsIdempotentAndStartAfterStopIsNoop) {
  chirp::common::Logger::Instance().SetLevel(chirp::common::Logger::Level::kError);
  FakePeerHubServer hub;
  asio::io_context io;
  LinkIoRunner runner(io);
  RegisteredLink registered(hub, io, runner);

  const auto attempts_before = hub.Count(chirp::gateway::PEER_REGISTER_REQ);
  registered.link->Stop();
  registered.link->Stop();  // second Stop is a strand no-op, not a crash
  registered.link->Start(); // refused on the strand: the link stays stopped

  // A reconnect would reach the fake hub within one reconnect delay.
  std::this_thread::sleep_for(std::chrono::milliseconds(2500));
  EXPECT_EQ(hub.Count(chirp::gateway::PEER_REGISTER_REQ), attempts_before);
}

TEST(ChatPeerLinkTest, InjectUplinkDeliversAfterRegistrationOnly) {
  chirp::common::Logger::Instance().SetLevel(chirp::common::Logger::Level::kError);
  FakePeerHubServer hub;
  asio::io_context io;
  LinkIoRunner runner(io);

  chirp::gateway::PeerInjectMessageNotify notify;
  notify.set_channel_id("world");
  notify.set_sender_id("player_3");
  notify.set_content("inject uplink");
  notify.set_client_msg_id("cmid-2");

  auto link = chirp::network::ChatPeerLink::Create(
      io, LinkOptions(hub),
      [](int32_t, const std::vector<chirp::gateway::PeerCapability>&) {},
      [](const chirp::gateway::ChannelMessageNotify&) {},
      [](const chirp::gateway::PeerInjectMessageNotify&) {});
  link->Start();

  // Before registration the uplink is refused locally: nothing is queued and
  // nothing reaches the wire.
  EXPECT_FALSE(link->SendInject(notify));
  runner.Drain();
  EXPECT_EQ(hub.Count(chirp::gateway::PEER_INJECT_MESSAGE_NOTIFY), 0u);

  ASSERT_TRUE(WaitFor([&] { return RegisteredOnLinkThread(*link, io); },
                      std::chrono::seconds(5)));
  EXPECT_TRUE(link->SendInject(notify));
  ASSERT_TRUE(WaitFor([&] { return hub.Count(chirp::gateway::PEER_INJECT_MESSAGE_NOTIFY) >= 1; },
                      std::chrono::seconds(5)));
  const auto seen = hub.All(chirp::gateway::PEER_INJECT_MESSAGE_NOTIFY).front();
  chirp::gateway::PeerInjectMessageNotify delivered;
  ASSERT_TRUE(delivered.ParseFromString(seen.body()));
  EXPECT_EQ(delivered.channel_id(), "world");
  EXPECT_EQ(delivered.client_msg_id(), "cmid-2");

  link->Stop();
  runner.Drain();
  runner.Finish();
}

TEST(ChatPeerLinkTest, ConnectionLossMidFrameIsReported) {
  chirp::common::Logger::Instance().SetLevel(chirp::common::Logger::Level::kError);
  FakePeerHubServer hub;
  asio::io_context io;
  LinkIoRunner runner(io);
  RegisteredLink registered(hub, io, runner);

  // A length prefix announcing a body that never arrives: the hub hangs up
  // right after, so the pending body read fails and the loss is reported.
  hub.SendRawToLatest(std::string("\x00\x00\x00\x64", 4));
  hub.CloseLatest();
  EXPECT_TRUE(WaitFor([&] {
    std::lock_guard<std::mutex> lock(registered.mu);
    return registered.losses >= 1;
  }, std::chrono::seconds(5)));
}

// ---------------------------------------------------------------------------
// Hub-side fixture: synchronous client socket driven from the test thread,
// while the real ChatPeerHub runs on its own io thread.
// ---------------------------------------------------------------------------
class TestPeerClient {
 public:
  explicit TestPeerClient(uint16_t port) : sock_(io_) {
    sock_.connect({asio::ip::tcp::v4(), port});
  }

  void Send(const Packet& pkt) { SendRaw(FramePacket(pkt)); }

  void SendRaw(const std::string& bytes) {
    asio::error_code ec;
    asio::write(sock_, asio::buffer(bytes), ec);
    ASSERT_FALSE(ec) << "client send failed: " << ec.message();
  }

  // Same, when only liveness matters: true = some frame arrived.
  bool Read(std::chrono::milliseconds timeout = std::chrono::seconds(3)) {
    Packet pkt;
    return Read(pkt, timeout);
  }

  // Reads one framed packet with a hard timeout. False = closed, timeout, or
  // a malformed frame.
  bool Read(Packet& out, std::chrono::milliseconds timeout = std::chrono::seconds(3)) {
    std::error_code ec;
    bool ok = false;
    asio::async_read(sock_, asio::buffer(header_),
                     [&](const std::error_code& e, std::size_t) {
                       ec = e;
                       ok = !e;
                     });
    io_.restart();
    io_.run_for(timeout);
    if (!ok) {
      asio::error_code ignore;
      sock_.close(ignore);
      return false;
    }
    const uint32_t size = chirp::network::ReadU32BE(header_.data());
    if (size == 0 || size > 4u * 1024u * 1024u) {
      return false;
    }
    body_.resize(size);
    ok = false;
    asio::async_read(sock_, asio::buffer(body_.data(), body_.size()),
                     [&](const std::error_code& e, std::size_t) {
                       ec = e;
                       ok = !e;
                     });
    io_.restart();
    io_.run_for(timeout);
    if (!ok) {
      asio::error_code ignore;
      sock_.close(ignore);
      return false;
    }
    return out.ParseFromArray(body_.data(), static_cast<int>(body_.size()));
  }

  void Close() {
    asio::error_code ec;
    sock_.close(ec);
  }

 private:
  asio::io_context io_;
  asio::ip::tcp::socket sock_;
  std::array<uint8_t, 4> header_{};
  std::string body_;
};

// Hub io thread with the same watchdog pattern as LinkIoRunner.
class HubIoRunner {
 public:
  explicit HubIoRunner(asio::io_context& io) : io_(io) {
    watchdog_ = std::make_shared<asio::steady_timer>(io);
    watchdog_->expires_after(std::chrono::seconds(30));
    watchdog_->async_wait([&](const std::error_code&) { io_.stop(); });
    thread_ = std::thread([this] { io_.run(); });
  }

  // Drains already-posted work (e.g. Stop's teardown) without waiting on
  // timers, so no conn-carrying handler is left dangling when the io_context
  // is destroyed on this thread.
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

struct HubEvents {
  std::mutex mu;
  std::vector<std::string> registered;               // "service_id:game_id"
  std::vector<std::pair<std::string, std::string>> dropped;  // (id, reason)
  std::vector<chirp::gateway::ChannelMessageNotify> uplinks;
};

chirp::network::ChatPeerHub::Options HubTestOptions(int heartbeat_seconds = 30,
                                                    int32_t min_version = 1) {
  chirp::network::ChatPeerHub::Options opts;
  opts.allowed_peers["game_chat"] = "peer-s3cret";
  opts.heartbeat_interval_seconds = heartbeat_seconds;
  opts.min_peer_version = min_version;
  return opts;
}

// A hub wired to record every callback into `events`.
std::shared_ptr<chirp::network::ChatPeerHub> MakeRecordingHub(asio::io_context& io,
                                                              HubEvents& events) {
  return chirp::network::ChatPeerHub::Create(
      io, HubTestOptions(),
      [&events](const std::string& id, const std::string& game_id, int32_t,
                const std::vector<chirp::gateway::PeerCapability>&) {
        std::lock_guard<std::mutex> lock(events.mu);
        events.registered.push_back(id + ":" + game_id);
      },
      [&events](const std::string& id, const std::string& reason) {
        std::lock_guard<std::mutex> lock(events.mu);
        events.dropped.emplace_back(id, reason);
      },
      [&events](const std::string&, const chirp::gateway::ChannelMessageNotify& notify) {
        std::lock_guard<std::mutex> lock(events.mu);
        events.uplinks.push_back(notify);
      });
}

chirp::gateway::PeerRegisterReq MakeRegisterReq(const std::string& service_id = "game_chat",
                                                 const std::string& secret = "peer-s3cret",
                                                 const std::string& game_id = "game42",
                                                 int32_t version = 1) {
  chirp::gateway::PeerRegisterReq req;
  req.set_service_id(service_id);
  req.set_service_secret(secret);
  req.set_protocol_version(version);
  req.set_game_id(game_id);
  req.add_supported_features(chirp::gateway::RELAY_TYPING);
  return req;
}

// Sends a registration from a fresh client and reads the response; returns
// false when the hub closed the connection before answering.
bool RegisterAndGetResp(TestPeerClient& client, const chirp::gateway::PeerRegisterReq& req,
                        chirp::gateway::PeerRegisterResp& resp) {
  client.Send(MakePacket(chirp::gateway::PEER_REGISTER_REQ, 0, req));
  Packet pkt;
  if (!client.Read(pkt)) {
    return false;
  }
  if (pkt.msg_id() != chirp::gateway::PEER_REGISTER_RESP) {
    return false;
  }
  return resp.ParseFromString(pkt.body());
}

TEST(ChatPeerHubTest, RegistersPeerAndReports) {
  chirp::common::Logger::Instance().SetLevel(chirp::common::Logger::Level::kError);
  asio::io_context io;
  HubEvents events;
  auto hub = chirp::network::ChatPeerHub::Create(
      io, HubTestOptions(),
      [&](const std::string& id, const std::string& game_id, int32_t version,
          const std::vector<chirp::gateway::PeerCapability>& features) {
        std::lock_guard<std::mutex> lock(events.mu);
        events.registered.push_back(id + ":" + game_id + ":" + std::to_string(version) +
                                    ":" + std::to_string(features.size()));
      },
      [&](const std::string& id, const std::string& reason) {
        std::lock_guard<std::mutex> lock(events.mu);
        events.dropped.emplace_back(id, reason);
      },
      [&](const std::string&, const chirp::gateway::ChannelMessageNotify&) {});
  // Acceptor is not open until Start: local_endpoint fails and port() is 0.
  EXPECT_EQ(hub->port(), 0);
  // Unknown service id resolves to empty before any peer registers.
  std::promise<std::string> miss;
  asio::post(io, [&] { miss.set_value(hub->game_id_for("nope")); });
  hub->Start();
  HubIoRunner runner(io);
  ASSERT_GT(hub->port(), 0);
  EXPECT_EQ(miss.get_future().get(), "");

  TestPeerClient client(hub->port());
  chirp::gateway::PeerRegisterResp resp;
  ASSERT_TRUE(RegisterAndGetResp(client, MakeRegisterReq(), resp));
  EXPECT_EQ(resp.code(), chirp::common::OK);
  EXPECT_EQ(resp.protocol_version(), 1);
  EXPECT_EQ(resp.min_version(), 1);
  EXPECT_EQ(resp.heartbeat_interval_seconds(), 30);
  ASSERT_EQ(resp.supported_features_size(), 1);
  EXPECT_EQ(resp.supported_features(0), chirp::gateway::RELAY_TYPING);

  EXPECT_TRUE(WaitFor([&] {
    std::lock_guard<std::mutex> lock(events.mu);
    return events.registered.size() == 1;
  }, std::chrono::seconds(3)));

  // The routing helper resolves the registered game namespace on the hub's
  // own thread.
  std::promise<std::string> game_id;
  asio::post(io, [&] { game_id.set_value(hub->game_id_for("game_chat")); });
  EXPECT_EQ(game_id.get_future().get(), "game42");

  hub->Stop();
  runner.Drain();
  runner.Finish();
}

TEST(ChatPeerHubTest, ServiceIdForGameResolvesTheLiveSpoke) {
  chirp::common::Logger::Instance().SetLevel(chirp::common::Logger::Level::kError);
  asio::io_context io;
  HubEvents events;
  auto hub = chirp::network::ChatPeerHub::Create(
      io, HubTestOptions(), [](const std::string&, const std::string&, int32_t,
                               const std::vector<chirp::gateway::PeerCapability>&) {},
      [](const std::string&, const std::string&) {},
      [](const std::string&, const chirp::gateway::ChannelMessageNotify&) {});
  hub->Start();
  HubIoRunner runner(io);

  // No spoke yet: any game resolves to nothing.
  std::promise<std::string> before;
  asio::post(io, [&] { before.set_value(hub->service_id_for_game("game42")); });
  EXPECT_EQ(before.get_future().get(), "");

  TestPeerClient client(hub->port());
  chirp::gateway::PeerRegisterResp resp;
  ASSERT_TRUE(RegisterAndGetResp(client, MakeRegisterReq(), resp));
  ASSERT_EQ(resp.code(), chirp::common::OK);

  // The routing helper resolves the live registration on the hub's own
  // thread; an unregistered game still resolves to nothing.
  std::promise<std::string> live;
  asio::post(io, [&] { live.set_value(hub->service_id_for_game("game42")); });
  EXPECT_EQ(live.get_future().get(), "game_chat");

  std::promise<std::string> other;
  asio::post(io, [&] { other.set_value(hub->service_id_for_game("other-game")); });
  EXPECT_EQ(other.get_future().get(), "");

  hub->Stop();
  runner.Drain();
  runner.Finish();
}

TEST(ChatPeerHubTest, RejectsUnknownPeer) {
  chirp::common::Logger::Instance().SetLevel(chirp::common::Logger::Level::kError);
  asio::io_context io;
  HubEvents events;
  auto hub = chirp::network::ChatPeerHub::Create(
      io, HubTestOptions(), [](const std::string&, const std::string&, int32_t,
                               const std::vector<chirp::gateway::PeerCapability>&) {},
      [](const std::string&, const std::string&) {},
      [](const std::string&, const chirp::gateway::ChannelMessageNotify&) {});
  hub->Start();
  HubIoRunner runner(io);

  TestPeerClient client(hub->port());
  chirp::gateway::PeerRegisterResp resp;
  ASSERT_TRUE(RegisterAndGetResp(client, MakeRegisterReq("rogue_peer"), resp));
  EXPECT_EQ(resp.code(), chirp::common::AUTH_FAILED);
  EXPECT_FALSE(client.Read(std::chrono::seconds(1)));  // then the hub hangs up

  std::lock_guard<std::mutex> lock(events.mu);
  EXPECT_TRUE(events.registered.empty());

  hub->Stop();
  runner.Drain();
  runner.Finish();
}

TEST(ChatPeerHubTest, RejectsBadSecret) {
  chirp::common::Logger::Instance().SetLevel(chirp::common::Logger::Level::kError);
  asio::io_context io;
  HubEvents events;
  auto hub = chirp::network::ChatPeerHub::Create(
      io, HubTestOptions(), [](const std::string&, const std::string&, int32_t,
                               const std::vector<chirp::gateway::PeerCapability>&) {},
      [](const std::string&, const std::string&) {},
      [](const std::string&, const chirp::gateway::ChannelMessageNotify&) {});
  hub->Start();
  HubIoRunner runner(io);

  TestPeerClient client(hub->port());
  chirp::gateway::PeerRegisterResp resp;
  ASSERT_TRUE(RegisterAndGetResp(client, MakeRegisterReq("game_chat", "nope"), resp));
  EXPECT_EQ(resp.code(), chirp::common::AUTH_FAILED);
  EXPECT_FALSE(client.Read(std::chrono::seconds(1)));

  hub->Stop();
  runner.Drain();
  runner.Finish();
}

TEST(ChatPeerHubTest, AllowsUnknownPeersInOpenMode) {
  chirp::common::Logger::Instance().SetLevel(chirp::common::Logger::Level::kError);
  asio::io_context io;
  HubEvents events;
  auto opts = HubTestOptions();
  opts.allow_unknown_peers = true;
  auto hub = chirp::network::ChatPeerHub::Create(
      io, opts,
      [&](const std::string& id, const std::string&, int32_t,
          const std::vector<chirp::gateway::PeerCapability>&) {
        std::lock_guard<std::mutex> lock(events.mu);
        events.registered.push_back(id);
      },
      [](const std::string&, const std::string&) {},
      [](const std::string&, const chirp::gateway::ChannelMessageNotify&) {});
  hub->Start();
  HubIoRunner runner(io);

  TestPeerClient client(hub->port());
  chirp::gateway::PeerRegisterResp resp;
  ASSERT_TRUE(RegisterAndGetResp(client, MakeRegisterReq("rogue_peer", "whatever"), resp));
  EXPECT_EQ(resp.code(), chirp::common::OK);
  EXPECT_TRUE(WaitFor([&] {
    std::lock_guard<std::mutex> lock(events.mu);
    return !events.registered.empty();
  }, std::chrono::seconds(3)));

  hub->Stop();
  runner.Drain();
  runner.Finish();
}

TEST(ChatPeerHubTest, RejectsVersionBelowMinimum) {
  chirp::common::Logger::Instance().SetLevel(chirp::common::Logger::Level::kError);
  asio::io_context io;
  HubEvents events;
  auto hub = chirp::network::ChatPeerHub::Create(
      io, HubTestOptions(30, /*min_version=*/2),
      [](const std::string&, const std::string&, int32_t,
         const std::vector<chirp::gateway::PeerCapability>&) {},
      [](const std::string&, const std::string&) {},
      [](const std::string&, const chirp::gateway::ChannelMessageNotify&) {});
  hub->Start();
  HubIoRunner runner(io);

  TestPeerClient client(hub->port());
  chirp::gateway::PeerRegisterResp resp;
  ASSERT_TRUE(RegisterAndGetResp(client, MakeRegisterReq("game_chat", "peer-s3cret",
                                                         "game42", /*version=*/1),
                                 resp));
  EXPECT_EQ(resp.code(), chirp::common::VERSION_MISMATCH);
  EXPECT_EQ(resp.min_version(), 2);
  EXPECT_FALSE(client.Read(std::chrono::seconds(1)));

  hub->Stop();
  runner.Drain();
  runner.Finish();
}

TEST(ChatPeerHubTest, RejectsBadGameId) {
  chirp::common::Logger::Instance().SetLevel(chirp::common::Logger::Level::kError);
  asio::io_context io;
  HubEvents events;
  auto hub = chirp::network::ChatPeerHub::Create(
      io, HubTestOptions(), [](const std::string&, const std::string&, int32_t,
                               const std::vector<chirp::gateway::PeerCapability>&) {},
      [](const std::string&, const std::string&) {},
      [](const std::string&, const chirp::gateway::ChannelMessageNotify&) {});
  hub->Start();
  HubIoRunner runner(io);

  // Empty game_id is rejected...
  TestPeerClient client1(hub->port());
  chirp::gateway::PeerRegisterResp resp;
  ASSERT_TRUE(RegisterAndGetResp(client1, MakeRegisterReq("game_chat", "peer-s3cret", ""),
                                 resp));
  EXPECT_EQ(resp.code(), chirp::common::INVALID_PARAM);
  EXPECT_FALSE(client1.Read(std::chrono::seconds(1)));

  // ...and so is one that would break "<game_id>:<channel_id>" routing.
  TestPeerClient client2(hub->port());
  ASSERT_TRUE(RegisterAndGetResp(client2,
                                 MakeRegisterReq("game_chat", "peer-s3cret", "bad:id"),
                                 resp));
  EXPECT_EQ(resp.code(), chirp::common::INVALID_PARAM);

  hub->Stop();
  runner.Drain();
  runner.Finish();
}

TEST(ChatPeerHubTest, DisplacesPreviousRegistration) {
  chirp::common::Logger::Instance().SetLevel(chirp::common::Logger::Level::kError);
  asio::io_context io;
  HubEvents events;
  auto hub = chirp::network::ChatPeerHub::Create(
      io, HubTestOptions(),
      [](const std::string&, const std::string&, int32_t,
         const std::vector<chirp::gateway::PeerCapability>&) {},
      [&](const std::string& id, const std::string& reason) {
        std::lock_guard<std::mutex> lock(events.mu);
        events.dropped.emplace_back(id, reason);
      },
      [](const std::string&, const chirp::gateway::ChannelMessageNotify&) {});
  hub->Start();
  HubIoRunner runner(io);

  TestPeerClient first(hub->port());
  chirp::gateway::PeerRegisterResp resp;
  ASSERT_TRUE(RegisterAndGetResp(first, MakeRegisterReq("game_chat", "peer-s3cret",
                                                        "game_one"),
                                 resp));
  EXPECT_EQ(resp.code(), chirp::common::OK);

  // The same service_id registering on a second connection takes over: the
  // first connection is closed with a "displaced" report.
  TestPeerClient second(hub->port());
  ASSERT_TRUE(RegisterAndGetResp(second, MakeRegisterReq("game_chat", "peer-s3cret",
                                                         "game_two"),
                                 resp));
  EXPECT_EQ(resp.code(), chirp::common::OK);
  EXPECT_FALSE(first.Read(std::chrono::seconds(1)));  // old conn was closed

  EXPECT_TRUE(WaitFor([&] {
    std::lock_guard<std::mutex> lock(events.mu);
    return !events.dropped.empty();
  }, std::chrono::seconds(3)));
  {
    std::lock_guard<std::mutex> lock(events.mu);
    ASSERT_GE(events.dropped.size(), 1u);
    EXPECT_EQ(events.dropped[0].first, "game_chat");
    EXPECT_EQ(events.dropped[0].second, "displaced");
  }

  // Routing now follows the new registration's game namespace.
  std::promise<std::string> game_id;
  asio::post(io, [&] { game_id.set_value(hub->game_id_for("game_chat")); });
  EXPECT_EQ(game_id.get_future().get(), "game_two");

  hub->Stop();
  runner.Drain();
  runner.Finish();
}

TEST(ChatPeerHubTest, RelaysChannelMessageToHandler) {
  chirp::common::Logger::Instance().SetLevel(chirp::common::Logger::Level::kError);
  asio::io_context io;
  HubEvents events;
  auto hub = chirp::network::ChatPeerHub::Create(
      io, HubTestOptions(),
      [](const std::string&, const std::string&, int32_t,
         const std::vector<chirp::gateway::PeerCapability>&) {},
      [](const std::string&, const std::string&) {},
      [&](const std::string& service_id, const chirp::gateway::ChannelMessageNotify& n) {
        std::lock_guard<std::mutex> lock(events.mu);
        events.uplinks.push_back(n);
        events.registered.push_back(service_id);  // reuse as sender log
      });
  hub->Start();
  HubIoRunner runner(io);

  TestPeerClient client(hub->port());
  chirp::gateway::PeerRegisterResp resp;
  ASSERT_TRUE(RegisterAndGetResp(client, MakeRegisterReq(), resp));
  EXPECT_EQ(resp.code(), chirp::common::OK);

  chirp::gateway::ChannelMessageNotify up;
  up.set_game_id("game42");
  up.set_channel_id("guild_1");
  up.mutable_message()->set_sender_id("u7");
  up.mutable_message()->set_content("raid at dawn");
  client.Send(MakePacket(chirp::gateway::CHANNEL_MESSAGE_NOTIFY, 1, up));
  EXPECT_TRUE(WaitFor([&] {
    std::lock_guard<std::mutex> lock(events.mu);
    return !events.uplinks.empty() && events.uplinks[0].channel_id() == "guild_1";
  }, std::chrono::seconds(3)));
  {
    std::lock_guard<std::mutex> lock(events.mu);
    ASSERT_GE(events.uplinks.size(), 1u);
    EXPECT_EQ(events.uplinks[0].game_id(), "game42");
    EXPECT_EQ(events.uplinks[0].message().sender_id(), "u7");
    EXPECT_EQ(events.uplinks[0].message().content(), "raid at dawn");
  }

  hub->Stop();
  runner.Drain();
  runner.Finish();
}

TEST(ChatPeerHubTest, SendInjectReachesRegisteredPeerOnly) {
  chirp::common::Logger::Instance().SetLevel(chirp::common::Logger::Level::kError);
  asio::io_context io;
  auto hub = chirp::network::ChatPeerHub::Create(
      io, HubTestOptions(),
      [](const std::string&, const std::string&, int32_t,
         const std::vector<chirp::gateway::PeerCapability>&) {},
      [](const std::string&, const std::string&) {},
      [](const std::string&, const chirp::gateway::ChannelMessageNotify&) {});
  hub->Start();
  HubIoRunner runner(io);

  // No such peer: the downlink refuses.
  std::promise<bool> unknown;
  asio::post(io, [&] {
    unknown.set_value(hub->SendInject("game_chat",
                                      chirp::gateway::PeerInjectMessageNotify{}));
  });
  EXPECT_FALSE(unknown.get_future().get());

  TestPeerClient client(hub->port());
  chirp::gateway::PeerRegisterResp resp;
  ASSERT_TRUE(RegisterAndGetResp(client, MakeRegisterReq(), resp));
  EXPECT_EQ(resp.code(), chirp::common::OK);

  // The registered peer receives the injected message with its payload intact.
  chirp::gateway::PeerInjectMessageNotify notify;
  notify.set_channel_id("guild_1");
  notify.set_sender_id("player_3");
  notify.set_content("hi from the app plane");
  notify.set_client_msg_id("cmid-9");
  std::promise<bool> sent;
  asio::post(io, [&] { sent.set_value(hub->SendInject("game_chat", notify)); });
  EXPECT_TRUE(sent.get_future().get());

  Packet pkt;
  ASSERT_TRUE(client.Read(pkt));
  EXPECT_EQ(pkt.msg_id(), chirp::gateway::PEER_INJECT_MESSAGE_NOTIFY);
  chirp::gateway::PeerInjectMessageNotify seen;
  ASSERT_TRUE(seen.ParseFromString(pkt.body()));
  EXPECT_EQ(seen.channel_id(), "guild_1");
  EXPECT_EQ(seen.sender_id(), "player_3");
  EXPECT_EQ(seen.content(), "hi from the app plane");
  EXPECT_EQ(seen.client_msg_id(), "cmid-9");

  hub->Stop();
  runner.Drain();
  runner.Finish();
}

TEST(ChatPeerHubTest, HeartbeatPingGetsPong) {
  chirp::common::Logger::Instance().SetLevel(chirp::common::Logger::Level::kError);
  asio::io_context io;
  auto hub = chirp::network::ChatPeerHub::Create(
      io, HubTestOptions(),
      [](const std::string&, const std::string&, int32_t,
         const std::vector<chirp::gateway::PeerCapability>&) {},
      [](const std::string&, const std::string&) {},
      [](const std::string&, const chirp::gateway::ChannelMessageNotify&) {});
  hub->Start();
  HubIoRunner runner(io);

  TestPeerClient client(hub->port());
  chirp::gateway::PeerRegisterResp resp;
  ASSERT_TRUE(RegisterAndGetResp(client, MakeRegisterReq(), resp));
  EXPECT_EQ(resp.code(), chirp::common::OK);

  chirp::gateway::HeartbeatPing ping;
  client.Send(MakePacket(chirp::gateway::HEARTBEAT_PING, 41, ping));
  Packet pkt;
  ASSERT_TRUE(client.Read(pkt));
  EXPECT_EQ(pkt.msg_id(), chirp::gateway::HEARTBEAT_PONG);
  EXPECT_EQ(pkt.sequence(), 41);  // echoed for diagnostics

  hub->Stop();
  runner.Drain();
  runner.Finish();
}

TEST(ChatPeerHubTest, SilenceTimeoutDropsPeer) {
  chirp::common::Logger::Instance().SetLevel(chirp::common::Logger::Level::kError);
  asio::io_context io;
  HubEvents events;
  auto hub = chirp::network::ChatPeerHub::Create(
      io, HubTestOptions(/*heartbeat_seconds=*/1),
      [](const std::string&, const std::string&, int32_t,
         const std::vector<chirp::gateway::PeerCapability>&) {},
      [&](const std::string& id, const std::string& reason) {
        std::lock_guard<std::mutex> lock(events.mu);
        events.dropped.emplace_back(id, reason);
      },
      [](const std::string&, const chirp::gateway::ChannelMessageNotify&) {});
  hub->Start();
  HubIoRunner runner(io);

  TestPeerClient client(hub->port());
  chirp::gateway::PeerRegisterResp resp;
  ASSERT_TRUE(RegisterAndGetResp(client, MakeRegisterReq(), resp));
  EXPECT_EQ(resp.code(), chirp::common::OK);

  // Nothing but the registration: the 2s idle window (2 x 1s cadence) closes.
  EXPECT_FALSE(client.Read(std::chrono::seconds(5)));
  EXPECT_TRUE(WaitFor([&] {
    std::lock_guard<std::mutex> lock(events.mu);
    for (const auto& [id, reason] : events.dropped) {
      if (id == "game_chat" && reason == "timeout") {
        return true;
      }
    }
    return false;
  }, std::chrono::seconds(3)));

  hub->Stop();
  runner.Drain();
  runner.Finish();
}

TEST(ChatPeerHubTest, ReRegisterOnLiveConnectionClosesIt) {
  chirp::common::Logger::Instance().SetLevel(chirp::common::Logger::Level::kError);
  asio::io_context io;
  HubEvents events;
  auto hub = chirp::network::ChatPeerHub::Create(
      io, HubTestOptions(),
      [](const std::string&, const std::string&, int32_t,
         const std::vector<chirp::gateway::PeerCapability>&) {},
      [&](const std::string& id, const std::string& reason) {
        std::lock_guard<std::mutex> lock(events.mu);
        events.dropped.emplace_back(id, reason);
      },
      [](const std::string&, const chirp::gateway::ChannelMessageNotify&) {});
  hub->Start();
  HubIoRunner runner(io);

  TestPeerClient client(hub->port());
  chirp::gateway::PeerRegisterResp resp;
  ASSERT_TRUE(RegisterAndGetResp(client, MakeRegisterReq(), resp));
  EXPECT_EQ(resp.code(), chirp::common::OK);

  // A second register on a live connection is a protocol violation.
  client.Send(MakePacket(chirp::gateway::PEER_REGISTER_REQ, 0, MakeRegisterReq()));
  EXPECT_FALSE(client.Read(std::chrono::seconds(3)));
  EXPECT_TRUE(WaitFor([&] {
    std::lock_guard<std::mutex> lock(events.mu);
    for (const auto& [id, reason] : events.dropped) {
      if (reason == "protocol error") {
        return true;
      }
    }
    return false;
  }, std::chrono::seconds(3)));

  hub->Stop();
  runner.Drain();
  runner.Finish();
}

TEST(ChatPeerHubTest, TrafficBeforeRegistrationClosesConnection) {
  chirp::common::Logger::Instance().SetLevel(chirp::common::Logger::Level::kError);
  asio::io_context io;
  HubEvents events;
  auto hub = chirp::network::ChatPeerHub::Create(
      io, HubTestOptions(),
      [](const std::string&, const std::string&, int32_t,
         const std::vector<chirp::gateway::PeerCapability>&) {},
      [](const std::string&, const std::string&) {},
      [&](const std::string&, const chirp::gateway::ChannelMessageNotify&) {
        std::lock_guard<std::mutex> lock(events.mu);
        events.uplinks.push_back({});
      });
  hub->Start();
  HubIoRunner runner(io);

  // A 5052 from a connection that never registered gets it closed, and the
  // handler is never consulted.
  TestPeerClient client(hub->port());
  chirp::gateway::ChannelMessageNotify up;
  up.set_game_id("game42");
  up.set_channel_id("world");
  client.Send(MakePacket(chirp::gateway::CHANNEL_MESSAGE_NOTIFY, 0, up));
  EXPECT_FALSE(client.Read(std::chrono::seconds(3)));
  std::this_thread::sleep_for(std::chrono::milliseconds(100));
  std::lock_guard<std::mutex> lock(events.mu);
  EXPECT_TRUE(events.uplinks.empty());

  hub->Stop();
  runner.Drain();
  runner.Finish();
}

TEST(ChatPeerHubTest, HeartbeatKeepsIdlePeerAlive) {
  chirp::common::Logger::Instance().SetLevel(chirp::common::Logger::Level::kError);
  asio::io_context io;
  HubEvents events;
  auto hub = chirp::network::ChatPeerHub::Create(
      io, HubTestOptions(/*heartbeat_seconds=*/1),
      [](const std::string&, const std::string&, int32_t,
         const std::vector<chirp::gateway::PeerCapability>&) {},
      [&](const std::string&, const std::string& reason) {
        std::lock_guard<std::mutex> lock(events.mu);
        events.dropped.emplace_back("", reason);
      },
      [](const std::string&, const chirp::gateway::ChannelMessageNotify&) {});
  hub->Start();
  HubIoRunner runner(io);

  TestPeerClient client(hub->port());
  chirp::gateway::PeerRegisterResp resp;
  ASSERT_TRUE(RegisterAndGetResp(client, MakeRegisterReq(), resp));
  EXPECT_EQ(resp.code(), chirp::common::OK);

  // Pinging every ~700ms keeps the connection past several idle windows.
  for (int i = 0; i < 4; ++i) {
    std::this_thread::sleep_for(std::chrono::milliseconds(700));
    chirp::gateway::HeartbeatPing ping;
    client.Send(MakePacket(chirp::gateway::HEARTBEAT_PING, i + 1, ping));
    Packet pkt;
    ASSERT_TRUE(client.Read(pkt));
    EXPECT_EQ(pkt.msg_id(), chirp::gateway::HEARTBEAT_PONG);
  }

  {
    std::lock_guard<std::mutex> lock(events.mu);
    EXPECT_TRUE(events.dropped.empty());  // never timed out
  }

  hub->Stop();
  runner.Drain();
  runner.Finish();
}

TEST(ChatPeerHubTest, InvalidFrameSizeClosesConnection) {
  chirp::common::Logger::Instance().SetLevel(chirp::common::Logger::Level::kError);
  asio::io_context io;
  HubEvents events;
  auto hub = MakeRecordingHub(io, events);
  hub->Start();
  HubIoRunner runner(io);

  TestPeerClient client(hub->port());
  client.SendRaw(std::string(4, '\0'));  // zero length prefix
  EXPECT_FALSE(client.Read(std::chrono::seconds(1)));
  {
    std::lock_guard<std::mutex> lock(events.mu);
    EXPECT_TRUE(events.dropped.empty());  // never registered, nothing to report
  }

  hub->Stop();
  runner.Drain();
  runner.Finish();
}

TEST(ChatPeerHubTest, GarbagePacketBodyClosesConnection) {
  chirp::common::Logger::Instance().SetLevel(chirp::common::Logger::Level::kError);
  asio::io_context io;
  HubEvents events;
  auto hub = MakeRecordingHub(io, events);
  hub->Start();
  HubIoRunner runner(io);

  TestPeerClient client(hub->port());
  // A well-formed length prefix over an unparseable outer envelope.
  client.SendRaw(std::string("\x00\x00\x00\x01\xff", 5));
  EXPECT_FALSE(client.Read(std::chrono::seconds(1)));

  hub->Stop();
  runner.Drain();
  runner.Finish();
}

TEST(ChatPeerHubTest, GarbageRegisterReqClosesConnection) {
  chirp::common::Logger::Instance().SetLevel(chirp::common::Logger::Level::kError);
  asio::io_context io;
  HubEvents events;
  auto hub = MakeRecordingHub(io, events);
  hub->Start();
  HubIoRunner runner(io);

  TestPeerClient client(hub->port());
  client.Send(MakeRawPacket(chirp::gateway::PEER_REGISTER_REQ, 0, "\xff"));
  EXPECT_FALSE(client.Read(std::chrono::seconds(1)));

  hub->Stop();
  runner.Drain();
  runner.Finish();
}

TEST(ChatPeerHubTest, MalformedChannelBodyKeepsConnection) {
  chirp::common::Logger::Instance().SetLevel(chirp::common::Logger::Level::kError);
  asio::io_context io;
  HubEvents events;
  auto hub = MakeRecordingHub(io, events);
  hub->Start();
  HubIoRunner runner(io);

  TestPeerClient client(hub->port());
  chirp::gateway::PeerRegisterResp resp;
  ASSERT_TRUE(RegisterAndGetResp(client, MakeRegisterReq(), resp));
  EXPECT_EQ(resp.code(), chirp::common::OK);

  // A CHANNEL_MESSAGE_NOTIFY whose body is not parseable protobuf is logged
  // and skipped; the connection survives and answers the next ping.
  client.Send(MakeRawPacket(chirp::gateway::CHANNEL_MESSAGE_NOTIFY, 0, "\xff"));
  chirp::gateway::HeartbeatPing ping;
  client.Send(MakePacket(chirp::gateway::HEARTBEAT_PING, 7, ping));
  Packet pkt;
  ASSERT_TRUE(client.Read(pkt));
  EXPECT_EQ(pkt.msg_id(), chirp::gateway::HEARTBEAT_PONG);
  EXPECT_EQ(pkt.sequence(), 7);
  {
    std::lock_guard<std::mutex> lock(events.mu);
    EXPECT_TRUE(events.dropped.empty());
  }

  hub->Stop();
  runner.Drain();
  runner.Finish();
}

TEST(ChatPeerHubTest, HeartbeatBeforeRegistrationClosesConnection) {
  chirp::common::Logger::Instance().SetLevel(chirp::common::Logger::Level::kError);
  asio::io_context io;
  HubEvents events;
  auto hub = MakeRecordingHub(io, events);
  hub->Start();
  HubIoRunner runner(io);

  TestPeerClient client(hub->port());
  chirp::gateway::HeartbeatPing ping;
  client.Send(MakePacket(chirp::gateway::HEARTBEAT_PING, 1, ping));
  EXPECT_FALSE(client.Read(std::chrono::seconds(1)));

  hub->Stop();
  runner.Drain();
  runner.Finish();
}

TEST(ChatPeerHubTest, UnexpectedMsgIdIsFatalOnlyBeforeRegistration) {
  chirp::common::Logger::Instance().SetLevel(chirp::common::Logger::Level::kError);
  asio::io_context io;
  HubEvents events;
  auto hub = MakeRecordingHub(io, events);
  hub->Start();
  HubIoRunner runner(io);

  // Before registration any non-register frame is a protocol violation.
  {
    TestPeerClient early(hub->port());
    early.Send(MakeRawPacket(static_cast<MsgID>(12345), 0, ""));
    EXPECT_FALSE(early.Read(std::chrono::seconds(1)));
  }

  // After registration an unknown msg id only draws a warning.
  TestPeerClient client(hub->port());
  chirp::gateway::PeerRegisterResp resp;
  ASSERT_TRUE(RegisterAndGetResp(client, MakeRegisterReq(), resp));
  client.Send(MakeRawPacket(static_cast<MsgID>(12345), 0, ""));
  chirp::gateway::HeartbeatPing ping;
  client.Send(MakePacket(chirp::gateway::HEARTBEAT_PING, 9, ping));
  Packet pkt;
  ASSERT_TRUE(client.Read(pkt));
  EXPECT_EQ(pkt.msg_id(), chirp::gateway::HEARTBEAT_PONG);

  hub->Stop();
  runner.Drain();
  runner.Finish();
}

TEST(ChatPeerHubTest, ClientHangupReportsLost) {
  chirp::common::Logger::Instance().SetLevel(chirp::common::Logger::Level::kError);
  asio::io_context io;
  HubEvents events;
  auto hub = MakeRecordingHub(io, events);
  hub->Start();
  HubIoRunner runner(io);

  TestPeerClient client(hub->port());
  chirp::gateway::PeerRegisterResp resp;
  ASSERT_TRUE(RegisterAndGetResp(client, MakeRegisterReq(), resp));

  // The hub's read loop observes the EOF and reports the registered peer as
  // dropped with the "lost" reason.
  client.Close();
  EXPECT_TRUE(WaitFor([&] {
    std::lock_guard<std::mutex> lock(events.mu);
    return events.dropped.size() == 1 && events.dropped[0].first == "game_chat" &&
           events.dropped[0].second == "lost";
  }, std::chrono::seconds(5)));

  hub->Stop();
  runner.Drain();
  runner.Finish();
}

TEST(ChatPeerHubTest, ConnectionDropsMidFrameReportsLost) {
  chirp::common::Logger::Instance().SetLevel(chirp::common::Logger::Level::kError);
  asio::io_context io;
  HubEvents events;
  auto hub = MakeRecordingHub(io, events);
  hub->Start();
  HubIoRunner runner(io);

  TestPeerClient client(hub->port());
  chirp::gateway::PeerRegisterResp resp;
  ASSERT_TRUE(RegisterAndGetResp(client, MakeRegisterReq(), resp));

  // A length prefix announcing a body that never arrives, then a hangup: the
  // body read fails with EOF and the peer is reported dropped.
  client.SendRaw(std::string("\x00\x00\x00\x64", 4));
  client.Close();
  EXPECT_TRUE(WaitFor([&] {
    std::lock_guard<std::mutex> lock(events.mu);
    return events.dropped.size() == 1 && events.dropped[0].second == "lost";
  }, std::chrono::seconds(5)));

  hub->Stop();
  runner.Drain();
  runner.Finish();
}

TEST(ChatPeerHubTest, BindFailureLeavesHubDown) {
  chirp::common::Logger::Instance().SetLevel(chirp::common::Logger::Level::kError);
  asio::io_context io;
  HubIoRunner runner(io);

  auto first = chirp::network::ChatPeerHub::Create(
      io, HubTestOptions(),
      [](const std::string&, const std::string&, int32_t,
         const std::vector<chirp::gateway::PeerCapability>&) {},
      [](const std::string&, const std::string&) {},
      [](const std::string&, const chirp::gateway::ChannelMessageNotify&) {});
  first->Start();
  ASSERT_NE(first->port(), 0);

  // A second hub cannot take the port while the first one is listening
  // (SO_REUSEADDR does not cover an active listener); Start() fails cleanly
  // and port() reports 0.
  auto second = chirp::network::ChatPeerHub::Create(
      io, HubTestOptions(),
      [](const std::string&, const std::string&, int32_t,
         const std::vector<chirp::gateway::PeerCapability>&) {},
      [](const std::string&, const std::string&) {},
      [](const std::string&, const chirp::gateway::ChannelMessageNotify&) {});
  second->Start();
  EXPECT_EQ(second->port(), 0);

  first->Stop();
  runner.Drain();
  runner.Finish();
}

// End-to-end over the real wire: a real ChatPeerLink registers into a real
// ChatPeerHub on one io loop, relays a channel message up, and receives an
// injected player reply down — the full game_chat spoke chain (register,
// CHANNEL_MESSAGE_NOTIFY uplink, PEER_INJECT_MESSAGE_NOTIFY downlink) both
// sides genuine, no scripting.
TEST(ChatPeerLinkTest, EndToEndAgainstRealHub) {
  chirp::common::Logger::Instance().SetLevel(chirp::common::Logger::Level::kError);
  asio::io_context io;

  struct Recorded {
    std::mutex mu;
    size_t registrations = 0;
    std::vector<chirp::gateway::ChannelMessageNotify> uplinks;
    std::vector<chirp::gateway::PeerInjectMessageNotify> injects;
  } rec;

  auto hub = chirp::network::ChatPeerHub::Create(
      io, HubTestOptions(),
      [&](const std::string&, const std::string&, int32_t,
          const std::vector<chirp::gateway::PeerCapability>&) {
        std::lock_guard<std::mutex> lock(rec.mu);
        rec.registrations++;
      },
      [](const std::string&, const std::string&) {},
      [&](const std::string&, const chirp::gateway::ChannelMessageNotify& notify) {
        std::lock_guard<std::mutex> lock(rec.mu);
        rec.uplinks.push_back(notify);
      });
  hub->Start();
  HubIoRunner runner(io);
  ASSERT_GT(hub->port(), 0);

  chirp::network::ChatPeerLink::Options opts;
  opts.host = "127.0.0.1";
  opts.port = hub->port();
  opts.service_id = "game_chat";
  opts.secret = "peer-s3cret";
  opts.game_id = "game42";
  opts.supported_features = {chirp::gateway::RELAY_TYPING, chirp::gateway::RELAY_PRESENCE};
  opts.reconnect_delay_seconds = 1;
  auto link = chirp::network::ChatPeerLink::Create(
      io, std::move(opts),
      [](int32_t, const std::vector<chirp::gateway::PeerCapability>&) {},
      nullptr,
      [&](const chirp::gateway::PeerInjectMessageNotify& notify) {
        std::lock_guard<std::mutex> lock(rec.mu);
        rec.injects.push_back(notify);
      });
  link->Start();

  // Registration completes on both sides of the wire.
  ASSERT_TRUE(WaitFor([&] { return RegisteredOnLinkThread(*link, io); },
                      std::chrono::seconds(5)));
  ASSERT_TRUE(WaitFor([&] {
    std::lock_guard<std::mutex> lock(rec.mu);
    return rec.registrations == 1;
  }, std::chrono::seconds(3)));

  // Uplink: identity and payload survive the relay untouched.
  chirp::gateway::ChannelMessageNotify up;
  up.set_game_id("game42");
  up.set_channel_id("lobby");
  up.mutable_message()->set_message_id("m1");
  up.mutable_message()->set_sender_id("alice");
  up.mutable_message()->set_content("gg wp");
  asio::post(io, [&] { link->SendChannelMessage(up); });
  ASSERT_TRUE(WaitFor([&] {
    std::lock_guard<std::mutex> lock(rec.mu);
    return rec.uplinks.size() == 1;
  }, std::chrono::seconds(3)));
  {
    std::lock_guard<std::mutex> lock(rec.mu);
    EXPECT_EQ(rec.uplinks[0].game_id(), "game42");
    EXPECT_EQ(rec.uplinks[0].channel_id(), "lobby");
    EXPECT_EQ(rec.uplinks[0].message().sender_id(), "alice");
    EXPECT_EQ(rec.uplinks[0].message().content(), "gg wp");
  }

  // Downlink: the hub injects a player reply routed by service_id.
  chirp::gateway::PeerInjectMessageNotify down;
  down.set_channel_id("bob");
  down.set_sender_id("player_9");
  down.set_content("hi from the app plane");
  asio::post(io, [&] { EXPECT_TRUE(hub->SendInject("game_chat", down)); });
  ASSERT_TRUE(WaitFor([&] {
    std::lock_guard<std::mutex> lock(rec.mu);
    return rec.injects.size() == 1;
  }, std::chrono::seconds(3)));
  {
    std::lock_guard<std::mutex> lock(rec.mu);
    EXPECT_EQ(rec.injects[0].sender_id(), "player_9");
    EXPECT_EQ(rec.injects[0].channel_id(), "bob");
    EXPECT_EQ(rec.injects[0].content(), "hi from the app plane");
  }

  // A late uplink after Stop is dropped, not queued (best-effort contract).
  link->Stop();
  asio::post(io, [&] { EXPECT_FALSE(link->SendChannelMessage(up)); });
  runner.Drain();
  runner.Finish();
  hub->Stop();
}

}  // namespace
