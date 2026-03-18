#include "common/metrics.h"

#include <sstream>
#include <algorithm>

namespace chirp {
namespace common {

#ifdef CHIRP_USE_PROMETHEUS

Metrics& Metrics::Instance() {
  static Metrics instance;
  return instance;
}

Metrics::Metrics()
    : registry_(std::make_shared<prometheus::Registry>()) {}

Metrics::~Metrics() = default;

void Metrics::CounterIncrement(const std::string& name, double value) {
  CounterIncrement(name, {}, value);
}

void Metrics::CounterIncrement(const std::string& name,
                              const std::unordered_map<std::string, std::string>& labels,
                              double value) {
  auto& family = families_[name];
  if (!family.counter) {
    family.counter = prometheus::BuildCounter()
        .Name(name)
        .Help(name)
        .Register(*registry_);
  }
  family.counter->Add(labels).Increment(value);
}

void Metrics::GaugeSet(const std::string& name, double value) {
  GaugeSet(name, {}, value);
}

void Metrics::GaugeIncrement(const std::string& name, double delta) {
  auto& family = families_[name];
  if (!family.gauge) {
    family.gauge = prometheus::BuildGauge()
        .Name(name)
        .Help(name)
        .Register(*registry_);
  }
  family.gauge->Add({}).Increment(delta);
}

void Metrics::GaugeDecrement(const std::string& name, double delta) {
  GaugeIncrement(name, -delta);
}

void Metrics::GaugeSet(const std::string& name,
                       const std::unordered_map<std::string, std::string>& labels,
                       double value) {
  auto& family = families_[name];
  if (!family.gauge) {
    family.gauge = prometheus::BuildGauge()
        .Name(name)
        .Help(name)
        .Register(*registry_);
  }
  family.gauge->Add(labels).Set(value);
}

void Metrics::HistogramObserve(const std::string& name, double value) {
  HistogramObserve(name, {}, value);
}

void Metrics::HistogramObserve(const std::string& name,
                               const std::unordered_map<std::string, std::string>& labels,
                               double value) {
  auto& family = families_[name];
  if (!family.histogram) {
    family.histogram = prometheus::BuildHistogram()
        .Name(name)
        .Help(name)
        .Register(*registry_);
  }
  family.histogram->Add(labels).Observe(value);
}

void Metrics::SummaryObserve(const std::string& name, double value) {
  SummaryObserve(name, {}, value);
}

void Metrics::SummaryObserve(const std::string& name,
                             const std::unordered_map<std::string, std::string>& labels,
                             double value) {
  auto& family = families_[name];
  if (!family.summary) {
    family.summary = prometheus::BuildSummary()
        .Name(name)
        .Help(name)
        .Register(*registry_);
  }
  family.summary->Add(labels).Observe(value);
}

std::string Metrics::Export() {
  std::ostringstream ss;
  prometheus::TextSerializer::Serialize(ss, registry_->Collect());
  return ss.str();
}

std::string Metrics::GetMetricsEndpoint() {
  return Export();
}

#endif // CHIRP_USE_PROMETHEUS

// SimpleMetrics implementation

SimpleMetrics& SimpleMetrics::Instance() {
  static SimpleMetrics instance;
  return instance;
}

SimpleMetrics::Counter* SimpleMetrics::GetCounter(const std::string& name) {
  std::lock_guard<std::mutex> lock(mutex_);
  return &counters_[name];
}

SimpleMetrics::Gauge* SimpleMetrics::GetGauge(const std::string& name) {
  std::lock_guard<std::mutex> lock(mutex_);
  return &gauges_[name];
}

SimpleMetrics::Histogram* SimpleMetrics::GetHistogram(const std::string& name) {
  std::lock_guard<std::mutex> lock(mutex_);
  return &histograms_[name];
}

std::string SimpleMetrics::ExportPrometheus() {
  std::lock_guard<std::mutex> lock(mutex_);
  std::ostringstream ss;

  // Export counters
  for (const auto& [name, counter] : counters_) {
    ss << "# TYPE " << name << " counter\n";
    ss << name << " " << counter.Get() << "\n";
  }

  // Export gauges
  for (const auto& [name, gauge] : gauges_) {
    ss << "# TYPE " << name << " gauge\n";
    ss << name << " " << gauge.Get() << "\n";
  }

  // Export histograms
  for (const auto& [name, hist] : histograms_) {
    ss << "# TYPE " << name << " histogram\n";
    ss << name << "_count " << hist.GetCount() << "\n";
    ss << name << "_sum " << hist.GetSum() << "\n";

    uint64_t prev = 0;
    const uint64_t buckets[] = {1, 5, 10, 25, 50, 100, 250, 500, 1000, 2500, 5000, 10000};
    for (size_t i = 0; i < Histogram::kNumBuckets; ++i) {
      uint64_t count = hist.buckets[i].load();
      ss << name << "_bucket{le=\"" << buckets[i] << "\"} " << count << "\n";
      prev = count;
    }
    ss << name << "_bucket{le=\"+Inf\"} " << hist.GetCount() << "\n";
  }

  return ss.str();
}

} // namespace common
} // namespace chirp
