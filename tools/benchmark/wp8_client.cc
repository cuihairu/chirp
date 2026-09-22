// WP-8 player self-service probe: exercises the app edge's server-plane
// forwarding chain end to end — login, channel subscribe (idempotent),
// unread summary, mark-read, unsubscribe, subscription listing. Every step
// must answer code=0 or the tool exits non-zero, so a smoke script can gate
// on the exit code alone.
//
// The subscription requests deliberately leave player_id empty: the edge
// must pin it to the login identity ("clients may only act as themselves"),
// and the hub rejects an empty player with INVALID_PARAM — an OK answer
// therefore proves the pin happened on the way through.

#include <chrono>
#include <cstdint>
#include <cstdlib>
#include <iostream>
#include <string>
#include <thread>

#include <asio.hpp>

#include "network/byte_order.h"
#include "network/protobuf_framing.h"
#include "proto/auth.pb.h"
#include "proto/game_server_gateway.pb.h"
#include "proto/gateway.pb.h"

namespace {

std::string GetArg(int argc, char** argv, const std::string& key, const std::string& def) {
  for (int i = 1; i < argc; i++) {
    if (argv[i] == key && i + 1 < argc) {
      return argv[i + 1];
    }
  }
  return def;
}

bool ReadFrame(asio::ip::tcp::socket& sock, std::string* payload) {
  uint8_t len_be[4];
  asio::error_code ec;
  asio::read(sock, asio::buffer(len_be, 4), ec);
  if (ec) {
    return false;
  }
  const uint32_t len = chirp::network::ReadU32BE(len_be);
  payload->resize(len);
  asio::read(sock, asio::buffer(payload->data(), payload->size()), ec);
  return !ec;
}

bool SendAndRead(asio::ip::tcp::socket& sock,
                 chirp::gateway::MsgID msg_id,
                 int64_t seq,
                 const std::string& body,
                 chirp::gateway::Packet* out_pkt) {
  chirp::gateway::Packet pkt;
  pkt.set_msg_id(msg_id);
  pkt.set_sequence(seq);
  pkt.set_body(body);
  auto out = chirp::network::ProtobufFraming::Encode(pkt);
  asio::write(sock, asio::buffer(out));

  // Skip frames whose sequence does not match (server pushes carry their
  // own sequence numbers).
  for (;;) {
    std::string payload;
    if (!ReadFrame(sock, &payload)) {
      return false;
    }
    if (!out_pkt->ParseFromArray(payload.data(), static_cast<int>(payload.size()))) {
      return false;
    }
    if (out_pkt->sequence() == seq) {
      return true;
    }
  }
}

[[noreturn]] void Fail(const std::string& step, const std::string& detail) {
  std::cerr << "wp8_client failed at " << step << ": " << detail << "\n";
  std::exit(1);
}

// The edge answers SERVER_UNAVAILABLE while the sg peer connection is still
// coming up: ServerGatewayPeer dials at startup and fails fast whenever the
// connection is down ("the caller retries on its own schedule"). A fast
// machine can reach the first WP-8 request inside the peer's reconnect
// backoff, so the probe retries exactly that code, bounded — a persistently
// broken chain still fails the run.
constexpr int kMaxUnavailableRetries = 20; // 20 * 500ms = 10s cap

template <typename Resp>
Resp RpcWithRetry(asio::ip::tcp::socket& sock,
                  chirp::gateway::MsgID req_id,
                  const std::string& body,
                  int64_t* seq,
                  const char* step) {
  for (int attempt = 0;; attempt++) {
    chirp::gateway::Packet pkt;
    if (!SendAndRead(sock, req_id, (*seq)++, body, &pkt)) {
      Fail(step, "no response");
    }
    Resp resp;
    if (!resp.ParseFromString(pkt.body())) {
      Fail(step, "unparseable response");
    }
    if (resp.code() == chirp::common::SERVER_UNAVAILABLE &&
        attempt < kMaxUnavailableRetries) {
      std::this_thread::sleep_for(std::chrono::milliseconds(500));
      continue;
    }
    return resp;
  }
}

} // namespace

int main(int argc, char** argv) {
  const std::string host = GetArg(argc, argv, "--host", "127.0.0.1");
  const uint16_t port = static_cast<uint16_t>(std::atoi(GetArg(argc, argv, "--port", "7100").c_str()));
  const std::string user = GetArg(argc, argv, "--user", "wp8_user");
  const std::string game = GetArg(argc, argv, "--game", "game_alpha");
  const std::string channel = GetArg(argc, argv, "--channel", "world");

  asio::io_context io;
  asio::ip::tcp::resolver resolver(io);
  asio::ip::tcp::socket sock(io);
  asio::connect(sock, resolver.resolve(host, std::to_string(port)));

  int64_t seq = 1;
  chirp::gateway::Packet pkt;

  // 1. Login (scaffold or auth-backed depending on the edge's --auth_host).
  {
    chirp::auth::LoginRequest req;
    req.set_token(user);
    req.set_device_id("bench");
    req.set_platform("pc");
    if (!SendAndRead(sock, chirp::gateway::LOGIN_REQ, seq++, req.SerializeAsString(), &pkt)) {
      Fail("login", "no LOGIN_RESP");
    }
    chirp::auth::LoginResponse resp;
    if (!resp.ParseFromString(pkt.body())) {
      Fail("login", "unparseable LOGIN_RESP");
    }
    if (resp.code() != chirp::common::OK) {
      Fail("login", "code=" + std::to_string(resp.code()));
    }
    std::cout << "login code=0 user=" << (resp.user_id().empty() ? user : resp.user_id()) << "\n";
  }

  // 2. Subscribe (server mints the subscription id).
  std::string sub_id;
  {
    chirp::game_server_gateway::SubscribePlayerChannelRequest req;
    req.set_game_id(game);
    req.set_channel_id(channel);
    const auto resp = RpcWithRetry<chirp::game_server_gateway::SubscribePlayerChannelResponse>(
        sock, chirp::gateway::SUBSCRIBE_PLAYER_CHANNEL_REQ, req.SerializeAsString(), &seq,
        "subscribe");
    if (resp.code() != chirp::common::OK) {
      Fail("subscribe", "code=" + std::to_string(resp.code()) +
                            " (empty player_id must be pinned by the edge)");
    }
    if (resp.subscription_id().empty()) {
      Fail("subscribe", "empty subscription_id (server should mint one)");
    }
    sub_id = resp.subscription_id();
    std::cout << "subscribe code=0 sub=" << sub_id << " existed=" << resp.existed() << "\n";
  }

  // 3. Re-subscribe the same tuple: idempotent echo of the same id.
  {
    chirp::game_server_gateway::SubscribePlayerChannelRequest req;
    req.set_game_id(game);
    req.set_channel_id(channel);
    const auto resp = RpcWithRetry<chirp::game_server_gateway::SubscribePlayerChannelResponse>(
        sock, chirp::gateway::SUBSCRIBE_PLAYER_CHANNEL_REQ, req.SerializeAsString(), &seq,
        "resubscribe");
    if (resp.code() != chirp::common::OK || !resp.existed() || resp.subscription_id() != sub_id) {
      Fail("resubscribe", "code=" + std::to_string(resp.code()) +
                              " existed=" + std::to_string(resp.existed()) +
                              " sub=" + resp.subscription_id());
    }
    std::cout << "resubscribe code=0 existed=1 sub=" << resp.subscription_id() << "\n";
  }

  // 4. Unread summary (empty ledger is fine — the point is the round trip).
  {
    chirp::game_server_gateway::GetUnreadSummaryRequest req;
    const auto resp = RpcWithRetry<chirp::game_server_gateway::GetUnreadSummaryResponse>(
        sock, chirp::gateway::GET_UNREAD_SUMMARY_REQ, req.SerializeAsString(), &seq,
        "unread_summary");
    if (resp.code() != chirp::common::OK) {
      Fail("unread_summary", "code=" + std::to_string(resp.code()));
    }
    std::cout << "unread_summary code=0 total=" << resp.total_unread() << "\n";
  }

  // 5. Mark everything read (idempotent; 0 cleared for an empty ledger).
  {
    chirp::game_server_gateway::MarkChannelsReadRequest req;
    const auto resp = RpcWithRetry<chirp::game_server_gateway::MarkChannelsReadResponse>(
        sock, chirp::gateway::MARK_CHANNELS_READ_REQ, req.SerializeAsString(), &seq,
        "mark_read");
    if (resp.code() != chirp::common::OK) {
      Fail("mark_read", "code=" + std::to_string(resp.code()));
    }
    std::cout << "mark_read code=0 cleared=" << resp.cleared() << "\n";
  }

  // 6. Summary returns to zero after the clear.
  {
    chirp::game_server_gateway::GetUnreadSummaryRequest req;
    const auto resp = RpcWithRetry<chirp::game_server_gateway::GetUnreadSummaryResponse>(
        sock, chirp::gateway::GET_UNREAD_SUMMARY_REQ, req.SerializeAsString(), &seq,
        "unread_summary_after");
    if (resp.code() != chirp::common::OK || resp.total_unread() != 0) {
      Fail("unread_summary_after", "code=" + std::to_string(resp.code()) +
                                       " total=" + std::to_string(resp.total_unread()));
    }
    std::cout << "unread_summary_after code=0 total=0\n";
  }

  // 7. Unsubscribe by the triple.
  {
    chirp::game_server_gateway::UnsubscribePlayerChannelRequest req;
    req.set_game_id(game);
    req.set_channel_id(channel);
    const auto resp = RpcWithRetry<chirp::game_server_gateway::UnsubscribePlayerChannelResponse>(
        sock, chirp::gateway::UNSUBSCRIBE_PLAYER_CHANNEL_REQ, req.SerializeAsString(), &seq,
        "unsubscribe");
    if (resp.code() != chirp::common::OK) {
      Fail("unsubscribe", "code=" + std::to_string(resp.code()));
    }
    std::cout << "unsubscribe code=0\n";
  }

  // 8. Listing no longer contains the channel.
  {
    chirp::game_server_gateway::GetPlayerSubscriptionsRequest req;
    req.set_game_id(game);
    const auto resp = RpcWithRetry<chirp::game_server_gateway::GetPlayerSubscriptionsResponse>(
        sock, chirp::gateway::GET_PLAYER_SUBSCRIPTIONS_REQ, req.SerializeAsString(), &seq,
        "list");
    if (resp.code() != chirp::common::OK) {
      Fail("list", "code=" + std::to_string(resp.code()));
    }
    for (const auto& sub : resp.subscriptions()) {
      if (sub.channel_id() == channel) {
        Fail("list", "channel still listed after unsubscribe");
      }
    }
    std::cout << "list code=0 count=" << resp.subscriptions_size() << "\n";
  }

  std::cout << "wp8 self-service chain ok (user=" << user << " game=" << game
            << " channel=" << channel << ")\n";
  return 0;
}
