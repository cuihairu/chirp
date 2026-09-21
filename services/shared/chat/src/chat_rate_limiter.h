#pragma once

#include <memory>
#include <string>

#include "network/redis_client.h"

namespace chirp::chat {

// Abuse controls for the direct client entry: while chat still accepts SDK
// connections itself (scaffolding login, SEND_MESSAGE_REQ), every login
// attempt is counted per client IP and every validated send per user, in
// fixed 60s windows stored in Redis. The shape mirrors services/app/auth rate
// limiting (GET counter -> SETEX increment), with the same availability
// contract: every failure mode fails OPEN. This is a blunt abuse gate, never
// a self-inflicted outage — when Redis is absent or unreachable the limiter
// is inert.
class ChatRateLimiter {
 public:
  struct Config {
    int max_logins_per_minute_per_ip = 30;
    int max_sends_per_minute_per_user = 120;
    int window_seconds = 60;
  };

  struct Result {
    bool allowed = true;
    int current_count = 0;
    int limit = 0;
  };

  // A null redis disables the limiter (same contract as MessageStore).
  ChatRateLimiter(std::shared_ptr<chirp::network::RedisClient> redis,
                  Config config);

  // Counts one login attempt from client_ip; an unknown address (empty
  // string) is throttled under the shared "unknown" identity.
  Result CheckLogin(const std::string& client_ip);

  // Counts one validated message send by user_id.
  Result CheckSend(const std::string& user_id);

 private:
  Result Check(const std::string& key, int limit);

  std::shared_ptr<chirp::network::RedisClient> redis_;
  Config config_;
};

}  // namespace chirp::chat
