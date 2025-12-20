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
};

} // namespace chirp::network

