#include "network/tls_server.h"

#include <asio/ssl.hpp>

#include "network/tls_session.h"

namespace chirp::network {

TlsTcpServer::TlsTcpServer(asio::io_context& io, uint16_t port,
                           std::shared_ptr<asio::ssl::context> ssl, FrameCallback on_frame,
                           CloseCallback on_close)
    : TcpServer(io, port, std::move(on_frame), std::move(on_close)), ssl_(std::move(ssl)) {}

std::shared_ptr<Session> TlsTcpServer::MakeSession(asio::ip::tcp::socket socket) {
  asio::ssl::stream<asio::ip::tcp::socket> stream(std::move(socket), *ssl_);
  return std::make_shared<TlsTcpSession>(std::move(stream), on_frame_, on_close_);
}

TlsWebSocketServer::TlsWebSocketServer(asio::io_context& io, uint16_t port,
                                       std::shared_ptr<asio::ssl::context> ssl, FrameCallback on_frame,
                                       CloseCallback on_close)
    : WebSocketServer(io, port, std::move(on_frame), std::move(on_close)), ssl_(std::move(ssl)) {}

std::shared_ptr<Session> TlsWebSocketServer::MakeSession(asio::ip::tcp::socket socket) {
  asio::ssl::stream<asio::ip::tcp::socket> stream(std::move(socket), *ssl_);
  return std::make_shared<TlsWebSocketSession>(std::move(stream), on_frame_, on_close_);
}

} // namespace chirp::network
