#include "tcp_client.h"

#include "common/logger.h"

using chirp::common::Logger;

namespace chirp {
namespace network {

TcpClient::TcpClient(asio::io_context& io)
    : io_(io), socket_(io_) {}

TcpClient::~TcpClient() {
  Disconnect();
}

void TcpClient::SetCallbacks(FrameCallback on_frame, CloseCallback on_close) {
  on_frame_ = std::move(on_frame);
  on_close_ = std::move(on_close);
}

bool TcpClient::Connect(const std::string& host, uint16_t port) {
  try {
    asio::ip::tcp::endpoint endpoint(asio::ip::make_address(host), port);
    asio::error_code ec;
    socket_.connect(endpoint, ec);
    if (ec) {
      Logger::Instance().Error("TCP connect failed: " + ec.message());
      return false;
    }

    // Create session with stored callbacks
    session_ = std::make_shared<TcpSession>(std::move(socket_), on_frame_, on_close_);
    session_->Start();
    return true;
  } catch (const std::exception& e) {
    Logger::Instance().Error(std::string("TCP connect exception: ") + e.what());
    return false;
  }
}

void TcpClient::Disconnect() {
  if (session_) {
    session_->Close();
    session_.reset();
  }
}

} // namespace network
} // namespace chirp
