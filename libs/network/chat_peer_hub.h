#ifndef CHIRP_NETWORK_CHAT_PEER_HUB_H_
#define CHIRP_NETWORK_CHAT_PEER_HUB_H_

#include <array>
#include <cstdint>
#include <functional>
#include <map>
#include <memory>
#include <string>
#include <vector>

#include <asio.hpp>

#include "proto/common.pb.h"
#include "proto/gateway.pb.h"

namespace chirp::network {

// The hub side of the chat peer registration protocol (docs/architecture.md
// 对等注册协议): accepts connections from game-plane chirp_chat spokes, checks
// each PEER_REGISTER_REQ against the whitelist and the minimum version,
// displaces a previous registration with the same service_id, and then relays
// channel messages (inbound CHANNEL_MESSAGE_NOTIFY, outbound
// PEER_INJECT_MESSAGE_NOTIFY). A connection that stays silent for twice the
// heartbeat cadence is dropped; a dropped peer must re-register.
//
// Single-owner lifecycle: the hub lives in an io_context owned by the chat
// main loop, so accept/read/timer handlers all run there without a strand.
// The Send calls are also io-context-thread entry points (same contract as
// ChatPeerLink's fast path). Every async handler captures the hub's
// shared_ptr, so the hub - and each connection - stays alive exactly as long
// as some handler can still touch it; Stop() merely closes everything early.
class ChatPeerHub : public std::enable_shared_from_this<ChatPeerHub> {
 public:
  using PeerRegisteredHandler =
      std::function<void(const std::string& service_id, const std::string& game_id,
                         int32_t negotiated_version,
                         const std::vector<chirp::gateway::PeerCapability>& features)>;
  // reason: "displaced" (a second registration with the same service_id took
  // over), "timeout" (silent past twice the heartbeat cadence), "lost" (the
  // socket errored/closed), "rejected" (registration denied).
  using PeerDroppedHandler = std::function<void(const std::string& service_id,
                                                const std::string& reason)>;
  using ChannelMessageHandler = std::function<void(
      const std::string& service_id, const chirp::gateway::ChannelMessageNotify&)>;

  struct Options {
    uint16_t port = 8200;
    // service_id -> shared secret. An empty map rejects everyone unless
    // allow_unknown_peers is set (open registration mode).
    std::map<std::string, std::string> allowed_peers;
    int32_t min_peer_version = 1;
    bool allow_unknown_peers = false;
    // Assigned to every peer in PEER_REGISTER_RESP; a connection silent for
    // twice this cadence is dropped.
    int heartbeat_interval_seconds = 30;
  };

  // The hub's own protocol version; the negotiated version is
  // min(kPeerProtocolVersion, the spoke's).
  static constexpr int32_t kPeerProtocolVersion = 1;

  static std::shared_ptr<ChatPeerHub> Create(asio::io_context& io, Options options,
                                             PeerRegisteredHandler on_registered,
                                             PeerDroppedHandler on_dropped,
                                             ChannelMessageHandler on_channel_message);
  ~ChatPeerHub();

  void Start();
  // Asynchronous: closes the acceptor and every connection. Safe from any
  // thread - the teardown is posted onto the hub's io_context, the only
  // thread its state belongs to. To make the teardown actually run, the
  // caller must keep the io_context alive afterwards (drain or run until the
  // work drains, e.g. before stopping the loop).
  void Stop();

  // Bound port once Start() has opened the acceptor (0 before that, and on
  // bind failure). Tests bind port 0 and read the assigned one back.
  uint16_t port() const;

  // Downlink to a registered peer. io-context-thread entry points; return
  // false when no peer with that service_id is currently registered.
  bool SendInject(const std::string& service_id,
                  const chirp::gateway::PeerInjectMessageNotify& notify);

  // Registered game_id for a service_id ("" when unknown). The hub prefixes
  // "<game_id>:" when routing App-side sends back to a game plane.
  std::string game_id_for(const std::string& service_id) const;

 private:
  struct PrivateTag { explicit PrivateTag() = default; };

  // One accepted connection, from accept to deregistration. The first frame
  // must be PEER_REGISTER_REQ; until then the connection carries no identity
  // and sits in the pre-registration pen under the same idle timer. Handlers
  // take the hub as a shared_ptr instead of storing it: storing one would
  // close a hub->conn->hub reference cycle.
  struct PeerConn : public std::enable_shared_from_this<PeerConn> {
    explicit PeerConn(asio::ip::tcp::socket s);
    ~PeerConn();

    void ReadHeader(const std::shared_ptr<ChatPeerHub>& hub);
    void ReadBody(uint32_t size, const std::shared_ptr<ChatPeerHub>& hub);
    void HandlePacket(const chirp::gateway::Packet& pkt,
                      const std::shared_ptr<ChatPeerHub>& hub);
    void ArmIdleTimer(const std::shared_ptr<ChatPeerHub>& hub);
    void SendRawPacket(chirp::gateway::MsgID msg_id, int64_t seq, const std::string& body);
    // Idempotent; also deregisters from the hub and fires on_dropped_ when
    // this conn still owns its peer-table entry.
    void Close(const std::shared_ptr<ChatPeerHub>& hub, const std::string& reason);

    asio::ip::tcp::socket socket;
    asio::steady_timer idle_timer;
    std::array<uint8_t, 4> header{};
    std::string body;
    std::string service_id;  // empty until registered
    std::string game_id;
    std::vector<chirp::gateway::PeerCapability> features;
    bool registered = false;
    bool closing = false;
    int64_t uplink_seq = 0;
  };

  ChatPeerHub(asio::io_context& io, Options options, PeerRegisteredHandler on_registered,
              PeerDroppedHandler on_dropped, ChannelMessageHandler on_channel_message,
              PrivateTag);

  void DoAccept();
  void HandleRegister(const std::shared_ptr<PeerConn>& conn,
                      const chirp::gateway::PeerRegisterReq& req);
  void DoStop();

  Options options_;
  asio::io_context& io_;
  PeerRegisteredHandler on_registered_;
  PeerDroppedHandler on_dropped_;
  ChannelMessageHandler on_channel_message_;
  asio::ip::tcp::acceptor acceptor_;
  // service_id -> live connection; entries are erased on deregistration.
  std::map<std::string, std::shared_ptr<PeerConn>> peers_;
  // Accepted-but-not-yet-registered connections; kept here only so the hub
  // can hand ownership to tests/observers if needed - Close() removes the
  // conn from this pen too.
  std::vector<std::shared_ptr<PeerConn>> peers_unregistered_;
  bool stopping_ = false;
};

}  // namespace chirp::network

#endif  // CHIRP_NETWORK_CHAT_PEER_HUB_H_
