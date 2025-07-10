#pragma once

#include "types/common_types.hpp"
#include "trading_engine_service.hpp"
#include <memory>
#include <string>
#include <vector>
#include <functional>
#include <thread>
#include <atomic>
#include <chrono>

namespace ats {
namespace trading_engine {

// Redis message structure
struct RedisMessage {
    std::string channel;
    std::string message;
    std::chrono::system_clock::time_point timestamp;
    
    RedisMessage() : timestamp(std::chrono::system_clock::now()) {}
    RedisMessage(const std::string& ch, const std::string& msg)
        : channel(ch), message(msg), timestamp(std::chrono::system_clock::now()) {}
};

// Price update event from Redis
struct PriceUpdateEvent {
    types::Ticker ticker;
    std::string event_type;
    std::string source_channel;
    std::chrono::system_clock::time_point received_at;
    
    PriceUpdateEvent() : received_at(std::chrono::system_clock::now()) {}
};

// Redis subscriber configuration
struct RedisSubscriberConfig {
    std::string host;
    int port;
    std::string password;
    std::vector<std::string> channels;
    std::string channel_pattern;
    
    // Connection settings
    std::chrono::seconds connection_timeout;
    std::chrono::seconds command_timeout;
    int max_reconnect_attempts;
    std::chrono::seconds reconnect_delay;
    
    // Performance settings
    size_t message_buffer_size;
    int worker_thread_count;
    bool enable_message_batching;
    std::chrono::milliseconds batch_timeout;
    
    // Monitoring
    bool enable_health_check;
    std::chrono::seconds health_check_interval;
    bool enable_metrics_collection;
    
    RedisSubscriberConfig()
        : host("localhost"), port(6379)
        , connection_timeout(std::chrono::seconds(10))
        , command_timeout(std::chrono::seconds(5))
        , max_reconnect_attempts(10)
        , reconnect_delay(std::chrono::seconds(5))
        , message_buffer_size(10000)
        , worker_thread_count(2)
        , enable_message_batching(false)
        , batch_timeout(std::chrono::milliseconds(100))
        , enable_health_check(true)
        , health_check_interval(std::chrono::seconds(30))
        , enable_metrics_collection(true) {}
};

// Subscriber statistics
struct SubscriberStatistics {
    std::atomic<size_t> total_messages_received{0};
    std::atomic<size_t> total_messages_processed{0};
    std::atomic<size_t> total_price_updates{0};
    std::atomic<size_t> total_parsing_errors{0};
    std::atomic<size_t> total_connection_errors{0};
    std::atomic<size_t> total_reconnections{0};
    
    std::atomic<double> messages_per_second{0.0};
    std::atomic<std::chrono::milliseconds> average_processing_latency{std::chrono::milliseconds(0)};
    std::atomic<std::chrono::milliseconds> last_message_time{std::chrono::milliseconds(0)};
    
    std::chrono::system_clock::time_point start_time;
    std::atomic<bool> is_connected{false};
    std::atomic<std::chrono::milliseconds> uptime{std::chrono::milliseconds(0)};
    
    SubscriberStatistics() {
        start_time = std::chrono::system_clock::now();
    }
};

// Main Redis subscriber class
class RedisSubscriber {
public:
    RedisSubscriber();
    ~RedisSubscriber();
    
    // Lifecycle management
    bool initialize(const RedisSubscriberConfig& config);
    bool start();
    void stop();
    bool is_running() const;
    
    // Subscription management
    bool subscribe_to_channel(const std::string& channel);
    bool subscribe_to_pattern(const std::string& pattern);
    bool unsubscribe_from_channel(const std::string& channel);
    bool unsubscribe_from_pattern(const std::string& pattern);
    bool unsubscribe_all();
    
    std::vector<std::string> get_subscribed_channels() const;
    std::vector<std::string> get_subscribed_patterns() const;
    
    // Message handling callbacks
    using MessageCallback = std::function<void(const RedisMessage&)>;
    using PriceUpdateCallback = std::function<void(const PriceUpdateEvent&)>;
    using ConnectionCallback = std::function<void(bool connected, const std::string& reason)>;
    using ErrorCallback = std::function<void(const std::string& error)>;
    
    void set_message_callback(MessageCallback callback);
    void set_price_update_callback(PriceUpdateCallback callback);
    void set_connection_callback(ConnectionCallback callback);
    void set_error_callback(ErrorCallback callback);
    
    // Connection management
    bool is_connected() const;
    bool reconnect();
    std::string get_connection_info() const;
    
    // Statistics and monitoring
    SubscriberStatistics get_statistics() const;
    void reset_statistics();
    
    // Health check
    bool is_healthy() const;
    std::vector<std::string> get_health_issues() const;
    bool ping_server();
    
    // Configuration updates
    void update_config(const RedisSubscriberConfig& config);
    RedisSubscriberConfig get_config() const;
    
    // Manual message publishing (for testing)
    bool publish_message(const std::string& channel, const std::string& message);
    bool publish_ticker_update(const std::string& channel, const types::Ticker& ticker);
    
private:
    struct Implementation;
    std::unique_ptr<Implementation> impl_;
    
    // Connection management
    bool connect_to_redis();
    void disconnect_from_redis();
    void handle_connection_lost();
    void handle_reconnection();
    
    // Message processing
    void message_processing_loop();
    void process_raw_message(const RedisMessage& message);
    void process_price_update_message(const RedisMessage& message);
    
    // Message parsing
    PriceUpdateEvent parse_price_message(const RedisMessage& message);
    types::Ticker parse_ticker_json(const std::string& json_str);
    bool is_price_update_message(const RedisMessage& message);
    
    // Error handling
    void handle_parsing_error(const RedisMessage& message, const std::string& error);
    void handle_connection_error(const std::string& error);
    void handle_subscription_error(const std::string& channel, const std::string& error);
    
    // Statistics updates
    void update_statistics();
    void record_message_received();
    void record_message_processed(std::chrono::microseconds processing_time);
    void record_parsing_error();
    
    // Health monitoring
    void health_check_loop();
    void check_connection_health();
    void check_message_flow_health();
};

// Trade logger for storing execution records
class TradeLogger {
public:
    TradeLogger();
    ~TradeLogger();
    
    // Initialization
    bool initialize(const std::string& influxdb_url, const std::string& database);
    bool initialize_file_logging(const std::string& log_directory);
    
    // Trade execution logging
    bool log_trade_execution(const TradeExecution& execution);
    bool log_arbitrage_opportunity(const ArbitrageOpportunity& opportunity);
    bool log_order_execution(const OrderExecutionDetails& order_details);
    
    // Batch logging for performance
    bool log_trade_executions_batch(const std::vector<TradeExecution>& executions);
    bool log_order_executions_batch(const std::vector<OrderExecutionDetails>& orders);
    
    // Performance and statistics logging
    bool log_performance_metrics(const TradingStatistics& stats);
    bool log_portfolio_snapshot(const types::Portfolio& portfolio);
    bool log_balance_update(const std::string& exchange, const types::Balance& balance);
    
    // Query operations
    std::vector<TradeExecution> query_trade_history(std::chrono::hours lookback);
    std::vector<TradeExecution> query_trades_by_symbol(const std::string& symbol, std::chrono::hours lookback);
    std::vector<OrderExecutionDetails> query_order_history(const std::string& exchange, std::chrono::hours lookback);
    
    // Analytics queries
    double calculate_total_profit(std::chrono::hours period);
    double calculate_success_rate(std::chrono::hours period);
    std::unordered_map<std::string, double> get_profit_by_symbol(std::chrono::hours period);
    std::unordered_map<std::string, double> get_volume_by_exchange(std::chrono::hours period);
    
    // Data management
    bool flush_pending_logs();
    bool compact_old_data(std::chrono::hours max_age);
    size_t get_pending_log_count() const;
    
    // Configuration
    void set_batch_size(size_t batch_size);
    void set_flush_interval(std::chrono::seconds interval);
    void enable_file_logging(bool enable);
    void enable_database_logging(bool enable);
    
    // Health and diagnostics
    bool is_healthy() const;
    std::string get_status() const;
    size_t get_total_logs_written() const;
    
private:
    struct Implementation;
    std::unique_ptr<Implementation> impl_;
    
    // InfluxDB integration
    bool write_to_influxdb(const std::string& measurement, const std::string& line_protocol);
    std::string trade_execution_to_line_protocol(const TradeExecution& execution);
    std::string order_execution_to_line_protocol(const OrderExecutionDetails& order);
    std::string arbitrage_opportunity_to_line_protocol(const ArbitrageOpportunity& opportunity);
    
    // File logging
    bool write_to_file(const std::string& log_entry);
    std::string trade_execution_to_csv(const TradeExecution& execution);
    std::string create_log_filename(const std::string& prefix);
    
    // Batch processing
    void process_pending_logs();
    void flush_file_buffers();
    
    // Data formatting
    std::string format_timestamp(std::chrono::system_clock::time_point timestamp);
    std::string escape_string_for_influx(const std::string& str);
    std::string format_currency_amount(double amount, const std::string& currency = "USD");
};

// Price event processor for detecting arbitrage opportunities
class PriceEventProcessor {
public:
    PriceEventProcessor();
    ~PriceEventProcessor();
    
    // Initialization
    bool initialize(std::shared_ptr<SpreadCalculator> spread_calculator);
    
    // Event processing
    void process_price_update(const PriceUpdateEvent& event);
    void process_ticker_update(const types::Ticker& ticker);
    
    // Opportunity detection
    using OpportunityDetectedCallback = std::function<void(const ArbitrageOpportunity&)>;
    void set_opportunity_detected_callback(OpportunityDetectedCallback callback);
    
    // Configuration
    void set_minimum_spread_threshold(double threshold);
    void set_minimum_profit_threshold(double threshold);
    void set_opportunity_timeout(std::chrono::milliseconds timeout);
    void enable_cross_exchange_analysis(bool enable);
    
    // Market data caching
    void update_market_cache(const types::Ticker& ticker);
    types::Ticker get_latest_ticker(const std::string& exchange, const std::string& symbol) const;
    std::vector<types::Ticker> get_all_latest_tickers() const;
    
    // Statistics
    size_t get_opportunities_detected() const;
    size_t get_price_updates_processed() const;
    double get_average_processing_latency_ms() const;
    
private:
    struct Implementation;
    std::unique_ptr<Implementation> impl_;
    
    // Opportunity detection logic
    std::vector<ArbitrageOpportunity> detect_arbitrage_opportunities(const types::Ticker& updated_ticker);
    bool is_profitable_opportunity(const types::Ticker& buy_ticker, const types::Ticker& sell_ticker);
    ArbitrageOpportunity create_opportunity(const types::Ticker& buy_ticker, const types::Ticker& sell_ticker);
    
    // Market data management
    void update_ticker_cache(const types::Ticker& ticker);
    void cleanup_stale_data();
    
    // Validation
    bool is_valid_ticker_update(const types::Ticker& ticker);
    bool is_opportunity_valid(const ArbitrageOpportunity& opportunity);
    
    // Performance tracking
    void record_processing_time(std::chrono::microseconds time);
    void update_detection_statistics();
};

// Utility functions for Redis operations
namespace redis_utils {
    
    // Channel name builders
    std::string build_price_channel(const std::string& exchange, const std::string& symbol);
    std::string build_ticker_channel(const std::string& exchange);
    std::string build_trade_channel(const std::string& exchange, const std::string& symbol);
    std::string build_arbitrage_channel();
    
    // Message formatting
    std::string format_ticker_message(const types::Ticker& ticker);
    std::string format_trade_execution_message(const TradeExecution& execution);
    std::string format_opportunity_message(const ArbitrageOpportunity& opportunity);
    
    // Message parsing
    types::Ticker parse_ticker_message(const std::string& message);
    TradeExecution parse_trade_execution_message(const std::string& message);
    ArbitrageOpportunity parse_opportunity_message(const std::string& message);
    
    // Connection utilities
    std::string build_redis_url(const std::string& host, int port, const std::string& password = "");
    bool test_redis_connection(const std::string& host, int port, const std::string& password = "");
    
    // Data validation
    bool is_valid_redis_message(const RedisMessage& message);
    bool is_json_message(const std::string& message);
    bool validate_channel_name(const std::string& channel);
    
    // Performance utilities
    std::chrono::microseconds measure_parsing_time(std::function<void()> parse_function);
    size_t estimate_message_size(const std::string& message);
    
    // Error handling
    std::string format_redis_error(const std::string& operation, const std::string& error);
    bool is_recoverable_redis_error(const std::string& error);
    std::chrono::milliseconds get_recommended_retry_delay(const std::string& error);
}

} // namespace trading_engine
} // namespace ats