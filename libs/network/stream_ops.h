#pragma once

// Per-stream-type operations for the stream-generic session cores
// (TcpSessionT / WebSocketSessionT). The specializations are declared here
// and defined in stream_ops.cc — the only network TU (besides the TLS server
// and the ssl context factory) that includes <asio/ssl.hpp>, so plain-socket
// consumers (every service and existing test TU) never see OpenSSL headers.

#include <asio.hpp>
#include <asio/ssl.hpp>

namespace chirp::network {

template <typename Stream> struct StreamOps;  // primary intentionally undefined

// Plain TCP: everything goes straight to the socket.
template <>
struct StreamOps<asio::ip::tcp::socket> {
  static asio::ip::tcp::endpoint RemoteEndpoint(const asio::ip::tcp::socket& s);
  static void Close(asio::ip::tcp::socket& s, bool /*tls_established*/);
  static bool PeerHalfClosed(asio::ip::tcp::socket& s);

  // Plain sessions have no pre-read handshake; invoke on_done synchronously.
  template <typename Handler>
  static void StartHandshake(asio::ip::tcp::socket& /*s*/,
                             asio::strand<asio::any_io_executor>& /*strand*/,
                             Handler&& on_done) {
    on_done(std::error_code());
  }
};

// TLS over TCP.
template <>
struct StreamOps<asio::ssl::stream<asio::ip::tcp::socket>> {
  using Stream = asio::ssl::stream<asio::ip::tcp::socket>;

  static asio::ip::tcp::endpoint RemoteEndpoint(const Stream& s);

  // Best-effort, never-blocking teardown. asio's synchronous stream::shutdown
  // loops reading the peer's close_notify and can park the io thread on a
  // hostile peer, so this goes through the native one-shot SSL_shutdown
  // (queues close_notify) and then closes the TCP socket.
  static void Close(Stream& s, bool tls_established);

  // Half-close detection needs a plaintext peek; unknown over TLS.
  static bool PeerHalfClosed(Stream& s);

  template <typename Handler>
  static void StartHandshake(Stream& s, asio::strand<asio::any_io_executor>& strand,
                             Handler&& on_done) {
    s.async_handshake(asio::ssl::stream_base::server,
                      asio::bind_executor(strand, std::forward<Handler>(on_done)));
  }
};

} // namespace chirp::network
