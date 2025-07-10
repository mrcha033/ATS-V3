#pragma once

#include <string>
#include <memory>
#include <chrono>
#include <atomic>
#include <vector>
#include <unordered_map>
#include <mutex>
#include <thread>
#include <nlohmann/json.hpp>

namespace ats {
namespace price_collector {

// Performance metrics structure
struct PerformanceMetrics {
    // Message throughput
    std::atomic<size_t> messages_received{0};
    std::atomic<size_t> messages_processed{0};
    std::atomic<size_t> messages_per_second{0};
    
    // Latency metrics (in milliseconds)
    std::atomic<double> avg_processing_latency{0.0};
    std::atomic<double> avg_network_latency{0.0};
    std::atomic<double> avg_storage_latency{0.0};
    std::atomic<double> p95_processing_latency{0.0};
    std::atomic<double> p99_processing_latency{0.0};
    
    // Error rates
    std::atomic<size_t> total_errors{0};
    std::atomic<size_t> network_errors{0};
    std::atomic<size_t> parsing_errors{0};
    std::atomic<size_t> storage_errors{0};
    std::atomic<double> error_rate_percent{0.0};
    
    // Resource utilization
    std::atomic<double> cpu_usage_percent{0.0};
    std::atomic<double> memory_usage_mb{0.0};
    std::atomic<double> network_bandwidth_mbps{0.0};
    std::atomic<size_t> queue_size{0};
    std::atomic<double> queue_utilization_percent{0.0};
    
    // Exchange-specific metrics
    std::unordered_map<std::string, size_t> messages_per_exchange;
    std::unordered_map<std::string, double> latency_per_exchange;
    std::unordered_map<std::string, size_t> errors_per_exchange;
    
    // Time tracking
    std::chrono::system_clock::time_point start_time;
    std::atomic<std::chrono::milliseconds> uptime{std::chrono::milliseconds(0)};
    
    PerformanceMetrics() {
        start_time = std::chrono::system_clock::now();
    }
};

// CPU and memory monitoring
class SystemResourceMonitor {
public:
    SystemResourceMonitor();
    ~SystemResourceMonitor();
    
    // Resource monitoring
    double get_cpu_usage_percent() const;
    double get_memory_usage_mb() const;
    double get_memory_usage_percent() const;
    double get_available_memory_mb() const;
    
    // Process-specific monitoring
    double get_process_cpu_usage() const;
    double get_process_memory_mb() const;
    size_t get_thread_count() const;
    size_t get_handle_count() const;
    
    // Network monitoring
    double get_network_rx_mbps() const;
    double get_network_tx_mbps() const;
    double get_network_total_mbps() const;
    
    // Disk I/O monitoring
    double get_disk_read_mbps() const;
    double get_disk_write_mbps() const;
    
    // System information
    std::string get_os_version() const;
    std::string get_cpu_model() const;
    size_t get_total_memory_mb() const;
    size_t get_cpu_core_count() const;
    
    // Start/stop monitoring
    void start_monitoring();
    void stop_monitoring();
    bool is_monitoring() const;
    
private:
    struct Implementation;
    std::unique_ptr<Implementation> impl_;
    
    void update_metrics();
    void monitoring_thread_main();
};

// Latency tracking with percentile calculations
class LatencyTracker {
public:
    LatencyTracker(size_t sample_size = 10000);
    ~LatencyTracker();
    
    // Record latency measurement
    void record_latency(std::chrono::milliseconds latency);
    void record_latency_microseconds(std::chrono::microseconds latency);
    
    // Get statistics
    double get_average_latency_ms() const;
    double get_median_latency_ms() const;
    double get_p95_latency_ms() const;
    double get_p99_latency_ms() const;
    double get_max_latency_ms() const;
    double get_min_latency_ms() const;
    
    // Sample management
    size_t get_sample_count() const;
    void clear_samples();
    void set_sample_size(size_t size);
    
    // Percentile calculation
    double get_percentile(double percentile) const;
    std::vector<double> get_latency_distribution() const;
    
private:
    struct Implementation;
    std::unique_ptr<Implementation> impl_;
    
    void update_statistics();
    double calculate_percentile(const std::vector<double>& sorted_samples, double percentile) const;
};

// Throughput measurement
class ThroughputMeter {
public:
    ThroughputMeter(std::chrono::seconds window = std::chrono::seconds(60));
    ~ThroughputMeter();
    
    // Record events
    void record_event();
    void record_events(size_t count);
    
    // Get throughput measurements
    double get_events_per_second() const;
    double get_events_per_minute() const;
    size_t get_total_events() const;
    
    // Window management
    void set_measurement_window(std::chrono::seconds window);
    std::chrono::seconds get_measurement_window() const;
    
    // Reset counters
    void reset();
    
private:
    struct Implementation;
    std::unique_ptr<Implementation> impl_;
    
    void cleanup_old_events();
};

// Main performance monitor class
class PerformanceMonitor {
public:
    PerformanceMonitor();
    ~PerformanceMonitor();
    
    // Lifecycle management
    bool start();
    void stop();
    bool is_running() const;
    
    // Metric recording
    void record_message_received(const std::string& exchange = "");
    void record_message_processed(const std::string& exchange = "");
    void record_processing_latency(std::chrono::milliseconds latency, const std::string& exchange = "");
    void record_network_latency(std::chrono::milliseconds latency, const std::string& exchange = "");
    void record_storage_latency(std::chrono::milliseconds latency, const std::string& exchange = "");
    
    // Error recording
    void record_error(const std::string& error_type, const std::string& exchange = "");
    void record_network_error(const std::string& exchange = "");
    void record_parsing_error(const std::string& exchange = "");
    void record_storage_error(const std::string& exchange = "");
    
    // Queue monitoring
    void update_queue_size(size_t current_size, size_t max_size);
    
    // Performance metrics retrieval
    PerformanceMetrics get_current_metrics() const;
    nlohmann::json get_metrics_json() const;
    std::string generate_performance_report() const;
    
    // Exchange-specific metrics
    nlohmann::json get_exchange_metrics(const std::string& exchange) const;
    std::vector<std::string> get_monitored_exchanges() const;
    
    // Historical data
    std::vector<PerformanceMetrics> get_metrics_history(std::chrono::minutes duration) const;
    void enable_historical_tracking(bool enable, std::chrono::seconds interval = std::chrono::seconds(60));
    
    // Alerting thresholds
    void set_cpu_threshold(double percentage);
    void set_memory_threshold(double mb);
    void set_latency_threshold(std::chrono::milliseconds threshold);
    void set_error_rate_threshold(double percentage);
    void set_queue_threshold(double utilization_percentage);
    
    // Alert callback
    using AlertCallback = std::function<void(const std::string& alert_type, const std::string& message, double value)>;
    void set_alert_callback(AlertCallback callback);
    
    // Configuration
    void set_monitoring_interval(std::chrono::seconds interval);
    void enable_detailed_exchange_monitoring(bool enable);
    void enable_system_resource_monitoring(bool enable);
    
    // Health check
    bool is_healthy() const;
    std::vector<std::string> get_health_issues() const;
    
    // Reset and maintenance
    void reset_metrics();
    void reset_exchange_metrics(const std::string& exchange);
    
private:
    struct Implementation;
    std::unique_ptr<Implementation> impl_;
    
    // Internal monitoring
    void monitoring_thread_main();
    void update_derived_metrics();
    void check_alert_thresholds();
    void store_historical_metrics();
    
    // Alert handling
    void trigger_alert(const std::string& alert_type, const std::string& message, double value);
    
    // Metric calculations
    double calculate_error_rate() const;
    double calculate_queue_utilization() const;
    void update_throughput_metrics();
};

// Performance benchmark utilities
class PerformanceBenchmark {
public:
    PerformanceBenchmark(const std::string& benchmark_name);
    ~PerformanceBenchmark();
    
    // Benchmark execution
    template<typename Func>
    auto measure_execution_time(Func&& func) -> decltype(func());
    
    template<typename Func>
    void benchmark_function(Func&& func, size_t iterations);
    
    // Results
    std::chrono::microseconds get_average_execution_time() const;
    std::chrono::microseconds get_min_execution_time() const;
    std::chrono::microseconds get_max_execution_time() const;
    size_t get_iterations_count() const;
    
    // Throughput benchmarking
    double measure_throughput_ops_per_second(std::function<void()> operation, 
                                           std::chrono::seconds duration);
    
    // Memory benchmarking
    size_t measure_memory_usage(std::function<void()> operation);
    
    // Report generation
    std::string generate_benchmark_report() const;
    nlohmann::json get_benchmark_results() const;
    
private:
    struct Implementation;
    std::unique_ptr<Implementation> impl_;
    
    void record_execution_time(std::chrono::microseconds time);
};

// Utility functions for performance monitoring
namespace performance_utils {
    
    // Time measurement utilities
    class ScopedTimer {
    public:
        ScopedTimer(std::chrono::microseconds& result);
        ScopedTimer(std::function<void(std::chrono::microseconds)> callback);
        ~ScopedTimer();
        
    private:
        std::chrono::high_resolution_clock::time_point start_time_;
        std::chrono::microseconds* result_ptr_;
        std::function<void(std::chrono::microseconds)> callback_;
    };
    
    // CPU usage calculation
    double calculate_cpu_usage();
    double get_process_cpu_usage();
    
    // Memory usage calculation
    size_t get_process_memory_usage();
    size_t get_system_memory_usage();
    size_t get_available_system_memory();
    
    // Network monitoring
    struct NetworkStats {
        size_t bytes_received;
        size_t bytes_sent;
        size_t packets_received;
        size_t packets_sent;
        std::chrono::system_clock::time_point timestamp;
    };
    
    NetworkStats get_network_stats();
    double calculate_bandwidth_mbps(const NetworkStats& before, const NetworkStats& after);
    
    // Performance reporting
    std::string format_bytes(size_t bytes);
    std::string format_duration(std::chrono::microseconds duration);
    std::string format_percentage(double percentage);
    std::string format_throughput(double ops_per_second);
    
    // Performance testing helpers
    void warm_up_cpu(std::chrono::milliseconds duration);
    void stress_test_memory(size_t mb_to_allocate);
    void benchmark_storage_write(const std::string& storage_path, size_t data_size);
    void benchmark_storage_read(const std::string& storage_path, size_t data_size);
    
    // Health check utilities
    bool check_cpu_health(double threshold_percentage = 90.0);
    bool check_memory_health(double threshold_percentage = 90.0);
    bool check_disk_health(const std::string& path, double threshold_percentage = 90.0);
    bool check_network_health();
}

// Macros for easy performance measurement
#define MEASURE_EXECUTION_TIME(var) \
    performance_utils::ScopedTimer _timer(var)

#define MEASURE_EXECUTION_TIME_CALLBACK(callback) \
    performance_utils::ScopedTimer _timer(callback)

#define BENCHMARK_FUNCTION(func, iterations) \
    PerformanceBenchmark _benchmark(__FUNCTION__); \
    _benchmark.benchmark_function(func, iterations)

} // namespace price_collector
} // namespace ats