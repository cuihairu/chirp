// Unit tests for the common metrics library: SimpleMetrics collectors,
// the Prometheus text export, the LatencyTracker/ScopedGauge helpers and
// the mini HTTP server that exposes /metrics.

#include <gtest/gtest.h>

#include <asio.hpp>

#include "common/metrics.h"
#include "common/metrics_http_server.h"

#include <chrono>
#include <future>
#include <thread>

namespace {

using chirp::common::LatencyTracker;
using chirp::common::MetricsHttpServer;
using chirp::common::ScopedGaugeIncrement;
using chirp::common::SimpleMetrics;

class SimpleMetricsTest : public ::testing::Test {
 protected:
  void SetUp() override {
    // Use fresh names per test: the singleton accumulates state.
    counter_name_ = "chirp_test_counter_" + std::to_string(++seq_);
    gauge_name_ = "chirp_test_gauge_" + std::to_string(seq_);
    histogram_name_ = "chirp_test_histogram_" + std::to_string(seq_);
  }

  static int seq_;
  std::string counter_name_;
  std::string gauge_name_;
  std::string histogram_name_;
};

int SimpleMetricsTest::seq_ = 0;

TEST_F(SimpleMetricsTest, CountersAddUp) {
  auto* counter = SimpleMetrics::Instance().GetCounter(counter_name_);
  counter->Increment();
  counter->Add(41);
  EXPECT_EQ(counter->Get(), 42u);
  EXPECT_EQ(SimpleMetrics::Instance().GetCounter(counter_name_)->Get(), 42u);
}

TEST_F(SimpleMetricsTest, GaugesMoveBothWays) {
  auto* gauge = SimpleMetrics::Instance().GetGauge(gauge_name_);
  gauge->Set(10);
  gauge->Increment();
  gauge->Add(5);
  gauge->Decrement();
  EXPECT_EQ(gauge->Get(), 15);
}

TEST_F(SimpleMetricsTest, HistogramsBucketObservations) {
  auto* hist = SimpleMetrics::Instance().GetHistogram(histogram_name_);
  hist->Observe(3);
  hist->Observe(7);
  hist->Observe(100000);  // above every bucket -> no bucket increments
  EXPECT_EQ(hist->GetCount(), 3u);
  EXPECT_EQ(hist->GetSum(), 100010u);
}

TEST_F(SimpleMetricsTest, ExportPrometheusRendersAllTypes) {
  SimpleMetrics::Instance().GetCounter(counter_name_)->Add(5);
  SimpleMetrics::Instance().GetGauge(gauge_name_)->Set(7);
  SimpleMetrics::Instance().GetHistogram(histogram_name_)->Observe(1);

  const std::string text = SimpleMetrics::Instance().ExportPrometheus();
  EXPECT_NE(text.find("# TYPE " + counter_name_ + " counter"), std::string::npos);
  EXPECT_NE(text.find(counter_name_ + " 5"), std::string::npos);
  EXPECT_NE(text.find("# TYPE " + gauge_name_ + " gauge"), std::string::npos);
  EXPECT_NE(text.find(gauge_name_ + " 7"), std::string::npos);
  EXPECT_NE(text.find("# TYPE " + histogram_name_ + " histogram"), std::string::npos);
  EXPECT_NE(text.find(histogram_name_ + "_count 1"), std::string::npos);
  EXPECT_NE(text.find(histogram_name_ + "_bucket{le=\"+Inf\"} 1"), std::string::npos);
}

TEST_F(SimpleMetricsTest, HelpersObserveOnScopeExit) {
  {
    LatencyTracker tracker(histogram_name_);
    EXPECT_GE(tracker.ElapsedMs(), 0.0);
    CHIRP_COUNTER(counter_name_, 3);
    CHIRP_GAUGE_SET(gauge_name_, 9);
    CHIRP_GAUGE_INC(gauge_name_);
    CHIRP_GAUGE_DEC(gauge_name_);
    CHIRP_HISTOGRAM(histogram_name_, 4);
  }
  EXPECT_EQ(SimpleMetrics::Instance().GetCounter(counter_name_)->Get(), 3u);
  EXPECT_EQ(SimpleMetrics::Instance().GetGauge(gauge_name_)->Get(), 9);
  EXPECT_EQ(SimpleMetrics::Instance().GetHistogram(histogram_name_)->GetCount(), 2u);

  {
    ScopedGaugeIncrement scoped(gauge_name_);
    EXPECT_EQ(SimpleMetrics::Instance().GetGauge(gauge_name_)->Get(), 10);
    CHIRP_SCOPED_GAUGE(other_gauge);
    EXPECT_EQ(SimpleMetrics::Instance().GetGauge("other_gauge")->Get(), 1);
  }
  EXPECT_EQ(SimpleMetrics::Instance().GetGauge(gauge_name_)->Get(), 9);
  EXPECT_EQ(SimpleMetrics::Instance().GetGauge("other_gauge")->Get(), 0);
}

// ---------------------------------------------------------------------------
// MetricsHttpServer
// ---------------------------------------------------------------------------

class MetricsHttpServerTest : public ::testing::Test {
 protected:
  void SetUp() override {
    server_ = std::make_unique<MetricsHttpServer>(io_, 0);
    EXPECT_TRUE(server_->Start());
    EXPECT_TRUE(server_->IsRunning());
    EXPECT_GT(server_->port(), 0u);
    runner_ = std::thread([this] { io_.run(); });
  }

  void TearDown() override {
    StopServer();
    io_.stop();
    if (runner_.joinable()) {
      runner_.join();
    }
  }

  // Stops the server and waits until the acceptor-close lambda it posts to
  // the io context has actually run, so io_.stop() cannot race it.
  void StopServer() {
    server_->Stop();
    std::promise<void> drained;
    asio::post(io_, [&drained] { drained.set_value(); });
    // Handlers posted on the same io context run in order: once the sentinel
    // fires, the close lambda posted by Stop() has already executed.
    drained.get_future().wait_for(std::chrono::milliseconds(1000));
  }

  // Sends one HTTP request on its own io context and returns the response.
  std::string Request(const std::string& raw) {
    asio::io_context io;
    asio::ip::tcp::socket sock(io);
    sock.connect(asio::ip::tcp::endpoint(asio::ip::address_v4::loopback(),
                                         server_->port()));
    asio::write(sock, asio::buffer(raw));
    std::string response;
    char buf[4096];
    asio::error_code ec;
    while (true) {
      size_t n = sock.read_some(asio::buffer(buf), ec);
      if (ec) break;
      response.append(buf, n);
    }
    return response;
  }

  asio::io_context io_;
  std::unique_ptr<MetricsHttpServer> server_;
  std::thread runner_;
};

TEST_F(MetricsHttpServerTest, StartStopAndIdempotence) {
  EXPECT_TRUE(server_->Start());  // already running -> still true
  server_->Stop();
  EXPECT_FALSE(server_->IsRunning());
  server_->Stop();  // idempotent
}

TEST_F(MetricsHttpServerTest, EndpointsServeExpectedBodies) {
  // /metrics serves the default SimpleMetrics export.
  const std::string metrics = Request("GET /metrics HTTP/1.1\r\n\r\n");
  EXPECT_NE(metrics.find("200"), std::string::npos);

  const std::string health = Request("GET /health HTTP/1.1\r\n\r\n");
  EXPECT_NE(health.find("200"), std::string::npos);
  EXPECT_NE(health.find("OK"), std::string::npos);

  // Custom routes are honored.
  server_->AddRoute("/custom", [](const std::string&) { return "{\"ok\":true}"; });
  const std::string custom = Request("GET /custom HTTP/1.1\r\n\r\n");
  EXPECT_NE(custom.find("{\"ok\":true}"), std::string::npos);

  // Unknown paths 404; non-GET methods 405.
  EXPECT_NE(Request("GET /nope HTTP/1.1\r\n\r\n").find("404"), std::string::npos);
  EXPECT_NE(Request("POST /metrics HTTP/1.1\r\n\r\n").find("405"), std::string::npos);
}

TEST_F(MetricsHttpServerTest, AbortedRequestDoesNotKillServer) {
  // A client that hangs up mid-request drives the read-error branch of the
  // request handler.
  {
    asio::io_context io;
    asio::ip::tcp::socket sock(io);
    sock.connect(
        asio::ip::tcp::endpoint(asio::ip::address_v4::loopback(), server_->port()));
    asio::write(sock, asio::buffer("GET /half HTTP/1.1\r\n"));  // no final CRLF
  }  // socket closed here -> async_read_until errors out server-side
  std::this_thread::sleep_for(std::chrono::milliseconds(100));

  // The server must keep serving after the aborted connection.
  EXPECT_NE(Request("GET /health HTTP/1.1\r\n\r\n").find("200"), std::string::npos);
}

}  // namespace
