#pragma once

#include <atomic>
#include <cstdint>
#include <memory>
#include <string>

#include "network/session.h"
#include "proto/chat.pb.h"
#include "proto/gateway.pb.h"

namespace chirp::chat::runtime {

int64_t NowMs();
std::string GetArg(int argc, char** argv, const std::string& key, const std::string& def);
uint16_t ParseU16Arg(int argc, char** argv, const std::string& key, uint16_t def);
int ParseIntArg(int argc, char** argv, const std::string& key, int def);
std::string RandomHex(size_t bytes);

// Monotonic id minting for message/delivery ids. Each instance starts from a
// random counter offset so two processes minting in the same millisecond no
// longer share a counter range (review 表一 P2). The counter stays within
// this instance; the millisecond is embedded for ordering and log triage.
class IdGenerator {
 public:
  explicit IdGenerator(uint64_t seed);
  // Deterministic formatting seam: now_ms is injectable for tests that need
  // two ids minted in the same millisecond.
  std::string Next(const std::string& prefix, int64_t now_ms = NowMs());
  static uint64_t RandomSeed();

 private:
  std::atomic<uint64_t> counter_;
};

std::string GenerateMessageId();
std::string GenerateDeliveryId();

void SendPacket(const std::shared_ptr<network::Session>& session,
                gateway::MsgID msg_id,
                int64_t seq,
                const std::string& body);

void SendChatNotify(const std::shared_ptr<network::Session>& session,
                    const chirp::chat::ChatMessage& msg);

}  // namespace chirp::chat::runtime
