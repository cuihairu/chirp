#pragma once

#include <cstdint>
#include <string>
#include <unordered_map>

#include "proto/chat.pb.h"

namespace chirp::chat {

// Minimum send interval per channel in milliseconds (game_chat_features P0
// 发送频率限制): world 5s, guild 2s, private 1s. 0 = unpaced — team, marquee
// and any future channel type rely on the blunt per-user gate alone.
int64_t MinSendIntervalMs(ChannelType type);

// Per-user, per-channel send pacing. Handlers run on the connection's io
// thread, so the state is touched from that thread only — no lock.
class ChannelPacer {
 public:
  // True = allowed, and the send is recorded at now_ms. False = the user
  // already sent to this channel within MinSendIntervalMs(type); nothing is
  // recorded, so the window stays anchored to the last allowed send.
  bool Allow(const std::string& user_id, ChannelType type, int64_t now_ms);

 private:
  std::unordered_map<std::string, int64_t> last_send_ms_;
};

} // namespace chirp::chat
