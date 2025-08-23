#pragma once

#include "exchange_interface.hpp"
#include "types/common_types.hpp"
#include "config/config_manager.hpp"
#include <memory>
#include <vector>
#include <unordered_map>
#include <unordered_set>
#include <thread>
#include <atomic>
#include <mutex>
#include <queue>
#include <chrono>
#include <shared_mutex>

namespace ats {
namespace price_collector {

// Forward declarations
class MarketDataStorage;
class RedisPublisher;
class InfluxDBClient;
class PerformanceMonitor;

// Price update event structure
struct PriceUpdateEvent {
    types::Ticker ticker;
    std::string event_type;
    std::chrono::system_clock::time_point timestamp;
    std::string source_exchange;
    
    PriceUpdateEvent(const types::Ticker& t, const std::string& type = "ticker")
        : ticker(t), event_type(type), timestamp(std::chrono::system_clock::now())
        , source_exchange(t.exchange) {}
};

// Service configuration
struct ServiceConfig {
    bool enable_redis_publishing;
    bool enable_influxdb_storage;
    bool enable_local_storage;
    bool enable_performance_monitoring;
    
    std::string redis_channel_prefix;
    std::string influxdb_measurement;
    std::string local_storage_path;
    
    size_t max_queue_size;
    std::chrono::milliseconds publish_interval;
    std::chrono::milliseconds storage_flush_interval;
    std::chrono::milliseconds health_check_interval;
    
    int worker_thread_count;
    bool enable_compression;
    bool enable_deduplication;
    std::chrono::milliseconds deduplication_window;
    
    ServiceConfig()
        : enable_redis_publishing(true), enable_influxdb_storage(true)
        , enable_local_storage(true), enable_performance_monitoring(true)
        , redis_channel_prefix("ats:prices")
        , influxdb_measurement("market_data")
        , local_storage_path("./data/market_data")
        , max_queue_size(10000)
        , publish_interval(std::chrono::milliseconds(100))
        , storage_flush_interval(std::chrono::milliseconds(1000))
        , health_check_interval(std::chrono::milliseconds(5000))
        , worker_thread_count(4)
        , enable_compression(false)
        , enable_deduplication(true)
        , deduplication_window(std::chrono::milliseconds(500)) {}
};

// Service statistics
struct ServiceStatistics {
    std::atomic<size_t> total_messages_received{0};
    std::atomic<size_t> total_messages_processed{0};
    std::atomic<size_t> total_messages_published{0};
    std::atomic<size_t> total_messages_stored{0};
    std::atomic<size_t> total_errors{0};
    std::atomic<size_t> total_duplicates_filtered{0};
    
    std::atomic<size_t> current_queue_size{0};
    std::atomic<double> messages_per_second{0.0};
    std::atomic<std::chrono::milliseconds> average_processing_latency{std::chrono::milliseconds(0)};
    
    std::chrono::system_clock::time_point service_start_time;
    std::atomic<std::chrono::milliseconds> uptime{std::chrono::milliseconds(0)};
    
    ServiceStatistics() {
        service_start_time = std::chrono::system_clock::now();
    }
};

// Main price collection service
class PriceCollectorService {
public:
    PriceCollectorService();
    ~PriceCollectorService();
    
    // Service lifecycle
    bool initialize(const config::ConfigManager& config);
    bool start();
    void stop();
    bool is_running() const;
    
    // Exchange management
    bool add_exchange(std::unique_ptr<ExchangeInterface> exchange);
    bool remove_exchange(const std::string& exchange_id);
    std::vector<std::string> get_connected_exchanges() const;
    ExchangeInterface* get_exchange(const std::string& exchange_id) const;
    
    // Subscription management
    bool subscribe_to_symbol(const std::string& exchange_id, const std::string& symbol, 
                            bool ticker = true, bool orderbook = false, bool trades = false);
    bool subscribe_to_symbols(const std::string& exchange_id, 
                             const std::vector<SubscriptionRequest>& requests);
    bool unsubscribe_from_symbol(const std::string& exchange_id, const std::string& symbol);
    bool unsubscribe_all(const std::string& exchange_id = "");
    
    // Data retrieval
    std::vector<types::Ticker> get_latest_tickers() const;
    types::Ticker get_latest_ticker(const std::string& exchange, const std::string& symbol) const;
    std::vector<types::Ticker> get_ticker_history(const std::string& exchange, 
                                                 const std::string& symbol,
                                                 std::chrono::system_clock::time_point from,
                                                 std::chrono::system_clock::time_point to) const;
    
    // Market data snapshots
    types::MarketSnapshot get_market_snapshot() const;
    types::MarketSnapshot get_exchange_snapshot(const std::string& exchange_id) const;
    
    // Statistics and monitoring
    ServiceStatistics get_statistics() const;
    std::unordered_map<std::string, ExchangeCapabilities> get_exchange_capabilities() const;
    std::unordered_map<std::string, ConnectionStatus> get_connection_statuses() const;
    
    // Configuration
    void update_service_config(const ServiceConfig& config);
    ServiceConfig get_service_config() const;
    
    // Event callbacks
    using PriceUpdateCallback = std::function<void(const PriceUpdateEvent&)>;
    using ConnectionStatusCallback = std::function<void(const std::string& exchange, bool connected)>;
    using ErrorCallback = std::function<void(const std::string& error)>;
    
    void set_price_update_callback(PriceUpdateCallback callback);
    void set_connection_status_callback(ConnectionStatusCallback callback);
    void set_error_callback(ErrorCallback callback);
    
    // Health check
    bool is_healthy() const;
    std::vector<std::string> get_health_issues() const;
    
private:
    // Configuration and state
    ServiceConfig config_;
    std::atomic<bool> running_{false};
    std::atomic<bool> initialized_{false};
    
    // Storage and publishing components
    std::unique_ptr<MarketDataStorage> local_storage_;
    std::unique_ptr<RedisPublisher> redis_publisher_;
    std::unique_ptr<InfluxDBClient> influxdb_client_;
    std::unique_ptr<PerformanceMonitor> performance_monitor_;
    
    // Exchange management
    std::unordered_map<std::string, std::unique_ptr<ExchangeInterface>> exchanges_;
    mutable std::shared_mutex exchanges_mutex_;
    
    // Data processing
    std::queue<PriceUpdateEvent> event_queue_;
    std::mutex queue_mutex_;
    std::condition_variable queue_condition_;
    
    // Worker threads
    std::vector<std::thread> worker_threads_;
    std::thread health_check_thread_;
    std::thread statistics_thread_;
    
    // Statistics and monitoring
    mutable ServiceStatistics statistics_;
    std::unordered_map<std::string, types::Ticker> latest_tickers_;
    mutable std::shared_mutex tickers_mutex_;
    
    // Deduplication
    std::unordered_set<std::string> recent_messages_;
    std::mutex deduplication_mutex_;
    std::chrono::system_clock::time_point last_dedup_cleanup_;
    
    // Callbacks
    PriceUpdateCallback price_update_callback_;
    ConnectionStatusCallback connection_status_callback_;
    ErrorCallback error_callback_;
    
    // Event handlers
    void on_ticker_received(const types::Ticker& ticker);
    void on_orderbook_received(const std::string& symbol, const std::string& exchange,
                              const std::vector<std::pair<double, double>>& bids,
                              const std::vector<std::pair<double, double>>& asks);
    void on_trade_received(const std::string& symbol, const std::string& exchange,
                          double price, double quantity, types::Timestamp timestamp);
    void on_connection_status_changed(const std::string& exchange, bool connected);
    
    // Worker thread functions
    void worker_thread_main();
    void health_check_thread_main();
    void statistics_thread_main();
    
    // Processing methods
    void process_event(const PriceUpdateEvent& event);
    void publish_to_redis(const PriceUpdateEvent& event);
    void store_to_influxdb(const PriceUpdateEvent& event);
    void store_locally(const PriceUpdateEvent& event);
    
    // Deduplication
    bool is_duplicate_message(const PriceUpdateEvent& event);
    void cleanup_deduplication_cache();
    std::string generate_message_hash(const PriceUpdateEvent& event);
    
    // Utility methods
    void update_statistics();
    void log_service_status();
    void handle_error(const std::string& error_message);
    
    // Initialization helpers
    bool initialize_storage_components(const config::ConfigManager& config);
    bool initialize_exchanges(const config::ConfigManager& config);
    void start_worker_threads();
    void stop_worker_threads();
};

// Factory for creating exchange adapters
class ExchangeAdapterFactory {
public:
    static std::unique_ptr<ExchangeInterface> create_adapter(const std::string& exchange_id);
    static std::vector<std::string> get_supported_exchanges();
    static bool is_exchange_supported(const std::string& exchange_id);
};

// Utility functions
namespace price_collector_utils {
    
    // Symbol validation and normalization
    std::string normalize_symbol(const std::string& symbol);
    bool is_valid_symbol(const std::string& symbol);
    std::vector<std::string> parse_symbol_list(const std::string& symbol_list);
    
    // Market data validation
    bool is_valid_ticker(const types::Ticker& ticker);
    bool is_reasonable_price(double price);
    bool is_reasonable_volume(double volume);
    
    // Performance utilities
    std::chrono::milliseconds calculate_latency(types::Timestamp message_time);
    double calculate_messages_per_second(size_t message_count, std::chrono::milliseconds duration);
    
    // Configuration helpers
    ServiceConfig load_service_config(const config::ConfigManager& config);
    bool validate_service_config(const ServiceConfig& config);
    
    // Error handling
    std::string format_exchange_error(const std::string& exchange_id, const std::string& error);
    std::string format_processing_error(const std::string& operation, const std::string& error);
    
    // Health check utilities
    bool check_exchange_health(const ExchangeInterface* exchange);
    bool check_storage_health(const MarketDataStorage* storage);
    bool check_publisher_health(const RedisPublisher* publisher);
    
    // Data conversion
    nlohmann::json ticker_to_json(const types::Ticker& ticker);
    std::string ticker_to_csv(const types::Ticker& ticker);
    std::vector<uint8_t> compress_data(const std::vector<uint8_t>& data);
    std::vector<uint8_t> decompress_data(const std::vector<uint8_t>& compressed_data);
}

} // namespace price_collector
} // namespace ats