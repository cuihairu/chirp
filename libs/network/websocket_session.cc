#include "network/websocket_session.h"

#include <sstream>

#include "network/stream_ops.h"
#include "network/websocket_util.h"

namespace chirp::network {
namespace {

std::string FindHeaderValue(const std::string& headers, const std::string& key) {
  std::istringstream iss(headers);
  std::string line;
  const std::string want = key + ":";
  while (std::getline(iss, line)) {
    if (!line.empty() && line.back() == '\r') {
      line.pop_back();
    }
    if (IStartsWith(line, want)) {
      return TrimAsciiWhitespace(line.substr(want.size()));
    }
  }
  return "";
}

} // namespace

template <typename Stream>
WebSocketSessionT<Stream>::WebSocketSessionT(Stream socket, FrameCallback on_frame,
                                             CloseCallback on_close, bool handshake_done)
    : socket_(std::move(socket)),
      strand_(socket_.get_executor()),
      on_frame_(std::move(on_frame)),
      on_close_(std::move(on_close)),
      handshake_done_(handshake_done) {}

template <typename Stream>
void WebSocketSessionT<Stream>::Start() {
  // Plain sessions: StartHandshake invokes the handler synchronously with a
  // cleared error code, which lands on DoRead() exactly like the old
  // `Start() { DoRead(); }`. TLS (wss) sessions: async_handshake on the
  // server side; a failed handshake closes the session and the server keeps
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
asio::ip::tcp::endpoint WebSocketSessionT<Stream>::RemoteEndpoint() const {
  return StreamOps<Stream>::RemoteEndpoint(socket_);
}

template <typename Stream>
std::string WebSocketSessionT<Stream>::RemoteAddress() const {
  return RemoteEndpoint().address().to_string();
}

template <typename Stream>
void WebSocketSessionT<Stream>::Close() {
  asio::post(strand_, [self = this->shared_from_this()] { self->DoClose(); });
}

template <typename Stream>
void WebSocketSessionT<Stream>::Send(std::string bytes) {
  asio::post(strand_, [self = this->shared_from_this(), bytes = std::move(bytes)]() mutable {
    if (self->closed_) {
      return;
    }
    self->write_q_.push_back(BuildWebSocketFrame(/*opcode=*/0x2, bytes, /*mask=*/false));
    if (!self->write_in_flight_) {
      self->write_in_flight_ = true;
      self->DoWrite();
    }
  });
}

template <typename Stream>
void WebSocketSessionT<Stream>::SendAndClose(std::string bytes) {
  asio::post(strand_, [self = this->shared_from_this(), bytes = std::move(bytes)]() mutable {
    if (self->closed_) {
      return;
    }
    self->close_after_write_ = true;
    self->write_q_.push_back(BuildWebSocketFrame(/*opcode=*/0x2, bytes, /*mask=*/false));
    if (!self->write_in_flight_) {
      self->write_in_flight_ = true;
      self->DoWrite();
    }
  });
}

template <typename Stream>
void WebSocketSessionT<Stream>::DoRead() {
  auto self = this->shared_from_this();
  socket_.async_read_some(asio::buffer(read_buf_),
                          asio::bind_executor(strand_, [self](std::error_code ec, std::size_t n) {
                            if (ec) {
                              self->DoClose();
                              return;
                            }

                            if (!self->handshake_done_) {
                              self->handshake_buf_.append(reinterpret_cast<const char*>(self->read_buf_.data()), n);
                              if (!self->TryConsumeHandshake()) {
                                self->DoRead();
                                return;
                              }
                            } else {
                              self->ws_parser_.Append(self->read_buf_.data(), n);
                            }

                            self->ConsumeWebSocketFrames();
                            self->DoRead();
                          }));
}

template <typename Stream>
bool WebSocketSessionT<Stream>::TryConsumeHandshake() {
  const size_t end = handshake_buf_.find("\r\n\r\n");
  if (end == std::string::npos) {
    return false;
  }

  const std::string request = handshake_buf_.substr(0, end + 4);
  std::string leftover = handshake_buf_.substr(end + 4);
  handshake_buf_.clear();
  handshake_done_ = true;

  const std::string key = FindHeaderValue(request, "Sec-WebSocket-Key");
  const std::string accept = ComputeWebSocketAccept(key);

  std::string resp = "HTTP/1.1 101 Switching Protocols\r\n";
  resp += "Upgrade: websocket\r\n";
  resp += "Connection: Upgrade\r\n";
  resp += "Sec-WebSocket-Accept: " + accept + "\r\n\r\n";

  write_q_.push_back(resp);
  if (!write_in_flight_) {
    write_in_flight_ = true;
    DoWrite();
  }

  if (!leftover.empty()) {
    ws_parser_.Append(reinterpret_cast<const uint8_t*>(leftover.data()), leftover.size());
  }

  return true;
}

template <typename Stream>
void WebSocketSessionT<Stream>::ConsumeWebSocketFrames() {
  while (true) {
    auto f = ws_parser_.PopFrame();
    if (!f) {
      break;
    }

    if (!f->fin) {
      DoClose();
      return;
    }

    switch (f->opcode) {
    case 0x2: { // binary
      framer_.Append(reinterpret_cast<const uint8_t*>(f->payload.data()), f->payload.size());
      while (true) {
        auto frame = framer_.PopFrame();
        if (!frame) {
          break;
        }
        if (on_frame_) {
          on_frame_(std::static_pointer_cast<Session>(this->shared_from_this()), std::move(*frame));
        }
      }
      break;
    }
    case 0x9: { // ping
      write_q_.push_back(BuildWebSocketFrame(/*opcode=*/0xA, f->payload, /*mask=*/false));
      if (!write_in_flight_) {
        write_in_flight_ = true;
        DoWrite();
      }
      break;
    }
    case 0x8: { // close
      write_q_.push_back(BuildWebSocketFrame(/*opcode=*/0x8, "", /*mask=*/false));
      close_after_write_ = true;
      if (!write_in_flight_) {
        write_in_flight_ = true;
        DoWrite();
      }
      break;
    }
    default:
      // ignore
      break;
    }
  }
}

template <typename Stream>
void WebSocketSessionT<Stream>::DoWrite() {
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
void WebSocketSessionT<Stream>::DoClose() {
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
template class WebSocketSessionT<asio::ip::tcp::socket>;
template class WebSocketSessionT<asio::ssl::stream<asio::ip::tcp::socket>>;

} // namespace chirp::network
