#include "brute_force_protector.h"

#include "common/logger.h"
#include "user_store.h"

namespace chirp::auth {

BruteForceProtector::BruteForceProtector(std::shared_ptr<RedisAuthStore> redis_store,
                                        std::shared_ptr<UserStore> user_store,
                                        const Config& config)
    : redis_store_(std::move(redis_store)),
      user_store_(std::move(user_store)),
      config_(config) {}

BruteForceProtector::~BruteForceProtector() = default;

BruteForceProtector::Result BruteForceProtector::CheckLoginAttempt(const std::string& identifier,
                                                                 const std::string& ip_address) {
  Result result;

  std::string normalized_id = NormalizeIdentifier(identifier);

  // Check if account is locked
  if (redis_store_->IsAccountLocked(normalized_id)) {
    result.allowed = false;
    result.permanently_locked = true;
    result.error_message = "Account is temporarily locked due to suspicious activity. "
                          "Please contact support or try again later.";
    Logger::Instance().Warn("Blocked login attempt for locked account: " + normalized_id);
    return result;
  }

  // Check IP-based lock if enabled
  if (config_.lock_by_ip) {
    std::string ip_lock_key = "ip_lock:" + ip_address;
    if (redis_store_->IsAccountLocked(ip_lock_key)) {
      result.allowed = false;
      result.error_message = "Too many failed attempts from this IP address. "
                            "Please try again later.";
      Logger::Instance().Warn("Blocked login attempt from locked IP: " + ip_address);
      return result;
    }
  }

  // Get current failed attempt count
  result.failed_attempts = redis_store_->GetFailedLoginCount(normalized_id);

  // Check if approaching limit
  if (result.failed_attempts >= config_.max_failed_attempts) {
    // Lock the account
    int64_t lock_duration = CalculateLockDuration(result.failed_attempts);
    redis_store_->LockAccount(normalized_id, lock_duration);

    result.allowed = false;
    result.lock_duration_remaining_seconds = lock_duration;
    result.error_message = "Account temporarily locked due to multiple failed attempts. "
                          "Please try again in " + std::to_string(lock_duration / 60) + " minutes.";
    Logger::Instance().Warn("Account locked after " + std::to_string(result.failed_attempts) +
                           " failed attempts: " + normalized_id);
    return result;
  }

  // Warning threshold at 80% of max
  if (result.failed_attempts >= (config_.max_failed_attempts * 4 / 5)) {
    result.error_message = "Warning: " + std::to_string(result.failed_attempts) +
                          " failed attempts. Account will be locked after " +
                          std::to_string(config_.max_failed_attempts) + " attempts.";
  }

  result.allowed = true;
  return result;
}

void BruteForceProtector::RecordFailedAttempt(const std::string& identifier,
                                             const std::string& ip_address) {
  std::string normalized_id = NormalizeIdentifier(identifier);

  redis_store_->RecordFailedLogin(normalized_id, ip_address);

  // Check for permanent lock threshold
  int failed_count = redis_store_->GetFailedLoginCount(normalized_id);
  if (config_.permanent_lock_threshold > 0 &&
      failed_count >= config_.permanent_lock_threshold) {
    redis_store_->LockAccount(normalized_id, config_.max_lock_duration_seconds);
    Logger::Instance().Error("Permanent lock applied to account: " + normalized_id);
  }

  // Also lock IP if enabled and threshold reached
  if (config_.lock_by_ip) {
    int ip_failed_count = redis_store_->GetFailedLoginCount("ip:" + ip_address);
    if (ip_failed_count >= config_.max_failed_attempts) {
      std::string ip_lock_key = "ip_lock:" + ip_address;
      redis_store_->LockAccount(ip_lock_key, config_.base_lock_duration_seconds);
      Logger::Instance().Warn("IP address locked due to failed attempts: " + ip_address);
    }
  }
}

void BruteForceProtector::RecordSuccess(const std::string& identifier,
                                       const std::string& ip_address) {
  std::string normalized_id = NormalizeIdentifier(identifier);

  // Clear failed attempts on successful login
  redis_store_->ClearFailedLogins(normalized_id);
  redis_store_->ClearFailedLogins("ip:" + ip_address);

  // Unlock if was locked
  redis_store_->UnlockAccount(normalized_id);

  Logger::Instance().Info("Successful login for: " + normalized_id + " from " + ip_address);
}

bool BruteForceProtector::LockAccount(const std::string& user_id, int64_t duration_seconds) {
  int64_t duration = duration_seconds > 0 ? duration_seconds : config_.max_lock_duration_seconds;
  return redis_store_->LockAccount(user_id, duration);
}

bool BruteForceProtector::UnlockAccount(const std::string& user_id) {
  return redis_store_->UnlockAccount(user_id);
}

bool BruteForceProtector::IsAccountLocked(const std::string& user_id) {
  return redis_store_->IsAccountLocked(user_id);
}

int BruteForceProtector::GetFailedAttemptCount(const std::string& identifier) {
  std::string normalized_id = NormalizeIdentifier(identifier);
  return redis_store_->GetFailedLoginCount(normalized_id);
}

int64_t BruteForceProtector::CalculateLockDuration(int failed_attempts) {
  // Exponential backoff: base * 2^(attempts - max)
  int excess_attempts = failed_attempts - config_.max_failed_attempts + 1;

  int64_t duration = config_.base_lock_duration_seconds;
  for (int i = 1; i < excess_attempts; ++i) {
    duration *= 2;
    if (duration > config_.max_lock_duration_seconds) {
      duration = config_.max_lock_duration_seconds;
      break;
    }
  }

  return duration;
}

std::string BruteForceProtector::NormalizeIdentifier(const std::string& identifier) {
  // Check if identifier is username or email
  auto user_data = user_store_->FindByUsername(identifier);
  if (user_data) {
    return user_data->user_id;
  }

  user_data = user_store_->FindByEmail(identifier);
  if (user_data) {
    return user_data->user_id;
  }

  // Return as-is if not found
  return identifier;
}

} // namespace chirp::auth
