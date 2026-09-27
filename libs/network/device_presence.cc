#include "network/device_presence.h"

#include <chrono>

#include "network/protobuf_framing.h"
#include "proto/gateway.pb.h"

namespace chirp::network {
namespace {

int64_t NowMs() {
  return std::chrono::duration_cast<std::chrono::milliseconds>(
             std::chrono::system_clock::now().time_since_epoch())
      .count();
}

chirp::auth::DevicePresence MakePresence(const std::string& platform,
                                         const std::string& device_id,
                                         bool online) {
  chirp::auth::DevicePresence entry;
  entry.set_platform(platform);
  entry.set_device_id(device_id);
  entry.set_online(online);
  entry.set_ts(NowMs());
  return entry;
}

} // namespace

std::string LoginKickReason(const std::string& raw_platform) {
  return raw_platform.empty() ? "logged in on another device"
                              : "logged in on another " + raw_platform;
}

void FillOnlineDevices(const std::shared_ptr<SessionRegistry>& state,
                       const std::string& user_id,
                       chirp::auth::LoginResponse* resp,
                       const Session* exclude) {
  for (const auto& info : ListOnlineDevices(state, user_id, exclude)) {
    // 初始清单里所有条目都是在线的（offline 不需要列出来）。
    *resp->add_online_devices() = MakePresence(info.platform, info.device_id, true);
  }
}

void BroadcastDevicePresence(const std::shared_ptr<SessionRegistry>& state,
                             const std::string& user_id,
                             const std::string& platform,
                             const std::string& device_id,
                             bool online,
                             const Session* exclude) {
  chirp::auth::DevicesPresenceNotify notify;
  // 单次绑定变化一条；repeated 保留批量能力。
  *notify.add_devices() = MakePresence(NormalizePlatformId(platform), device_id, online);

  chirp::gateway::Packet pkt;
  pkt.set_msg_id(chirp::gateway::DEVICES_PRESENCE_NOTIFY);
  pkt.set_sequence(0);
  pkt.set_body(notify.SerializeAsString());
  const auto framed = ProtobufFraming::Encode(pkt);
  const std::string bytes(reinterpret_cast<const char*>(framed.data()), framed.size());

  for (const auto& session : GetUserSessions(state, user_id)) {
    if (exclude != nullptr && session.get() == exclude) {
      continue;
    }
    session->Send(bytes);
  }
}

} // namespace chirp::network
