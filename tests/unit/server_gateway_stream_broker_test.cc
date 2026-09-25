// Stream broker consumer tests: an in-memory Redis Streams fake (real RESP
// on the wire through FakeRedisServer) drives the group-consume / ack /
// XAUTOCLAIM-replay state machine, plus failure injections for every
// reconnect path.
#include <gtest/gtest.h>

#include <algorithm>
#include <chrono>
#include <map>
#include <mutex>
#include <set>
#include <string>
#include <thread>
#include <utility>
#include <vector>

#include "fake_servers.h"
#include "logger.h"
#include "network/redis_client.h"
#include "proto/chat.pb.h"
#include "proto/common.pb.h"
#include "proto/game_server_gateway.pb.h"
#include "stream_broker.h"

namespace {

using chirp::game_server_gateway::MessageInjectRequest;
using chirp::game_server_gateway::MessageInjectResponse;
using chirp::game_server_gateway::StreamBrokerConfig;
using chirp::game_server_gateway::StreamBrokerConsumer;
using chirp::game_server_gateway::StreamInjectEnvelope;
using chirp::network::RedisClient;
using chirp::network::RedisResp;

template <typename Pred>
bool WaitFor(Pred pred, std::chrono::milliseconds timeout) {
  const auto deadline = std::chrono::steady_clock::now() + timeout;
  while (std::chrono::steady_clock::now() < deadline) {
    if (pred()) {
      return true;
    }
    std::this_thread::sleep_for(std::chrono::milliseconds(5));
  }
  return pred();
}

// In-memory Redis Streams semantics served over a real RESP socket.
class FakeStreamRedis {
 public:
  FakeStreamRedis() : server_([this](const std::vector<std::string>& a) { return Handle(a); }) {}

  uint16_t port() const { return server_.port(); }

  // Produces an entry without occupying a client connection.
  std::string Add(const std::string& stream, const std::vector<std::string>& fields) {
    std::lock_guard<std::mutex> lock(mu_);
    entries_[stream].push_back(MakeEntry(stream, fields));
    return entries_[stream].back().id;
  }

  size_t Pending(const std::string& stream) {
    std::lock_guard<std::mutex> lock(mu_);
    size_t n = 0;
    for (const auto& e : entries_[stream]) {
      if (e.pending) {
        n++;
      }
    }
    return n;
  }

  // Flat field lists of every entry ever added to the stream.
  std::vector<std::vector<std::string>> All(const std::string& stream) {
    std::lock_guard<std::mutex> lock(mu_);
    std::vector<std::vector<std::string>> out;
    for (const auto& e : entries_[stream]) {
      out.push_back(e.fields);
    }
    return out;
  }

  // Failure injections (all one-shot).
  void DropNextReplyWrite() { drop_next_reply_ = true; }
  void DropNextAck() { drop_next_ack_ = true; }
  void DropNextGroupCreate() { drop_next_create_ = true; }
  void PoisonNextClaim() { poison_next_claim_ = true; }
  void FailNextClaim() { fail_next_claim_ = true; }
  void KickConnections() { server_.CloseAll(); }

 private:
  struct Entry {
    std::string id;
    std::vector<std::string> fields;
    bool delivered = false;
    bool pending = false;
    std::chrono::steady_clock::time_point idle_start;
  };

  Entry MakeEntry(const std::string& stream, const std::vector<std::string>& fields) {
    Entry e;
    e.id = stream + "-" + std::to_string(++seq_);
    e.fields = fields;
    return e;
  }

  std::string EntryResp(const Entry& e) {
    std::string fields = "*" + std::to_string(e.fields.size()) + "\r\n";
    for (const auto& v : e.fields) {
      fields += chirp_test::Bulk(v);
    }
    return "*2\r\n" + chirp_test::Bulk(e.id) + fields;
  }

  // Swallows the in-flight command and closes the connection, so the client
  // sees a transport error instead of a reply.
  std::string DropAndClose() {
    server_.CloseAll();
    return "";
  }

  std::string Handle(const std::vector<std::string>& a) {
    std::lock_guard<std::mutex> lock(mu_);
    if (a.empty()) {
      return "-ERR empty command\r\n";
    }
    if (a[0] == "XGROUP" && a.size() >= 5 && a[1] == "CREATE") {
      if (drop_next_create_) {
        drop_next_create_ = false;
        return DropAndClose();
      }
      if (groups_[a[2]].count(a[3])) {
        return "-BUSYGROUP Consumer Group name already exists\r\n";
      }
      groups_[a[2]].insert(a[3]);
      return chirp_test::Simple("OK");
    }
    if (a[0] == "XADD" && a.size() >= 4 && a.size() % 2 == 0) {
      const std::string key = a[1];
      if (drop_next_reply_) {
        drop_next_reply_ = false;
        return DropAndClose();
      }
      entries_[key].push_back(MakeEntry(key, {a.begin() + 2, a.end()}));
      return chirp_test::Bulk(entries_[key].back().id);
    }
    if (a[0] == "XREADGROUP" && a.size() >= 8) {
      const size_t streams_at = std::distance(a.begin(), std::find(a.begin(), a.end(), "STREAMS"));
      if (streams_at + 2 >= a.size() || a[streams_at + 2] != ">") {
        return "-ERR unsupported XREADGROUP form\r\n";
      }
      const std::string key = a[streams_at + 1];
      if (!groups_[key].count(a[2])) {
        return "-NOGROUP no such group\r\n";
      }
      std::vector<const Entry*> ready;
      for (const auto& e : entries_[key]) {
        if (!e.delivered) {
          ready.push_back(&e);
        }
      }
      if (ready.empty()) {
        return "*-1\r\n";
      }
      // [[key, entries]] - the outer element is a [key, entries] pair array.
      std::string body = "*1\r\n*2\r\n" + chirp_test::Bulk(key) + "*" +
                         std::to_string(ready.size()) + "\r\n";
      for (const auto* e : ready) {
        body += EntryResp(*e);
      }
      for (auto& e : entries_[key]) {
        if (!e.delivered) {
          e.delivered = true;
          e.pending = true;
          e.idle_start = std::chrono::steady_clock::now();
        }
      }
      return body;
    }
    if (a[0] == "XACK" && a.size() >= 4) {
      if (drop_next_ack_) {
        drop_next_ack_ = false;
        return DropAndClose();
      }
      int n = 0;
      for (size_t i = 3; i < a.size(); i++) {
        for (auto& e : entries_[a[1]]) {
          if (e.id == a[i] && e.pending) {
            e.pending = false;
            n++;
          }
        }
      }
      return chirp_test::Int(n);
    }
    if (a[0] == "XAUTOCLAIM" && a.size() >= 6) {
      if (poison_next_claim_) {
        poison_next_claim_ = false;
        return chirp_test::Bulk("garbage");
      }
      if (fail_next_claim_) {
        fail_next_claim_ = false;
        return DropAndClose();
      }
      const std::string key = a[1];
      const long min_idle = std::stol(a[4]);
      const auto now = std::chrono::steady_clock::now();
      std::vector<Entry*> claimed;
      for (auto& e : entries_[key]) {
        if (!e.pending || claimed.size() >= 16) {
          continue;
        }
        const auto idle_ms = std::chrono::duration_cast<std::chrono::milliseconds>(
                                 now - e.idle_start)
                                 .count();
        if (idle_ms >= min_idle) {
          e.idle_start = now;
          claimed.push_back(&e);
        }
      }
      // [next_start, entries] (RESP2, Redis 6.2 shape)
      std::string body = "*2\r\n" + chirp_test::Bulk("0-0") + "*" +
                         std::to_string(claimed.size()) + "\r\n";
      for (const auto* e : claimed) {
        body += EntryResp(*e);
      }
      return body;
    }
    return "-ERR unknown command '" + a[0] + "'\r\n";
  }

  std::mutex mu_;
  uint64_t seq_ = 0;
  bool drop_next_reply_ = false;
  bool drop_next_ack_ = false;
  bool fail_next_claim_ = false;
  bool drop_next_create_ = false;
  bool poison_next_claim_ = false;
  std::map<std::string, std::set<std::string>> groups_;
  std::map<std::string, std::vector<Entry>> entries_;
  chirp_test::FakeRedisServer server_;
};

// Records handler calls; the response code is scriptable mid-test.
struct RecordingHandler {
  std::mutex mu;
  std::vector<MessageInjectRequest> calls;
  chirp::common::ErrorCode code = chirp::common::OK;

  MessageInjectResponse Respond(const MessageInjectRequest& req) {
    std::lock_guard<std::mutex> lock(mu);
    calls.push_back(req);
    MessageInjectResponse resp;
    resp.set_code(code);
    resp.set_inject_id(req.inject_id());
    return resp;
  }

  size_t Count() {
    std::lock_guard<std::mutex> lock(mu);
    return calls.size();
  }
};

StreamBrokerConfig BrokerConfig(const FakeStreamRedis& redis) {
  StreamBrokerConfig config;
  config.redis_host = "127.0.0.1";
  config.redis_port = redis.port();
  config.stream = "inject";
  config.group = "g";
  config.consumer = "c1";
  config.service_secrets = {{"chat", "sec"}};
  config.block_ms = 20;
  config.claim_min_idle_ms = 30;
  config.claim_interval_ms = 40;
  config.reconnect_delay_ms = 10;
  return config;
}

std::vector<std::string> ValidFields() {
  return {"service_id", "chat",     "secret", "sec",      "sender_kind", "NPC",
          "sender_id",  "npc:1",    "channel_type", "PRIVATE", "receiver_id", "player_1",
          "content",    "hello",    "inject_id", "inj-9"};
}

TEST(StreamBrokerParseTest, ParsesEnvelopeWithPrefixAndNumericChannel) {
  StreamInjectEnvelope env;
  ASSERT_TRUE(chirp::game_server_gateway::ParseInjectEnvelope(
      {"service_id", "chat", "sender_kind", "SENDER_NPC", "channel_type", "2", "sender_id", "n",
       "content", "hi"},
      &env));
  EXPECT_EQ(env.service_id, "chat");
  EXPECT_EQ(env.req.sender_kind(), chirp::game_server_gateway::SENDER_NPC);
  EXPECT_EQ(env.req.channel_type(), static_cast<int32_t>(chirp::chat::GUILD));
  EXPECT_TRUE(env.reply_to.empty());
}

TEST(StreamBrokerParseTest, ParsesGameIdAndLeavesItAbsentOnLegacyEntries) {
  StreamInjectEnvelope env;
  ASSERT_TRUE(chirp::game_server_gateway::ParseInjectEnvelope(
      {"sender_kind", "SERVICE", "channel_type", "WORLD", "game_id", "game-a", "channel_id", "c1"},
      &env));
  EXPECT_EQ(env.req.game_id(), "game-a");

  // Legacy entries (no game_id field) keep the direct-injection semantics.
  StreamInjectEnvelope legacy;
  ASSERT_TRUE(chirp::game_server_gateway::ParseInjectEnvelope(
      {"sender_kind", "SYSTEM", "channel_type", "WORLD", "channel_id", "c1"}, &legacy));
  EXPECT_TRUE(legacy.req.game_id().empty());
}

TEST(StreamBrokerParseTest, ParsesEveryKindAndChannelName) {
  StreamInjectEnvelope env;
  ASSERT_TRUE(chirp::game_server_gateway::ParseInjectEnvelope(
      {"sender_kind", "SYSTEM", "channel_type", "TEAM"}, &env));
  EXPECT_EQ(env.req.sender_kind(), chirp::game_server_gateway::SENDER_SYSTEM);
  EXPECT_EQ(env.req.channel_type(), static_cast<int32_t>(chirp::chat::TEAM));

  ASSERT_TRUE(chirp::game_server_gateway::ParseInjectEnvelope(
      {"sender_kind", "SERVICE", "channel_type", "GUILD"}, &env));
  EXPECT_EQ(env.req.sender_kind(), chirp::game_server_gateway::SENDER_SERVICE);
  EXPECT_EQ(env.req.channel_type(), static_cast<int32_t>(chirp::chat::GUILD));

  ASSERT_TRUE(chirp::game_server_gateway::ParseInjectEnvelope(
      {"sender_kind", "NPC", "channel_type", "0"}, &env));
  EXPECT_EQ(env.req.channel_type(), static_cast<int32_t>(chirp::chat::PRIVATE));
}

TEST(StreamBrokerParseTest, RejectsUnmappableEnvelopes) {
  StreamInjectEnvelope env;
  EXPECT_FALSE(chirp::game_server_gateway::ParseInjectEnvelope({"odd"}, &env));
  EXPECT_FALSE(chirp::game_server_gateway::ParseInjectEnvelope({"sender_kind", "WEIRD"}, &env));
  EXPECT_FALSE(chirp::game_server_gateway::ParseInjectEnvelope(
      {"sender_kind", "NPC", "channel_type", "BAD"}, &env));
  EXPECT_FALSE(chirp::game_server_gateway::ParseInjectEnvelope(
      {"sender_kind", "NPC", "channel_type", "9"}, &env));
  EXPECT_FALSE(chirp::game_server_gateway::ParseInjectEnvelope(
      {"sender_kind", "NPC", "channel_type", "abc"}, &env));
}

TEST(StreamBrokerParseTest, RejectsEmptyPartialAndNegativeNumericChannels) {
  StreamInjectEnvelope env;
  // empty -> strtol skipped (value -1)
  EXPECT_FALSE(chirp::game_server_gateway::ParseInjectEnvelope(
      {"sender_kind", "NPC", "channel_type", ""}, &env));
  // trailing garbage: end != begin but *end != '\0'
  EXPECT_FALSE(chirp::game_server_gateway::ParseInjectEnvelope(
      {"sender_kind", "NPC", "channel_type", "2x"}, &env));
  // negative value
  EXPECT_FALSE(chirp::game_server_gateway::ParseInjectEnvelope(
      {"sender_kind", "NPC", "channel_type", "-1"}, &env));
  // no conversion at all: end == begin
  EXPECT_FALSE(chirp::game_server_gateway::ParseInjectEnvelope(
      {"sender_kind", "NPC", "channel_type", "+"}, &env));
}

// Builds an entry list ([[id, [f, v, ...]], ...]) as a RESP array value.
chirp::network::RedisResp MakeEntryList() {
  chirp::network::RedisResp list;
  list.type = RedisResp::Type::kArray;
  auto& pair = list.array.emplace_back();
  pair.type = RedisResp::Type::kArray;
  auto& id = pair.array.emplace_back();
  id.type = RedisResp::Type::kBulkString;
  id.str = "5-1";
  auto& fields = pair.array.emplace_back();
  fields.type = RedisResp::Type::kArray;
  for (const char* v : {"a", "1", "b", "2"}) {
    auto& item = fields.array.emplace_back();
    item.type = RedisResp::Type::kBulkString;
    item.str = v;
  }
  return list;
}

TEST(StreamBrokerParseTest, ParsesStreamReplies) {
  std::vector<chirp::game_server_gateway::StreamEntry> entries;

  chirp::network::RedisResp null_reply;
  EXPECT_TRUE(chirp::game_server_gateway::ParseXReadGroupResp(null_reply, &entries));
  EXPECT_TRUE(entries.empty());

  // XREADGROUP: [[stream_name, entries]]
  chirp::network::RedisResp read_group;
  read_group.type = RedisResp::Type::kArray;
  auto& stream = read_group.array.emplace_back();
  stream.type = RedisResp::Type::kArray;
  auto& name = stream.array.emplace_back();
  name.type = RedisResp::Type::kBulkString;
  name.str = "inject";
  stream.array.push_back(MakeEntryList());

  ASSERT_TRUE(chirp::game_server_gateway::ParseXReadGroupResp(read_group, &entries));
  ASSERT_EQ(entries.size(), 1u);
  EXPECT_EQ(entries[0].id, "5-1");
  ASSERT_EQ(entries[0].fields.size(), 4u);

  // XAUTOCLAIM: [next_start, entries, (deleted ids on 7+)]
  for (const size_t elements : {size_t{2}, size_t{3}}) {
    chirp::network::RedisResp claim;
    claim.type = RedisResp::Type::kArray;
    auto& next = claim.array.emplace_back();
    next.type = RedisResp::Type::kBulkString;
    next.str = "0-0";
    claim.array.push_back(MakeEntryList());
    while (claim.array.size() < elements) {
      claim.array.emplace_back();  // trailing deleted-ids element
    }
    entries.clear();
    ASSERT_TRUE(chirp::game_server_gateway::ParseXAutoClaimResp(claim, &entries));
    ASSERT_EQ(entries.size(), 1u);
    EXPECT_EQ(entries[0].id, "5-1");
  }

  // Structural failures.
  chirp::network::RedisResp scalar;
  scalar.type = RedisResp::Type::kBulkString;
  EXPECT_FALSE(chirp::game_server_gateway::ParseXReadGroupResp(scalar, &entries));
  EXPECT_FALSE(chirp::game_server_gateway::ParseXAutoClaimResp(scalar, &entries));
  chirp::network::RedisResp short_array;
  short_array.type = RedisResp::Type::kArray;
  EXPECT_FALSE(chirp::game_server_gateway::ParseXReadGroupResp(short_array, &entries));
  EXPECT_FALSE(chirp::game_server_gateway::ParseXAutoClaimResp(short_array, &entries));

  // Outer array size != 1 (size 0 above; size 2 takes the same || arm).
  chirp::network::RedisResp outer_size2;
  outer_size2.type = RedisResp::Type::kArray;
  outer_size2.array.emplace_back();
  outer_size2.array.emplace_back();
  EXPECT_FALSE(chirp::game_server_gateway::ParseXReadGroupResp(outer_size2, &entries));
  EXPECT_FALSE(chirp::game_server_gateway::ParseXAutoClaimResp(outer_size2, &entries));

  // Outer size == 1 but the stream element is not an array.
  chirp::network::RedisResp outer_non_array;
  outer_non_array.type = RedisResp::Type::kArray;
  auto& only_str = outer_non_array.array.emplace_back();
  only_str.type = RedisResp::Type::kBulkString;
  only_str.str = "inject";
  EXPECT_FALSE(chirp::game_server_gateway::ParseXReadGroupResp(outer_non_array, &entries));

  auto bad_entry_list = MakeEntryList();
  bad_entry_list.array.clear();
  auto& not_a_pair = bad_entry_list.array.emplace_back();
  not_a_pair.type = RedisResp::Type::kBulkString;  // entry must be [id, fields]
  chirp::network::RedisResp bad_entry;
  bad_entry.type = RedisResp::Type::kArray;
  bad_entry.array.push_back(bad_entry_list);
  EXPECT_FALSE(chirp::game_server_gateway::ParseXReadGroupResp(bad_entry, &entries));
  EXPECT_FALSE(chirp::game_server_gateway::ParseXAutoClaimResp(bad_entry, &entries));

  // Entry with a single element instead of the [id, fields] pair.
  auto short_entry_list = MakeEntryList();
  short_entry_list.array[0].array.resize(1);
  chirp::network::RedisResp short_entry;
  short_entry.type = RedisResp::Type::kArray;
  short_entry.array.push_back(short_entry_list);
  EXPECT_FALSE(chirp::game_server_gateway::ParseXReadGroupResp(short_entry, &entries));
  short_entry.array.clear();
  auto& next = short_entry.array.emplace_back();
  next.type = RedisResp::Type::kBulkString;
  next.str = "0-0";
  short_entry.array.push_back(short_entry_list);
  EXPECT_FALSE(chirp::game_server_gateway::ParseXAutoClaimResp(short_entry, &entries));

  // Fields payload that is not an array.
  auto bad_fields_list = MakeEntryList();
  bad_fields_list.array[0].array[1].type = RedisResp::Type::kInteger;
  chirp::network::RedisResp bad_fields;
  bad_fields.type = RedisResp::Type::kArray;
  bad_fields.array.push_back(bad_fields_list);
  EXPECT_FALSE(chirp::game_server_gateway::ParseXReadGroupResp(bad_fields, &entries));
  bad_fields.array.clear();
  auto& next2 = bad_fields.array.emplace_back();
  next2.type = RedisResp::Type::kBulkString;
  next2.str = "0-0";
  bad_fields.array.push_back(bad_fields_list);
  EXPECT_FALSE(chirp::game_server_gateway::ParseXAutoClaimResp(bad_fields, &entries));

  // XAUTOCLAIM element 2 that is not an entry array.
  chirp::network::RedisResp claim_scalar;
  claim_scalar.type = RedisResp::Type::kArray;
  auto& claim_next = claim_scalar.array.emplace_back();
  claim_next.type = RedisResp::Type::kBulkString;
  claim_next.str = "0-0";
  auto& claim_list = claim_scalar.array.emplace_back();
  claim_list.type = RedisResp::Type::kBulkString;
  EXPECT_FALSE(chirp::game_server_gateway::ParseXAutoClaimResp(claim_scalar, &entries));

  // Outer [key, entries] pair is an array but has size != 2.
  chirp::network::RedisResp pair_wrong_size;
  pair_wrong_size.type = RedisResp::Type::kArray;
  auto& outer = pair_wrong_size.array.emplace_back();
  outer.type = RedisResp::Type::kArray;
  auto& only_name = outer.array.emplace_back();
  only_name.type = RedisResp::Type::kBulkString;
  only_name.str = "inject";
  EXPECT_FALSE(chirp::game_server_gateway::ParseXReadGroupResp(pair_wrong_size, &entries));

  // Entry item is an array with size > 2 (size == 1 is covered above).
  auto long_pair_list = MakeEntryList();
  long_pair_list.array[0].array.resize(3);
  long_pair_list.array[0].array[2].type = RedisResp::Type::kBulkString;
  chirp::network::RedisResp long_entry;
  long_entry.type = RedisResp::Type::kArray;
  long_entry.array.push_back(long_pair_list);
  EXPECT_FALSE(chirp::game_server_gateway::ParseXReadGroupResp(long_entry, &entries));

  // Properly nested [[name, entries]] so ParseEntryList (not the outer
  // guard) rejects a non-array item / non-pair item / fields-not-array.
  auto WrapEntries = [](const chirp::network::RedisResp& entry_list) {
    chirp::network::RedisResp resp;
    resp.type = RedisResp::Type::kArray;
    auto& stream = resp.array.emplace_back();
    stream.type = RedisResp::Type::kArray;
    auto& name = stream.array.emplace_back();
    name.type = RedisResp::Type::kBulkString;
    name.str = "inject";
    stream.array.push_back(entry_list);
    return resp;
  };

  chirp::network::RedisResp scalar_item_list;
  scalar_item_list.type = RedisResp::Type::kArray;
  auto& scalar_item = scalar_item_list.array.emplace_back();
  scalar_item.type = RedisResp::Type::kBulkString;
  scalar_item.str = "not-a-pair";
  EXPECT_FALSE(
      chirp::game_server_gateway::ParseXReadGroupResp(WrapEntries(scalar_item_list), &entries));

  auto size1_item_list = MakeEntryList();
  size1_item_list.array[0].array.resize(1);
  EXPECT_FALSE(chirp::game_server_gateway::ParseXReadGroupResp(WrapEntries(size1_item_list), &entries));

  auto size3_item_list = MakeEntryList();
  size3_item_list.array[0].array.resize(3);
  size3_item_list.array[0].array[2].type = RedisResp::Type::kBulkString;
  EXPECT_FALSE(chirp::game_server_gateway::ParseXReadGroupResp(WrapEntries(size3_item_list), &entries));

  auto bad_fields_nested = MakeEntryList();
  bad_fields_nested.array[0].array[1].type = RedisResp::Type::kInteger;
  EXPECT_FALSE(
      chirp::game_server_gateway::ParseXReadGroupResp(WrapEntries(bad_fields_nested), &entries));
}

TEST(StreamBrokerLifecycleTest, StopWithoutStartIsANoOp) {
  chirp::common::Logger::Instance().SetLevel(chirp::common::Logger::Level::kError);
  FakeStreamRedis redis;
  RecordingHandler handler;
  StreamBrokerConsumer broker(BrokerConfig(redis),
                              [&](const MessageInjectRequest& req) { return handler.Respond(req); });
  // Stop() before Start() returns immediately - there is no thread to join
  // and nothing has been armed yet.
  const auto stopped_at = std::chrono::steady_clock::now();
  broker.Stop();
  EXPECT_LT(std::chrono::duration_cast<std::chrono::milliseconds>(
                std::chrono::steady_clock::now() - stopped_at)
                .count(),
            1000);
  EXPECT_EQ(handler.Count(), 0u);

  // ...and because that Stop was a true no-op, a later Start still arms the
  // consumer: an entry added to the live stream must be consumed.
  broker.Start();
  redis.Add("inject", ValidFields());
  ASSERT_TRUE(WaitFor([&] { return handler.Count() >= 1; }, std::chrono::seconds(5)));
  broker.Stop();
  EXPECT_EQ(handler.Count(), 1u);
  EXPECT_TRUE(redis.All("reply").empty());  // no reply_to field -> no XADD
}

TEST(StreamBrokerLifecycleTest, StopAfterStartDoesNotRestart) {
  chirp::common::Logger::Instance().SetLevel(chirp::common::Logger::Level::kError);
  FakeStreamRedis redis;
  RecordingHandler handler;
  StreamBrokerConsumer broker(BrokerConfig(redis),
                              [&](const MessageInjectRequest& req) { return handler.Respond(req); });
  broker.Start();
  redis.Add("inject", ValidFields());
  ASSERT_TRUE(WaitFor([&] { return handler.Count() >= 1; }, std::chrono::seconds(5)));

  broker.Stop();
  broker.Start();  // must not spawn a second thread

  redis.Add("inject", ValidFields());
  std::this_thread::sleep_for(std::chrono::milliseconds(300));
  // If the restart had worked, this second entry would be consumed by now.
  EXPECT_EQ(handler.Count(), 1u);
  broker.Stop();
}

TEST(StreamBrokerTest, ConsumesValidInjectionAndAcks) {
  chirp::common::Logger::Instance().SetLevel(chirp::common::Logger::Level::kError);
  FakeStreamRedis redis;
  RecordingHandler handler;
  StreamBrokerConsumer broker(BrokerConfig(redis),
                              [&](const MessageInjectRequest& req) { return handler.Respond(req); });
  broker.Start();

  auto fields = ValidFields();
  fields.push_back("reply_to");
  fields.push_back("reply");
  redis.Add("inject", fields);

  ASSERT_TRUE(WaitFor([&] { return handler.Count() >= 1; }, std::chrono::seconds(5)));
  {
    std::lock_guard<std::mutex> lock(handler.mu);
    const auto& req = handler.calls[0];
    EXPECT_EQ(req.inject_id(), "inj-9");
    EXPECT_EQ(req.sender_kind(), chirp::game_server_gateway::SENDER_NPC);
    EXPECT_EQ(req.sender_id(), "npc:1");
    EXPECT_EQ(req.channel_type(), static_cast<int32_t>(chirp::chat::PRIVATE));
    EXPECT_EQ(req.receiver_id(), "player_1");
    EXPECT_EQ(req.content(), "hello");
  }
  EXPECT_TRUE(WaitFor([&] { return redis.Pending("inject") == 0; }, std::chrono::seconds(5)));

  // The reply stream carries the terminal outcome keyed by inject_id.
  ASSERT_TRUE(WaitFor([&] { return redis.All("reply").size() == 1; }, std::chrono::seconds(5)));
  const auto reply = redis.All("reply")[0];
  ASSERT_EQ(reply.size(), 4u);
  EXPECT_EQ(reply[0], "inject_id");
  EXPECT_EQ(reply[1], "inj-9");
  EXPECT_EQ(reply[2], "code");
  EXPECT_EQ(reply[3], "OK");

  broker.Stop();
}

TEST(StreamBrokerTest, GeneratesInjectIdWhenMissing) {
  chirp::common::Logger::Instance().SetLevel(chirp::common::Logger::Level::kError);
  FakeStreamRedis redis;
  RecordingHandler handler;
  StreamBrokerConsumer broker(BrokerConfig(redis),
                              [&](const MessageInjectRequest& req) { return handler.Respond(req); });
  broker.Start();

  auto fields = ValidFields();
  fields.erase(std::find(fields.begin(), fields.end(), "inject_id"));
  fields.erase(std::find(fields.begin(), fields.end(), "inj-9"));
  fields.push_back("reply_to");
  fields.push_back("reply");
  redis.Add("inject", fields);

  ASSERT_TRUE(WaitFor([&] { return redis.All("reply").size() == 1; }, std::chrono::seconds(5)));
  const auto reply = redis.All("reply")[0];
  ASSERT_EQ(reply.size(), 4u);
  EXPECT_EQ(reply[0], "inject_id");
  EXPECT_NE(reply[1].find("c1-"), std::string::npos);  // consumer-prefixed generated id

  broker.Stop();
}

TEST(StreamBrokerTest, ServerUnavailableKeepsPendingUntilReplayed) {
  chirp::common::Logger::Instance().SetLevel(chirp::common::Logger::Level::kError);
  FakeStreamRedis redis;
  RecordingHandler handler;
  handler.code = chirp::common::SERVER_UNAVAILABLE;
  StreamBrokerConsumer broker(BrokerConfig(redis),
                              [&](const MessageInjectRequest& req) { return handler.Respond(req); });
  broker.Start();

  redis.Add("inject", ValidFields());
  ASSERT_TRUE(WaitFor([&] { return handler.Count() >= 1; }, std::chrono::seconds(5)));
  ASSERT_TRUE(WaitFor([&] { return redis.Pending("inject") == 1; }, std::chrono::seconds(5)));

  // Chat is back: the pending entry is claimed and succeeds.
  handler.code = chirp::common::OK;
  EXPECT_TRUE(WaitFor([&] { return handler.Count() >= 2 && redis.Pending("inject") == 0; },
                      std::chrono::seconds(5)));
  {
    std::lock_guard<std::mutex> lock(handler.mu);
    EXPECT_EQ(handler.calls[0].inject_id(), handler.calls[1].inject_id());  // same entry
  }
  EXPECT_TRUE(redis.All("reply").empty());  // no terminal reply for a replayed entry

  broker.Stop();
}

TEST(StreamBrokerTest, InvalidParamIsAckedAndReplied) {
  chirp::common::Logger::Instance().SetLevel(chirp::common::Logger::Level::kError);
  FakeStreamRedis redis;
  RecordingHandler handler;
  handler.code = chirp::common::INVALID_PARAM;
  StreamBrokerConsumer broker(BrokerConfig(redis),
                              [&](const MessageInjectRequest& req) { return handler.Respond(req); });
  broker.Start();

  auto fields = ValidFields();
  fields.push_back("reply_to");
  fields.push_back("reply");
  redis.Add("inject", fields);

  EXPECT_TRUE(WaitFor(
      [&] { return redis.Pending("inject") == 0 && redis.All("reply").size() == 1; },
      std::chrono::seconds(5)));
  const auto reply = redis.All("reply")[0];
  EXPECT_EQ(reply[3], "INVALID_PARAM");

  broker.Stop();
}

TEST(StreamBrokerTest, PoisonAndUnauthorizedEntriesNeverReachTheHandler) {
  chirp::common::Logger::Instance().SetLevel(chirp::common::Logger::Level::kError);
  FakeStreamRedis redis;
  RecordingHandler handler;
  StreamBrokerConsumer broker(BrokerConfig(redis),
                              [&](const MessageInjectRequest& req) { return handler.Respond(req); });
  broker.Start();

  redis.Add("inject", {"odd"});                                        // odd field count
  redis.Add("inject", {"sender_kind", "NPC", "channel_type", "BAD"});  // unmappable
  redis.Add("inject", {"service_id", "chat", "secret", "WRONG", "sender_kind", "SYSTEM",
                       "channel_type", "WORLD", "sender_id", "s", "content", "x", "reply_to",
                       "reply"});  // bad secret

  EXPECT_TRUE(WaitFor([&] { return redis.Pending("inject") == 0; }, std::chrono::seconds(5)));
  EXPECT_EQ(handler.Count(), 0u);

  // Auth failures still answer on the reply stream.
  ASSERT_TRUE(WaitFor([&] { return redis.All("reply").size() == 1; }, std::chrono::seconds(5)));
  const auto reply = redis.All("reply")[0];
  EXPECT_EQ(reply[3], "AUTH_FAILED");

  broker.Stop();
}

TEST(StreamBrokerTest, AckFailureKeepsEntryPendingForReplay) {
  chirp::common::Logger::Instance().SetLevel(chirp::common::Logger::Level::kError);
  FakeStreamRedis redis;
  RecordingHandler handler;
  StreamBrokerConsumer broker(BrokerConfig(redis),
                              [&](const MessageInjectRequest& req) { return handler.Respond(req); });
  broker.Start();

  // The first ack is swallowed: the entry stays pending and is redelivered
  // by the PEL sweep on the reconnected socket.
  redis.DropNextAck();
  redis.Add("inject", ValidFields());
  EXPECT_TRUE(WaitFor([&] { return handler.Count() >= 2 && redis.Pending("inject") == 0; },
                      std::chrono::seconds(5)));

  broker.Stop();
}

TEST(StreamBrokerTest, ReplyWriteFailureDoesNotLoseTheAck) {
  chirp::common::Logger::Instance().SetLevel(chirp::common::Logger::Level::kError);
  FakeStreamRedis redis;
  RecordingHandler handler;
  handler.code = chirp::common::SERVER_UNAVAILABLE;
  StreamBrokerConsumer broker(BrokerConfig(redis),
                              [&](const MessageInjectRequest& req) { return handler.Respond(req); });
  broker.Start();

  auto fields = ValidFields();
  fields.push_back("reply_to");
  fields.push_back("reply");
  redis.Add("inject", fields);
  ASSERT_TRUE(WaitFor([&] { return handler.Count() >= 1 && redis.Pending("inject") == 1; },
                      std::chrono::seconds(5)));

  // Chat is back. The sweep redelivers the entry, the ack lands, but the
  // reply write hits a dropped connection: the connection is recycled while
  // the ack (already durable) survives.
  handler.code = chirp::common::OK;
  redis.DropNextReplyWrite();
  redis.KickConnections();
  EXPECT_TRUE(WaitFor([&] { return handler.Count() >= 2 && redis.Pending("inject") == 0; },
                      std::chrono::seconds(5)));
  EXPECT_TRUE(redis.All("reply").empty());

  // The broker survives and keeps consuming after the reconnect.
  redis.Add("inject", ValidFields());
  EXPECT_TRUE(WaitFor([&] { return handler.Count() >= 3; }, std::chrono::seconds(5)));

  broker.Stop();
}

TEST(StreamBrokerTest, ClaimTransportFailureTriggersReconnect) {
  chirp::common::Logger::Instance().SetLevel(chirp::common::Logger::Level::kError);
  FakeStreamRedis redis;
  RecordingHandler handler;
  StreamBrokerConsumer broker(BrokerConfig(redis),
                              [&](const MessageInjectRequest& req) { return handler.Respond(req); });
  redis.FailNextClaim();  // the very first PEL sweep dies mid-command
  broker.Start();

  // The broker reconnects and keeps consuming.
  redis.Add("inject", ValidFields());
  EXPECT_TRUE(WaitFor([&] { return handler.Count() >= 1 && redis.Pending("inject") == 0; },
                      std::chrono::seconds(5)));

  broker.Stop();
}

TEST(StreamBrokerTest, StopDuringConnectRetryExitsPromptly) {
  chirp::common::Logger::Instance().SetLevel(chirp::common::Logger::Level::kError);
  StreamBrokerConfig config;
  config.redis_host = std::string(300, 'a');
  config.redis_port = 6379;
  config.reconnect_delay_ms = 0;  // spin: the thread then spends all its time
                                  // inside the failing connect attempt
  // Each attempt exits either after the retry sleep or straight from the
  // failed connect; either way Stop must join promptly.
  for (int i = 0; i < 8; ++i) {
    StreamBrokerConsumer broker(
        config, [](const MessageInjectRequest&) { return MessageInjectResponse{}; });
    broker.Start();
    std::this_thread::sleep_for(std::chrono::milliseconds(2));
    const auto stop_at = std::chrono::steady_clock::now();
    broker.Stop();
    const auto stop_ms = std::chrono::duration_cast<std::chrono::milliseconds>(
                             std::chrono::steady_clock::now() - stop_at)
                             .count();
    // A Stop that waited out a retry schedule (or blocked in getaddrinfo)
    // would blow far past this bound.
    EXPECT_LT(stop_ms, 1500) << "iteration " << i;
  }
}

TEST(StreamBrokerTest, ReconnectsAfterConnectionLossAndGroupCreateDrop) {
  chirp::common::Logger::Instance().SetLevel(chirp::common::Logger::Level::kError);
  FakeStreamRedis redis;
  RecordingHandler handler;
  StreamBrokerConsumer broker(BrokerConfig(redis),
                              [&](const MessageInjectRequest& req) { return handler.Respond(req); });
  broker.Start();

  redis.Add("inject", ValidFields());
  ASSERT_TRUE(WaitFor([&] { return handler.Count() >= 1; }, std::chrono::seconds(5)));

  // Dropping the next XGROUP CREATE must not kill the broker: the dropped
  // connection is retried, then BUSYGROUP on the existing group is tolerated.
  redis.DropNextGroupCreate();
  redis.KickConnections();
  redis.Add("inject", ValidFields());
  EXPECT_TRUE(WaitFor([&] { return handler.Count() >= 2; }, std::chrono::seconds(5)));
  EXPECT_TRUE(WaitFor([&] { return redis.Pending("inject") == 0; }, std::chrono::seconds(5)));

  broker.Stop();
}

TEST(StreamBrokerTest, MalformedClaimReplyTriggersReconnect) {
  chirp::common::Logger::Instance().SetLevel(chirp::common::Logger::Level::kError);
  FakeStreamRedis redis;
  RecordingHandler handler;
  StreamBrokerConsumer broker(BrokerConfig(redis),
                              [&](const MessageInjectRequest& req) { return handler.Respond(req); });
  broker.Start();

  redis.PoisonNextClaim();  // first sweep sees garbage and reconnects
  redis.Add("inject", ValidFields());
  EXPECT_TRUE(WaitFor([&] { return handler.Count() >= 1 && redis.Pending("inject") == 0; },
                      std::chrono::seconds(5)));

  broker.Stop();
}

TEST(StreamBrokerTest, ConnectFailuresKeepRetryingWithoutHanging) {
  chirp::common::Logger::Instance().SetLevel(chirp::common::Logger::Level::kError);
  // Unresolvable host and a refusing port both land in the retry loop.
  for (const auto& [host, port] : std::vector<std::pair<std::string, uint16_t>>{
           {std::string(300, 'a'), 6379}, {"127.0.0.1", 1}}) {
    StreamBrokerConfig config;
    config.redis_host = host;
    config.redis_port = port;
    config.reconnect_delay_ms = 10;
    const auto started_at = std::chrono::steady_clock::now();
    StreamBrokerConsumer broker(
        config, [](const MessageInjectRequest&) { return MessageInjectResponse{}; });
    broker.Start();
    std::this_thread::sleep_for(std::chrono::milliseconds(150));
    const auto stop_at = std::chrono::steady_clock::now();
    broker.Stop();
    const auto stop_ms = std::chrono::duration_cast<std::chrono::milliseconds>(
                             std::chrono::steady_clock::now() - stop_at)
                             .count();
    // The retry loop keeps running between Start and Stop; only the join is
    // bounded here, and it must never turn into a hang.
    EXPECT_LT(stop_ms, 3000) << "host=" << host;
    EXPECT_LT(std::chrono::duration_cast<std::chrono::milliseconds>(
                  std::chrono::steady_clock::now() - started_at)
                  .count(),
              5000)
        << "host=" << host;
  }
}

TEST(StreamBrokerTest, EmptyConsumerFallsBackToDefaultPrefix) {
  chirp::common::Logger::Instance().SetLevel(chirp::common::Logger::Level::kError);
  FakeStreamRedis redis;
  RecordingHandler handler;
  auto config = BrokerConfig(redis);
  config.consumer.clear();  // takes the "brk" prefix arm
  StreamBrokerConsumer broker(config,
                              [&](const MessageInjectRequest& req) { return handler.Respond(req); });
  broker.Start();

  auto fields = ValidFields();
  fields.erase(std::find(fields.begin(), fields.end(), "inject_id"));
  fields.erase(std::find(fields.begin(), fields.end(), "inj-9"));
  fields.push_back("reply_to");
  fields.push_back("reply");
  redis.Add("inject", fields);

  ASSERT_TRUE(WaitFor([&] { return redis.All("reply").size() == 1; }, std::chrono::seconds(5)));
  const auto reply = redis.All("reply")[0];
  ASSERT_EQ(reply.size(), 4u);
  EXPECT_NE(reply[1].find("brk-"), std::string::npos);

  broker.Stop();
}

TEST(StreamBrokerTest, StopDuringReconnectSleepExitsTheWaitLoop) {
  chirp::common::Logger::Instance().SetLevel(chirp::common::Logger::Level::kError);
  StreamBrokerConfig config;
  config.redis_host = "127.0.0.1";
  config.redis_port = 1;  // connection refused -> SleepInterruptible path
  config.reconnect_delay_ms = 500;  // long enough for Stop to interrupt mid-sleep
  StreamBrokerConsumer broker(
      config, [](const MessageInjectRequest&) { return MessageInjectResponse{}; });
  broker.Start();
  std::this_thread::sleep_for(std::chrono::milliseconds(30));
  const auto stop_at = std::chrono::steady_clock::now();
  broker.Stop();  // must cut the 500ms sleep short via !stopping_
  const auto stop_ms = std::chrono::duration_cast<std::chrono::milliseconds>(
                           std::chrono::steady_clock::now() - stop_at)
                           .count();
  // SleepInterruptible polls stopping_ every 50ms: an interrupted 500ms
  // sleep joins in well under 400ms, while a missed interrupt would leave
  // Stop waiting out most of the remaining sleep.
  EXPECT_LT(stop_ms, 400);
}

TEST(StreamBrokerTest, EnsureGroupToleratesNonOkAndNonBusygroupReplies) {
  chirp::common::Logger::Instance().SetLevel(chirp::common::Logger::Level::kError);
  // Drive EnsureGroup indirectly: first CREATE answers a non-OK simple
  // string (type-ok but str != "OK"), then a non-BUSYGROUP error, then a
  // non-error non-simple reply — each must fail EnsureGroup and reconnect.
  class WeirdGroupRedis {
   public:
    WeirdGroupRedis() : server_([this](const std::vector<std::string>& a) { return Handle(a); }) {}
    uint16_t port() const { return server_.port(); }
    // XGROUP CREATE attempts observed so far (the retry ladder under test).
    int create_count() {
      std::lock_guard<std::mutex> lock(mu_);
      return create_count_;
    }

   private:
    std::string Handle(const std::vector<std::string>& a) {
      std::lock_guard<std::mutex> lock(mu_);
      if (!a.empty() && a[0] == "XGROUP") {
        const int n = ++create_count_;
        if (n == 1) return "+QUEUED\r\n";                 // simple, not OK
        if (n == 2) return "-ERR no permission\r\n";      // error, not BUSYGROUP
        if (n == 3) return "$-1\r\n";                     // not simple/error
        return chirp_test::Simple("OK");
      }
      if (!a.empty() && a[0] == "XREADGROUP") {
        return "$-1\r\n";  // BLOCK timed out: no data, loop keeps polling
      }
      if (!a.empty() && a[0] == "XAUTOCLAIM") {
        // Well-formed empty claim: once EnsureGroup succeeds the consumer has
        // nothing left to reconnect for, so XGROUP CREATE must stop growing.
        return "*2\r\n$3\r\n0-0\r\n*0\r\n";
      }
      return "-ERR unknown\r\n";
    }
    int create_count_ = 0;
    std::mutex mu_;
    chirp_test::FakeRedisServer server_;
  };

  WeirdGroupRedis redis;
  RecordingHandler handler;
  auto config = BrokerConfig(FakeStreamRedis());
  config.redis_port = redis.port();
  StreamBrokerConsumer broker(config,
                              [&](const MessageInjectRequest& req) { return handler.Respond(req); });
  broker.Start();
  // Three rejected EnsureGroup replies (simple, error, null) and the fourth
  // attempt is accepted: the consumer must survive all three and get through.
  ASSERT_TRUE(WaitFor([&] { return redis.create_count() >= 4; }, std::chrono::seconds(5)));
  const int settled = redis.create_count();
  EXPECT_GE(settled, 4);
  std::this_thread::sleep_for(std::chrono::milliseconds(400));
  // Exactly the three bad replies forced reconnects. With a valid "OK" the
  // consumer settles into its read/claim loop and never re-issues XGROUP
  // CREATE, so the count must not have moved while it ran.
  EXPECT_EQ(redis.create_count(), settled);
  broker.Stop();
}

TEST(StreamBrokerTest, SecretFieldAbsentIsRejected) {
  chirp::common::Logger::Instance().SetLevel(chirp::common::Logger::Level::kError);
  FakeStreamRedis redis;
  RecordingHandler handler;
  StreamBrokerConsumer broker(BrokerConfig(redis),
                              [&](const MessageInjectRequest& req) { return handler.Respond(req); });
  broker.Start();

  // service_id is known but the secret field is missing entirely.
  redis.Add("inject", {"service_id", "chat", "sender_kind", "SYSTEM", "channel_type", "WORLD",
                       "sender_id", "s", "content", "x", "reply_to", "reply"});
  EXPECT_TRUE(WaitFor([&] { return redis.All("reply").size() == 1; }, std::chrono::seconds(5)));
  EXPECT_EQ(handler.Count(), 0u);
  EXPECT_EQ(redis.All("reply")[0][3], "AUTH_FAILED");

  // Unknown service_id short-circuits before the secret lookup.
  redis.Add("inject", {"service_id", "unknown_svc", "secret", "sec", "sender_kind", "SYSTEM",
                       "channel_type", "WORLD", "sender_id", "s", "content", "x", "reply_to",
                       "reply"});
  EXPECT_TRUE(WaitFor([&] { return redis.All("reply").size() == 2; }, std::chrono::seconds(5)));
  EXPECT_EQ(handler.Count(), 0u);
  EXPECT_EQ(redis.All("reply")[1][3], "AUTH_FAILED");

  broker.Stop();
}

TEST(StreamBrokerTest, EmptyReplyToSkipsXAdd) {
  chirp::common::Logger::Instance().SetLevel(chirp::common::Logger::Level::kError);
  FakeStreamRedis redis;
  RecordingHandler handler;
  StreamBrokerConsumer broker(BrokerConfig(redis),
                              [&](const MessageInjectRequest& req) { return handler.Respond(req); });
  broker.Start();

  auto fields = ValidFields();
  fields.push_back("reply_to");
  fields.push_back("");  // present but empty -> no XADD
  redis.Add("inject", fields);

  EXPECT_TRUE(WaitFor([&] { return handler.Count() >= 1 && redis.Pending("inject") == 0; },
                      std::chrono::seconds(5)));
  EXPECT_TRUE(redis.All("").empty());
  EXPECT_TRUE(redis.All("reply").empty());

  broker.Stop();
}

TEST(StreamBrokerCommandTest, RedisClientCommandRoundTrip) {
  chirp::common::Logger::Instance().SetLevel(chirp::common::Logger::Level::kError);
  FakeStreamRedis redis;
  RedisClient client("127.0.0.1", redis.port());

  const auto added = client.Command({"XADD", "cmd-stream", "f", "v"});
  ASSERT_TRUE(added.has_value());
  EXPECT_EQ(added->type, RedisResp::Type::kBulkString);
  EXPECT_EQ(redis.All("cmd-stream").size(), 1u);

  const auto rejected = client.Command({"XGROUP", "CREATE", "cmd-stream", "g", "0"});
  ASSERT_TRUE(rejected.has_value());
  // Missing MKSTREAM on a fresh fake stream is fine for the fake, but an
  // unknown command must surface as a RESP error, not a transport failure.
  const auto err = client.Command({"NOSUCHCMD", "x"});
  ASSERT_TRUE(err.has_value());
  EXPECT_EQ(err->type, RedisResp::Type::kError);

  RedisClient dead("127.0.0.1", 1);
  EXPECT_FALSE(dead.Command({"PING"}).has_value());
}

}  // namespace
