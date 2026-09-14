// HttpPushTransport: real HTTP/1.1 request building, response parsing and
// deadline handling behind the PushTransport seam. Transport logic is driven
// through a scripted connection; a loopback TCP round trip exercises the
// real connection factory end to end.

#include "http_push_transport.h"

#include <gtest/gtest.h>

#include <algorithm>
#include <atomic>
#include <cstdlib>
#include <poll.h>
#include <cstring>
#include <functional>
#include <string>
#include <vector>

#include <asio.hpp>

namespace {

using chirp::notification::HttpConnectionFactory;
using chirp::notification::HttpConnection;
using chirp::notification::HttpPushTransport;
using chirp::notification::PushRequest;
using chirp::notification::TcpHttpConnectionFactory;

// ---------------------------------------------------------------------------
// Scripted connection: records the wire bytes and replays canned reads.
// ---------------------------------------------------------------------------

struct ScriptStep {
  bool wait_result = true;
  int read_result = 0;  // ReadSome return value (<0 error, 0 EOF, >0 bytes)
  std::string bytes;
};

// The connection is owned (and destroyed) by the transport during Post(),
// so tests that need to inspect the wire bytes pass a sink they own.
class ScriptedConnection : public HttpConnection {
 public:
  explicit ScriptedConnection(std::vector<ScriptStep> steps,
                              std::shared_ptr<std::string> sink = nullptr)
      : steps_(std::move(steps)), sink_(std::move(sink)) {}

  bool WriteAll(const char* data, std::size_t size) override {
    if (fail_write_) {
      return false;
    }
    if (sink_) {
      sink_->append(data, size);
    }
    return true;
  }

  bool WaitReadable(int) override { return wait_ok_; }

  int ReadSome(char* data, std::size_t size) override {
    if (next_ >= steps_.size()) {
      return 0;  // behave like an orderly close after the script runs out
    }
    const ScriptStep& step = steps_[next_++];
    if (step.read_result < 0) {
      return step.read_result;
    }
    const std::size_t n = std::min(step.bytes.size(), size);
    std::memcpy(data, step.bytes.data(), n);
    return static_cast<int>(n);
  }

  void FailWrites() { fail_write_ = true; }
  void FailWaits() { wait_ok_ = false; }

 private:
  std::vector<ScriptStep> steps_;
  std::shared_ptr<std::string> sink_;
  bool fail_write_ = false;
  bool wait_ok_ = true;
  std::size_t next_ = 0;
};

class ScriptedFactory : public HttpConnectionFactory {
 public:
  std::unique_ptr<HttpConnection> Connect(const std::string& host,
                                          std::uint16_t port,
                                          const std::string& scheme) override {
    last_host = host;
    last_port = port;
    last_scheme = scheme;
    ++connects;
    if (connection) {
      return std::move(connection);
    }
    return nullptr;
  }

  std::unique_ptr<HttpConnection> connection;
  std::string last_host;
  std::uint16_t last_port = 0;
  std::string last_scheme;
  int connects = 0;
};

PushRequest SampleRequest() {
  PushRequest request;
  request.provider = "fcm";
  request.url = "https://fcm.googleapis.com/v1/projects/demo/messages:send";
  request.payload = "{\"to\":\"token\"}";
  request.headers["Content-Type"] = "application/json";
  request.headers["Authorization"] = "Bearer tok";
  return request;
}

std::string Response(const std::string& status_line, const std::string& body,
                     bool with_length = true) {
  std::string head = status_line + "\r\n";
  if (with_length) {
    head += "Content-Length: " + std::to_string(body.size()) + "\r\n";
  }
  return head + "\r\n" + body;
}

TEST(HttpPushTransportScriptedTest, SuccessWithContentLength) {
  auto wire = std::make_shared<std::string>();
  auto connection = std::make_unique<ScriptedConnection>(
      std::vector<ScriptStep>{{.read_result = 0,
                               .bytes = Response("HTTP/1.1 200 OK", "created")}},
      wire);
  ScriptedFactory factory;
  factory.connection = std::move(connection);
  HttpPushTransport transport(std::shared_ptr<HttpConnectionFactory>(&factory, [](auto*) {}));

  const PushRequest request = SampleRequest();
  const std::string body = transport.Post(request);
  EXPECT_EQ(body, "created");

  const std::string& written = *wire;
  const std::string expected_length =
      "Content-Length: " + std::to_string(request.payload.size()) + "\r\n";
  EXPECT_EQ(written.substr(0, 5), "POST ");
  EXPECT_NE(written.find("POST /v1/projects/demo/messages:send HTTP/1.1\r\n"),
            std::string::npos);
  EXPECT_NE(written.find("Host: fcm.googleapis.com\r\n"), std::string::npos);
  EXPECT_NE(written.find("Content-Type: application/json\r\n"),
            std::string::npos);
  EXPECT_NE(written.find("Authorization: Bearer tok\r\n"), std::string::npos);
  EXPECT_NE(written.find(expected_length), std::string::npos);
  EXPECT_NE(written.find("Connection: close\r\n"), std::string::npos);
  EXPECT_EQ(written.substr(written.size() - request.payload.size()),
            request.payload);
}

TEST(HttpPushTransportScriptedTest, CallerFramingHeadersAreOverridden) {
  auto wire = std::make_shared<std::string>();
  auto connection = std::make_unique<ScriptedConnection>(
      std::vector<ScriptStep>{{.read_result = 0, .bytes = Response("HTTP/1.1 200 OK", "x")}},
      wire);
  ScriptedFactory factory;
  factory.connection = std::move(connection);
  HttpPushTransport transport(std::shared_ptr<HttpConnectionFactory>(&factory, [](auto*) {}));

  PushRequest request = SampleRequest();
  const std::string real_length =
      "Content-Length: " + std::to_string(request.payload.size()) + "\r\n";
  request.headers["Host"] = "evil.example.com";
  request.headers["Content-Length"] = "1";
  request.headers["Connection"] = "keep-alive";
  EXPECT_EQ(transport.Post(request), "x");

  EXPECT_EQ(wire->find("evil.example.com"), std::string::npos);
  EXPECT_EQ(wire->find("Content-Length: 1\r\n"), std::string::npos);
  EXPECT_EQ(wire->find("keep-alive"), std::string::npos);
  EXPECT_NE(wire->find(real_length), std::string::npos);
}

TEST(HttpPushTransportScriptedTest, SuccessWithoutContentLengthReadsToEof) {
  auto connection = std::make_unique<ScriptedConnection>(std::vector<ScriptStep>{
      {.read_result = 0, .bytes = "HTTP/1.1 200 OK\r\n\r\nhel"},
      {.read_result = 0, .bytes = "lo"},
      {.read_result = 0, .bytes = ""}});
  ScriptedFactory factory;
  factory.connection = std::move(connection);
  HttpPushTransport transport(std::shared_ptr<HttpConnectionFactory>(&factory, [](auto*) {}));
  EXPECT_EQ(transport.Post(SampleRequest()), "hello");
}

TEST(HttpPushTransportScriptedTest, NonSuccessStatusYieldsEmpty) {
  auto connection = std::make_unique<ScriptedConnection>(std::vector<ScriptStep>{
      {.read_result = 0, .bytes = Response("HTTP/1.1 500 Internal Server Error", "boom")}});
  ScriptedFactory factory;
  factory.connection = std::move(connection);
  HttpPushTransport transport(std::shared_ptr<HttpConnectionFactory>(&factory, [](auto*) {}));
  EXPECT_EQ(transport.Post(SampleRequest()), "");
}

TEST(HttpPushTransportScriptedTest, MalformedStatusLinesYieldEmpty) {
  const std::vector<std::string> heads = {
      "garbage\r\n\r\n",        // not HTTP/
      "HTTP/1.1\r\n\r\n",      // no status code
      "HTTP/1.1 abc\r\n\r\n",  // non-numeric code
      "HTTP/1.1 20\r\n\r\n",   // too short
      "HTTP/1.1 200\r\nContent-Length:\r\n\r\n",     // CL with no value
      "HTTP/1.1 200\r\nContent-Length:  \r\n\r\n",   // CL with blanks only
      "HTTP/1.1 200\r\nContent-Length: 1a\r\n\r\n",  // CL with non-digits
      "HTTP/1.1 200\r\nContent-Length: x2\r\n\r\n",  // CL not digit-first
  };
  for (const std::string& head : heads) {
    auto connection = std::make_unique<ScriptedConnection>(
        std::vector<ScriptStep>{{.read_result = 0, .bytes = head}});
    ScriptedFactory factory;
    factory.connection = std::move(connection);
    HttpPushTransport transport(
        std::shared_ptr<HttpConnectionFactory>(&factory, [](auto*) {}));
    EXPECT_EQ(transport.Post(SampleRequest()), "") << head;
  }
}

TEST(HttpPushTransportScriptedTest, StatusLineWithoutReasonParses) {
  auto connection = std::make_unique<ScriptedConnection>(
      std::vector<ScriptStep>{{.read_result = 0, .bytes = "HTTP/1.1 204\r\n\r\ndone"}});
  ScriptedFactory factory;
  factory.connection = std::move(connection);
  HttpPushTransport transport(std::shared_ptr<HttpConnectionFactory>(&factory, [](auto*) {}));
  EXPECT_EQ(transport.Post(SampleRequest()), "done");
}

TEST(HttpPushTransportScriptedTest, WriteFailureYieldsEmpty) {
  auto connection = std::make_unique<ScriptedConnection>(std::vector<ScriptStep>{});
  connection->FailWrites();
  ScriptedFactory factory;
  factory.connection = std::move(connection);
  HttpPushTransport transport(std::shared_ptr<HttpConnectionFactory>(&factory, [](auto*) {}));
  EXPECT_EQ(transport.Post(SampleRequest()), "");
}

TEST(HttpPushTransportScriptedTest, WaitFailureYieldsEmpty) {
  auto connection = std::make_unique<ScriptedConnection>(std::vector<ScriptStep>{});
  connection->FailWaits();
  ScriptedFactory factory;
  factory.connection = std::move(connection);
  HttpPushTransport transport(std::shared_ptr<HttpConnectionFactory>(&factory, [](auto*) {}));
  EXPECT_EQ(transport.Post(SampleRequest()), "");
}

TEST(HttpPushTransportScriptedTest, ReadErrorYieldsEmpty) {
  auto connection = std::make_unique<ScriptedConnection>(
      std::vector<ScriptStep>{{.read_result = -1}});
  ScriptedFactory factory;
  factory.connection = std::move(connection);
  HttpPushTransport transport(std::shared_ptr<HttpConnectionFactory>(&factory, [](auto*) {}));
  EXPECT_EQ(transport.Post(SampleRequest()), "");
}

TEST(HttpPushTransportScriptedTest, EofBeforeHeadYieldsEmpty) {
  auto connection = std::make_unique<ScriptedConnection>(
      std::vector<ScriptStep>{{.read_result = 0, .bytes = "HTTP/1.1 20"}});
  ScriptedFactory factory;
  factory.connection = std::move(connection);
  HttpPushTransport transport(std::shared_ptr<HttpConnectionFactory>(&factory, [](auto*) {}));
  EXPECT_EQ(transport.Post(SampleRequest()), "");
}

TEST(HttpPushTransportScriptedTest, CloseMidDeclaredBodyYieldsEmpty) {
  auto connection = std::make_unique<ScriptedConnection>(std::vector<ScriptStep>{
      {.read_result = 0,
       .bytes = "HTTP/1.1 200 OK\r\nContent-Length: 10\r\n\r\nabc"},
      {.read_result = 0, .bytes = ""}});
  // The head declares 10 bytes; only "abc" arrives before EOF.
  ScriptedFactory factory;
  factory.connection = std::move(connection);
  HttpPushTransport transport(std::shared_ptr<HttpConnectionFactory>(&factory, [](auto*) {}));
  EXPECT_EQ(transport.Post(SampleRequest()), "");
}

TEST(HttpPushTransportScriptedTest, DeadlineExhaustedYieldsEmpty) {
  auto connection = std::make_unique<ScriptedConnection>(std::vector<ScriptStep>{});
  ScriptedFactory factory;
  factory.connection = std::move(connection);
  HttpPushTransport::Config config;
  config.deadline_ms = -1;  // budget already gone
  HttpPushTransport transport(
      std::shared_ptr<HttpConnectionFactory>(&factory, [](auto*) {}), config);
  EXPECT_EQ(transport.Post(SampleRequest()), "");
}

TEST(HttpPushTransportScriptedTest, HeadTooLargeYieldsEmpty) {
  auto connection = std::make_unique<ScriptedConnection>(std::vector<ScriptStep>{
      {.read_result = 0, .bytes = "HTTP/1.1 200 OK\r\nX-Pad: " + std::string(64, 'p')}});
  ScriptedFactory factory;
  factory.connection = std::move(connection);
  HttpPushTransport::Config config;
  config.max_header_bytes = 16;
  HttpPushTransport transport(
      std::shared_ptr<HttpConnectionFactory>(&factory, [](auto*) {}), config);
  EXPECT_EQ(transport.Post(SampleRequest()), "");
}

TEST(HttpPushTransportScriptedTest, DeclaredBodyTooLargeYieldsEmpty) {
  std::string head =
      "HTTP/1.1 200 OK\r\nContent-Length: 1000\r\n\r\n";
  auto connection = std::make_unique<ScriptedConnection>(
      std::vector<ScriptStep>{{.read_result = 0, .bytes = head}});
  ScriptedFactory factory;
  factory.connection = std::move(connection);
  HttpPushTransport::Config config;
  config.max_body_bytes = 16;
  HttpPushTransport transport(
      std::shared_ptr<HttpConnectionFactory>(&factory, [](auto*) {}), config);
  EXPECT_EQ(transport.Post(SampleRequest()), "");
}

TEST(HttpPushTransportScriptedTest, EofBodyTooLargeYieldsEmpty) {
  auto connection = std::make_unique<ScriptedConnection>(std::vector<ScriptStep>{
      {.read_result = 0, .bytes = "HTTP/1.1 200 OK\r\n\r\n" + std::string(64, 'b')}});
  ScriptedFactory factory;
  factory.connection = std::move(connection);
  HttpPushTransport::Config config;
  config.max_body_bytes = 16;
  HttpPushTransport transport(
      std::shared_ptr<HttpConnectionFactory>(&factory, [](auto*) {}), config);
  EXPECT_EQ(transport.Post(SampleRequest()), "");
}

TEST(HttpPushTransportScriptedTest, ConnectFailureYieldsEmpty) {
  ScriptedFactory factory;  // Connect returns nullptr
  HttpPushTransport transport(std::shared_ptr<HttpConnectionFactory>(&factory, [](auto*) {}));
  EXPECT_EQ(transport.Post(SampleRequest()), "");
}

TEST(HttpPushTransportScriptedTest, UrlEndpointIsParsedIntoConnect) {
  auto wire = std::make_shared<std::string>();
  auto connection = std::make_unique<ScriptedConnection>(
      std::vector<ScriptStep>{{.read_result = 0, .bytes = Response("HTTP/1.1 200 OK", "ok")}},
      wire);
  ScriptedFactory factory;
  factory.connection = std::move(connection);
  HttpPushTransport transport(std::shared_ptr<HttpConnectionFactory>(&factory, [](auto*) {}));

  PushRequest request = SampleRequest();
  request.url = "http://127.0.0.1:8080/push?api=x";
  EXPECT_EQ(transport.Post(request), "ok");
  EXPECT_EQ(factory.last_host, "127.0.0.1");
  EXPECT_EQ(factory.last_port, 8080);
  EXPECT_EQ(factory.last_scheme, "http");
  EXPECT_EQ(factory.connects, 1);
  EXPECT_NE(wire->find("Host: 127.0.0.1:8080\r\n"), std::string::npos);
}

TEST(HttpPushTransportScriptedTest, UnusableUrlsNeverReachTheFactory) {
  const std::vector<std::string> urls = {
      "example.com/no-scheme",  // missing scheme
      "ftp://example.com/x",    // unsupported scheme
      "://missing-scheme/x",    // empty scheme
      "http://host:0/x",        // port zero
      "http://host:99999/x",    // port out of range
      "http://host:abc/x",      // non-numeric port
      "http://host:/x",         // empty port
      "http://[::1:8080/x",     // unterminated IPv6 literal
      "http://[::1]x:8080/x",   // junk after IPv6 literal
      "http://[::1]:abc/x",     // IPv6 with bad port
      "http:///no-host",        // empty host
  };
  for (const std::string& url : urls) {
    ScriptedFactory factory;
    HttpPushTransport transport(
        std::shared_ptr<HttpConnectionFactory>(&factory, [](auto*) {}));
    PushRequest request = SampleRequest();
    request.url = url;
    EXPECT_EQ(transport.Post(request), "") << url;
    EXPECT_EQ(factory.connects, 0) << url;
  }
}

TEST(HttpPushTransportScriptedTest, Ipv6EndpointsParse) {
  ScriptedFactory factory;
  factory.connection = std::make_unique<ScriptedConnection>(
      std::vector<ScriptStep>{{.read_result = 0, .bytes = Response("HTTP/1.1 200 OK", "ok")}});
  HttpPushTransport transport(std::shared_ptr<HttpConnectionFactory>(&factory, [](auto*) {}));

  PushRequest request = SampleRequest();
  request.url = "http://[::1]:8080/push";
  EXPECT_EQ(transport.Post(request), "ok");
  EXPECT_EQ(factory.last_host, "::1");
  EXPECT_EQ(factory.last_port, 8080);

  // No port after the literal: the scheme default applies.
  factory.connection = std::make_unique<ScriptedConnection>(
      std::vector<ScriptStep>{{.read_result = 0, .bytes = Response("HTTP/1.1 200 OK", "ok")}});
  PushRequest bare = SampleRequest();
  bare.url = "http://[::1]/push";
  EXPECT_EQ(transport.Post(bare), "ok");
  EXPECT_EQ(factory.last_host, "::1");
  EXPECT_EQ(factory.last_port, 80);
}

TEST(HttpPushTransportScriptedTest, UrlWithoutPathDefaultsToSlash) {
  ScriptedFactory factory;
  factory.connection = std::make_unique<ScriptedConnection>(
      std::vector<ScriptStep>{{.read_result = 0, .bytes = Response("HTTP/1.1 200 OK", "ok")}});
  HttpPushTransport transport(std::shared_ptr<HttpConnectionFactory>(&factory, [](auto*) {}));

  PushRequest request = SampleRequest();
  request.url = "https://push.example.com";
  EXPECT_EQ(transport.Post(request), "ok");
  EXPECT_EQ(factory.last_host, "push.example.com");
  EXPECT_EQ(factory.last_port, 443);
}

// ---------------------------------------------------------------------------
// Loopback: the real TcpHttpConnectionFactory against a local HTTP server.
// ---------------------------------------------------------------------------

// Minimal one-connection-at-a-time HTTP server; the handler owns the socket.
class LoopbackHttpServer {
 public:
  using Handler = std::function<void(asio::ip::tcp::socket)>;

  explicit LoopbackHttpServer(Handler handler) : handler_(std::move(handler)) {
    asio::ip::tcp::acceptor acceptor(io_, asio::ip::tcp::endpoint(asio::ip::tcp::v4(), 0));
    port_ = acceptor.local_endpoint().port();
    acceptor_ = std::make_unique<asio::ip::tcp::acceptor>(std::move(acceptor));
    accept_thread_ = std::thread([this] { AcceptLoop(); });
  }

  ~LoopbackHttpServer() {
    stop_ = true;
    accept_thread_.join();
    for (std::thread& t : conn_threads_) {
      if (t.joinable()) {
        t.join();
      }
    }
  }

  std::uint16_t port() const { return port_; }

  // Reads the full request (head + Content-Length body).
  static std::string ReadRequest(asio::ip::tcp::socket& socket) {
    std::string buf;
    char chunk[2048];
    for (;;) {
      const std::size_t head_end = buf.find("\r\n\r\n");
      if (head_end != std::string::npos) {
        std::size_t content_length = 0;
        std::size_t pos = 0;
        while ((pos = buf.find("Content-Length: ", pos)) != std::string::npos &&
               pos < head_end) {
          content_length = static_cast<std::size_t>(
              std::atoi(buf.c_str() + pos + sizeof("Content-Length: ") - 1));
          break;
        }
        const std::size_t body = head_end + 4;
        if (buf.size() >= body + content_length) {
          return buf;
        }
      }
      std::error_code ec;
      const std::size_t n = socket.read_some(asio::buffer(chunk), ec);
      if (ec) {
        return buf;
      }
      buf.append(chunk, n);
    }
  }

  static void WriteAll(asio::ip::tcp::socket& socket, const std::string& bytes) {
    std::error_code ec;
    asio::write(socket, asio::buffer(bytes), ec);
  }

  // Closes with an RST instead of a graceful FIN.
  static void Reset(asio::ip::tcp::socket& socket) {
    asio::socket_base::linger linger(true, 0);
    std::error_code ec;
    socket.set_option(linger, ec);
    socket.close(ec);
  }

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
      conn_threads_.emplace_back([this, s = std::move(socket)]() mutable {
        handler_(std::move(s));
      });
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

HttpPushTransport::Config TestConfig() {
  HttpPushTransport::Config config;
  config.deadline_ms = 2000;
  return config;
}

TEST(HttpPushTransportLoopbackTest, RoundTripDeliversRequestAndBody) {
  std::string seen_request;
  LoopbackHttpServer server([&](asio::ip::tcp::socket socket) {
    seen_request = LoopbackHttpServer::ReadRequest(socket);
    LoopbackHttpServer::WriteAll(socket, Response("HTTP/1.1 200 OK", "queued"));
  });

  HttpPushTransport transport(std::make_shared<TcpHttpConnectionFactory>(),
                              TestConfig());
  PushRequest request = SampleRequest();
  request.url = "http://127.0.0.1:" + std::to_string(server.port()) + "/v1/send";
  EXPECT_EQ(transport.Post(request), "queued");

  EXPECT_NE(seen_request.find("POST /v1/send HTTP/1.1\r\n"), std::string::npos);
  EXPECT_NE(seen_request.find("Host: 127.0.0.1:" + std::to_string(server.port())),
            std::string::npos);
  EXPECT_NE(seen_request.find("Authorization: Bearer tok\r\n"), std::string::npos);
}

TEST(HttpPushTransportLoopbackTest, RefusedConnectionYieldsEmpty) {
  // Port 1 on loopback: nothing listens there.
  TcpHttpConnectionFactory::Config config;
  config.connect_timeout_ms = 500;
  HttpPushTransport transport(
      std::make_shared<TcpHttpConnectionFactory>(config), TestConfig());
  PushRequest request = SampleRequest();
  request.url = "http://127.0.0.1:1/send";
  EXPECT_EQ(transport.Post(request), "");
}

TEST(HttpPushTransportLoopbackTest, UnresponsiveEndpointYieldsEmpty) {
  // 192.0.2.1 (RFC 5737 TEST-NET-1) never answers the push. How Post loses
  // depends on the sandbox: a plain blackhole lets the connect lose to the
  // connect deadline, a SYN-proxying gateway completes the handshake and the
  // post loses to its own deadline - either way the contract is "".
  TcpHttpConnectionFactory::Config config;
  config.connect_timeout_ms = 250;
  HttpPushTransport transport(
      std::make_shared<TcpHttpConnectionFactory>(config),
      HttpPushTransport::Config{.deadline_ms = 700});
  PushRequest request = SampleRequest();
  request.url = "http://192.0.2.1:81/send";
  EXPECT_EQ(transport.Post(request), "");
}

TEST(HttpPushTransportLoopbackTest, UnresolvableHostYieldsEmpty) {
  TcpHttpConnectionFactory::Config config;
  config.connect_timeout_ms = 500;
  HttpPushTransport transport(
      std::make_shared<TcpHttpConnectionFactory>(config), TestConfig());
  PushRequest request = SampleRequest();
  request.url = "http://invalid.invalid/send";
  EXPECT_EQ(transport.Post(request), "");
}

TEST(HttpPushTransportLoopbackTest, CloseWithoutResponseYieldsEmpty) {
  LoopbackHttpServer server([](asio::ip::tcp::socket socket) {
    LoopbackHttpServer::ReadRequest(socket);
    std::error_code ec;
    socket.close(ec);
  });
  HttpPushTransport transport(std::make_shared<TcpHttpConnectionFactory>(),
                              TestConfig());
  PushRequest request = SampleRequest();
  request.url = "http://127.0.0.1:" + std::to_string(server.port()) + "/x";
  EXPECT_EQ(transport.Post(request), "");
}

TEST(HttpPushTransportLoopbackTest, CloseMidBodyYieldsEmpty) {
  LoopbackHttpServer server([](asio::ip::tcp::socket socket) {
    LoopbackHttpServer::ReadRequest(socket);
    LoopbackHttpServer::WriteAll(socket, "HTTP/1.1 200 OK\r\nContent-Length: 10\r\n\r\nabc");
    std::error_code ec;
    socket.close(ec);
  });
  HttpPushTransport transport(std::make_shared<TcpHttpConnectionFactory>(),
                              TestConfig());
  PushRequest request = SampleRequest();
  request.url = "http://127.0.0.1:" + std::to_string(server.port()) + "/x";
  EXPECT_EQ(transport.Post(request), "");
}

TEST(HttpPushTransportLoopbackTest, EofDelimitedBodyIsAccepted) {
  LoopbackHttpServer server([](asio::ip::tcp::socket socket) {
    LoopbackHttpServer::ReadRequest(socket);
    LoopbackHttpServer::WriteAll(socket, "HTTP/1.1 200 OK\r\n\r\nclose-delimited");
    std::error_code ec;
    socket.shutdown(asio::ip::tcp::socket::shutdown_send, ec);
    socket.close(ec);
  });
  HttpPushTransport transport(std::make_shared<TcpHttpConnectionFactory>(),
                              TestConfig());
  PushRequest request = SampleRequest();
  request.url = "http://127.0.0.1:" + std::to_string(server.port()) + "/x";
  EXPECT_EQ(transport.Post(request), "close-delimited");
}

TEST(HttpPushTransportLoopbackTest, ResetConnectionYieldsEmpty) {
  LoopbackHttpServer server([](asio::ip::tcp::socket socket) {
    LoopbackHttpServer::Reset(socket);  // RST before reading the request
  });
  HttpPushTransport transport(std::make_shared<TcpHttpConnectionFactory>(),
                              TestConfig());
  PushRequest request = SampleRequest();
  request.url = "http://127.0.0.1:" + std::to_string(server.port()) + "/x";
  EXPECT_EQ(transport.Post(request), "");
}

}  // namespace
