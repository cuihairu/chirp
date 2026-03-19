#include "redis_auth_store.h"

#include <sstream>
#include <thread>

#include "logger.h"
#include "network/redis_client.h"

namespace chirp::auth {
namespace {

int64_t NowMs() {
  using namespace std::chrono;
  return duration_cast<milliseconds>(system_clock::now().time_since_epoch()).count();
}

std::string SessionKey(const std::string& session_id) {
  return "chirp:auth:session:" + session_id;
}

std::string UserSessionsKey(const std::string& user_id) {
  return "chirp:auth:user_sessions:" + user_id;
}

std::string RefreshTokenKey(const std::string& token_id) {
  return "chirp:auth:refresh_token:" + token_id;
}

std::string UserTokensKey(const std::string& user_id) {
  return "chirp:auth:user_tokens:" + user_id;
}

std::string RateLimitKey(const std::string& key) {
  return "chirp:auth:rate_limit:" + key;
}

std::string FailedLoginKey(const std::string& identifier) {
  return "chirp:auth:failed_login:" + identifier;
}

std::string AccountLockKey(const std::string& user_id) {
  return "chirp:auth:account_lock:" + user_id;
}

std::string UserDevicesKey(const std::string& user_id) {
  return "chirp:auth:user_devices:" + user_id;
}

} // namespace

struct RedisAuthStore::Impl {
  Config config;
  std::shared_ptr<chirp::network::RedisClient> redis;
  asio::io_context& io;
  std::atomic<bool> connected{false};

  Impl(asio::io_context& i, const Config& cfg) : io(i), config(cfg) {
    redis = std::make_shared<chirp::network::RedisClient>(config.host, config.port);
  }
};

RedisAuthStore::RedisAuthStore(asio::io_context& io, const Config& config)
    : impl_(std::make_unique<Impl>(io, config)) {}

RedisAuthStore::~RedisAuthStore() {
  Disconnect();
}

bool RedisAuthStore::Connect() {
  // Simple connection test
  auto result = impl_->redis->Get("ping");
  impl_->connected = result.has_value();
  if (impl_->connected) {
    Logger::Instance().Info("RedisAuthStore connected to " + impl_->config.host +
                           ":" + std::to_string(impl_->config.port));
  }
  return impl_->connected;
}

void RedisAuthStore::Disconnect() {
  impl_->connected = false;
}

bool RedisAuthStore::IsConnected() const {
  return impl_->connected.load();
}

bool RedisAuthStore::StoreSession(const std::string& session_id,
                                 const std::string& user_id,
                                 const std::string& device_id,
                                 const std::string& platform,
                                 int64_t expires_at) {
  if (!impl_->connected) {
    return false;
  }

  // Store session data as hash
  std::string key = SessionKey(session_id);
  std::string value = user_id + "|" + device_id + "|" + platform + "|" +
                     std::to_string(expires_at) + "|" + std::to_string(NowMs());

  int64_t ttl = (expires_at - NowMs()) / 1000;
  if (ttl <= 0) {
    ttl = impl_->config.session_ttl_seconds;
  }

  bool result = impl_->redis->SetEx(key, value, static_cast<int>(ttl));

  if (result) {
    // Add to user's session set
    std::string user_key = UserSessionsKey(user_id);
    impl_->redis->RPush(user_key, session_id);
    impl_->redis->Expire(user_key, ttl);

    // Add to user's devices set
    if (!device_id.empty()) {
      std::string devices_key = UserDevicesKey(user_id);
      std::string device_entry = device_id + ":" + platform;
      impl_->redis->SetEx(devices_key + ":" + device_id, platform, ttl * 24);  // Keep longer
    }
  }

  return result;
}

std::optional<std::string> RedisAuthStore::GetSessionUser(const std::string& session_id) {
  if (!impl_->connected) {
    return std::nullopt;
  }

  auto result = impl_->redis->Get(SessionKey(session_id));
  if (!result) {
    return std::nullopt;
  }

  // Parse value: user_id|device_id|platform|expires_at|created_at
  std::string value = *result;
  size_t pos = value.find('|');
  if (pos != std::string::npos) {
    std::string user_id = value.substr(0, pos);

    // Check expiration
    size_t pos2 = value.find('|', pos + 1);
    if (pos2 != std::string::npos) {
      size_t pos3 = value.find('|', pos2 + 1);
      if (pos3 != std::string::npos) {
        std::string expires_str = value.substr(pos3 + 1, value.find('|', pos3 + 1) - pos3 - 1);
        int64_t expires_at = std::stoll(expires_str);
        if (expires_at > NowMs()) {
          return user_id;
        }
      }
    }
  }

  return std::nullopt;
}

bool RedisAuthStore::UpdateSessionActivity(const std::string& session_id, int64_t activity_time) {
  if (!impl_->connected) {
    return false;
  }

  // Get current session data
  auto result = impl_->redis->Get(SessionKey(session_id));
  if (!result) {
    return false;
  }

  std::string value = *result;
  size_t pos1 = value.find('|');
  size_t pos2 = value.find('|', pos1 + 1);
  size_t pos3 = value.find('|', pos2 + 1);

  if (pos1 == std::string::npos || pos2 == std::string::npos || pos3 == std::string::npos) {
    return false;
  }

  std::string user_id = value.substr(0, pos1);
  std::string device_id = value.substr(pos1 + 1, pos2 - pos1 - 1);
  std::string platform = value.substr(pos2 + 1, pos3 - pos2 - 1);
  size_t pos4 = value.find('|', pos3 + 1);
  std::string expires_str = pos4 != std::string::npos ?
      value.substr(pos3 + 1, pos4 - pos3 - 1) : value.substr(pos3 + 1);
  int64_t expires_at = std::stoll(expires_str);

  // Update with new activity time
  return StoreSession(session_id, user_id, device_id, platform, expires_at);
}

bool RedisAuthStore::DeleteSession(const std::string& session_id) {
  if (!impl_->connected) {
    return false;
  }

  return impl_->redis->Del(SessionKey(session_id));
}

int RedisAuthStore::DeleteAllUserSessions(const std::string& user_id) {
  if (!impl_->connected) {
    return 0;
  }

  // Get all session IDs for user
  auto sessions = impl_->redis->LRange(UserSessionsKey(user_id), 0, -1);
  int count = 0;

  for (const auto& session_id : sessions) {
    if (DeleteSession(session_id)) {
      count++;
    }
  }

  // Clear the user sessions list
  impl_->redis->Del(UserSessionsKey(user_id));

  return count;
}

bool RedisAuthStore::StoreRefreshToken(const std::string& token_id,
                                      const std::string& user_id,
                                      const std::string& session_id,
                                      int64_t expires_at) {
  if (!impl_->connected) {
    return false;
  }

  std::string key = RefreshTokenKey(token_id);
  std::string value = user_id + "|" + session_id + "|" + std::to_string(expires_at);

  int64_t ttl = (expires_at - NowMs()) / 1000;
  if (ttl <= 0) {
    ttl = impl_->config.refresh_token_ttl_seconds;
  }

  bool result = impl_->redis->SetEx(key, value, static_cast<int>(ttl));

  if (result) {
    // Add to user's token set
    std::string user_key = UserTokensKey(user_id);
    impl_->redis->RPush(user_key, token_id);
    impl_->redis->Expire(user_key, ttl);
  }

  return result;
}

std::optional<std::string> RedisAuthStore::GetRefreshTokenUser(const std::string& token_id) {
  if (!impl_->connected) {
    return std::nullopt;
  }

  auto result = impl_->redis->Get(RefreshTokenKey(token_id));
  if (!result) {
    return std::nullopt;
  }

  // Parse value: user_id|session_id|expires_at
  std::string value = *result;
  size_t pos = value.find('|');
  if (pos != std::string::npos) {
    std::string user_id = value.substr(0, pos);

    // Check expiration
    size_t pos2 = value.find('|', pos + 1);
    if (pos2 != std::string::npos) {
      std::string expires_str = value.substr(pos2 + 1);
      int64_t expires_at = std::stoll(expires_str);
      if (expires_at > NowMs()) {
        return user_id;
      }
    }
  }

  return std::nullopt;
}

bool RedisAuthStore::DeleteRefreshToken(const std::string& token_id) {
  if (!impl_->connected) {
    return false;
  }

  return impl_->redis->Del(RefreshTokenKey(token_id));
}

bool RedisAuthStore::CheckRateLimit(const std::string& key, int max_attempts) {
  if (!impl_->connected) {
    return true;  // Allow if Redis not available
  }

  std::string rate_key = RateLimitKey(key);
  auto current = impl_->redis->Get(rate_key);

  int count = current ? std::stoi(*current) : 0;

  if (count >= max_attempts) {
    return false;
  }

  // Increment counter
  impl_->redis->SetEx(rate_key, std::to_string(count + 1), impl_->config.rate_limit_ttl_seconds);
  return true;
}

int RedisAuthStore::GetRateLimitCount(const std::string& key) {
  if (!impl_->connected) {
    return 0;
  }

  auto result = impl_->redis->Get(RateLimitKey(key));
  return result ? std::stoi(*result) : 0;
}

bool RedisAuthStore::ResetRateLimit(const std::string& key) {
  if (!impl_->connected) {
    return false;
  }

  return impl_->redis->Del(RateLimitKey(key));
}

bool RedisAuthStore::RecordFailedLogin(const std::string& identifier, const std::string& ip_address) {
  if (!impl_->connected) {
    return false;
  }

  std::string key = FailedLoginKey(identifier);
  auto current = impl_->redis->Get(key);

  int count = current ? std::stoi(*current) : 0;

  // Store for 15 minutes
  impl_->redis->SetEx(key, std::to_string(count + 1), 900);

  // Also track by IP
  std::string ip_key = FailedLoginKey("ip:" + ip_address);
  auto ip_count = impl_->redis->Get(ip_key);
  impl_->redis->SetEx(ip_key, std::to_string(ip_count ? std::stoi(*ip_count) + 1 : 1), 900);

  return true;
}

int RedisAuthStore::GetFailedLoginCount(const std::string& identifier) {
  if (!impl_->connected) {
    return 0;
  }

  auto result = impl_->redis->Get(FailedLoginKey(identifier));
  return result ? std::stoi(*result) : 0;
}

bool RedisAuthStore::ClearFailedLogins(const std::string& identifier) {
  if (!impl_->connected) {
    return false;
  }

  return impl_->redis->Del(FailedLoginKey(identifier));
}

bool RedisAuthStore::IsAccountLocked(const std::string& user_id) {
  if (!impl_->connected) {
    return false;
  }

  return impl_->redis->Get(AccountLockKey(user_id)).has_value();
}

bool RedisAuthStore::LockAccount(const std::string& user_id, int64_t lock_duration_seconds) {
  if (!impl_->connected) {
    return false;
  }

  std::string key = AccountLockKey(user_id);
  return impl_->redis->SetEx(key, std::to_string(NowMs()), static_cast<int>(lock_duration_seconds));
}

bool RedisAuthStore::UnlockAccount(const std::string& user_id) {
  if (!impl_->connected) {
    return false;
  }

  return impl_->redis->Del(AccountLockKey(user_id));
}

std::vector<std::string> RedisAuthStore::GetUserDevices(const std::string& user_id) {
  if (!impl_->connected) {
    return {};
  }

  // Get all device keys for user
  std::string pattern = UserDevicesKey(user_id) + ":*";
  auto keys = impl_->redis->Keys(pattern);

  std::vector<std::string> devices;
  for (const auto& key : keys) {
    // Extract device_id from key
    size_t pos = key.find_last_key(':');
    if (pos != std::string::npos) {
      devices.push_back(key.substr(pos + 1));
    }
  }

  return devices;
}

bool RedisAuthStore::RemoveDevice(const std::string& user_id, const std::string& device_id) {
  if (!impl_->connected) {
    return false;
  }

  std::string key = UserDevicesKey(user_id) + ":" + device_id;
  return impl_->redis->Del(key);
}

} // namespace chirp::auth
