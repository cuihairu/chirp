#pragma once

#include <array>
#include <atomic>
#include <deque>
#include <functional>
#include <memory>
#include <string>

#include <asio.hpp>

#include "network/length_prefixed_framer.h"
#include "network/session.h"
#include "network/websocket_frame.h"

namespace chirp::network {

// Stream-generic WebSocket session core: Stream is asio::ip::tcp::socket
// (plain) or asio::ssl::stream<asio::ip::tcp::socket> (wss). The upgrade
// handshake and all frame logic operate on decrypted plaintext buffers, so
// the byte-level code is shared verbatim across both stream types; the
// bodies live in websocket_session.cc with explicit instantiations there.
template <typename Stream>
class WebSocketSessionT : public Session,
                          public std::enable_shared_from_this<WebSocketSessionT<Stream>> {
public:
  using FrameCallback = std::function<void(std::shared_ptr<Session>, std::string&& payload)>;
  using CloseCallback = std::function<void(std::shared_ptr<Session>)>;

  // `handshake_done` must be true for client-side sessions whose upgrade
  // handshake was already completed elsewhere (e.g. WebSocketClient::Connect);
  // such sessions parse incoming bytes as WebSocket frames right away.
  WebSocketSessionT(Stream socket, FrameCallback on_frame,
                    CloseCallback on_close = nullptr,
                    bool handshake_done = false);

  void Start();
  void Close() override;
  bool IsClosed() const override { return closed_; }

  void Send(std::string bytes) override;
  void SendAndClose(std::string bytes) override;

  asio::ip::tcp::endpoint RemoteEndpoint() const;

  std::string RemoteAddress() const override;

private:
  void DoRead();
  void DoWrite();
  void DoClose();

  bool TryConsumeHandshake();
  void ConsumeWebSocketFrames();

  Stream socket_;
  asio::strand<asio::any_io_executor> strand_;
  FrameCallback on_frame_;
  CloseCallback on_close_;

  std::array<uint8_t, 4096> read_buf_{};
  std::string handshake_buf_;
  WebSocketFrameParser ws_parser_;
  LengthPrefixedFramer framer_;

  std::deque<std::string> write_q_;
  bool write_in_flight_{false};
  bool close_after_write_{false};
  std::atomic<bool> closed_{false};  // written on the io strand, read by IsClosed()
  bool handshake_done_{false};
  bool stream_established_{false};
};

// Plain-TCP alias: every existing consumer (servers, WebSocketClient, tests)
// keeps using this name; the TLS instantiation is declared in tls_session.h.
using WebSocketSession = WebSocketSessionT<asio::ip::tcp::socket>;
extern template class WebSocketSessionT<asio::ip::tcp::socket>;

} // namespace chirp::network
