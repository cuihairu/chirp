#pragma once

#include <atomic>
#include <cstdint>
#include <functional>
#include <map>
#include <mutex>
#include <string>
#include <thread>
#include <vector>

#include <asio.hpp>

#include "network/redis_protocol.h"
#include "proto/common.pb.h"
#include "proto/game_server_gateway.pb.h"

namespace chirp::game_server_gateway {

// One entry read from the inject stream: its id plus the flat XADD
// field-value pairs.
struct StreamEntry {
  std::string id;
  std::vector<std::string> fields;
};

// Stream-level envelope around the inject request. `service_id`/`secret`
// authenticate the producer (same credentials as the connection plane);
// they are stream-layer fields and do not exist in the proto.
struct StreamInjectEnvelope {
  std::string service_id;
  std::string reply_to;  // optional stream name for the result write-back
  MessageInjectRequest req;
};

// Maps flat XADD field-value pairs to an envelope. Field names:
// service_id, secret, sender_kind (SYSTEM/NPC/SERVICE, SENDER_ prefix
// tolerated), channel_type (PRIVATE/TEAM/GUILD/WORLD or 0..3), sender_id,
// channel_id, receiver_id, content, inject_id, reply_to. Returns false for
// unmappable input (odd field count, missing service_id, unknown enum
// names) - such entries are poison and are acked without replaying.
bool ParseInjectEnvelope(const std::vector<std::string>& flat, StreamInjectEnvelope* out);

// RESP2 shape: [stream, [[id, [field, value, ...]], ...]]; a null reply
// yields an empty list. Returns false on a structurally wrong reply.
bool ParseXReadGroupResp(const chirp::network::RedisResp& resp, std::vector<StreamEntry>* out);

// RESP2 shape: [next_start, [[id, [field, value, ...]], ...], (deleted)]
// - the trailing deleted-ids element only exists on Redis 7+.
bool ParseXAutoClaimResp(const chirp::network::RedisResp& resp, std::vector<StreamEntry>* out);

struct StreamBrokerConfig {
  std::string redis_host;  // empty disables the consumer
  uint16_t redis_port = 6379;
  std::string stream = "chirp:server_plane:inject";
  std::string group = "chirp-plane";
  std::string consumer;  // main.cc defaults it to "<hostname>:<pid>"
  // service_id -> shared secret, the same map as the connection plane.
  std::map<std::string, std::string> service_secrets;
  int block_ms = 1000;            // XREADGROUP BLOCK; also the idle sleep
  int claim_min_idle_ms = 30000;  // XAUTOCLAIM min-idle-time
  int claim_interval_ms = 1000;   // PEL sweep cadence
  int reconnect_delay_ms = 500;
};

// Upstream fallback intake for game backends that cannot host a
// long-connection client: they XADD flat field-value messages to a Redis
// Stream and this consumer turns them into MessageInjectRequest calls.
// At-least-once with explicit acks: OK / INVALID_PARAM / AUTH_FAILED ack
// immediately (malformed and rejected entries must not replay), while
// SERVER_UNAVAILABLE stays in the PEL for XAUTOCLAIM redelivery until the
// chat service is back. The result write-back (when the entry carries
// `reply_to`) happens only on acked terminal outcomes, so a replayed entry
// answers exactly once - "at least once" if the write itself fails.
class StreamBrokerConsumer {
 public:
  using InjectHandler = std::function<MessageInjectResponse(const MessageInjectRequest&)>;

  StreamBrokerConsumer(StreamBrokerConfig config, InjectHandler handle_inject);
  ~StreamBrokerConsumer();  // implies Stop()

  StreamBrokerConsumer(const StreamBrokerConsumer&) = delete;
  StreamBrokerConsumer& operator=(const StreamBrokerConsumer&) = delete;

  void Start();
  void Stop();

 private:
  void Run();
  void SetLive(asio::ip::tcp::socket* s);
  void SleepInterruptible(int ms);

  StreamBrokerConfig config_;
  InjectHandler handle_inject_;
  std::thread thread_;
  std::atomic<bool> stopping_{false};
  bool started_ = false;
  uint64_t inject_seq_ = 0;
  std::mutex mu_;                 // guards live_socket_
  asio::ip::tcp::socket* live_socket_ = nullptr;
};

}  // namespace chirp::game_server_gateway
