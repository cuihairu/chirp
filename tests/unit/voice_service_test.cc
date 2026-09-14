// Unit tests for the voice service packet handlers (services/voice/src/main.cc).
// main.cc is included with main() renamed; handlers are driven directly with
// in-memory MockSessions, so room state, broadcast fan-out and the targeted
// WebRTC signaling relay are all pinned without sockets.

#include <gtest/gtest.h>

#include <memory>
#include <string>
#include <vector>

#include "proto/common.pb.h"
#include "proto/gateway.pb.h"
#include "proto/voice.pb.h"

// Relative path, same convention as the app gateway test: unambiguous even
// if another main.cc ever lands on the include path.
#define main chirp_voice_main
#include "../../services/voice/src/main.cc"
#undef main

namespace {

using chirp::gateway::Packet;

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

Packet MakePacket(chirp::gateway::MsgID id, int64_t seq, const std::string& body) {
  Packet pkt;
  pkt.set_msg_id(id);
  pkt.set_sequence(seq);
  pkt.set_body(body);
  return pkt;
}

// Strips the u32-BE length prefix produced by ProtobufFraming::Encode and
// parses the payload (ProtobufFraming::Decode itself expects a bare message).
template <typename T>
bool DecodeFramed(const std::string& framed, T* out) {
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

// Collects every Packet a mock session has received.
std::vector<Packet> ReceivedPackets(const MockSession& s) {
  std::vector<Packet> out;
  for (const auto& framed : s.sent) {
    Packet pkt;
    if (DecodeFramed(framed, &pkt)) {
      out.push_back(pkt);
    }
  }
  return out;
}

// Parses the most recent frame as a Packet and its body as T.
template <typename T>
bool LastBody(const MockSession& s, T* out) {
  if (s.sent.empty()) {
    return false;
  }
  Packet pkt;
  if (!DecodeFramed(s.sent.back(), &pkt)) {
    return false;
  }
  return out->ParseFromString(pkt.body());
}

class VoiceServiceTest : public ::testing::Test {
 protected:
  std::shared_ptr<VoiceState> state_ = std::make_shared<VoiceState>();
  std::shared_ptr<MockSession> session_ = std::make_shared<MockSession>();

  void SendPacketBody(chirp::gateway::MsgID id, int64_t seq, const std::string& body) {
    HandlePacket(state_, session_, MakePacket(id, seq, body).SerializeAsString());
  }

  // Creates a room with the given capacity and returns its id.
  std::string CreateRoom(int32_t max_participants) {
    chirp::voice::CreateRoomRequest req;
    req.set_user_id("creator");
    req.set_room_type(chirp::voice::GROUP);
    req.set_room_name("test room");
    req.set_max_participants(max_participants);
    SendPacketBody(chirp::gateway::CREATE_ROOM_REQ, 1, req.SerializeAsString());
    chirp::voice::CreateRoomResponse resp;
    EXPECT_TRUE(LastBody(*session_, &resp));
    EXPECT_EQ(resp.code(), chirp::common::OK);
    return resp.room_id();
  }

  // Joins user_id (on its own fresh session) and returns that session.
  std::shared_ptr<MockSession> Join(const std::string& user_id, const std::string& room_id,
                                    chirp::common::ErrorCode expect = chirp::common::OK) {
    auto s = std::make_shared<MockSession>();
    chirp::voice::JoinRoomRequest req;
    req.set_user_id(user_id);
    req.set_room_id(room_id);
    HandlePacket(state_, s, MakePacket(chirp::gateway::JOIN_ROOM_REQ, 2, req.SerializeAsString()).SerializeAsString());
    chirp::voice::JoinRoomResponse resp;
    EXPECT_FALSE(s->sent.empty());
    if (!s->sent.empty()) {
      EXPECT_TRUE(LastBody(*s, &resp));
      EXPECT_EQ(resp.code(), expect) << "user " << user_id;
    }
    return s;
  }
};

TEST_F(VoiceServiceTest, CreateRoomHappyPath) {
  chirp::voice::CreateRoomRequest req;
  req.set_user_id("creator");
  req.set_room_type(chirp::voice::PEER_TO_PEER);
  req.set_room_name("call");
  req.set_max_participants(2);
  SendPacketBody(chirp::gateway::CREATE_ROOM_REQ, 7, req.SerializeAsString());

  ASSERT_EQ(session_->sent.size(), 1u);
  chirp::voice::CreateRoomResponse resp;
  ASSERT_TRUE(LastBody(*session_, &resp));
  EXPECT_EQ(resp.code(), chirp::common::OK);
  EXPECT_EQ(resp.room_id().rfind("room_", 0), 0u) << resp.room_id();

  std::lock_guard<std::mutex> lock(state_->mu);
  EXPECT_EQ(state_->rooms.count(resp.room_id()), 1u);
}

TEST_F(VoiceServiceTest, CreateRoomGarbageBodyRejected) {
  SendPacketBody(chirp::gateway::CREATE_ROOM_REQ, 7, "\xff\xfe\xfd");

  ASSERT_EQ(session_->sent.size(), 1u);
  chirp::voice::CreateRoomResponse resp;
  ASSERT_TRUE(LastBody(*session_, &resp));
  EXPECT_EQ(resp.code(), chirp::common::INVALID_PARAM);
}

TEST_F(VoiceServiceTest, JoinUnknownRoomRejected) {
  chirp::voice::JoinRoomRequest req;
  req.set_user_id("u1");
  req.set_room_id("room_does_not_exist");
  SendPacketBody(chirp::gateway::JOIN_ROOM_REQ, 3, req.SerializeAsString());

  ASSERT_EQ(session_->sent.size(), 1u);
  chirp::voice::JoinRoomResponse resp;
  ASSERT_TRUE(LastBody(*session_, &resp));
  EXPECT_EQ(resp.code(), chirp::common::USER_NOT_FOUND);
}

TEST_F(VoiceServiceTest, JoinRoomRegistersParticipantAndBindsSession) {
  const std::string room = CreateRoom(0);

  auto s = Join("u1", room);

  chirp::voice::JoinRoomResponse resp;
  ASSERT_TRUE(LastBody(*s, &resp));
  EXPECT_EQ(resp.room_id(), room);
  EXPECT_EQ(resp.sdp_answer(), "");  // no sdp_offer sent
  ASSERT_EQ(resp.participant_ids_size(), 1);
  EXPECT_EQ(resp.participant_ids(0), "u1");

  std::lock_guard<std::mutex> lock(state_->mu);
  EXPECT_EQ(state_->user_to_room["u1"], room);
  // Session bound: broadcasts and disconnect cleanup can find this user now.
  ASSERT_EQ(state_->user_to_session.count("u1"), 1u);
  EXPECT_EQ(state_->user_to_session["u1"].lock().get(), s.get());
  ASSERT_EQ(state_->session_to_user.count(s.get()), 1u);
  EXPECT_EQ(state_->session_to_user[s.get()], "u1");
}

TEST_F(VoiceServiceTest, JoinRoomSdpOfferEchoedAsAnswer) {
  const std::string room = CreateRoom(0);
  auto s = std::make_shared<MockSession>();
  chirp::voice::JoinRoomRequest req;
  req.set_user_id("u1");
  req.set_room_id(room);
  req.set_sdp_offer("v=0 offer");
  HandlePacket(state_, s, MakePacket(chirp::gateway::JOIN_ROOM_REQ, 4, req.SerializeAsString()).SerializeAsString());

  chirp::voice::JoinRoomResponse resp;
  ASSERT_TRUE(LastBody(*s, &resp));
  EXPECT_EQ(resp.sdp_answer(), "v=0 offer");
}

TEST_F(VoiceServiceTest, JoinNotifiesExistingParticipants) {
  const std::string room = CreateRoom(0);
  auto u1 = Join("u1", room);

  auto u2 = std::make_shared<MockSession>();
  chirp::voice::JoinRoomRequest req;
  req.set_user_id("u2");
  req.set_room_id(room);
  HandlePacket(state_, u2, MakePacket(chirp::gateway::JOIN_ROOM_REQ, 5, req.SerializeAsString()).SerializeAsString());

  // u1 must receive the join notification; u2's own session only gets the join
  // response (the notification excludes the joiner).
  bool u1_notified = false;
  for (const auto& pkt : ReceivedPackets(*u1)) {
    if (pkt.msg_id() == chirp::gateway::PARTICIPANT_JOINED_NOTIFY) {
      chirp::voice::ParticipantJoinedNotify notify;
      ASSERT_TRUE(notify.ParseFromString(pkt.body()));
      EXPECT_EQ(notify.participant().user_id(), "u2");
      u1_notified = true;
    }
  }
  EXPECT_TRUE(u1_notified);
  for (const auto& pkt : ReceivedPackets(*u2)) {
    EXPECT_NE(pkt.msg_id(), chirp::gateway::PARTICIPANT_JOINED_NOTIFY);
  }
}

TEST_F(VoiceServiceTest, JoinFullRoomRejectedWithoutStateChange) {
  const std::string room = CreateRoom(2);
  Join("u1", room);
  Join("u2", room);

  // u3 is currently in no room; a rejected join must not create a mapping.
  auto u3 = std::make_shared<MockSession>();
  chirp::voice::JoinRoomRequest req;
  req.set_user_id("u3");
  req.set_room_id(room);
  HandlePacket(state_, u3, MakePacket(chirp::gateway::JOIN_ROOM_REQ, 6, req.SerializeAsString()).SerializeAsString());

  chirp::voice::JoinRoomResponse resp;
  ASSERT_TRUE(LastBody(*u3, &resp));
  EXPECT_EQ(resp.code(), chirp::common::INTERNAL_ERROR);  // room full

  std::lock_guard<std::mutex> lock(state_->mu);
  EXPECT_EQ(state_->user_to_room.count("u3"), 0u);
  std::lock_guard<std::mutex> room_lock(state_->rooms[room]->mu);
  EXPECT_EQ(state_->rooms[room]->participants.count("u3"), 0u);
}

TEST_F(VoiceServiceTest, JoinFullRoomKeepsPreviousMembership) {
  const std::string r1 = CreateRoom(1);
  const std::string r2 = CreateRoom(1);
  Join("u1", r1);
  Join("u2", r2);  // r2 is now full

  // u1 tries to switch into the full r2: rejected, and its r1 membership must
  // survive (the old code evicted it before checking capacity).
  Join("u1", r2, chirp::common::INTERNAL_ERROR);

  std::lock_guard<std::mutex> lock(state_->mu);
  EXPECT_EQ(state_->user_to_room["u1"], r1);
  std::lock_guard<std::mutex> r1_lock(state_->rooms[r1]->mu);
  EXPECT_EQ(state_->rooms[r1]->participants.count("u1"), 1u);
}

TEST_F(VoiceServiceTest, SwitchRoomRemovesFromPreviousRoom) {
  const std::string r1 = CreateRoom(0);
  const std::string r2 = CreateRoom(0);
  auto u1 = Join("u1", r1);
  auto u2 = Join("u2", r1);

  Join("u1", r2);

  std::lock_guard<std::mutex> lock(state_->mu);
  EXPECT_EQ(state_->user_to_room["u1"], r2);
  {
    std::lock_guard<std::mutex> r1_lock(state_->rooms[r1]->mu);
    EXPECT_EQ(state_->rooms[r1]->participants.count("u1"), 0u);
    EXPECT_EQ(state_->rooms[r1]->participants.count("u2"), 1u);
  }
  {
    std::lock_guard<std::mutex> r2_lock(state_->rooms[r2]->mu);
    EXPECT_EQ(state_->rooms[r2]->participants.count("u1"), 1u);
  }
  (void)u2;
}

TEST_F(VoiceServiceTest, LeaveRoomRemovesAndNotifiesOthers) {
  const std::string room = CreateRoom(0);
  auto u1 = Join("u1", room);
  auto u2 = Join("u2", room);
  (void)u1;

  chirp::voice::LeaveRoomRequest req;
  req.set_user_id("u1");
  req.set_room_id(room);
  HandlePacket(state_, u2, MakePacket(chirp::gateway::LEAVE_ROOM_REQ, 8, req.SerializeAsString()).SerializeAsString());
  // Leave is driven by the user_id in the request body, not the session.

  chirp::voice::LeaveRoomResponse resp;
  ASSERT_TRUE(LastBody(*u2, &resp));
  EXPECT_EQ(resp.code(), chirp::common::OK);

  // The broadcast targets the remaining participants; the leaver is excluded.
  bool u2_notified = false;
  for (const auto& pkt : ReceivedPackets(*u2)) {
    if (pkt.msg_id() == chirp::gateway::PARTICIPANT_LEFT_NOTIFY) {
      chirp::voice::ParticipantLeftNotify notify;
      ASSERT_TRUE(notify.ParseFromString(pkt.body()));
      EXPECT_EQ(notify.user_id(), "u1");
      u2_notified = true;
    }
  }
  EXPECT_TRUE(u2_notified);
  for (const auto& pkt : ReceivedPackets(*u1)) {
    EXPECT_NE(pkt.msg_id(), chirp::gateway::PARTICIPANT_LEFT_NOTIFY);
  }

  std::lock_guard<std::mutex> lock(state_->mu);
  EXPECT_EQ(state_->user_to_room.count("u1"), 0u);
  std::lock_guard<std::mutex> room_lock(state_->rooms[room]->mu);
  EXPECT_EQ(state_->rooms[room]->participants.count("u1"), 0u);
}

TEST_F(VoiceServiceTest, LeaveUnknownRoomIsIdempotentOk) {
  chirp::voice::LeaveRoomRequest req;
  req.set_user_id("u1");
  req.set_room_id("room_nope");
  SendPacketBody(chirp::gateway::LEAVE_ROOM_REQ, 9, req.SerializeAsString());

  ASSERT_EQ(session_->sent.size(), 1u);
  chirp::voice::LeaveRoomResponse resp;
  ASSERT_TRUE(LastBody(*session_, &resp));
  EXPECT_EQ(resp.code(), chirp::common::OK);
}

TEST_F(VoiceServiceTest, HeartbeatPongEchoesTimestampAndSequence) {
  chirp::gateway::HeartbeatPing ping;
  ping.set_timestamp(1234567890);
  SendPacketBody(chirp::gateway::HEARTBEAT_PING, 77, ping.SerializeAsString());

  ASSERT_EQ(session_->sent.size(), 1u);
  Packet pkt;
  ASSERT_TRUE(DecodeFramed(session_->sent.back(), &pkt));
  EXPECT_EQ(pkt.msg_id(), chirp::gateway::HEARTBEAT_PONG);
  EXPECT_EQ(pkt.sequence(), 77);
  chirp::gateway::HeartbeatPong pong;
  ASSERT_TRUE(pong.ParseFromString(pkt.body()));
  EXPECT_EQ(pong.timestamp(), 1234567890);
}

TEST_F(VoiceServiceTest, GarbagePacketBodyIgnored) {
  SendPacketBody(chirp::gateway::HEARTBEAT_PING, 1, "\x01\x02\x03");
  EXPECT_TRUE(session_->sent.empty());
}

TEST_F(VoiceServiceTest, UnknownMessageIdIgnored) {
  SendPacketBody(chirp::gateway::SEND_MESSAGE_REQ, 1, "");
  EXPECT_TRUE(session_->sent.empty());
}

TEST_F(VoiceServiceTest, IceCandidateTargetedReachesOnlyDestination) {
  const std::string room = CreateRoom(0);
  auto u1 = Join("u1", room);
  auto u2 = Join("u2", room);

  chirp::voice::IceCandidateMessage msg;
  msg.set_room_id(room);
  msg.set_from_user_id("u1");
  msg.set_to_user_id("u2");
  msg.mutable_candidate()->set_candidate("candidate:1 1 UDP 1 10.0.0.1 5000 typ host");
  HandlePacket(state_, u1, MakePacket(chirp::gateway::ICE_CANDIDATE_MSG, 10, msg.SerializeAsString()).SerializeAsString());

  bool u2_got = false;
  for (const auto& pkt : ReceivedPackets(*u2)) {
    if (pkt.msg_id() == chirp::gateway::ICE_CANDIDATE_MSG) {
      chirp::voice::IceCandidateMessage got;
      ASSERT_TRUE(got.ParseFromString(pkt.body()));
      EXPECT_EQ(got.from_user_id(), "u1");
      u2_got = true;
    }
  }
  EXPECT_TRUE(u2_got);
  // The sender must not receive its own targeted candidate back.
  for (const auto& pkt : ReceivedPackets(*u1)) {
    EXPECT_NE(pkt.msg_id(), chirp::gateway::ICE_CANDIDATE_MSG);
  }
}

TEST_F(VoiceServiceTest, IceCandidateWithoutTargetBroadcasts) {
  const std::string room = CreateRoom(0);
  auto u1 = Join("u1", room);
  auto u2 = Join("u2", room);

  chirp::voice::IceCandidateMessage msg;
  msg.set_room_id(room);
  msg.set_from_user_id("u1");
  HandlePacket(state_, u1, MakePacket(chirp::gateway::ICE_CANDIDATE_MSG, 11, msg.SerializeAsString()).SerializeAsString());

  auto count_ice = [](const MockSession& s) {
    size_t n = 0;
    for (const auto& pkt : ReceivedPackets(s)) {
      if (pkt.msg_id() == chirp::gateway::ICE_CANDIDATE_MSG) {
        n++;
      }
    }
    return n;
  };
  EXPECT_EQ(count_ice(*u2), 1u);
  EXPECT_EQ(count_ice(*u1), 1u);  // broadcast reaches everyone, sender included
}

TEST_F(VoiceServiceTest, IceCandidateDroppedForNonParticipantTarget) {
  const std::string room = CreateRoom(0);
  auto u1 = Join("u1", room);

  chirp::voice::IceCandidateMessage msg;
  msg.set_room_id(room);
  msg.set_from_user_id("u1");
  msg.set_to_user_id("outsider");
  HandlePacket(state_, u1, MakePacket(chirp::gateway::ICE_CANDIDATE_MSG, 12, msg.SerializeAsString()).SerializeAsString());

  for (const auto& pkt : ReceivedPackets(*u1)) {
    EXPECT_NE(pkt.msg_id(), chirp::gateway::ICE_CANDIDATE_MSG);
  }
}

TEST_F(VoiceServiceTest, SdpOfferTargetedReachesOnlyDestination) {
  const std::string room = CreateRoom(0);
  auto u1 = Join("u1", room);
  auto u2 = Join("u2", room);

  chirp::voice::SdpOfferMessage msg;
  msg.set_room_id(room);
  msg.set_from_user_id("u1");
  msg.set_to_user_id("u2");
  msg.set_sdp_offer("v=0 offer");
  HandlePacket(state_, u1, MakePacket(chirp::gateway::SDP_OFFER_MSG, 13, msg.SerializeAsString()).SerializeAsString());

  bool u2_got = false;
  for (const auto& pkt : ReceivedPackets(*u2)) {
    if (pkt.msg_id() == chirp::gateway::SDP_OFFER_MSG) {
      chirp::voice::SdpOfferMessage got;
      ASSERT_TRUE(got.ParseFromString(pkt.body()));
      EXPECT_EQ(got.sdp_offer(), "v=0 offer");
      u2_got = true;
    }
  }
  EXPECT_TRUE(u2_got);
  for (const auto& pkt : ReceivedPackets(*u1)) {
    EXPECT_NE(pkt.msg_id(), chirp::gateway::SDP_OFFER_MSG);
  }
}

TEST_F(VoiceServiceTest, SdpOfferWithoutTargetBroadcasts) {
  const std::string room = CreateRoom(0);
  auto u1 = Join("u1", room);
  auto u2 = Join("u2", room);

  chirp::voice::SdpOfferMessage msg;
  msg.set_room_id(room);
  msg.set_from_user_id("u2");
  msg.set_sdp_offer("v=0 offer");
  HandlePacket(state_, u2, MakePacket(chirp::gateway::SDP_OFFER_MSG, 14, msg.SerializeAsString()).SerializeAsString());

  auto count_sdp = [](const MockSession& s) {
    size_t n = 0;
    for (const auto& pkt : ReceivedPackets(s)) {
      if (pkt.msg_id() == chirp::gateway::SDP_OFFER_MSG) {
        n++;
      }
    }
    return n;
  };
  EXPECT_EQ(count_sdp(*u1), 1u);
  EXPECT_EQ(count_sdp(*u2), 1u);
}

TEST_F(VoiceServiceTest, DisconnectCleansUpAndNotifiesRoom) {
  const std::string room = CreateRoom(0);
  auto u1 = Join("u1", room);
  auto u2 = Join("u2", room);
  (void)u2;

  HandleDisconnect(state_, u1);

  std::lock_guard<std::mutex> lock(state_->mu);
  EXPECT_EQ(state_->user_to_room.count("u1"), 0u);
  EXPECT_EQ(state_->user_to_session.count("u1"), 0u);
  EXPECT_EQ(state_->session_to_user.count(u1.get()), 0u);
  std::lock_guard<std::mutex> room_lock(state_->rooms[room]->mu);
  EXPECT_EQ(state_->rooms[room]->participants.count("u1"), 0u);
}

TEST_F(VoiceServiceTest, DisconnectNotifiesRemainingParticipants) {
  const std::string room = CreateRoom(0);
  auto u1 = Join("u1", room);
  auto u2 = Join("u2", room);

  HandleDisconnect(state_, u1);

  bool u2_notified = false;
  for (const auto& pkt : ReceivedPackets(*u2)) {
    if (pkt.msg_id() == chirp::gateway::PARTICIPANT_LEFT_NOTIFY) {
      chirp::voice::ParticipantLeftNotify notify;
      ASSERT_TRUE(notify.ParseFromString(pkt.body()));
      EXPECT_EQ(notify.user_id(), "u1");
      u2_notified = true;
    }
  }
  EXPECT_TRUE(u2_notified);
}

TEST_F(VoiceServiceTest, DisconnectUnboundSessionIsNoop) {
  auto s = std::make_shared<MockSession>();
  HandleDisconnect(state_, s);  // must not crash or touch state
  EXPECT_TRUE(s->sent.empty());
}

}  // namespace
