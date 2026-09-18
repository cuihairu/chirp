// Unit tests for the voice service packet handlers (services/voice/src/main.cc).
// main.cc is included with main() renamed; handlers are driven directly with
// in-memory MockSessions, so room state, broadcast fan-out and the targeted
// WebRTC signaling relay are all pinned without sockets.

#include <gtest/gtest.h>

#include <memory>
#include <string>
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
  const std::string room = CreateRoom(0);
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
  chirp::common::LoginTokenVerifier verifier("s3cret");
  state_->cfg.verifier = &verifier;
  const std::string room = CreateRoom(0);  // create itself passes: no user_id to gate

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

  const std::string room = CreateRoom(0);
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

  const std::string room = CreateRoom(0);
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
  std::lock_guard<std::mutex> room_lock(state_->rooms[room]->mu);
  EXPECT_EQ(state_->rooms[room]->participants.count("u1"), 0u);
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

}  // namespace
