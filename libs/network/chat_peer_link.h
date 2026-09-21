#ifndef CHIRP_NETWORK_CHAT_PEER_LINK_H_
#define CHIRP_NETWORK_CHAT_PEER_LINK_H_

#include <array>
#include <cstdint>
#include <functional>
#include <memory>
#include <string>
#include <vector>

#include <asio.hpp>

#include "proto/chat.pb.h"
#include "proto/common.pb.h"
#include "proto/gateway.pb.h"

namespace chirp::network {

// The spoke side of the chat peer registration protocol (docs/architecture.md
// 对等注册协议): a game-plane chirp_chat dials the hub (the app-plane
// chirp_chat), sends PEER_REGISTER_REQ once per connection, and on success
// relays channel messages up (CHANNEL_MESSAGE_NOTIFY) and consumes injected
// player replies (PEER_INJECT_MESSAGE_NOTIFY). The hub assigns the heartbeat
// cadence in the register response; any other traffic keeps the connection
// alive the same way.
//
// Mirrors ServerGatewayPeer: every entry point posts onto one strand, all
// socket and timer handlers run there, and the class keeps itself alive
// through enable_shared_from_this. Reconnects with a fixed delay until
// stopped; while disconnected the uplink sends are dropped (the game plane
// keeps running - bridging is best-effort by design).
class ChatPeerLink : public std::enable_shared_from_this<ChatPeerLink> {
 public:
  using RegisteredHandler = std::function<void(
      int32_t negotiated_version,
      const std::vector<chirp::gateway::PeerCapability>& features)>;
  using ChannelMessageHandler =
      std::function<void(const chirp::gateway::ChannelMessageNotify&)>;
  using InjectHandler =
      std::function<void(const chirp::gateway::PeerInjectMessageNotify&)>;
  using LostHandler = std::function<void()>;

  struct Options {
    std::string host;
    uint16_t port = 8200;
    std::string service_id = "game_chat";
    std::string secret;
    int32_t protocol_version = 1;
    // Namespace for this peer's channels on the hub side; the hub rejects
    // ids containing ':' because it prefixes "<game_id>:" itself.
    std::string game_id;
    std::vector<chirp::gateway::PeerCapability> supported_features;
    int reconnect_delay_seconds = 3;
    // Fallback cadence until the hub assigns one in PEER_REGISTER_RESP.
    int heartbeat_interval_seconds = 30;
  };

  static std::shared_ptr<ChatPeerLink> Create(asio::io_context& io, Options options,
                                              RegisteredHandler on_registered,
                                              ChannelMessageHandler on_channel_message,
                                              InjectHandler on_inject,
                                              LostHandler on_lost = nullptr);
  ~ChatPeerLink();

  void Start();
  void Stop();

  // Channel message uplink. Call from the link's io_context thread (the chat
  // main loop) only: the fast-path registered_ check reads strand-owned state
  // without a lock, which is exactly race-free on the single-threaded chat io.
  // The actual send posts onto the strand. Returns false when the link is not
  // registered - nothing is queued, the caller treats bridging as best-effort.
  bool SendChannelMessage(const chirp::gateway::ChannelMessageNotify& notify);
  bool SendInject(const chirp::gateway::PeerInjectMessageNotify& notify);

  // Same threading contract as the Send calls above: io_context thread only.
  bool registered() const { return registered_; }

 private:
  struct PrivateTag { explicit PrivateTag() = default; };
  ChatPeerLink(asio::io_context& io, Options options, RegisteredHandler on_registered,
               ChannelMessageHandler on_channel_message, InjectHandler on_inject,
               LostHandler on_lost, PrivateTag);

  void DoConnect();
  void SendRegister();
  void ReadHeader();
  void ReadBody(uint32_t size);
  void HandlePacket(const chirp::gateway::Packet& pkt);
  void SendPacket(chirp::gateway::MsgID msg_id, int64_t seq,
                  const google::protobuf::Message& body);
  void SendRawPacket(chirp::gateway::MsgID msg_id, int64_t seq, const std::string& body);
  void SendHeartbeat();
  void ArmHeartbeat();
  void OnConnectionLost();
  void ScheduleReconnect();

  asio::io_context& io_;
  Options options_;
  RegisteredHandler on_registered_;
  ChannelMessageHandler on_channel_message_;
  InjectHandler on_inject_;
  LostHandler on_lost_;
  // Serializes every entry point and every socket/timer handler; also keeps
  // the implicit is_open() checks inside async ops from racing a close().
  asio::strand<asio::any_io_executor> strand_;
  asio::ip::tcp::socket socket_;
  asio::steady_timer timer_;  // reconnect delay and heartbeat, one at a time
  std::array<uint8_t, 4> header_{};
  std::string body_;
  bool registered_ = false;
  bool stopping_ = false;
  int64_t heartbeat_seq_ = 0;
  int64_t uplink_seq_ = 0;
  int heartbeat_interval_seconds_ = 30;  // reassigned by the hub on register
};

}  // namespace chirp::network

#endif  // CHIRP_NETWORK_CHAT_PEER_LINK_H_
