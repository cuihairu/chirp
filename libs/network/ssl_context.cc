#include "network/ssl_context.h"

#include <system_error>
#include <utility>

#include <openssl/ssl.h>

#include <asio/ssl.hpp>

namespace chirp::network {

std::shared_ptr<asio::ssl::context> MakeServerSslContext(const std::string& cert_path,
                                                         const std::string& key_path,
                                                         std::string* error) {
  auto fail = [error](std::string message) {
    if (error) {
      *error = std::move(message);
    }
    return std::shared_ptr<asio::ssl::context>();
  };

  auto ctx = std::make_shared<asio::ssl::context>(asio::ssl::context::tls_server);
  std::error_code ec;

  ctx->use_certificate_chain_file(cert_path, ec);
  if (ec) {
    return fail("load certificate '" + cert_path + "': " + ec.message());
  }

  ctx->use_private_key_file(key_path, asio::ssl::context::pem, ec);
  if (ec) {
    return fail("load private key '" + key_path + "': " + ec.message());
  }

  // Hardening with fixed arguments cannot fail, so there is no error path
  // to handle: TLS 1.2 as the floor, SSLv2/3 and compression off. The
  // native min-version call is the only raw OpenSSL use in the repo
  // (asio 1.32 has no set_min_proto_version).
  ctx->set_options(asio::ssl::context::default_workarounds | asio::ssl::context::no_sslv2 |
                   asio::ssl::context::no_sslv3 | asio::ssl::context::no_compression);
  SSL_CTX_set_min_proto_version(ctx->native_handle(), TLS1_2_VERSION);

  return ctx;
}

} // namespace chirp::network
