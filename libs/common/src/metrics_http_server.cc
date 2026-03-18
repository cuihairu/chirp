#include "common/metrics_http_server.h"
#include "common/metrics.h"

#include <sstream>
#include <regex>

namespace chirp {
namespace common {

MetricsHttpServer::MetricsHttpServer(asio::io_context& io, uint16_t port)
    : io_(io),
      port_(port),
      running_(false),
      acceptor_(io_, asio::ip::tcp::endpoint(asio::ip::tcp::v4(), port)) {

  // Default metrics handler
  metrics_handler_ = []() {
    return SimpleMetrics::Instance().ExportPrometheus();
  };
}

MetricsHttpServer::~MetricsHttpServer() {
  Stop();
}

bool MetricsHttpServer::Start() {
  if (running_.load()) {
    return true;
  }

  try {
    AcceptConnection();
    running_ = true;
    return true;
  } catch (const std::exception& e) {
    return false;
  }
}

void MetricsHttpServer::Stop() {
  if (!running_.load()) {
    return;
  }

  running_ = false;
  asio::post(io_, [this]() {
    asio::error_code ec;
    acceptor_.close(ec);
  });
}

void MetricsHttpServer::AddRoute(const std::string& path, RouteHandler handler) {
  routes_[path] = std::move(handler);
}

void MetricsHttpServer::AcceptConnection() {
  auto socket = std::make_shared<asio::ip::tcp::socket>(io_);

  acceptor_.async_accept(*socket, [this, socket](asio::error_code ec) {
    if (!ec && running_.load()) {
      HandleConnection(socket);
      AcceptConnection();  // Accept next connection
    }
  });
}

void MetricsHttpServer::HandleConnection(std::shared_ptr<asio::ip::tcp::socket> socket) {
  asio::steady_timer timer(io_, std::chrono::seconds(5));

  auto buffer = std::make_shared<asio::streambuf>();

  asio::async_read_until(*socket, *buffer, "\r\n\r\n",
    [this, socket, buffer](asio::error_code ec, size_t) {
      if (ec) {
        return;
      }

      std::istream stream(buffer.get());
      std::string request((std::istreambuf_iterator<char>(stream)),
                         std::istreambuf_iterator<char>());

      std::string response = HandleRequest(request);

      asio::async_write(*socket, asio::buffer(response),
        [socket](asio::error_code, size_t) {
          asio::error_code ec;
          socket->shutdown(asio::ip::tcp::socket::shutdown_both, ec);
          socket->close(ec);
        });
    });
}

std::string MetricsHttpServer::HandleRequest(const std::string& request) {
  // Simple HTTP request parser
  std::istringstream iss(request);
  std::string method, path, version;
  iss >> method >> path >> version;

  // Only handle GET requests
  if (method != "GET") {
    return BuildResponse("Method Not Allowed", "text/plain", 405);
  }

  // Handle /metrics endpoint
  if (path == "/metrics" || path == "/metrics/") {
    std::string body = metrics_handler_ ? metrics_handler_() : "";
    return BuildResponse(body, "text/plain; version=0.0.4; charset=utf-8");
  }

  // Handle health check
  if (path == "/health" || path == "/health/") {
    return BuildResponse("OK", "text/plain");
  }

  // Handle custom routes
  auto it = routes_.find(path);
  if (it != routes_.end()) {
    std::string body = it->second(path);
    return BuildResponse(body, "application/json");
  }

  // 404
  return BuildResponse("Not Found", "text/plain", 404);
}

std::string MetricsHttpServer::BuildResponse(const std::string& body,
                                            const std::string& content_type,
                                            int status_code) {
  std::ostringstream ss;
  ss << "HTTP/1.1 " << status_code << " " << StatusText(status_code) << "\r\n";
  ss << "Content-Type: " << content_type << "\r\n";
  ss << "Content-Length: " << body.length() << "\r\n";
  ss << "Connection: close\r\n";
  ss << "\r\n";
  ss << body;
  return ss.str();
}

std::string MetricsHttpServer::StatusText(int status_code) {
  switch (status_code) {
    case 200: return "OK";
    case 404: return "Not Found";
    case 405: return "Method Not Allowed";
    case 500: return "Internal Server Error";
    default: return "Unknown";
  }
}

} // namespace common
} // namespace chirp
