#pragma once

#include <cstdint>
#include <functional>
#include <memory>

#include <asio.hpp>

#include "network/tcp_session.h"

namespace chirp::network {

class TcpServer {
public:
  using FrameCallback = TcpSession::FrameCallback;

  TcpServer(asio::io_context& io, uint16_t port, FrameCallback on_frame);

  void Start();
  void Stop();

private:
  void DoAccept();

  asio::io_context& io_;
  asio::ip::tcp::acceptor acceptor_;
  FrameCallback on_frame_;
};

} // namespace chirp::network
