#include "network/websocket_session.h"

#include <sstream>

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

WebSocketSession::WebSocketSession(asio::ip::tcp::socket socket, FrameCallback on_frame, CloseCallback on_close)
    : socket_(std::move(socket)),
      strand_(socket_.get_executor()),
      on_frame_(std::move(on_frame)),
      on_close_(std::move(on_close)) {}

void WebSocketSession::Start() { DoRead(); }

asio::ip::tcp::endpoint WebSocketSession::RemoteEndpoint() const {
  asio::error_code ec;
  return socket_.remote_endpoint(ec);
}

void WebSocketSession::Close() {
  asio::post(strand_, [self = shared_from_this()] { self->DoClose(); });
}

void WebSocketSession::Send(std::string bytes) {
  asio::post(strand_, [self = shared_from_this(), bytes = std::move(bytes)]() mutable {
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

void WebSocketSession::SendAndClose(std::string bytes) {
  asio::post(strand_, [self = shared_from_this(), bytes = std::move(bytes)]() mutable {
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

void WebSocketSession::DoRead() {
  auto self = shared_from_this();
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

bool WebSocketSession::TryConsumeHandshake() {
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

void WebSocketSession::ConsumeWebSocketFrames() {
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
          on_frame_(std::static_pointer_cast<Session>(shared_from_this()), std::move(*frame));
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

void WebSocketSession::DoWrite() {
  auto self = shared_from_this();
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

void WebSocketSession::DoClose() {
  if (closed_) {
    return;
  }
  closed_ = true;

  asio::error_code ec;
  socket_.shutdown(asio::ip::tcp::socket::shutdown_both, ec);
  socket_.close(ec);

  if (on_close_) {
    on_close_(std::static_pointer_cast<Session>(shared_from_this()));
  }
}

} // namespace chirp::network

