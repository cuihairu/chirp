#include "notification_handlers.h"

#include <chrono>

namespace chirp {
namespace notification {

namespace {

int64_t NowMs() {
  return std::chrono::duration_cast<std::chrono::milliseconds>(
             std::chrono::system_clock::now().time_since_epoch())
      .count();
}

void Stamp(chirp::gateway::Packet* resp, chirp::gateway::MsgID msg_id, int64_t sequence) {
  resp->set_msg_id(msg_id);
  resp->set_sequence(sequence);
}

}  // namespace

NotificationHandlers::NotificationHandlers(NotificationService& service) : service_(service) {}

bool NotificationHandlers::HandlePacket(const chirp::gateway::Packet& pkt,
                                       chirp::gateway::Packet* resp) {
  switch (pkt.msg_id()) {
    case chirp::gateway::REGISTER_DEVICE_REQ:
      return HandleRegister(pkt, resp);
    case chirp::gateway::UNREGISTER_DEVICE_REQ:
      return HandleUnregister(pkt, resp);
    case chirp::gateway::UPDATE_DEVICE_TOKEN_REQ:
      return HandleUpdateToken(pkt, resp);
    case chirp::gateway::GET_USER_DEVICES_REQ:
      return HandleGetDevices(pkt, resp);
    case chirp::gateway::PUSH_NOTIFICATION_REQ:
      return HandlePush(pkt, resp);
    default:
      return false;
  }
}

bool NotificationHandlers::HandleRegister(const chirp::gateway::Packet& pkt,
                                         chirp::gateway::Packet* resp) {
  RegisterDeviceRequest req;
  RegisterDeviceResponse body;
  Stamp(resp, chirp::gateway::REGISTER_DEVICE_RESP, pkt.sequence());
  if (!req.ParseFromString(pkt.body()) || req.user_id().empty() || req.device_id().empty()) {
    body.set_code(chirp::common::INVALID_PARAM);
  } else {
    DeviceRegistration reg;
    reg.device_id = req.device_id();
    reg.user_id = req.user_id();
    reg.platform = req.platform();
    reg.fcm_token = req.fcm_token();
    reg.apns_token = req.apns_token();
    reg.push_kit_token = req.push_kit_token();
    reg.app_version = req.app_version();
    reg.os_version = req.os_version();
    // RegisterDevice creates or updates and always succeeds.
    body.set_code(chirp::common::OK);
    body.set_server_time(NowMs());
    service_.RegisterDevice(reg);
  }
  resp->set_body(body.SerializeAsString());
  return true;
}

bool NotificationHandlers::HandleUnregister(const chirp::gateway::Packet& pkt,
                                           chirp::gateway::Packet* resp) {
  UnregisterDeviceRequest req;
  UnregisterDeviceResponse body;
  Stamp(resp, chirp::gateway::UNREGISTER_DEVICE_RESP, pkt.sequence());
  if (!req.ParseFromString(pkt.body()) || req.device_id().empty()) {
    body.set_code(chirp::common::INVALID_PARAM);
  } else if (!service_.UnregisterDevice(req.device_id())) {
    body.set_code(chirp::common::USER_NOT_FOUND);
  } else {
    body.set_code(chirp::common::OK);
    body.set_server_time(NowMs());
  }
  resp->set_body(body.SerializeAsString());
  return true;
}

bool NotificationHandlers::HandleUpdateToken(const chirp::gateway::Packet& pkt,
                                            chirp::gateway::Packet* resp) {
  UpdateDeviceTokenRequest req;
  UpdateDeviceTokenResponse body;
  Stamp(resp, chirp::gateway::UPDATE_DEVICE_TOKEN_RESP, pkt.sequence());
  const bool parsed = req.ParseFromString(pkt.body());
  // The service stores one token per device: pick the first non-empty in
  // fcm -> apns -> push_kit order.
  const std::string& token = !req.fcm_token().empty()   ? req.fcm_token()
                             : !req.apns_token().empty() ? req.apns_token()
                                                        : req.push_kit_token();
  if (!parsed || req.device_id().empty() || token.empty()) {
    body.set_code(chirp::common::INVALID_PARAM);
  } else if (!service_.UpdateDeviceToken(req.device_id(), token)) {
    body.set_code(chirp::common::USER_NOT_FOUND);
  } else {
    body.set_code(chirp::common::OK);
    body.set_server_time(NowMs());
  }
  resp->set_body(body.SerializeAsString());
  return true;
}

bool NotificationHandlers::HandleGetDevices(const chirp::gateway::Packet& pkt,
                                           chirp::gateway::Packet* resp) {
  GetUserDevicesRequest req;
  GetUserDevicesResponse body;
  Stamp(resp, chirp::gateway::GET_USER_DEVICES_RESP, pkt.sequence());
  if (!req.ParseFromString(pkt.body()) || req.user_id().empty()) {
    body.set_code(chirp::common::INVALID_PARAM);
  } else {
    body.set_code(chirp::common::OK);
    for (const auto& device : service_.GetUserDevices(req.user_id())) {
      DeviceInfo* info = body.add_devices();
      info->set_device_id(device.device_id);
      info->set_user_id(device.user_id);
      info->set_platform(device.platform);
      info->set_app_version(device.app_version);
      info->set_os_version(device.os_version);
      info->set_registered_at(device.registered_at);
      info->set_is_active(device.is_active);
    }
  }
  resp->set_body(body.SerializeAsString());
  return true;
}

bool NotificationHandlers::HandlePush(const chirp::gateway::Packet& pkt,
                                     chirp::gateway::Packet* resp) {
  PushNotificationRequest req;
  PushNotificationResponse body;
  Stamp(resp, chirp::gateway::PUSH_NOTIFICATION_RESP, pkt.sequence());
  if (!req.ParseFromString(pkt.body()) || req.user_id().empty()) {
    body.set_code(chirp::common::INVALID_PARAM);
  } else {
    NotificationPayload payload;
    payload.title = req.title();
    payload.body = req.body();
    payload.icon = req.icon();
    payload.image = req.image();
    payload.sound = req.sound();
    payload.tag = req.tag();
    payload.badge = req.badge();
    payload.click_action = req.click_action();
    for (const auto& [key, value] : req.data()) {
      payload.data[key] = value;
    }
    body.set_code(service_.SendNotification(req.user_id(), payload) ? chirp::common::OK
                                                                    : chirp::common::USER_NOT_FOUND);
    body.set_server_time(NowMs());
  }
  resp->set_body(body.SerializeAsString());
  return true;
}

}  // namespace notification
}  // namespace chirp
