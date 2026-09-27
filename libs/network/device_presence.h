#pragma once

#include <memory>
#include <string>

#include "network/session.h"
#include "network/session_registry.h"
#include "proto/auth.pb.h"

namespace chirp::network {

/// @brief 多端在线（P0）顶号理由：同 platform 新登录顶掉旧会话时随
/// KICK_NOTIFY 下发的文案。原始 platform 为空时按旧语义回退 "device"
/// （legacy 客户端没有 platform 概念，归一化后的 "default" 不该出现在
/// 玩家可见的文案里）。
std::string LoginKickReason(const std::string& raw_platform);

/// @brief Fills resp->online_devices with the user's live (platform, device)
/// bindings minus `exclude` - the session that just logged in gets the
/// initial listing in its LoginResponse and must not list itself.
void FillOnlineDevices(const std::shared_ptr<SessionRegistry>& state,
                       const std::string& user_id,
                       chirp::auth::LoginResponse* resp,
                       const Session* exclude = nullptr);

/// @brief Notifies every other live session of user_id that one device's
/// online state changed (login -> online, disconnect/kick -> offline), as a
/// DEVICES_PRESENCE_NOTIFY (sequence 0, read-only, style-aligned with
/// KICK_NOTIFY). Registry-local by design: other instances of a multi-node
/// deployment do not hear about it, same as every other local notify.
void BroadcastDevicePresence(const std::shared_ptr<SessionRegistry>& state,
                             const std::string& user_id,
                             const std::string& platform,
                             const std::string& device_id,
                             bool online,
                             const Session* exclude = nullptr);

} // namespace chirp::network
