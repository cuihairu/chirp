#ifndef CHIRP_SERVICES_NOTIFICATION_HTTP_PUSH_TRANSPORT_H_
#define CHIRP_SERVICES_NOTIFICATION_HTTP_PUSH_TRANSPORT_H_

#include <cstddef>
#include <cstdint>
#include <memory>
#include <string>

#include "push_transport.h"

namespace chirp {
namespace app_notification {

// One established HTTP connection (request bytes in, response bytes out).
// Implementations own the wire: plain TCP today, TLS once the build carries
// OpenSSL. All methods are blocking; timeouts are expressed through
// WaitReadable deadlines rather than per-call parameters.
class HttpConnection {
 public:
  virtual ~HttpConnection() = default;

  // Writes exactly size bytes; false on any error (peer gone, TLS fault).
  virtual bool WriteAll(const char* data, std::size_t size) = 0;

  // Blocks until data is readable or deadline_ms elapses. False on timeout
  // or error; true means the next ReadSome will not spin.
  virtual bool WaitReadable(int deadline_ms) = 0;

  // Reads up to size bytes. Returns bytes read, 0 on orderly close,
  // -1 on error. Never blocks past a preceding WaitReadable deadline.
  virtual int ReadSome(char* data, std::size_t size) = 0;
};

// Decides how bytes reach an endpoint. One instance may serve any number of
// sequential connections; implementations must be safe for the caller's
// single-threaded dispatch loop.
class HttpConnectionFactory {
 public:
  virtual ~HttpConnectionFactory() = default;

  // Opens host:port. scheme is "http" or "https" - TLS-aware factories use
  // it to pick the handshake, plain factories ignore it. Returns nullptr on
  // failure (resolution, refusal, deadline).
  virtual std::unique_ptr<HttpConnection> Connect(const std::string& host,
                                                  std::uint16_t port,
                                                  const std::string& scheme) = 0;
};

// Default factory: blocking TCP over asio with a connect deadline.
class TcpHttpConnectionFactory : public HttpConnectionFactory {
 public:
  struct Config {
    int connect_timeout_ms = 3000;
  };
  TcpHttpConnectionFactory() : TcpHttpConnectionFactory(Config{}) {}
  explicit TcpHttpConnectionFactory(Config config);
  ~TcpHttpConnectionFactory() override;

  std::unique_ptr<HttpConnection> Connect(const std::string& host,
                                          std::uint16_t port,
                                          const std::string& scheme) override;

 private:
  struct Impl;
  Config config_;
  std::unique_ptr<Impl> impl_;
};

// TLS-aware factory. "https" endpoints get a certificate-verified TLS 1.2+
// handshake (with SNI for host names; IP literals connect without it),
// "http" endpoints fall through to plain TCP so one factory can serve both.
// An empty ca_file defers to the OpenSSL default trust store; deployments
// with a private CA point ca_file at it instead.
class SslHttpConnectionFactory : public HttpConnectionFactory {
 public:
  struct Config {
    int connect_timeout_ms = 3000;
    int handshake_timeout_ms = 3000;
    bool verify_certificates = true;
    std::string ca_file;  // empty: OpenSSL default trust store
  };
  SslHttpConnectionFactory() : SslHttpConnectionFactory(Config{}) {}
  explicit SslHttpConnectionFactory(Config config);
  ~SslHttpConnectionFactory() override;

  std::unique_ptr<HttpConnection> Connect(const std::string& host,
                                          std::uint16_t port,
                                          const std::string& scheme) override;

 private:
  struct Impl;
  Config config_;
  std::unique_ptr<Impl> impl_;  // owns the shared ssl::context
};

// Real HTTP/1.1 POST transport behind the PushTransport seam. Sends the
// prebuilt provider payload, reads the response and returns the body for
// 2xx statuses; every other outcome (bad URL, connect/write/read failure,
// deadline, malformed or non-2xx response) answers empty - the same
// "no usable response" contract LoggingPushTransport established.
class HttpPushTransport : public PushTransport {
 public:
  struct Config {
    int deadline_ms = 5000;         // whole-request budget
    std::size_t max_header_bytes = 32 * 1024;
    std::size_t max_body_bytes = 1024 * 1024;
  };

  explicit HttpPushTransport(std::shared_ptr<HttpConnectionFactory> factory)
      : HttpPushTransport(std::move(factory), Config{}) {}
  HttpPushTransport(std::shared_ptr<HttpConnectionFactory> factory,
                    Config config);
  ~HttpPushTransport() override;

  std::string Post(const PushRequest& request) override;

 private:
  // Runs one request; every failure sets *err (never empty on empty body).
  std::string DoPost(const PushRequest& request, std::string* err);

  std::shared_ptr<HttpConnectionFactory> factory_;
  Config config_;
};

}  // namespace app_notification
}  // namespace chirp

#endif  // CHIRP_SERVICES_NOTIFICATION_HTTP_PUSH_TRANSPORT_H_
