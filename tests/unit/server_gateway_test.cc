// Coverage tests for the server plane hub: service registry, event queue,
// and the packet handlers. Peers are recording fakes, so no sockets are
// involved and every routing branch is exercised directly.
#include <gtest/gtest.h>

#include <map>
#include <memory>
#include <optional>
#include <string>
#include <vector>

#include "event_queue.h"
#include "proto/common.pb.h"
#include "proto/gateway.pb.h"
#include "proto/server_gateway.pb.h"
#include "server_gateway_handlers.h"
#include "service_registry.h"

namespace {

namespace sg = chirp::server_gateway;

using chirp::common::AUTH_FAILED;
using chirp::common::INVALID_PARAM;
using chirp::common::OK;
using chirp::common::SERVER_UNAVAILABLE;

struct SentFrame {
  chirp::gateway::MsgID msg_id;
  std::string body;
};

class RecordingPeer : public sg::PeerSender {
 public:
  bool Send(chirp::gateway::MsgID msg_id, const google::protobuf::Message& body) override {
    if (!send_ok) {
      return false;
    }
    sent.push_back({msg_id, body.SerializeAsString()});
    return true;
  }

  template <typename T>
  std::vector<T> Decode(chirp::gateway::MsgID msg_id) const {
    std::vector<T> out;
    for (const auto& frame : sent) {
      if (frame.msg_id != msg_id) {
        continue;
      }
      T message;
      message.ParseFromString(frame.body);
      out.push_back(std::move(message));
    }
    return out;
  }

  int Count(chirp::gateway::MsgID msg_id) const {
    int total = 0;
    for (const auto& frame : sent) {
      if (frame.msg_id == msg_id) {
        ++total;
      }
    }
    return total;
  }

  std::vector<SentFrame> sent;
  bool send_ok = true;
};

sg::EventPublishRequest MakePublishRequest(const std::string& target,
                                           const std::string& event_type) {
  sg::EventPublishRequest req;
  req.set_target_service_id(target);
  req.set_event_type(event_type);
  req.set_payload("payload");
  return req;
}

class ServerGatewayTest : public ::testing::Test {
 protected:
  void SetUp() override {
    config_.service_secrets = {{"chat", "chat-secret"},
                               {"game", "game-secret"},
                               {"trade", "trade-secret"}};
    handlers_ = std::make_unique<sg::ServerGatewayHandlers>(config_, registry_, queue_, identities_,
                                                            subscriptions_);
  }

  std::shared_ptr<RecordingPeer> AuthAs(const std::string& service_id) {
    auto peer = std::make_shared<RecordingPeer>();
    const auto out = TryAuthAs(service_id, peer);
    EXPECT_EQ(out.code, OK);
    EXPECT_EQ(out.service_id, service_id);
    return peer;
  }

  sg::AuthOutcome TryAuthAs(const std::string& service_id,
                            const std::shared_ptr<RecordingPeer>& peer) {
    sg::ServerAuthRequest req;
    req.set_service_id(service_id);
    req.set_secret(service_id + "-secret");
    req.set_protocol_version(1);
    return handlers_->HandleAuth(req, peer);
  }

  sg::EventPublishResponse Publish(const std::string& target, const std::string& event_type,
                                   const std::string& event_id = "") {
    sg::EventPublishRequest req;
    req.set_target_service_id(target);
    req.set_event_type(event_type);
    req.set_payload("payload");
    if (!event_id.empty()) {
      req.set_event_id(event_id);
    }
    return handlers_->HandleEventPublish(req);
  }

  sg::EventAckResponse AckAs(const std::string& service_id,
                             const std::vector<std::string>& event_ids) {
    sg::EventAckRequest req;
    for (const auto& id : event_ids) {
      req.add_event_ids(id);
    }
    return handlers_->HandleEventAck(req, service_id);
  }

  sg::ServerGatewayConfig config_;
  sg::ServiceRegistry registry_;
  sg::EventQueue queue_{1000};
  sg::IdentityRegistry identities_;
  sg::SubscriptionRegistry subscriptions_;
  std::unique_ptr<sg::ServerGatewayHandlers> handlers_;
};

// ---------------------------------------------------------------------------
// ServiceRegistry
// ---------------------------------------------------------------------------

TEST_F(ServerGatewayTest, RegistryRoundTrip) {
  auto peer = std::make_shared<RecordingPeer>();
  EXPECT_FALSE(registry_.IsOnline("game"));
  EXPECT_EQ(registry_.Size(), 0u);
  EXPECT_EQ(registry_.Register("game", peer), nullptr);
  EXPECT_TRUE(registry_.IsOnline("game"));
  EXPECT_EQ(registry_.Get("game").get(), peer.get());
  EXPECT_EQ(registry_.Size(), 1u);
}

TEST_F(ServerGatewayTest, RegistryReplacesAndReturnsOldPeer) {
  auto first = std::make_shared<RecordingPeer>();
  auto second = std::make_shared<RecordingPeer>();
  EXPECT_EQ(registry_.Register("game", first), nullptr);
  EXPECT_EQ(registry_.Register("game", second).get(), first.get());
  EXPECT_EQ(registry_.Get("game").get(), second.get());
  EXPECT_EQ(registry_.Size(), 1u);
}

TEST_F(ServerGatewayTest, UnregisterOnlyRemovesMatchingPeer) {
  auto first = std::make_shared<RecordingPeer>();
  auto second = std::make_shared<RecordingPeer>();
  registry_.Register("game", first);
  registry_.Register("game", second);
  // The displaced connection closing late must not remove the new peer.
  EXPECT_FALSE(registry_.Unregister("game", first.get()));
  EXPECT_TRUE(registry_.IsOnline("game"));
  EXPECT_TRUE(registry_.Unregister("game", second.get()));
  EXPECT_FALSE(registry_.IsOnline("game"));
}

TEST_F(ServerGatewayTest, UnregisterUnknownServiceReturnsFalse) {
  EXPECT_FALSE(registry_.Unregister("ghost", nullptr));
}

// ---------------------------------------------------------------------------
// EventQueue
// ---------------------------------------------------------------------------

TEST_F(ServerGatewayTest, QueueClaimPreservesOrderAndMarksInFlight) {
  sg::EventQueue q(10);
  sg::PendingEvent a;
  a.event_id = "a";
  sg::PendingEvent b;
  b.event_id = "b";
  EXPECT_TRUE(q.Enqueue("game", a));
  EXPECT_TRUE(q.Enqueue("game", b));
  EXPECT_EQ(q.UnackedCount("game"), 2u);

  const auto batch = q.ClaimDeliverable("game");
  ASSERT_EQ(batch.size(), 2u);
  EXPECT_EQ(batch[0].event_id, "a");
  EXPECT_EQ(batch[1].event_id, "b");
  EXPECT_EQ(batch[0].attempt, 1);
  // Everything claimed is in flight: a second claim returns nothing.
  EXPECT_TRUE(q.ClaimDeliverable("game").empty());
  EXPECT_EQ(q.UnackedCount("game"), 2u);
}

TEST_F(ServerGatewayTest, QueueAckRemovesOnlyAckedEvents) {
  sg::EventQueue q(10);
  for (const char* id : {"a", "b", "c"}) {
    sg::PendingEvent ev;
    ev.event_id = id;
    ASSERT_TRUE(q.Enqueue("game", ev));
  }
  q.Ack("game", {"b", "unknown-id"});
  EXPECT_EQ(q.UnackedCount("game"), 2u);
  q.ResetInFlight("game");
  const auto batch = q.ClaimDeliverable("game");
  ASSERT_EQ(batch.size(), 2u);
  EXPECT_EQ(batch[0].event_id, "a");
  EXPECT_EQ(batch[1].event_id, "c");
}

TEST_F(ServerGatewayTest, QueueResetAllowsRedeliveryWithIncrementedAttempt) {
  sg::EventQueue q(10);
  sg::PendingEvent ev;
  ev.event_id = "a";
  ASSERT_TRUE(q.Enqueue("game", ev));

  ASSERT_EQ(q.ClaimDeliverable("game").size(), 1u);
  q.ResetInFlight("game");
  const auto redelivered = q.ClaimDeliverable("game");
  ASSERT_EQ(redelivered.size(), 1u);
  EXPECT_EQ(redelivered[0].attempt, 2);
}

TEST_F(ServerGatewayTest, QueueRejectsPublishesWhenFull) {
  sg::EventQueue q(2);
  for (int i = 0; i < 2; ++i) {
    sg::PendingEvent ev;
    ev.event_id = "e" + std::to_string(i);
    EXPECT_TRUE(q.Enqueue("game", ev));
  }
  sg::PendingEvent overflow;
  overflow.event_id = "overflow";
  EXPECT_FALSE(q.Enqueue("game", overflow));
  EXPECT_EQ(q.UnackedCount("game"), 2u);
}

TEST_F(ServerGatewayTest, QueueIsolatesServices) {
  sg::EventQueue q(10);
  sg::PendingEvent ev;
  ev.event_id = "a";
  ASSERT_TRUE(q.Enqueue("game", ev));
  ASSERT_TRUE(q.Enqueue("trade", ev));
  EXPECT_EQ(q.ClaimDeliverable("game").size(), 1u);
  EXPECT_EQ(q.UnackedCount("game"), 1u);
  EXPECT_EQ(q.UnackedCount("trade"), 1u);
  EXPECT_EQ(q.UnackedCount("nobody"), 0u);
}

// ---------------------------------------------------------------------------
// Auth
// ---------------------------------------------------------------------------

TEST_F(ServerGatewayTest, AuthOkRegistersPeerAndReportsHeartbeatInterval) {
  const auto peer = AuthAs("game");
  EXPECT_TRUE(registry_.IsOnline("game"));
}

TEST_F(ServerGatewayTest, AuthRejectsUnknownService) {
  auto peer = std::make_shared<RecordingPeer>();
  sg::ServerAuthRequest req;
  req.set_service_id("intruder");
  req.set_secret("anything");
  req.set_protocol_version(1);
  EXPECT_EQ(handlers_->HandleAuth(req, peer).code, AUTH_FAILED);
  EXPECT_FALSE(registry_.IsOnline("intruder"));
}

TEST_F(ServerGatewayTest, AuthRejectsWrongSecret) {
  auto peer = std::make_shared<RecordingPeer>();
  sg::ServerAuthRequest req;
  req.set_service_id("game");
  req.set_secret("nope");
  req.set_protocol_version(1);
  EXPECT_EQ(handlers_->HandleAuth(req, peer).code, AUTH_FAILED);
  EXPECT_FALSE(registry_.IsOnline("game"));
}

TEST_F(ServerGatewayTest, AuthRejectsWrongProtocolVersion) {
  auto peer = std::make_shared<RecordingPeer>();
  sg::ServerAuthRequest req;
  req.set_service_id("game");
  req.set_secret("game-secret");
  req.set_protocol_version(99);
  const auto out = handlers_->HandleAuth(req, peer);
  EXPECT_EQ(out.code, AUTH_FAILED);
}

TEST_F(ServerGatewayTest, AuthRedeliversPendingEventsOnReconnect) {
  // The target is offline when the event is published, so it is queued.
  const auto published = Publish("game", "quest.trigger");
  EXPECT_TRUE(published.queued());

  const auto peer = AuthAs("game");
  const auto delivered = peer->Decode<chirp::server_gateway::EventDeliverNotify>(
      chirp::gateway::EVENT_DELIVER_NOTIFY);
  ASSERT_EQ(delivered.size(), 1u);
  EXPECT_EQ(delivered[0].event_id(), published.event_id());
  EXPECT_EQ(delivered[0].event_type(), "quest.trigger");
  EXPECT_EQ(delivered[0].attempt(), 1);
}

TEST_F(ServerGatewayTest, AuthReplacesPreviousConnection) {
  auto first = std::make_shared<RecordingPeer>();
  ASSERT_EQ(TryAuthAs("game", first).code, OK);

  auto second = std::make_shared<RecordingPeer>();
  const auto second_out = TryAuthAs("game", second);
  ASSERT_EQ(second_out.code, OK);
  // The second login displaced the first connection.
  ASSERT_TRUE(second_out.replaced_peer);
  EXPECT_EQ(second_out.replaced_peer.get(), first.get());
  EXPECT_EQ(registry_.Get("game").get(), second.get());

  // The displaced connection closing late must not drop the live one, and
  // must not reset the live connection's in-flight tracking (no redelivery).
  handlers_->OnPeerDisconnected("game", first.get());
  EXPECT_TRUE(registry_.IsOnline("game"));

  const auto published = Publish("game", "trade.state");
  EXPECT_FALSE(published.queued());
  EXPECT_EQ(first->Count(chirp::gateway::EVENT_DELIVER_NOTIFY), 0);
  EXPECT_EQ(second->Count(chirp::gateway::EVENT_DELIVER_NOTIFY), 1);
}

TEST_F(ServerGatewayTest, DisconnectedPeerIsRequeuedForRedelivery) {
  const auto peer = AuthAs("game");
  const auto published = Publish("game", "quest.trigger");
  EXPECT_FALSE(published.queued());
  EXPECT_EQ(peer->Count(chirp::gateway::EVENT_DELIVER_NOTIFY), 1);

  handlers_->OnPeerDisconnected("game", peer.get());
  EXPECT_FALSE(registry_.IsOnline("game"));

  // Reconnect: the unacknowledged event is redelivered with attempt 2.
  const auto returning = AuthAs("game");
  const auto redelivered = returning->Decode<chirp::server_gateway::EventDeliverNotify>(
      chirp::gateway::EVENT_DELIVER_NOTIFY);
  ASSERT_EQ(redelivered.size(), 1u);
  EXPECT_EQ(redelivered[0].attempt(), 2);
}

// ---------------------------------------------------------------------------
// Heartbeat
// ---------------------------------------------------------------------------

TEST_F(ServerGatewayTest, HeartbeatAnswersWithServerTime) {
  chirp::server_gateway::ServerHeartbeatPing ping;
  ping.set_client_time_ms(1234);
  const auto pong = handlers_->HandleHeartbeat(ping);
  EXPECT_GT(pong.server_time_ms(), 0);
}

// ---------------------------------------------------------------------------
// Message injection
// ---------------------------------------------------------------------------

sg::MessageInjectRequest ValidInject() {
  sg::MessageInjectRequest req;
  req.set_inject_id("inj-1");
  req.set_sender_kind(chirp::server_gateway::SENDER_NPC);
  req.set_sender_id("npc:blacksmith_01");
  req.set_channel_type(3);  // WORLD
  req.set_channel_id("world");
  req.set_content("hello travelers");
  return req;
}

TEST_F(ServerGatewayTest, InjectRejectsEmptyContent) {
  auto req = ValidInject();
  req.clear_content();
  EXPECT_EQ(handlers_->HandleInject(req).code(), INVALID_PARAM);
}

TEST_F(ServerGatewayTest, InjectRejectsUnknownSenderKind) {
  auto req = ValidInject();
  req.set_sender_kind(chirp::server_gateway::SENDER_UNKNOWN);
  EXPECT_EQ(handlers_->HandleInject(req).code(), INVALID_PARAM);
}

TEST_F(ServerGatewayTest, InjectRejectsEmptySenderId) {
  auto req = ValidInject();
  req.clear_sender_id();
  EXPECT_EQ(handlers_->HandleInject(req).code(), INVALID_PARAM);
}

TEST_F(ServerGatewayTest, InjectRejectsMissingTarget) {
  auto req = ValidInject();
  req.clear_channel_id();
  req.clear_receiver_id();
  EXPECT_EQ(handlers_->HandleInject(req).code(), INVALID_PARAM);
}

TEST_F(ServerGatewayTest, InjectForwardsToChatWhenOnline) {
  const auto chat = AuthAs("chat");
  const auto resp = handlers_->HandleInject(ValidInject());
  EXPECT_EQ(resp.code(), OK);
  EXPECT_EQ(resp.inject_id(), "inj-1");

  const auto forwarded = chat->Decode<chirp::server_gateway::InjectMessageNotify>(
      chirp::gateway::INJECT_MESSAGE_NOTIFY);
  ASSERT_EQ(forwarded.size(), 1u);
  EXPECT_EQ(forwarded[0].message().inject_id(), "inj-1");
  EXPECT_EQ(forwarded[0].message().sender_id(), "npc:blacksmith_01");
  EXPECT_EQ(forwarded[0].message().sender_kind(),
            chirp::server_gateway::SENDER_NPC);
  EXPECT_EQ(forwarded[0].message().content(), "hello travelers");
}

TEST_F(ServerGatewayTest, InjectFailsWhenChatOffline) {
  EXPECT_EQ(handlers_->HandleInject(ValidInject()).code(), SERVER_UNAVAILABLE);
}

TEST_F(ServerGatewayTest, InjectFailsWhenChatWriteFails) {
  const auto chat = AuthAs("chat");
  chat->send_ok = false;
  EXPECT_EQ(handlers_->HandleInject(ValidInject()).code(), SERVER_UNAVAILABLE);
}

// ---------------------------------------------------------------------------
// Event publish / ack
// ---------------------------------------------------------------------------

TEST_F(ServerGatewayTest, PublishRejectsMissingFields) {
  sg::EventPublishRequest req;
  req.set_target_service_id("game");
  req.set_event_type("quest.trigger");
  req.set_payload("payload");

  // Missing event type.
  req.clear_event_type();
  EXPECT_EQ(handlers_->HandleEventPublish(req).code(), INVALID_PARAM);

  // Missing payload.
  req.set_event_type("quest.trigger");
  req.clear_payload();
  EXPECT_EQ(handlers_->HandleEventPublish(req).code(), INVALID_PARAM);

  // Missing target service.
  req.set_payload("payload");
  req.clear_target_service_id();
  EXPECT_EQ(handlers_->HandleEventPublish(req).code(), INVALID_PARAM);
}

TEST_F(ServerGatewayTest, PublishOfflineQueuesEvent) {
  const auto resp = Publish("game", "quest.trigger");
  EXPECT_EQ(resp.code(), OK);
  EXPECT_TRUE(resp.queued());
  EXPECT_FALSE(resp.event_id().empty());
  EXPECT_EQ(queue_.UnackedCount("game"), 1u);
}

TEST_F(ServerGatewayTest, PublishOnlineDeliversImmediately) {
  const auto game = AuthAs("game");
  const auto resp = Publish("game", "quest.trigger", "quest-42");
  EXPECT_EQ(resp.code(), OK);
  EXPECT_FALSE(resp.queued());
  EXPECT_EQ(resp.event_id(), "quest-42");

  const auto delivered = game->Decode<chirp::server_gateway::EventDeliverNotify>(
      chirp::gateway::EVENT_DELIVER_NOTIFY);
  ASSERT_EQ(delivered.size(), 1u);
  EXPECT_EQ(delivered[0].event_id(), "quest-42");
  EXPECT_EQ(delivered[0].attempt(), 1);
  EXPECT_GT(delivered[0].published_at_ms(), 0);
  EXPECT_EQ(queue_.UnackedCount("game"), 1u);  // still unacked (in flight)
}

TEST_F(ServerGatewayTest, PublishGeneratesEventIdWhenMissing) {
  const auto first = Publish("game", "a");
  const auto second = Publish("game", "b");
  EXPECT_NE(first.event_id(), second.event_id());
  EXPECT_EQ(first.event_id().rfind("evt-", 0), 0);
}

TEST_F(ServerGatewayTest, PublishFailsWhenQueueFull) {
  sg::ServerGatewayConfig small = config_;
  small.max_pending_events_per_service = 1;
  sg::EventQueue small_queue(1);
  sg::ServerGatewayHandlers small_handlers(small, registry_, small_queue, identities_,
                                           subscriptions_);
  small_handlers.HandleEventPublish(MakePublishRequest("game", "first"));
  const auto resp = small_handlers.HandleEventPublish(MakePublishRequest("game", "second"));
  EXPECT_EQ(resp.code(), SERVER_UNAVAILABLE);
}

TEST_F(ServerGatewayTest, AckRemovesPendingEvents) {
  const auto resp = Publish("game", "quest.trigger");
  EXPECT_EQ(AckAs("game", {resp.event_id()}).code(), OK);
  EXPECT_EQ(queue_.UnackedCount("game"), 0u);
}

TEST_F(ServerGatewayTest, AckUnknownIdsAreIdempotent) {
  EXPECT_EQ(AckAs("game", {"never-published"}).code(), OK);
  EXPECT_EQ(queue_.UnackedCount("game"), 0u);
}

// ---------------------------------------------------------------------------
// IdentityRegistry (WP-8 slice 1: player identity bindings)
// ---------------------------------------------------------------------------

// In-memory stand-in for the write-through client: the registry only needs
// Get/Set/Del/Keys. `fail` simulates a dead Redis (writes/reads no-op).
class FakeRedisClient : public chirp::network::RedisClient {
 public:
  explicit FakeRedisClient(std::shared_ptr<std::map<std::string, std::string>> store)
      : RedisClient("127.0.0.1", 1), store_(std::move(store)) {}

  std::optional<std::string> Get(const std::string& key) override {
    if (fail) {
      return std::nullopt;
    }
    const auto it = store_->find(key);
    return it == store_->end() ? std::nullopt : std::optional<std::string>(it->second);
  }
  bool Set(const std::string& key, const std::string& value) override {
    if (fail) {
      return false;
    }
    (*store_)[key] = value;
    return true;
  }
  bool Del(const std::string& key) override {
    if (fail) {
      return false;
    }
    // The real DEL answers 0 for a missing key and the client reports true:
    // the delete itself succeeded.
    store_->erase(key);
    return true;
  }
  std::vector<std::string> Keys(const std::string& pattern) override {
    std::vector<std::string> out;
    if (fail) {
      return out;
    }
    const std::string prefix = pattern.substr(0, pattern.size() - 1);
    for (const auto& [key, value] : *store_) {
      if (key.rfind(prefix, 0) == 0) {
        out.push_back(key);
      }
    }
    return out;
  }

  bool fail = false;

 private:
  std::shared_ptr<std::map<std::string, std::string>> store_;
};

sg::BindPlayerIdentityRequest MakeBindRequest(const std::string& binding_id,
                                              const std::string& player_id,
                                              const std::string& game_id,
                                              const std::string& game_user_id) {
  sg::BindPlayerIdentityRequest req;
  req.set_binding_id(binding_id);
  req.set_player_id(player_id);
  req.set_game_id(game_id);
  req.set_game_user_id(game_user_id);
  return req;
}

TEST(IdentityRegistryTest, BindLifecycleAndIdempotency) {
  sg::IdentityRegistry registry;
  EXPECT_EQ(registry.Size(), 0u);

  EXPECT_EQ(registry.Bind("b1", "player-1", "game-a", "u-1", 1000),
            sg::IdentityRegistry::BindOutcome::kBound);
  // The exact same tuple under the same idempotency key is a no-op.
  EXPECT_EQ(registry.Bind("b1", "player-1", "game-a", "u-1", 2000),
            sg::IdentityRegistry::BindOutcome::kExisted);
  // The same key asserting a different tuple would silently break duplicate
  // detection and is rejected.
  EXPECT_EQ(registry.Bind("b1", "player-2", "game-a", "u-1", 3000),
            sg::IdentityRegistry::BindOutcome::kInvalid);
  // Empty fields are rejected (the handler validates first, the store
  // enforces the same contract).
  EXPECT_EQ(registry.Bind("", "player-1", "game-a", "u-2", 1000),
            sg::IdentityRegistry::BindOutcome::kInvalid);
  EXPECT_EQ(registry.Bind("b2", "", "game-a", "u-2", 1000),
            sg::IdentityRegistry::BindOutcome::kInvalid);
  EXPECT_EQ(registry.Bind("b2", "player-1", "", "u-2", 1000),
            sg::IdentityRegistry::BindOutcome::kInvalid);
  EXPECT_EQ(registry.Bind("b2", "player-1", "game-a", "", 1000),
            sg::IdentityRegistry::BindOutcome::kInvalid);
  EXPECT_EQ(registry.Size(), 1u);
}

TEST(IdentityRegistryTest, RebindingGameUserReplacesTheOldBinding) {
  sg::IdentityRegistry registry;
  EXPECT_EQ(registry.Bind("b1", "player-1", "game-a", "u-1", 1000),
            sg::IdentityRegistry::BindOutcome::kBound);
  // The backend re-asserts: game user u-1 actually belongs to player-2 now.
  EXPECT_EQ(registry.Bind("b2", "player-2", "game-a", "u-1", 2000),
            sg::IdentityRegistry::BindOutcome::kBound);

  const auto player = registry.Resolve("game-a", "u-1");
  ASSERT_NE(player, nullptr);
  EXPECT_EQ(*player, "player-2");
  EXPECT_EQ(registry.GetByPlayer("player-1").size(), 0u);
  ASSERT_EQ(registry.GetByPlayer("player-2").size(), 1u);
  EXPECT_EQ(registry.GetByPlayer("player-2")[0].binding_id(), "b2");
  EXPECT_EQ(registry.GetByPlayer("player-2")[0].bound_at_ms(), 2000);
  EXPECT_EQ(registry.Size(), 1u);
}

TEST(IdentityRegistryTest, OnePlayerHoldsManyGameIdentities) {
  sg::IdentityRegistry registry;
  EXPECT_EQ(registry.Bind("b1", "player-1", "game-a", "u-1", 1000),
            sg::IdentityRegistry::BindOutcome::kBound);
  EXPECT_EQ(registry.Bind("b2", "player-1", "game-b", "char-9", 2000),
            sg::IdentityRegistry::BindOutcome::kBound);
  ASSERT_EQ(registry.GetByPlayer("player-1").size(), 2u);
  EXPECT_EQ(*registry.Resolve("game-a", "u-1"), "player-1");
  EXPECT_EQ(*registry.Resolve("game-b", "char-9"), "player-1");
}

TEST(IdentityRegistryTest, UnbindByIdAndByPairAreIdempotent) {
  sg::IdentityRegistry registry;
  EXPECT_EQ(registry.Bind("b1", "player-1", "game-a", "u-1", 1000),
            sg::IdentityRegistry::BindOutcome::kBound);
  EXPECT_EQ(registry.Bind("b2", "player-1", "game-b", "char-9", 2000),
            sg::IdentityRegistry::BindOutcome::kBound);

  EXPECT_TRUE(registry.UnbindById("b1"));
  EXPECT_FALSE(registry.UnbindById("b1"));  // already gone
  EXPECT_FALSE(registry.UnbindById("never-bound"));
  EXPECT_FALSE(registry.UnbindById(""));
  EXPECT_EQ(registry.Resolve("game-a", "u-1"), nullptr);

  EXPECT_TRUE(registry.UnbindByGameUser("game-b", "char-9"));
  EXPECT_FALSE(registry.UnbindByGameUser("game-b", "char-9"));
  EXPECT_FALSE(registry.UnbindByGameUser("game-b", ""));
  EXPECT_FALSE(registry.UnbindByGameUser("", "char-9"));
  EXPECT_EQ(registry.Size(), 0u);
}

TEST(IdentityRegistryTest, ResolveUnboundReturnsNull) {
  sg::IdentityRegistry registry;
  EXPECT_EQ(registry.Resolve("game-a", "u-1"), nullptr);
}

TEST(IdentityRegistryTest, RedisWriteThroughAndLoadRestore) {
  const auto store = std::make_shared<std::map<std::string, std::string>>();

  sg::IdentityRegistry writer([store]() mutable {
    return std::make_unique<FakeRedisClient>(store);
  });
  // Loading an empty store is a clean no-op.
  writer.Load();

  EXPECT_EQ(writer.Bind("b1", "player-1", "game-a", "u-1", 1000),
            sg::IdentityRegistry::BindOutcome::kBound);
  EXPECT_EQ(writer.Bind("b2", "player-2", "game-b", "char-9", 2000),
            sg::IdentityRegistry::BindOutcome::kBound);
  EXPECT_EQ(store->size(), 2u);

  // A fresh hub process restores every binding through Load.
  sg::IdentityRegistry reader([store]() mutable {
    return std::make_unique<FakeRedisClient>(store);
  });
  reader.Load();
  EXPECT_EQ(reader.Size(), 2u);
  EXPECT_EQ(*reader.Resolve("game-a", "u-1"), "player-1");
  ASSERT_EQ(reader.GetByPlayer("player-2").size(), 1u);
  EXPECT_EQ(reader.GetByPlayer("player-2")[0].game_id(), "game-b");

  // Unbind removes the persisted record.
  EXPECT_TRUE(writer.UnbindById("b1"));
  EXPECT_EQ(store->size(), 1u);

  // A corrupted record is skipped, not fatal.
  (*store)["chirp:binding:entry:junk"] = "\x01\x02not-a-proto";
  sg::IdentityRegistry tolerant([store]() mutable {
    return std::make_unique<FakeRedisClient>(store);
  });
  tolerant.Load();
  EXPECT_EQ(tolerant.Size(), 1u);
}

TEST(IdentityRegistryTest, RedisFailureDegradesToMemoryOnly) {
  const auto store = std::make_shared<std::map<std::string, std::string>>();
  sg::IdentityRegistry registry([store]() mutable {
    auto client = std::make_unique<FakeRedisClient>(store);
    client->fail = true;
    return client;
  });
  registry.Load();  // reads fail; must not crash or block

  EXPECT_EQ(registry.Bind("b1", "player-1", "game-a", "u-1", 1000),
            sg::IdentityRegistry::BindOutcome::kBound);
  EXPECT_EQ(*registry.Resolve("game-a", "u-1"), "player-1");
  EXPECT_TRUE(registry.UnbindById("b1"));
  EXPECT_EQ(store->size(), 0u);  // nothing ever reached Redis
}

TEST(IdentityRegistryTest, MemoryOnlyByDefault) {
  sg::IdentityRegistry registry;
  registry.Load();  // no factory: nothing to load
  EXPECT_EQ(registry.Bind("b1", "player-1", "game-a", "u-1", 1000),
            sg::IdentityRegistry::BindOutcome::kBound);
  EXPECT_EQ(*registry.Resolve("game-a", "u-1"), "player-1");
}

// ---------------------------------------------------------------------------
// Player channel subscriptions (WP-8 slice 2)
// ---------------------------------------------------------------------------

sg::SubscribePlayerChannelRequest MakeSubscribeRequest(const std::string& subscription_id,
                                                       const std::string& player_id,
                                                       const std::string& game_id,
                                                       const std::string& channel_id) {
  sg::SubscribePlayerChannelRequest req;
  req.set_subscription_id(subscription_id);
  req.set_player_id(player_id);
  req.set_game_id(game_id);
  req.set_channel_id(channel_id);
  return req;
}

TEST(SubscriptionRegistryTest, SubscribeLifecycleAndIdempotency) {
  sg::SubscriptionRegistry registry;
  EXPECT_EQ(registry.Size(), 0u);

  std::string id = "s1";
  EXPECT_EQ(registry.Subscribe(&id, "player-1", "game-a", "world-1", 1000),
            sg::SubscriptionRegistry::SubscribeOutcome::kSubscribed);
  // The exact same tuple under the same idempotency key is a no-op.
  EXPECT_EQ(registry.Subscribe(&id, "player-1", "game-a", "world-1", 2000),
            sg::SubscriptionRegistry::SubscribeOutcome::kExisted);
  // The same key asserting a different tuple would silently break duplicate
  // detection and is rejected.
  std::string reused = "s1";
  EXPECT_EQ(registry.Subscribe(&reused, "player-2", "game-a", "world-1", 3000),
            sg::SubscriptionRegistry::SubscribeOutcome::kInvalid);
  // Empty tuple fields are rejected (the handler validates first, the store
  // enforces the same contract).
  std::string keep = "s2";
  EXPECT_EQ(registry.Subscribe(&keep, "", "game-a", "world-2", 1000),
            sg::SubscriptionRegistry::SubscribeOutcome::kInvalid);
  EXPECT_EQ(registry.Subscribe(&keep, "player-1", "", "world-2", 1000),
            sg::SubscriptionRegistry::SubscribeOutcome::kInvalid);
  EXPECT_EQ(registry.Subscribe(&keep, "player-1", "game-a", "", 1000),
            sg::SubscriptionRegistry::SubscribeOutcome::kInvalid);
  EXPECT_EQ(registry.Size(), 1u);
}

TEST(SubscriptionRegistryTest, ReSubscribingTupleUnderNewIdReplaces) {
  sg::SubscriptionRegistry registry;
  std::string id = "s1";
  EXPECT_EQ(registry.Subscribe(&id, "player-1", "game-a", "world-1", 1000),
            sg::SubscriptionRegistry::SubscribeOutcome::kSubscribed);
  // The same tuple re-asserted under a new id replaces the old record.
  std::string reid = "s2";
  EXPECT_EQ(registry.Subscribe(&reid, "player-1", "game-a", "world-1", 2000),
            sg::SubscriptionRegistry::SubscribeOutcome::kSubscribed);

  ASSERT_EQ(registry.GetForPlayer("player-1", "").size(), 1u);
  EXPECT_EQ(registry.GetForPlayer("player-1", "")[0].subscription_id(), "s2");
  EXPECT_EQ(registry.GetForPlayer("player-1", "")[0].subscribed_at_ms(), 2000);
  EXPECT_FALSE(registry.UnsubscribeById("s1"));  // replaced away
  EXPECT_EQ(registry.Size(), 1u);
}

TEST(SubscriptionRegistryTest, SelfSubscribeMintsAStableId) {
  sg::SubscriptionRegistry registry;
  std::string minted;
  EXPECT_EQ(registry.Subscribe(&minted, "player-1", "game-a", "world-1", 1000),
            sg::SubscriptionRegistry::SubscribeOutcome::kSubscribed);
  EXPECT_TRUE(minted.rfind("sub-", 0) == 0);

  // An id-less re-subscribe (the self-service path) keeps the record — and
  // its id — stable instead of churning ids.
  std::string again;
  EXPECT_EQ(registry.Subscribe(&again, "player-1", "game-a", "world-1", 2000),
            sg::SubscriptionRegistry::SubscribeOutcome::kExisted);
  EXPECT_EQ(again, minted);
  EXPECT_EQ(registry.Size(), 1u);
}

TEST(SubscriptionRegistryTest, UnsubscribeByIdAndByTupleAreIdempotent) {
  sg::SubscriptionRegistry registry;
  std::string id = "s1";
  ASSERT_EQ(registry.Subscribe(&id, "player-1", "game-a", "world-1", 1000),
            sg::SubscriptionRegistry::SubscribeOutcome::kSubscribed);

  // Half a triple is not a selector; only the full triple (or the id) is.
  EXPECT_FALSE(registry.UnsubscribeByTuple("player-1", "game-a", ""));
  EXPECT_FALSE(registry.UnsubscribeByTuple("", "game-a", "world-1"));

  EXPECT_TRUE(registry.UnsubscribeByTuple("player-1", "game-a", "world-1"));
  EXPECT_FALSE(registry.UnsubscribeByTuple("player-1", "game-a", "world-1"));
  EXPECT_FALSE(registry.UnsubscribeById("s1"));

  ASSERT_EQ(registry.Subscribe(&id, "player-1", "game-a", "world-1", 2000),
            sg::SubscriptionRegistry::SubscribeOutcome::kSubscribed);
  EXPECT_TRUE(registry.UnsubscribeById("s1"));
  EXPECT_FALSE(registry.UnsubscribeById("s1"));
  EXPECT_EQ(registry.Size(), 0u);
}

TEST(SubscriptionRegistryTest, GetForPlayerWithGameFilter) {
  sg::SubscriptionRegistry registry;
  std::string id = "s1";
  ASSERT_EQ(registry.Subscribe(&id, "player-1", "game-a", "world-1", 1000),
            sg::SubscriptionRegistry::SubscribeOutcome::kSubscribed);
  id = "s2";
  ASSERT_EQ(registry.Subscribe(&id, "player-1", "game-b", "ranked-1", 2000),
            sg::SubscriptionRegistry::SubscribeOutcome::kSubscribed);
  id = "s3";
  ASSERT_EQ(registry.Subscribe(&id, "player-2", "game-a", "world-1", 3000),
            sg::SubscriptionRegistry::SubscribeOutcome::kSubscribed);

  EXPECT_EQ(registry.GetForPlayer("player-1", "").size(), 2u);
  const auto filtered = registry.GetForPlayer("player-1", "game-a");
  ASSERT_EQ(filtered.size(), 1u);
  EXPECT_EQ(filtered[0].subscription_id(), "s1");
  EXPECT_EQ(registry.GetForPlayer("player-1", "game-c").size(), 0u);
  EXPECT_EQ(registry.GetForPlayer("nobody", "").size(), 0u);
}

TEST(SubscriptionRegistryTest, RedisWriteThroughAndLoadRestore) {
  const auto store = std::make_shared<std::map<std::string, std::string>>();

  sg::SubscriptionRegistry writer([store]() mutable {
    return std::make_unique<FakeRedisClient>(store);
  });
  writer.Load();  // loading an empty store is a clean no-op

  std::string id = "s1";
  EXPECT_EQ(writer.Subscribe(&id, "player-1", "game-a", "world-1", 1000),
            sg::SubscriptionRegistry::SubscribeOutcome::kSubscribed);
  id = "s2";
  EXPECT_EQ(writer.Subscribe(&id, "player-2", "game-b", "ranked-1", 2000),
            sg::SubscriptionRegistry::SubscribeOutcome::kSubscribed);
  EXPECT_EQ(store->size(), 2u);

  // A fresh hub process restores every subscription through Load.
  sg::SubscriptionRegistry reader([store]() mutable {
    return std::make_unique<FakeRedisClient>(store);
  });
  reader.Load();
  EXPECT_EQ(reader.Size(), 2u);
  ASSERT_EQ(reader.GetForPlayer("player-2", "").size(), 1u);
  EXPECT_EQ(reader.GetForPlayer("player-2", "")[0].game_id(), "game-b");

  // Unsubscribe removes the persisted record.
  EXPECT_TRUE(writer.UnsubscribeById("s1"));
  EXPECT_EQ(store->size(), 1u);

  // A corrupted record is skipped, not fatal.
  (*store)["chirp:subscription:entry:junk"] = "\x01\x02not-a-proto";
  sg::SubscriptionRegistry tolerant([store]() mutable {
    return std::make_unique<FakeRedisClient>(store);
  });
  tolerant.Load();
  EXPECT_EQ(tolerant.Size(), 1u);
}

TEST(SubscriptionRegistryTest, LoadedTupleClashAppliesReplaceRule) {
  // Two hub instances persisted the same tuple under different ids (their
  // write-throughs never saw each other). Load order applies the same
  // replace rule a Subscribe would: the later record wins.
  const auto store = std::make_shared<std::map<std::string, std::string>>();
  sg::SubscriptionRegistry first([store]() mutable {
    return std::make_unique<FakeRedisClient>(store);
  });
  std::string id = "s1";
  ASSERT_EQ(first.Subscribe(&id, "player-1", "game-a", "world-1", 1000),
            sg::SubscriptionRegistry::SubscribeOutcome::kSubscribed);
  sg::SubscriptionRegistry second([store]() mutable {
    return std::make_unique<FakeRedisClient>(store);
  });
  id = "s2";
  ASSERT_EQ(second.Subscribe(&id, "player-1", "game-a", "world-1", 2000),
            sg::SubscriptionRegistry::SubscribeOutcome::kSubscribed);
  ASSERT_EQ(store->size(), 2u);

  sg::SubscriptionRegistry loader([store]() mutable {
    return std::make_unique<FakeRedisClient>(store);
  });
  loader.Load();
  ASSERT_EQ(loader.Size(), 1u);
  EXPECT_EQ(loader.GetForPlayer("player-1", "")[0].subscription_id(), "s2");
}

TEST(SubscriptionRegistryTest, RedisFailureDegradesToMemoryOnly) {
  const auto store = std::make_shared<std::map<std::string, std::string>>();
  sg::SubscriptionRegistry registry([store]() mutable {
    auto client = std::make_unique<FakeRedisClient>(store);
    client->fail = true;
    return client;
  });
  registry.Load();  // reads fail; must not crash or block

  std::string id = "s1";
  EXPECT_EQ(registry.Subscribe(&id, "player-1", "game-a", "world-1", 1000),
            sg::SubscriptionRegistry::SubscribeOutcome::kSubscribed);
  EXPECT_EQ(registry.GetForPlayer("player-1", "").size(), 1u);
  EXPECT_TRUE(registry.UnsubscribeById("s1"));
  EXPECT_EQ(store->size(), 0u);  // nothing ever reached Redis
}

TEST(SubscriptionRegistryTest, MemoryOnlyByDefault) {
  sg::SubscriptionRegistry registry;
  registry.Load();  // no factory: nothing to load
  std::string id = "s1";
  EXPECT_EQ(registry.Subscribe(&id, "player-1", "game-a", "world-1", 1000),
            sg::SubscriptionRegistry::SubscribeOutcome::kSubscribed);
  EXPECT_EQ(registry.GetForPlayer("player-1", "").size(), 1u);
}

// ---------------------------------------------------------------------------
// Binding handlers (wire-facing validation)
// ---------------------------------------------------------------------------

TEST_F(ServerGatewayTest, BindHandlerValidatesAndReportsExisted) {
  auto resp = handlers_->HandleBindPlayerIdentity(MakeBindRequest("b1", "player-1", "game-a", "u-1"));
  EXPECT_EQ(resp.code(), OK);
  EXPECT_FALSE(resp.existed());
  EXPECT_EQ(resp.binding_id(), "b1");

  resp = handlers_->HandleBindPlayerIdentity(MakeBindRequest("b1", "player-1", "game-a", "u-1"));
  EXPECT_EQ(resp.code(), OK);
  EXPECT_TRUE(resp.existed());

  resp = handlers_->HandleBindPlayerIdentity(MakeBindRequest("b2", "", "game-a", "u-2"));
  EXPECT_EQ(resp.code(), INVALID_PARAM);
}

TEST_F(ServerGatewayTest, UnbindHandlerRequiresExactlyOneSelector) {
  EXPECT_EQ(handlers_->HandleUnbindPlayerIdentity([] {
              sg::UnbindPlayerIdentityRequest req;
              req.set_binding_id("b1");
              req.set_game_id("game-a");  // both selectors: ambiguous
              req.set_game_user_id("u-1");
              return req;
            }()).code(), INVALID_PARAM);

  EXPECT_EQ(handlers_->HandleUnbindPlayerIdentity([] {
              sg::UnbindPlayerIdentityRequest req;  // no selector at all
              return req;
            }()).code(), INVALID_PARAM);

  EXPECT_EQ(handlers_->HandleUnbindPlayerIdentity([] {
              sg::UnbindPlayerIdentityRequest req;
              req.set_game_id("game-a");  // half a pair
              return req;
            }()).code(), INVALID_PARAM);

  EXPECT_EQ(handlers_->HandleUnbindPlayerIdentity([] {
              sg::UnbindPlayerIdentityRequest req;
              req.set_game_user_id("u-1");  // the other half
              return req;
            }()).code(), INVALID_PARAM);

  // A complete pair unbinds; unknown targets still answer OK (idempotent).
  EXPECT_EQ(handlers_->HandleBindPlayerIdentity(MakeBindRequest("b1", "player-1", "game-a", "u-1")).code(), OK);
  EXPECT_EQ(handlers_->HandleUnbindPlayerIdentity([] {
              sg::UnbindPlayerIdentityRequest req;
              req.set_game_id("game-a");
              req.set_game_user_id("u-1");
              return req;
            }()).code(), OK);
  EXPECT_EQ(identities_.Size(), 0u);
}

TEST_F(ServerGatewayTest, GetHandlerReturnsBindingsForPlayer) {
  EXPECT_EQ(handlers_->HandleGetPlayerIdentities([] {
              sg::GetPlayerIdentitiesRequest req;  // empty player_id
              return req;
            }()).code(), INVALID_PARAM);

  EXPECT_EQ(handlers_->HandleBindPlayerIdentity(MakeBindRequest("b1", "player-1", "game-a", "u-1")).code(), OK);
  EXPECT_EQ(handlers_->HandleBindPlayerIdentity(MakeBindRequest("b2", "player-1", "game-b", "char-9")).code(), OK);

  sg::GetPlayerIdentitiesRequest req;
  req.set_player_id("player-1");
  const auto resp = handlers_->HandleGetPlayerIdentities(req);
  EXPECT_EQ(resp.code(), OK);
  EXPECT_EQ(resp.bindings_size(), 2);

  // Unknown players answer OK with an empty list.
  sg::GetPlayerIdentitiesRequest unknown;
  unknown.set_player_id("nobody");
  const auto empty = handlers_->HandleGetPlayerIdentities(unknown);
  EXPECT_EQ(empty.code(), OK);
  EXPECT_EQ(empty.bindings_size(), 0);
}

TEST_F(ServerGatewayTest, ResolveHandlerValidatesAndReportsUnbound) {
  EXPECT_EQ(handlers_->HandleResolveGameUser([] {
              sg::ResolveGameUserRequest req;
              req.set_game_id("game-a");  // game_user_id missing
              return req;
            }()).code(), INVALID_PARAM);

  sg::ResolveGameUserRequest unbound;
  unbound.set_game_id("game-a");
  unbound.set_game_user_id("u-1");
  auto resp = handlers_->HandleResolveGameUser(unbound);
  EXPECT_EQ(resp.code(), OK);
  EXPECT_EQ(resp.player_id(), "");

  EXPECT_EQ(handlers_->HandleBindPlayerIdentity(MakeBindRequest("b1", "player-1", "game-a", "u-1")).code(), OK);
  resp = handlers_->HandleResolveGameUser(unbound);
  EXPECT_EQ(resp.code(), OK);
  EXPECT_EQ(resp.player_id(), "player-1");
}

// ---------------------------------------------------------------------------
// Subscription handlers (wire-facing validation)
// ---------------------------------------------------------------------------

TEST_F(ServerGatewayTest, SubscribeHandlerValidatesMintsAndReportsExisted) {
  // Missing tuple fields are a bad request.
  sg::SubscribePlayerChannelRequest missing;
  missing.set_player_id("player-1");
  missing.set_game_id("game-a");
  EXPECT_EQ(handlers_->HandleSubscribePlayerChannel(missing).code(), INVALID_PARAM);

  // A backend-asserted id is echoed; the same request replays as existed.
  auto resp = handlers_->HandleSubscribePlayerChannel(
      MakeSubscribeRequest("s1", "player-1", "game-a", "world-1"));
  EXPECT_EQ(resp.code(), OK);
  EXPECT_EQ(resp.subscription_id(), "s1");
  EXPECT_FALSE(resp.existed());
  resp = handlers_->HandleSubscribePlayerChannel(
      MakeSubscribeRequest("s1", "player-1", "game-a", "world-1"));
  EXPECT_EQ(resp.code(), OK);
  EXPECT_TRUE(resp.existed());

  // An empty id (the app edge's self-service path) gets a minted one.
  resp = handlers_->HandleSubscribePlayerChannel(
      MakeSubscribeRequest("", "player-2", "game-b", "ranked-1"));
  EXPECT_EQ(resp.code(), OK);
  EXPECT_FALSE(resp.existed());
  EXPECT_TRUE(resp.subscription_id().rfind("sub-", 0) == 0);
}

TEST_F(ServerGatewayTest, UnsubscribeHandlerRequiresExactlyOneSelector) {
  sg::UnsubscribePlayerChannelRequest req;
  EXPECT_EQ(handlers_->HandleUnsubscribePlayerChannel(req).code(), INVALID_PARAM);

  req.set_subscription_id("s1");
  req.set_player_id("player-1");
  req.set_game_id("game-a");
  req.set_channel_id("world-1");
  EXPECT_EQ(handlers_->HandleUnsubscribePlayerChannel(req).code(), INVALID_PARAM);  // both

  req.clear_subscription_id();
  req.clear_channel_id();  // half a triple
  EXPECT_EQ(handlers_->HandleUnsubscribePlayerChannel(req).code(), INVALID_PARAM);

  req.set_channel_id("world-1");  // full triple, unknown target: idempotent OK
  EXPECT_EQ(handlers_->HandleUnsubscribePlayerChannel(req).code(), OK);

  // By id, on an existing subscription.
  ASSERT_EQ(handlers_->HandleSubscribePlayerChannel(
                MakeSubscribeRequest("s9", "player-1", "game-a", "world-1"))
                .code(),
            OK);
  req.set_subscription_id("s9");
  req.clear_player_id();
  req.clear_game_id();
  req.clear_channel_id();
  EXPECT_EQ(handlers_->HandleUnsubscribePlayerChannel(req).code(), OK);
}

TEST_F(ServerGatewayTest, GetHandlerReturnsSubscriptionsForPlayer) {
  sg::GetPlayerSubscriptionsRequest empty;
  EXPECT_EQ(handlers_->HandleGetPlayerSubscriptions(empty).code(), INVALID_PARAM);

  ASSERT_EQ(handlers_->HandleSubscribePlayerChannel(
                MakeSubscribeRequest("s1", "player-1", "game-a", "world-1"))
                .code(),
            OK);
  ASSERT_EQ(handlers_->HandleSubscribePlayerChannel(
                MakeSubscribeRequest("s2", "player-1", "game-b", "ranked-1"))
                .code(),
            OK);

  sg::GetPlayerSubscriptionsRequest all;
  all.set_player_id("player-1");
  auto resp = handlers_->HandleGetPlayerSubscriptions(all);
  EXPECT_EQ(resp.code(), OK);
  ASSERT_EQ(resp.subscriptions_size(), 2);

  all.set_game_id("game-b");
  resp = handlers_->HandleGetPlayerSubscriptions(all);
  ASSERT_EQ(resp.subscriptions_size(), 1);
  EXPECT_EQ(resp.subscriptions(0).subscription_id(), "s2");
}

}  // namespace
