#pragma once

#include "performance_metrics.hpp"
#include "backtest_engine.hpp"
#include <string>
#include <vector>
#include <chrono>
#include <memory>
#include <unordered_map>

namespace ats {
namespace backtest {

// InfluxDB connection configuration
struct InfluxDBConfig {
    std::string url = "http://localhost:8086";
    std::string database = "backtest_results";
    std::string username;
    std::string password;
    std::string token;                          // For InfluxDB 2.x
    std::string organization = "ats";           // For InfluxDB 2.x
    std::string bucket = "backtest_results";    // For InfluxDB 2.x
    
    // Connection settings
    int timeout_seconds = 30;
    int max_retries = 3;
    int retry_delay_ms = 1000;
    bool use_ssl = false;
    
    // Batch settings
    int batch_size = 1000;
    int flush_interval_seconds = 10;
    bool enable_compression = true;
    
    // Data retention
    std::string retention_policy = "default";
    int retention_days = 90;
};

// Data point for InfluxDB storage
struct InfluxDataPoint {
    std::string measurement;
    std::unordered_map<std::string, std::string> tags;
    std::unordered_map<std::string, double> fields_double;
    std::unordered_map<std::string, int64_t> fields_int;
    std::unordered_map<std::string, std::string> fields_string;
    std::chrono::system_clock::time_point timestamp;
    
    InfluxDataPoint() = default;
    InfluxDataPoint(const std::string& measurement_name)
        : measurement(measurement_name), timestamp(std::chrono::system_clock::now()) {}
    
    // Helper methods
    void add_tag(const std::string& key, const std::string& value);
    void add_field(const std::string& key, double value);
    void add_field(const std::string& key, int64_t value);
    void add_field(const std::string& key, const std::string& value);
    
    // Convert to InfluxDB line protocol
    std::string to_line_protocol() const;
};

// Query result from InfluxDB
struct InfluxQueryResult {
    std::vector<std::vector<std::string>> columns;
    std::vector<std::vector<std::string>> values;
    std::string error;
    bool success = false;
    
    // Helper methods
    bool has_data() const { return !values.empty(); }
    size_t row_count() const { return values.size(); }
    size_t column_count() const { return columns.empty() ? 0 : columns[0].size(); }
    
    // Get column index by name
    int get_column_index(const std::string& column_name) const;
    
    // Get value by row and column
    std::string get_value(size_t row, const std::string& column_name) const;
    double get_double_value(size_t row, const std::string& column_name) const;
    int64_t get_int_value(size_t row, const std::string& column_name) const;
};

// InfluxDB client for storing backtest results
class InfluxDBStorage {
public:
    InfluxDBStorage();
    ~InfluxDBStorage();
    
    // Connection management
    bool initialize(const InfluxDBConfig& config);
    bool connect();
    void disconnect();
    bool is_connected() const;
    
    // Configuration
    void set_config(const InfluxDBConfig& config);
    InfluxDBConfig get_config() const;
    
    // Database operations
    bool create_database(const std::string& database_name = "");
    bool drop_database(const std::string& database_name = "");
    bool database_exists(const std::string& database_name = "") const;
    
    // Data writing
    bool write_backtest_result(const BacktestResult& result, 
                              const std::string& strategy_name = "",
                              const std::unordered_map<std::string, std::string>& additional_tags = {});
    
    bool write_performance_metrics(const PerformanceMetrics& metrics,
                                  const std::string& strategy_name,
                                  const std::unordered_map<std::string, std::string>& tags = {});
    
    bool write_trade_results(const std::vector<TradeResult>& trades,
                            const std::string& strategy_name,
                            const std::unordered_map<std::string, std::string>& tags = {});
    
    bool write_portfolio_history(const std::vector<PortfolioSnapshot>& portfolio_history,
                                const std::string& strategy_name,
                                const std::unordered_map<std::string, std::string>& tags = {});
    
    // Batch operations
    bool write_data_points(const std::vector<InfluxDataPoint>& points);
    bool write_single_point(const InfluxDataPoint& point);
    
    // Querying
    InfluxQueryResult query(const std::string& query);
    
    // Pre-built queries
    InfluxQueryResult get_backtest_results(const std::string& strategy_name = "",
                                          std::chrono::system_clock::time_point start_time = {},
                                          std::chrono::system_clock::time_point end_time = {},
                                          int limit = 0);
    
    InfluxQueryResult get_performance_metrics(const std::string& strategy_name = "",
                                             std::chrono::system_clock::time_point start_time = {},
                                             std::chrono::system_clock::time_point end_time = {});
    
    InfluxQueryResult get_trade_history(const std::string& symbol = "",
                                       const std::string& strategy_name = "",
                                       std::chrono::system_clock::time_point start_time = {},
                                       std::chrono::system_clock::time_point end_time = {},
                                       int limit = 1000);
    
    // Analytics queries
    InfluxQueryResult get_strategy_comparison(const std::vector<std::string>& strategy_names,
                                             const std::string& metric = "total_return");
    
    InfluxQueryResult get_rolling_performance(const std::string& strategy_name,
                                             int window_days = 30);
    
    // Data management
    bool delete_old_data(int days_to_keep = 90);
    bool delete_strategy_data(const std::string& strategy_name);
    
    // Health and monitoring
    bool ping();
    std::string get_version();
    std::unordered_map<std::string, std::string> get_server_stats();
    
    // Utilities
    std::string escape_string(const std::string& str) const;
    std::string format_timestamp(std::chrono::system_clock::time_point timestamp) const;
    std::chrono::system_clock::time_point parse_timestamp(const std::string& timestamp_str) const;
    
private:
    InfluxDBConfig config_;
    bool is_connected_ = false;
    std::string last_error_;
    
    // HTTP client for API calls
    std::string make_http_request(const std::string& method,
                                 const std::string& endpoint,
                                 const std::string& data = "",
                                 const std::unordered_map<std::string, std::string>& headers = {});
    
    // URL building
    std::string build_write_url() const;
    std::string build_query_url() const;
    std::string build_ping_url() const;
    
    // Authentication
    std::unordered_map<std::string, std::string> get_auth_headers() const;
    
    // Data conversion helpers
    InfluxDataPoint convert_performance_metrics_to_point(const PerformanceMetrics& metrics,
                                                        const std::string& strategy_name,
                                                        const std::unordered_map<std::string, std::string>& tags) const;
    
    InfluxDataPoint convert_trade_result_to_point(const TradeResult& trade,
                                                 const std::string& strategy_name,
                                                 const std::unordered_map<std::string, std::string>& tags) const;
    
    InfluxDataPoint convert_portfolio_snapshot_to_point(const PortfolioSnapshot& snapshot,
                                                       const std::string& strategy_name,
                                                       const std::unordered_map<std::string, std::string>& tags) const;
    
    // Query building helpers
    std::string build_time_filter(std::chrono::system_clock::time_point start_time,
                                 std::chrono::system_clock::time_point end_time) const;
    
    std::string build_tag_filter(const std::unordered_map<std::string, std::string>& tags) const;
    
    // Error handling
    void set_last_error(const std::string& error);
    bool handle_response_error(const std::string& response);
    
    // Validation
    bool validate_config() const;
    bool validate_measurement_name(const std::string& measurement) const;
    bool validate_tag_key(const std::string& key) const;
    bool validate_field_key(const std::string& key) const;
};

// Batch writer for high-performance data insertion
class InfluxBatchWriter {
public:
    InfluxBatchWriter(std::shared_ptr<InfluxDBStorage> storage, int batch_size = 1000);
    ~InfluxBatchWriter();
    
    // Batch operations
    bool add_point(const InfluxDataPoint& point);
    bool flush();
    bool auto_flush_enable(int interval_seconds = 10);
    void auto_flush_disable();
    
    // Statistics
    size_t get_pending_count() const;
    size_t get_total_written() const;
    size_t get_total_failed() const;
    
    // Configuration
    void set_batch_size(int batch_size);
    int get_batch_size() const;
    
private:
    std::shared_ptr<InfluxDBStorage> storage_;
    std::vector<InfluxDataPoint> batch_;
    int batch_size_;
    
    // Statistics
    size_t total_written_ = 0;
    size_t total_failed_ = 0;
    
    // Auto-flush
    bool auto_flush_enabled_ = false;
    int auto_flush_interval_seconds_ = 10;
    std::chrono::system_clock::time_point last_flush_time_;
    
    // Thread safety
    mutable std::mutex batch_mutex_;
    
    // Helper methods
    bool should_auto_flush() const;
    void reset_batch();
};

// High-level backtest result manager
class BacktestResultManager {
public:
    BacktestResultManager(std::shared_ptr<InfluxDBStorage> storage);
    ~BacktestResultManager();
    
    // Store complete backtest results
    bool store_backtest_result(const BacktestResult& result,
                              const std::string& strategy_name,
                              const std::unordered_map<std::string, std::string>& metadata = {});
    
    // Retrieve results
    std::vector<PerformanceMetrics> get_strategy_performance_history(
        const std::string& strategy_name,
        std::chrono::system_clock::time_point start_time = {},
        std::chrono::system_clock::time_point end_time = {});
    
    std::vector<TradeResult> get_strategy_trades(
        const std::string& strategy_name,
        const std::string& symbol = "",
        std::chrono::system_clock::time_point start_time = {},
        std::chrono::system_clock::time_point end_time = {},
        int limit = 1000);
    
    // Analytics
    std::unordered_map<std::string, double> compare_strategies(
        const std::vector<std::string>& strategy_names,
        const std::string& metric = "sharpe_ratio");
    
    std::vector<std::pair<std::chrono::system_clock::time_point, double>> get_equity_curve(
        const std::string& strategy_name,
        std::chrono::system_clock::time_point start_time = {},
        std::chrono::system_clock::time_point end_time = {});
    
    // Maintenance
    bool cleanup_old_results(int days_to_keep = 90);
    std::unordered_map<std::string, size_t> get_storage_statistics();
    
private:
    std::shared_ptr<InfluxDBStorage> storage_;
    std::unique_ptr<InfluxBatchWriter> batch_writer_;
    
    // Data conversion helpers
    PerformanceMetrics parse_performance_metrics_from_query(const InfluxQueryResult& result, size_t row) const;
    TradeResult parse_trade_result_from_query(const InfluxQueryResult& result, size_t row) const;
};

// Exception classes
class InfluxDBException : public std::exception {
public:
    explicit InfluxDBException(const std::string& message) : message_(message) {}
    const char* what() const noexcept override { return message_.c_str(); }
    
private:
    std::string message_;
};

class InfluxDBConnectionException : public InfluxDBException {
public:
    explicit InfluxDBConnectionException(const std::string& message) 
        : InfluxDBException("InfluxDB Connection Error: " + message) {}
};

class InfluxDBQueryException : public InfluxDBException {
public:
    explicit InfluxDBQueryException(const std::string& message) 
        : InfluxDBException("InfluxDB Query Error: " + message) {}
};

class InfluxDBWriteException : public InfluxDBException {
public:
    explicit InfluxDBWriteException(const std::string& message) 
        : InfluxDBException("InfluxDB Write Error: " + message) {}
};

} // namespace backtest
} // namespace ats