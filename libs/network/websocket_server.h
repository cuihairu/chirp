#pragma once

#include <cstdint>
#include <functional>

#include <asio.hpp>

#include "network/websocket_session.h"

namespace chirp::network {

class WebSocketServer {
public:
  using FrameCallback = WebSocketSession::FrameCallback;
  using CloseCallback = WebSocketSession::CloseCallback;

  WebSocketServer(asio::io_context& io, uint16_t port, FrameCallback on_frame, CloseCallback on_close = nullptr);

  void Start();
  void Stop();

private:
  void DoAccept();

  asio::io_context& io_;
  asio::ip::tcp::acceptor acceptor_;
  FrameCallback on_frame_;
  CloseCallback on_close_;
};

} // namespace chirp::network

