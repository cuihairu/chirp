#include <gtest/gtest.h>

#include "network/protobuf_framing.h"
#include "network/length_prefixed_framer.h"
#include "proto/common.pb.h"
#include "proto/gateway.pb.h"

namespace chirp::network {
namespace {

// ProtobufFraming 测试
TEST(ProtobufFramingTest, EncodeDecode) {
  chirp::gateway::Packet original;
  original.set_msg_id(chirp::gateway::HEARTBEAT_PING);
  original.set_sequence(42);
  original.set_body("test_payload");

  std::vector<uint8_t> encoded = ProtobufFraming::Encode(original);
  EXPECT_FALSE(encoded.empty());
  EXPECT_GE(encoded.size(), 4u); // 至少有 4 字节的长度前缀

  // 解码：先用 LengthPrefixedFramer 拆出 payload，再解析 protobuf。
  LengthPrefixedFramer framer;
  framer.Append(encoded.data(), encoded.size());
  auto payload = framer.PopFrame();
  ASSERT_TRUE(payload.has_value());

  chirp::gateway::Packet decoded;
  bool success = ProtobufFraming::Decode(*payload, &decoded);
  EXPECT_TRUE(success);
  EXPECT_EQ(original.msg_id(), decoded.msg_id());
  EXPECT_EQ(original.sequence(), decoded.sequence());
  EXPECT_EQ(original.body(), decoded.body());
}

TEST(ProtobufFramingTest, DecodeInvalidData) {
  std::string invalid_data = "invalid_protobuf_data";
  chirp::gateway::Packet decoded;
  bool success = ProtobufFraming::Decode(invalid_data, &decoded);
  EXPECT_FALSE(success);
}

TEST(ProtobufFramingTest, DecodeNullptr) {
  std::string valid_data = "some_data";
  bool success = ProtobufFraming::Decode(valid_data, nullptr);
  EXPECT_FALSE(success);
}

// LengthPrefixedFramer 测试
TEST(LengthPrefixedFramerTest, SingleFrame) {
  LengthPrefixedFramer framer;

  // 创建一个带有长度前缀的数据包
  uint32_t length = 12; // payload 大小
  std::vector<uint8_t> data;
  data.reserve(4 + length);
  data.push_back((length >> 24) & 0xFF);
  data.push_back((length >> 16) & 0xFF);
  data.push_back((length >> 8) & 0xFF);
  data.push_back(length & 0xFF);

  for (uint32_t i = 0; i < length; ++i) {
    data.push_back(static_cast<uint8_t>('A' + i));
  }

  framer.Append(data.data(), data.size());

  auto frame = framer.PopFrame();
  EXPECT_TRUE(frame.has_value());
  EXPECT_EQ(12u, frame->size());
  EXPECT_EQ("ABCDEFGHIJKL", *frame);

  // 应该没有更多数据了
  frame = framer.PopFrame();
  EXPECT_FALSE(frame.has_value());
}

TEST(LengthPrefixedFramerTest, PartialFrame) {
  LengthPrefixedFramer framer;

  // 只发送长度字段的一部分
  uint8_t partial_length[2] = {0x00, 0x00};
  framer.Append(partial_length, 2);

  auto frame = framer.PopFrame();
  EXPECT_FALSE(frame.has_value());

  // 发送剩余的长度
  uint8_t rest_length[2] = {0x00, 0x05}; // 长度 = 5
  framer.Append(rest_length, 2);

  frame = framer.PopFrame();
  EXPECT_FALSE(frame.has_value()); // 仍然没有 payload

  // 发送部分 payload
  uint8_t partial_payload[3] = {'A', 'B', 'C'};
  framer.Append(partial_payload, 3);

  frame = framer.PopFrame();
  EXPECT_FALSE(frame.has_value());

  // 发送剩余 payload
  uint8_t rest_payload[2] = {'D', 'E'};
  framer.Append(rest_payload, 2);

  frame = framer.PopFrame();
  EXPECT_TRUE(frame.has_value());
  EXPECT_EQ("ABCDE", *frame);
}

TEST(LengthPrefixedFramerTest, MultipleFrames) {
  LengthPrefixedFramer framer;

  auto create_frame = [](const std::string& payload) -> std::vector<uint8_t> {
    uint32_t length = static_cast<uint32_t>(payload.size());
    std::vector<uint8_t> data;
    data.reserve(4 + length);
    data.push_back((length >> 24) & 0xFF);
    data.push_back((length >> 16) & 0xFF);
    data.push_back((length >> 8) & 0xFF);
    data.push_back(length & 0xFF);
    data.insert(data.end(), payload.begin(), payload.end());
    return data;
  };

  std::vector<uint8_t> frame1 = create_frame("First");
  std::vector<uint8_t> frame2 = create_frame("Second");
  std::vector<uint8_t> frame3 = create_frame("Third");

  // 连续追加多个帧
  framer.Append(frame1.data(), frame1.size());
  framer.Append(frame2.data(), frame2.size());
  framer.Append(frame3.data(), frame3.size());

  // 应该能按顺序读取所有帧
  auto f1 = framer.PopFrame();
  EXPECT_TRUE(f1.has_value());
  EXPECT_EQ("First", *f1);

  auto f2 = framer.PopFrame();
  EXPECT_TRUE(f2.has_value());
  EXPECT_EQ("Second", *f2);

  auto f3 = framer.PopFrame();
  EXPECT_TRUE(f3.has_value());
  EXPECT_EQ("Third", *f3);

  auto f4 = framer.PopFrame();
  EXPECT_FALSE(f4.has_value());
}

TEST(LengthPrefixedFramerTest, EmptyPayload) {
  LengthPrefixedFramer framer;

  uint8_t empty_frame[4] = {0x00, 0x00, 0x00, 0x00};
  framer.Append(empty_frame, 4);

  auto frame = framer.PopFrame();
  EXPECT_TRUE(frame.has_value());
  EXPECT_TRUE(frame->empty());
}

TEST(LengthPrefixedFramerTest, Clear) {
  LengthPrefixedFramer framer;

  uint8_t partial_data[3] = {0x00, 0x00, 0x00};
  framer.Append(partial_data, 3);

  EXPECT_GT(framer.BufferedBytes(), 0u);

  framer.Clear();
  EXPECT_EQ(0u, framer.BufferedBytes());
}

} // namespace
} // namespace chirp::network
