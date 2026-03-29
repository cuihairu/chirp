#ifndef CHIRP_SERVICES_CHAT_CHANNEL_MANAGER_H_
#define CHIRP_SERVICES_CHAT_CHANNEL_MANAGER_H_

#include <memory>
#include <mutex>
#include <string>
#include <unordered_map>
#include <unordered_set>
#include <vector>
#include <atomic>

#include "proto/chat.pb.h"

namespace chirp {
namespace chat {

// Permission checker for channels
class ChannelPermissionChecker {
public:
  // Check if user has permission in channel
  static bool HasPermission(const Channel& channel,
                           const std::string& user_id,
                           const std::string& role_id,
                           ChannelPermissions::Field field);

  // Get effective permissions for user in channel
  static ChannelPermissions GetEffectivePermissions(const Channel& channel,
                                                     const std::string& user_id,
                                                     const std::string& role_id);
};

// Channel category data
struct CategoryData {
  std::string category_id;
  std::string group_id;
  std::string name;
  int32_t position = 0;
  bool is_collapsed = false;
  int64_t created_at = 0;
  mutable std::mutex mu;
};

// Channel data
struct ChannelData {
  std::string channel_id;
  std::string group_id;
  std::string category_id;
  std::string name;
  ChannelKind kind = ChannelKind::CHANNEL_KIND_TEXT;
  int32_t position = 0;
  std::string description;
  bool is_nsfw = false;
  int64_t created_at = 0;
  int64_t slowmode_seconds = 0;
  int32_t bitrate = 64000;
  int32_t user_limit = 0;
  std::string rtc_region;

  std::vector<PermissionOverrideEntry> permission_overrides;
  std::unordered_map<std::string, int64_t> slowmode_until;  // user_id -> timestamp

  mutable std::mutex mu;
};

// Manages Discord-like channels within groups/servers
class ChannelManager {
public:
  ChannelManager();
  ~ChannelManager() = default;

  // Category management
  std::string CreateCategory(const std::string& group_id,
                            const std::string& name,
                            int32_t position);

  bool GetCategory(const std::string& category_id, ChannelCategory* info);
  bool UpdateCategory(const std::string& category_id,
                     const std::string& name,
                     int32_t position);
  bool DeleteCategory(const std::string& category_id);

  std::vector<ChannelCategory> GetCategories(const std::string& group_id);

  // Channel management
  std::string CreateChannel(const std::string& group_id,
                           const std::string& name,
                           ChannelKind kind,
                           const std::string& category_id,
                           const std::string& description,
                           const std::vector<PermissionOverrideEntry>& permission_overrides,
                           int32_t position);

  bool GetChannel(const std::string& channel_id, Channel* info);
  bool UpdateChannel(const std::string& channel_id,
                    const std::string& name,
                    const std::string& description,
                    int32_t position,
                    const std::string& category_id,
                    const std::vector<PermissionOverrideEntry>& permission_overrides);
  bool DeleteChannel(const std::string& channel_id);

  std::vector<Channel> GetChannels(const std::string& group_id,
                                  const std::string& user_id,
                                  const std::string& role_id);

  // Permission checking
  bool HasPermission(const std::string& channel_id,
                    const std::string& user_id,
                    const std::string& role_id,
                    const ChannelPermissions& required);

  bool CanRead(const std::string& channel_id,
              const std::string& user_id,
              const std::string& role_id);

  bool CanWrite(const std::string& channel_id,
               const std::string& user_id,
               const std::string& role_id);

  bool CanSpeak(const std::string& channel_id,
               const std::string& user_id,
               const std::string& role_id);

  bool CanJoin(const std::string& channel_id,
              const std::string& user_id,
              const std::string& role_id);

  // Slow mode checking
  bool CanSendMessage(const std::string& channel_id,
                     const std::string& user_id,
                     int64_t now_ms);

  void RecordMessageSent(const std::string& channel_id, const std::string& user_id);

  // Voice channel management
  bool JoinVoiceChannel(const std::string& channel_id, const std::string& user_id);
  bool LeaveVoiceChannel(const std::string& channel_id, const std::string& user_id);
  std::vector<std::string> GetVoiceChannelParticipants(const std::string& channel_id);

  // Channel search
  std::vector<Channel> SearchChannels(const std::string& group_id,
                                     const std::string& query);

private:
  std::string GenerateCategoryId();
  std::string GenerateChannelId();

  // Helper to sort channels by position
  template<typename T>
  void SortByPosition(std::vector<T>& items) {
    std::sort(items.begin(), items.end(),
      [](const T& a, const T& b) {
        return a.position < b.position;
      });
  }

  mutable std::mutex mu_;

  std::atomic<uint64_t> category_seq_{0};
  std::atomic<uint64_t> channel_seq_{0};

  // Storage
  std::unordered_map<std::string, std::shared_ptr<CategoryData>> categories_;
  std::unordered_map<std::string, std::shared_ptr<ChannelData>> channels_;

  // Group to categories/channels mapping
  std::unordered_map<std::string, std::unordered_set<std::string>> group_to_categories_;
  std::unordered_map<std::string, std::unordered_set<std::string>> group_to_channels_;

  // Voice channel participants
  std::unordered_map<std::string, std::unordered_set<std::string>> voice_participants_;
};

} // namespace chat
} // namespace chirp

#endif // CHIRP_SERVICES_CHAT_CHANNEL_MANAGER_H_
