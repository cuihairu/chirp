#pragma once

#include <array>
#include <deque>
#include <functional>
#include <memory>
#include <mutex>
#include <string>

#include <asio.hpp>

#include "network/length_prefixed_framer.h"

namespace chirp::network {

class TcpSession : public std::enable_shared_from_this<TcpSession> {
public:
  using FrameCallback = std::function<void(std::shared_ptr<TcpSession>, std::string&& payload)>;
  using CloseCallback = std::function<void(std::shared_ptr<TcpSession>)>;

  TcpSession(asio::ip::tcp::socket socket, FrameCallback on_frame, CloseCallback on_close = nullptr);

  void Start();
  void Close();

  // Sends bytes as-is (caller decides framing). Thread-safe.
  void Send(std::string bytes);

  // Sends bytes and closes the connection once pending writes are flushed.
  void SendAndClose(std::string bytes);

  asio::ip::tcp::endpoint RemoteEndpoint() const;

private:
  void DoRead();
  void DoWrite();
  void DoClose();

  asio::ip::tcp::socket socket_;
  asio::strand<asio::any_io_executor> strand_;
  FrameCallback on_frame_;
  CloseCallback on_close_;

  std::array<uint8_t, 4096> read_buf_{};
  LengthPrefixedFramer framer_;

  std::deque<std::string> write_q_;
  bool write_in_flight_{false};
  bool close_after_write_{false};
  bool closed_{false};
};

} // namespace chirp::network
