// Unit tests for the social service packet handlers (services/social/src/main.cc).
// main.cc is included with main() renamed; handlers are driven directly with
// in-memory MockSessions, so the friend/pending/blocked tables, presence
// fan-out and the Redis write-through snapshots are all pinned without
// sockets (Redis goes through the loopback FakeRedisServer when needed).

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
#include "proto/social.pb.h"

// Relative path, same convention as the voice service test: unambiguous even
// if another main.cc ever lands on the include path.
#define main chirp_social_main
#include "../../services/social/src/main.cc"
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

class SocialServiceTest : public ::testing::Test {
 protected:
  std::shared_ptr<SocialState> state_ = std::make_shared<SocialState>();
  std::shared_ptr<MockSession> session_ = std::make_shared<MockSession>();

  void Deliver(chirp::gateway::MsgID id, int64_t seq, const std::string& body,
               const std::shared_ptr<MockSession>& to = nullptr) {
    HandlePacket(state_, redis_, verifier_,
                 to ? to : session_, MakePacket(id, seq, body).SerializeAsString());
  }

  // Subclasses with a fake redis override this member in SetUp().
  std::shared_ptr<chirp::network::RedisClient> redis_;

  void SendPacketBody(chirp::gateway::MsgID id, int64_t seq, const std::string& body) {
    Deliver(id, seq, body);
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

  // Makes two users friends through the real protocol (add + accept).
  void MakeFriends(const std::string& a, const std::string& b) {
    auto sa = Login(a);
    auto sb = Login(b);
    chirp::social::AddFriendRequest add;
    add.set_user_id(a);
    add.set_target_user_id(b);
    Deliver(chirp::gateway::ADD_FRIEND_REQ, 2, add.SerializeAsString(), sa);
    chirp::social::AddFriendResponse add_resp;
    ASSERT_TRUE(LastBody(*sa, &add_resp));
    ASSERT_EQ(add_resp.code(), chirp::common::OK);

    chirp::social::FriendRequestAction action;
    action.set_user_id(b);
    action.set_request_id(add_resp.request_id());
    action.set_accept(true);
    Deliver(chirp::gateway::FRIEND_REQUEST_ACTION_REQ, 3, action.SerializeAsString(), sb);
    chirp::social::FriendRequestActionResponse action_resp;
    ASSERT_TRUE(LastBody(*sb, &action_resp));
    ASSERT_EQ(action_resp.code(), chirp::common::OK);
  }

  const chirp::common::LoginTokenVerifier* verifier_ = nullptr;
};

// ---------------------------------------------------------------------------
// Login
// ---------------------------------------------------------------------------

TEST_F(SocialServiceTest, ScaffoldLoginSucceedsAndBinds) {
  chirp::auth::LoginRequest req;
  req.set_token("user_a");
  SendPacketBody(chirp::gateway::LOGIN_REQ, 1, req.SerializeAsString());

  chirp::auth::LoginResponse resp;
  ASSERT_TRUE(LastBody(*session_, &resp));
  EXPECT_EQ(resp.code(), chirp::common::OK);
  EXPECT_EQ(resp.user_id(), "user_a");
  EXPECT_FALSE(resp.session_id().empty());
  // The binding powers every later business handler on this session.
  chirp::social::GetFriendListRequest list;
  list.set_user_id("user_a");
  SendPacketBody(chirp::gateway::GET_FRIEND_LIST_REQ, 2, list.SerializeAsString());
  chirp::social::GetFriendListResponse list_resp;
  ASSERT_TRUE(LastBody(*session_, &list_resp));
  EXPECT_EQ(list_resp.code(), chirp::common::OK);
}

TEST_F(SocialServiceTest, JwtTokenLogsInAsSubject) {
  const int64_t now = NowMs() / 1000;
  chirp::common::LoginTokenVerifier verifier("s3cret");
  verifier_ = &verifier;

  chirp::auth::LoginRequest req;
  req.set_token(chirp::common::JwtSignHS256("user_jwt", now, "s3cret", now + 600));
  SendPacketBody(chirp::gateway::LOGIN_REQ, 1, req.SerializeAsString());

  chirp::auth::LoginResponse resp;
  ASSERT_TRUE(LastBody(*session_, &resp));
  EXPECT_EQ(resp.code(), chirp::common::OK);
  EXPECT_EQ(resp.user_id(), "user_jwt");
}

TEST_F(SocialServiceTest, JwtLoginAcceptsSecretLongerThanHashBlock) {
  // A secret beyond the64-byte HMAC block forces the hash-then-use key path
  // in HmacSha256 (this binary links the coverage-instrumented library copy
  // of sha256.cc, where that path's branch lives).
  const int64_t now = NowMs() / 1000;
  const std::string long_secret(131, 'x');
  chirp::common::LoginTokenVerifier verifier(long_secret);
  verifier_ = &verifier;

  chirp::auth::LoginRequest req;
  req.set_token(
      chirp::common::JwtSignHS256("user_long", now, long_secret, now + 600));
  SendPacketBody(chirp::gateway::LOGIN_REQ, 1, req.SerializeAsString());

  chirp::auth::LoginResponse resp;
  ASSERT_TRUE(LastBody(*session_, &resp));
  EXPECT_EQ(resp.code(), chirp::common::OK);
  EXPECT_EQ(resp.user_id(), "user_long");
}

TEST_F(SocialServiceTest, BadJwtTokensRejectedAndUnbound) {
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
  chirp::social::GetFriendListRequest list;
  list.set_user_id("user_a");
  SendPacketBody(chirp::gateway::GET_FRIEND_LIST_REQ, 2, list.SerializeAsString());
  chirp::social::GetFriendListResponse list_resp;
  ASSERT_TRUE(LastBody(*session_, &list_resp));
  EXPECT_EQ(list_resp.code(), chirp::common::AUTH_FAILED);
}

TEST_F(SocialServiceTest, SameDeviceRebindKicksOldSession) {
  auto first = Login("user_a", "phone");
  auto second = Login("user_a", "phone");

  // The old session is told and closed; the registry keeps only the new one.
  ASSERT_EQ(CountNotify(*first, chirp::gateway::KICK_NOTIFY), 1);
  EXPECT_TRUE(first->close_after_send);
  EXPECT_EQ(chirp::network::GetUserSessions(state_->registry, "user_a").size(), 1u);
  EXPECT_EQ(chirp::network::GetUserSessions(state_->registry, "user_a")[0], second);
}

TEST_F(SocialServiceTest, DifferentDevicesCoexist) {
  auto phone = Login("user_a", "phone");
  auto desktop = Login("user_a", "desktop");
  auto sessions = chirp::network::GetUserSessions(state_->registry, "user_a");
  EXPECT_EQ(sessions.size(), 2u);
  // Neither was kicked.
  EXPECT_FALSE(phone->close_after_send);
  EXPECT_FALSE(desktop->close_after_send);
}

TEST_F(SocialServiceTest, LoginBroadcastsOnlineToFriendsOnly) {
  auto& friends = state_->friends["user_a"];
  friends.insert("user_b");  // friend
  state_->friends["user_x"]; // user_a also has x in another row? No: x is NOT a friend.

  auto b = Login("user_b");
  auto x = Login("user_x");
  (void)x;

  Login("user_a");

  // b sees the ONLINE notify, x (not a friend) does not.
  int online_at_b = 0;
  for (const auto& pkt : ReceivedPackets(*b)) {
    if (pkt.msg_id() != chirp::gateway::PRESENCE_NOTIFY) {
      continue;
    }
    chirp::social::PresenceNotify notify;
    ASSERT_TRUE(notify.ParseFromString(pkt.body()));
    if (notify.user_id() == "user_a" && notify.status() == chirp::social::ONLINE) {
      ++online_at_b;
    }
  }
  EXPECT_EQ(online_at_b, 1);
  EXPECT_EQ(CountNotify(*x, chirp::gateway::PRESENCE_NOTIFY), 0);
}

// ---------------------------------------------------------------------------
// Friends
// ---------------------------------------------------------------------------

TEST_F(SocialServiceTest, AddFriendNotifiesTargetAndReturnsId) {
  auto a = Login("user_a");
  auto b = Login("user_b");

  chirp::social::AddFriendRequest add;
  add.set_user_id("user_a");
  add.set_target_user_id("user_b");
  add.set_message("hi");
  Deliver(chirp::gateway::ADD_FRIEND_REQ, 2, add.SerializeAsString(), a);

  chirp::social::AddFriendResponse resp;
  ASSERT_TRUE(LastBody(*a, &resp));
  ASSERT_EQ(resp.code(), chirp::common::OK);
  EXPECT_FALSE(resp.request_id().empty());

  ASSERT_EQ(CountNotify(*b, chirp::gateway::FRIEND_REQUEST_NOTIFY), 1);
  Packet pkt = ReceivedPackets(*b).back();
  chirp::social::FriendRequestNotify notify;
  ASSERT_TRUE(notify.ParseFromString(pkt.body()));
  EXPECT_EQ(notify.request_id(), resp.request_id());
  EXPECT_EQ(notify.from_user_id(), "user_a");
  EXPECT_EQ(notify.message(), "hi");
}

TEST_F(SocialServiceTest, AcceptMakesMutualFriendsAndNotifiesBothWithPeerId) {
  auto a = Login("user_a");
  auto b = Login("user_b");

  chirp::social::AddFriendRequest add;
  add.set_user_id("user_a");
  add.set_target_user_id("user_b");
  Deliver(chirp::gateway::ADD_FRIEND_REQ, 2, add.SerializeAsString(), a);
  chirp::social::AddFriendResponse add_resp;
  ASSERT_TRUE(LastBody(*a, &add_resp));

  chirp::social::FriendRequestAction action;
  action.set_user_id("user_b");
  action.set_request_id(add_resp.request_id());
  action.set_accept(true);
  Deliver(chirp::gateway::FRIEND_REQUEST_ACTION_REQ, 3, action.SerializeAsString(), b);

  // Both directions in the friend table.
  EXPECT_EQ(state_->friends["user_a"].count("user_b"), 1u);
  EXPECT_EQ(state_->friends["user_b"].count("user_a"), 1u);

  // Both sides get ACCEPTED with the OTHER party's id (never their own -
  // that is the self-echo bug this pins against).
  chirp::social::FriendAcceptedNotify to_a, to_b;
  std::string body;
  ASSERT_EQ(CountNotify(*a, chirp::gateway::FRIEND_ACCEPTED_NOTIFY), 1);
  ASSERT_TRUE(LastNotifyOf(*a, chirp::gateway::FRIEND_ACCEPTED_NOTIFY, &body));
  ASSERT_TRUE(to_a.ParseFromString(body));
  EXPECT_EQ(to_a.user_id(), "user_b");
  ASSERT_EQ(CountNotify(*b, chirp::gateway::FRIEND_ACCEPTED_NOTIFY), 1);
  ASSERT_TRUE(LastNotifyOf(*b, chirp::gateway::FRIEND_ACCEPTED_NOTIFY, &body));
  ASSERT_TRUE(to_b.ParseFromString(body));
  EXPECT_EQ(to_b.user_id(), "user_a");
}

TEST_F(SocialServiceTest, DeclineLeavesNoFriendshipAndNoNotify) {
  auto a = Login("user_a");
  auto b = Login("user_b");

  chirp::social::AddFriendRequest add;
  add.set_user_id("user_a");
  add.set_target_user_id("user_b");
  Deliver(chirp::gateway::ADD_FRIEND_REQ, 2, add.SerializeAsString(), a);
  chirp::social::AddFriendResponse add_resp;
  ASSERT_TRUE(LastBody(*a, &add_resp));

  chirp::social::FriendRequestAction action;
  action.set_user_id("user_b");
  action.set_request_id(add_resp.request_id());
  action.set_accept(false);
  Deliver(chirp::gateway::FRIEND_REQUEST_ACTION_REQ, 3, action.SerializeAsString(), b);

  chirp::social::FriendRequestActionResponse resp;
  ASSERT_TRUE(LastBody(*b, &resp));
  EXPECT_EQ(resp.code(), chirp::common::OK);

  EXPECT_TRUE(state_->friends["user_a"].empty());
  EXPECT_TRUE(state_->friends["user_b"].empty());
  EXPECT_TRUE(state_->pending_requests.empty());
  EXPECT_EQ(CountNotify(*a, chirp::gateway::FRIEND_ACCEPTED_NOTIFY), 0);
}

TEST_F(SocialServiceTest, UnknownOrForeignRequestActionRejected) {
  auto a = Login("user_a");
  auto b = Login("user_b");

  chirp::social::AddFriendRequest add;
  add.set_user_id("user_a");
  add.set_target_user_id("user_b");
  Deliver(chirp::gateway::ADD_FRIEND_REQ, 2, add.SerializeAsString(), a);
  chirp::social::AddFriendResponse add_resp;
  ASSERT_TRUE(LastBody(*a, &add_resp));

  // Unknown request id.
  chirp::social::FriendRequestAction unknown;
  unknown.set_user_id("user_b");
  unknown.set_request_id("does-not-exist");
  unknown.set_accept(true);
  Deliver(chirp::gateway::FRIEND_REQUEST_ACTION_REQ, 3, unknown.SerializeAsString(), b);
  chirp::social::FriendRequestActionResponse resp;
  ASSERT_TRUE(LastBody(*b, &resp));
  EXPECT_EQ(resp.code(), chirp::common::INVALID_PARAM);

  // The requester (not the receiver) may not settle their own request.
  chirp::social::FriendRequestAction foreign;
  foreign.set_user_id("user_a");
  foreign.set_request_id(add_resp.request_id());
  foreign.set_accept(true);
  Deliver(chirp::gateway::FRIEND_REQUEST_ACTION_REQ, 4, foreign.SerializeAsString(), a);
  ASSERT_TRUE(LastBody(*a, &resp));
  EXPECT_EQ(resp.code(), chirp::common::INVALID_PARAM);
  // Nothing was mutated by either rejected action.
  EXPECT_EQ(state_->pending_requests.count(add_resp.request_id()), 1u);
}

TEST_F(SocialServiceTest, RemoveNotifiesPeerAndRepeatsIdempotently) {
  auto a = Login("user_a");
  auto b = Login("user_b");
  (void)a;
  MakeFriends("user_a", "user_b");
  // Fresh sessions to observe notifications cleanly.
  auto a2 = Login("user_a");
  auto b2 = Login("user_b");
  // Same-device rebinds kicked the older sessions; use the latest ones.

  chirp::social::RemoveFriendRequest rm;
  rm.set_user_id("user_a");
  rm.set_friend_user_id("user_b");
  Deliver(chirp::gateway::REMOVE_FRIEND_REQ, 5, rm.SerializeAsString(), a2);
  chirp::social::RemoveFriendResponse resp;
  ASSERT_TRUE(LastBody(*a2, &resp));
  EXPECT_EQ(resp.code(), chirp::common::OK);

  EXPECT_TRUE(state_->friends["user_a"].empty());
  EXPECT_TRUE(state_->friends["user_b"].empty());
  ASSERT_EQ(CountNotify(*b2, chirp::gateway::FRIEND_REMOVED_NOTIFY), 1);
  chirp::social::FriendRemovedNotify notify;
  ASSERT_TRUE(notify.ParseFromString(ReceivedPackets(*b2).back().body()));
  EXPECT_EQ(notify.user_id(), "user_a");

  // Removing again (no friendship left) still succeeds.
  Deliver(chirp::gateway::REMOVE_FRIEND_REQ, 6, rm.SerializeAsString(), a2);
  chirp::social::RemoveFriendResponse resp2;
  ASSERT_TRUE(LastBody(*a2, &resp2));
  EXPECT_EQ(resp2.code(), chirp::common::OK);
}

TEST_F(SocialServiceTest, GetFriendListPagination) {
  auto a = Login("user_a");
  auto& ids = state_->friends["user_a"];
  ids.insert("f1");
  ids.insert("f2");
  ids.insert("f3");
  ids.insert("f4");
  ids.insert("f5");

  chirp::social::GetFriendListRequest list;
  list.set_user_id("user_a");

  list.set_limit(2);
  list.set_offset(0);
  Deliver(chirp::gateway::GET_FRIEND_LIST_REQ, 1, list.SerializeAsString(), a);
  chirp::social::GetFriendListResponse resp;
  ASSERT_TRUE(LastBody(*a, &resp));
  EXPECT_EQ(resp.code(), chirp::common::OK);
  EXPECT_EQ(resp.total_count(), 5);
  EXPECT_EQ(resp.friends_size(), 2);
  EXPECT_EQ(resp.friends(0).user_id(), "f1");  // sorted order
  EXPECT_EQ(resp.friends(1).user_id(), "f2");
  EXPECT_EQ(resp.friends(0).status(), chirp::social::ACCEPTED);

  list.set_offset(4);
  Deliver(chirp::gateway::GET_FRIEND_LIST_REQ, 2, list.SerializeAsString(), a);
  ASSERT_TRUE(LastBody(*a, &resp));
  EXPECT_EQ(resp.friends_size(), 1);
  EXPECT_EQ(resp.friends(0).user_id(), "f5");

  list.set_limit(0);  // no limit
  list.set_offset(0);
  Deliver(chirp::gateway::GET_FRIEND_LIST_REQ, 3, list.SerializeAsString(), a);
  ASSERT_TRUE(LastBody(*a, &resp));
  EXPECT_EQ(resp.friends_size(), 5);
}

// ---------------------------------------------------------------------------
// Pending & blocked interactions
// ---------------------------------------------------------------------------

TEST_F(SocialServiceTest, GetPendingReturnsIncomingOnly) {
  auto a = Login("user_a");
  auto b = Login("user_b");
  auto c = Login("user_c");

  chirp::social::AddFriendRequest add;
  add.set_user_id("user_a");
  add.set_target_user_id("user_b");
  Deliver(chirp::gateway::ADD_FRIEND_REQ, 1, add.SerializeAsString(), a);
  add.set_user_id("user_c");
  Deliver(chirp::gateway::ADD_FRIEND_REQ, 2, add.SerializeAsString(), c);

  chirp::social::GetPendingRequestsRequest pending;
  pending.set_user_id("user_b");
  Deliver(chirp::gateway::GET_PENDING_REQUESTS_REQ, 3, pending.SerializeAsString(), b);
  chirp::social::GetPendingRequestsResponse resp;
  ASSERT_TRUE(LastBody(*b, &resp));
  EXPECT_EQ(resp.code(), chirp::common::OK);
  EXPECT_EQ(resp.requests_size(), 2);

  // The requesters see no incoming side of their own requests.
  pending.set_user_id("user_a");
  Deliver(chirp::gateway::GET_PENDING_REQUESTS_REQ, 4, pending.SerializeAsString(), a);
  ASSERT_TRUE(LastBody(*a, &resp));
  EXPECT_EQ(resp.requests_size(), 0);
}

TEST_F(SocialServiceTest, AddFriendRejectedWhenTargetBlocked) {
  auto a = Login("user_a");
  auto b = Login("user_b");

  chirp::social::BlockUserRequest block;
  block.set_user_id("user_b");
  block.set_target_user_id("user_a");
  Deliver(chirp::gateway::BLOCK_USER_REQ, 1, block.SerializeAsString(), b);

  chirp::social::AddFriendRequest add;
  add.set_user_id("user_a");
  add.set_target_user_id("user_b");
  Deliver(chirp::gateway::ADD_FRIEND_REQ, 2, add.SerializeAsString(), a);
  chirp::social::AddFriendResponse resp;
  ASSERT_TRUE(LastBody(*a, &resp));
  EXPECT_EQ(resp.code(), chirp::common::INTERNAL_ERROR);
  EXPECT_TRUE(state_->pending_requests.empty());
}

// ---------------------------------------------------------------------------
// Block
// ---------------------------------------------------------------------------

TEST_F(SocialServiceTest, BlockSeversFriendshipAndNotifiesPeer) {
  auto a = Login("user_a");
  auto b = Login("user_b");
  MakeFriends("user_a", "user_b");
  auto a2 = Login("user_a");
  auto b2 = Login("user_b");

  chirp::social::BlockUserRequest block;
  block.set_user_id("user_a");
  block.set_target_user_id("user_b");
  Deliver(chirp::gateway::BLOCK_USER_REQ, 1, block.SerializeAsString(), a2);
  chirp::social::BlockUserResponse resp;
  ASSERT_TRUE(LastBody(*a2, &resp));
  EXPECT_EQ(resp.code(), chirp::common::OK);

  EXPECT_EQ(state_->blocked["user_a"].count("user_b"), 1u);
  EXPECT_TRUE(state_->friends["user_a"].empty());
  EXPECT_TRUE(state_->friends["user_b"].empty());
  ASSERT_EQ(CountNotify(*b2, chirp::gateway::FRIEND_REMOVED_NOTIFY), 1);

  chirp::social::GetBlockedListRequest blocked_list;
  blocked_list.set_user_id("user_a");
  Deliver(chirp::gateway::GET_BLOCKED_LIST_REQ, 2, blocked_list.SerializeAsString(), a2);
  chirp::social::GetBlockedListResponse list_resp;
  ASSERT_TRUE(LastBody(*a2, &list_resp));
  ASSERT_EQ(list_resp.blocked_user_ids_size(), 1);
  EXPECT_EQ(list_resp.blocked_user_ids(0), "user_b");
}

TEST_F(SocialServiceTest, UnblockAllowsNewHandshake) {
  auto a = Login("user_a");
  auto b = Login("user_b");

  chirp::social::BlockUserRequest block;
  block.set_user_id("user_a");
  block.set_target_user_id("user_b");
  Deliver(chirp::gateway::BLOCK_USER_REQ, 1, block.SerializeAsString(), a);

  chirp::social::UnblockUserRequest unblock;
  unblock.set_user_id("user_a");
  unblock.set_target_user_id("user_b");
  Deliver(chirp::gateway::UNBLOCK_USER_REQ, 2, unblock.SerializeAsString(), a);
  chirp::social::UnblockUserResponse resp;
  ASSERT_TRUE(LastBody(*a, &resp));
  EXPECT_EQ(resp.code(), chirp::common::OK);
  EXPECT_TRUE(state_->blocked["user_a"].empty());

  // Repeated unblock stays OK (idempotent).
  Deliver(chirp::gateway::UNBLOCK_USER_REQ, 3, unblock.SerializeAsString(), a);
  ASSERT_TRUE(LastBody(*a, &resp));
  EXPECT_EQ(resp.code(), chirp::common::OK);

  // A fresh handshake now goes through.
  chirp::social::AddFriendRequest add;
  add.set_user_id("user_a");
  add.set_target_user_id("user_b");
  Deliver(chirp::gateway::ADD_FRIEND_REQ, 4, add.SerializeAsString(), a);
  chirp::social::AddFriendResponse add_resp;
  ASSERT_TRUE(LastBody(*a, &add_resp));
  EXPECT_EQ(add_resp.code(), chirp::common::OK);
}

// ---------------------------------------------------------------------------
// Presence
// ---------------------------------------------------------------------------

TEST_F(SocialServiceTest, SetPresenceBroadcastsOnlyToFriends) {
  auto b = Login("user_b");
  auto x = Login("user_x");
  auto a = Login("user_a");
  state_->friends["user_a"].insert("user_b");  // b is a friend, x is not

  chirp::social::SetPresenceRequest set;
  set.set_user_id("user_a");
  set.set_status(chirp::social::AWAY);
  set.set_status_message("in a meeting");
  Deliver(chirp::gateway::SET_PRESENCE_REQ, 1, set.SerializeAsString(), a);

  chirp::social::SetPresenceResponse resp;
  ASSERT_TRUE(LastBody(*a, &resp));
  EXPECT_EQ(resp.code(), chirp::common::OK);

  ASSERT_EQ(CountNotify(*b, chirp::gateway::PRESENCE_NOTIFY), 1);
  chirp::social::PresenceNotify notify;
  ASSERT_TRUE(notify.ParseFromString(ReceivedPackets(*b).back().body()));
  EXPECT_EQ(notify.user_id(), "user_a");
  EXPECT_EQ(notify.status(), chirp::social::AWAY);
  EXPECT_EQ(notify.status_message(), "in a meeting");
  EXPECT_EQ(CountNotify(*x, chirp::gateway::PRESENCE_NOTIFY), 0);
}

TEST_F(SocialServiceTest, GetPresenceReturnsOfflinePlaceholder) {
  auto a = Login("user_a");

  chirp::social::GetPresenceRequest get;
  get.add_user_ids("user_ghost");
  Deliver(chirp::gateway::GET_PRESENCE_REQ, 1, get.SerializeAsString(), a);
  chirp::social::GetPresenceResponse resp;
  ASSERT_TRUE(LastBody(*a, &resp));
  EXPECT_EQ(resp.code(), chirp::common::OK);
  ASSERT_EQ(resp.presences_size(), 1);
  EXPECT_EQ(resp.presences(0).user_id(), "user_ghost");
  EXPECT_EQ(resp.presences(0).status(), chirp::social::OFFLINE);
}

TEST_F(SocialServiceTest, DisconnectBroadcastsOfflineToFriends) {
  auto b = Login("user_b");
  auto a = Login("user_a");
  state_->friends["user_a"].insert("user_b");
  state_->friends["user_b"].insert("user_a");

  HandleDisconnect(state_, nullptr, a);

  ASSERT_EQ(CountNotify(*b, chirp::gateway::PRESENCE_NOTIFY), 1);
  chirp::social::PresenceNotify notify;
  ASSERT_TRUE(notify.ParseFromString(ReceivedPackets(*b).back().body()));
  EXPECT_EQ(notify.user_id(), "user_a");
  EXPECT_EQ(notify.status(), chirp::social::OFFLINE);
  EXPECT_EQ(state_->presence["user_a"].status(), chirp::social::OFFLINE);
}

TEST_F(SocialServiceTest, OneOfTwoDevicesDisconnectingStaysOnline) {
  auto b = Login("user_b");
  auto a1 = Login("user_a", "phone");
  auto a2 = Login("user_a", "desktop");
  (void)a1;
  state_->friends["user_a"].insert("user_b");
  state_->friends["user_b"].insert("user_a");

  HandleDisconnect(state_, nullptr, a2);

  // The user is still online on "phone": no OFFLINE broadcast, no flip.
  EXPECT_EQ(CountNotify(*b, chirp::gateway::PRESENCE_NOTIFY), 0);
  EXPECT_EQ(state_->presence["user_a"].status(), chirp::social::ONLINE);
}

TEST_F(SocialServiceTest, DisconnectWithoutLoginIsSilent) {
  auto b = Login("user_b");
  auto stranger = std::make_shared<MockSession>();

  HandleDisconnect(state_, nullptr, stranger);

  EXPECT_EQ(CountNotify(*b, chirp::gateway::PRESENCE_NOTIFY), 0);
  // The only presence row is the one login(b) created; the stranger left no
  // trace.
  EXPECT_EQ(state_->presence.size(), 1u);
  EXPECT_EQ(state_->presence.count("user_b"), 1u);
}

// ---------------------------------------------------------------------------
// Redis write-through + restore
// ---------------------------------------------------------------------------

class SocialRedisTest : public SocialServiceTest {
 protected:
  void SetUp() override {
    SocialServiceTest::SetUp();
    mem_ = std::make_unique<chirp_test::InMemoryRedis>();
    fake_ = std::make_unique<chirp_test::FakeRedisServer>(
        [this](const std::vector<std::string>& args) { return mem_->Handle(args); });
    redis_ = std::make_shared<chirp::network::RedisClient>("127.0.0.1", fake_->port());
  }

  void TearDown() override {
    redis_.reset();
    fake_.reset();
    mem_.reset();
    SocialServiceTest::TearDown();
  }

  std::unique_ptr<chirp_test::InMemoryRedis> mem_;
  std::unique_ptr<chirp_test::FakeRedisServer> fake_;
};

TEST_F(SocialRedisTest, AcceptPersistsFriendSnapshot) {
  auto a = Login("user_a");
  auto b = Login("user_b");

  chirp::social::AddFriendRequest add;
  add.set_user_id("user_a");
  add.set_target_user_id("user_b");
  Deliver(chirp::gateway::ADD_FRIEND_REQ, 1, add.SerializeAsString(), a);
  chirp::social::AddFriendResponse add_resp;
  ASSERT_TRUE(LastBody(*a, &add_resp));

  chirp::social::FriendRequestAction action;
  action.set_user_id("user_b");
  action.set_request_id(add_resp.request_id());
  action.set_accept(true);
  Deliver(chirp::gateway::FRIEND_REQUEST_ACTION_REQ, 2, action.SerializeAsString(), b);

  chirp::social::StoredFriendList stored;
  ASSERT_TRUE(stored.ParseFromString(mem_->GetDirect("chirp:social:friends:user_a")));
  ASSERT_EQ(stored.friend_user_ids_size(), 1);
  EXPECT_EQ(stored.friend_user_ids(0), "user_b");
}

TEST_F(SocialRedisTest, LoadRestoresStateFromSnapshots) {
  // Build state through the protocol: a<->b friends, c->d pending, c blocks e.
  MakeFriends("user_a", "user_b");
  auto c = Login("user_c");
  auto d = Login("user_d");
  chirp::social::AddFriendRequest add;
  add.set_user_id("user_c");
  add.set_target_user_id("user_d");
  Deliver(chirp::gateway::ADD_FRIEND_REQ, 1, add.SerializeAsString(), c);
  chirp::social::BlockUserRequest block;
  block.set_user_id("user_c");
  block.set_target_user_id("user_e");
  Deliver(chirp::gateway::BLOCK_USER_REQ, 2, block.SerializeAsString(), c);

  // A fresh state restores everything from the same fake redis.
  auto restored = std::make_shared<SocialState>();
  ASSERT_TRUE(LoadSocialState(redis_, restored));

  EXPECT_EQ(restored->friends["user_a"].count("user_b"), 1u);
  EXPECT_EQ(restored->friends["user_b"].count("user_a"), 1u);
  ASSERT_EQ(restored->pending_requests.size(), 1u);  // c->d stored on both sides, deduped
  EXPECT_EQ(restored->blocked["user_c"].count("user_e"), 1u);
}

TEST_F(SocialRedisTest, UnreachableRedisStillServesRequests) {
  auto dead = std::make_shared<chirp::network::RedisClient>("127.0.0.1", static_cast<uint16_t>(1));
  auto fresh = std::make_shared<SocialState>();
  EXPECT_FALSE(LoadSocialState(dead, fresh));  // pure in-memory mode

  auto a = std::make_shared<MockSession>();
  HandlePacket(fresh, dead, nullptr, a, MakePacket(chirp::gateway::LOGIN_REQ, 1, [&] {
                 chirp::auth::LoginRequest req;
                 req.set_token("user_a");
                 return req.SerializeAsString();
               }()).SerializeAsString());
  chirp::auth::LoginResponse login_resp;
  ASSERT_TRUE(LastBody(*a, &login_resp));
  ASSERT_EQ(login_resp.code(), chirp::common::OK);

  chirp::social::AddFriendRequest add;
  add.set_user_id("user_a");
  add.set_target_user_id("user_b");
  HandlePacket(fresh, dead, nullptr, a, MakePacket(chirp::gateway::ADD_FRIEND_REQ, 2,
                                                   add.SerializeAsString()).SerializeAsString());
  chirp::social::AddFriendResponse resp;
  ASSERT_TRUE(LastBody(*a, &resp));
  EXPECT_EQ(resp.code(), chirp::common::OK);
  EXPECT_EQ(fresh->pending_requests.size(), 1u);
}

TEST_F(SocialRedisTest, RemoveUpdatesSnapshot) {
  MakeFriends("user_a", "user_b");
  ASSERT_TRUE(chirp::social::StoredFriendList()
                  .ParseFromString(mem_->GetDirect("chirp:social:friends:user_a")));

  auto a = Login("user_a");
  chirp::social::RemoveFriendRequest rm;
  rm.set_user_id("user_a");
  rm.set_friend_user_id("user_b");
  Deliver(chirp::gateway::REMOVE_FRIEND_REQ, 1, rm.SerializeAsString(), a);

  chirp::social::StoredFriendList stored;
  ASSERT_TRUE(stored.ParseFromString(mem_->GetDirect("chirp:social:friends:user_a")));
  EXPECT_EQ(stored.friend_user_ids_size(), 0);
}

TEST_F(SocialRedisTest, PresenceSnapshotKeepsTtlKey) {
  auto a = Login("user_a");

  chirp::social::SetPresenceRequest set;
  set.set_user_id("user_a");
  set.set_status(chirp::social::IN_GAME);
  HandlePacket(state_, redis_, verifier_, a,
               MakePacket(chirp::gateway::SET_PRESENCE_REQ, 1, set.SerializeAsString()).SerializeAsString());

  chirp::social::PresenceNotify stored;
  ASSERT_TRUE(stored.ParseFromString(mem_->GetDirect("chirp:social:presence:user_a")));
  EXPECT_EQ(stored.user_id(), "user_a");
  EXPECT_EQ(stored.status(), chirp::social::IN_GAME);
}

}  // namespace
