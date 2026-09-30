// Unit tests for the social service packet handlers (services/social/src/main.cc).
// main.cc is included with main() renamed; handlers are driven directly with
// in-memory MockSessions, so the friend/pending/blocked tables, presence
// fan-out and the Redis write-through snapshots are all pinned without
// sockets (Redis goes through the loopback FakeRedisServer when needed).

#include <gtest/gtest.h>

#include <map>
#include <memory>
#include <optional>
#include <string>
#include <utility>
#include <vector>

#include "common/jwt.h"
#include "fake_servers.h"
#include "in_memory_redis.h"
#include "proto/auth.pb.h"
#include "proto/common.pb.h"
#include "proto/gateway.pb.h"
#include "proto/game_server_gateway.pb.h"
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
  std::shared_ptr<MockSession> Login(const std::string& user_id, const std::string& device = "",
                                     const std::string& platform = "") {
    auto s = std::make_shared<MockSession>();
    chirp::auth::LoginRequest req;
    req.set_token(user_id);
    req.set_device_id(device);
    req.set_platform(platform);
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

TEST_F(SocialServiceTest, DifferentPlatformsCoexist) {
  // 多端在线：跨 platform 共存（同 device 不同 platform 也互不干扰）。
  auto phone = Login("user_a", "phone", "ios");
  auto desktop = Login("user_a", "desktop", "web");
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
  auto a1 = Login("user_a", "phone", "ios");
  auto a2 = Login("user_a", "desktop", "web");
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


// ---------------------------------------------------------------------------
// 游戏在线状态 overlay (chat chirp:game_presence:events -> friends)
// ---------------------------------------------------------------------------

namespace {

std::string PresenceEventBody(const std::string& player, const std::string& game, bool online) {
  chirp::game_server_gateway::GamePresenceEvent event;
  event.set_player_id(player);
  event.set_game_id(game);
  event.set_online(online);
  return event.SerializeAsString();
}

constexpr const char* kEventsChannel = "chirp:game_presence:events";

}  // namespace

TEST_F(SocialServiceTest, GameAssertionPresentsInGameWithoutAnySession) {
  // The overlay's whole point: a player can be in-game with no companion
  // app connected. Base OFFLINE + a live game assertion => IN_GAME.
  ConsumeGamePresenceEvent(state_, kEventsChannel, PresenceEventBody("user_b", "g1", true));

  const auto info = EffectivePresence(state_, "user_b");
  EXPECT_EQ(info.user_id(), "user_b");
  EXPECT_EQ(info.status(), chirp::social::IN_GAME);
  EXPECT_EQ(info.status_message(), "g1");
  ASSERT_EQ(info.metadata().count("g1"), 1);
  EXPECT_EQ(info.metadata().at("g1"), "1");
}

TEST_F(SocialServiceTest, ReleasedGameAssertionFallsBackToBase) {
  auto b = Login("user_b");

  ConsumeGamePresenceEvent(state_, kEventsChannel, PresenceEventBody("user_b", "g1", true));
  EXPECT_EQ(EffectivePresence(state_, "user_b").status(), chirp::social::IN_GAME);

  // 退出游戏(断言失效)→ 状态自然下线:回到基础的 ONLINE。
  ConsumeGamePresenceEvent(state_, kEventsChannel, PresenceEventBody("user_b", "g1", false));
  const auto info = EffectivePresence(state_, "user_b");
  EXPECT_EQ(info.status(), chirp::social::ONLINE);
  EXPECT_TRUE(info.status_message().empty());
  EXPECT_EQ(state_->game_presence.count("user_b"), 0u);
  (void)b;
}

TEST_F(SocialServiceTest, OverlaySurvivesSocialLogout) {
  // Two lifecycles: disconnecting the companion app does not leave the game.
  auto b = Login("user_b");
  ConsumeGamePresenceEvent(state_, kEventsChannel, PresenceEventBody("user_b", "g1", true));

  HandleDisconnect(state_, nullptr, b);

  const auto info = EffectivePresence(state_, "user_b");
  EXPECT_EQ(info.status(), chirp::social::IN_GAME);
  EXPECT_EQ(info.status_message(), "g1");
}

TEST_F(SocialServiceTest, MultipleGamesJoinSortedAndExitOneAtATime) {
  ConsumeGamePresenceEvent(state_, kEventsChannel, PresenceEventBody("user_b", "g2", true));
  ConsumeGamePresenceEvent(state_, kEventsChannel, PresenceEventBody("user_b", "g1", true));

  auto info = EffectivePresence(state_, "user_b");
  EXPECT_EQ(info.status(), chirp::social::IN_GAME);
  EXPECT_EQ(info.status_message(), "g1,g2");  // set order, deterministic
  EXPECT_EQ(info.metadata().size(), 2u);

  ConsumeGamePresenceEvent(state_, kEventsChannel, PresenceEventBody("user_b", "g2", false));
  info = EffectivePresence(state_, "user_b");
  EXPECT_EQ(info.status(), chirp::social::IN_GAME);
  EXPECT_EQ(info.status_message(), "g1");
  EXPECT_EQ(info.metadata().count("g2"), 0);

  ConsumeGamePresenceEvent(state_, kEventsChannel, PresenceEventBody("user_b", "g1", false));
  EXPECT_EQ(EffectivePresence(state_, "user_b").status(), chirp::social::OFFLINE);
  EXPECT_EQ(state_->game_presence.count("user_b"), 0u);
}

TEST_F(SocialServiceTest, PresenceEventBroadcastsMergedViewToFriends) {
  auto a = Login("user_a");
  Login("user_b");
  state_->friends["user_a"].insert("user_b");
  state_->friends["user_b"].insert("user_a");

  ConsumeGamePresenceEvent(state_, kEventsChannel, PresenceEventBody("user_b", "g1", true));

  ASSERT_EQ(CountNotify(*a, chirp::gateway::PRESENCE_NOTIFY), 1);
  chirp::social::PresenceNotify notify;
  ASSERT_TRUE(notify.ParseFromString(ReceivedPackets(*a).back().body()));
  EXPECT_EQ(notify.user_id(), "user_b");
  EXPECT_EQ(notify.status(), chirp::social::IN_GAME);
  EXPECT_EQ(notify.status_message(), "g1");

  ConsumeGamePresenceEvent(state_, kEventsChannel, PresenceEventBody("user_b", "g1", false));
  ASSERT_EQ(CountNotify(*a, chirp::gateway::PRESENCE_NOTIFY), 2);
  ASSERT_TRUE(notify.ParseFromString(ReceivedPackets(*a).back().body()));
  EXPECT_EQ(notify.status(), chirp::social::ONLINE);  // back to the base view
}

TEST_F(SocialServiceTest, DisconnectWithLiveGameAssertionNotifiesInGame) {
  auto a = Login("user_a");
  auto b = Login("user_b");
  state_->friends["user_a"].insert("user_b");
  state_->friends["user_b"].insert("user_a");
  ConsumeGamePresenceEvent(state_, kEventsChannel, PresenceEventBody("user_b", "g1", true));
  const size_t before = ReceivedPackets(*a).size();

  // The friend's app session drops, but they are still in-game: the merged
  // broadcast must say IN_GAME, not OFFLINE.
  HandleDisconnect(state_, nullptr, b);

  ASSERT_EQ(ReceivedPackets(*a).size(), before + 1u);
  chirp::social::PresenceNotify notify;
  EXPECT_EQ(ReceivedPackets(*a).back().msg_id(), chirp::gateway::PRESENCE_NOTIFY);
  ASSERT_TRUE(notify.ParseFromString(ReceivedPackets(*a).back().body()));
  EXPECT_EQ(notify.user_id(), "user_b");
  EXPECT_EQ(notify.status(), chirp::social::IN_GAME);
}

TEST_F(SocialServiceTest, LoginBroadcastUsesTheMergedView) {
  auto a = Login("user_a");
  state_->friends["user_a"].insert("user_b");
  state_->friends["user_b"].insert("user_a");
  ConsumeGamePresenceEvent(state_, kEventsChannel, PresenceEventBody("user_b", "g7", true));

  // user_b comes back online on the app while still in game g7: friends see
  // the more specific IN_GAME truth, with the game id attached. (The event
  // above already pushed one notify to a; the login pushes the merged one.)
  auto b = Login("user_b");
  ASSERT_EQ(CountNotify(*a, chirp::gateway::PRESENCE_NOTIFY), 2);
  chirp::social::PresenceNotify notify;
  ASSERT_TRUE(notify.ParseFromString(ReceivedPackets(*a).back().body()));
  EXPECT_EQ(notify.status(), chirp::social::IN_GAME);
  EXPECT_EQ(notify.status_message(), "g7");
  (void)b;
}

TEST_F(SocialServiceTest, GetPresenceAnswersWithTheMergedView) {
  auto a = Login("user_a");

  // Ghost with no base and no assertion: the OFFLINE placeholder.
  chirp::social::GetPresenceRequest get;
  get.add_user_ids("user_b");
  Deliver(chirp::gateway::GET_PRESENCE_REQ, 1, get.SerializeAsString(), a);
  chirp::social::GetPresenceResponse resp;
  ASSERT_TRUE(LastBody(*a, &resp));
  ASSERT_EQ(resp.presences_size(), 1);
  EXPECT_EQ(resp.presences(0).status(), chirp::social::OFFLINE);

  ConsumeGamePresenceEvent(state_, kEventsChannel, PresenceEventBody("user_b", "g1", true));

  get.Clear();
  get.add_user_ids("user_b");
  Deliver(chirp::gateway::GET_PRESENCE_REQ, 2, get.SerializeAsString(), a);
  ASSERT_TRUE(LastBody(*a, &resp));
  ASSERT_EQ(resp.presences_size(), 1);
  EXPECT_EQ(resp.presences(0).status(), chirp::social::IN_GAME);
  EXPECT_EQ(resp.presences(0).status_message(), "g1");
  ASSERT_EQ(resp.presences(0).metadata().count("g1"), 1);
}

TEST_F(SocialServiceTest, MalformedOrOffChannelEventsAreIgnored) {
  auto a = Login("user_a");
  state_->friends["user_a"].insert("user_b");
  state_->friends["user_b"].insert("user_a");
  const size_t before = ReceivedPackets(*a).size();

  ConsumeGamePresenceEvent(state_, "some/other/channel", PresenceEventBody("user_b", "g1", true));
  ConsumeGamePresenceEvent(state_, kEventsChannel, "\x01\x02not-a-proto");
  // An event without a player/game is refused (it would poison the overlay).
  ConsumeGamePresenceEvent(state_, kEventsChannel, PresenceEventBody("", "g1", true));

  EXPECT_EQ(ReceivedPackets(*a).size(), before);
  EXPECT_EQ(state_->game_presence.count("user_b"), 0u);
  EXPECT_EQ(EffectivePresence(state_, "user_b").status(), chirp::social::OFFLINE);
}

TEST_F(SocialServiceTest, SetPresenceAfterGameAssertionStillPresentsInGame) {
  // 基础状态与游戏断言互不覆盖:SET_PRESENCE AWAY 只是 base,正在游戏中
  // 仍然是更具体的事实。
  auto b = Login("user_b");
  ConsumeGamePresenceEvent(state_, kEventsChannel, PresenceEventBody("user_b", "g1", true));

  chirp::social::SetPresenceRequest set;
  set.set_user_id("user_b");
  set.set_status(chirp::social::AWAY);
  set.set_status_message("in a meeting");
  Deliver(chirp::gateway::SET_PRESENCE_REQ, 1, set.SerializeAsString(), b);

  const auto info = EffectivePresence(state_, "user_b");
  EXPECT_EQ(info.status(), chirp::social::IN_GAME);
  EXPECT_EQ(info.status_message(), "g1");
  // The base keeps the explicit AWAY for when the game assertion goes away.
  EXPECT_EQ(state_->presence["user_b"].status(), chirp::social::AWAY);
}

// ---------------------------------------------------------------------------
// Batch 16: error/guard/idempotency arms (handler-face coverage follow-up).
// The suites above pin the happy paths and the presence overlay; these pin
// the garbage-body guards, the forged-user_id guard, the duplicate-request
// idempotency, the Redis degrade paths and the dispatch edges.
// ---------------------------------------------------------------------------

TEST_F(SocialServiceTest, ArgHelpersHitMissDanglingAndGarbage) {
  char bin[] = "bin";
  char port[] = "--port";
  char value[] = "8100";
  char dangling[] = "--dangling";
  char* argv[] = {bin, port, value, dangling};
  EXPECT_EQ(GetArg(4, argv, "--port", "8000"), "8100");
  EXPECT_EQ(GetArg(4, argv, "--missing", "8000"), "8000");
  // A key in the last slot has no value to take; the default wins.
  EXPECT_EQ(GetArg(4, argv, "--dangling", "8000"), "8000");
  EXPECT_EQ(ParseU16Arg(4, argv, "--port", 123), 8100);
  EXPECT_EQ(ParseU16Arg(4, argv, "--gone", 123), 123);
  char garbage[] = "not-a-number";
  char* argv2[] = {bin, port, garbage};
  EXPECT_EQ(ParseU16Arg(3, argv2, "--port", 123), 0);  // std::atoi -> 0
}

// A logged-in session forging req.user_id = someone else gets INVALID_PARAM
// from every guarded business op (the response is for the *authenticated*
// identity, never the claimed one).
TEST_F(SocialServiceTest, ForgedUserIdsAreRejectedAcrossBusinessOps) {
  // The session is bound to user_a; every request below claims user_b. The
  // mismatch (not the missing login) is what must reject them.
  auto a = Login("user_a");

  const auto expect_invalid_param = [&](chirp::gateway::MsgID id, const std::string& body) {
    Deliver(id, 7, body, a);
  };

  {
    chirp::social::AddFriendRequest req;
    req.set_user_id("user_b");
    req.set_target_user_id("user_c");
    expect_invalid_param(chirp::gateway::ADD_FRIEND_REQ, req.SerializeAsString());
    chirp::social::AddFriendResponse resp;
    ASSERT_TRUE(LastBody(*a, &resp));
    EXPECT_EQ(resp.code(), chirp::common::INVALID_PARAM);
    EXPECT_GT(resp.server_time(), 0);
  }
  {
    chirp::social::FriendRequestAction req;
    req.set_user_id("user_b");
    req.set_request_id("nope");
    expect_invalid_param(chirp::gateway::FRIEND_REQUEST_ACTION_REQ, req.SerializeAsString());
    chirp::social::FriendRequestActionResponse resp;
    ASSERT_TRUE(LastBody(*a, &resp));
    EXPECT_EQ(resp.code(), chirp::common::INVALID_PARAM);
  }
  {
    chirp::social::RemoveFriendRequest req;
    req.set_user_id("user_b");
    req.set_friend_user_id("user_c");
    expect_invalid_param(chirp::gateway::REMOVE_FRIEND_REQ, req.SerializeAsString());
    chirp::social::RemoveFriendResponse resp;
    ASSERT_TRUE(LastBody(*a, &resp));
    EXPECT_EQ(resp.code(), chirp::common::INVALID_PARAM);
  }
  {
    chirp::social::GetFriendListRequest req;
    req.set_user_id("user_b");
    expect_invalid_param(chirp::gateway::GET_FRIEND_LIST_REQ, req.SerializeAsString());
    chirp::social::GetFriendListResponse resp;
    ASSERT_TRUE(LastBody(*a, &resp));
    EXPECT_EQ(resp.code(), chirp::common::INVALID_PARAM);
  }
  {
    chirp::social::GetPendingRequestsRequest req;
    req.set_user_id("user_b");
    expect_invalid_param(chirp::gateway::GET_PENDING_REQUESTS_REQ, req.SerializeAsString());
    chirp::social::GetPendingRequestsResponse resp;
    ASSERT_TRUE(LastBody(*a, &resp));
    EXPECT_EQ(resp.code(), chirp::common::INVALID_PARAM);
  }
  {
    chirp::social::BlockUserRequest req;
    req.set_user_id("user_b");
    req.set_target_user_id("user_c");
    expect_invalid_param(chirp::gateway::BLOCK_USER_REQ, req.SerializeAsString());
    chirp::social::BlockUserResponse resp;
    ASSERT_TRUE(LastBody(*a, &resp));
    EXPECT_EQ(resp.code(), chirp::common::INVALID_PARAM);
  }
  {
    chirp::social::UnblockUserRequest req;
    req.set_user_id("user_b");
    req.set_target_user_id("user_c");
    expect_invalid_param(chirp::gateway::UNBLOCK_USER_REQ, req.SerializeAsString());
    chirp::social::UnblockUserResponse resp;
    ASSERT_TRUE(LastBody(*a, &resp));
    EXPECT_EQ(resp.code(), chirp::common::INVALID_PARAM);
  }
  {
    chirp::social::GetBlockedListRequest req;
    req.set_user_id("user_b");
    expect_invalid_param(chirp::gateway::GET_BLOCKED_LIST_REQ, req.SerializeAsString());
    chirp::social::GetBlockedListResponse resp;
    ASSERT_TRUE(LastBody(*a, &resp));
    EXPECT_EQ(resp.code(), chirp::common::INVALID_PARAM);
  }
  {
    chirp::social::SetPresenceRequest req;
    req.set_user_id("user_b");
    req.set_status(chirp::social::AWAY);
    expect_invalid_param(chirp::gateway::SET_PRESENCE_REQ, req.SerializeAsString());
    chirp::social::SetPresenceResponse resp;
    ASSERT_TRUE(LastBody(*a, &resp));
    EXPECT_EQ(resp.code(), chirp::common::INVALID_PARAM);
  }

  // No forge attempt left a trace in the tables.
  EXPECT_TRUE(state_->friends.empty());
  EXPECT_TRUE(state_->pending_requests.empty());
  EXPECT_TRUE(state_->blocked.empty());
}

// Every dispatched op answers garbage bodies with INVALID_PARAM (heartbeat
// stays silent) and nothing lands in the state tables.
TEST_F(SocialServiceTest, GarbageBodiesAreRejectedAcrossDispatch) {
  const std::string kJunk = "\xff\xfe not a message";

  {
    auto s = std::make_shared<MockSession>();
    Deliver(chirp::gateway::LOGIN_REQ, 1, kJunk, s);
    chirp::auth::LoginResponse resp;
    ASSERT_TRUE(LastBody(*s, &resp));
    EXPECT_EQ(resp.code(), chirp::common::INVALID_PARAM);
    EXPECT_GT(resp.server_time(), 0);
  }
  const std::vector<std::pair<chirp::gateway::MsgID, bool>> kOps = {
      {chirp::gateway::ADD_FRIEND_REQ, true},
      {chirp::gateway::FRIEND_REQUEST_ACTION_REQ, true},
      {chirp::gateway::REMOVE_FRIEND_REQ, true},
      {chirp::gateway::GET_FRIEND_LIST_REQ, true},
      {chirp::gateway::GET_PENDING_REQUESTS_REQ, true},
      {chirp::gateway::BLOCK_USER_REQ, true},
      {chirp::gateway::UNBLOCK_USER_REQ, true},
      {chirp::gateway::GET_BLOCKED_LIST_REQ, true},
      {chirp::gateway::SET_PRESENCE_REQ, true},
      {chirp::gateway::GET_PRESENCE_REQ, true},
      {chirp::gateway::HEARTBEAT_PING, false},  // heartbeat with garbage: silent
  };
  for (const auto& [id, responds] : kOps) {
    auto s = std::make_shared<MockSession>();
    Deliver(id, 2, kJunk, s);
    if (responds) {
      // Each handler answers with its own _RESP id and a non-empty body.
      const auto pkts = ReceivedPackets(*s);
      ASSERT_EQ(pkts.size(), 1u) << "msg_id=" << id;
      EXPECT_NE(pkts[0].msg_id(), id) << "msg_id=" << id;
      EXPECT_NE(pkts[0].body().size(), 0u) << "msg_id=" << id;
    } else {
      EXPECT_TRUE(s->sent.empty()) << "msg_id=" << id;
    }
  }
  EXPECT_TRUE(state_->friends.empty());
  EXPECT_TRUE(state_->pending_requests.empty());
  EXPECT_TRUE(state_->blocked.empty());
  EXPECT_TRUE(state_->presence.empty());
}

TEST_F(SocialServiceTest, ScaffoldLoginWithEmptyTokenIsRejected) {
  chirp::auth::LoginRequest req;  // token stays empty -> empty scaffold identity
  Deliver(chirp::gateway::LOGIN_REQ, 1, req.SerializeAsString());
  chirp::auth::LoginResponse resp;
  ASSERT_TRUE(LastBody(*session_, &resp));
  EXPECT_EQ(resp.code(), chirp::common::INVALID_PARAM);
  EXPECT_EQ(session_->sent.size(), 1u);  // nothing but the rejection
  EXPECT_EQ(state_->presence.size(), 0u);
}

TEST_F(SocialServiceTest, AddFriendRejectsEmptySelfAndExistingTargets) {
  auto a = Login("user_a");
  MakeFriends("user_a", "user_b");

  const auto add = [&](const std::string& target) {
    chirp::social::AddFriendRequest req;
    req.set_user_id("user_a");
    req.set_target_user_id(target);
    Deliver(chirp::gateway::ADD_FRIEND_REQ, 3, req.SerializeAsString(), a);
    chirp::social::AddFriendResponse resp;
    EXPECT_TRUE(LastBody(*a, &resp));
    return resp;
  };

  EXPECT_EQ(add("").code(), chirp::common::INVALID_PARAM);        // empty target
  EXPECT_EQ(add("user_a").code(), chirp::common::INVALID_PARAM);  // self
  EXPECT_EQ(add("user_b").code(), chirp::common::INTERNAL_ERROR);  // already friends
  EXPECT_EQ(state_->pending_requests.size(), 0u);  // none of the rejects stored one
}

TEST_F(SocialServiceTest, DuplicateAddFriendReturnsOriginalRequest) {
  auto a = Login("user_a");
  auto b = Login("user_b");

  chirp::social::AddFriendRequest req;
  req.set_user_id("user_a");
  req.set_target_user_id("user_b");
  Deliver(chirp::gateway::ADD_FRIEND_REQ, 1, req.SerializeAsString(), a);
  chirp::social::AddFriendResponse first;
  ASSERT_TRUE(LastBody(*a, &first));
  ASSERT_EQ(first.code(), chirp::common::OK);
  ASSERT_FALSE(first.request_id().empty());

  // The identical open request is idempotent: same id, no second entry.
  Deliver(chirp::gateway::ADD_FRIEND_REQ, 2, req.SerializeAsString(), a);
  chirp::social::AddFriendResponse second;
  ASSERT_TRUE(LastBody(*a, &second));
  EXPECT_EQ(second.code(), chirp::common::OK);
  EXPECT_EQ(second.request_id(), first.request_id());
  EXPECT_EQ(state_->pending_requests.size(), 1u);
  // The duplicate re-sends the request notify to the target (the handler
  // notifies after the dedup lookup, on both paths).
  EXPECT_EQ(CountNotify(*b, chirp::gateway::FRIEND_REQUEST_NOTIFY), 2);
}

// A handshake in both directions settles both records: accepting A->B also
// drops B->A (the mirror), while unrelated requests are left alone.
TEST_F(SocialServiceTest, AcceptingRequestAlsoSettlesTheReverseMirror) {
  auto a = Login("user_a");
  auto b = Login("user_b");

  chirp::social::AddFriendRequest add;
  add.set_user_id("user_a");
  add.set_target_user_id("user_b");
  Deliver(chirp::gateway::ADD_FRIEND_REQ, 1, add.SerializeAsString(), a);
  chirp::social::AddFriendResponse forward;
  ASSERT_TRUE(LastBody(*a, &forward));

  add.set_user_id("user_b");
  add.set_target_user_id("user_a");
  Deliver(chirp::gateway::ADD_FRIEND_REQ, 2, add.SerializeAsString(), b);
  chirp::social::AddFriendResponse reverse;
  ASSERT_TRUE(LastBody(*b, &reverse));
  ASSERT_NE(forward.request_id(), reverse.request_id());

  // An unrelated third request must survive the mirror sweep.
  auto c = Login("user_c");
  auto d = Login("user_d");
  add.set_user_id("user_c");
  add.set_target_user_id("user_d");
  Deliver(chirp::gateway::ADD_FRIEND_REQ, 3, add.SerializeAsString(), c);
  chirp::social::AddFriendResponse third;
  ASSERT_TRUE(LastBody(*c, &third));

  chirp::social::FriendRequestAction action;
  action.set_user_id("user_b");
  action.set_request_id(forward.request_id());
  action.set_accept(true);
  Deliver(chirp::gateway::FRIEND_REQUEST_ACTION_REQ, 4, action.SerializeAsString(), b);
  chirp::social::FriendRequestActionResponse resp;
  ASSERT_TRUE(LastBody(*b, &resp));
  EXPECT_EQ(resp.code(), chirp::common::OK);

  EXPECT_EQ(state_->friends["user_a"].count("user_b"), 1u);
  EXPECT_EQ(state_->friends["user_b"].count("user_a"), 1u);
  ASSERT_EQ(state_->pending_requests.size(), 1u);  // only the unrelated c->d
  EXPECT_EQ(state_->pending_requests.count(third.request_id()), 1u);
}

TEST_F(SocialServiceTest, RemoveFriendRejectsEmptyAndSelfTargets) {
  auto a = Login("user_a");

  const auto remove = [&](const std::string& target) {
    chirp::social::RemoveFriendRequest req;
    req.set_user_id("user_a");
    req.set_friend_user_id(target);
    Deliver(chirp::gateway::REMOVE_FRIEND_REQ, 1, req.SerializeAsString(), a);
    chirp::social::RemoveFriendResponse resp;
    EXPECT_TRUE(LastBody(*a, &resp));
    return resp;
  };

  EXPECT_EQ(remove("").code(), chirp::common::INVALID_PARAM);
  EXPECT_EQ(remove("user_a").code(), chirp::common::INVALID_PARAM);
}

TEST_F(SocialServiceTest, BlockUserRejectsEmptyAndSelfTargets) {
  auto a = Login("user_a");

  const auto block = [&](const std::string& target) {
    chirp::social::BlockUserRequest req;
    req.set_user_id("user_a");
    req.set_target_user_id(target);
    Deliver(chirp::gateway::BLOCK_USER_REQ, 1, req.SerializeAsString(), a);
    chirp::social::BlockUserResponse resp;
    EXPECT_TRUE(LastBody(*a, &resp));
    return resp;
  };

  EXPECT_EQ(block("").code(), chirp::common::INVALID_PARAM);
  EXPECT_EQ(block("user_a").code(), chirp::common::INVALID_PARAM);
  EXPECT_EQ(state_->blocked.size(), 0u);
}

// Unblocking one entry keeps the others blocked - and the Redis snapshot
// keeps them too (an empty snapshot would delete the whole list).
TEST_F(SocialRedisTest, UnblockKeepsRemainingBlockedEntriesAndSnapshot) {
  auto a = Login("user_a");

  for (const auto& target : {"user_b", "user_c"}) {
    chirp::social::BlockUserRequest block;
    block.set_user_id("user_a");
    block.set_target_user_id(target);
    Deliver(chirp::gateway::BLOCK_USER_REQ, 1, block.SerializeAsString(), a);
    chirp::social::BlockUserResponse resp;
    ASSERT_TRUE(LastBody(*a, &resp));
    ASSERT_EQ(resp.code(), chirp::common::OK);
  }

  // Empty target is rejected before anything is unblocked (unlike the other
  // handlers, unblock has no self-target arm - only the empty check).
  {
    chirp::social::UnblockUserRequest empty;
    empty.set_user_id("user_a");
    Deliver(chirp::gateway::UNBLOCK_USER_REQ, 2, empty.SerializeAsString(), a);
    chirp::social::UnblockUserResponse resp;
    ASSERT_TRUE(LastBody(*a, &resp));
    EXPECT_EQ(resp.code(), chirp::common::INVALID_PARAM);
    EXPECT_EQ(state_->blocked["user_a"].size(), 2u);  // untouched
  }

  chirp::social::UnblockUserRequest unblock;
  unblock.set_user_id("user_a");
  unblock.set_target_user_id("user_b");
  Deliver(chirp::gateway::UNBLOCK_USER_REQ, 2, unblock.SerializeAsString(), a);
  chirp::social::UnblockUserResponse resp;
  ASSERT_TRUE(LastBody(*a, &resp));
  EXPECT_EQ(resp.code(), chirp::common::OK);

  chirp::social::GetBlockedListRequest list;
  list.set_user_id("user_a");
  Deliver(chirp::gateway::GET_BLOCKED_LIST_REQ, 3, list.SerializeAsString(), a);
  chirp::social::GetBlockedListResponse list_resp;
  ASSERT_TRUE(LastBody(*a, &list_resp));
  ASSERT_EQ(list_resp.code(), chirp::common::OK);
  ASSERT_EQ(list_resp.blocked_user_ids_size(), 1);
  EXPECT_EQ(list_resp.blocked_user_ids(0), "user_c");

  chirp::social::StoredBlockedList stored;
  ASSERT_TRUE(stored.ParseFromString(mem_->GetDirect("chirp:social:blocked:user_a")));
  ASSERT_EQ(stored.blocked_user_ids_size(), 1);
  EXPECT_EQ(stored.blocked_user_ids(0), "user_c");
}

TEST_F(SocialServiceTest, SetPresenceRejectsInvalidStatusEnum) {
  auto a = Login("user_a");

  chirp::social::SetPresenceRequest req;
  req.set_user_id("user_a");
  req.set_status(static_cast<chirp::social::PresenceStatus>(999));
  Deliver(chirp::gateway::SET_PRESENCE_REQ, 1, req.SerializeAsString(), a);
  chirp::social::SetPresenceResponse resp;
  ASSERT_TRUE(LastBody(*a, &resp));
  EXPECT_EQ(resp.code(), chirp::common::INVALID_PARAM);
  // The rejected status never touched the stored presence.
  EXPECT_NE(state_->presence["user_a"].status(), 999);
}

TEST_F(SocialServiceTest, GetPresenceWithoutLoginReturnsAuthFailed) {
  chirp::social::GetPresenceRequest req;
  req.add_user_ids("user_a");
  Deliver(chirp::gateway::GET_PRESENCE_REQ, 1, req.SerializeAsString());
  chirp::social::GetPresenceResponse resp;
  ASSERT_TRUE(LastBody(*session_, &resp));
  EXPECT_EQ(resp.code(), chirp::common::AUTH_FAILED);
  EXPECT_EQ(resp.presences_size(), 0);
}

// Free-form metadata rides along both the in-memory presence and the Redis
// snapshot (the merged view itself stays covered by the overlay suites).
TEST_F(SocialRedisTest, SetPresenceMetadataLandsInStateAndSnapshot) {
  auto a = Login("user_a");

  chirp::social::SetPresenceRequest set;
  set.set_user_id("user_a");
  set.set_status(chirp::social::AWAY);
  set.set_status_message("brb");
  (*set.mutable_metadata())["mood"] = "focused";
  Deliver(chirp::gateway::SET_PRESENCE_REQ, 1, set.SerializeAsString(), a);
  chirp::social::SetPresenceResponse resp;
  ASSERT_TRUE(LastBody(*a, &resp));
  EXPECT_EQ(resp.code(), chirp::common::OK);

  {
    std::lock_guard<std::mutex> lock(state_->mu);
    const auto& info = state_->presence["user_a"];
    ASSERT_EQ(info.metadata().count("mood"), 1);
    EXPECT_EQ(info.metadata().at("mood"), "focused");
  }

  chirp::social::PresenceNotify stored;
  ASSERT_TRUE(stored.ParseFromString(mem_->GetDirect("chirp:social:presence:user_a")));
  EXPECT_EQ(stored.status(), chirp::social::AWAY);
  ASSERT_EQ(stored.metadata().count("mood"), 1);
  EXPECT_EQ(stored.metadata().at("mood"), "focused");
}

// The last device going offline writes the offline snapshot to Redis (the
// offline fan-out is covered by the presence suites with a null redis).
TEST_F(SocialRedisTest, LastDeviceDisconnectWritesOfflineSnapshotToRedis) {
  auto a = Login("user_a");
  HandleDisconnect(state_, redis_, a);

  chirp::social::PresenceNotify stored;
  ASSERT_TRUE(stored.ParseFromString(mem_->GetDirect("chirp:social:presence:user_a")));
  EXPECT_EQ(stored.status(), chirp::social::OFFLINE);
  EXPECT_EQ(stored.user_id(), "user_a");
}

TEST_F(SocialServiceTest, DispatchToleratesGarbagePayloadAndUnknownMsgId) {
  HandlePacket(state_, nullptr, nullptr, session_, std::string("\xff\xfe not a packet"));
  EXPECT_TRUE(session_->sent.empty());

  Packet pkt;
  pkt.set_msg_id(static_cast<chirp::gateway::MsgID>(9999));
  pkt.set_sequence(1);
  HandlePacket(state_, nullptr, nullptr, session_, pkt.SerializeAsString());
  EXPECT_TRUE(session_->sent.empty());
  EXPECT_FALSE(session_->closed);
}

TEST_F(SocialServiceTest, HeartbeatEchoesTimestampAndToleratesGarbageBody) {
  chirp::gateway::HeartbeatPing ping;
  ping.set_timestamp(1234);
  Deliver(chirp::gateway::HEARTBEAT_PING, 5, ping.SerializeAsString());
  chirp::gateway::HeartbeatPong pong;
  ASSERT_TRUE(LastBody(*session_, &pong));
  EXPECT_EQ(pong.timestamp(), 1234);
  EXPECT_GT(pong.server_time(), 0);

  const size_t sent_before = session_->sent.size();
  Deliver(chirp::gateway::HEARTBEAT_PING, 6, std::string("\xff junk"));
  EXPECT_EQ(session_->sent.size(), sent_before);  // garbage ping: silent
}

// Redis write-through is best-effort: a failing Set is logged, never thrown.
class FailSetRedis : public chirp::network::RedisClient {
 public:
  FailSetRedis() : RedisClient("127.0.0.1", 1) {}
  bool Set(const std::string& /*key*/, const std::string& /*value*/) override {
    ++set_calls;
    return false;
  }
  int set_calls = 0;
};

TEST_F(SocialServiceTest, PersistFailuresAreLoggedNotThrown) {
  auto redis = std::make_shared<FailSetRedis>();
  PersistFriends(redis, "user_a", {"user_b"});
  PersistPending(redis, "user_a", {});
  PersistBlocked(redis, "user_a", {"user_b"});
  EXPECT_EQ(redis->set_calls, 3);
}

// Scripted Redis double: no sockets, PING answers, Keys/Get serve a fixture.
class ScriptedRedis : public chirp::network::RedisClient {
 public:
  ScriptedRedis() : RedisClient("127.0.0.1", 1) {}

  std::optional<chirp::network::RedisResp> Command(
      const std::vector<std::string>& args) override {
    if (!args.empty() && args[0] == "PING") {
      chirp::network::RedisResp resp;
      resp.type = chirp::network::RedisResp::Type::kSimpleString;
      resp.str = "PONG";
      return resp;
    }
    return std::nullopt;
  }
  std::vector<std::string> Keys(const std::string& pattern) override {
    auto it = keys_by_pattern.find(pattern);
    return it == keys_by_pattern.end() ? std::vector<std::string>{} : it->second;
  }
  std::optional<std::string> Get(const std::string& key) override {
    auto it = values.find(key);
    return it == values.end() ? std::nullopt : std::make_optional(it->second);
  }

  std::map<std::string, std::vector<std::string>> keys_by_pattern;
  std::map<std::string, std::string> values;
};

TEST_F(SocialServiceTest, LoadWithoutRedisReturnsFalse) {
  auto fresh = std::make_shared<SocialState>();
  EXPECT_FALSE(LoadSocialState(nullptr, fresh));  // pure in-memory mode
}

// A corrupt snapshot (or a bare prefix key with an empty user id) is skipped
// with a warning; the valid ones still load.
TEST_F(SocialServiceTest, LoadSkipsCorruptAndBareKeySnapshots) {
  auto redis = std::make_shared<ScriptedRedis>();

  chirp::social::StoredFriendList good_friends;
  good_friends.add_friend_user_ids("user_z");

  redis->keys_by_pattern["chirp:social:friends:*"] = {
      "chirp:social:friends:",      // bare prefix: empty user id -> skipped
      "chirp:social:friends:good",  // valid
      "chirp:social:friends:bad",   // corrupt value -> skipped
  };
  redis->values["chirp:social:friends:good"] = good_friends.SerializeAsString();
  redis->values["chirp:social:friends:bad"] = "\xff\xfe junk";
  redis->keys_by_pattern["chirp:social:pending:*"] = {"chirp:social:pending:bad"};
  redis->values["chirp:social:pending:bad"] = "\xff\xfe junk";
  redis->keys_by_pattern["chirp:social:blocked:*"] = {"chirp:social:blocked:bad"};
  redis->values["chirp:social:blocked:bad"] = "\xff\xfe junk";

  auto fresh = std::make_shared<SocialState>();
  EXPECT_TRUE(LoadSocialState(redis, fresh));
  EXPECT_EQ(fresh->friends.size(), 1u);
  EXPECT_EQ(fresh->friends["good"].count("user_z"), 1u);
  EXPECT_TRUE(fresh->pending_requests.empty());
  EXPECT_TRUE(fresh->blocked.empty());
}

}  // namespace
