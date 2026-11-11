// Unit tests for the voice service packet handlers (services/voice/src/main.cc).
// main.cc is included with main() renamed; handlers are driven directly with
// in-memory MockSessions, so room state, broadcast fan-out and the targeted
// WebRTC signaling relay are all pinned without sockets.

#include <gtest/gtest.h>

#include <memory>
#include <optional>
#include <string>
#include <thread>
#include <vector>

#include "common/jwt.h"
#include "proto/auth.pb.h"
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

  // Creates a room with the given capacity and returns its id. The creator
  // id must match the session's bound identity once CREATE_ROOM is
  // identity-gated (audit fix), so JWT/bound-session tests pass their own.
  std::string CreateRoom(int32_t max_participants, const std::string& creator = "creator") {
    chirp::voice::CreateRoomRequest req;
    req.set_user_id(creator);
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

  // Drives one speaking-queue request on the given session and returns the
  // parsed ack (VOICE-RT 麦序模式).
  template <typename ReqT>
  std::optional<chirp::voice::MicQueueResponse> MicOp(const std::shared_ptr<MockSession>& s,
                                                      chirp::gateway::MsgID req_id,
                                                      [[maybe_unused]] chirp::gateway::MsgID resp_id,
                                                      int64_t seq,
                                                      const ReqT& req) {
    HandlePacket(state_, s,
                 MakePacket(req_id, seq, req.SerializeAsString()).SerializeAsString());
    chirp::voice::MicQueueResponse resp;
    if (s->sent.empty() || !LastBody(*s, &resp)) {
      return std::nullopt;
    }
    return resp;
  }

  // Sets the room to managed mode on the host session.
  void SetManaged(const std::string& room) {
    chirp::voice::SetMicModeRequest req;
    req.set_user_id("creator");
    req.set_room_id(room);
    req.set_mode(chirp::voice::MIC_MANAGED);
    const auto r = MicOp(session_, chirp::gateway::SET_MIC_MODE_REQ,
                         chirp::gateway::SET_MIC_MODE_RESP, 10, req);
    ASSERT_TRUE(r.has_value());
    EXPECT_EQ(r->code(), chirp::common::OK);
  }

  // Queries room info on the host session and returns it.
  std::optional<chirp::voice::GetRoomInfoResponse> GetRoomInfo(const std::string& room) {
    chirp::voice::GetRoomInfoRequest req;
    req.set_room_id(room);
    HandlePacket(state_, session_,
                 MakePacket(chirp::gateway::GET_ROOM_INFO_REQ, 30, req.SerializeAsString())
                     .SerializeAsString());
    chirp::voice::GetRoomInfoResponse resp;
    if (session_->sent.empty() || !LastBody(*session_, &resp)) {
      return std::nullopt;
    }
    return resp;
  }

  // Collects the MIC_QUEUE_STATE_NOTIFY frames a session has received.
  std::vector<chirp::voice::MicQueueStateNotify> MicQueueNotifies(const MockSession& s) {
    std::vector<chirp::voice::MicQueueStateNotify> out;
    for (const auto& p : ReceivedPackets(s)) {
      if (p.msg_id() == chirp::gateway::MIC_QUEUE_STATE_NOTIFY) {
        chirp::voice::MicQueueStateNotify n;
        if (n.ParseFromString(p.body())) {
          out.push_back(n);
        }
      }
    }
    return out;
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

TEST_F(VoiceServiceTest, LeaveRoomReclaimsEmptiedRoom) {
  const std::string room = CreateRoom(0);
  Join("u1", room);

  chirp::voice::LeaveRoomRequest req;
  req.set_user_id("u1");
  req.set_room_id(room);
  SendPacketBody(chirp::gateway::LEAVE_ROOM_REQ, 40, req.SerializeAsString());

  // The last participant left: the room entry is reclaimed, not leaked.
  std::lock_guard<std::mutex> lock(state_->mu);
  EXPECT_EQ(state_->rooms.count(room), 0u);
}

TEST_F(VoiceServiceTest, DisconnectReclaimsEmptiedRoom) {
  const std::string room = CreateRoom(0);
  auto u1 = Join("u1", room);

  HandleDisconnect(state_, u1);

  {
    std::lock_guard<std::mutex> lock(state_->mu);
    EXPECT_EQ(state_->rooms.count(room), 0u);
  }
  // The reclaimed id is dead for joiners, same as an unknown room.
  Join("u2", room, chirp::common::USER_NOT_FOUND);
}

TEST_F(VoiceServiceTest, RoomWithRemainingParticipantsIsNotReclaimed) {
  const std::string room = CreateRoom(0);
  auto u1 = Join("u1", room);
  Join("u2", room);
  (void)u1;

  chirp::voice::LeaveRoomRequest req;
  req.set_user_id("u1");
  req.set_room_id(room);
  SendPacketBody(chirp::gateway::LEAVE_ROOM_REQ, 41, req.SerializeAsString());

  // u2 still holds the room alive after u1 leaves.
  std::lock_guard<std::mutex> lock(state_->mu);
  EXPECT_EQ(state_->rooms.count(room), 1u);
}

// ===========================================================================
// Authentication: LOGIN_REQ + the AuthorizeActor gate
// ===========================================================================

// Drives a LOGIN on `s` against `state` and parses the LoginResponse.
chirp::auth::LoginResponse DoLogin(const std::shared_ptr<VoiceState>& state,
                                   const std::shared_ptr<MockSession>& s,
                                   const std::string& token) {
  chirp::auth::LoginRequest req;
  req.set_token(token);
  HandlePacket(state, s, MakePacket(chirp::gateway::LOGIN_REQ, 20, req.SerializeAsString()).SerializeAsString());
  chirp::auth::LoginResponse resp;
  EXPECT_TRUE(LastBody(*s, &resp));
  return resp;
}

// ---------------------------------------------------------------------------
// Auth group
// ---------------------------------------------------------------------------

TEST_F(VoiceServiceTest, ScaffoldLoginTreatsTokenAsUserId) {
  const auto resp = DoLogin(state_, session_, "u9");

  EXPECT_EQ(resp.code(), chirp::common::OK);
  EXPECT_EQ(resp.user_id(), "u9");
  std::lock_guard<std::mutex> lock(state_->mu);
  EXPECT_EQ(state_->session_to_user[session_.get()], "u9");
  EXPECT_EQ(state_->user_to_session["u9"].lock().get(), session_.get());
  EXPECT_EQ(state_->authenticated_sessions.count(session_.get()), 1u);
}

TEST_F(VoiceServiceTest, ScaffoldLoginEmptyTokenRejected) {
  const auto resp = DoLogin(state_, session_, "");

  EXPECT_EQ(resp.code(), chirp::common::INVALID_PARAM);
  std::lock_guard<std::mutex> lock(state_->mu);
  EXPECT_TRUE(state_->session_to_user.empty());
  EXPECT_TRUE(state_->authenticated_sessions.empty());
}

TEST_F(VoiceServiceTest, JwtModeRejectsBadToken) {
  chirp::common::LoginTokenVerifier verifier("s3cret");
  state_->cfg.verifier = &verifier;

  const auto resp = DoLogin(state_, session_, "not-a-jwt");

  EXPECT_EQ(resp.code(), chirp::common::AUTH_FAILED);
  std::lock_guard<std::mutex> lock(state_->mu);
  EXPECT_TRUE(state_->session_to_user.empty());
}

TEST_F(VoiceServiceTest, JwtModeAcceptsValidTokenAndPinsIdentity) {
  chirp::common::LoginTokenVerifier verifier("s3cret");
  state_->cfg.verifier = &verifier;
  const int64_t now = NowMs() / 1000;

  const auto resp = DoLogin(state_, session_, chirp::common::JwtSignHS256("u1", now, "s3cret", now + 600));
  ASSERT_EQ(resp.code(), chirp::common::OK);
  EXPECT_EQ(resp.user_id(), "u1");

  // The pinned identity is accepted for business packets sent on the same
  // session (a fresh session would be unauthenticated).
  const std::string room = CreateRoom(0, "u1");
  chirp::voice::JoinRoomRequest req;
  req.set_user_id("u1");
  req.set_room_id(room);
  SendPacketBody(chirp::gateway::JOIN_ROOM_REQ, 3, req.SerializeAsString());

  chirp::voice::JoinRoomResponse join_resp;
  ASSERT_TRUE(LastBody(*session_, &join_resp));
  EXPECT_EQ(join_resp.code(), chirp::common::OK);
  std::lock_guard<std::mutex> lock(state_->mu);
  EXPECT_EQ(state_->user_to_room["u1"], room);
}

TEST_F(VoiceServiceTest, JwtModeRejectsUnauthenticatedBusinessPacket) {
  // The room is created before the verifier is installed (the target of this
  // test is the unauthenticated join, and CREATE_ROOM is identity-gated too).
  const std::string room = CreateRoom(0);
  chirp::common::LoginTokenVerifier verifier("s3cret");
  state_->cfg.verifier = &verifier;

  chirp::voice::JoinRoomRequest req;
  req.set_user_id("u1");
  req.set_room_id(room);
  SendPacketBody(chirp::gateway::JOIN_ROOM_REQ, 3, req.SerializeAsString());

  chirp::voice::JoinRoomResponse resp;
  ASSERT_TRUE(LastBody(*session_, &resp));
  EXPECT_EQ(resp.code(), chirp::common::AUTH_FAILED);
  std::lock_guard<std::mutex> lock(state_->mu);
  EXPECT_EQ(state_->user_to_room.count("u1"), 0u);
}

TEST_F(VoiceServiceTest, JwtModeRejectsIdentityMismatch) {
  chirp::common::LoginTokenVerifier verifier("s3cret");
  state_->cfg.verifier = &verifier;
  const int64_t now = NowMs() / 1000;
  ASSERT_EQ(DoLogin(state_, session_, chirp::common::JwtSignHS256("u1", now, "s3cret", now + 600)).code(),
            chirp::common::OK);

  const std::string room = CreateRoom(0, "u1");
  chirp::voice::JoinRoomRequest req;
  req.set_user_id("u2");  // forging someone else's identity
  req.set_room_id(room);
  SendPacketBody(chirp::gateway::JOIN_ROOM_REQ, 3, req.SerializeAsString());

  chirp::voice::JoinRoomResponse resp;
  ASSERT_TRUE(LastBody(*session_, &resp));
  EXPECT_EQ(resp.code(), chirp::common::INVALID_PARAM);
  std::lock_guard<std::mutex> lock(state_->mu);
  EXPECT_EQ(state_->user_to_room.count("u2"), 0u);
}

TEST_F(VoiceServiceTest, ScaffoldBoundSessionRejectsMismatchedIdentity) {
  ASSERT_EQ(DoLogin(state_, session_, "u1").code(), chirp::common::OK);

  const std::string room = CreateRoom(0, "u1");
  chirp::voice::JoinRoomRequest req;
  req.set_user_id("u2");
  req.set_room_id(room);
  SendPacketBody(chirp::gateway::JOIN_ROOM_REQ, 3, req.SerializeAsString());

  chirp::voice::JoinRoomResponse resp;
  ASSERT_TRUE(LastBody(*session_, &resp));
  EXPECT_EQ(resp.code(), chirp::common::INVALID_PARAM);
}

TEST_F(VoiceServiceTest, ScaffoldModeKeepsLegacySelfReportedJoin) {
  // No verifier configured, no LOGIN: the self-reported user_id keeps working
  // (the moat every pre-existing test in this file relies on).
  const std::string room = CreateRoom(0);
  Join("u1", room);

  std::lock_guard<std::mutex> lock(state_->mu);
  EXPECT_EQ(state_->user_to_room["u1"], room);
}

// ===========================================================================
// Audit 2026-11-11 fixes: LEAVE_ROOM / CREATE_ROOM identity gates, the
// ICE/SDP relay triple gate, and the room-switch reuse of the leave teardown.
// ===========================================================================

TEST_F(VoiceServiceTest, JwtModeRejectsUnauthenticatedLeave) {
  const std::string room = CreateRoom(0);
  Join("u1", room);

  chirp::common::LoginTokenVerifier verifier("s3cret");
  state_->cfg.verifier = &verifier;

  // An unauthenticated frame tries to drive u1 out of the room.
  chirp::voice::LeaveRoomRequest req;
  req.set_user_id("u1");
  req.set_room_id(room);
  SendPacketBody(chirp::gateway::LEAVE_ROOM_REQ, 42, req.SerializeAsString());

  chirp::voice::LeaveRoomResponse resp;
  ASSERT_TRUE(LastBody(*session_, &resp));
  EXPECT_EQ(resp.code(), chirp::common::AUTH_FAILED);
  std::lock_guard<std::mutex> lock(state_->mu);
  EXPECT_EQ(state_->user_to_room.count("u1"), 1u);
  std::lock_guard<std::mutex> room_lock(state_->rooms[room]->mu);
  EXPECT_EQ(state_->rooms[room]->participants.count("u1"), 1u);
}

TEST_F(VoiceServiceTest, JwtModeRejectsLeaveClaimingAnotherIdentity) {
  chirp::common::LoginTokenVerifier verifier("s3cret");
  state_->cfg.verifier = &verifier;
  const int64_t now = NowMs() / 1000;
  ASSERT_EQ(DoLogin(state_, session_, chirp::common::JwtSignHS256("u1", now, "s3cret", now + 600)).code(),
            chirp::common::OK);
  const std::string room = CreateRoom(0, "u1");

  chirp::voice::JoinRoomRequest jreq;
  jreq.set_user_id("u1");
  jreq.set_room_id(room);
  SendPacketBody(chirp::gateway::JOIN_ROOM_REQ, 43, jreq.SerializeAsString());

  // The bound u1 tries to leave AS u2: the claim mismatch is rejected.
  chirp::voice::LeaveRoomRequest req;
  req.set_user_id("u2");
  req.set_room_id(room);
  SendPacketBody(chirp::gateway::LEAVE_ROOM_REQ, 44, req.SerializeAsString());

  chirp::voice::LeaveRoomResponse resp;
  ASSERT_TRUE(LastBody(*session_, &resp));
  EXPECT_EQ(resp.code(), chirp::common::INVALID_PARAM);
  std::lock_guard<std::mutex> lock(state_->mu);
  EXPECT_EQ(state_->user_to_room.count("u2"), 0u);
  EXPECT_EQ(state_->user_to_room["u1"], room);
}

TEST_F(VoiceServiceTest, JwtModeRejectsUnauthenticatedCreateRoom) {
  chirp::common::LoginTokenVerifier verifier("s3cret");
  state_->cfg.verifier = &verifier;

  chirp::voice::CreateRoomRequest req;
  req.set_user_id("creator");
  SendPacketBody(chirp::gateway::CREATE_ROOM_REQ, 45, req.SerializeAsString());

  chirp::voice::CreateRoomResponse resp;
  ASSERT_TRUE(LastBody(*session_, &resp));
  EXPECT_EQ(resp.code(), chirp::common::AUTH_FAILED);
  std::lock_guard<std::mutex> lock(state_->mu);
  EXPECT_TRUE(state_->rooms.empty());
}

TEST_F(VoiceServiceTest, JwtModeRelayRequiresLogin) {
  const std::string room = CreateRoom(0);
  auto u1 = Join("u1", room);
  auto u2 = Join("u2", room);

  chirp::common::LoginTokenVerifier verifier("s3cret");
  state_->cfg.verifier = &verifier;

  // u1's session is unauthenticated in JWT mode: the relay drops silently.
  chirp::voice::IceCandidateMessage msg;
  msg.set_room_id(room);
  msg.set_from_user_id("u1");
  msg.set_to_user_id("u2");
  HandlePacket(state_, u1, MakePacket(chirp::gateway::ICE_CANDIDATE_MSG, 46, msg.SerializeAsString()).SerializeAsString());
  for (const auto& pkt : ReceivedPackets(*u2)) {
    EXPECT_NE(pkt.msg_id(), chirp::gateway::ICE_CANDIDATE_MSG);
  }
}

TEST_F(VoiceServiceTest, JwtModeRelayRejectsIdentityMismatch) {
  chirp::common::LoginTokenVerifier verifier("s3cret");
  state_->cfg.verifier = &verifier;
  const int64_t now = NowMs() / 1000;
  ASSERT_EQ(DoLogin(state_, session_, chirp::common::JwtSignHS256("u1", now, "s3cret", now + 600)).code(),
            chirp::common::OK);
  const std::string room = CreateRoom(0, "u1");

  chirp::voice::JoinRoomRequest jreq;
  jreq.set_user_id("u1");
  jreq.set_room_id(room);
  SendPacketBody(chirp::gateway::JOIN_ROOM_REQ, 47, jreq.SerializeAsString());

  // Bound u1 relays an ICE claiming to be u2: dropped (a broadcast would
  // come back to the sender itself, so silence on session_ proves the drop).
  chirp::voice::IceCandidateMessage msg;
  msg.set_room_id(room);
  msg.set_from_user_id("u2");
  SendPacketBody(chirp::gateway::ICE_CANDIDATE_MSG, 48, msg.SerializeAsString());
  for (const auto& pkt : ReceivedPackets(*session_)) {
    EXPECT_NE(pkt.msg_id(), chirp::gateway::ICE_CANDIDATE_MSG);
  }
}

TEST_F(VoiceServiceTest, ScaffoldRelayDropsClaimedNonParticipant) {
  const std::string room = CreateRoom(0);
  auto u1 = Join("u1", room);

  // Scaffold mode keeps the self-reported identity, but the claim must hold
  // a seat in the room: "mallory" has none, so the frame is dropped.
  chirp::voice::IceCandidateMessage msg;
  msg.set_room_id(room);
  msg.set_from_user_id("mallory");
  auto outsider = std::make_shared<MockSession>();
  HandlePacket(state_, outsider, MakePacket(chirp::gateway::ICE_CANDIDATE_MSG, 49, msg.SerializeAsString()).SerializeAsString());
  for (const auto& pkt : ReceivedPackets(*u1)) {
    EXPECT_NE(pkt.msg_id(), chirp::gateway::ICE_CANDIDATE_MSG);
  }
}

TEST_F(VoiceServiceTest, SwitchRoomReleasesMicAndNotifiesPreviousRoom) {
  const std::string room1 = CreateRoom(0);
  auto u1 = Join("u1", room1);
  auto u2 = Join("u2", room1);
  SetManaged(room1);

  // u2 applies and is approved: they hold the mic.
  chirp::voice::MicQueueApplyRequest apply;
  apply.set_user_id("u2");
  apply.set_room_id(room1);
  ASSERT_TRUE(MicOp(u2, chirp::gateway::MIC_QUEUE_APPLY_REQ,
                    chirp::gateway::MIC_QUEUE_APPLY_RESP, 60, apply).has_value());

  chirp::voice::MicQueueApproveRequest approve;
  approve.set_user_id("creator");
  approve.set_room_id(room1);
  const auto approved = MicOp(session_, chirp::gateway::MIC_QUEUE_APPROVE_REQ,
                              chirp::gateway::MIC_QUEUE_APPROVE_RESP, 61, approve);
  ASSERT_TRUE(approved.has_value());
  EXPECT_EQ(approved->holder_user_id(), "u2");

  // Switching rooms releases the mic and announces the LEFT in room1.
  const std::string room2 = CreateRoom(0);
  chirp::voice::JoinRoomRequest jreq;
  jreq.set_user_id("u2");
  jreq.set_room_id(room2);
  HandlePacket(state_, u2, MakePacket(chirp::gateway::JOIN_ROOM_REQ, 62, jreq.SerializeAsString()).SerializeAsString());

  bool left = false;
  for (const auto& pkt : ReceivedPackets(*u1)) {
    if (pkt.msg_id() == chirp::gateway::PARTICIPANT_LEFT_NOTIFY) {
      chirp::voice::ParticipantLeftNotify n;
      ASSERT_TRUE(n.ParseFromString(pkt.body()));
      EXPECT_EQ(n.user_id(), "u2");
      left = true;
    }
  }
  EXPECT_TRUE(left);

  // The floor cleared: the queue-state broadcast after the switch carries no
  // holder (and u2 was pulled off the queue with it).
  const auto notifies = MicQueueNotifies(*u1);
  ASSERT_FALSE(notifies.empty());
  EXPECT_EQ(notifies.back().holder_user_id(), "");
  std::lock_guard<std::mutex> lock(state_->mu);
  ASSERT_EQ(state_->rooms.count(room1), 1u);
  std::lock_guard<std::mutex> room_lock(state_->rooms[room1]->mu);
  EXPECT_TRUE(state_->rooms[room1]->mic_holder_user_id.empty());
  EXPECT_TRUE(state_->rooms[room1]->mic_queue.empty());
}

TEST_F(VoiceServiceTest, LoginKicksPreviousSession) {
  const std::string room = CreateRoom(0);
  auto s1 = std::make_shared<MockSession>();
  ASSERT_EQ(DoLogin(state_, s1, "u1").code(), chirp::common::OK);
  // Join from s1 itself: the Join helper would open a third session and
  // silently re-bind u1 before the kick even happens.
  chirp::voice::JoinRoomRequest jreq;
  jreq.set_user_id("u1");
  jreq.set_room_id(room);
  HandlePacket(state_, s1, MakePacket(chirp::gateway::JOIN_ROOM_REQ, 5, jreq.SerializeAsString()).SerializeAsString());

  auto s2 = std::make_shared<MockSession>();
  const auto resp = DoLogin(state_, s2, "u1");
  EXPECT_EQ(resp.code(), chirp::common::OK);
  EXPECT_TRUE(resp.kick_previous());

  // The old session was kicked, removed from the room and closed.
  bool s1_kicked = false;
  for (const auto& pkt : ReceivedPackets(*s1)) {
    if (pkt.msg_id() == chirp::gateway::KICK_NOTIFY) {
      s1_kicked = true;
    }
  }
  EXPECT_TRUE(s1_kicked);
  EXPECT_TRUE(s1->closed);
  std::lock_guard<std::mutex> lock(state_->mu);
  // The kick emptied the room: emptied rooms are reclaimed (audit fix
  // 2026-11-11), so the entry is gone rather than live-but-empty.
  EXPECT_EQ(state_->rooms.count(room), 0u);
  EXPECT_EQ(state_->session_to_user[s2.get()], "u1");
}

TEST_F(VoiceServiceTest, LoginTwiceOnSameSessionIsIdempotent) {
  ASSERT_EQ(DoLogin(state_, session_, "u1").code(), chirp::common::OK);
  const auto resp = DoLogin(state_, session_, "u1");

  EXPECT_EQ(resp.code(), chirp::common::OK);
  EXPECT_FALSE(session_->closed);  // no self-kick
}

// ===========================================================================
// SDP answer relay (mirrors the SDP offer tests)
// ===========================================================================

TEST_F(VoiceServiceTest, SdpAnswerTargetedReachesOnlyDestination) {
  const std::string room = CreateRoom(0);
  auto u1 = Join("u1", room);
  auto u2 = Join("u2", room);

  chirp::voice::SdpAnswerMessage msg;
  msg.set_room_id(room);
  msg.set_from_user_id("u1");
  msg.set_to_user_id("u2");
  msg.set_sdp_answer("v=0 answer");
  HandlePacket(state_, u1, MakePacket(chirp::gateway::SDP_ANSWER_MSG, 13, msg.SerializeAsString()).SerializeAsString());

  bool u2_got = false;
  for (const auto& pkt : ReceivedPackets(*u2)) {
    if (pkt.msg_id() == chirp::gateway::SDP_ANSWER_MSG) {
      chirp::voice::SdpAnswerMessage got;
      ASSERT_TRUE(got.ParseFromString(pkt.body()));
      EXPECT_EQ(got.sdp_answer(), "v=0 answer");
      u2_got = true;
    }
  }
  EXPECT_TRUE(u2_got);
  for (const auto& pkt : ReceivedPackets(*u1)) {
    EXPECT_NE(pkt.msg_id(), chirp::gateway::SDP_ANSWER_MSG);
  }
}

TEST_F(VoiceServiceTest, SdpAnswerWithoutTargetBroadcasts) {
  const std::string room = CreateRoom(0);
  auto u1 = Join("u1", room);
  auto u2 = Join("u2", room);

  chirp::voice::SdpAnswerMessage msg;
  msg.set_room_id(room);
  msg.set_from_user_id("u2");
  msg.set_sdp_answer("v=0 answer");
  HandlePacket(state_, u2, MakePacket(chirp::gateway::SDP_ANSWER_MSG, 14, msg.SerializeAsString()).SerializeAsString());

  auto count_answer = [](const MockSession& s) {
    size_t n = 0;
    for (const auto& pkt : ReceivedPackets(s)) {
      if (pkt.msg_id() == chirp::gateway::SDP_ANSWER_MSG) {
        n++;
      }
    }
    return n;
  };
  EXPECT_EQ(count_answer(*u1), 1u);
  EXPECT_EQ(count_answer(*u2), 1u);  // broadcast reaches everyone, sender included
}

TEST_F(VoiceServiceTest, SdpAnswerDroppedForNonParticipantTarget) {
  const std::string room = CreateRoom(0);
  auto u1 = Join("u1", room);

  chirp::voice::SdpAnswerMessage msg;
  msg.set_room_id(room);
  msg.set_from_user_id("u1");
  msg.set_to_user_id("outsider");
  msg.set_sdp_answer("v=0 answer");
  HandlePacket(state_, u1, MakePacket(chirp::gateway::SDP_ANSWER_MSG, 15, msg.SerializeAsString()).SerializeAsString());

  for (const auto& pkt : ReceivedPackets(*u1)) {
    EXPECT_NE(pkt.msg_id(), chirp::gateway::SDP_ANSWER_MSG);
  }
}

TEST_F(VoiceServiceTest, SdpAnswerDroppedForUnknownRoom) {
  chirp::voice::SdpAnswerMessage msg;
  msg.set_room_id("room_nope");
  msg.set_sdp_answer("v=0 answer");
  SendPacketBody(chirp::gateway::SDP_ANSWER_MSG, 15, msg.SerializeAsString());
  EXPECT_TRUE(session_->sent.empty());
}

// ===========================================================================
// mute / deafen
// ===========================================================================

TEST_F(VoiceServiceTest, SetMuteUpdatesStateAndNotifiesOthers) {
  const std::string room = CreateRoom(0);
  auto u1 = Join("u1", room);
  auto u2 = Join("u2", room);

  chirp::voice::SetMuteRequest req;
  req.set_user_id("u1");
  req.set_room_id(room);
  req.set_muted(true);
  HandlePacket(state_, u1, MakePacket(chirp::gateway::SET_MUTE_REQ, 30, req.SerializeAsString()).SerializeAsString());

  chirp::voice::SetMuteResponse resp;
  ASSERT_TRUE(LastBody(*u1, &resp));
  EXPECT_EQ(resp.code(), chirp::common::OK);

  // u2 sees the state change; u1's own confirmation is the RESP only.
  bool u2_notified = false;
  for (const auto& pkt : ReceivedPackets(*u2)) {
    if (pkt.msg_id() == chirp::gateway::PARTICIPANT_STATE_CHANGED_NOTIFY) {
      chirp::voice::ParticipantStateChangedNotify notify;
      ASSERT_TRUE(notify.ParseFromString(pkt.body()));
      EXPECT_EQ(notify.user_id(), "u1");
      EXPECT_EQ(notify.state(), chirp::voice::MUTED);
      u2_notified = true;
    }
  }
  EXPECT_TRUE(u2_notified);
  for (const auto& pkt : ReceivedPackets(*u1)) {
    EXPECT_NE(pkt.msg_id(), chirp::gateway::PARTICIPANT_STATE_CHANGED_NOTIFY);
  }

  std::lock_guard<std::mutex> room_lock(state_->rooms[room]->mu);
  EXPECT_EQ(state_->rooms[room]->participants["u1"].state(), chirp::voice::MUTED);
  EXPECT_TRUE(state_->rooms[room]->participants["u1"].muted());
}

TEST_F(VoiceServiceTest, SetUnmuteRestoresConnected) {
  const std::string room = CreateRoom(0);
  auto u1 = Join("u1", room);

  chirp::voice::SetMuteRequest mute;
  mute.set_user_id("u1");
  mute.set_room_id(room);
  mute.set_muted(true);
  HandlePacket(state_, u1, MakePacket(chirp::gateway::SET_MUTE_REQ, 30, mute.SerializeAsString()).SerializeAsString());

  chirp::voice::SetMuteRequest unmute;
  unmute.set_user_id("u1");
  unmute.set_room_id(room);
  unmute.set_muted(false);
  HandlePacket(state_, u1, MakePacket(chirp::gateway::SET_MUTE_REQ, 31, unmute.SerializeAsString()).SerializeAsString());

  std::lock_guard<std::mutex> room_lock(state_->rooms[room]->mu);
  EXPECT_EQ(state_->rooms[room]->participants["u1"].state(), chirp::voice::CONNECTED);
  EXPECT_FALSE(state_->rooms[room]->participants["u1"].muted());
}

TEST_F(VoiceServiceTest, SetDeafenSetsDeafenedState) {
  const std::string room = CreateRoom(0);
  auto u1 = Join("u1", room);

  chirp::voice::SetDeafenRequest req;
  req.set_user_id("u1");
  req.set_room_id(room);
  req.set_deafened(true);
  HandlePacket(state_, u1, MakePacket(chirp::gateway::SET_DEAFEN_REQ, 32, req.SerializeAsString()).SerializeAsString());

  chirp::voice::SetDeafenResponse resp;
  ASSERT_TRUE(LastBody(*u1, &resp));
  EXPECT_EQ(resp.code(), chirp::common::OK);
  std::lock_guard<std::mutex> room_lock(state_->rooms[room]->mu);
  EXPECT_EQ(state_->rooms[room]->participants["u1"].state(), chirp::voice::DEAFENED);
  EXPECT_TRUE(state_->rooms[room]->participants["u1"].deafened());
}

TEST_F(VoiceServiceTest, UnmuteKeepsDeafened) {
  const std::string room = CreateRoom(0);
  auto u1 = Join("u1", room);

  // mute + deafen, then unmute the mic: the participant stays DEAFENED while
  // the deafen flag is set, no matter what the mic does.
  chirp::voice::SetMuteRequest mute;
  mute.set_user_id("u1");
  mute.set_room_id(room);
  mute.set_muted(true);
  HandlePacket(state_, u1, MakePacket(chirp::gateway::SET_MUTE_REQ, 30, mute.SerializeAsString()).SerializeAsString());

  chirp::voice::SetDeafenRequest deafen;
  deafen.set_user_id("u1");
  deafen.set_room_id(room);
  deafen.set_deafened(true);
  HandlePacket(state_, u1, MakePacket(chirp::gateway::SET_DEAFEN_REQ, 31, deafen.SerializeAsString()).SerializeAsString());

  chirp::voice::SetMuteRequest unmute;
  unmute.set_user_id("u1");
  unmute.set_room_id(room);
  unmute.set_muted(false);
  HandlePacket(state_, u1, MakePacket(chirp::gateway::SET_MUTE_REQ, 32, unmute.SerializeAsString()).SerializeAsString());

  {
    std::lock_guard<std::mutex> room_lock(state_->rooms[room]->mu);
    EXPECT_EQ(state_->rooms[room]->participants["u1"].state(), chirp::voice::DEAFENED);
    EXPECT_FALSE(state_->rooms[room]->participants["u1"].muted());
    EXPECT_TRUE(state_->rooms[room]->participants["u1"].deafened());
  }

  chirp::voice::SetDeafenRequest undeafen;
  undeafen.set_user_id("u1");
  undeafen.set_room_id(room);
  undeafen.set_deafened(false);
  HandlePacket(state_, u1, MakePacket(chirp::gateway::SET_DEAFEN_REQ, 33, undeafen.SerializeAsString()).SerializeAsString());

  // The mic is still unmuted from the step above, so clearing deafen lands
  // on plain CONNECTED (the flags are independent, no hidden memory).
  {
    std::lock_guard<std::mutex> room_lock(state_->rooms[room]->mu);
    EXPECT_EQ(state_->rooms[room]->participants["u1"].state(), chirp::voice::CONNECTED);
    EXPECT_FALSE(state_->rooms[room]->participants["u1"].muted());
    EXPECT_FALSE(state_->rooms[room]->participants["u1"].deafened());
  }

  // The mirror order: mute, deafen, undeafen — the mic flag survives, so the
  // participant lands on MUTED, not CONNECTED.
  chirp::voice::SetMuteRequest mute2;
  mute2.set_user_id("u1");
  mute2.set_room_id(room);
  mute2.set_muted(true);
  HandlePacket(state_, u1, MakePacket(chirp::gateway::SET_MUTE_REQ, 34, mute2.SerializeAsString()).SerializeAsString());

  chirp::voice::SetDeafenRequest deafen2;
  deafen2.set_user_id("u1");
  deafen2.set_room_id(room);
  deafen2.set_deafened(true);
  HandlePacket(state_, u1, MakePacket(chirp::gateway::SET_DEAFEN_REQ, 35, deafen2.SerializeAsString()).SerializeAsString());

  HandlePacket(state_, u1, MakePacket(chirp::gateway::SET_DEAFEN_REQ, 36, undeafen.SerializeAsString()).SerializeAsString());

  std::lock_guard<std::mutex> room_lock(state_->rooms[room]->mu);
  EXPECT_EQ(state_->rooms[room]->participants["u1"].state(), chirp::voice::MUTED);
  EXPECT_TRUE(state_->rooms[room]->participants["u1"].muted());
  EXPECT_FALSE(state_->rooms[room]->participants["u1"].deafened());
}

TEST_F(VoiceServiceTest, SetMuteUnknownRoomRejected) {
  chirp::voice::SetMuteRequest req;
  req.set_user_id("u1");
  req.set_room_id("room_nope");
  req.set_muted(true);
  SendPacketBody(chirp::gateway::SET_MUTE_REQ, 30, req.SerializeAsString());

  chirp::voice::SetMuteResponse resp;
  ASSERT_TRUE(LastBody(*session_, &resp));
  EXPECT_EQ(resp.code(), chirp::common::USER_NOT_FOUND);
}

TEST_F(VoiceServiceTest, SetMuteNonParticipantRejected) {
  const std::string room = CreateRoom(0);
  auto u1 = Join("u1", room);

  chirp::voice::SetMuteRequest req;
  req.set_user_id("outsider");
  req.set_room_id(room);
  req.set_muted(true);
  HandlePacket(state_, u1, MakePacket(chirp::gateway::SET_MUTE_REQ, 30, req.SerializeAsString()).SerializeAsString());

  chirp::voice::SetMuteResponse resp;
  ASSERT_TRUE(LastBody(*u1, &resp));
  EXPECT_EQ(resp.code(), chirp::common::USER_NOT_FOUND);
  std::lock_guard<std::mutex> room_lock(state_->rooms[room]->mu);
  EXPECT_EQ(state_->rooms[room]->participants.count("outsider"), 0u);
}

TEST_F(VoiceServiceTest, SetMuteGarbageBodyRejected) {
  SendPacketBody(chirp::gateway::SET_MUTE_REQ, 30, "\xff\xfe");
  chirp::voice::SetMuteResponse resp;
  ASSERT_TRUE(LastBody(*session_, &resp));
  EXPECT_EQ(resp.code(), chirp::common::INVALID_PARAM);
}

// ===========================================================================
// Room queries
// ===========================================================================

TEST_F(VoiceServiceTest, GetRoomInfoReturnsParticipantsAndRoomMeta) {
  const std::string room = CreateRoom(0);
  Join("u1", room);
  Join("u2", room);

  chirp::voice::GetRoomInfoRequest req;
  req.set_room_id(room);
  SendPacketBody(chirp::gateway::GET_ROOM_INFO_REQ, 40, req.SerializeAsString());

  chirp::voice::GetRoomInfoResponse resp;
  ASSERT_TRUE(LastBody(*session_, &resp));
  EXPECT_EQ(resp.code(), chirp::common::OK);
  EXPECT_EQ(resp.room_id(), room);
  EXPECT_EQ(resp.room_name(), "test room");
  EXPECT_EQ(resp.room_type(), chirp::voice::GROUP);
  ASSERT_EQ(resp.participants_size(), 2);
  EXPECT_EQ(resp.max_participants(), 0);
}

TEST_F(VoiceServiceTest, GetRoomInfoUnknownRoomRejected) {
  chirp::voice::GetRoomInfoRequest req;
  req.set_room_id("room_nope");
  SendPacketBody(chirp::gateway::GET_ROOM_INFO_REQ, 40, req.SerializeAsString());

  chirp::voice::GetRoomInfoResponse resp;
  ASSERT_TRUE(LastBody(*session_, &resp));
  EXPECT_EQ(resp.code(), chirp::common::USER_NOT_FOUND);
}

TEST_F(VoiceServiceTest, GetUserRoomReturnsCurrentRoom) {
  const std::string room = CreateRoom(0);
  Join("u1", room);

  chirp::voice::GetUserRoomRequest req;
  req.set_user_id("u1");
  SendPacketBody(chirp::gateway::GET_USER_ROOM_REQ, 41, req.SerializeAsString());

  chirp::voice::GetUserRoomResponse resp;
  ASSERT_TRUE(LastBody(*session_, &resp));
  EXPECT_EQ(resp.code(), chirp::common::OK);
  EXPECT_EQ(resp.room_id(), room);
  EXPECT_EQ(resp.participant().user_id(), "u1");
}

TEST_F(VoiceServiceTest, GetUserRoomForUserNotInRoomRejected) {
  chirp::voice::GetUserRoomRequest req;
  req.set_user_id("u1");
  SendPacketBody(chirp::gateway::GET_USER_ROOM_REQ, 41, req.SerializeAsString());

  chirp::voice::GetUserRoomResponse resp;
  ASSERT_TRUE(LastBody(*session_, &resp));
  EXPECT_EQ(resp.code(), chirp::common::USER_NOT_FOUND);
  EXPECT_EQ(resp.room_id(), "");
}

// ===========================================================================
// TURN credentials (coturn REST short-term credentials)
// ===========================================================================

// Renders a digest as lowercase hex for known-answer assertions.
std::string ToHex(const std::array<uint8_t, 20>& digest) {
  static const char* kHex = "0123456789abcdef";
  std::string hex;
  for (const uint8_t b : digest) {
    hex.push_back(kHex[b >> 4]);
    hex.push_back(kHex[b & 0xF]);
  }
  return hex;
}

TEST_F(VoiceServiceTest, Sha1KnownAnswerVectors) {
  EXPECT_EQ(ToHex(Sha1("abc")), "a9993e364706816aba3e25717850c26c9cd0d89d");
}

TEST_F(VoiceServiceTest, HmacSha1KnownAnswer) {
  EXPECT_EQ(ToHex(HmacSha1("key", "The quick brown fox jumps over the lazy dog")),
            "de7c9b85b8b78aa6bc8a7a36f70a90701c9db4d9");
}

TEST_F(VoiceServiceTest, Base64EncodeStandardPadded) {
  const auto mac = HmacSha1("frozen_secret", "1790000000:u42");
  const std::string encoded = Base64Encode(mac.data(), mac.size());
  EXPECT_EQ(encoded.size(), 28u);  // 20 bytes -> 28 chars with '=' padding
  EXPECT_EQ(encoded.back(), '=');
  // Frozen vector cross-checked against python hmac/hashlib.
  EXPECT_EQ(encoded, "RAgTHvgkOVCaHl77yUqvFymQ4VU=");
}

TEST_F(VoiceServiceTest, TurnCredentialFormatMatchesFrozenVector) {
  const std::string username = MakeTurnUsername(1790000000, "u42");
  EXPECT_EQ(username, "1790000000:u42");
  EXPECT_EQ(MakeTurnCredential("frozen_secret", username), "RAgTHvgkOVCaHl77yUqvFymQ4VU=");
}

TEST_F(VoiceServiceTest, TurnCredentialUsernameCarriesExpiryAndUser) {
  const std::string username = MakeTurnUsername(1234567890, "alice");
  EXPECT_EQ(username, "1234567890:alice");
  EXPECT_EQ(username.find(':'), 10u);  // leading unix-timestamp integer
}

TEST_F(VoiceServiceTest, JoinResponseCarriesIceServersWhenConfigured) {
  state_->cfg.turn_uri =
      "turn:coturn:3478?transport=udp, turn:coturn:3478?transport=tcp, stun:coturn:3478";
  state_->cfg.turn_secret = "dev_turn_secret";
  state_->cfg.turn_credential_ttl_seconds = 3600;

  const std::string room = CreateRoom(0);
  auto s = Join("u1", room);

  chirp::voice::JoinRoomResponse resp;
  ASSERT_TRUE(LastBody(*s, &resp));
  ASSERT_EQ(resp.ice_servers_size(), 1);
  const auto& ice = resp.ice_servers(0);
  ASSERT_EQ(ice.urls_size(), 3);
  EXPECT_EQ(ice.urls(0), "turn:coturn:3478?transport=udp");
  EXPECT_EQ(ice.urls(1), "turn:coturn:3478?transport=tcp");
  EXPECT_EQ(ice.urls(2), "stun:coturn:3478");

  // username = "{expiry}:{user_id}" with expiry == now_s + ttl.
  ASSERT_FALSE(ice.username().empty());
  const size_t colon = ice.username().find(':');
  ASSERT_NE(colon, std::string::npos);
  const int64_t expiry = std::atoll(ice.username().substr(0, colon).c_str());
  EXPECT_EQ(expiry, NowMs() / 1000 + 3600);
  EXPECT_EQ(ice.username().substr(colon + 1), "u1");
  EXPECT_FALSE(ice.credential().empty());
}

TEST_F(VoiceServiceTest, JoinResponseHasNoIceServersByDefault) {
  const std::string room = CreateRoom(0);
  auto s = Join("u1", room);

  chirp::voice::JoinRoomResponse resp;
  ASSERT_TRUE(LastBody(*s, &resp));
  EXPECT_EQ(resp.ice_servers_size(), 0);  // backward-compat pin
}

// ===========================================================================
// Idle-connection sweep (heartbeat timeout)
// ===========================================================================

TEST_F(VoiceServiceTest, SweepIdleSessionRemovesFromRoomAndCloses) {
  const std::string room = CreateRoom(0);
  auto u1 = Join("u1", room);
  auto u2 = Join("u2", room);

  // Backdate u1's last_seen past the timeout and sweep.
  {
    std::lock_guard<std::mutex> lock(state_->mu);
    state_->cfg.heartbeat_timeout_ms = 75000;
    state_->session_to_last_seen_ms[u1.get()] = NowMs() - 100000;
  }
  SweepHeartbeatTimeout(state_, NowMs());

  EXPECT_TRUE(u1->closed);
  {
    std::lock_guard<std::mutex> lock(state_->mu);
    EXPECT_EQ(state_->user_to_room.count("u1"), 0u);
    EXPECT_EQ(state_->user_to_session.count("u1"), 0u);
    EXPECT_EQ(state_->session_to_user.count(u1.get()), 0u);
    EXPECT_EQ(state_->session_to_last_seen_ms.count(u1.get()), 0u);
  }
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

TEST_F(VoiceServiceTest, SweepKeepsFreshSession) {
  const std::string room = CreateRoom(0);
  auto u1 = Join("u1", room);

  {
    std::lock_guard<std::mutex> lock(state_->mu);
    state_->cfg.heartbeat_timeout_ms = 75000;
    state_->session_to_last_seen_ms[u1.get()] = NowMs();  // fresh
  }
  SweepHeartbeatTimeout(state_, NowMs());

  EXPECT_FALSE(u1->closed);
  std::lock_guard<std::mutex> lock(state_->mu);
  EXPECT_EQ(state_->user_to_room["u1"], room);
}

TEST_F(VoiceServiceTest, HeartbeatRefreshesLastSeen) {
  const std::string room = CreateRoom(0);
  auto u1 = Join("u1", room);

  // Backdate past the timeout, then let a heartbeat arrive: the sweep must
  // keep the session alive.
  {
    std::lock_guard<std::mutex> lock(state_->mu);
    state_->cfg.heartbeat_timeout_ms = 75000;
    state_->session_to_last_seen_ms[u1.get()] = NowMs() - 100000;
  }
  chirp::gateway::HeartbeatPing ping;
  ping.set_timestamp(1);
  HandlePacket(state_, u1, MakePacket(chirp::gateway::HEARTBEAT_PING, 50, ping.SerializeAsString()).SerializeAsString());
  SweepHeartbeatTimeout(state_, NowMs());

  EXPECT_FALSE(u1->closed);
}

TEST_F(VoiceServiceTest, SweepSkipsSessionsWithoutLastSeenEntry) {
  const std::string room = CreateRoom(0);
  auto u1 = Join("u1", room);

  {
    std::lock_guard<std::mutex> lock(state_->mu);
    state_->cfg.heartbeat_timeout_ms = 75000;
    state_->session_to_last_seen_ms.clear();  // simulate never-seen sessions
  }
  SweepHeartbeatTimeout(state_, NowMs());

  EXPECT_FALSE(u1->closed);
}

TEST_F(VoiceServiceTest, SweepExpiredSessionNotInRoomJustCloses) {
  auto s = std::make_shared<MockSession>();
  ASSERT_EQ(DoLogin(state_, s, "u1").code(), chirp::common::OK);  // bound, no room

  {
    std::lock_guard<std::mutex> lock(state_->mu);
    state_->cfg.heartbeat_timeout_ms = 75000;
    state_->session_to_last_seen_ms[s.get()] = NowMs() - 100000;
  }
  SweepHeartbeatTimeout(state_, NowMs());

  EXPECT_TRUE(s->closed);
  std::lock_guard<std::mutex> lock(state_->mu);
  EXPECT_EQ(state_->session_to_user.count(s.get()), 0u);
}

TEST_F(VoiceServiceTest, HeartbeatTimeoutZeroDisablesSweep) {
  const std::string room = CreateRoom(0);
  auto u1 = Join("u1", room);

  {
    std::lock_guard<std::mutex> lock(state_->mu);
    state_->cfg.heartbeat_timeout_ms = 0;
    state_->session_to_last_seen_ms[u1.get()] = NowMs() - 1000000;
  }
  SweepHeartbeatTimeout(state_, NowMs());

  EXPECT_FALSE(u1->closed);
  std::lock_guard<std::mutex> lock(state_->mu);
  EXPECT_EQ(state_->user_to_room["u1"], room);
}

TEST_F(VoiceServiceTest, SweepUnboundExpiredSessionCleansAuthState) {
  auto s = std::make_shared<MockSession>();
  ASSERT_EQ(DoLogin(state_, s, "u1").code(), chirp::common::OK);

  // The disconnect path must clear the auth and last_seen rows along with the
  // room bookkeeping, so a kicked session leaves no stale state behind.
  {
    std::lock_guard<std::mutex> lock(state_->mu);
    state_->cfg.heartbeat_timeout_ms = 75000;
    state_->session_to_last_seen_ms[s.get()] = NowMs() - 100000;
  }
  HandleDisconnect(state_, s);

  std::lock_guard<std::mutex> lock(state_->mu);
  EXPECT_EQ(state_->authenticated_sessions.count(s.get()), 0u);
  EXPECT_EQ(state_->session_to_last_seen_ms.count(s.get()), 0u);
}

// ===========================================================================
// Batch 18: handler-face gap closure (argv helpers, HMAC long-key arm, parse
// guards, verifier-gated room queries, signaling-relay silence, the sweep
// timer itself and the non-Packet dispatch payload).
// ===========================================================================

TEST_F(VoiceServiceTest, ArgHelpersHitMissDanglingAndGarbage) {
  char bin[] = "bin";
  char port[] = "--port";
  char value[] = "5300";
  char dangling[] = "--dangling";
  char* argv[] = {bin, port, value, dangling};
  EXPECT_EQ(GetArg(4, argv, "--port", "5000"), "5300");
  EXPECT_EQ(GetArg(4, argv, "--missing", "5000"), "5000");
  EXPECT_EQ(GetArg(4, argv, "--dangling", "5000"), "5000");  // flag with no value
  EXPECT_EQ(ParseU16Arg(4, argv, "--port", 123), 5300);
  EXPECT_EQ(ParseU16Arg(4, argv, "--gone", 123), 123);
  char garbage[] = "not-a-number";
  char* argv2[] = {bin, port, garbage};
  EXPECT_EQ(ParseU16Arg(3, argv2, "--port", 123), 0);  // std::atoi -> 0
}

// Keys larger than the 64-byte SHA-1 block are hashed first (RFC 2202 shape);
// frozen vector cross-checked against python hmac/hashlib.
TEST_F(VoiceServiceTest, HmacSha1LongKeyIsHashedFirst) {
  const std::string key(131, '\xaa');
  const std::string data = "Test Using Larger Than Block-Size Key - Hash Key First";
  EXPECT_EQ(ToHex(HmacSha1(key, data)), "90d0dace1c1bdc957339307803160335bde6df2b");
  EXPECT_EQ(MakeTurnCredential(key, data), "kNDazhwb3JVzOTB4AxYDNb3m3ys=");
}

TEST_F(VoiceServiceTest, LoginGarbageBodyRejected) {
  SendPacketBody(chirp::gateway::LOGIN_REQ, 80, "\xff\xfe nope");
  chirp::auth::LoginResponse resp;
  ASSERT_TRUE(LastBody(*session_, &resp));
  EXPECT_EQ(resp.code(), chirp::common::INVALID_PARAM);
  {
    std::lock_guard<std::mutex> lock(state_->mu);
    EXPECT_EQ(state_->session_to_user.count(session_.get()), 0u);  // nothing bound
  }
}

TEST_F(VoiceServiceTest, JoinRoomGarbageBodyRejected) {
  SendPacketBody(chirp::gateway::JOIN_ROOM_REQ, 81, "\xff\xfe nope");
  chirp::voice::JoinRoomResponse resp;
  ASSERT_TRUE(LastBody(*session_, &resp));
  EXPECT_EQ(resp.code(), chirp::common::INVALID_PARAM);
}

TEST_F(VoiceServiceTest, LeaveRoomGarbageBodyRejected) {
  SendPacketBody(chirp::gateway::LEAVE_ROOM_REQ, 82, "\xff\xfe nope");
  chirp::voice::LeaveRoomResponse resp;
  ASSERT_TRUE(LastBody(*session_, &resp));
  EXPECT_EQ(resp.code(), chirp::common::INVALID_PARAM);
}

// TURN url segments carry padding spaces around them after the comma split;
// every segment must come out trimmed. The 131-byte secret also drives the
// hashed-first HMAC arm end to end through the join response.
TEST_F(VoiceServiceTest, JoinResponseTrimsSpacesAroundTurnUrls) {
  state_->cfg.turn_uri = "turn:a:3478 , turn:b:3478 ,stun:c:3478";
  state_->cfg.turn_secret = std::string(131, '\xaa');
  state_->cfg.turn_credential_ttl_seconds = 3600;

  const std::string room = CreateRoom(0);
  auto s = Join("u1", room);

  chirp::voice::JoinRoomResponse resp;
  ASSERT_TRUE(LastBody(*s, &resp));
  ASSERT_EQ(resp.ice_servers_size(), 1);
  const auto& ice = resp.ice_servers(0);
  ASSERT_EQ(ice.urls_size(), 3);
  EXPECT_EQ(ice.urls(0), "turn:a:3478");
  EXPECT_EQ(ice.urls(1), "turn:b:3478");
  EXPECT_EQ(ice.urls(2), "stun:c:3478");
  const size_t colon = ice.username().find(':');
  ASSERT_NE(colon, std::string::npos);
  EXPECT_EQ(ice.username().substr(colon + 1), "u1");
  EXPECT_EQ(ice.credential().size(), 28u);  // base64(20-byte HMAC-SHA1)
}

// SetParticipantFlag's authorize guard: a logged-in session claiming someone
// else's user id is rejected for both flag handlers before any room lookup.
TEST_F(VoiceServiceTest, MuteDeafenIdentityMismatchRejectedAfterLogin) {
  ASSERT_EQ(DoLogin(state_, session_, "u1").code(), chirp::common::OK);

  chirp::voice::SetMuteRequest req;
  req.set_user_id("mallory");
  req.set_muted(true);
  SendPacketBody(chirp::gateway::SET_MUTE_REQ, 83, req.SerializeAsString());
  chirp::voice::SetMuteResponse resp;
  ASSERT_TRUE(LastBody(*session_, &resp));
  EXPECT_EQ(resp.code(), chirp::common::INVALID_PARAM);

  chirp::voice::SetDeafenRequest dreq;
  dreq.set_user_id("mallory");
  dreq.set_deafened(true);
  SendPacketBody(chirp::gateway::SET_DEAFEN_REQ, 84, dreq.SerializeAsString());
  chirp::voice::SetDeafenResponse dresp;
  ASSERT_TRUE(LastBody(*session_, &dresp));
  EXPECT_EQ(dresp.code(), chirp::common::INVALID_PARAM);
}

TEST_F(VoiceServiceTest, RoomInfoAndUserRoomGarbageBodiesRejected) {
  SendPacketBody(chirp::gateway::GET_ROOM_INFO_REQ, 85, "\xff\xfe nope");
  chirp::voice::GetRoomInfoResponse iresp;
  ASSERT_TRUE(LastBody(*session_, &iresp));
  EXPECT_EQ(iresp.code(), chirp::common::INVALID_PARAM);

  SendPacketBody(chirp::gateway::GET_USER_ROOM_REQ, 86, "\xff\xfe nope");
  chirp::voice::GetUserRoomResponse uresp;
  ASSERT_TRUE(LastBody(*session_, &uresp));
  EXPECT_EQ(uresp.code(), chirp::common::INVALID_PARAM);
}

// With a verifier configured, the room queries are login-gated even with a
// well-formed body (GET_ROOM_INFO claims no user id at all - the gate is
// purely "did this session LOGIN").
TEST_F(VoiceServiceTest, RoomInfoAndUserRoomRequireLoginWhenSecretConfigured) {
  chirp::common::LoginTokenVerifier verifier("s3cret");
  state_->cfg.verifier = &verifier;

  chirp::voice::GetRoomInfoRequest info;
  SendPacketBody(chirp::gateway::GET_ROOM_INFO_REQ, 87, info.SerializeAsString());
  chirp::voice::GetRoomInfoResponse iresp;
  ASSERT_TRUE(LastBody(*session_, &iresp));
  EXPECT_EQ(iresp.code(), chirp::common::AUTH_FAILED);

  chirp::voice::GetUserRoomRequest user_room;
  user_room.set_user_id("u1");
  SendPacketBody(chirp::gateway::GET_USER_ROOM_REQ, 88, user_room.SerializeAsString());
  chirp::voice::GetUserRoomResponse uresp;
  ASSERT_TRUE(LastBody(*session_, &uresp));
  EXPECT_EQ(uresp.code(), chirp::common::AUTH_FAILED);
}

// A stale user_to_room entry (room dissolved out from under the mapping)
// answers USER_NOT_FOUND via the FindRoom-null arm instead of crashing.
TEST_F(VoiceServiceTest, GetUserRoomStaleRoomEntryReturnsNotFound) {
  {
    std::lock_guard<std::mutex> lock(state_->mu);
    state_->user_to_room["ghost"] = "r-gone";
  }
  chirp::voice::GetUserRoomRequest req;
  req.set_user_id("ghost");
  SendPacketBody(chirp::gateway::GET_USER_ROOM_REQ, 89, req.SerializeAsString());

  chirp::voice::GetUserRoomResponse resp;
  ASSERT_TRUE(LastBody(*session_, &resp));
  EXPECT_EQ(resp.code(), chirp::common::USER_NOT_FOUND);
}

// The three relay handlers are fire-and-forget: neither a garbage body nor a
// well-formed body naming an unknown room ever produces a reply or a close.
TEST_F(VoiceServiceTest, SignalingRelayToleratesGarbageBodiesAndUnknownRooms) {
  const std::string room = CreateRoom(0);
  auto u1 = Join("u1", room);

  for (const auto id : {chirp::gateway::ICE_CANDIDATE_MSG, chirp::gateway::SDP_OFFER_MSG,
                        chirp::gateway::SDP_ANSWER_MSG}) {
    const size_t before = u1->sent.size();
    HandlePacket(state_, u1, MakePacket(id, 90, "\xff\xfe nope").SerializeAsString());
    EXPECT_EQ(u1->sent.size(), before) << "garbage body echoed for msg_id "
                                       << static_cast<int>(id);
  }

  const size_t before_unknown_room = u1->sent.size();
  chirp::voice::IceCandidateMessage ice;
  ice.set_room_id("r-nope");
  HandlePacket(state_, u1,
               MakePacket(chirp::gateway::ICE_CANDIDATE_MSG, 91, ice.SerializeAsString())
                   .SerializeAsString());
  chirp::voice::SdpOfferMessage offer;
  offer.set_room_id("r-nope");
  HandlePacket(state_, u1,
               MakePacket(chirp::gateway::SDP_OFFER_MSG, 92, offer.SerializeAsString())
                   .SerializeAsString());

  EXPECT_EQ(u1->sent.size(), before_unknown_room);
  EXPECT_FALSE(u1->closed);
}

// A payload that is not even a Packet is dropped by the dispatcher with only
// a warning - no reply, no close, no state change.
TEST_F(VoiceServiceTest, DispatchToleratesNonPacketPayload) {
  HandlePacket(state_, session_, std::string("\xff\xfe\xfd not a Packet"));
  EXPECT_TRUE(session_->sent.empty());
  EXPECT_FALSE(session_->closed);
}

// Drives the real sweep timer: 5s cadence, sweep on expiry, self-reschedule,
// and the cancelled completion must not sweep again.
TEST_F(VoiceServiceTest, HeartbeatSweepTimerFiresReschedulesAndCancelStops) {
  const std::string room = CreateRoom(0);
  auto u1 = Join("u1", room);
  auto u2 = Join("u2", room);
  {
    std::lock_guard<std::mutex> lock(state_->mu);
    state_->cfg.heartbeat_timeout_ms = 75000;
    state_->session_to_last_seen_ms[u1.get()] = NowMs() - 100000;
  }

  asio::io_context io;
  auto timer = std::make_shared<asio::steady_timer>(io);
  ScheduleHeartbeatSweep(timer, state_);

  // Real 5-second cadence: pump until the first sweep fires. The stale
  // session closing is the observable that SweepHeartbeatTimeout ran via the
  // timer path (not a direct call).
  const auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(6);
  while (!u1->closed && std::chrono::steady_clock::now() < deadline) {
    io.poll();
    io.restart();
    std::this_thread::sleep_for(std::chrono::milliseconds(25));
  }
  ASSERT_TRUE(u1->closed);
  EXPECT_FALSE(u2->closed);

  // The fired completion rescheduled itself: exactly one live wait to
  // retire, and the aborted completion returns without sweeping again.
  EXPECT_EQ(timer->cancel(), 1u);
  io.poll();
  io.restart();
  {
    std::lock_guard<std::mutex> lock(state_->mu);
    state_->session_to_last_seen_ms[u2.get()] = NowMs() - 100000;
  }
  io.poll();
  io.restart();
  EXPECT_FALSE(u2->closed);
}

// ---------------------------------------------------------------------------
// Speaking queue (VOICE-RT 麦序模式): the full 申请 -> 批准 -> 发言 -> 下麦
// lifecycle plus host moderation, 切麦 and the permission boundaries.
// ---------------------------------------------------------------------------

TEST_F(VoiceServiceTest, MicQueueFlowApplyApproveRelease) {
  const std::string room = CreateRoom(0);
  auto alice = Join("alice", room);
  auto bob = Join("bob", room);
  SetManaged(room);

  // Alice applies for the mic (bob stays out of the queue).
  chirp::voice::MicQueueApplyRequest apply;
  apply.set_user_id("alice");
  apply.set_room_id(room);
  auto r = MicOp(alice, chirp::gateway::MIC_QUEUE_APPLY_REQ,
                 chirp::gateway::MIC_QUEUE_APPLY_RESP, 11, apply);
  ASSERT_TRUE(r.has_value());
  EXPECT_EQ(r->code(), chirp::common::OK);
  EXPECT_EQ(r->queue_depth(), 1);
  EXPECT_TRUE(r->holder_user_id().empty());

  // The host approves the queue head -> alice holds the floor.
  chirp::voice::MicQueueApproveRequest approve;
  approve.set_user_id("creator");
  approve.set_room_id(room);
  r = MicOp(session_, chirp::gateway::MIC_QUEUE_APPROVE_REQ,
            chirp::gateway::MIC_QUEUE_APPROVE_RESP, 12, approve);
  ASSERT_TRUE(r.has_value());
  EXPECT_EQ(r->code(), chirp::common::OK);
  EXPECT_EQ(r->holder_user_id(), "alice");
  EXPECT_EQ(r->queue_depth(), 0);

  // The other participant saw the state notify with the new holder.
  const auto bob_notifies = MicQueueNotifies(*bob);
  ASSERT_GE(bob_notifies.size(), 2u);  // apply + approve fan-outs
  EXPECT_EQ(bob_notifies.back().mode(), chirp::voice::MIC_MANAGED);
  EXPECT_EQ(bob_notifies.back().holder_user_id(), "alice");

  // A non-holder cannot pull the host's designated speaker off the mic.
  chirp::voice::MicQueueReleaseRequest hostile;
  hostile.set_user_id("bob");
  hostile.set_room_id(room);
  r = MicOp(bob, chirp::gateway::MIC_QUEUE_RELEASE_REQ,
            chirp::gateway::MIC_QUEUE_RELEASE_RESP, 13, hostile);
  ASSERT_TRUE(r.has_value());
  EXPECT_EQ(r->code(), chirp::common::AUTH_FAILED);

  // Alice steps down (下麦).
  chirp::voice::MicQueueReleaseRequest release;
  release.set_user_id("alice");
  release.set_room_id(room);
  r = MicOp(alice, chirp::gateway::MIC_QUEUE_RELEASE_REQ,
            chirp::gateway::MIC_QUEUE_RELEASE_RESP, 14, release);
  ASSERT_TRUE(r.has_value());
  EXPECT_EQ(r->code(), chirp::common::OK);
  EXPECT_TRUE(r->holder_user_id().empty());
}

TEST_F(VoiceServiceTest, MicQueueHostOnlyOperationsRejectedForMembers) {
  const std::string room = CreateRoom(0);
  auto alice = Join("alice", room);

  chirp::voice::SetMicModeRequest mode;
  mode.set_user_id("alice");
  mode.set_room_id(room);
  mode.set_mode(chirp::voice::MIC_MANAGED);
  auto r = MicOp(alice, chirp::gateway::SET_MIC_MODE_REQ,
                 chirp::gateway::SET_MIC_MODE_RESP, 10, mode);
  ASSERT_TRUE(r.has_value());
  EXPECT_EQ(r->code(), chirp::common::AUTH_FAILED);

  chirp::voice::MicQueueApproveRequest approve;
  approve.set_user_id("alice");
  approve.set_room_id(room);
  r = MicOp(alice, chirp::gateway::MIC_QUEUE_APPROVE_REQ,
            chirp::gateway::MIC_QUEUE_APPROVE_RESP, 11, approve);
  ASSERT_TRUE(r.has_value());
  EXPECT_EQ(r->code(), chirp::common::AUTH_FAILED);

  chirp::voice::MicQueueGrantRequest grant;
  grant.set_user_id("alice");
  grant.set_room_id(room);
  grant.set_target_user_id("bob");
  r = MicOp(alice, chirp::gateway::MIC_QUEUE_GRANT_REQ,
            chirp::gateway::MIC_QUEUE_GRANT_RESP, 12, grant);
  ASSERT_TRUE(r.has_value());
  EXPECT_EQ(r->code(), chirp::common::AUTH_FAILED);
}

TEST_F(VoiceServiceTest, MicQueueGrantBypassesQueueAndPullsApplicants) {
  const std::string room = CreateRoom(0);
  auto alice = Join("alice", room);
  auto bob = Join("bob", room);
  SetManaged(room);

  chirp::voice::MicQueueApplyRequest apply;
  apply.set_user_id("alice");
  apply.set_room_id(room);
  auto r = MicOp(alice, chirp::gateway::MIC_QUEUE_APPLY_REQ,
                 chirp::gateway::MIC_QUEUE_APPLY_RESP, 11, apply);
  ASSERT_TRUE(r.has_value());
  EXPECT_EQ(r->queue_depth(), 1);

  // 切麦: the host hands the floor to bob directly; alice stays queued.
  chirp::voice::MicQueueGrantRequest grant;
  grant.set_user_id("creator");
  grant.set_room_id(room);
  grant.set_target_user_id("bob");
  r = MicOp(session_, chirp::gateway::MIC_QUEUE_GRANT_REQ,
            chirp::gateway::MIC_QUEUE_GRANT_RESP, 12, grant);
  ASSERT_TRUE(r.has_value());
  EXPECT_EQ(r->code(), chirp::common::OK);
  EXPECT_EQ(r->holder_user_id(), "bob");
  EXPECT_EQ(r->queue_depth(), 1);

  // Granting a queued applicant pulls them off the queue.
  grant.set_target_user_id("alice");
  r = MicOp(session_, chirp::gateway::MIC_QUEUE_GRANT_REQ,
            chirp::gateway::MIC_QUEUE_GRANT_RESP, 13, grant);
  ASSERT_TRUE(r.has_value());
  EXPECT_EQ(r->holder_user_id(), "alice");
  EXPECT_EQ(r->queue_depth(), 0);
}

TEST_F(VoiceServiceTest, MicQueueApplyInFreeRoomIsRejected) {
  const std::string room = CreateRoom(0);
  auto alice = Join("alice", room);

  chirp::voice::MicQueueApplyRequest apply;
  apply.set_user_id("alice");
  apply.set_room_id(room);
  auto r = MicOp(alice, chirp::gateway::MIC_QUEUE_APPLY_REQ,
                 chirp::gateway::MIC_QUEUE_APPLY_RESP, 10, apply);
  ASSERT_TRUE(r.has_value());
  EXPECT_EQ(r->code(), chirp::common::INVALID_PARAM);
}

TEST_F(VoiceServiceTest, MicQueueApproveEmptyQueueAndUnknownTargetRejected) {
  const std::string room = CreateRoom(0);
  auto alice = Join("alice", room);
  SetManaged(room);

  // Nothing waiting: no head to approve.
  chirp::voice::MicQueueApproveRequest approve;
  approve.set_user_id("creator");
  approve.set_room_id(room);
  auto r = MicOp(session_, chirp::gateway::MIC_QUEUE_APPROVE_REQ,
                 chirp::gateway::MIC_QUEUE_APPROVE_RESP, 10, approve);
  ASSERT_TRUE(r.has_value());
  EXPECT_EQ(r->code(), chirp::common::INVALID_PARAM);

  // A target that never applied is not in the queue.
  approve.set_target_user_id("alice");
  r = MicOp(session_, chirp::gateway::MIC_QUEUE_APPROVE_REQ,
            chirp::gateway::MIC_QUEUE_APPROVE_RESP, 11, approve);
  ASSERT_TRUE(r.has_value());
  EXPECT_EQ(r->code(), chirp::common::USER_NOT_FOUND);
}

TEST_F(VoiceServiceTest, MicQueueLeaveClearsFloorAndKeepsQueue) {
  const std::string room = CreateRoom(0);
  auto alice = Join("alice", room);
  auto charlie = Join("charlie", room);
  SetManaged(room);

  chirp::voice::MicQueueApplyRequest apply;
  apply.set_user_id("alice");
  apply.set_room_id(room);
  ASSERT_TRUE(MicOp(alice, chirp::gateway::MIC_QUEUE_APPLY_REQ,
                    chirp::gateway::MIC_QUEUE_APPLY_RESP, 11, apply).has_value());
  apply.set_user_id("charlie");
  ASSERT_TRUE(MicOp(charlie, chirp::gateway::MIC_QUEUE_APPLY_REQ,
                    chirp::gateway::MIC_QUEUE_APPLY_RESP, 12, apply).has_value());

  chirp::voice::MicQueueApproveRequest approve;
  approve.set_user_id("creator");
  approve.set_room_id(room);
  approve.set_target_user_id("alice");
  ASSERT_TRUE(MicOp(session_, chirp::gateway::MIC_QUEUE_APPROVE_REQ,
                    chirp::gateway::MIC_QUEUE_APPROVE_RESP, 13, approve).has_value());

  // The holder leaves: the floor clears, charlie is still waiting.
  chirp::voice::LeaveRoomRequest leave;
  leave.set_user_id("alice");
  leave.set_room_id(room);
  HandlePacket(state_, alice,
               MakePacket(chirp::gateway::LEAVE_ROOM_REQ, 20, leave.SerializeAsString())
                   .SerializeAsString());

  const auto info = GetRoomInfo(room);
  ASSERT_TRUE(info.has_value());
  EXPECT_EQ(info->mic_mode(), chirp::voice::MIC_MANAGED);
  EXPECT_TRUE(info->mic_holder_user_id().empty());
  ASSERT_EQ(info->mic_queue_user_ids_size(), 1);
  EXPECT_EQ(info->mic_queue_user_ids(0), "charlie");
}

TEST_F(VoiceServiceTest, MicQueueHostLeaveResetsRoomToFreeSpeech) {
  const std::string room = CreateRoom(0);
  auto alice = Join("alice", room);
  SetManaged(room);

  chirp::voice::MicQueueApplyRequest apply;
  apply.set_user_id("alice");
  apply.set_room_id(room);
  ASSERT_TRUE(MicOp(alice, chirp::gateway::MIC_QUEUE_APPLY_REQ,
                    chirp::gateway::MIC_QUEUE_APPLY_RESP, 11, apply).has_value());
  chirp::voice::MicQueueApproveRequest approve;
  approve.set_user_id("creator");
  approve.set_room_id(room);
  ASSERT_TRUE(MicOp(session_, chirp::gateway::MIC_QUEUE_APPROVE_REQ,
                    chirp::gateway::MIC_QUEUE_APPROVE_RESP, 12, approve).has_value());

  // The host walks: moderation ends, the room returns to free speech.
  chirp::voice::LeaveRoomRequest leave;
  leave.set_user_id("creator");
  leave.set_room_id(room);
  HandlePacket(state_, session_,
               MakePacket(chirp::gateway::LEAVE_ROOM_REQ, 20, leave.SerializeAsString())
                   .SerializeAsString());

  const auto info = GetRoomInfo(room);
  ASSERT_TRUE(info.has_value());
  EXPECT_EQ(info->mic_mode(), chirp::voice::MIC_FREE);
  EXPECT_TRUE(info->mic_holder_user_id().empty());
  EXPECT_EQ(info->mic_queue_user_ids_size(), 0);
}

// Media tier classification (VOICE-RT 旁路分级): the boundary sits at 12
// participants; above it the SFU bypass takes over with a slower RTCP pace.
TEST(VoiceMediaTierTest, ClassificationPicksTierByRoomSize) {
  EXPECT_EQ(chirp::voice::direct::MediaTierForRoom(1), chirp::voice::FULL_MIX);
  EXPECT_EQ(chirp::voice::direct::MediaTierForRoom(12), chirp::voice::FULL_MIX);
  EXPECT_EQ(chirp::voice::direct::MediaTierForRoom(13), chirp::voice::FORWARD_BYPASS);
  EXPECT_EQ(chirp::voice::direct::MediaTierForRoom(500), chirp::voice::FORWARD_BYPASS);
  EXPECT_EQ(chirp::voice::direct::RtcpFeedbackMsForTier(chirp::voice::FULL_MIX), 1000);
  EXPECT_EQ(chirp::voice::direct::RtcpFeedbackMsForTier(chirp::voice::FORWARD_BYPASS), 2000);
}

TEST_F(VoiceServiceTest, JoinResponseCarriesTierForRoomSize) {
  const std::string room = CreateRoom(0);
  chirp::voice::JoinRoomResponse resp;

  // The 12th joiner still lands in the full-mix tier.
  std::shared_ptr<MockSession> s;
  for (int i = 1; i <= 12; ++i) {
    s = Join("u" + std::to_string(i), room);
  }
  ASSERT_TRUE(LastBody(*s, &resp));
  EXPECT_EQ(resp.media_tier(), chirp::voice::FULL_MIX);
  EXPECT_EQ(resp.rtcp_feedback_ms(), 1000);

  // Crossing the boundary flips the tier for the next joiner.
  s = Join("u13", room);
  ASSERT_TRUE(LastBody(*s, &resp));
  EXPECT_EQ(resp.media_tier(), chirp::voice::FORWARD_BYPASS);
  EXPECT_EQ(resp.rtcp_feedback_ms(), 2000);
}

}  // namespace
