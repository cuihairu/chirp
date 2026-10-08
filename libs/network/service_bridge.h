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
// through to the configured backend (chat or search), so clients only talk
// to the gateway while backend business packets (2xxx) still reach their real
// handler unchanged.
//
// Two failure modes (one instance each; chat keeps kick, search degrades):
//   - kKickClient (default): a dropped or rejected internal connection kicks
//     the real client (KICK_NOTIFY + close) so it reconnects and reattaches
//     cleanly. Chat traffic is all-or-nothing, so this stays the cleanest.
//   - kDegrade (search): a failed or absent pipe never touches the client
//     connection. Undeliverable 2248 SEARCH_MESSAGE_REQ get a synthesized
//     SEARCH_MESSAGE_RESP with SERVER_UNAVAILABLE, and the pipe is re-dialed
//     from the credentials kept at Attach on the next query (search is an
//     optional enhancement and must not take the session down with it).
//
// Lifecycle per client: Attach() dials the backend, authenticates the internal
// connection with SERVER_AUTH_REQ (the same trust gate the server plane
// uses), replays the client's LOGIN_REQ (token + device passthrough), and
// from then on relays frames verbatim in both directions. The gateway never
// inspects sequence numbers and the backend never learns it is behind a
// proxy: every push the backend emits on "the target user's own connection"
// arrives on this one internal connection and goes back to that one client.
//
// Threading: everything runs on the gateway's single io_context thread -
// public entry points are called from packet handlers, async completion
// handlers execute on the same io. No locking, and writes go through a
// one-in-flight async queue (a blocking write here would stall the whole
// gateway).
//
// Known limits (by design, recorded in TODO.md): the internal connection has
// no application-layer heartbeat (backends have no idle timeout, so this
// matches the direct-entry path), each client holds one backend connection,
// and the token is replayed verbatim - auth-enhanced (MySQL) tokens are not
// understood by the backend until login semantics are unified.
class ServiceBridge {
 public:
  // A disabled bridge (empty host) makes every entry point a no-op, which
  // keeps the gateway byte-for-byte compatible with the pre-bridge behavior.
  // search_degrade=true selects the degrade failure mode described above;
  // it is only meaningful for the search pipeline instance.
  ServiceBridge(asio::io_context& io, std::string host, uint16_t port,
                std::string service_id, std::string service_secret,
                bool search_degrade = false);
  ~ServiceBridge();

  bool enabled() const { return enabled_; }

  // Dials the backend and runs the handshake for a freshly authenticated
  // client. token/device_id are replayed verbatim in the internal LOGIN_REQ
  // and kept (degrade mode) so a failed pipe can be re-dialed later.
  void Attach(const std::shared_ptr<chirp::network::Session>& client,
              const std::string& token, const std::string& device_id);

  // Relays one 2xxx packet. Queued (bounded) while the handshake is still
  // in flight - clients typically send their first message right after
  // login. In degrade mode an undeliverable packet (no pipe, queue full,
  // handshake failure) is answered with SERVER_UNAVAILABLE instead of
  // dropping the client. (Non-const session: the degrade answer writes
  // back through Session::Send.)
  void ForwardPacket(chirp::network::Session* client,
                     const chirp::gateway::Packet& pkt, int msg_id);

  // Client disconnected or logged out: close the internal side, nothing
  // sent to the client. Idempotent.
  void Detach(const chirp::network::Session* client);

 private:
  struct InternalConn;
  // Attach 留存的登录凭据（弱引用客户端）：降级模式下断管后的下一条查询
  // 用它自动重拨，客户端无感。
  struct ClientCreds {
    std::weak_ptr<chirp::network::Session> client;
    std::string token;
    std::string device_id;
  };

  InternalConn& StartConn(const std::shared_ptr<chirp::network::Session>& client,
                          const std::string& token, const std::string& device_id);
  void FailClient(InternalConn& conn, const std::string& reason);
  // 2248 的降级回码：以请求的 sequence 合成 SEARCH_MESSAGE_RESP(SERVER_
  // UNAVAILABLE)，经客户端连接原路发回（不关闭、不断开）。
  void DegradeSearch(chirp::network::Session& client,
                     const chirp::gateway::Packet& pkt);
  // 降级重拨：凭据仍在且客户端存活时按原 token/device 重开管道。
  void ReattachForDegrade(const chirp::network::Session* client);

  asio::io_context& io_;
  bool enabled_;
  std::string host_;
  uint16_t port_;
  std::string service_id_;
  std::string service_secret_;
  bool search_degrade_;
  std::unordered_map<const chirp::network::Session*, std::shared_ptr<InternalConn>> conns_;
  std::unordered_map<const chirp::network::Session*, ClientCreds> creds_;
};

} // namespace chirp::gateway
