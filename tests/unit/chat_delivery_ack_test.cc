// Unit tests for the client delivery-ack manager: pending tracking, ack
// confirmation, timeout requeue into the offline path, and the capability
// lifecycle of ack-aware sessions.

#include <gtest/gtest.h>

#include <memory>
#include <string>
#include <vector>

#include "delivery_ack_manager.h"

namespace chirp::chat {
namespace {

class FakeSession : public network::Session {
public:
  void Send(std::string) override {}
  void SendAndClose(std::string) override {}
  void Close() override {}
  bool IsClosed() const override { return false; }
  std::string RemoteAddress() const override { return "127.0.0.1"; }
};

struct Recorded {
  std::vector<std::pair<std::string, std::string>> requeued;
  std::vector<std::pair<std::string, std::string>> late_acked;
};

DeliveryAckManager::Config FastConfig() {
  DeliveryAckManager::Config config;
  config.timeout_ms = 60;
  config.scan_interval_ms = 20;
  return config;
}

TEST(DeliveryAckTest, AckBeforeTimeoutClearsPendingWithoutRequeue) {
  asio::io_context io;
  Recorded recorded;
  DeliveryAckManager manager(io, FastConfig(),
                             [&](const std::string& u, const std::string& p) {
                               recorded.requeued.emplace_back(u, p);
                             },
                             [&](const std::string& u, const std::string& p) {
                               recorded.late_acked.emplace_back(u, p);
                             });
  manager.Start();

  manager.Track("msg_1", "user_2", "payload");
  EXPECT_EQ(manager.pending_count(), 1u);
  EXPECT_TRUE(manager.Acknowledge("msg_1"));
  EXPECT_EQ(manager.pending_count(), 0u);

  io.run_for(std::chrono::milliseconds(200));

  EXPECT_TRUE(recorded.requeued.empty());
  EXPECT_TRUE(recorded.late_acked.empty());
}

TEST(DeliveryAckTest, TimeoutRequeuesExactlyOnce) {
  asio::io_context io;
  Recorded recorded;
  DeliveryAckManager manager(io, FastConfig(),
                             [&](const std::string& u, const std::string& p) {
                               recorded.requeued.emplace_back(u, p);
                             },
                             [&](const std::string&, const std::string&) {});
  manager.Start();

  manager.Track("msg_1", "user_2", "the-payload");
  io.run_for(std::chrono::milliseconds(200));

  ASSERT_EQ(recorded.requeued.size(), 1u);
  EXPECT_EQ(recorded.requeued[0].first, "user_2");
  EXPECT_EQ(recorded.requeued[0].second, "the-payload");
  EXPECT_EQ(manager.pending_count(), 0u);
}

TEST(DeliveryAckTest, LateAckAfterRequeueInvokesCleanup) {
  asio::io_context io;
  Recorded recorded;
  DeliveryAckManager manager(io, FastConfig(),
                             [&](const std::string& u, const std::string& p) {
                               recorded.requeued.emplace_back(u, p);
                             },
                             [&](const std::string& u, const std::string& p) {
                               recorded.late_acked.emplace_back(u, p);
                             });
  manager.Start();

  manager.Track("msg_1", "user_2", "the-payload");
  io.run_for(std::chrono::milliseconds(200));
  ASSERT_EQ(recorded.requeued.size(), 1u);

  EXPECT_TRUE(manager.Acknowledge("msg_1"));
  ASSERT_EQ(recorded.late_acked.size(), 1u);
  EXPECT_EQ(recorded.late_acked[0].first, "user_2");
  EXPECT_EQ(recorded.late_acked[0].second, "the-payload");

  // A second ack for the same id is a no-op (offline copy already removed).
  EXPECT_FALSE(manager.Acknowledge("msg_1"));
  EXPECT_EQ(recorded.late_acked.size(), 1u);
}

TEST(DeliveryAckTest, UnknownAckIsANoop) {
  asio::io_context io;
  Recorded recorded;
  DeliveryAckManager manager(io, FastConfig(),
                             [&](const std::string& u, const std::string& p) {
                               recorded.requeued.emplace_back(u, p);
                             },
                             [&](const std::string& u, const std::string& p) {
                               recorded.late_acked.emplace_back(u, p);
                             });
  manager.Start();

  EXPECT_FALSE(manager.Acknowledge("never_tracked"));
  EXPECT_FALSE(manager.Acknowledge(""));
  EXPECT_TRUE(recorded.requeued.empty());
  EXPECT_TRUE(recorded.late_acked.empty());
}

TEST(DeliveryAckTest, RetrackingSameMessageRefreshesInsteadOfDuplicating) {
  asio::io_context io;
  Recorded recorded;
  DeliveryAckManager manager(io, FastConfig(),
                             [&](const std::string& u, const std::string& p) {
                               recorded.requeued.emplace_back(u, p);
                             },
                             [](const std::string&, const std::string&) {});
  manager.Start();

  manager.Track("msg_1", "user_2", "first");
  manager.Track("msg_1", "user_2", "second");
  EXPECT_EQ(manager.pending_count(), 1u);

  EXPECT_TRUE(manager.Acknowledge("msg_1"));
  io.run_for(std::chrono::milliseconds(200));
  EXPECT_TRUE(recorded.requeued.empty());
}

TEST(DeliveryAckTest, CapabilityLifecycleTracksLiveSessionsOnly) {
  asio::io_context io;
  DeliveryAckManager manager(io, FastConfig(),
                             [](const std::string&, const std::string&) {},
                             [](const std::string&, const std::string&) {});
  manager.Start();

  auto session = std::make_shared<FakeSession>();
  EXPECT_FALSE(manager.IsCapable(session.get()));

  manager.MarkCapable(session);
  EXPECT_TRUE(manager.IsCapable(session.get()));

  manager.ForgetSession(session.get());
  EXPECT_FALSE(manager.IsCapable(session.get()));

  // An entry whose session died (shared_ptr dropped) reads as not capable,
  // even before ForgetSession runs.
  auto dying = std::make_shared<FakeSession>();
  manager.MarkCapable(dying);
  const network::Session* dying_raw = dying.get();
  dying.reset();
  EXPECT_FALSE(manager.IsCapable(dying_raw));
}

TEST(DeliveryAckTest, RepeatStartAndNullForgetSessionAreNoops) {
  asio::io_context io;
  DeliveryAckManager manager(io, FastConfig(),
                             [](const std::string&, const std::string&) {},
                             [](const std::string&, const std::string&) {});
  manager.Start();
  // A second start while running must not double-book the scan timer.
  manager.Start();
  manager.ForgetSession(nullptr);

  auto session = std::make_shared<FakeSession>();
  manager.MarkCapable(session);
  EXPECT_TRUE(manager.IsCapable(session.get()));

  io.run_for(std::chrono::milliseconds(100));
  EXPECT_TRUE(manager.IsCapable(session.get()));
}

TEST(DeliveryAckTest, NullSessionCapabilityProbesAreSafe) {
  asio::io_context io;
  DeliveryAckManager manager(io, FastConfig(),
                             [](const std::string&, const std::string&) {},
                             [](const std::string&, const std::string&) {});
  manager.Start();

  manager.MarkCapable(nullptr);
  EXPECT_FALSE(manager.IsCapable(nullptr));
  EXPECT_EQ(manager.pending_count(), 0u);
}

TEST(DeliveryAckTest, TrackRejectsEmptyIdentifiers) {
  asio::io_context io;
  DeliveryAckManager manager(io, FastConfig(),
                             [](const std::string&, const std::string&) {},
                             [](const std::string&, const std::string&) {});
  manager.Start();

  manager.Track("", "user_2", "payload");
  manager.Track("msg_1", "", "payload");
  EXPECT_EQ(manager.pending_count(), 0u);
}

TEST(DeliveryAckTest, HeapAllocatedIdentifiersTrackAndRequeue) {
  asio::io_context io;
  Recorded recorded;
  DeliveryAckManager manager(io, FastConfig(),
                             [&](const std::string& u, const std::string& p) {
                               recorded.requeued.emplace_back(u, p);
                             },
                             [](const std::string&, const std::string&) {});
  manager.Start();

  // Long ids/payloads force heap allocation past the SSO buffer.
  const std::string msg(64, 'm');
  const std::string user(64, 'u');
  const std::string payload(128, 'p');
  manager.Track(msg, user, payload);
  EXPECT_EQ(manager.pending_count(), 1u);
  EXPECT_TRUE(manager.Acknowledge(msg));
  EXPECT_EQ(manager.pending_count(), 0u);

  manager.Track(msg + "x", user, payload);
  io.run_for(std::chrono::milliseconds(200));
  ASSERT_EQ(recorded.requeued.size(), 1u);
  EXPECT_EQ(recorded.requeued[0].first, user);
  EXPECT_EQ(recorded.requeued[0].second, payload);
}

TEST(DeliveryAckTest, RequeuedRetentionWindowClosesLateAcks) {
  asio::io_context io;
  Recorded recorded;
  DeliveryAckManager::Config config = FastConfig();
  config.timeout_ms = 30;
  config.scan_interval_ms = 10;
  config.requeued_retention_ms = 10;
  DeliveryAckManager manager(io, config,
                             [&](const std::string& u, const std::string& p) {
                               recorded.requeued.emplace_back(u, p);
                             },
                             [&](const std::string& u, const std::string& p) {
                               recorded.late_acked.emplace_back(u, p);
                             });
  manager.Start();

  manager.Track("msg_1", "user_2", "the-payload");
  // Within this window the message times out (~30ms), is requeued, and its
  // requeued_ entry is swept once the 10ms retention lapses.
  io.run_for(std::chrono::milliseconds(300));

  ASSERT_EQ(recorded.requeued.size(), 1u);
  // The retention window has closed: a late ack no longer cleans anything up.
  EXPECT_FALSE(manager.Acknowledge("msg_1"));
  EXPECT_TRUE(recorded.late_acked.empty());
}

TEST(DeliveryAckTest, DisabledManagerIsInert) {
  asio::io_context io;
  Recorded recorded;
  DeliveryAckManager::Config config;
  config.timeout_ms = 0;
  DeliveryAckManager manager(io, config,
                             [&](const std::string& u, const std::string& p) {
                               recorded.requeued.emplace_back(u, p);
                             },
                             [&](const std::string& u, const std::string& p) {
                               recorded.late_acked.emplace_back(u, p);
                             });

  manager.Start();
  auto session = std::make_shared<FakeSession>();
  manager.MarkCapable(session);
  manager.Track("msg_1", "user_2", "payload");

  EXPECT_FALSE(manager.IsCapable(session.get()));
  EXPECT_EQ(manager.pending_count(), 0u);
  EXPECT_FALSE(manager.Acknowledge("msg_1"));

  io.run_for(std::chrono::milliseconds(60));
  EXPECT_TRUE(recorded.requeued.empty());
}

} // namespace
} // namespace chirp::chat
