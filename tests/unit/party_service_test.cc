// Unit tests for the party service packet handlers (services/party/src/main.cc).
// main.cc is included with main() renamed; handlers are driven directly with
// in-memory MockSessions, so the party/invite tables, notify fan-out and the
// Redis write-through snapshots are all pinned without sockets (Redis goes
// through the loopback FakeRedisServer when needed).

#include <gtest/gtest.h>

#include <memory>
#include <string>
#include <vector>

#include "common/jwt.h"
#include "fake_servers.h"
#include "in_memory_redis.h"
#include "proto/auth.pb.h"
#include "proto/common.pb.h"
#include "proto/gateway.pb.h"
#include "proto/party.pb.h"

// Relative path, same convention as the voice/social service tests:
// unambiguous even if another main.cc ever lands on the include path.
#define main chirp_party_main
#include "../../services/party/src/main.cc"
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

// Counts frames of one notify msg_id a session has received.
int CountNotify(const MockSession& s, chirp::gateway::MsgID id) {
  int n = 0;
  for (const auto& pkt : ReceivedPackets(s)) {
    if (pkt.msg_id() == id) {
      ++n;
    }
  }
  return n;
}

// The LAST frame with the given msg_id (sessions accumulate responses and
// notifies in arrival order, so .back() alone is not enough).
bool LastNotifyOf(const MockSession& s, chirp::gateway::MsgID id, std::string* body) {
  const auto pkts = ReceivedPackets(s);
  for (auto it = pkts.rbegin(); it != pkts.rend(); ++it) {
    if (it->msg_id() == id) {
      *body = it->body();
      return true;
    }
  }
  return false;
}

// Parses the body of the last frame with the given msg_id as T. Responses
// that share a session with notifies must go through this, not LastBody:
// a notify often arrives after the response.
template <typename T>
bool LastOf(const MockSession& s, chirp::gateway::MsgID id, T* out) {
  std::string body;
  if (!LastNotifyOf(s, id, &body)) {
    return false;
  }
  return out->ParseFromString(body);
}

// Parses the most recent frame as a Packet and its body as T.
template <typename T>
bool LastBody(const MockSession& s, T* out, chirp::gateway::MsgID* id = nullptr) {
  if (s.sent.empty()) {
    return false;
  }
  Packet pkt;
  if (!DecodeFramed(s.sent.back(), &pkt)) {
    return false;
  }
  if (id) {
    *id = pkt.msg_id();
  }
  return out->ParseFromString(pkt.body());
}

class PartyServiceTest : public ::testing::Test {
 protected:
  std::shared_ptr<PartyState> state_ = std::make_shared<PartyState>();
  std::shared_ptr<MockSession> session_ = std::make_shared<MockSession>();
  std::shared_ptr<chirp::network::RedisClient> redis_;
  const chirp::common::LoginTokenVerifier* verifier_ = nullptr;

  void Deliver(chirp::gateway::MsgID id, int64_t seq, const std::string& body,
               const std::shared_ptr<MockSession>& to = nullptr) {
    HandlePacket(state_, redis_, verifier_, to ? to : session_,
                 MakePacket(id, seq, body).SerializeAsString());
  }

  // Scaffold login (token is user_id) on a fresh mock session.
  std::shared_ptr<MockSession> Login(const std::string& user_id, const std::string& device = "") {
    auto s = std::make_shared<MockSession>();
    chirp::auth::LoginRequest req;
    req.set_token(user_id);
    req.set_device_id(device);
    Deliver(chirp::gateway::LOGIN_REQ, 1, req.SerializeAsString(), s);
    chirp::auth::LoginResponse resp;
    EXPECT_TRUE(LastBody(*s, &resp));
    EXPECT_EQ(resp.code(), chirp::common::OK) << user_id;
    return s;
  }

  // Creates a party as `leader`; returns the new party_id ("", on error).
  std::string CreateParty(const std::shared_ptr<MockSession>& leader, const std::string& user_id,
                          int32_t max_members = 0) {
    chirp::party::CreatePartyRequest req;
    req.set_user_id(user_id);
    if (max_members > 0) {
      req.set_max_members(max_members);
    }
    Deliver(chirp::gateway::CREATE_PARTY_REQ, 10, req.SerializeAsString(), leader);
    chirp::party::CreatePartyResponse resp;
    LastBody(*leader, &resp);
    return resp.party().party_id();
  }

  // `from` invites `target` into party_id; returns the invite_id.
  std::string Invite(const std::shared_ptr<MockSession>& from, const std::string& from_id,
                     const std::string& party_id, const std::string& target) {
    chirp::party::InviteToPartyRequest req;
    req.set_user_id(from_id);
    req.set_party_id(party_id);
    req.set_target_user_id(target);
    Deliver(chirp::gateway::INVITE_TO_PARTY_REQ, 11, req.SerializeAsString(), from);
    chirp::party::InviteToPartyResponse resp;
    LastBody(*from, &resp);
    return resp.invite_id();
  }

  // `user` accepts invite_id; returns the response for further assertions.
  chirp::party::AcceptInviteResponse Accept(const std::shared_ptr<MockSession>& user,
                                            const std::string& user_id,
                                            const std::string& invite_id) {
    chirp::party::AcceptInviteRequest req;
    req.set_user_id(user_id);
    req.set_invite_id(invite_id);
    Deliver(chirp::gateway::ACCEPT_INVITE_REQ, 12, req.SerializeAsString(), user);
    chirp::party::AcceptInviteResponse resp;
    LastBody(*user, &resp);
    return resp;
  }

  // Composed: invite + accept, with both codes asserted OK.
  void JoinViaInvite(const std::shared_ptr<MockSession>& from, const std::string& from_id,
                     const std::string& party_id, const std::shared_ptr<MockSession>& target_session,
                     const std::string& target_id) {
    const auto invite_id = Invite(from, from_id, party_id, target_id);
    const auto resp = Accept(target_session, target_id, invite_id);
    ASSERT_EQ(resp.code(), chirp::common::OK) << target_id;
  }

  chirp::party::GetMyPartyResponse GetMyParty(const std::shared_ptr<MockSession>& user,
                                              const std::string& user_id) {
    chirp::party::GetMyPartyRequest req;
    req.set_user_id(user_id);
    Deliver(chirp::gateway::GET_MY_PARTY_REQ, 20, req.SerializeAsString(), user);
    chirp::party::GetMyPartyResponse resp;
    LastBody(*user, &resp);
    return resp;
  }
};

// ---------------------------------------------------------------------------
// Login / gating
// ---------------------------------------------------------------------------

TEST_F(PartyServiceTest, ScaffoldLoginSucceedsAndBinds) {
  chirp::auth::LoginRequest req;
  req.set_token("user_a");
  Deliver(chirp::gateway::LOGIN_REQ, 1, req.SerializeAsString());

  chirp::auth::LoginResponse resp;
  ASSERT_TRUE(LastBody(*session_, &resp));
  EXPECT_EQ(resp.code(), chirp::common::OK);
  EXPECT_EQ(resp.user_id(), "user_a");
  EXPECT_FALSE(resp.session_id().empty());
  // The binding powers every later business handler on this session.
  auto party = GetMyParty(session_, "user_a");
  EXPECT_EQ(party.code(), chirp::common::OK);
  EXPECT_FALSE(party.in_party());
}

TEST_F(PartyServiceTest, JwtTokenLogsInAsSubject) {
  const int64_t now = NowMs() / 1000;
  chirp::common::LoginTokenVerifier verifier("s3cret");
  verifier_ = &verifier;

  chirp::auth::LoginRequest req;
  req.set_token(chirp::common::JwtSignHS256("user_jwt", now, "s3cret", now + 600));
  Deliver(chirp::gateway::LOGIN_REQ, 1, req.SerializeAsString());

  chirp::auth::LoginResponse resp;
  ASSERT_TRUE(LastBody(*session_, &resp));
  EXPECT_EQ(resp.code(), chirp::common::OK);
  EXPECT_EQ(resp.user_id(), "user_jwt");
}

TEST_F(PartyServiceTest, BadJwtTokensRejectedAndUnbound) {
  const int64_t now = NowMs() / 1000;
  chirp::common::LoginTokenVerifier verifier("s3cret");
  verifier_ = &verifier;

  const std::vector<std::string> bad_tokens = {
      chirp::common::JwtSignHS256("user_a", now, "s3cret", now - 10),  // expired
      chirp::common::JwtSignHS256("user_a", now, "wrong", now + 600),  // wrong secret
      chirp::common::JwtSignHS256("user_a", now, "s3cret", 0),         // missing exp
      "garbage-not-a-jwt",
  };
  for (const auto& token : bad_tokens) {
    auto s = std::make_shared<MockSession>();
    chirp::auth::LoginRequest req;
    req.set_token(token);
    Deliver(chirp::gateway::LOGIN_REQ, 1, req.SerializeAsString(), s);
    chirp::auth::LoginResponse resp;
    ASSERT_TRUE(LastBody(*s, &resp));
    EXPECT_EQ(resp.code(), chirp::common::AUTH_FAILED) << token;
    EXPECT_EQ(resp.user_id(), "");
  }
  // Nothing got bound: a business request on the still-open session fails.
  auto party = GetMyParty(session_, "user_a");
  EXPECT_EQ(party.code(), chirp::common::AUTH_FAILED);
}

TEST_F(PartyServiceTest, SameDeviceRebindKicksOldSession) {
  auto first = Login("user_a", "phone");
  auto second = Login("user_a", "phone");

  // The old session is told and closed; the registry keeps only the new one.
  ASSERT_EQ(CountNotify(*first, chirp::gateway::KICK_NOTIFY), 1);
  EXPECT_TRUE(first->close_after_send);
  EXPECT_EQ(chirp::network::GetUserSessions(state_->registry, "user_a").size(), 1u);
  EXPECT_EQ(chirp::network::GetUserSessions(state_->registry, "user_a")[0], second);
}

TEST_F(PartyServiceTest, DifferentDevicesCoexist) {
  auto phone = Login("user_a", "phone");
  auto desktop = Login("user_a", "desktop");
  auto sessions = chirp::network::GetUserSessions(state_->registry, "user_a");
  EXPECT_EQ(sessions.size(), 2u);
  // Neither was kicked.
  EXPECT_FALSE(phone->close_after_send);
  EXPECT_FALSE(desktop->close_after_send);
}

TEST_F(PartyServiceTest, BusinessRequestsRequireLogin) {
  // A session that never logged in gets AUTH_FAILED, not a party.
  auto stranger = std::make_shared<MockSession>();
  chirp::party::CreatePartyRequest req;
  req.set_user_id("user_a");
  Deliver(chirp::gateway::CREATE_PARTY_REQ, 1, req.SerializeAsString(), stranger);
  chirp::party::CreatePartyResponse resp;
  ASSERT_TRUE(LastBody(*stranger, &resp));
  EXPECT_EQ(resp.code(), chirp::common::AUTH_FAILED);
  EXPECT_EQ(state_->parties.size(), 0u);
}

// ---------------------------------------------------------------------------
// Lifecycle
// ---------------------------------------------------------------------------

TEST_F(PartyServiceTest, CreatePartySnapshotCreatorLeads) {
  auto a = Login("user_a");
  const auto party_id = CreateParty(a, "user_a");

  ASSERT_FALSE(party_id.empty());
  chirp::party::CreatePartyResponse resp;
  ASSERT_TRUE(LastBody(*a, &resp));
  EXPECT_EQ(resp.code(), chirp::common::OK);
  EXPECT_EQ(resp.party().leader_id(), "user_a");
  EXPECT_EQ(resp.party().max_members(), 5);  // 0 => default
  ASSERT_EQ(resp.party().members_size(), 1);
  EXPECT_EQ(resp.party().members(0).user_id(), "user_a");
  EXPECT_FALSE(resp.party().members(0).ready());
  EXPECT_GT(resp.party().created_at(), 0);
}

TEST_F(PartyServiceTest, CreateRejectsOversizeCapacity) {
  auto a = Login("user_a");

  chirp::party::CreatePartyRequest over;
  over.set_user_id("user_a");
  over.set_max_members(9);
  Deliver(chirp::gateway::CREATE_PARTY_REQ, 1, over.SerializeAsString(), a);
  chirp::party::CreatePartyResponse resp;
  ASSERT_TRUE(LastBody(*a, &resp));
  EXPECT_EQ(resp.code(), chirp::common::INVALID_PARAM);
  EXPECT_EQ(state_->parties.size(), 0u);

  // The hard cap itself is accepted.
  EXPECT_FALSE(CreateParty(a, "user_a", 8).empty());
}

TEST_F(PartyServiceTest, LeaveBroadcastsLeftExceptLeaver) {
  auto a = Login("user_a");
  auto b = Login("user_b");
  const auto party_id = CreateParty(a, "user_a");
  JoinViaInvite(a, "user_a", party_id, b, "user_b");

  chirp::party::LeavePartyRequest leave;
  leave.set_user_id("user_a");
  leave.set_party_id(party_id);
  Deliver(chirp::gateway::LEAVE_PARTY_REQ, 13, leave.SerializeAsString(), a);

  chirp::party::LeavePartyResponse resp;
  ASSERT_TRUE(LastBody(*a, &resp));
  EXPECT_EQ(resp.code(), chirp::common::OK);
  EXPECT_FALSE(resp.party_disbanded());

  // b sees the leave with reason "left"; a (the leaver) gets no LEFT echo.
  std::string body;
  ASSERT_TRUE(LastNotifyOf(*b, chirp::gateway::PARTY_LEFT_NOTIFY, &body));
  chirp::party::PartyLeftNotify notify;
  ASSERT_TRUE(notify.ParseFromString(body));
  EXPECT_EQ(notify.user_id(), "user_a");
  EXPECT_EQ(notify.reason(), "left");
  EXPECT_EQ(CountNotify(*a, chirp::gateway::PARTY_LEFT_NOTIFY), 0);
}

TEST_F(PartyServiceTest, LeaderLeaveSucceedsEarliestJoiner) {
  auto a = Login("user_a");
  auto b = Login("user_b");
  auto c = Login("user_c");
  const auto party_id = CreateParty(a, "user_a");
  JoinViaInvite(a, "user_a", party_id, b, "user_b");
  JoinViaInvite(a, "user_a", party_id, c, "user_c");

  chirp::party::LeavePartyRequest leave;
  leave.set_user_id("user_a");
  leave.set_party_id(party_id);
  Deliver(chirp::gateway::LEAVE_PARTY_REQ, 13, leave.SerializeAsString(), a);

  // The earliest remaining joiner (b) becomes leader; every remaining member
  // gets the snapshot carrying the new leader.
  for (auto* member : {&b, &c}) {
    std::string body;
    ASSERT_TRUE(LastNotifyOf(**member, chirp::gateway::PARTY_STATE_CHANGED_NOTIFY, &body));
    chirp::party::PartyStateChangedNotify notify;
    ASSERT_TRUE(notify.ParseFromString(body));
    EXPECT_EQ(notify.party().leader_id(), "user_b");
    EXPECT_EQ(notify.party().members_size(), 2);
  }
  auto mine = GetMyParty(b, "user_b");
  ASSERT_TRUE(mine.in_party());
  EXPECT_EQ(mine.party().leader_id(), "user_b");
}

TEST_F(PartyServiceTest, LastMemberLeaveDisbandsSilently) {
  auto a = Login("user_a");
  const auto party_id = CreateParty(a, "user_a");

  chirp::party::LeavePartyRequest leave;
  leave.set_user_id("user_a");
  leave.set_party_id(party_id);
  Deliver(chirp::gateway::LEAVE_PARTY_REQ, 13, leave.SerializeAsString(), a);

  chirp::party::LeavePartyResponse resp;
  ASSERT_TRUE(LastBody(*a, &resp));
  EXPECT_EQ(resp.code(), chirp::common::OK);
  EXPECT_TRUE(resp.party_disbanded());
  EXPECT_EQ(state_->parties.size(), 0u);
  EXPECT_EQ(state_->user_to_party.size(), 0u);
}

TEST_F(PartyServiceTest, DisbandRequiresLeaderAndNotifiesRemaining) {
  auto a = Login("user_a");
  auto b = Login("user_b");
  const auto party_id = CreateParty(a, "user_a");
  JoinViaInvite(a, "user_a", party_id, b, "user_b");

  // A plain member cannot disband.
  chirp::party::DisbandPartyRequest req;
  req.set_user_id("user_b");
  req.set_party_id(party_id);
  Deliver(chirp::gateway::DISBAND_PARTY_REQ, 14, req.SerializeAsString(), b);
  chirp::party::DisbandPartyResponse resp;
  ASSERT_TRUE(LastBody(*b, &resp));
  EXPECT_EQ(resp.code(), chirp::common::AUTH_FAILED);
  EXPECT_EQ(state_->parties.size(), 1u);

  // The leader can; the remaining member is told.
  req.set_user_id("user_a");
  Deliver(chirp::gateway::DISBAND_PARTY_REQ, 15, req.SerializeAsString(), a);
  ASSERT_TRUE(LastBody(*a, &resp));
  EXPECT_EQ(resp.code(), chirp::common::OK);

  std::string body;
  ASSERT_TRUE(LastNotifyOf(*b, chirp::gateway::PARTY_DISBANDED_NOTIFY, &body));
  chirp::party::PartyDisbandedNotify notify;
  ASSERT_TRUE(notify.ParseFromString(body));
  EXPECT_EQ(notify.party_id(), party_id);
  EXPECT_EQ(notify.actor_user_id(), "user_a");
  EXPECT_EQ(state_->parties.size(), 0u);
  EXPECT_EQ(state_->user_to_party.size(), 0u);
}

TEST_F(PartyServiceTest, UnknownPartyOpsReturnNotFound) {
  auto a = Login("user_a");

  chirp::party::LeavePartyRequest leave;
  leave.set_user_id("user_a");
  leave.set_party_id("party_nope");
  Deliver(chirp::gateway::LEAVE_PARTY_REQ, 1, leave.SerializeAsString(), a);
  chirp::party::LeavePartyResponse leave_resp;
  ASSERT_TRUE(LastBody(*a, &leave_resp));
  EXPECT_EQ(leave_resp.code(), chirp::common::USER_NOT_FOUND);

  chirp::party::KickMemberRequest kick;
  kick.set_user_id("user_a");
  kick.set_party_id("party_nope");
  kick.set_target_user_id("user_x");
  Deliver(chirp::gateway::KICK_PARTY_MEMBER_REQ, 2, kick.SerializeAsString(), a);
  chirp::party::KickMemberResponse kick_resp;
  ASSERT_TRUE(LastBody(*a, &kick_resp));
  EXPECT_EQ(kick_resp.code(), chirp::common::USER_NOT_FOUND);

  chirp::party::SetReadyRequest ready;
  ready.set_user_id("user_a");
  ready.set_party_id("party_nope");
  Deliver(chirp::gateway::SET_READY_REQ, 3, ready.SerializeAsString(), a);
  chirp::party::SetReadyResponse ready_resp;
  ASSERT_TRUE(LastBody(*a, &ready_resp));
  EXPECT_EQ(ready_resp.code(), chirp::common::USER_NOT_FOUND);
}

// ---------------------------------------------------------------------------
// Invites
// ---------------------------------------------------------------------------

TEST_F(PartyServiceTest, InviteNotifiesAllTargetDevices) {
  auto a = Login("user_a");
  auto b_phone = Login("user_b", "phone");
  auto b_desktop = Login("user_b", "desktop");
  const auto party_id = CreateParty(a, "user_a");

  const auto invite_id = Invite(a, "user_a", party_id, "user_b");
  ASSERT_FALSE(invite_id.empty());

  chirp::party::InviteToPartyResponse resp;
  ASSERT_TRUE(LastBody(*a, &resp));
  EXPECT_EQ(resp.code(), chirp::common::OK);
  EXPECT_EQ(resp.invite_id(), invite_id);
  EXPECT_GT(resp.expires_at(), NowMs());

  // Both devices of the invitee get exactly one INVITE_NOTIFY.
  for (auto* device : {&b_phone, &b_desktop}) {
    ASSERT_EQ(CountNotify(**device, chirp::gateway::INVITE_NOTIFY), 1);
    std::string body;
    ASSERT_TRUE(LastNotifyOf(**device, chirp::gateway::INVITE_NOTIFY, &body));
    chirp::party::InviteNotify notify;
    ASSERT_TRUE(notify.ParseFromString(body));
    EXPECT_EQ(notify.invite_id(), invite_id);
    EXPECT_EQ(notify.from_user_id(), "user_a");
    EXPECT_EQ(notify.expires_at(), resp.expires_at());
    EXPECT_EQ(notify.party().party_id(), party_id);
    EXPECT_EQ(notify.party().members_size(), 1);
  }
}

TEST_F(PartyServiceTest, InviteRequiresMembership) {
  auto a = Login("user_a");
  auto outsider = Login("user_out");
  const auto party_id = CreateParty(a, "user_a");

  // A non-member of an existing party cannot invite.
  chirp::party::InviteToPartyRequest req;
  req.set_user_id("user_out");
  req.set_party_id(party_id);
  req.set_target_user_id("user_x");
  Deliver(chirp::gateway::INVITE_TO_PARTY_REQ, 1, req.SerializeAsString(), outsider);
  chirp::party::InviteToPartyResponse resp;
  ASSERT_TRUE(LastBody(*outsider, &resp));
  EXPECT_EQ(resp.code(), chirp::common::AUTH_FAILED);

  // An unknown party is "no such party".
  req.set_party_id("party_nope");
  Deliver(chirp::gateway::INVITE_TO_PARTY_REQ, 2, req.SerializeAsString(), outsider);
  ASSERT_TRUE(LastBody(*outsider, &resp));
  EXPECT_EQ(resp.code(), chirp::common::USER_NOT_FOUND);
}

TEST_F(PartyServiceTest, InviteRejectsSelfAndPartyMembers) {
  auto a = Login("user_a");
  auto b = Login("user_b");
  const auto party_id = CreateParty(a, "user_a");
  JoinViaInvite(a, "user_a", party_id, b, "user_b");

  chirp::party::InviteToPartyRequest self;
  self.set_user_id("user_a");
  self.set_party_id(party_id);
  self.set_target_user_id("user_a");
  Deliver(chirp::gateway::INVITE_TO_PARTY_REQ, 1, self.SerializeAsString(), a);
  chirp::party::InviteToPartyResponse resp;
  ASSERT_TRUE(LastBody(*a, &resp));
  EXPECT_EQ(resp.code(), chirp::common::INVALID_PARAM);

  // b is already in this (or any) party — inviting them is a no-op error.
  chirp::party::InviteToPartyRequest member = self;
  member.set_target_user_id("user_b");
  Deliver(chirp::gateway::INVITE_TO_PARTY_REQ, 2, member.SerializeAsString(), a);
  ASSERT_TRUE(LastBody(*a, &resp));
  EXPECT_EQ(resp.code(), chirp::common::INVALID_PARAM);
}

TEST_F(PartyServiceTest, ReinviteReusesIdAndRepings) {
  auto a = Login("user_a");
  auto b = Login("user_b");
  const auto party_id = CreateParty(a, "user_a");

  const auto first_id = Invite(a, "user_a", party_id, "user_b");
  const auto second_id = Invite(a, "user_a", party_id, "user_b");

  // Same open invite, refreshed; the invitee got pinged twice.
  EXPECT_EQ(second_id, first_id);
  EXPECT_EQ(CountNotify(*b, chirp::gateway::INVITE_NOTIFY), 2);
  EXPECT_EQ(state_->invites.size(), 1u);
}

TEST_F(PartyServiceTest, FullPartyInviteRejected) {
  auto a = Login("user_a");
  const auto party_id = CreateParty(a, "user_a", 1);

  chirp::party::InviteToPartyRequest req;
  req.set_user_id("user_a");
  req.set_party_id(party_id);
  req.set_target_user_id("user_b");
  Deliver(chirp::gateway::INVITE_TO_PARTY_REQ, 1, req.SerializeAsString(), a);
  chirp::party::InviteToPartyResponse resp;
  ASSERT_TRUE(LastBody(*a, &resp));
  EXPECT_EQ(resp.code(), chirp::common::INVALID_PARAM);
  EXPECT_EQ(state_->invites.size(), 0u);
}

TEST_F(PartyServiceTest, AcceptJoinsConsumesAndClearsOthers) {
  auto a = Login("user_a");
  auto c = Login("user_c");
  auto b = Login("user_b");
  const auto party1 = CreateParty(a, "user_a");
  const auto party2 = CreateParty(c, "user_c");

  const auto invite1 = Invite(a, "user_a", party1, "user_b");
  const auto invite2 = Invite(c, "user_c", party2, "user_b");

  const auto resp = Accept(b, "user_b", invite1);
  ASSERT_EQ(resp.code(), chirp::common::OK);
  EXPECT_EQ(resp.party().party_id(), party1);
  ASSERT_EQ(resp.party().members_size(), 2);
  EXPECT_EQ(resp.party().members(1).user_id(), "user_b");
  EXPECT_FALSE(resp.party().members(1).ready());

  // The inviter learns of the acceptance.
  std::string body;
  ASSERT_TRUE(LastNotifyOf(*a, chirp::gateway::INVITE_RESULT_NOTIFY, &body));
  chirp::party::InviteResultNotify result;
  ASSERT_TRUE(result.ParseFromString(body));
  EXPECT_EQ(result.invite_id(), invite1);
  EXPECT_EQ(result.target_user_id(), "user_b");
  EXPECT_TRUE(result.accepted());

  // Joining one party invalidated every other open invite to this user.
  EXPECT_EQ(state_->invites.size(), 0u);
  const auto again = Accept(b, "user_b", invite2);
  EXPECT_EQ(again.code(), chirp::common::USER_NOT_FOUND);
}

TEST_F(PartyServiceTest, SecondDeviceAcceptIdempotent) {
  auto a = Login("user_a");
  auto b1 = Login("user_b", "phone");
  auto b2 = Login("user_b", "desktop");
  const auto party_id = CreateParty(a, "user_a");
  const auto invite_id = Invite(a, "user_a", party_id, "user_b");

  const auto first = Accept(b1, "user_b", invite_id);
  ASSERT_EQ(first.code(), chirp::common::OK);

  // The second device accepting the (already consumed) invite is an
  // idempotent OK with the same snapshot — and no duplicate JOINED/RESULT.
  const auto second = Accept(b2, "user_b", invite_id);
  ASSERT_EQ(second.code(), chirp::common::OK);
  EXPECT_EQ(second.party().party_id(), party_id);
  EXPECT_EQ(second.party().members_size(), 2);

  EXPECT_EQ(CountNotify(*a, chirp::gateway::INVITE_RESULT_NOTIFY), 1);
  int joined_echoes = 0;
  for (const auto& pkt : ReceivedPackets(*a)) {
    if (pkt.msg_id() == chirp::gateway::PARTY_JOINED_NOTIFY) {
      ++joined_echoes;
    }
  }
  EXPECT_EQ(joined_echoes, 1);
}

TEST_F(PartyServiceTest, DeclineKeepsOtherInvitesAlive) {
  auto a = Login("user_a");
  auto c = Login("user_c");
  auto b = Login("user_b");
  const auto party1 = CreateParty(a, "user_a");
  const auto party2 = CreateParty(c, "user_c");

  // Two open invites from two parties; declining one leaves the other alone.
  const auto invite2 = Invite(c, "user_c", party2, "user_b");
  const auto invite1 = Invite(a, "user_a", party1, "user_b");

  chirp::party::DeclineInviteRequest decline;
  decline.set_user_id("user_b");
  decline.set_invite_id(invite2);
  Deliver(chirp::gateway::DECLINE_INVITE_REQ, 16, decline.SerializeAsString(), b);
  chirp::party::DeclineInviteResponse dresp;
  ASSERT_TRUE(LastOf(*b, chirp::gateway::DECLINE_INVITE_RESP, &dresp));
  EXPECT_EQ(dresp.code(), chirp::common::OK);
  EXPECT_EQ(state_->invites.count(invite1), 1u);

  // a's invite still works.
  const auto resp = Accept(b, "user_b", invite1);
  ASSERT_EQ(resp.code(), chirp::common::OK);
  EXPECT_EQ(resp.party().party_id(), party1);
}

TEST_F(PartyServiceTest, AcceptUnknownOrExpiredInvite) {
  auto a = Login("user_a");
  auto b = Login("user_b");
  const auto party_id = CreateParty(a, "user_a");

  chirp::party::AcceptInviteRequest unknown;
  unknown.set_user_id("user_b");
  unknown.set_invite_id("deadbeef");
  Deliver(chirp::gateway::ACCEPT_INVITE_REQ, 1, unknown.SerializeAsString(), b);
  chirp::party::AcceptInviteResponse resp;
  ASSERT_TRUE(LastBody(*b, &resp));
  EXPECT_EQ(resp.code(), chirp::common::USER_NOT_FOUND);

  // An expired open invite reads the same and is erased lazily.
  const auto invite_id = Invite(a, "user_a", party_id, "user_b");
  ASSERT_EQ(state_->invites.count(invite_id), 1u);
  state_->invites[invite_id].expires_at = NowMs() - 1;
  const auto expired = Accept(b, "user_b", invite_id);
  EXPECT_EQ(expired.code(), chirp::common::USER_NOT_FOUND);
  EXPECT_EQ(state_->invites.size(), 0u);
}

TEST_F(PartyServiceTest, AcceptAfterDisbandConsumesInvite) {
  auto a = Login("user_a");
  auto b = Login("user_b");
  const auto party_id = CreateParty(a, "user_a");
  const auto invite_id = Invite(a, "user_a", party_id, "user_b");

  chirp::party::DisbandPartyRequest disband;
  disband.set_user_id("user_a");
  disband.set_party_id(party_id);
  Deliver(chirp::gateway::DISBAND_PARTY_REQ, 14, disband.SerializeAsString(), a);

  // Open invites die with the party.
  const auto resp = Accept(b, "user_b", invite_id);
  EXPECT_EQ(resp.code(), chirp::common::USER_NOT_FOUND);
  EXPECT_EQ(state_->invites.size(), 0u);
}

TEST_F(PartyServiceTest, FullPartyAcceptKeepsInviteForRetry) {
  auto a = Login("user_a");
  auto b = Login("user_b");
  auto c = Login("user_c");
  const auto party_id = CreateParty(a, "user_a", 2);
  const auto invite_b = Invite(a, "user_a", party_id, "user_b");
  const auto invite_c = Invite(a, "user_a", party_id, "user_c");

  ASSERT_EQ(Accept(b, "user_b", invite_b).code(), chirp::common::OK);  // 2/2 full

  // c's invite still stands, but the seat is gone: rejected yet kept.
  const auto full = Accept(c, "user_c", invite_c);
  EXPECT_EQ(full.code(), chirp::common::INVALID_PARAM);
  ASSERT_EQ(state_->invites.count(invite_c), 1u);

  // A seat frees up — the same invite now succeeds.
  chirp::party::KickMemberRequest kick;
  kick.set_user_id("user_a");
  kick.set_party_id(party_id);
  kick.set_target_user_id("user_b");
  Deliver(chirp::gateway::KICK_PARTY_MEMBER_REQ, 15, kick.SerializeAsString(), a);

  const auto retried = Accept(c, "user_c", invite_c);
  ASSERT_EQ(retried.code(), chirp::common::OK);
  EXPECT_EQ(retried.party().members_size(), 2);
}

TEST_F(PartyServiceTest, DeclineNotifiesInviterAndConsumes) {
  auto a = Login("user_a");
  auto b = Login("user_b");
  const auto party_id = CreateParty(a, "user_a");
  const auto invite_id = Invite(a, "user_a", party_id, "user_b");

  chirp::party::DeclineInviteRequest decline;
  decline.set_user_id("user_b");
  decline.set_invite_id(invite_id);
  Deliver(chirp::gateway::DECLINE_INVITE_REQ, 16, decline.SerializeAsString(), b);
  chirp::party::DeclineInviteResponse resp;
  ASSERT_TRUE(LastBody(*b, &resp));
  EXPECT_EQ(resp.code(), chirp::common::OK);

  std::string body;
  ASSERT_TRUE(LastNotifyOf(*a, chirp::gateway::INVITE_RESULT_NOTIFY, &body));
  chirp::party::InviteResultNotify result;
  ASSERT_TRUE(result.ParseFromString(body));
  EXPECT_EQ(result.invite_id(), invite_id);
  EXPECT_FALSE(result.accepted());

  // The invite is gone.
  const auto again = Accept(b, "user_b", invite_id);
  EXPECT_EQ(again.code(), chirp::common::USER_NOT_FOUND);
}

// ---------------------------------------------------------------------------
// Member operations
// ---------------------------------------------------------------------------

TEST_F(PartyServiceTest, KickGuards) {
  auto a = Login("user_a");
  auto b = Login("user_b");
  auto c = Login("user_c");
  const auto party_id = CreateParty(a, "user_a");
  JoinViaInvite(a, "user_a", party_id, b, "user_b");
  JoinViaInvite(a, "user_a", party_id, c, "user_c");

  chirp::party::KickMemberRequest req;
  req.set_party_id(party_id);

  // Non-leader: AUTH_FAILED.
  req.set_user_id("user_c");
  req.set_target_user_id("user_b");
  Deliver(chirp::gateway::KICK_PARTY_MEMBER_REQ, 1, req.SerializeAsString(), c);
  chirp::party::KickMemberResponse resp;
  ASSERT_TRUE(LastBody(*c, &resp));
  EXPECT_EQ(resp.code(), chirp::common::AUTH_FAILED);

  // Non-member target: USER_NOT_FOUND.
  req.set_user_id("user_a");
  req.set_target_user_id("user_x");
  Deliver(chirp::gateway::KICK_PARTY_MEMBER_REQ, 2, req.SerializeAsString(), a);
  ASSERT_TRUE(LastBody(*a, &resp));
  EXPECT_EQ(resp.code(), chirp::common::USER_NOT_FOUND);

  // Kicking yourself is a leave, not a kick: INVALID_PARAM.
  req.set_target_user_id("user_a");
  Deliver(chirp::gateway::KICK_PARTY_MEMBER_REQ, 3, req.SerializeAsString(), a);
  ASSERT_TRUE(LastBody(*a, &resp));
  EXPECT_EQ(resp.code(), chirp::common::INVALID_PARAM);
  EXPECT_EQ(state_->parties[party_id]->members.size(), 3u);
}

TEST_F(PartyServiceTest, KickNotifiesTargetOnAllDevicesAndRemaining) {
  auto a = Login("user_a");
  auto b_phone = Login("user_b", "phone");
  auto b_desktop = Login("user_b", "desktop");
  auto c = Login("user_c");
  const auto party_id = CreateParty(a, "user_a");
  JoinViaInvite(a, "user_a", party_id, b_phone, "user_b");
  JoinViaInvite(a, "user_a", party_id, c, "user_c");

  chirp::party::KickMemberRequest kick;
  kick.set_user_id("user_a");
  kick.set_party_id(party_id);
  kick.set_target_user_id("user_b");
  Deliver(chirp::gateway::KICK_PARTY_MEMBER_REQ, 15, kick.SerializeAsString(), a);
  chirp::party::KickMemberResponse resp;
  ASSERT_TRUE(LastOf(*a, chirp::gateway::KICK_PARTY_MEMBER_RESP, &resp));
  EXPECT_EQ(resp.code(), chirp::common::OK);

  // The kicked user learns on every device, with the actor attached.
  for (auto* device : {&b_phone, &b_desktop}) {
    ASSERT_EQ(CountNotify(**device, chirp::gateway::PARTY_KICKED_NOTIFY), 1);
    std::string body;
    ASSERT_TRUE(LastNotifyOf(**device, chirp::gateway::PARTY_KICKED_NOTIFY, &body));
    chirp::party::PartyKickedNotify notify;
    ASSERT_TRUE(notify.ParseFromString(body));
    EXPECT_EQ(notify.party_id(), party_id);
    EXPECT_EQ(notify.actor_user_id(), "user_a");
  }

  // Remaining members see a LEFT with reason "kicked".
  std::string body;
  ASSERT_TRUE(LastNotifyOf(*c, chirp::gateway::PARTY_LEFT_NOTIFY, &body));
  chirp::party::PartyLeftNotify left;
  ASSERT_TRUE(left.ParseFromString(body));
  EXPECT_EQ(left.user_id(), "user_b");
  EXPECT_EQ(left.reason(), "kicked");
  EXPECT_EQ(state_->parties[party_id]->members.size(), 2u);
}

TEST_F(PartyServiceTest, KickOnlyMemberLeavesLeaderAlone) {
  auto a = Login("user_a");
  auto b = Login("user_b");
  const auto party_id = CreateParty(a, "user_a");
  JoinViaInvite(a, "user_a", party_id, b, "user_b");

  chirp::party::KickMemberRequest kick;
  kick.set_user_id("user_a");
  kick.set_party_id(party_id);
  kick.set_target_user_id("user_b");
  Deliver(chirp::gateway::KICK_PARTY_MEMBER_REQ, 15, kick.SerializeAsString(), a);
  chirp::party::KickMemberResponse resp;
  ASSERT_TRUE(LastOf(*a, chirp::gateway::KICK_PARTY_MEMBER_RESP, &resp));
  EXPECT_EQ(resp.code(), chirp::common::OK);

  // The party survives with just the leader.
  EXPECT_EQ(state_->parties.size(), 1u);
  EXPECT_EQ(state_->parties[party_id]->leader_id, "user_a");
  auto mine = GetMyParty(a, "user_a");
  EXPECT_TRUE(mine.in_party());
}

TEST_F(PartyServiceTest, TransferLeaderGuards) {
  auto a = Login("user_a");
  auto b = Login("user_b");
  auto c = Login("user_c");
  const auto party_id = CreateParty(a, "user_a");
  JoinViaInvite(a, "user_a", party_id, b, "user_b");
  JoinViaInvite(a, "user_a", party_id, c, "user_c");

  chirp::party::TransferLeaderRequest req;
  req.set_party_id(party_id);

  // Non-leader: AUTH_FAILED.
  req.set_user_id("user_b");
  req.set_target_user_id("user_c");
  Deliver(chirp::gateway::TRANSFER_LEADER_REQ, 1, req.SerializeAsString(), b);
  chirp::party::TransferLeaderResponse resp;
  ASSERT_TRUE(LastBody(*b, &resp));
  EXPECT_EQ(resp.code(), chirp::common::AUTH_FAILED);

  // Non-member target: USER_NOT_FOUND.
  req.set_user_id("user_a");
  req.set_target_user_id("user_x");
  Deliver(chirp::gateway::TRANSFER_LEADER_REQ, 2, req.SerializeAsString(), a);
  ASSERT_TRUE(LastBody(*a, &resp));
  EXPECT_EQ(resp.code(), chirp::common::USER_NOT_FOUND);

  // Handing leadership to yourself: INVALID_PARAM.
  req.set_target_user_id("user_a");
  Deliver(chirp::gateway::TRANSFER_LEADER_REQ, 3, req.SerializeAsString(), a);
  ASSERT_TRUE(LastBody(*a, &resp));
  EXPECT_EQ(resp.code(), chirp::common::INVALID_PARAM);
  EXPECT_EQ(state_->parties[party_id]->leader_id, "user_a");
}

TEST_F(PartyServiceTest, TransferSnapshotReachesAllIncludingActor) {
  auto a = Login("user_a");
  auto b = Login("user_b");
  const auto party_id = CreateParty(a, "user_a");
  JoinViaInvite(a, "user_a", party_id, b, "user_b");

  chirp::party::TransferLeaderRequest req;
  req.set_user_id("user_a");
  req.set_party_id(party_id);
  req.set_target_user_id("user_b");
  Deliver(chirp::gateway::TRANSFER_LEADER_REQ, 17, req.SerializeAsString(), a);
  chirp::party::TransferLeaderResponse resp;
  ASSERT_TRUE(LastOf(*a, chirp::gateway::TRANSFER_LEADER_RESP, &resp));
  EXPECT_EQ(resp.code(), chirp::common::OK);
  EXPECT_EQ(resp.leader_id(), "user_b");

  // Snapshot sync reaches EVERY member including the actor (deliberate
  // departure from voice's actor-excluded convention).
  for (auto* member : {&a, &b}) {
    std::string body;
    ASSERT_TRUE(LastNotifyOf(**member, chirp::gateway::PARTY_STATE_CHANGED_NOTIFY, &body));
    chirp::party::PartyStateChangedNotify notify;
    ASSERT_TRUE(notify.ParseFromString(body));
    EXPECT_EQ(notify.party().leader_id(), "user_b");
  }
}

TEST_F(PartyServiceTest, NewLeaderWieldsKick) {
  auto a = Login("user_a");
  auto b = Login("user_b");
  const auto party_id = CreateParty(a, "user_a");
  JoinViaInvite(a, "user_a", party_id, b, "user_b");

  chirp::party::TransferLeaderRequest req;
  req.set_user_id("user_a");
  req.set_party_id(party_id);
  req.set_target_user_id("user_b");
  Deliver(chirp::gateway::TRANSFER_LEADER_REQ, 17, req.SerializeAsString(), a);

  // The NEW leader can now kick the old one.
  chirp::party::KickMemberRequest kick;
  kick.set_user_id("user_b");
  kick.set_party_id(party_id);
  kick.set_target_user_id("user_a");
  Deliver(chirp::gateway::KICK_PARTY_MEMBER_REQ, 18, kick.SerializeAsString(), b);
  chirp::party::KickMemberResponse resp;
  ASSERT_TRUE(LastBody(*b, &resp));
  EXPECT_EQ(resp.code(), chirp::common::OK);
  EXPECT_EQ(state_->parties[party_id]->members.size(), 1u);
}

TEST_F(PartyServiceTest, ReadyTogglesAndBroadcastsIncludingActor) {
  auto a = Login("user_a");
  auto b = Login("user_b");
  const auto party_id = CreateParty(a, "user_a");
  JoinViaInvite(a, "user_a", party_id, b, "user_b");

  chirp::party::SetReadyRequest req;
  req.set_user_id("user_b");
  req.set_party_id(party_id);
  req.set_ready(true);
  Deliver(chirp::gateway::SET_READY_REQ, 19, req.SerializeAsString(), b);
  chirp::party::SetReadyResponse resp;
  ASSERT_TRUE(LastBody(*b, &resp));
  EXPECT_EQ(resp.code(), chirp::common::OK);

  // Both members (the actor included) hold the same fresh snapshot.
  for (auto* member : {&a, &b}) {
    std::string body;
    ASSERT_TRUE(LastNotifyOf(**member, chirp::gateway::PARTY_STATE_CHANGED_NOTIFY, &body));
    chirp::party::PartyStateChangedNotify notify;
    ASSERT_TRUE(notify.ParseFromString(body));
    bool b_ready = false;
    for (const auto& m : notify.party().members()) {
      if (m.user_id() == "user_b") {
        b_ready = m.ready();
      }
    }
    EXPECT_TRUE(b_ready);
  }
}

TEST_F(PartyServiceTest, ReadyOutsidePartyRejected) {
  auto a = Login("user_a");
  auto b = Login("user_b");
  auto d = Login("user_d");
  const auto party1 = CreateParty(a, "user_a");
  JoinViaInvite(a, "user_a", party1, b, "user_b");
  const auto party2 = CreateParty(d, "user_d");

  // b is in party1: touching party2 is a mismatch (INVALID_PARAM), an unknown
  // party id stays USER_NOT_FOUND for someone in no party at all.
  chirp::party::SetReadyRequest req;
  req.set_user_id("user_b");
  req.set_party_id(party2);
  Deliver(chirp::gateway::SET_READY_REQ, 1, req.SerializeAsString(), b);
  chirp::party::SetReadyResponse resp;
  ASSERT_TRUE(LastBody(*b, &resp));
  EXPECT_EQ(resp.code(), chirp::common::INVALID_PARAM);

  chirp::party::SetReadyRequest lonely;
  lonely.set_user_id("user_x");
  lonely.set_party_id("party_nope");
  (void)party2;
  Deliver(chirp::gateway::SET_READY_REQ, 2, lonely.SerializeAsString(), b);
  ASSERT_TRUE(LastBody(*b, &resp));
  EXPECT_EQ(resp.code(), chirp::common::INVALID_PARAM);  // x isn't the caller

  chirp::party::SetReadyRequest own;
  own.set_user_id("user_d");
  own.set_party_id("party_nope");
  Deliver(chirp::gateway::SET_READY_REQ, 3, own.SerializeAsString(), d);
  ASSERT_TRUE(LastBody(*d, &resp));
  EXPECT_EQ(resp.code(), chirp::common::USER_NOT_FOUND);
}

TEST_F(PartyServiceTest, GetMyPartyEmptyThenFull) {
  auto a = Login("user_a");

  auto empty = GetMyParty(a, "user_a");
  EXPECT_EQ(empty.code(), chirp::common::OK);
  EXPECT_FALSE(empty.in_party());

  const auto party_id = CreateParty(a, "user_a");
  auto full = GetMyParty(a, "user_a");
  EXPECT_EQ(full.code(), chirp::common::OK);
  EXPECT_TRUE(full.in_party());
  EXPECT_EQ(full.party().party_id(), party_id);
  EXPECT_EQ(full.party().leader_id(), "user_a");
  EXPECT_EQ(full.party().members_size(), 1);
}

// ---------------------------------------------------------------------------
// Multi-device / disconnect
// ---------------------------------------------------------------------------

TEST_F(PartyServiceTest, NonLastDeviceDisconnectStays) {
  auto a = Login("user_a");
  auto b1 = Login("user_b", "phone");
  auto b2 = Login("user_b", "desktop");
  const auto party_id = CreateParty(a, "user_a");
  JoinViaInvite(a, "user_a", party_id, b1, "user_b");

  // The FIRST of b's two devices goes away: b is still connected elsewhere,
  // so no one leaves and nobody is notified.
  HandleDisconnect(state_, redis_, b1);

  EXPECT_EQ(CountNotify(*a, chirp::gateway::PARTY_LEFT_NOTIFY), 0);
  ASSERT_EQ(state_->parties.count(party_id), 1u);
  EXPECT_EQ(state_->parties[party_id]->members.size(), 2u);
  EXPECT_EQ(state_->user_to_party["user_b"], party_id);
}

TEST_F(PartyServiceTest, LastDeviceDisconnectLeavesAsOffline) {
  auto a = Login("user_a");
  auto b1 = Login("user_b", "phone");
  auto b2 = Login("user_b", "desktop");
  const auto party_id = CreateParty(a, "user_a");
  JoinViaInvite(a, "user_a", party_id, b1, "user_b");

  HandleDisconnect(state_, redis_, b1);
  HandleDisconnect(state_, redis_, b2);

  // The LAST device leaving drops the user from the party, with LEFT
  // carrying reason "offline" on each of a's devices.
  std::string body;
  ASSERT_TRUE(LastNotifyOf(*a, chirp::gateway::PARTY_LEFT_NOTIFY, &body));
  chirp::party::PartyLeftNotify notify;
  ASSERT_TRUE(notify.ParseFromString(body));
  EXPECT_EQ(notify.user_id(), "user_b");
  EXPECT_EQ(notify.reason(), "offline");
  EXPECT_EQ(state_->parties[party_id]->members.size(), 1u);
}

TEST_F(PartyServiceTest, LeaderDisconnectSucceeds) {
  auto a = Login("user_a");
  auto b = Login("user_b");
  const auto party_id = CreateParty(a, "user_a");
  JoinViaInvite(a, "user_a", party_id, b, "user_b");

  HandleDisconnect(state_, redis_, a);

  // Same succession semantics as an explicit leave.
  std::string body;
  ASSERT_TRUE(LastNotifyOf(*b, chirp::gateway::PARTY_STATE_CHANGED_NOTIFY, &body));
  chirp::party::PartyStateChangedNotify notify;
  ASSERT_TRUE(notify.ParseFromString(body));
  EXPECT_EQ(notify.party().leader_id(), "user_b");
  EXPECT_EQ(state_->parties[party_id]->leader_id, "user_b");
}

TEST_F(PartyServiceTest, LastMemberDisconnectDisbandsSilently) {
  auto a = Login("user_a");
  const auto party_id = CreateParty(a, "user_a");

  HandleDisconnect(state_, redis_, a);

  EXPECT_EQ(state_->parties.size(), 0u);
  EXPECT_EQ(state_->user_to_party.size(), 0u);
}

TEST_F(PartyServiceTest, StrangerDisconnectHarmless) {
  auto a = Login("user_a");
  const auto party_id = CreateParty(a, "user_a");

  // A session that never logged in (or has no party) leaves no trace.
  auto stranger = std::make_shared<MockSession>();
  HandleDisconnect(state_, redis_, stranger);

  EXPECT_EQ(state_->parties.size(), 1u);
  EXPECT_EQ(CountNotify(*a, chirp::gateway::PARTY_LEFT_NOTIFY), 0);
}

TEST_F(PartyServiceTest, JoinerAcceptReachesEachDeviceOnce) {
  auto a1 = Login("user_a", "phone");
  auto a2 = Login("user_a", "desktop");
  auto b = Login("user_b");
  const auto party_id = CreateParty(a1, "user_a");
  const auto invite_id = Invite(a1, "user_a", party_id, "user_b");

  ASSERT_EQ(Accept(b, "user_b", invite_id).code(), chirp::common::OK);

  // Each of the remaining member's devices gets exactly one JOINED and one
  // STATE_CHANGED for the join.
  for (auto* device : {&a1, &a2}) {
    EXPECT_EQ(CountNotify(**device, chirp::gateway::PARTY_JOINED_NOTIFY), 1);
    EXPECT_EQ(CountNotify(**device, chirp::gateway::PARTY_STATE_CHANGED_NOTIFY), 1);
    std::string body;
    ASSERT_TRUE(LastNotifyOf(**device, chirp::gateway::PARTY_JOINED_NOTIFY, &body));
    chirp::party::PartyJoinedNotify notify;
    ASSERT_TRUE(notify.ParseFromString(body));
    EXPECT_EQ(notify.member().user_id(), "user_b");
  }
}

// ---------------------------------------------------------------------------
// Redis write-through + restore
// ---------------------------------------------------------------------------

class PartyRedisTest : public PartyServiceTest {
 protected:
  void SetUp() override {
    PartyServiceTest::SetUp();
    mem_ = std::make_unique<chirp_test::InMemoryRedis>();
    fake_ = std::make_unique<chirp_test::FakeRedisServer>(
        [this](const std::vector<std::string>& args) { return mem_->Handle(args); });
    redis_ = std::make_shared<chirp::network::RedisClient>("127.0.0.1", fake_->port());
  }

  void TearDown() override {
    redis_.reset();
    fake_.reset();
    mem_.reset();
    PartyServiceTest::TearDown();
  }

  std::unique_ptr<chirp_test::InMemoryRedis> mem_;
  std::unique_ptr<chirp_test::FakeRedisServer> fake_;
};

TEST_F(PartyRedisTest, AcceptPersistsSnapshot) {
  auto a = Login("user_a");
  auto b = Login("user_b");
  const auto party_id = CreateParty(a, "user_a");
  JoinViaInvite(a, "user_a", party_id, b, "user_b");

  chirp::party::StoredParty stored;
  ASSERT_TRUE(stored.ParseFromString(mem_->GetDirect("chirp:party:party:" + party_id)));
  EXPECT_EQ(stored.party_id(), party_id);
  EXPECT_EQ(stored.leader_id(), "user_a");
  ASSERT_EQ(stored.members_size(), 2);
  EXPECT_EQ(stored.members(1).user_id(), "user_b");
  EXPECT_FALSE(stored.members(1).ready());
}

TEST_F(PartyRedisTest, ReadyAndTransferUpdateSnapshot) {
  auto a = Login("user_a");
  auto b = Login("user_b");
  const auto party_id = CreateParty(a, "user_a");
  JoinViaInvite(a, "user_a", party_id, b, "user_b");

  chirp::party::SetReadyRequest ready;
  ready.set_user_id("user_b");
  ready.set_party_id(party_id);
  ready.set_ready(true);
  Deliver(chirp::gateway::SET_READY_REQ, 1, ready.SerializeAsString(), b);

  chirp::party::TransferLeaderRequest transfer;
  transfer.set_user_id("user_a");
  transfer.set_party_id(party_id);
  transfer.set_target_user_id("user_b");
  Deliver(chirp::gateway::TRANSFER_LEADER_REQ, 2, transfer.SerializeAsString(), a);

  chirp::party::StoredParty stored;
  ASSERT_TRUE(stored.ParseFromString(mem_->GetDirect("chirp:party:party:" + party_id)));
  EXPECT_EQ(stored.leader_id(), "user_b");
  ASSERT_EQ(stored.members_size(), 2);
  bool b_ready = false;
  for (const auto& m : stored.members()) {
    if (m.user_id() == "user_b") {
      b_ready = m.ready();
    }
  }
  EXPECT_TRUE(b_ready);
}

TEST_F(PartyRedisTest, LoadRestoresPartiesAndUserIndex) {
  auto a = Login("user_a");
  auto b = Login("user_b");
  auto c = Login("user_c");
  const auto party1 = CreateParty(a, "user_a");
  JoinViaInvite(a, "user_a", party1, b, "user_b");
  const auto party2 = CreateParty(c, "user_c");

  // A fresh state restores both parties (and the derived user index), while
  // nothing else survives: invites are memory-only.
  auto restored = std::make_shared<PartyState>();
  ASSERT_TRUE(LoadPartyState(redis_, restored));

  ASSERT_EQ(restored->parties.size(), 2u);
  ASSERT_EQ(restored->parties.count(party1), 1u);
  ASSERT_EQ(restored->parties.count(party2), 1u);
  EXPECT_EQ(restored->parties[party1]->leader_id, "user_a");
  EXPECT_EQ(restored->parties[party1]->members.size(), 2u);
  EXPECT_EQ(restored->user_to_party["user_a"], party1);
  EXPECT_EQ(restored->user_to_party["user_b"], party1);
  EXPECT_EQ(restored->user_to_party["user_c"], party2);
  EXPECT_TRUE(restored->invites.empty());
}

TEST_F(PartyRedisTest, DisbandDeletesKey) {
  auto a = Login("user_a");
  const auto party_id = CreateParty(a, "user_a");
  ASSERT_FALSE(mem_->GetDirect("chirp:party:party:" + party_id).empty());

  chirp::party::DisbandPartyRequest disband;
  disband.set_user_id("user_a");
  disband.set_party_id(party_id);
  Deliver(chirp::gateway::DISBAND_PARTY_REQ, 1, disband.SerializeAsString(), a);

  EXPECT_TRUE(mem_->GetDirect("chirp:party:party:" + party_id).empty());
}

TEST_F(PartyRedisTest, UnreachableRedisServesInMemory) {
  auto dead = std::make_shared<chirp::network::RedisClient>("127.0.0.1", static_cast<uint16_t>(1));
  auto fresh = std::make_shared<PartyState>();
  EXPECT_FALSE(LoadPartyState(dead, fresh));  // pure in-memory mode

  // The whole lifecycle still works without Redis backing.
  chirp::auth::LoginRequest login;
  login.set_token("user_a");
  HandlePacket(fresh, dead, nullptr, session_,
               MakePacket(chirp::gateway::LOGIN_REQ, 1, login.SerializeAsString()).SerializeAsString());
  chirp::auth::LoginResponse login_resp;
  ASSERT_TRUE(LastBody(*session_, &login_resp));
  ASSERT_EQ(login_resp.code(), chirp::common::OK);

  chirp::party::CreatePartyRequest create;
  create.set_user_id("user_a");
  HandlePacket(fresh, dead, nullptr, session_,
               MakePacket(chirp::gateway::CREATE_PARTY_REQ, 2, create.SerializeAsString())
                   .SerializeAsString());
  chirp::party::CreatePartyResponse create_resp;
  ASSERT_TRUE(LastBody(*session_, &create_resp));
  EXPECT_EQ(create_resp.code(), chirp::common::OK);
  EXPECT_EQ(fresh->parties.size(), 1u);
}

TEST_F(PartyRedisTest, CorruptAndEmptySnapshotsSkipped) {
  auto a = Login("user_a");
  const auto party_id = CreateParty(a, "user_a");

  // A garbage value and an empty-member snapshot both get dropped on load.
  mem_->SetDirect("chirp:party:party:zz_garbage", "not-a-protobuf");
  chirp::party::StoredParty hollow;
  hollow.set_party_id("zz_hollow");
  mem_->SetDirect("chirp:party:party:zz_hollow", hollow.SerializeAsString());

  auto restored = std::make_shared<PartyState>();
  ASSERT_TRUE(LoadPartyState(redis_, restored));
  ASSERT_EQ(restored->parties.size(), 1u);
  EXPECT_EQ(restored->parties.count(party_id), 1u);
  EXPECT_EQ(restored->parties.count("zz_hollow"), 0u);
}

} // namespace
