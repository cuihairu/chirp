#include "network/websocket_server.h"

namespace chirp::network {
namespace {

// bind_host must be a literal IP (no DNS): service --host flags carry IP
// literals, and a typo must abort startup instead of falling back to the
// wildcard address.
asio::ip::address ResolveBindAddress(const std::string& bind_host) {
  asio::error_code ec;
  const asio::ip::address address = asio::ip::make_address(bind_host, ec);
  if (ec) {
    throw asio::system_error(ec, "invalid bind host: " + bind_host);
  }
  return address;
}

}  // namespace

WebSocketServer::WebSocketServer(asio::io_context& io, uint16_t port, FrameCallback on_frame, CloseCallback on_close,
                                 const std::string& bind_host)
    : io_(io),
      strand_(io.get_executor()),
      acceptor_(io_, asio::ip::tcp::endpoint(ResolveBindAddress(bind_host), port)),
      on_frame_(std::move(on_frame)),
      on_close_(std::move(on_close)) {
  asio::error_code ec;
  const auto local = acceptor_.local_endpoint(ec);
  port_ = local.port();
  address_ = local.address();
}

void WebSocketServer::Start() { DoAccept(); }

void WebSocketServer::Stop() {
  // close() from the caller's thread would race with DoAccept handlers
  // touching acceptor_ on the io thread; post it through the same strand
  // instead. If the io_context already stopped, the destructor closes it.
  asio::post(strand_, [this] {
    asio::error_code ec;
    acceptor_.close(ec);
  });
}

std::shared_ptr<Session> WebSocketServer::MakeSession(asio::ip::tcp::socket socket) {
  return std::make_shared<WebSocketSession>(std::move(socket), on_frame_, on_close_);
}

void WebSocketServer::DoAccept() {
  acceptor_.async_accept(
      asio::bind_executor(strand_, [this](std::error_code ec, asio::ip::tcp::socket socket) {
        if (!ec) {
          auto session = MakeSession(std::move(socket));
          if (session) {
            session->Start();
          }
        }
        if (acceptor_.is_open()) {
          DoAccept();
        }
      }));
}

} // namespace chirp::network

