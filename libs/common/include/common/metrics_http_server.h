#ifndef CHIRP_COMMON_METRICS_HTTP_SERVER_H_
#define CHIRP_COMMON_METRICS_HTTP_SERVER_H_

#include <atomic>
#include <functional>
#include <memory>
#include <string>
#include <thread>
#include <unordered_map>

#include <asio.hpp>

namespace chirp {
namespace common {

// Simple HTTP server for exposing Prometheus metrics endpoint
class MetricsHttpServer {
public:
  using RouteHandler = std::function<std::string(const std::string& path)>;

  MetricsHttpServer(asio::io_context& io, uint16_t port = 9090);
  ~MetricsHttpServer();

  // Start the server
  bool Start();

  // Stop the server
  void Stop();

  // Set custom handler for /metrics endpoint
  void SetMetricsHandler(std::function<std::string()> handler) {
    metrics_handler_ = std::move(handler);
  }

  // Add custom route
  void AddRoute(const std::string& path, RouteHandler handler);

  bool IsRunning() const { return running_.load(); }

private:
  void AcceptConnection();
  void HandleConnection(std::shared_ptr<asio::ip::tcp::socket> socket);
  std::string HandleRequest(const std::string& request);
  std::string BuildResponse(const std::string& body,
                           const std::string& content_type = "text/plain",
                           int status_code = 200);
  std::string StatusText(int status_code);

  asio::io_context& io_;
  uint16_t port_;
  std::atomic<bool> running_;
  asio::ip::tcp::acceptor acceptor_;

  std::function<std::string()> metrics_handler_;
  std::unordered_map<std::string, RouteHandler> routes_;
};

} // namespace common
} // namespace chirp

#endif // CHIRP_COMMON_METRICS_HTTP_SERVER_H_
