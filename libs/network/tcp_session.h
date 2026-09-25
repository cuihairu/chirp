#pragma once

#include <array>
#include <atomic>
#include <deque>
#include <functional>
#include <memory>
#include <mutex>
#include <string>

#include <asio.hpp>

#include "network/length_prefixed_framer.h"
#include "network/session.h"

namespace chirp::network {

// Stream-generic session core: Stream is asio::ip::tcp::socket (plain) or
// asio::ssl::stream<asio::ip::tcp::socket> (TLS). All byte/queue/strand logic
// lives in tcp_session.cc; both stream types are explicitly instantiated
// there, so consumers never see template bodies or OpenSSL headers.
template <typename Stream>
class TcpSessionT : public Session,
                    public std::enable_shared_from_this<TcpSessionT<Stream>> {
public:
  using FrameCallback = std::function<void(std::shared_ptr<Session>, std::string&& payload)>;
  using CloseCallback = std::function<void(std::shared_ptr<Session>)>;

  TcpSessionT(Stream socket, FrameCallback on_frame, CloseCallback on_close = nullptr);

  void Start();
  void Close() override;
  bool IsClosed() const override { return closed_; }
  bool PeerHalfClosed() override;

  // Sends bytes as-is (caller decides framing). Thread-safe.
  void Send(std::string bytes) override;

  // Sends bytes and closes the connection once pending writes are flushed.
  void SendAndClose(std::string bytes) override;

  asio::ip::tcp::endpoint RemoteEndpoint() const;

  std::string RemoteAddress() const override;

private:
  void DoRead();
  void DoWrite();
  void DoClose();

  Stream socket_;
  asio::strand<asio::any_io_executor> strand_;
  FrameCallback on_frame_;
  CloseCallback on_close_;

  std::array<uint8_t, 4096> read_buf_{};
  LengthPrefixedFramer framer_;

  std::deque<std::string> write_q_;
  bool write_in_flight_{false};
  bool close_after_write_{false};
  std::atomic<bool> closed_{false};  // written on the io strand, read by IsClosed()
  bool stream_established_{false};
};

// Plain-TCP alias: every existing consumer (servers, TcpClient, tests) keeps
// using this name; the TLS instantiation is declared in tls_session.h.
using TcpSession = TcpSessionT<asio::ip::tcp::socket>;
extern template class TcpSessionT<asio::ip::tcp::socket>;

} // namespace chirp::network
