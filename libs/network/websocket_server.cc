#include "network/websocket_server.h"

namespace chirp::network {

WebSocketServer::WebSocketServer(asio::io_context& io, uint16_t port, FrameCallback on_frame, CloseCallback on_close)
    : io_(io),
      acceptor_(io_, asio::ip::tcp::endpoint(asio::ip::tcp::v4(), port)),
      on_frame_(std::move(on_frame)),
      on_close_(std::move(on_close)) {}

void WebSocketServer::Start() { DoAccept(); }

void WebSocketServer::Stop() {
  asio::error_code ec;
  acceptor_.close(ec);
}

void WebSocketServer::DoAccept() {
  acceptor_.async_accept([this](std::error_code ec, asio::ip::tcp::socket socket) {
    if (!ec) {
      auto session = std::make_shared<WebSocketSession>(std::move(socket), on_frame_, on_close_);
      session->Start();
    }
    if (acceptor_.is_open()) {
      DoAccept();
    }
  });
}

} // namespace chirp::network

