// SslHttpConnectionFactory: the TLS leg of the push transport, driven
// against a loopback HTTPS server presenting the committed test
// certificate (ca.crt / server.crt under data/push_tls; SAN covers
// 127.0.0.1 and localhost).

#include "http_push_transport.h"

#include <gtest/gtest.h>

#include <atomic>
#include <chrono>
#include <cstdio>
#include <functional>
#include <string>
#include <thread>
#include <vector>

#include <asio.hpp>
#include <asio/ssl.hpp>
#include <poll.h>

#ifndef CHIRP_PUSH_TLS_DATA_DIR
#error "CHIRP_PUSH_TLS_DATA_DIR must be defined by the build"
#endif

namespace {

using chirp::app_notification::HttpConnectionFactory;
using chirp::app_notification::HttpPushTransport;
using chirp::app_notification::PushRequest;
using chirp::app_notification::SslHttpConnectionFactory;

std::string DataFile(const std::string& name) {
  return std::string(CHIRP_PUSH_TLS_DATA_DIR) + "/" + name;
}

PushRequest SampleRequest() {
  PushRequest request;
  request.provider = "fcm";
  request.url = "https://push-test.chirp.invalid/v1/send";
  request.payload = "{\"to\":\"token\"}";
  request.headers["Content-Type"] = "application/json";
  return request;
}

std::string Response(const std::string& status_line, const std::string& body) {
  return status_line + "\r\nContent-Length: " + std::to_string(body.size()) +
         "\r\n\r\n" + body;
}

HttpPushTransport::Config TestConfig() {
  HttpPushTransport::Config config;
  config.deadline_ms = 4000;
  return config;
}

SslHttpConnectionFactory::Config TrustedFactoryConfig() {
  SslHttpConnectionFactory::Config config;
  config.connect_timeout_ms = 2000;
  config.handshake_timeout_ms = 2000;
  config.ca_file = DataFile("ca.crt");
  return config;
}

// Sync helpers shared by the TLS and plain handlers below.
template <typename Stream>
std::string ReadRequest(Stream& stream) {
  std::string buf;
  char chunk[2048];
  for (;;) {
    const std::size_t head_end = buf.find("\r\n\r\n");
    if (head_end != std::string::npos) {
      const std::size_t pos =
          buf.find("Content-Length: ");
      const std::size_t content_length =
          pos == std::string::npos || pos > head_end
              ? 0
              : static_cast<std::size_t>(
                    std::atoi(buf.c_str() + pos + sizeof("Content-Length: ") - 1));
      if (buf.size() >= head_end + 4 + content_length) {
        return buf;
      }
    }
    std::error_code ec;
    const std::size_t n = stream.read_some(asio::buffer(chunk), ec);
    if (ec) {
      return buf;
    }
    buf.append(chunk, n);
  }
}

template <typename Stream>
void WriteAll(Stream& stream, const std::string& bytes) {
  std::error_code ec;
  asio::write(stream, asio::buffer(bytes), ec);
}

// One-connection-at-a-time HTTPS server: accepts, handshakes as the server,
// then hands the established stream to the handler on the connection thread.
class LoopbackTlsServer {
 public:
  using Handler =
      std::function<void(asio::ssl::stream<asio::ip::tcp::socket>&)>;

  // A nonzero stall holds the connection thread before the server
  // handshake, so the client's handshake deadline can fire first.
  explicit LoopbackTlsServer(
      std::string cert_path, std::string key_path, Handler handler,
      std::chrono::milliseconds stall = std::chrono::milliseconds(0))
      : handler_(std::move(handler)), stall_(stall) {
    ctx_ = std::make_unique<asio::ssl::context>(asio::ssl::context::tls_server);
    ctx_->use_certificate_chain_file(std::move(cert_path));
    ctx_->use_private_key_file(std::move(key_path), asio::ssl::context::pem);
    asio::ip::tcp::acceptor acceptor(
        io_, asio::ip::tcp::endpoint(asio::ip::tcp::v4(), 0));
    port_ = acceptor.local_endpoint().port();
    acceptor_ = std::make_unique<asio::ip::tcp::acceptor>(std::move(acceptor));
    accept_thread_ = std::thread([this] { AcceptLoop(); });
  }

  ~LoopbackTlsServer() {
    stop_ = true;
    accept_thread_.join();
    for (std::thread& t : conn_threads_) {
      if (t.joinable()) {
        t.join();
      }
    }
  }

  std::uint16_t port() const { return port_; }

 private:
  // A blocking accept() is not woken by close() on Linux, so poll with a
  // short timeout and check the stop flag between rounds.
  void AcceptLoop() {
    while (!stop_.load()) {
      pollfd pfd{};
      pfd.fd = static_cast<int>(acceptor_->native_handle());
      pfd.events = POLLIN;
      if (::poll(&pfd, 1, 20) <= 0) {
        continue;
      }
      std::error_code ec;
      asio::ip::tcp::socket socket(io_);
      acceptor_->accept(socket, ec);
      if (ec) {
        continue;
      }
      conn_threads_.emplace_back(
          [this, s = std::move(socket)]() mutable {
            asio::ssl::stream<asio::ip::tcp::socket> stream(std::move(s),
                                                            *ctx_);
            if (stall_.count() > 0) {
              // The client's handshake deadline fires while we sleep.
              std::this_thread::sleep_for(stall_);
            }
            std::error_code handshake_ec;
            stream.handshake(asio::ssl::stream_base::server, handshake_ec);
            if (!handshake_ec && handler_) {
              handler_(stream);
            }
          });
    }
  }

  Handler handler_;
  std::chrono::milliseconds stall_{0};
  std::atomic<bool> stop_{false};
  asio::io_context io_;
  std::unique_ptr<asio::ssl::context> ctx_;
  std::unique_ptr<asio::ip::tcp::acceptor> acceptor_;
  std::thread accept_thread_;
  std::vector<std::thread> conn_threads_;
  std::uint16_t port_ = 0;
};

// Plain TCP variant for the http-scheme fallthrough of the SSL factory.
class LoopbackPlainServer {
 public:
  using Handler = std::function<void(asio::ip::tcp::socket&)>;

  explicit LoopbackPlainServer(Handler handler) : handler_(std::move(handler)) {
    asio::ip::tcp::acceptor acceptor(
        io_, asio::ip::tcp::endpoint(asio::ip::tcp::v4(), 0));
    port_ = acceptor.local_endpoint().port();
    acceptor_ = std::make_unique<asio::ip::tcp::acceptor>(std::move(acceptor));
    accept_thread_ = std::thread([this] { AcceptLoop(); });
  }

  ~LoopbackPlainServer() {
    stop_ = true;
    accept_thread_.join();
    for (std::thread& t : conn_threads_) {
      if (t.joinable()) {
        t.join();
      }
    }
  }

  std::uint16_t port() const { return port_; }

 private:
  void AcceptLoop() {
    while (!stop_.load()) {
      pollfd pfd{};
      pfd.fd = static_cast<int>(acceptor_->native_handle());
      pfd.events = POLLIN;
      if (::poll(&pfd, 1, 20) <= 0) {
        continue;
      }
      std::error_code ec;
      asio::ip::tcp::socket socket(io_);
      acceptor_->accept(socket, ec);
      if (ec) {
        continue;
      }
      conn_threads_.emplace_back(
          [this, s = std::move(socket)]() mutable { handler_(s); });
    }
  }

  Handler handler_;
  std::atomic<bool> stop_{false};
  asio::io_context io_;
  std::unique_ptr<asio::ip::tcp::acceptor> acceptor_;
  std::thread accept_thread_;
  std::vector<std::thread> conn_threads_;
  std::uint16_t port_ = 0;
};

TEST(HttpPushTransportTlsTest, HttpsLoopbackRoundTripWithTrustedCa) {
  std::string seen_request;
  LoopbackTlsServer server(DataFile("server.crt"), DataFile("server.key"),
                           [&](asio::ssl::stream<asio::ip::tcp::socket>& s) {
                             seen_request = ReadRequest(s);
                             WriteAll(s, Response("HTTP/1.1 200 OK", "tls-ok"));
                           });

  HttpPushTransport transport(
      std::make_shared<SslHttpConnectionFactory>(TrustedFactoryConfig()),
      TestConfig());
  PushRequest request = SampleRequest();
  request.url =
      "https://127.0.0.1:" + std::to_string(server.port()) + "/v1/send";
  EXPECT_EQ(transport.Post(request), "tls-ok");

  EXPECT_NE(seen_request.find("POST /v1/send HTTP/1.1\r\n"),
            std::string::npos);
  EXPECT_NE(seen_request.find("Content-Type: application/json\r\n"),
            std::string::npos);
}

TEST(HttpPushTransportTlsTest, HostnameEndpointVerifiesDnsSanViaSni) {
  LoopbackTlsServer server(DataFile("server.crt"), DataFile("server.key"),
                           [](asio::ssl::stream<asio::ip::tcp::socket>& s) {
                             WriteAll(s, Response("HTTP/1.1 200 OK", "hi"));
                           });

  HttpPushTransport transport(
      std::make_shared<SslHttpConnectionFactory>(TrustedFactoryConfig()),
      TestConfig());
  PushRequest request = SampleRequest();
  request.url = "https://localhost:" + std::to_string(server.port()) + "/x";
  EXPECT_EQ(transport.Post(request), "hi");
}

TEST(HttpPushTransportTlsTest, UntrustedCaFailsTheHandshake) {
  LoopbackTlsServer server(DataFile("server.crt"), DataFile("server.key"),
                           [](asio::ssl::stream<asio::ip::tcp::socket>& s) {
                             WriteAll(s, Response("HTTP/1.1 200 OK", "nope"));
                           });

  SslHttpConnectionFactory::Config factory_config = TrustedFactoryConfig();
  factory_config.ca_file = DataFile("ca2.crt");  // unrelated trust anchor
  HttpPushTransport transport(
      std::make_shared<SslHttpConnectionFactory>(factory_config), TestConfig());
  PushRequest request = SampleRequest();
  request.url =
      "https://127.0.0.1:" + std::to_string(server.port()) + "/x";
  EXPECT_EQ(transport.Post(request), "");
}

TEST(HttpPushTransportTlsTest, VerificationDisabledAcceptsSelfSigned) {
  LoopbackTlsServer server(DataFile("server.crt"), DataFile("server.key"),
                           [](asio::ssl::stream<asio::ip::tcp::socket>& s) {
                             WriteAll(s, Response("HTTP/1.1 200 OK", "off"));
                           });

  SslHttpConnectionFactory::Config factory_config = TrustedFactoryConfig();
  factory_config.verify_certificates = false;
  factory_config.ca_file.clear();
  HttpPushTransport transport(
      std::make_shared<SslHttpConnectionFactory>(factory_config), TestConfig());
  PushRequest request = SampleRequest();
  request.url =
      "https://127.0.0.1:" + std::to_string(server.port()) + "/x";
  EXPECT_EQ(transport.Post(request), "off");
}

TEST(HttpPushTransportTlsTest, MissingCaFileDisablesTheFactory) {
  SslHttpConnectionFactory::Config factory_config = TrustedFactoryConfig();
  factory_config.ca_file = DataFile("does-not-exist.crt");
  SslHttpConnectionFactory factory(factory_config);
  EXPECT_EQ(factory.Connect("127.0.0.1", 1, "https"), nullptr);
}

TEST(HttpPushTransportTlsTest, HandshakeTimeoutYieldsEmpty) {
  // The server sleeps before its handshake, so the client's 400 ms
  // handshake deadline fires first.
  LoopbackTlsServer server(DataFile("server.crt"), DataFile("server.key"),
                           nullptr, std::chrono::milliseconds(800));

  SslHttpConnectionFactory::Config factory_config = TrustedFactoryConfig();
  factory_config.handshake_timeout_ms = 400;
  HttpPushTransport transport(
      std::make_shared<SslHttpConnectionFactory>(factory_config), TestConfig());
  PushRequest request = SampleRequest();
  request.url =
      "https://127.0.0.1:" + std::to_string(server.port()) + "/x";
  EXPECT_EQ(transport.Post(request), "");
}

TEST(HttpPushTransportTlsTest, PlainSchemeFallsThroughToTcp) {
  LoopbackPlainServer server([](asio::ip::tcp::socket& s) {
    WriteAll(s, Response("HTTP/1.1 200 OK", "plain"));
  });

  HttpPushTransport transport(
      std::make_shared<SslHttpConnectionFactory>(TrustedFactoryConfig()),
      TestConfig());
  PushRequest request = SampleRequest();
  request.url = "http://127.0.0.1:" + std::to_string(server.port()) + "/x";
  EXPECT_EQ(transport.Post(request), "plain");
}

TEST(HttpPushTransportTlsTest, LargeBodySpansTlsRecords) {
  // Past one TLS record (~16 KiB of plaintext), so the client drains
  // several records and exercises the SSL-pending fast path.
  const std::string body(48 * 1024, 'b');
  LoopbackTlsServer server(DataFile("server.crt"), DataFile("server.key"),
                           [&](asio::ssl::stream<asio::ip::tcp::socket>& s) {
                             ReadRequest(s);
                             WriteAll(s, Response("HTTP/1.1 200 OK", body));
                           });

  HttpPushTransport transport(
      std::make_shared<SslHttpConnectionFactory>(TrustedFactoryConfig()),
      TestConfig());
  PushRequest request = SampleRequest();
  request.url =
      "https://127.0.0.1:" + std::to_string(server.port()) + "/x";
  EXPECT_EQ(transport.Post(request), body);
}

TEST(HttpPushTransportTlsTest, TruncatedBodyWithCloseNotifyYieldsEmpty) {
  // The server declares more body than it sends, then closes the TLS
  // session cleanly (close_notify). The client's next read surfaces as EOF
  // rather than an error, and the declared-but-missing body counts the
  // response as truncated: "".
  LoopbackTlsServer server(DataFile("server.crt"), DataFile("server.key"),
                           [](asio::ssl::stream<asio::ip::tcp::socket>& s) {
                             WriteAll(s, "HTTP/1.1 200 OK\r\nContent-Length: "
                                         "100\r\n\r\nxxxxxxxxxx");
                             std::error_code ec;
                             s.shutdown(ec);  // close_notify
                           });

  HttpPushTransport transport(
      std::make_shared<SslHttpConnectionFactory>(TrustedFactoryConfig()),
      TestConfig());
  PushRequest request = SampleRequest();
  request.url =
      "https://127.0.0.1:" + std::to_string(server.port()) + "/x";
  EXPECT_EQ(transport.Post(request), "");
}

TEST(HttpPushTransportTlsTest, RefusedConnectionYieldsEmpty) {
  SslHttpConnectionFactory::Config factory_config = TrustedFactoryConfig();
  factory_config.connect_timeout_ms = 500;
  HttpPushTransport transport(
      std::make_shared<SslHttpConnectionFactory>(factory_config), TestConfig());
  PushRequest request = SampleRequest();
  request.url = "https://127.0.0.1:1/x";  // loopback, nothing listens
  EXPECT_EQ(transport.Post(request), "");
}

}  // namespace
