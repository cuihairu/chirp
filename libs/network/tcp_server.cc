#include "network/tcp_server.h"

namespace chirp::network {

TcpServer::TcpServer(asio::io_context& io, uint16_t port, FrameCallback on_frame, CloseCallback on_close)
    : io_(io),
      strand_(io.get_executor()),
      acceptor_(io_, asio::ip::tcp::endpoint(asio::ip::tcp::v4(), port)),
      on_frame_(std::move(on_frame)),
      on_close_(std::move(on_close)) {
  asio::error_code ec;
  port_ = acceptor_.local_endpoint(ec).port();
}

void TcpServer::Start() { DoAccept(); }

void TcpServer::Stop() {
  // close() from the caller's thread would race with DoAccept handlers
  // touching acceptor_ on the io thread; post it through the same strand
  // instead. If the io_context already stopped, the destructor closes it.
  asio::post(strand_, [this] {
    asio::error_code ec;
    acceptor_.close(ec);
  });
}

std::shared_ptr<Session> TcpServer::MakeSession(asio::ip::tcp::socket socket) {
  return std::make_shared<TcpSession>(std::move(socket), on_frame_, on_close_);
}

void TcpServer::DoAccept() {
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
