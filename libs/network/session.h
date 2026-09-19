#pragma once

#include <string>

namespace chirp::network {

class Session {
public:
  virtual ~Session() = default;

  // Begins serving the connection. Only the accept loop calls this, right
  // after MakeSession hands a session over; default is a no-op so mock or
  // pre-started sessions need no override.
  virtual void Start() {}

  // Sends bytes as-is (caller decides framing). Thread-safe.
  virtual void Send(std::string bytes) = 0;

  // Sends bytes and closes the connection once pending writes are flushed.
  virtual void SendAndClose(std::string bytes) = 0;

  // Closes the connection.
  virtual void Close() = 0;

  // Check if the session is closed.
  virtual bool IsClosed() const = 0;

  // True when the peer has already closed its side: its FIN reached the
  // kernel but the read loop has not processed it yet. Writing to such a
  // session succeeds while nobody will ever read the bytes, so delivery
  // paths must treat it as offline and queue the message instead.
  // Best-effort; false means "not known to be half-closed".
  virtual bool PeerHalfClosed() { return false; }

  // Best-effort textual address of the remote peer (empty when unknown, e.g.
  // after the socket is gone). Used by edges for per-address rate limiting.
  virtual std::string RemoteAddress() const = 0;
};

} // namespace chirp::network

