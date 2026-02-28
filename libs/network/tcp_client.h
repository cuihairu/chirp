#ifndef CHIRP_NETWORK_TCP_CLIENT_H_
#define CHIRP_NETWORK_TCP_CLIENT_H_

#include <asio.hpp>
#include <functional>
#include <memory>
#include <string>

#include "session.h"
#include "tcp_session.h"

namespace chirp {
namespace network {

class TcpClient {
public:
  using FrameCallback = TcpSession::FrameCallback;
  using CloseCallback = TcpSession::CloseCallback;

  explicit TcpClient(asio::io_context& io);
  ~TcpClient();

  bool Connect(const std::string& host, uint16_t port);
  void Disconnect();
  std::shared_ptr<Session> GetSession() const { return session_; }
  bool IsConnected() const { return session_ && !session_->IsClosed(); }

  void SetCallbacks(FrameCallback on_frame, CloseCallback on_close = nullptr);

private:
  asio::io_context& io_;
  asio::ip::tcp::socket socket_;
  std::shared_ptr<TcpSession> session_;
  FrameCallback on_frame_;
  CloseCallback on_close_;
};

} // namespace network
} // namespace chirp

#endif // CHIRP_NETWORK_TCP_CLIENT_H_
