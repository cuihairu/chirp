#pragma once

#include <array>
#include <deque>
#include <functional>
#include <memory>
#include <string>

#include <asio.hpp>

#include "network/length_prefixed_framer.h"
#include "network/session.h"
#include "network/websocket_frame.h"

namespace chirp::network {

class WebSocketSession : public Session, public std::enable_shared_from_this<WebSocketSession> {
public:
  using FrameCallback = std::function<void(std::shared_ptr<Session>, std::string&& payload)>;
  using CloseCallback = std::function<void(std::shared_ptr<Session>)>;

  // `handshake_done` must be true for client-side sessions whose upgrade
  // handshake was already completed elsewhere (e.g. WebSocketClient::Connect);
  // such sessions parse incoming bytes as WebSocket frames right away.
  WebSocketSession(asio::ip::tcp::socket socket, FrameCallback on_frame,
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

  asio::ip::tcp::socket socket_;
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
  bool closed_{false};
  bool handshake_done_{false};
};

} // namespace chirp::network

