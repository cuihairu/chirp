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
  using CloseCallback = TcpSession::CloseCallback;

  TcpServer(asio::io_context& io, uint16_t port, FrameCallback on_frame, CloseCallback on_close = nullptr);

  void Start();
  void Stop();

private:
  void DoAccept();

  asio::io_context& io_;
  // Serializes every acceptor_ access: Start()/Stop() run on the caller's
  // thread while DoAccept handlers run on the io thread.
  asio::strand<asio::io_context::executor_type> strand_;
  asio::ip::tcp::acceptor acceptor_;
  FrameCallback on_frame_;
  CloseCallback on_close_;
};

} // namespace chirp::network
