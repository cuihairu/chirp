#pragma once

#include <array>
#include <string>
#include <unordered_map>
#include <utility>
#include <vector>

#include "proto/chat.pb.h"

namespace chirp::chat {

// Channel types a player may mute (game_chat_features P0 频道屏蔽): world,
// guild and team. Marquee and SYSTEM_CHANNEL are service broadcasts — the
// player cannot opt out of those; "never hear from this person" on private
// chat belongs to the block-list feature, not here. Order is the stable
// reporting order of GetChannelMutes/GetChannelMutesResponse.
constexpr ChannelType kMuteableChannels[] = {WORLD, GUILD, TEAM};

bool IsMuteableChannel(ChannelType type);

// Per-user push filters. Handlers run on the connection's io thread, so the
// state is touched from that thread only — no lock (same contract as
// ChannelPacer / RepeatGuard; multi-instance deployments keep per-instance
// state). A mute is a push filter: history stays browsable, and messages
// queued before the mute still refill on login.
class DeliveryPrefs {
 public:
  // Applies only to muteable channel types (IsMuteableChannel); anything
  // else returns false without touching state. Returns the resulting state.
  bool SetChannelMuted(const std::string& user_id, ChannelType type, bool muted);

  bool IsChannelMuted(const std::string& user_id, ChannelType type) const;

  // One entry per muteable channel, in kMuteableChannels order, including
  // unmuted ones.
  std::vector<std::pair<ChannelType, bool>> GetChannelMutes(
      const std::string& user_id) const;

 private:
  static int SlotFor(ChannelType type);

  // Per user, one bool per entry of kMuteableChannels.
  std::unordered_map<std::string, std::array<bool, 3>> mutes_;
};

} // namespace chirp::chat
