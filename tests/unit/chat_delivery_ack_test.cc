// Unit tests for the client delivery-ack manager: pending tracking, ack
// confirmation, timeout requeue into the offline path (with its per-device
// bucket slot), and the capability lifecycle of ack-aware sessions.

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

// 回队/迟到 ack 事件:(user, slot, payload) 三元组——slot 是这笔投递记账上
// 的离线桶，回队与清理都按它精确落桶(P1-5 per-device 拆分)。
struct Event {
  std::string user;
  std::string slot;
  std::string payload;
};

struct Recorded {
  std::vector<Event> requeued;
  std::vector<Event> late_acked;
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
                             [&](const std::string& u, const std::string& s,
                                 const std::string& p) {
                               recorded.requeued.push_back({u, s, p});
                             },
                             [&](const std::string& u, const std::string& s,
                                 const std::string& p) {
                               recorded.late_acked.push_back({u, s, p});
                             });
  manager.Start();

  manager.Track("msg_1", "", "user_2", kDefaultOfflineSlot, "payload");
  EXPECT_EQ(manager.pending_count(), 1u);
  EXPECT_TRUE(manager.Acknowledge("msg_1", ""));
  EXPECT_EQ(manager.pending_count(), 0u);

  io.run_for(std::chrono::milliseconds(200));

  EXPECT_TRUE(recorded.requeued.empty());
  EXPECT_TRUE(recorded.late_acked.empty());
}

TEST(DeliveryAckTest, TimeoutRequeuesExactlyOnce) {
  asio::io_context io;
  Recorded recorded;
  DeliveryAckManager manager(io, FastConfig(),
                             [&](const std::string& u, const std::string& s,
                                 const std::string& p) {
                               recorded.requeued.push_back({u, s, p});
                             },
                             [](const std::string&, const std::string&,
                                const std::string&) {});
  manager.Start();

  manager.Track("msg_1", "", "user_2", kDefaultOfflineSlot, "the-payload");
  io.run_for(std::chrono::milliseconds(200));

  ASSERT_EQ(recorded.requeued.size(), 1u);
  EXPECT_EQ(recorded.requeued[0].user, "user_2");
  EXPECT_EQ(recorded.requeued[0].slot, kDefaultOfflineSlot);
  EXPECT_EQ(recorded.requeued[0].payload, "the-payload");
  EXPECT_EQ(manager.pending_count(), 0u);
}

TEST(DeliveryAckTest, LateAckAfterRequeueInvokesCleanup) {
  asio::io_context io;
  Recorded recorded;
  DeliveryAckManager manager(io, FastConfig(),
                             [&](const std::string& u, const std::string& s,
                                 const std::string& p) {
                               recorded.requeued.push_back({u, s, p});
                             },
                             [&](const std::string& u, const std::string& s,
                                 const std::string& p) {
                               recorded.late_acked.push_back({u, s, p});
                             });
  manager.Start();

  manager.Track("msg_1", "", "user_2", kDefaultOfflineSlot, "the-payload");
  io.run_for(std::chrono::milliseconds(200));
  ASSERT_EQ(recorded.requeued.size(), 1u);

  EXPECT_TRUE(manager.Acknowledge("msg_1", ""));
  ASSERT_EQ(recorded.late_acked.size(), 1u);
  EXPECT_EQ(recorded.late_acked[0].user, "user_2");
  EXPECT_EQ(recorded.late_acked[0].slot, kDefaultOfflineSlot);
  EXPECT_EQ(recorded.late_acked[0].payload, "the-payload");

  // A second ack for the same id is a no-op (offline copy already removed).
  EXPECT_FALSE(manager.Acknowledge("msg_1", ""));
  EXPECT_EQ(recorded.late_acked.size(), 1u);
}

TEST(DeliveryAckTest, UnknownAckIsANoop) {
  asio::io_context io;
  Recorded recorded;
  DeliveryAckManager manager(io, FastConfig(),
                             [&](const std::string& u, const std::string& s,
                                 const std::string& p) {
                               recorded.requeued.push_back({u, s, p});
                             },
                             [&](const std::string& u, const std::string& s,
                                 const std::string& p) {
                               recorded.late_acked.push_back({u, s, p});
                             });
  manager.Start();

  EXPECT_FALSE(manager.Acknowledge("never_tracked", ""));
  EXPECT_FALSE(manager.Acknowledge("", ""));
  EXPECT_TRUE(recorded.requeued.empty());
  EXPECT_TRUE(recorded.late_acked.empty());
}

TEST(DeliveryAckTest, RetrackingSameMessageRefreshesInsteadOfDuplicating) {
  asio::io_context io;
  Recorded recorded;
  DeliveryAckManager manager(io, FastConfig(),
                             [&](const std::string& u, const std::string& s,
                                 const std::string& p) {
                               recorded.requeued.push_back({u, s, p});
                             },
                             [](const std::string&, const std::string&,
                                const std::string&) {});
  manager.Start();

  manager.Track("msg_1", "", "user_2", kDefaultOfflineSlot, "first");
  manager.Track("msg_1", "", "user_2", kDefaultOfflineSlot, "second");
  EXPECT_EQ(manager.pending_count(), 1u);

  EXPECT_TRUE(manager.Acknowledge("msg_1", ""));
  io.run_for(std::chrono::milliseconds(200));
  EXPECT_TRUE(recorded.requeued.empty());
}

TEST(DeliveryAckTest, AckMatchesByDeliverySubject) {
  // P1-5 余项：同一 message_id 的两笔投递（首投 + 补投副本）在途并存时，
  // ack 的 delivery_id 精确命中对应投递，互不干扰。
  asio::io_context io;
  Recorded recorded;
  DeliveryAckManager manager(io, FastConfig(),
                             [&](const std::string& u, const std::string& s,
                                 const std::string& p) {
                               recorded.requeued.push_back({u, s, p});
                             },
                             [](const std::string&, const std::string&,
                                const std::string&) {});
  manager.Start();

  manager.Track("m1", "", "user_2", kDefaultOfflineSlot, "first-delivery");
  manager.Track("m1", "dlv_refill", "user_2", kDefaultOfflineSlot, "refill-copy");
  EXPECT_EQ(manager.pending_count(), 2u);

  // 另一笔投递的主语：不命中。
  EXPECT_FALSE(manager.Acknowledge("m1", "dlv_other"));
  // 精确命中补投副本。
  EXPECT_TRUE(manager.Acknowledge("m1", "dlv_refill"));
  EXPECT_EQ(manager.pending_count(), 1u);
  // 旧客户端语义：空 delivery_id 退化按 message_id 匹配首投。
  EXPECT_TRUE(manager.Acknowledge("m1", ""));
  EXPECT_EQ(manager.pending_count(), 0u);

  io.run_for(std::chrono::milliseconds(200));
  EXPECT_TRUE(recorded.requeued.empty());
}

TEST(DeliveryAckTest, RequeuedCopyIsAckedByDeliverySubject) {
  // 补投副本超时回队后，携带同一 delivery_id 的迟到 ack 精确清离线副本；
  // 空 delivery_id 的旧客户端不会误清（首投没被 requeue）。
  asio::io_context io;
  Recorded recorded;
  DeliveryAckManager manager(io, FastConfig(),
                             [&](const std::string& u, const std::string& s,
                                 const std::string& p) {
                               recorded.requeued.push_back({u, s, p});
                             },
                             [&](const std::string& u, const std::string& s,
                                 const std::string& p) {
                               recorded.late_acked.push_back({u, s, p});
                             });
  manager.Start();

  manager.Track("m_refill", "dlv_kept", "user_2", kDefaultOfflineSlot, "kept-copy");
  io.run_for(std::chrono::milliseconds(200));
  ASSERT_EQ(recorded.requeued.size(), 1u);

  EXPECT_FALSE(manager.Acknowledge("m_refill", ""));
  EXPECT_TRUE(manager.Acknowledge("m_refill", "dlv_kept"));
  ASSERT_EQ(recorded.late_acked.size(), 1u);
  EXPECT_EQ(recorded.late_acked[0].user, "user_2");
  EXPECT_EQ(recorded.late_acked[0].slot, kDefaultOfflineSlot);
  EXPECT_EQ(recorded.late_acked[0].payload, "kept-copy");
}

TEST(DeliveryAckTest, RetrackingSameDeliveryIdRefreshesOnly) {
  // 重投同 id（ack 超时回队再补投，payload 里的 delivery_id 不变）→ 刷新
  // 而不是重复挂起——同一次投递的重投同 id，pending 恒为 1。
  asio::io_context io;
  Recorded recorded;
  DeliveryAckManager manager(io, FastConfig(),
                             [&](const std::string& u, const std::string& s,
                                 const std::string& p) {
                               recorded.requeued.push_back({u, s, p});
                             },
                             [](const std::string&, const std::string&,
                                const std::string&) {});
  manager.Start();

  manager.Track("m1", "dlv_re", "user_2", kDefaultOfflineSlot, "retry-1");
  manager.Track("m1", "dlv_re", "user_2", kDefaultOfflineSlot, "retry-2");
  EXPECT_EQ(manager.pending_count(), 1u);

  EXPECT_TRUE(manager.Acknowledge("m1", "dlv_re"));
  EXPECT_EQ(manager.pending_count(), 0u);
  io.run_for(std::chrono::milliseconds(200));
  EXPECT_TRUE(recorded.requeued.empty());
}

TEST(DeliveryAckTest, TimeoutRequeuesToTrackedSlot) {
  // P1-5 per-device 拆分：补投认领端记自己的 slot，超时回队落该端的设备桶，
  // 不回 default 共享桶——重投只找认领端，不被其他端的登录截走。
  asio::io_context io;
  Recorded recorded;
  DeliveryAckManager manager(io, FastConfig(),
                             [&](const std::string& u, const std::string& s,
                                 const std::string& p) {
                               recorded.requeued.push_back({u, s, p});
                             },
                             [](const std::string&, const std::string&,
                                const std::string&) {});
  manager.Start();

  manager.Track("m1", "dlv_web", "user_2", "web", "web-copy");
  io.run_for(std::chrono::milliseconds(200));

  ASSERT_EQ(recorded.requeued.size(), 1u);
  EXPECT_EQ(recorded.requeued[0].user, "user_2");
  EXPECT_EQ(recorded.requeued[0].slot, "web");
  EXPECT_EQ(recorded.requeued[0].payload, "web-copy");
}

TEST(DeliveryAckTest, LateAckCleansTrackedSlot) {
  // 迟到 ack 清理按记账 slot 精确落桶（与回队同桶），不扫别的设备桶。
  asio::io_context io;
  Recorded recorded;
  DeliveryAckManager manager(io, FastConfig(),
                             [&](const std::string& u, const std::string& s,
                                 const std::string& p) {
                               recorded.requeued.push_back({u, s, p});
                             },
                             [&](const std::string& u, const std::string& s,
                                 const std::string& p) {
                               recorded.late_acked.push_back({u, s, p});
                             });
  manager.Start();

  manager.Track("m1", "dlv_ios", "user_2", "ios", "ios-copy");
  io.run_for(std::chrono::milliseconds(200));
  ASSERT_EQ(recorded.requeued.size(), 1u);

  EXPECT_TRUE(manager.Acknowledge("m1", "dlv_ios"));
  ASSERT_EQ(recorded.late_acked.size(), 1u);
  EXPECT_EQ(recorded.late_acked[0].user, "user_2");
  EXPECT_EQ(recorded.late_acked[0].slot, "ios");
  EXPECT_EQ(recorded.late_acked[0].payload, "ios-copy");
}

TEST(DeliveryAckTest, CapabilityLifecycleTracksLiveSessionsOnly) {
  asio::io_context io;
  DeliveryAckManager manager(io, FastConfig(),
                             [](const std::string&, const std::string&,
                                const std::string&) {},
                             [](const std::string&, const std::string&,
                                const std::string&) {});
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
                             [](const std::string&, const std::string&,
                                const std::string&) {},
                             [](const std::string&, const std::string&,
                                const std::string&) {});
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
                             [](const std::string&, const std::string&,
                                const std::string&) {},
                             [](const std::string&, const std::string&,
                                const std::string&) {});
  manager.Start();

  manager.MarkCapable(nullptr);
  EXPECT_FALSE(manager.IsCapable(nullptr));
  EXPECT_EQ(manager.pending_count(), 0u);
}

TEST(DeliveryAckTest, TrackRejectsEmptyIdentifiers) {
  asio::io_context io;
  DeliveryAckManager manager(io, FastConfig(),
                             [](const std::string&, const std::string&,
                                const std::string&) {},
                             [](const std::string&, const std::string&,
                                const std::string&) {});
  manager.Start();

  manager.Track("", "", "user_2", kDefaultOfflineSlot, "payload");
  manager.Track("msg_1", "", "", kDefaultOfflineSlot, "payload");
  EXPECT_EQ(manager.pending_count(), 0u);
}

TEST(DeliveryAckTest, HeapAllocatedIdentifiersTrackAndRequeue) {
  asio::io_context io;
  Recorded recorded;
  DeliveryAckManager manager(io, FastConfig(),
                             [&](const std::string& u, const std::string& s,
                                 const std::string& p) {
                               recorded.requeued.push_back({u, s, p});
                             },
                             [](const std::string&, const std::string&,
                                const std::string&) {});
  manager.Start();

  // Long ids/payloads force heap allocation past the SSO buffer.
  const std::string msg(64, 'm');
  const std::string user(64, 'u');
  const std::string payload(128, 'p');
  manager.Track(msg, "", user, kDefaultOfflineSlot, payload);
  EXPECT_EQ(manager.pending_count(), 1u);
  EXPECT_TRUE(manager.Acknowledge(msg, ""));
  EXPECT_EQ(manager.pending_count(), 0u);

  manager.Track(msg + "x", "", user, kDefaultOfflineSlot, payload);
  io.run_for(std::chrono::milliseconds(200));
  ASSERT_EQ(recorded.requeued.size(), 1u);
  EXPECT_EQ(recorded.requeued[0].user, user);
  EXPECT_EQ(recorded.requeued[0].payload, payload);
}

TEST(DeliveryAckTest, RequeuedRetentionWindowClosesLateAcks) {
  asio::io_context io;
  Recorded recorded;
  DeliveryAckManager::Config config = FastConfig();
  config.timeout_ms = 30;
  config.scan_interval_ms = 10;
  config.requeued_retention_ms = 10;
  DeliveryAckManager manager(io, config,
                             [&](const std::string& u, const std::string& s,
                                 const std::string& p) {
                               recorded.requeued.push_back({u, s, p});
                             },
                             [&](const std::string& u, const std::string& s,
                                 const std::string& p) {
                               recorded.late_acked.push_back({u, s, p});
                             });
  manager.Start();

  manager.Track("msg_1", "", "user_2", kDefaultOfflineSlot, "the-payload");
  // Within this window the message times out (~30ms), is requeued, and its
  // requeued_ entry is swept once the 10ms retention lapses.
  io.run_for(std::chrono::milliseconds(300));

  ASSERT_EQ(recorded.requeued.size(), 1u);
  // The retention window has closed: a late ack no longer cleans anything up.
  EXPECT_FALSE(manager.Acknowledge("msg_1", ""));
  EXPECT_TRUE(recorded.late_acked.empty());
}

TEST(DeliveryAckTest, DisabledManagerIsInert) {
  asio::io_context io;
  Recorded recorded;
  DeliveryAckManager::Config config;
  config.timeout_ms = 0;
  DeliveryAckManager manager(io, config,
                             [&](const std::string& u, const std::string& s,
                                 const std::string& p) {
                               recorded.requeued.push_back({u, s, p});
                             },
                             [&](const std::string& u, const std::string& s,
                                 const std::string& p) {
                               recorded.late_acked.push_back({u, s, p});
                             });

  manager.Start();
  auto session = std::make_shared<FakeSession>();
  manager.MarkCapable(session);
  manager.Track("msg_1", "", "user_2", kDefaultOfflineSlot, "payload");

  EXPECT_FALSE(manager.IsCapable(session.get()));
  EXPECT_EQ(manager.pending_count(), 0u);
  EXPECT_FALSE(manager.Acknowledge("msg_1", ""));

  io.run_for(std::chrono::milliseconds(60));
  EXPECT_TRUE(recorded.requeued.empty());
}

} // namespace
} // namespace chirp::chat
