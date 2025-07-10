#pragma once

#include "trading_engine_service.hpp"
#include "order_router.hpp"
#include "spread_calculator.hpp"
#include "types/common_types.hpp"
#include <memory>
#include <string>
#include <vector>
#include <unordered_map>
#include <chrono>
#include <queue>
#include <thread>
#include <atomic>
#include <functional>

namespace ats {
namespace trading_engine {

// InfluxDB connection configuration
struct InfluxDBConfig {
    std::string url;
    std::string database;
    std::string username;
    std::string password;
    std::string retention_policy;
    
    // Connection settings
    std::chrono::seconds connection_timeout;
    std::chrono::seconds read_timeout;
    std::chrono::seconds write_timeout;
    int max_retries;
    std::chrono::milliseconds retry_delay;
    
    // Batching settings
    size_t batch_size;
    std::chrono::milliseconds flush_interval;
    size_t max_buffer_size;
    bool enable_compression;
    
    // Performance settings
    int worker_thread_count;
    bool enable_async_writes;
    bool enable_health_check;
    std::chrono::seconds health_check_interval;
    
    InfluxDBConfig()
        : url("http://localhost:8086")
        , database("ats_trading")
        , retention_policy("autogen")
        , connection_timeout(std::chrono::seconds(10))
        , read_timeout(std::chrono::seconds(30))
        , write_timeout(std::chrono::seconds(30))
        , max_retries(3)
        , retry_delay(std::chrono::milliseconds(1000))
        , batch_size(1000)
        , flush_interval(std::chrono::milliseconds(5000))
        , max_buffer_size(10000)
        , enable_compression(true)
        , worker_thread_count(2)
        , enable_async_writes(true)
        , enable_health_check(true)
        , health_check_interval(std::chrono::seconds(60)) {}
};

// InfluxDB write statistics
struct InfluxDBStatistics {
    std::atomic<size_t> total_points_written{0};
    std::atomic<size_t> total_batches_written{0};
    std::atomic<size_t> total_write_errors{0};
    std::atomic<size_t> total_connection_errors{0};
    std::atomic<size_t> total_retries{0};
    
    std::atomic<double> average_write_latency_ms{0.0};
    std::atomic<double> average_batch_size{0.0};
    std::atomic<double> write_success_rate{0.0};
    std::atomic<size_t> points_per_second{0};
    
    std::atomic<size_t> pending_points{0};
    std::atomic<size_t> buffer_usage{0};
    std::atomic<bool> is_connected{false};
    std::atomic<bool> is_healthy{false};
    
    std::chrono::system_clock::time_point last_successful_write;
    std::chrono::system_clock::time_point last_connection_attempt;
    std::chrono::system_clock::time_point session_start;
    
    InfluxDBStatistics() {
        auto now = std::chrono::system_clock::now();
        last_successful_write = now;
        last_connection_attempt = now;
        session_start = now;
    }
};

// Line protocol data point
struct InfluxDBPoint {
    std::string measurement;
    std::unordered_map<std::string, std::string> tags;
    std::unordered_map<std::string, double> fields_double;
    std::unordered_map<std::string, int64_t> fields_int;
    std::unordered_map<std::string, std::string> fields_string;
    std::unordered_map<std::string, bool> fields_bool;
    std::chrono::system_clock::time_point timestamp;
    
    InfluxDBPoint() : timestamp(std::chrono::system_clock::now()) {}
    
    InfluxDBPoint(const std::string& measurement_name) 
        : measurement(measurement_name), timestamp(std::chrono::system_clock::now()) {}
    
    // Convenience methods for adding fields
    InfluxDBPoint& add_tag(const std::string& key, const std::string& value);
    InfluxDBPoint& add_field(const std::string& key, double value);
    InfluxDBPoint& add_field(const std::string& key, int64_t value);
    InfluxDBPoint& add_field(const std::string& key, const std::string& value);
    InfluxDBPoint& add_field(const std::string& key, bool value);
    InfluxDBPoint& set_timestamp(std::chrono::system_clock::time_point time);
    
    std::string to_line_protocol() const;
};

// Enhanced InfluxDB client
class InfluxDBClient {
public:
    InfluxDBClient();
    ~InfluxDBClient();
    
    // Connection management
    bool initialize(const InfluxDBConfig& config);
    bool connect();
    void disconnect();
    bool is_connected() const;
    bool is_healthy() const;
    
    // Synchronous write operations
    bool write_point(const InfluxDBPoint& point);
    bool write_points(const std::vector<InfluxDBPoint>& points);
    bool write_line_protocol(const std::string& line_protocol);
    bool write_line_protocols(const std::vector<std::string>& line_protocols);
    
    // Asynchronous write operations
    bool write_point_async(const InfluxDBPoint& point);
    bool write_points_async(const std::vector<InfluxDBPoint>& points);
    bool flush_pending_writes();
    
    // Query operations
    std::string query(const std::string& query_string);
    std::vector<std::unordered_map<std::string, std::string>> query_table(const std::string& query_string);
    
    // Database management
    bool create_database(const std::string& database_name);
    bool drop_database(const std::string& database_name);
    std::vector<std::string> list_databases();
    
    bool create_retention_policy(const std::string& policy_name, 
                                const std::string& duration,
                                int replication_factor = 1,
                                bool is_default = false);
    
    // Statistics and monitoring
    InfluxDBStatistics get_statistics() const;
    void reset_statistics();
    
    // Configuration
    void update_config(const InfluxDBConfig& config);
    InfluxDBConfig get_config() const;
    
    // Health check and diagnostics
    bool ping();
    std::string get_version();
    std::vector<std::string> get_health_issues() const;
    std::string get_status_report() const;
    
    // Event callbacks
    using WriteSuccessCallback = std::function<void(size_t points_written)>;
    using WriteErrorCallback = std::function<void(const std::string& error)>;
    using ConnectionStatusCallback = std::function<void(bool connected)>;
    
    void set_write_success_callback(WriteSuccessCallback callback);
    void set_write_error_callback(WriteErrorCallback callback);
    void set_connection_status_callback(ConnectionStatusCallback callback);

private:
    struct Implementation;
    std::unique_ptr<Implementation> impl_;
    
    // Connection helpers
    bool establish_connection();
    void handle_connection_lost();
    bool test_connection();
    
    // Write operation helpers
    bool write_batch_internal(const std::vector<std::string>& line_protocols);
    bool send_write_request(const std::string& data);
    
    // Async processing
    void start_async_writer();
    void stop_async_writer();
    void async_writer_loop();
    void process_write_queue();
    
    // Error handling and retry logic
    bool should_retry_write(const std::string& error);
    bool retry_write_operation(const std::string& data, int max_attempts);
    void handle_write_error(const std::string& error);
    
    // Health monitoring
    void start_health_monitor();
    void stop_health_monitor();
    void health_monitor_loop();
    void check_connection_health();
    
    // Statistics updates
    void update_write_statistics(size_t points_count, std::chrono::milliseconds latency, bool success);
    void update_connection_statistics(bool connected);
    
    // Utility methods
    std::string escape_tag_value(const std::string& value) const;
    std::string escape_field_value(const std::string& value) const;
    std::string format_timestamp(std::chrono::system_clock::time_point timestamp) const;
    
    // HTTP client helpers
    std::string create_write_url() const;
    std::string create_query_url() const;
    std::map<std::string, std::string> create_auth_headers() const;
};

// Specialized trading data logger using InfluxDB
class TradingDataLogger {
public:
    TradingDataLogger();
    ~TradingDataLogger();
    
    // Initialization
    bool initialize(const InfluxDBConfig& config);
    bool start();
    void stop();
    
    // Trading execution logging
    bool log_trade_execution(const TradeExecution& execution);
    bool log_arbitrage_opportunity(const ArbitrageOpportunity& opportunity);
    bool log_order_execution(const OrderExecutionDetails& order);
    bool log_rollback_result(const EnhancedRollbackResult& rollback);
    
    // Market data logging
    bool log_ticker_update(const types::Ticker& ticker);
    bool log_spread_analysis(const SpreadAnalysis& analysis);
    bool log_market_depth(const MarketDepth& depth);
    
    // Performance and system metrics
    bool log_trading_statistics(const TradingStatistics& stats);
    bool log_order_router_metrics(const OrderRouter::PerformanceMetrics& metrics);
    bool log_system_health(const std::string& component, bool healthy, const std::string& details = "");
    
    // Portfolio and balance logging
    bool log_portfolio_snapshot(const types::Portfolio& portfolio);
    bool log_balance_update(const std::string& exchange, const types::Balance& balance);
    bool log_position_change(const std::string& symbol, double old_position, double new_position);
    
    // Risk and compliance logging
    bool log_risk_event(const std::string& event_type, const std::string& details, double severity = 1.0);
    bool log_compliance_check(const std::string& check_type, bool passed, const std::string& details = "");
    
    // Batch logging for performance
    bool log_trade_executions_batch(const std::vector<TradeExecution>& executions);
    bool log_ticker_updates_batch(const std::vector<types::Ticker>& tickers);
    bool log_order_executions_batch(const std::vector<OrderExecutionDetails>& orders);
    
    // Query operations for analytics
    std::vector<TradeExecution> query_trade_history(std::chrono::hours lookback);
    std::vector<types::Ticker> query_price_history(const std::string& symbol, 
                                                    const std::string& exchange,
                                                    std::chrono::hours lookback);
    
    // Analytics queries
    double calculate_total_profit(std::chrono::hours period);
    double calculate_success_rate(std::chrono::hours period);
    std::unordered_map<std::string, double> get_profit_by_symbol(std::chrono::hours period);
    std::unordered_map<std::string, double> get_volume_by_exchange(std::chrono::hours period);
    std::unordered_map<std::string, size_t> get_trade_count_by_hour(std::chrono::hours period);
    
    // Real-time analytics
    double get_current_profit_rate();
    double get_current_trade_frequency();
    std::vector<std::string> get_most_active_symbols(int limit = 10);
    
    // Data management
    bool create_continuous_queries();
    bool setup_retention_policies();
    bool compact_old_data(std::chrono::hours max_age);
    size_t get_pending_log_count() const;
    
    // Configuration and monitoring
    void set_batch_size(size_t batch_size);
    void set_flush_interval(std::chrono::milliseconds interval);
    bool is_healthy() const;
    InfluxDBStatistics get_statistics() const;

private:
    std::unique_ptr<InfluxDBClient> influx_client_;
    
    // Data conversion methods
    InfluxDBPoint trade_execution_to_point(const TradeExecution& execution);
    InfluxDBPoint arbitrage_opportunity_to_point(const ArbitrageOpportunity& opportunity);
    InfluxDBPoint order_execution_to_point(const OrderExecutionDetails& order);
    InfluxDBPoint ticker_to_point(const types::Ticker& ticker);
    InfluxDBPoint spread_analysis_to_point(const SpreadAnalysis& analysis);
    InfluxDBPoint balance_to_point(const std::string& exchange, const types::Balance& balance);
    InfluxDBPoint rollback_result_to_point(const EnhancedRollbackResult& rollback);
    
    // Query result parsing
    std::vector<TradeExecution> parse_trade_executions(const std::string& query_result);
    std::vector<types::Ticker> parse_tickers(const std::string& query_result);
    
    // Continuous queries and retention policies
    std::vector<std::string> create_trading_continuous_queries();
    std::vector<std::pair<std::string, std::string>> create_trading_retention_policies();
    
    // Helper methods
    std::string create_trade_history_query(std::chrono::hours lookback);
    std::string create_profit_calculation_query(std::chrono::hours period);
    std::string create_symbol_activity_query(std::chrono::hours period, int limit);
};

// InfluxDB utility functions
namespace influxdb_utils {
    
    // Line protocol helpers
    std::string escape_measurement_name(const std::string& name);
    std::string escape_tag_key(const std::string& key);
    std::string escape_tag_value(const std::string& value);
    std::string escape_field_key(const std::string& key);
    std::string escape_string_field_value(const std::string& value);
    
    // Timestamp conversion
    int64_t to_nanoseconds(std::chrono::system_clock::time_point timestamp);
    std::chrono::system_clock::time_point from_nanoseconds(int64_t nanoseconds);
    std::string format_influx_timestamp(std::chrono::system_clock::time_point timestamp);
    
    // Query builders
    std::string build_select_query(const std::string& measurement,
                                  const std::vector<std::string>& fields,
                                  const std::string& where_clause = "",
                                  const std::string& time_range = "",
                                  const std::string& group_by = "",
                                  int limit = 0);
    
    std::string build_aggregation_query(const std::string& measurement,
                                       const std::string& field,
                                       const std::string& aggregation_function,
                                       const std::string& time_range,
                                       const std::string& group_by_time = "1h");
    
    std::string build_time_range_filter(std::chrono::hours lookback);
    std::string build_time_range_filter(std::chrono::system_clock::time_point start,
                                       std::chrono::system_clock::time_point end);
    
    // Data validation
    bool validate_measurement_name(const std::string& name);
    bool validate_tag_key(const std::string& key);
    bool validate_field_key(const std::string& key);
    bool validate_line_protocol(const std::string& line_protocol);
    
    // Performance helpers
    size_t estimate_line_protocol_size(const InfluxDBPoint& point);
    size_t estimate_batch_size(const std::vector<InfluxDBPoint>& points);
    std::vector<std::vector<InfluxDBPoint>> split_into_batches(const std::vector<InfluxDBPoint>& points,
                                                              size_t max_batch_size);
    
    // Configuration helpers
    bool validate_influxdb_config(const InfluxDBConfig& config);
    std::vector<std::string> get_config_validation_errors(const InfluxDBConfig& config);
    InfluxDBConfig create_production_config();
    InfluxDBConfig create_development_config();
    
    // Error handling
    bool is_retryable_error(const std::string& error_message);
    std::chrono::milliseconds get_retry_delay(int attempt_number);
    std::string format_influxdb_error(const std::string& operation, const std::string& error);
}

} // namespace trading_engine
} // namespace ats