#include "network/stream_ops.h"

#ifdef _WIN32
#include <winsock2.h>
#else
#include <sys/socket.h>
#endif

#include <openssl/ssl.h>

namespace chirp::network {

// ---------------------------------------------------------------------------
// Plain TCP
// ---------------------------------------------------------------------------

asio::ip::tcp::endpoint StreamOps<asio::ip::tcp::socket>::RemoteEndpoint(
    const asio::ip::tcp::socket& s) {
  asio::error_code ec;
  return s.remote_endpoint(ec);
}

void StreamOps<asio::ip::tcp::socket>::Close(asio::ip::tcp::socket& s, bool) {
  asio::error_code ec;
  s.shutdown(asio::ip::tcp::socket::shutdown_both, ec);
  s.close(ec);
}

bool StreamOps<asio::ip::tcp::socket>::PeerHalfClosed(asio::ip::tcp::socket& s) {
  // Non-blocking MSG_PEEK: 0 bytes means the peer's FIN is sitting in the
  // kernel buffer while the read loop has not processed it yet - writes to
  // this session would vanish. EAGAIN (nothing pending) or pending data
  // both mean still alive as far as we can tell.
  char peek;
#ifdef _WIN32
  // Winsock 无 MSG_DONTWAIT:asio 的异步会话本就在非阻塞模式下,recv 出
  // WSAEWOULDBLOCK 与 POSIX 的 EAGAIN 同义(视为存活);返回 int,无 ssize_t。
  const int n = ::recv(s.native_handle(), &peek, 1, MSG_PEEK);
#else
  const ssize_t n = ::recv(s.native_handle(), &peek, 1, MSG_PEEK | MSG_DONTWAIT);
#endif
  return n == 0;
}

// ---------------------------------------------------------------------------
// TLS over TCP
// ---------------------------------------------------------------------------

asio::ip::tcp::endpoint StreamOps<asio::ssl::stream<asio::ip::tcp::socket>>::RemoteEndpoint(
    const Stream& s) {
  asio::error_code ec;
  return s.lowest_layer().remote_endpoint(ec);
}

void StreamOps<asio::ssl::stream<asio::ip::tcp::socket>>::Close(Stream& s,
                                                                bool tls_established) {
  asio::error_code ec;
  // Guarantee the native call below cannot block the io thread even if the
  // socket somehow ended up in blocking mode.
  s.next_layer().non_blocking(true, ec);
  if (tls_established) {
    // One-shot: queue close_notify and ignore WANT_* — the socket is
    // non-blocking so SSL_shutdown cannot park here, and closing the TCP
    // socket still flushes the queued record. We never wait for the peer's
    // close_notify (that is what asio's synchronous shutdown() would do).
    (void)::SSL_shutdown(static_cast<SSL*>(s.native_handle()));
  }
  s.next_layer().shutdown(asio::ip::tcp::socket::shutdown_both, ec);
  s.next_layer().close(ec);
}

bool StreamOps<asio::ssl::stream<asio::ip::tcp::socket>>::PeerHalfClosed(Stream&) {
  return false;
}

} // namespace chirp::network
