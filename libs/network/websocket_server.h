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
  virtual ~WebSocketServer() = default;

  void Start();
  void Stop();

  // The bound port: the requested one, or the ephemeral port the kernel
  // picked when constructed with port 0. Valid immediately after the ctor.
  uint16_t Port() const { return port_; }

private:
  void DoAccept();

  // Declaration order matters: acceptor_ binds io_, so io_ initializes first.
  asio::io_context& io_;
  // Serializes every acceptor_ access: Start()/Stop() run on the caller's
  // thread while DoAccept handlers run on the io thread.
  asio::strand<asio::io_context::executor_type> strand_;
  asio::ip::tcp::acceptor acceptor_;
  uint16_t port_ = 0;

protected:
  // Accept-loop seam: subclasses wrap the accepted socket (e.g. in an
  // ssl::stream) before it becomes a session. Return null to drop the
  // connection.
  virtual std::shared_ptr<Session> MakeSession(asio::ip::tcp::socket socket);

  FrameCallback on_frame_;
  CloseCallback on_close_;
};

} // namespace chirp::network

