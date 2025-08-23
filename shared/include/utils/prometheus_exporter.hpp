#pragma once

#include <memory>
#include <string>
#include <chrono>
#include <unordered_map>

namespace ats {
namespace utils {

// Simplified prometheus exporter interface for fallback implementation
class PrometheusExporter {
public:
    PrometheusExporter();
    ~PrometheusExporter();

    // Service lifecycle
    bool start(const std::string& endpoint = "0.0.0.0", int port = 8080);
    void stop();

    // Simple metrics interface
    void increment_counter(const std::string& name, const std::unordered_map<std::string, std::string>& labels = {});
    void set_gauge(const std::string& name, double value, const std::unordered_map<std::string, std::string>& labels = {});
    void observe_histogram(const std::string& name, double value, const std::unordered_map<std::string, std::string>& labels = {});

private:
    class Impl;
    std::unique_ptr<Impl> impl_;
};

// RAII timer for measuring operation duration
class PrometheusTimer {
public:
    explicit PrometheusTimer(PrometheusExporter& exporter, const std::string& operation)
        : exporter_(exporter)
        , operation_(operation)
        , start_time_(std::chrono::high_resolution_clock::now()) {}

    ~PrometheusTimer() {
        auto end_time = std::chrono::high_resolution_clock::now();
        auto duration = std::chrono::duration_cast<std::chrono::microseconds>(
            end_time - start_time_).count();
        exporter_.observe_histogram(operation_ + "_duration_ms", duration / 1000.0);
    }

private:
    PrometheusExporter& exporter_;
    std::string operation_;
    std::chrono::high_resolution_clock::time_point start_time_;
};

// Convenient macros for Prometheus monitoring
#define PROMETHEUS_TIMER(exporter, operation) \
    ats::utils::PrometheusTimer timer(exporter, operation)

} // namespace utils
} // namespace ats