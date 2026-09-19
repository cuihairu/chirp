#pragma once

#include <memory>

#include <asio.hpp>

#include "network/tcp_server.h"
#include "network/websocket_server.h"

namespace asio {
namespace ssl {
class context;
} // namespace ssl
} // namespace asio

namespace chirp::network {

// TLS edges: the plain servers' accept loop, but every accepted socket is
// wrapped in an ssl::stream (server-side handshake) before it becomes a
// session. `ssl` must be a context built by MakeServerSslContext; listeners
// sharing one context share the loaded certificate.
class TlsTcpServer final : public TcpServer {
public:
  TlsTcpServer(asio::io_context& io, uint16_t port, std::shared_ptr<asio::ssl::context> ssl,
               FrameCallback on_frame, CloseCallback on_close = nullptr);

protected:
  std::shared_ptr<Session> MakeSession(asio::ip::tcp::socket socket) override;

private:
  std::shared_ptr<asio::ssl::context> ssl_;
};

class TlsWebSocketServer final : public WebSocketServer {
public:
  TlsWebSocketServer(asio::io_context& io, uint16_t port, std::shared_ptr<asio::ssl::context> ssl,
                     FrameCallback on_frame, CloseCallback on_close = nullptr);

protected:
  std::shared_ptr<Session> MakeSession(asio::ip::tcp::socket socket) override;

private:
  std::shared_ptr<asio::ssl::context> ssl_;
};

} // namespace chirp::network
