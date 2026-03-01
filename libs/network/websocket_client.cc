#include "websocket_client.h"

#include "common/logger.h"
#include "websocket_utils.h"

using chirp::common::Logger;

namespace chirp {
namespace network {

WebSocketClient::WebSocketClient(asio::io_context& io)
    : io_(io), socket_(io_) {}

WebSocketClient::~WebSocketClient() {
  Disconnect();
}

void WebSocketClient::SetCallbacks(FrameCallback on_frame, CloseCallback on_close) {
  on_frame_ = std::move(on_frame);
  on_close_ = std::move(on_close);
}

bool WebSocketClient::Connect(const std::string& host, uint16_t port, const std::string& path) {
  try {
    asio::ip::tcp::endpoint endpoint(asio::ip::make_address(host), port);
    asio::error_code ec;
    socket_.connect(endpoint, ec);
    if (ec) {
      Logger::Instance().Error("WebSocket connect failed: " + ec.message());
      return false;
    }

    // Send WebSocket handshake using helper function
    std::string handshake = BuildWebSocketHandshake(host, port, path);

    asio::write(socket_, asio::buffer(handshake), ec);
    if (ec) {
      Logger::Instance().Error("WebSocket handshake send failed: " + ec.message());
      return false;
    }

    // Read response
    std::array<char, 1024> response_buf;
    size_t bytes_read = socket_.read_some(asio::buffer(response_buf), ec);
    if (ec) {
      Logger::Instance().Error("WebSocket handshake response failed: " + ec.message());
      return false;
    }

    std::string response(response_buf.data(), bytes_read);
    if (!IsWebSocketUpgradeSuccessful(response)) {
      Logger::Instance().Error("WebSocket handshake failed: Invalid response");
      return false;
    }

    // Create session with stored callbacks and start it
    session_ = std::make_shared<WebSocketSession>(std::move(socket_), on_frame_, on_close_);
    session_->Start();
    return true;
  } catch (const std::exception& e) {
    Logger::Instance().Error(std::string("WebSocket connect exception: ") + e.what());
    return false;
  }
}

void WebSocketClient::Disconnect() {
  if (session_) {
    session_->Close();
    session_.reset();
  }
}

} // namespace network
} // namespace chirp
