#ifndef CHIRP_COMMON_METRICS_H_
#define CHIRP_COMMON_METRICS_H_

#include <atomic>
#include <chrono>
#include <functional>
#include <memory>
#include <string>
#include <unordered_map>

#ifdef CHIRP_USE_PROMETHEUS
#include <prometheus/registry.h>
#include <prometheus/gauge.h>
#include <prometheus/counter.h>
#include <prometheus/histogram.h>
#include <prometheus/summary.h>
#include <prometheus/text_serializer.h>
#endif

namespace chirp {
namespace common {

// Thread-safe metrics collector
class Metrics {
public:
  static Metrics& Instance();

  // Counter - monotonically increasing value
  void CounterIncrement(const std::string& name, double value = 1.0);
  void CounterIncrement(const std::string& name, const std::unordered_map<std::string, std::string>& labels, double value = 1.0);

  // Gauge - can go up and down
  void GaugeSet(const std::string& name, double value);
  void GaugeIncrement(const std::string& name, double delta = 1.0);
  void GaugeDecrement(const std::string& name, double delta = 1.0);
  void GaugeSet(const std::string& name, const std::unordered_map<std::string, std::string>& labels, double value);

  // Histogram - count observations in buckets
  void HistogramObserve(const std::string& name, double value);
  void HistogramObserve(const std::string& name, const std::unordered_map<std::string, std::string>& labels, double value);

  // Summary - calculate quantiles
  void SummaryObserve(const std::string& name, double value);
  void SummaryObserve(const std::string& name, const std::unordered_map<std::string, std::string>& labels, double value);

  // Export metrics in Prometheus text format
  std::string Export();

  // HTTP endpoint handler for /metrics
  std::string GetMetricsEndpoint();

private:
  Metrics();
  ~Metrics();

#ifdef CHIRP_USE_PROMETHEUS
  std::shared_ptr<prometheus::Registry> registry_;

  struct FamilyPointers {
    std::shared_ptr<prometheus::Counter::Family> counter;
    std::shared_ptr<prometheus::Gauge::Family> gauge;
    std::shared_ptr<prometheus::Histogram::Family> histogram;
    std::shared_ptr<prometheus::Summary::Family> summary;
  };

  std::unordered_map<std::string, FamilyPointers> families_;
#endif
};

// Simple metrics (no Prometheus dependency)
class SimpleMetrics {
public:
  struct Counter {
    std::atomic<uint64_t> value{0};
    void Increment() { ++value; }
    void Add(uint64_t delta) { value += delta; }
    uint64_t Get() const { return value.load(); }
  };

  struct Gauge {
    std::atomic<int64_t> value{0};
    void Set(int64_t v) { value = v; }
    void Increment() { ++value; }
    void Decrement() { --value; }
    void Add(int64_t delta) { value += delta; }
    int64_t Get() const { return value.load(); }
  };

  struct Histogram {
    std::atomic<uint64_t> count{0};
    std::atomic<uint64_t> sum{0};
    static constexpr size_t kNumBuckets = 12;
    std::atomic<uint64_t> buckets[kNumBuckets]{
      1, 5, 10, 25, 50, 100, 250, 500, 1000, 2500, 5000, 10000
    };

    void Observe(int64_t value) {
      ++count;
      sum += value;
      // Find bucket
      for (size_t i = 0; i < kNumBuckets; ++i) {
        if (value <= buckets[i].load()) {
          ++buckets[i];
          break;
        }
      }
    }

    uint64_t GetCount() const { return count.load(); }
    uint64_t GetSum() const { return sum.load(); }
  };

  static SimpleMetrics& Instance();

  Counter* GetCounter(const std::string& name);
  Gauge* GetGauge(const std::string& name);
  Histogram* GetHistogram(const std::string& name);

  std::string ExportPrometheus();

private:
  SimpleMetrics() = default;

  std::unordered_map<std::string, Counter> counters_;
  std::unordered_map<std::string, Gauge> gauges_;
  std::unordered_map<std::string, Histogram> histograms_;

  mutable std::mutex mutex_;
};

// Latency tracker helper
class LatencyTracker {
public:
  LatencyTracker(const std::string& name,
                const std::unordered_map<std::string, std::string> labels = {})
      : name_(name), labels_(labels),
        start_(std::chrono::steady_clock::now()) {}

  ~LatencyTracker() {
    auto elapsed = std::chrono::duration_cast<std::chrono::microseconds>(
        std::chrono::steady_clock::now() - start_).count();
    SimpleMetrics::Instance().GetHistogram(name_)->Observe(elapsed);
  }

  double ElapsedMs() const {
    auto elapsed = std::chrono::duration_cast<std::chrono::microseconds>(
        std::chrono::steady_clock::now() - start_).count();
    return elapsed / 1000.0;
  }

private:
  std::string name_;
  std::unordered_map<std::string, std::string> labels_;
  std::chrono::steady_clock::time_point start_;
};

// RAII helper for gauges
class ScopedGaugeIncrement {
public:
  ScopedGaugeIncrement(const std::string& name)
      : name_(name) {
    SimpleMetrics::Instance().GetGauge(name_)->Increment();
  }

  ~ScopedGaugeIncrement() {
    SimpleMetrics::Instance().GetGauge(name_)->Decrement();
  }

private:
  std::string name_;
};

} // namespace common
} // namespace chirp

// Convenience macros
#define CHIRP_COUNTER(name, value) \
  chirp::common::SimpleMetrics::Instance().GetCounter(name)->Add(value)

#define CHIRP_GAUGE_SET(name, value) \
  chirp::common::SimpleMetrics::Instance().GetGauge(name)->Set(value)

#define CHIRP_GAUGE_INC(name) \
  chirp::common::SimpleMetrics::Instance().GetGauge(name)->Increment()

#define CHIRP_GAUGE_DEC(name) \
  chirp::common::SimpleMetrics::Instance().GetGauge(name)->Decrement()

#define CHIRP_HISTOGRAM(name, value) \
  chirp::common::SimpleMetrics::Instance().GetHistogram(name)->Observe(value)

#define CHIRP_LATENCY_TRACKER(name) \
  chirp::common::LatencyTracker _latency_tracker_##name(#name)

#define CHIRP_SCOPED_GAUGE(name) \
  chirp::common::ScopedGaugeIncrement _scoped_gauge_##name(#name)

#endif // CHIRP_COMMON_METRICS_H_
