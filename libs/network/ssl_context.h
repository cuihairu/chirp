#pragma once

#include <memory>
#include <string>

namespace asio {
namespace ssl {
class context;
} // namespace ssl
} // namespace asio

namespace chirp::network {

// Builds the shared server-side ssl::context used by TLS edges: PEM
// certificate chain + private key loaded from disk, TLS 1.2 as the minimum
// protocol version, SSLv2/SSLv3 and compression disabled. Returns null on
// failure and, when `error` is non-null, fills it with a message naming the
// file that could not be loaded.
std::shared_ptr<asio::ssl::context> MakeServerSslContext(const std::string& cert_path,
                                                         const std::string& key_path,
                                                         std::string* error = nullptr);

} // namespace chirp::network
