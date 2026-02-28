#ifndef CHIRP_NETWORK_WEBSOCKET_CLIENT_H_
#define CHIRP_NETWORK_WEBSOCKET_CLIENT_H_

#include <asio.hpp>
#include <functional>
#include <memory>
#include <string>

#include "session.h"
#include "websocket_session.h"

namespace chirp {
namespace network {

class WebSocketClient {
public:
  using FrameCallback = WebSocketSession::FrameCallback;
  using CloseCallback = WebSocketSession::CloseCallback;

  explicit WebSocketClient(asio::io_context& io);
  ~WebSocketClient();

  bool Connect(const std::string& host, uint16_t port,
               const std::string& path = "/");
  void Disconnect();
  std::shared_ptr<Session> GetSession() const { return session_; }
  bool IsConnected() const { return session_ && !session_->IsClosed(); }

  void SetCallbacks(FrameCallback on_frame, CloseCallback on_close = nullptr);

private:
  asio::io_context& io_;
  asio::ip::tcp::socket socket_;
  std::shared_ptr<WebSocketSession> session_;
  FrameCallback on_frame_;
  CloseCallback on_close_;
};

} // namespace network
} // namespace chirp

#endif // CHIRP_NETWORK_WEBSOCKET_CLIENT_H_
