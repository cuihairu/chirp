#include "http_push_transport.h"

#include <algorithm>
#include <cctype>
#include <chrono>
#include <poll.h>

#include <asio.hpp>

#include "logger.h"

namespace chirp {
namespace notification {
namespace {

using common::Logger;

// URL subset the push providers actually use: scheme://host[:port][/path].
struct HttpEndpoint {
  std::string scheme;
  std::string host;
  std::uint16_t port = 0;
  std::string path;
};

bool ParsePort(const std::string& text, std::uint16_t* out) {
  if (text.empty()) {
    return false;
  }
  std::size_t value = 0;
  for (const char c : text) {
    if (c < '0' || c > '9') {
      return false;
    }
    value = value * 10 + static_cast<std::size_t>(c - '0');
    if (value > 65535) {
      return false;
    }
  }
  if (value == 0) {
    return false;
  }
  *out = static_cast<std::uint16_t>(value);
  return true;
}

bool ParseHttpUrl(const std::string& url, HttpEndpoint* out) {
  const std::size_t scheme_end = url.find("://");
  if (scheme_end == std::string::npos) {
    return false;
  }
  out->scheme = url.substr(0, scheme_end);
  if (out->scheme != "http" && out->scheme != "https") {
    return false;
  }
  const std::size_t authority_begin = scheme_end + 3;
  const std::size_t path_slash = url.find('/', authority_begin);
  const std::string authority =
      path_slash == std::string::npos
          ? url.substr(authority_begin)
          : url.substr(authority_begin, path_slash - authority_begin);
  out->path = path_slash == std::string::npos ? "/" : url.substr(path_slash);

  const std::uint16_t default_port = out->scheme == "https" ? 443 : 80;
  if (!authority.empty() && authority.front() == '[') {
    // IPv6 literal: [::1]:8080
    const std::size_t close = authority.find(']');
    if (close == std::string::npos) {
      return false;
    }
    out->host = authority.substr(1, close - 1);
    if (close + 1 == authority.size()) {
      out->port = default_port;
    } else {
      if (authority[close + 1] != ':') {
        return false;
      }
      if (!ParsePort(authority.substr(close + 2), &out->port)) {
        return false;
      }
    }
  } else {
    const std::size_t colon = authority.rfind(':');
    if (colon == std::string::npos) {
      out->host = authority;
      out->port = default_port;
    } else {
      out->host = authority.substr(0, colon);
      if (!ParsePort(authority.substr(colon + 1), &out->port)) {
        return false;
      }
    }
  }
  return !out->host.empty();
}

std::string ToLower(std::string text) {
  std::transform(text.begin(), text.end(), text.begin(), [](unsigned char c) {
    return static_cast<char>(std::tolower(c));
  });
  return text;
}

std::string BuildRequest(const PushRequest& request, const HttpEndpoint& ep) {
  std::string wire;
  wire += "POST " + ep.path + " HTTP/1.1\r\n";
  const bool default_port =
      (ep.scheme == "https" && ep.port == 443) ||
      (ep.scheme == "http" && ep.port == 80);
  wire += "Host: " + ep.host;
  if (!default_port) {
    wire += ":" + std::to_string(ep.port);
  }
  wire += "\r\n";
  for (const auto& [name, value] : request.headers) {
    const std::string lower = ToLower(name);
    // Framing headers are owned by the transport.
    if (lower == "host" || lower == "content-length" || lower == "connection") {
      continue;
    }
    wire += name + ": " + value + "\r\n";
  }
  wire += "Content-Length: " + std::to_string(request.payload.size()) + "\r\n";
  wire += "Connection: close\r\n\r\n";
  wire += request.payload;
  return wire;
}

// Extracts the status code and the Content-Length (nullopt-style when
// absent), then strips the head block ending at head_end, leaving *buf
// holding any body bytes read so far. The caller guarantees buf contains
// the "\r\n\r\n" terminator at head_end.
bool ParseResponseHead(std::string* buf, std::size_t head_end, int* status,
                       bool* has_length, std::size_t* length) {
  const std::size_t line_end = buf->find("\r\n");
  const std::string line = buf->substr(0, line_end);
  if (line.compare(0, 5, "HTTP/") != 0) {
    return false;
  }
  const std::size_t sp1 = line.find(' ');
  if (sp1 == std::string::npos) {
    return false;
  }
  std::size_t sp2 = line.find(' ', sp1 + 1);
  if (sp2 == std::string::npos) {
    sp2 = line.size();
  }
  const std::string code = line.substr(sp1 + 1, sp2 - sp1 - 1);
  if (code.size() != 3) {
    return false;
  }
  int value = 0;
  for (const char c : code) {
    if (c < '0' || c > '9') {
      return false;
    }
    value = value * 10 + (c - '0');
  }
  *status = value;

  *has_length = false;
  *length = 0;
  std::size_t pos = line_end + 2;
  while (pos < head_end) {
    const std::size_t eol = buf->find("\r\n", pos);
    if (eol == std::string::npos || eol > head_end) {
      break;
    }
    const std::string header = ToLower(buf->substr(pos, eol - pos));
    if (header.rfind("content-length:", 0) == 0) {
      const std::size_t digits_begin = header.find_first_not_of(
          " \t", sizeof("content-length:") - 1);
      if (digits_begin == std::string::npos) {
        return false;
      }
      std::size_t parsed = 0;
      const std::string digits = header.substr(digits_begin);
      for (const char c : digits) {
        if (c < '0' || c > '9') {
          return false;
        }
        parsed = parsed * 10 + static_cast<std::size_t>(c - '0');
      }
      *has_length = true;
      *length = parsed;
    }
    pos = eol + 2;
  }

  buf->erase(0, head_end + 4);
  return true;
}

bool IsSuccess(int status) { return status >= 200 && status < 300; }

// ---------------------------------------------------------------------------
// TCP connection
// ---------------------------------------------------------------------------

class TcpHttpConnection : public HttpConnection {
 public:
  explicit TcpHttpConnection(std::unique_ptr<asio::ip::tcp::socket> socket)
      : socket_(std::move(socket)) {}

  bool WriteAll(const char* data, std::size_t size) override {
    std::error_code ec;
    asio::write(*socket_, asio::buffer(data, size), ec);
    return !ec;
  }

  bool WaitReadable(int deadline_ms) override {
    pollfd pfd{};
    pfd.fd = static_cast<int>(socket_->native_handle());
    pfd.events = POLLIN;
    const int rc = ::poll(&pfd, 1, std::max(deadline_ms, 0));
    return rc > 0 && (pfd.revents & (POLLIN | POLLHUP | POLLERR)) != 0;
  }

  int ReadSome(char* data, std::size_t size) override {
    std::error_code ec;
    const std::size_t n = socket_->read_some(asio::buffer(data, size), ec);
    if (ec) {
      return ec == asio::error::eof ? 0 : -1;
    }
    return static_cast<int>(n);
  }

 private:
  std::unique_ptr<asio::ip::tcp::socket> socket_;
};

}  // namespace

// ---------------------------------------------------------------------------
// TcpHttpConnectionFactory
// ---------------------------------------------------------------------------

struct TcpHttpConnectionFactory::Impl {
  // Nothing shared yet: each Connect uses a private io_context so a timed
  // out attempt can never leak handlers into the next one.
};

TcpHttpConnectionFactory::TcpHttpConnectionFactory(Config config)
    : config_(config), impl_(std::make_unique<Impl>()) {}

TcpHttpConnectionFactory::~TcpHttpConnectionFactory() = default;

std::unique_ptr<HttpConnection> TcpHttpConnectionFactory::Connect(
    const std::string& host, std::uint16_t port, const std::string& scheme) {
  (void)scheme;  // plain TCP only; a TLS factory owns https handshakes
  asio::io_context io;
  asio::ip::tcp::resolver resolver(io);
  std::error_code ec;
  const auto endpoints =
      resolver.resolve(host, std::to_string(port), ec);
  if (ec) {
    return nullptr;
  }

  auto socket = std::make_shared<asio::ip::tcp::socket>(io);
  auto timer = std::make_shared<asio::steady_timer>(io);
  bool settled = false;
  std::error_code connect_ec;
  timer->expires_after(std::chrono::milliseconds(config_.connect_timeout_ms));
  timer->async_wait([&](const std::error_code& wait_ec) {
    if (!wait_ec) {  // deadline: abort the connect
      std::error_code cancel_ec;
      socket->cancel(cancel_ec);
    }
  });

  asio::async_connect(*socket, endpoints,
                      [&, socket, timer](const std::error_code& e,
                                         const asio::ip::tcp::endpoint&) {
                        settled = true;
                        connect_ec = e;
                        timer->cancel();
                      });
  io.run();
  if (!settled || connect_ec) {
    return nullptr;
  }
  return std::unique_ptr<HttpConnection>(
      new TcpHttpConnection(std::make_unique<asio::ip::tcp::socket>(
          std::move(*socket))));
}

// ---------------------------------------------------------------------------
// HttpPushTransport
// ---------------------------------------------------------------------------

HttpPushTransport::HttpPushTransport(
    std::shared_ptr<HttpConnectionFactory> factory, Config config)
    : factory_(std::move(factory)), config_(config) {}

HttpPushTransport::~HttpPushTransport() = default;

std::string HttpPushTransport::Post(const PushRequest& request) {
  std::string err;
  std::string body = DoPost(request, &err);
  if (body.empty() && !err.empty()) {
    Logger::Instance().Warn("push-transport " + request.provider + " post to " +
                            request.url + " failed: " + err);
  }
  return body;
}

std::string HttpPushTransport::DoPost(const PushRequest& request,
                                      std::string* err) {
  HttpEndpoint endpoint;
  if (!ParseHttpUrl(request.url, &endpoint)) {
    *err = "unusable url";
    return "";
  }
  auto connection = factory_->Connect(endpoint.host, endpoint.port,
                                      endpoint.scheme);
  if (!connection) {
    *err = "connect failed";
    return "";
  }

  const std::string wire = BuildRequest(request, endpoint);
  if (!connection->WriteAll(wire.data(), wire.size())) {
    *err = "write failed";
    return "";
  }

  const auto start = std::chrono::steady_clock::now();
  std::string buf;
  int status = 0;
  bool head_done = false;
  bool has_length = false;
  std::size_t content_length = 0;
  char chunk[4096];
  while (true) {
    const auto elapsed = std::chrono::duration_cast<std::chrono::milliseconds>(
                             std::chrono::steady_clock::now() - start)
                             .count();
    const int remaining = config_.deadline_ms - static_cast<int>(elapsed);
    if (remaining <= 0) {
      *err = "deadline exceeded";
      return "";
    }
    if (!connection->WaitReadable(remaining)) {
      *err = "wait failed or timed out";
      return "";
    }
    const int n = connection->ReadSome(chunk, sizeof(chunk));
    if (n < 0) {
      *err = "read failed";
      return "";
    }
    if (n == 0) {
      break;  // orderly close: Connection: close makes this the response end
    }
    buf.append(chunk, static_cast<std::size_t>(n));
    if (!head_done) {
      if (buf.size() > config_.max_header_bytes) {
        *err = "response head too large";
        return "";
      }
      const std::size_t head_end = buf.find("\r\n\r\n");
      if (head_end == std::string::npos) {
        continue;
      }
      if (!ParseResponseHead(&buf, head_end, &status, &has_length,
                             &content_length)) {
        *err = "malformed response head";
        return "";
      }
      head_done = true;
      if (has_length && content_length > config_.max_body_bytes) {
        *err = "declared body too large";
        return "";
      }
    }
    if (has_length) {
      if (buf.size() < content_length) {
        continue;  // wait for the rest of the declared body
      }
      buf.resize(content_length);
      break;
    }
    if (buf.size() > config_.max_body_bytes) {
      *err = "body too large";
      return "";
    }
  }

  if (!head_done) {
    *err = "connection closed before response head";
    return "";
  }
  if (has_length && buf.size() < content_length) {
    *err = "connection closed mid-body";
    return "";
  }
  if (!IsSuccess(status)) {
    *err = "http status " + std::to_string(status);
    return "";
  }
  return buf;
}

}  // namespace notification
}  // namespace chirp
