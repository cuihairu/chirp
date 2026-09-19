#pragma once

#include <asio.hpp>
#include <asio/ssl.hpp>

#include "network/tcp_session.h"
#include "network/websocket_session.h"

namespace chirp::network {

// TLS instantiations of the stream-generic session cores. The template
// bodies live in tcp_session.cc / websocket_session.cc; these extern
// declarations let TLS consumers link against the explicit instantiations
// there instead of requesting (undefined) implicit ones locally.
extern template class TcpSessionT<asio::ssl::stream<asio::ip::tcp::socket>>;
extern template class WebSocketSessionT<asio::ssl::stream<asio::ip::tcp::socket>>;

using TlsTcpSession = TcpSessionT<asio::ssl::stream<asio::ip::tcp::socket>>;
using TlsWebSocketSession = WebSocketSessionT<asio::ssl::stream<asio::ip::tcp::socket>>;

} // namespace chirp::network
