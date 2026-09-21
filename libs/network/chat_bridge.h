#pragma once

#include <cstdint>
#include <memory>
#include <string>
#include <unordered_map>

#include <asio.hpp>

#include "network/session.h"

namespace chirp {
namespace gateway {
class Packet;
}
}

namespace chirp::gateway {

// Pipes one internal TCP connection per authenticated client edge session
// through to chirp_chat, so clients only talk to the gateway while chat
// business packets (2xxx) still reach their real handler unchanged.
//
// Lifecycle per client: Attach() dials chat, authenticates the internal
// connection with SERVER_AUTH_REQ (the same trust gate the server plane
// uses), replays the client's LOGIN_REQ (token + device passthrough), and
// from then on relays frames verbatim in both directions. The gateway never
// inspects sequence numbers and chat never learns it is behind a proxy:
// every push chat emits on "the target user's own connection" arrives on
// this one internal connection and goes back to that one client.
//
// Failure model: a dropped or rejected internal connection kicks the real
// client (KICK_NOTIFY + close) so it reconnects and reattaches cleanly;
// Detach() (client gone / logged out) just closes the internal side, which
// triggers chat's normal idempotent disconnect cleanup.
//
// Threading: everything runs on the gateway's single io_context thread -
// public entry points are called from packet handlers, async completion
// handlers execute on the same io. No locking, and writes go through a
// one-in-flight async queue (a blocking write here would stall the whole
// gateway).
//
// Known limits (by design, recorded in TODO.md): the internal connection has
// no application-layer heartbeat (chat has no idle timeout, so this matches
// the direct-entry path), each client holds one chat connection, and the
// token is replayed verbatim - auth-enhanced (MySQL) tokens are not
// understood by chat until login semantics are unified.
class ChatBridge {
 public:
  // A disabled bridge (empty host) makes every entry point a no-op, which
  // keeps the gateway byte-for-byte compatible with the pre-bridge behavior.
  ChatBridge(asio::io_context& io, std::string host, uint16_t port,
             std::string service_id, std::string service_secret);
  ~ChatBridge();

  bool enabled() const { return enabled_; }

  // Dials chat and runs the handshake for a freshly authenticated client.
  // token/device_id are replayed verbatim in the internal LOGIN_REQ.
  void Attach(const std::shared_ptr<chirp::network::Session>& client,
              const std::string& token, const std::string& device_id);

  // Relays one 2xxx packet. Queued (bounded) while the handshake is still
  // in flight - clients typically send their first message right after
  // login - and dropped when no connection exists for this client.
  void ForwardToChat(const chirp::network::Session* client,
                     const chirp::gateway::Packet& pkt);

  // Client disconnected or logged out: close the internal side, nothing
  // sent to the client. Idempotent.
  void Detach(const chirp::network::Session* client);

 private:
  struct InternalConn;

  InternalConn& StartConn(const std::shared_ptr<chirp::network::Session>& client,
                          const std::string& token, const std::string& device_id);
  void FailClient(InternalConn& conn, const std::string& reason);

  asio::io_context& io_;
  bool enabled_;
  std::string host_;
  uint16_t port_;
  std::string service_id_;
  std::string service_secret_;
  std::unordered_map<const chirp::network::Session*, std::shared_ptr<InternalConn>> conns_;
};

} // namespace chirp::gateway
