#include "channel_manager.h"

#include <algorithm>
#include <chrono>

namespace chirp {
namespace chat {

namespace {

// Default permissions for different channel types
ChannelPermissions GetDefaultPermissions(ChannelKind kind) {
  ChannelPermissions perms;
  perms.set_can_read(true);
  perms.set_can_write(true);
  perms.set_can_speak(true);
  perms.set_can_join(true);
  return perms;
}

} // namespace

// ChannelPermissionChecker implementation

bool ChannelPermissionChecker::HasPermission(const Channel& channel,
                                            const std::string& user_id,
                                            const std::string& role_id,
                                            ChannelPermissions::Field field) {
  auto perms = GetEffectivePermissions(channel, user_id, role_id);

  switch (field) {
    case ChannelPermissions::kCanRead:
      return perms.can_read();
    case ChannelPermissions::kCanWrite:
      return perms.can_write();
    case ChannelPermissions::kCanSpeak:
      return perms.can_speak();
    case ChannelPermissions::kCanJoin:
      return perms.can_join();
    case ChannelPermissions::kCanManage:
      return perms.can_manage();
    default:
      return true;  // Default to allow
  }
}

ChannelPermissions ChannelPermissionChecker::GetEffectivePermissions(
    const Channel& channel,
    const std::string& user_id,
    const std::string& role_id) {

  // Start with default permissions for channel type
  auto perms = GetDefaultPermissions(channel.kind());

  // Apply permission overrides in order (deny takes precedence)
  for (const auto& override_entry : channel.permission_overrides()) {
    bool applies = false;

    if (override_entry.type() == PermissionType::PERMISSION_TYPE_ROLE &&
        !role_id.empty() &&
        override_entry.id() == role_id) {
      applies = true;
    } else if (override_entry.type() == PermissionType::PERMISSION_TYPE_USER &&
               override_entry.id() == user_id) {
      applies = true;
    }

    if (applies) {
      // Apply allow overrides
      if (override_entry.has_allow()) {
        const auto& allow = override_entry.allow();
        if (allow.has_can_read()) perms.set_can_read(allow.can_read());
        if (allow.has_can_write()) perms.set_can_write(allow.can_write());
        if (allow.has_can_speak()) perms.set_can_speak(allow.can_speak());
        if (allow.has_can_join()) perms.set_can_join(allow.can_join());
        if (allow.has_can_manage()) perms.set_can_manage(allow.can_manage());
      }

      // Apply deny overrides (takes precedence)
      if (override_entry.has_deny()) {
        const auto& deny = override_entry.deny();
        if (deny.has_can_read()) perms.set_can_read(deny.can_read());
        if (deny.has_can_write()) perms.set_can_write(deny.can_write());
        if (deny.has_can_speak()) perms.set_can_speak(deny.can_speak());
        if (deny.has_can_join()) perms.set_can_join(deny.can_join());
        if (deny.has_can_manage()) perms.set_can_manage(deny.can_manage());
      }
    }
  }

  return perms;
}

// ChannelManager implementation

ChannelManager::ChannelManager() {
  auto now = std::chrono::duration_cast<std::chrono::milliseconds>(
      std::chrono::system_clock::now().time_since_epoch()).count();
}

std::string ChannelManager::GenerateCategoryId() {
  return "cat_" + std::to_string(++category_seq_);
}

std::string ChannelManager::GenerateChannelId() {
  return "ch_" + std::to_string(++channel_seq_);
}

std::string ChannelManager::CreateCategory(const std::string& group_id,
                                          const std::string& name,
                                          int32_t position) {
  auto category = std::make_shared<CategoryData>();
  category->category_id = GenerateCategoryId();
  category->group_id = group_id;
  category->name = name;
  category->position = position;
  category->is_collapsed = false;
  category->created_at = std::chrono::duration_cast<std::chrono::milliseconds>(
      std::chrono::system_clock::now().time_since_epoch()).count();

  {
    std::lock_guard<std::mutex> lock(mu_);
    categories_[category->category_id] = category;
    group_to_categories_[group_id].insert(category->category_id);
  }

  return category->category_id;
}

bool ChannelManager::GetCategory(const std::string& category_id,
                                ChannelCategory* info) {
  std::lock_guard<std::mutex> lock(mu_);
  auto it = categories_.find(category_id);
  if (it == categories_.end()) {
    return false;
  }

  const auto& cat = it->second;
  std::lock_guard<std::mutex> cat_lock(cat->mu);

  info->set_category_id(cat->category_id);
  info->set_group_id(cat->group_id);
  info->set_name(cat->name);
  info->set_position(cat->position);
  info->set_is_collapsed(cat->is_collapsed);
  info->set_created_at(cat->created_at);

  return true;
}

bool ChannelManager::UpdateCategory(const std::string& category_id,
                                   const std::string& name,
                                   int32_t position) {
  std::lock_guard<std::mutex> lock(mu_);
  auto it = categories_.find(category_id);
  if (it == categories_.end()) {
    return false;
  }

  auto& cat = it->second;
  std::lock_guard<std::mutex> cat_lock(cat->mu);

  if (!name.empty()) {
    cat->name = name;
  }
  if (position >= 0) {
    cat->position = position;
  }

  return true;
}

bool ChannelManager::DeleteCategory(const std::string& category_id) {
  std::lock_guard<std::mutex> lock(mu_);
  auto it = categories_.find(category_id);
  if (it == categories_.end()) {
    return false;
  }

  auto& cat = it->second;
  std::string group_id = cat->group_id;

  // Remove channels in this category (uncategorized them)
  for (auto& [ch_id, ch] : channels_) {
    std::lock_guard<std::mutex> ch_lock(ch->mu);
    if (ch->category_id == category_id) {
      ch->category_id.clear();
    }
  }

  categories_.erase(it);
  group_to_categories_[group_id].erase(category_id);

  return true;
}

std::vector<ChannelCategory> ChannelManager::GetCategories(
    const std::string& group_id) {
  std::vector<ChannelCategory> result;

  std::lock_guard<std::mutex> lock(mu_);
  auto it = group_to_categories_.find(group_id);
  if (it == group_to_categories_.end()) {
    return result;
  }

  for (const auto& cat_id : it->second) {
    auto cat_it = categories_.find(cat_id);
    if (cat_it != categories_.end()) {
      ChannelCategory info;
      if (GetCategory(cat_id, &info)) {
        result.push_back(std::move(info));
      }
    }
  }

  SortByPosition(result);
  return result;
}

std::string ChannelManager::CreateChannel(
    const std::string& group_id,
    const std::string& name,
    ChannelKind kind,
    const std::string& category_id,
    const std::string& description,
    const std::vector<PermissionOverrideEntry>& permission_overrides,
    int32_t position) {
  auto channel = std::make_shared<ChannelData>();
  channel->channel_id = GenerateChannelId();
  channel->group_id = group_id;
  channel->category_id = category_id;
  channel->name = name;
  channel->kind = kind;
  channel->position = position;
  channel->description = description;
  channel->is_nsfw = false;
  channel->created_at = std::chrono::duration_cast<std::chrono::milliseconds>(
      std::chrono::system_clock::now().time_since_epoch()).count();
  channel->slowmode_seconds = 0;
  channel->bitrate = kind == ChannelKind::CHANNEL_KIND_VOICE ? 64000 : 0;
  channel->user_limit = 0;
  channel->permission_overrides = permission_overrides;

  {
    std::lock_guard<std::mutex> lock(mu_);
    channels_[channel->channel_id] = channel;
    group_to_channels_[group_id].insert(channel->channel_id);
  }

  return channel->channel_id;
}

bool ChannelManager::GetChannel(const std::string& channel_id, Channel* info) {
  std::lock_guard<std::mutex> lock(mu_);
  auto it = channels_.find(channel_id);
  if (it == channels_.end()) {
    return false;
  }

  const auto& ch = it->second;
  std::lock_guard<std::mutex> ch_lock(ch->mu);

  info->set_channel_id(ch->channel_id);
  info->set_group_id(ch->group_id);
  info->set_category_id(ch->category_id);
  info->set_name(ch->name);
  info->set_kind(ch->kind);
  info->set_position(ch->position);
  info->set_description(ch->description);
  info->set_is_nsfw(ch->is_nsfw);
  info->set_created_at(ch->created_at);
  info->set_slowmode_seconds(ch->slowmode_seconds);
  info->set_bitrate(ch->bitrate);
  info->set_user_limit(ch->user_limit);
  info->set_rtc_region(ch->rtc_region);

  for (const auto& override_entry : ch->permission_overrides) {
    auto* entry = info->add_permission_overrides();
    *entry = override_entry;
  }

  return true;
}

bool ChannelManager::UpdateChannel(
    const std::string& channel_id,
    const std::string& name,
    const std::string& description,
    int32_t position,
    const std::string& category_id,
    const std::vector<PermissionOverrideEntry>& permission_overrides) {
  std::lock_guard<std::mutex> lock(mu_);
  auto it = channels_.find(channel_id);
  if (it == channels_.end()) {
    return false;
  }

  auto& ch = it->second;
  std::lock_guard<std::mutex> ch_lock(ch->mu);

  if (!name.empty()) {
    ch->name = name;
  }
  if (!description.empty()) {
    ch->description = description;
  }
  if (position >= 0) {
    ch->position = position;
  }
  if (!category_id.empty()) {
    ch->category_id = category_id;
  }
  if (!permission_overrides.empty()) {
    ch->permission_overrides = permission_overrides;
  }

  return true;
}

bool ChannelManager::DeleteChannel(const std::string& channel_id) {
  std::lock_guard<std::mutex> lock(mu_);
  auto it = channels_.find(channel_id);
  if (it == channels_.end()) {
    return false;
  }

  auto& ch = it->second;
  std::string group_id = ch->group_id;

  channels_.erase(it);
  group_to_channels_[group_id].erase(channel_id);
  voice_participants_.erase(channel_id);

  return true;
}

std::vector<Channel> ChannelManager::GetChannels(
    const std::string& group_id,
    const std::string& user_id,
    const std::string& role_id) {
  std::vector<Channel> result;

  std::lock_guard<std::mutex> lock(mu_);
  auto it = group_to_channels_.find(group_id);
  if (it == group_to_channels_.end()) {
    return result;
  }

  for (const auto& ch_id : it->second) {
    auto ch_it = channels_.find(ch_id);
    if (ch_it != channels_.end()) {
      Channel info;
      if (GetChannel(ch_id, &info)) {
        // Filter channels user can read
        if (HasPermission(ch_id, user_id, role_id,
                         ChannelPermissions::kCanRead)) {
          result.push_back(std::move(info));
        }
      }
    }
  }

  SortByPosition(result);
  return result;
}

bool ChannelManager::HasPermission(const std::string& channel_id,
                                  const std::string& user_id,
                                  const std::string& role_id,
                                  const ChannelPermissions& required) {
  Channel channel;
  if (!GetChannel(channel_id, &channel)) {
    return false;
  }

  auto perms = ChannelPermissionChecker::GetEffectivePermissions(
      channel, user_id, role_id);

  // Check all required permissions
  if (required.has_can_read() && required.can_read() && !perms.can_read()) {
    return false;
  }
  if (required.has_can_write() && required.can_write() && !perms.can_write()) {
    return false;
  }
  if (required.has_can_speak() && required.can_speak() && !perms.can_speak()) {
    return false;
  }
  if (required.has_can_join() && required.can_join() && !perms.can_join()) {
    return false;
  }
  if (required.has_can_manage() && required.can_manage() && !perms.can_manage()) {
    return false;
  }

  return true;
}

bool ChannelManager::CanRead(const std::string& channel_id,
                            const std::string& user_id,
                            const std::string& role_id) {
  ChannelPermissions required;
  required.set_can_read(true);
  return HasPermission(channel_id, user_id, role_id, required);
}

bool ChannelManager::CanWrite(const std::string& channel_id,
                             const std::string& user_id,
                             const std::string& role_id) {
  ChannelPermissions required;
  required.set_can_write(true);
  return HasPermission(channel_id, user_id, role_id, required);
}

bool ChannelManager::CanSpeak(const std::string& channel_id,
                             const std::string& user_id,
                             const std::string& role_id) {
  ChannelPermissions required;
  required.set_can_speak(true);
  return HasPermission(channel_id, user_id, role_id, required);
}

bool ChannelManager::CanJoin(const std::string& channel_id,
                            const std::string& user_id,
                            const std::string& role_id) {
  ChannelPermissions required;
  required.set_can_join(true);
  return HasPermission(channel_id, user_id, role_id, required);
}

bool ChannelManager::CanSendMessage(const std::string& channel_id,
                                   const std::string& user_id,
                                   int64_t now_ms) {
  std::lock_guard<std::mutex> lock(mu_);
  auto it = channels_.find(channel_id);
  if (it == channels_.end()) {
    return false;
  }

  const auto& ch = it->second;
  std::lock_guard<std::mutex> ch_lock(ch->mu);

  if (ch->slowmode_seconds <= 0) {
    return true;  // No slow mode
  }

  auto slow_it = ch->slowmode_until.find(user_id);
  if (slow_it == ch->slowmode_until.end()) {
    return true;  // Never sent a message
  }

  return now_ms >= slow_it->second;
}

void ChannelManager::RecordMessageSent(const std::string& channel_id,
                                       const std::string& user_id) {
  std::lock_guard<std::mutex> lock(mu_);
  auto it = channels_.find(channel_id);
  if (it == channels_.end()) {
    return;
  }

  auto& ch = it->second;
  std::lock_guard<std::mutex> ch_lock(ch->mu);

  if (ch->slowmode_seconds > 0) {
    auto now = std::chrono::duration_cast<std::chrono::milliseconds>(
        std::chrono::system_clock::now().time_since_epoch()).count();
    ch->slowmode_until[user_id] = now + (ch->slowmode_seconds * 1000);
  }
}

bool ChannelManager::JoinVoiceChannel(const std::string& channel_id,
                                     const std::string& user_id) {
  std::lock_guard<std::mutex> lock(mu_);
  auto it = channels_.find(channel_id);
  if (it == channels_.end()) {
    return false;
  }

  const auto& ch = it->second;
  std::lock_guard<std::mutex> ch_lock(ch->mu);

  if (ch->kind != ChannelKind::CHANNEL_KIND_VOICE &&
      ch->kind != ChannelKind::CHANNEL_KIND_STAGE) {
    return false;
  }

  if (ch->user_limit > 0) {
    auto& participants = voice_participants_[channel_id];
    if (static_cast<int32_t>(participants.size()) >= ch->user_limit) {
      return false;  // Channel full
    }
  }

  voice_participants_[channel_id].insert(user_id);
  return true;
}

bool ChannelManager::LeaveVoiceChannel(const std::string& channel_id,
                                      const std::string& user_id) {
  std::lock_guard<std::mutex> lock(mu_);
  auto it = voice_participants_.find(channel_id);
  if (it == voice_participants_.end()) {
    return false;
  }

  it->second.erase(user_id);
  if (it->second.empty()) {
    voice_participants_.erase(it);
  }

  return true;
}

std::vector<std::string> ChannelManager::GetVoiceChannelParticipants(
    const std::string& channel_id) {
  std::vector<std::string> result;

  std::lock_guard<std::mutex> lock(mu_);
  auto it = voice_participants_.find(channel_id);
  if (it != voice_participants_.end()) {
    result.insert(result.end(), it->second.begin(), it->second.end());
  }

  return result;
}

std::vector<Channel> ChannelManager::SearchChannels(const std::string& group_id,
                                                    const std::string& query) {
  std::vector<Channel> result;

  std::lock_guard<std::mutex> lock(mu_);
  auto it = group_to_channels_.find(group_id);
  if (it == group_to_channels_.end()) {
    return result;
  }

  std::string lower_query = query;
  std::transform(lower_query.begin(), lower_query.end(),
                 lower_query.begin(), ::tolower);

  for (const auto& ch_id : it->second) {
    auto ch_it = channels_.find(ch_id);
    if (ch_it != channels_.end()) {
      const auto& ch = ch_it->second;
      std::lock_guard<std::mutex> ch_lock(ch->mu);

      std::string lower_name = ch->name;
      std::transform(lower_name.begin(), lower_name.end(),
                     lower_name.begin(), ::tolower);

      std::string lower_desc = ch->description;
      std::transform(lower_desc.begin(), lower_desc.end(),
                     lower_desc.begin(), ::tolower);

      if (lower_name.find(lower_query) != std::string::npos ||
          lower_desc.find(lower_query) != std::string::npos) {
        Channel info;
        if (GetChannel(ch_id, &info)) {
          result.push_back(std::move(info));
        }
      }
    }
  }

  return result;
}

} // namespace chat
} // namespace chirp
