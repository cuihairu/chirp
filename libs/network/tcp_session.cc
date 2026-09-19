#include "network/tcp_session.h"

#include "network/stream_ops.h"

namespace chirp::network {

template <typename Stream>
TcpSessionT<Stream>::TcpSessionT(Stream socket, FrameCallback on_frame, CloseCallback on_close)
    : socket_(std::move(socket)),
      strand_(socket_.get_executor()),
      on_frame_(std::move(on_frame)),
      on_close_(std::move(on_close)) {}

template <typename Stream>
void TcpSessionT<Stream>::Start() {
  // Plain sessions: StartHandshake invokes the handler synchronously with a
  // cleared error code, which lands on DoRead() exactly like the old
  // `Start() { DoRead(); }`. TLS sessions: async_handshake on the server
  // side; a failed handshake closes the session and the server keeps
  // accepting. `self` must be captured here — the accept loop drops its
  // reference right after Start(), so a pending TLS handshake is the only
  // thing keeping the session alive.
  auto self = this->shared_from_this();
  StreamOps<Stream>::StartHandshake(socket_, strand_, [self](std::error_code ec) {
    if (ec) {
      self->DoClose();
      return;
    }
    self->stream_established_ = true;
    self->DoRead();
  });
}

template <typename Stream>
void TcpSessionT<Stream>::Close() {
  asio::post(strand_, [self = this->shared_from_this()] { self->DoClose(); });
}

template <typename Stream>
asio::ip::tcp::endpoint TcpSessionT<Stream>::RemoteEndpoint() const {
  return StreamOps<Stream>::RemoteEndpoint(socket_);
}

template <typename Stream>
std::string TcpSessionT<Stream>::RemoteAddress() const {
  return RemoteEndpoint().address().to_string();
}

template <typename Stream>
bool TcpSessionT<Stream>::PeerHalfClosed() {
  if (closed_) {
    return true;
  }
  return StreamOps<Stream>::PeerHalfClosed(socket_);
}

template <typename Stream>
void TcpSessionT<Stream>::Send(std::string bytes) {
  asio::post(strand_, [self = this->shared_from_this(), bytes = std::move(bytes)]() mutable {
    self->write_q_.push_back(std::move(bytes));
    if (!self->write_in_flight_) {
      self->write_in_flight_ = true;
      self->DoWrite();
    }
  });
}

template <typename Stream>
void TcpSessionT<Stream>::SendAndClose(std::string bytes) {
  asio::post(strand_, [self = this->shared_from_this(), bytes = std::move(bytes)]() mutable {
    self->close_after_write_ = true;
    self->write_q_.push_back(std::move(bytes));
    if (!self->write_in_flight_) {
      self->write_in_flight_ = true;
      self->DoWrite();
    }
  });
}

template <typename Stream>
void TcpSessionT<Stream>::DoRead() {
  auto self = this->shared_from_this();
  socket_.async_read_some(asio::buffer(read_buf_),
                          asio::bind_executor(strand_, [self](std::error_code ec, std::size_t n) {
                            if (ec) {
                              self->DoClose();
                              return;
                            }
                            self->framer_.Append(self->read_buf_.data(), n);
                            while (true) {
                              auto frame = self->framer_.PopFrame();
                              if (!frame) {
                                break;
                              }
                              if (self->on_frame_) {
                                self->on_frame_(std::static_pointer_cast<Session>(self), std::move(*frame));
                              }
                            }
                            self->DoRead();
                          }));
}

template <typename Stream>
void TcpSessionT<Stream>::DoWrite() {
  auto self = this->shared_from_this();
  if (write_q_.empty()) {
    write_in_flight_ = false;
    if (close_after_write_) {
      DoClose();
    }
    return;
  }

  asio::async_write(socket_, asio::buffer(write_q_.front()),
                    asio::bind_executor(strand_, [self](std::error_code ec, std::size_t /*n*/) {
                      if (ec) {
                        self->DoClose();
                        return;
                      }
                      self->write_q_.pop_front();
                      self->DoWrite();
                    }));
}

template <typename Stream>
void TcpSessionT<Stream>::DoClose() {
  if (closed_) {
    return;
  }
  closed_ = true;

  StreamOps<Stream>::Close(socket_, stream_established_);

  if (on_close_) {
    on_close_(std::static_pointer_cast<Session>(this->shared_from_this()));
  }
}

// Explicit instantiations: the template bodies live only in this TU, so all
// consumers (servers, clients, tests) link against these and the coverage
// report attributes every line here. stream_ops.h must be included above
// this point so the StreamOps specializations are declared at the
// instantiation points.
template class TcpSessionT<asio::ip::tcp::socket>;
template class TcpSessionT<asio::ssl::stream<asio::ip::tcp::socket>>;

} // namespace chirp::network
