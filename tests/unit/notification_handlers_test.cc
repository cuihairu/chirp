// Unit tests for the notification plane packet handlers (6xxx): routing
// between the Packet envelope and the in-process NotificationService.

#include <gtest/gtest.h>

#include <string>

#include "notification_handlers.h"

using chirp::common::ErrorCode;
using chirp::gateway::Packet;

namespace {

constexpr int64_t kSeq = 77;

Packet MakePacket(chirp::gateway::MsgID msg_id, const std::string& body) {
  Packet pkt;
  pkt.set_msg_id(msg_id);
  pkt.set_sequence(kSeq);
  pkt.set_body(body);
  return pkt;
}

class NotificationHandlersTest : public ::testing::Test {
 protected:
  chirp::notification::NotificationService svc_;
  chirp::notification::NotificationHandlers handlers_{svc_};
};

TEST_F(NotificationHandlersTest, RegistersDeviceAndAnswersPairedResp) {
  chirp::notification::RegisterDeviceRequest req;
  req.set_user_id("u1");
  req.set_device_id("dev-1");
  req.set_platform("android");
  req.set_fcm_token("tok");

  Packet resp;
  ASSERT_TRUE(handlers_.HandlePacket(
      MakePacket(chirp::gateway::REGISTER_DEVICE_REQ, req.SerializeAsString()), &resp));
  EXPECT_EQ(resp.msg_id(), chirp::gateway::REGISTER_DEVICE_RESP);
  EXPECT_EQ(resp.sequence(), kSeq);

  chirp::notification::RegisterDeviceResponse body;
  ASSERT_TRUE(body.ParseFromString(resp.body()));
  EXPECT_EQ(body.code(), ErrorCode::OK);
  EXPECT_GT(body.server_time(), 0);
  EXPECT_EQ(svc_.GetUserDevices("u1").size(), 1u);
}

TEST_F(NotificationHandlersTest, RegisterRejectsGarbageAndMissingIds) {
  Packet resp;
  ASSERT_TRUE(handlers_.HandlePacket(
      MakePacket(chirp::gateway::REGISTER_DEVICE_REQ, "not-proto"), &resp));
  chirp::notification::RegisterDeviceResponse bad;
  ASSERT_TRUE(bad.ParseFromString(resp.body()));
  EXPECT_EQ(bad.code(), ErrorCode::INVALID_PARAM);

  chirp::notification::RegisterDeviceRequest no_ids;  // user_id/device_id empty
  Packet resp2;
  ASSERT_TRUE(handlers_.HandlePacket(
      MakePacket(chirp::gateway::REGISTER_DEVICE_REQ, no_ids.SerializeAsString()), &resp2));
  ASSERT_TRUE(bad.ParseFromString(resp2.body()));
  EXPECT_EQ(bad.code(), ErrorCode::INVALID_PARAM);
}

TEST_F(NotificationHandlersTest, UnregistersDeviceOrReportsUnknown) {
  chirp::notification::DeviceRegistration reg;
  reg.device_id = "dev-2";
  reg.user_id = "u2";
  svc_.RegisterDevice(reg);

  chirp::notification::UnregisterDeviceRequest req;
  req.set_user_id("u2");
  req.set_device_id("dev-2");
  Packet resp;
  ASSERT_TRUE(handlers_.HandlePacket(
      MakePacket(chirp::gateway::UNREGISTER_DEVICE_REQ, req.SerializeAsString()), &resp));
  EXPECT_EQ(resp.msg_id(), chirp::gateway::UNREGISTER_DEVICE_RESP);
  chirp::notification::UnregisterDeviceResponse ok;
  ASSERT_TRUE(ok.ParseFromString(resp.body()));
  EXPECT_EQ(ok.code(), ErrorCode::OK);

  // Unknown device: USER_NOT_FOUND.
  Packet resp2;
  ASSERT_TRUE(handlers_.HandlePacket(
      MakePacket(chirp::gateway::UNREGISTER_DEVICE_REQ, req.SerializeAsString()), &resp2));
  chirp::notification::UnregisterDeviceResponse missing;
  ASSERT_TRUE(missing.ParseFromString(resp2.body()));
  EXPECT_EQ(missing.code(), ErrorCode::USER_NOT_FOUND);

  // Empty device id / garbage body: INVALID_PARAM.
  Packet resp3;
  ASSERT_TRUE(handlers_.HandlePacket(
      MakePacket(chirp::gateway::UNREGISTER_DEVICE_REQ, "garbage"), &resp3));
  chirp::notification::UnregisterDeviceResponse bad;
  ASSERT_TRUE(bad.ParseFromString(resp3.body()));
  EXPECT_EQ(bad.code(), ErrorCode::INVALID_PARAM);
}

TEST_F(NotificationHandlersTest, UpdatesFirstNonEmptyToken) {
  chirp::notification::DeviceRegistration reg;
  reg.device_id = "dev-3";
  reg.user_id = "u3";
  reg.platform = "ios";
  svc_.RegisterDevice(reg);

  chirp::notification::UpdateDeviceTokenRequest req;
  req.set_device_id("dev-3");
  req.set_apns_token("apns-new");
  Packet resp;
  ASSERT_TRUE(handlers_.HandlePacket(
      MakePacket(chirp::gateway::UPDATE_DEVICE_TOKEN_REQ, req.SerializeAsString()), &resp));
  EXPECT_EQ(resp.msg_id(), chirp::gateway::UPDATE_DEVICE_TOKEN_RESP);
  chirp::notification::UpdateDeviceTokenResponse ok;
  ASSERT_TRUE(ok.ParseFromString(resp.body()));
  EXPECT_EQ(ok.code(), ErrorCode::OK);
  EXPECT_EQ(svc_.GetUserDevices("u3")[0].apns_token, "apns-new");

  // All token fields empty: INVALID_PARAM.
  chirp::notification::UpdateDeviceTokenRequest empty;
  empty.set_device_id("dev-3");
  Packet resp2;
  ASSERT_TRUE(handlers_.HandlePacket(
      MakePacket(chirp::gateway::UPDATE_DEVICE_TOKEN_REQ, empty.SerializeAsString()), &resp2));
  ASSERT_TRUE(ok.ParseFromString(resp2.body()));
  EXPECT_EQ(ok.code(), ErrorCode::INVALID_PARAM);

  // Unknown device: USER_NOT_FOUND.
  req.set_device_id("nope");
  Packet resp3;
  ASSERT_TRUE(handlers_.HandlePacket(
      MakePacket(chirp::gateway::UPDATE_DEVICE_TOKEN_REQ, req.SerializeAsString()), &resp3));
  ASSERT_TRUE(ok.ParseFromString(resp3.body()));
  EXPECT_EQ(ok.code(), ErrorCode::USER_NOT_FOUND);
}

TEST_F(NotificationHandlersTest, ListsUserDevices) {
  chirp::notification::DeviceRegistration reg;
  reg.device_id = "dev-4";
  reg.user_id = "u4";
  reg.platform = "web";
  reg.app_version = "1.2";
  reg.os_version = "linux";
  svc_.RegisterDevice(reg);

  chirp::notification::GetUserDevicesRequest req;
  req.set_user_id("u4");
  Packet resp;
  ASSERT_TRUE(handlers_.HandlePacket(
      MakePacket(chirp::gateway::GET_USER_DEVICES_REQ, req.SerializeAsString()), &resp));
  EXPECT_EQ(resp.msg_id(), chirp::gateway::GET_USER_DEVICES_RESP);
  chirp::notification::GetUserDevicesResponse body;
  ASSERT_TRUE(body.ParseFromString(resp.body()));
  ASSERT_EQ(body.devices_size(), 1);
  EXPECT_EQ(body.devices(0).device_id(), "dev-4");
  EXPECT_EQ(body.devices(0).platform(), "web");
  EXPECT_EQ(body.devices(0).app_version(), "1.2");
  EXPECT_EQ(body.devices(0).os_version(), "linux");
  EXPECT_TRUE(body.devices(0).is_active());

  // Empty user_id / garbage: INVALID_PARAM.
  Packet resp2;
  ASSERT_TRUE(handlers_.HandlePacket(
      MakePacket(chirp::gateway::GET_USER_DEVICES_REQ, "garbage"), &resp2));
  ASSERT_TRUE(body.ParseFromString(resp2.body()));
  EXPECT_EQ(body.code(), ErrorCode::INVALID_PARAM);
}

TEST_F(NotificationHandlersTest, PushesToRegisteredDevice) {
  chirp::notification::DeviceRegistration reg;  // tokenless: stub succeeds
  reg.device_id = "dev-5";
  reg.user_id = "u5";
  reg.platform = "android";
  svc_.RegisterDevice(reg);

  chirp::notification::PushNotificationRequest req;
  req.set_user_id("u5");
  req.set_title("t");
  req.set_body("b");
  (*req.mutable_data())["k"] = "v";
  Packet resp;
  ASSERT_TRUE(handlers_.HandlePacket(
      MakePacket(chirp::gateway::PUSH_NOTIFICATION_REQ, req.SerializeAsString()), &resp));
  EXPECT_EQ(resp.msg_id(), chirp::gateway::PUSH_NOTIFICATION_RESP);
  EXPECT_EQ(resp.sequence(), kSeq);
  chirp::notification::PushNotificationResponse ok;
  ASSERT_TRUE(ok.ParseFromString(resp.body()));
  EXPECT_EQ(ok.code(), ErrorCode::OK);

  // A user with no devices: USER_NOT_FOUND.
  req.set_user_id("nobody");
  Packet resp2;
  ASSERT_TRUE(handlers_.HandlePacket(
      MakePacket(chirp::gateway::PUSH_NOTIFICATION_REQ, req.SerializeAsString()), &resp2));
  chirp::notification::PushNotificationResponse missing;
  ASSERT_TRUE(missing.ParseFromString(resp2.body()));
  EXPECT_EQ(missing.code(), ErrorCode::USER_NOT_FOUND);

  // Empty user_id / garbage: INVALID_PARAM.
  Packet resp3;
  ASSERT_TRUE(handlers_.HandlePacket(
      MakePacket(chirp::gateway::PUSH_NOTIFICATION_REQ, "garbage"), &resp3));
  chirp::notification::PushNotificationResponse bad;
  ASSERT_TRUE(bad.ParseFromString(resp3.body()));
  EXPECT_EQ(bad.code(), ErrorCode::INVALID_PARAM);
}

TEST_F(NotificationHandlersTest, UnknownMsgIdReturnsFalse) {
  Packet resp;
  EXPECT_FALSE(handlers_.HandlePacket(
      MakePacket(chirp::gateway::SEND_MESSAGE_REQ, ""), &resp));
  EXPECT_FALSE(handlers_.HandlePacket(
      MakePacket(chirp::gateway::HEARTBEAT_PING, ""), &resp));  // other plane's id
}

}  // namespace
