// Seam TU for services/app/notification/src/main.cc: main() is renamed so the
// anonymous-namespace dispatch (SendPacket / HandleNotificationPacket, the
// hoisted body of the on_frame callback both listeners share) can be driven
// directly — batch 9-21 seam convention. main() itself is scaffolding
// (argv / listeners / cleanup timer) carried by the process-level smoke legs.

#include <gtest/gtest.h>

#include <memory>
#include <string>
#include <utility>
#include <vector>

#define main chirp_notification_main
#include "main.cc"
#undef main

namespace {

// Pure in-memory Session mock: records everything sent through it.
class MockSession : public chirp::network::Session {
 public:
  void Send(std::string bytes) override { sent.push_back(std::move(bytes)); }
  void SendAndClose(std::string bytes) override {
    sent.push_back(std::move(bytes));
    close_after_send = true;
  }
  void Close() override { closed = true; }
  bool IsClosed() const override { return closed; }
  std::string RemoteAddress() const override { return "127.0.0.1"; }

  std::vector<std::string> sent;
  bool closed = false;
  bool close_after_send = false;
};

// Strips the u32-BE length prefix produced by ProtobufFraming::Encode.
bool DecodeFramed(const std::string& framed, chirp::gateway::Packet* out) {
  if (framed.size() < 4u) {
    return false;
  }
  const uint32_t len =
      (static_cast<uint32_t>(static_cast<uint8_t>(framed[0])) << 24) |
      (static_cast<uint32_t>(static_cast<uint8_t>(framed[1])) << 16) |
      (static_cast<uint32_t>(static_cast<uint8_t>(framed[2])) << 8) |
      static_cast<uint32_t>(static_cast<uint8_t>(framed[3]));
  if (framed.size() != 4u + static_cast<size_t>(len)) {
    return false;
  }
  return out->ParseFromString(framed.substr(4, len));
}

class NotificationMainSessionTest : public ::testing::Test {
 protected:
  chirp::app_notification::NotificationService svc_{
      chirp::app_notification::FCMConfig{}, chirp::app_notification::APNsConfig{},
      std::make_shared<chirp::app_notification::LoggingPushTransport>()};
  chirp::app_notification::NotificationHandlers handlers_{svc_};
  std::shared_ptr<MockSession> session_ = std::make_shared<MockSession>();

  void RunFrame(chirp::gateway::MsgID id, int64_t seq, const std::string& body) {
    chirp::gateway::Packet pkt;
    pkt.set_msg_id(id);
    pkt.set_sequence(seq);
    pkt.set_body(body);
    HandleNotificationPacket(handlers_, session_, pkt.SerializeAsString());
  }
};

TEST_F(NotificationMainSessionTest, RegisterDeviceRoundTripsFramed) {
  chirp::app_notification::RegisterDeviceRequest req;
  req.set_user_id("u1");
  req.set_device_id("dev-1");
  req.set_platform("android");
  req.set_fcm_token("tok");

  RunFrame(chirp::gateway::REGISTER_DEVICE_REQ, /*seq=*/31, req.SerializeAsString());

  ASSERT_EQ(session_->sent.size(), 1u);
  chirp::gateway::Packet pkt;
  ASSERT_TRUE(DecodeFramed(session_->sent[0], &pkt));
  EXPECT_EQ(pkt.msg_id(), chirp::gateway::REGISTER_DEVICE_RESP);
  EXPECT_EQ(pkt.sequence(), 31);

  chirp::app_notification::RegisterDeviceResponse body;
  ASSERT_TRUE(body.ParseFromString(pkt.body()));
  EXPECT_EQ(body.code(), chirp::common::OK);
  EXPECT_GT(body.server_time(), 0);
  EXPECT_EQ(svc_.GetUserDevices("u1").size(), 1u);
}

TEST_F(NotificationMainSessionTest, GarbagePacketPayloadIsSilent) {
  HandleNotificationPacket(handlers_, session_, std::string("\xff\xff\xff\xff", 4));
  EXPECT_TRUE(session_->sent.empty());
  EXPECT_FALSE(session_->closed);  // a bad frame never tears down the listener
}

TEST_F(NotificationMainSessionTest, UnknownMsgIdSendsNothing) {
  RunFrame(static_cast<chirp::gateway::MsgID>(9999), 5, "whatever");
  EXPECT_TRUE(session_->sent.empty());
  EXPECT_FALSE(session_->closed);
}

TEST_F(NotificationMainSessionTest, GarbageBodyAnswersInvalidParam) {
  RunFrame(chirp::gateway::REGISTER_DEVICE_REQ, 7, "not-proto");

  ASSERT_EQ(session_->sent.size(), 1u);
  chirp::gateway::Packet pkt;
  ASSERT_TRUE(DecodeFramed(session_->sent[0], &pkt));
  EXPECT_EQ(pkt.msg_id(), chirp::gateway::REGISTER_DEVICE_RESP);
  EXPECT_EQ(pkt.sequence(), 7);
  chirp::app_notification::RegisterDeviceResponse body;
  ASSERT_TRUE(body.ParseFromString(pkt.body()));
  EXPECT_EQ(body.code(), chirp::common::INVALID_PARAM);
}

TEST_F(NotificationMainSessionTest, UnregisterThroughSeamRemovesDevice) {
  chirp::app_notification::DeviceRegistration reg;
  reg.device_id = "dev-9";
  reg.user_id = "u9";
  svc_.RegisterDevice(reg);
  ASSERT_EQ(svc_.GetUserDevices("u9").size(), 1u);

  chirp::app_notification::UnregisterDeviceRequest req;
  req.set_user_id("u9");
  req.set_device_id("dev-9");
  RunFrame(chirp::gateway::UNREGISTER_DEVICE_REQ, 9, req.SerializeAsString());

  ASSERT_EQ(session_->sent.size(), 1u);
  chirp::gateway::Packet pkt;
  ASSERT_TRUE(DecodeFramed(session_->sent[0], &pkt));
  EXPECT_EQ(pkt.msg_id(), chirp::gateway::UNREGISTER_DEVICE_RESP);
  chirp::app_notification::UnregisterDeviceResponse body;
  ASSERT_TRUE(body.ParseFromString(pkt.body()));
  EXPECT_EQ(body.code(), chirp::common::OK);
  EXPECT_TRUE(svc_.GetUserDevices("u9").empty());
}

}  // namespace
