#include "network/tcp_session.h"

namespace chirp::network {

TcpSession::TcpSession(asio::ip::tcp::socket socket, FrameCallback on_frame)
    : socket_(std::move(socket)), strand_(socket_.get_executor()), on_frame_(std::move(on_frame)) {}

void TcpSession::Start() { DoRead(); }

void TcpSession::Close() {
  asio::post(strand_, [self = shared_from_this()] {
    asio::error_code ec;
    self->socket_.shutdown(asio::ip::tcp::socket::shutdown_both, ec);
    self->socket_.close(ec);
  });
}

asio::ip::tcp::endpoint TcpSession::RemoteEndpoint() const {
  asio::error_code ec;
  return socket_.remote_endpoint(ec);
}

void TcpSession::Send(std::string bytes) {
  asio::post(strand_, [self = shared_from_this(), bytes = std::move(bytes)]() mutable {
    self->write_q_.push_back(std::move(bytes));
    if (!self->write_in_flight_) {
      self->write_in_flight_ = true;
      self->DoWrite();
    }
  });
}

void TcpSession::DoRead() {
  auto self = shared_from_this();
  socket_.async_read_some(asio::buffer(read_buf_),
                          asio::bind_executor(strand_, [self](std::error_code ec, std::size_t n) {
                            if (ec) {
                              return;
                            }
                            self->framer_.Append(self->read_buf_.data(), n);
                            while (true) {
                              auto frame = self->framer_.PopFrame();
                              if (!frame) {
                                break;
                              }
                              if (self->on_frame_) {
                                self->on_frame_(self, std::move(*frame));
                              }
                            }
                            self->DoRead();
                          }));
}

void TcpSession::DoWrite() {
  auto self = shared_from_this();
  if (write_q_.empty()) {
    write_in_flight_ = false;
    return;
  }

  asio::async_write(socket_, asio::buffer(write_q_.front()),
                    asio::bind_executor(strand_, [self](std::error_code ec, std::size_t /*n*/) {
                      if (ec) {
                        return;
                      }
                      self->write_q_.pop_front();
                      self->DoWrite();
                    }));
}

} // namespace chirp::network

