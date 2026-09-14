#pragma once

#include <string>

namespace chirp::network {

class Session {
public:
  virtual ~Session() = default;

  // Sends bytes as-is (caller decides framing). Thread-safe.
  virtual void Send(std::string bytes) = 0;

  // Sends bytes and closes the connection once pending writes are flushed.
  virtual void SendAndClose(std::string bytes) = 0;

  // Closes the connection.
  virtual void Close() = 0;

  // Check if the session is closed.
  virtual bool IsClosed() const = 0;

  // Best-effort textual address of the remote peer (empty when unknown, e.g.
  // after the socket is gone). Used by edges for per-address rate limiting.
  virtual std::string RemoteAddress() const = 0;
};

} // namespace chirp::network

