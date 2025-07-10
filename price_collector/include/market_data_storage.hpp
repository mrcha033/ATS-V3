#pragma once

#include "types/common_types.hpp"
#include <string>
#include <vector>
#include <memory>
#include <chrono>
#include <mutex>
#include <unordered_map>

namespace ats {
namespace price_collector {

// Storage interface for different backends
class MarketDataStorage {
public:
    virtual ~MarketDataStorage() = default;
    
    // Basic storage operations
    virtual bool initialize(const std::string& connection_string) = 0;
    virtual bool store_ticker(const types::Ticker& ticker) = 0;
    virtual bool store_tickers(const std::vector<types::Ticker>& tickers) = 0;
    
    // Retrieval operations
    virtual types::Ticker get_latest_ticker(const std::string& exchange, const std::string& symbol) const = 0;
    virtual std::vector<types::Ticker> get_ticker_history(const std::string& exchange, 
                                                         const std::string& symbol,
                                                         std::chrono::system_clock::time_point from,
                                                         std::chrono::system_clock::time_point to) const = 0;
    virtual std::vector<types::Ticker> get_all_latest_tickers() const = 0;
    
    // Maintenance operations
    virtual bool flush() = 0;
    virtual bool compact() = 0;
    virtual size_t get_size() const = 0;
    virtual bool is_healthy() const = 0;
    virtual std::string get_status() const = 0;
    
    // Statistics
    virtual size_t get_total_records() const = 0;
    virtual std::chrono::milliseconds get_average_write_latency() const = 0;
    virtual std::chrono::milliseconds get_average_read_latency() const = 0;
};

// RocksDB implementation
class RocksDBStorage : public MarketDataStorage {
public:
    RocksDBStorage();
    ~RocksDBStorage() override;
    
    bool initialize(const std::string& db_path) override;
    bool store_ticker(const types::Ticker& ticker) override;
    bool store_tickers(const std::vector<types::Ticker>& tickers) override;
    
    types::Ticker get_latest_ticker(const std::string& exchange, const std::string& symbol) const override;
    std::vector<types::Ticker> get_ticker_history(const std::string& exchange, 
                                                 const std::string& symbol,
                                                 std::chrono::system_clock::time_point from,
                                                 std::chrono::system_clock::time_point to) const override;
    std::vector<types::Ticker> get_all_latest_tickers() const override;
    
    bool flush() override;
    bool compact() override;
    size_t get_size() const override;
    bool is_healthy() const override;
    std::string get_status() const override;
    
    size_t get_total_records() const override;
    std::chrono::milliseconds get_average_write_latency() const override;
    std::chrono::milliseconds get_average_read_latency() const override;
    
    // RocksDB specific operations
    bool backup(const std::string& backup_path);
    bool restore(const std::string& backup_path);
    void set_write_batch_size(size_t batch_size);
    void set_cache_size(size_t cache_size_mb);
    
private:
    struct Implementation;
    std::unique_ptr<Implementation> impl_;
    
    std::string generate_key(const std::string& exchange, const std::string& symbol, 
                            std::chrono::system_clock::time_point timestamp) const;
    std::string generate_latest_key(const std::string& exchange, const std::string& symbol) const;
    std::string serialize_ticker(const types::Ticker& ticker) const;
    types::Ticker deserialize_ticker(const std::string& data) const;
};

// Redis client for pub/sub operations
class RedisPublisher {
public:
    RedisPublisher();
    ~RedisPublisher();
    
    // Connection management
    bool connect(const std::string& host, int port, const std::string& password = "");
    bool disconnect();
    bool is_connected() const;
    
    // Publishing operations
    bool publish_ticker(const std::string& channel, const types::Ticker& ticker);
    bool publish_json(const std::string& channel, const nlohmann::json& data);
    bool publish_raw(const std::string& channel, const std::string& message);
    
    // Batch publishing
    bool publish_tickers_batch(const std::string& channel_prefix, 
                              const std::vector<types::Ticker>& tickers);
    
    // Channel management
    void set_channel_prefix(const std::string& prefix);
    std::string get_full_channel_name(const std::string& channel) const;
    
    // Statistics and monitoring
    size_t get_published_count() const;
    size_t get_failed_count() const;
    std::chrono::milliseconds get_average_publish_latency() const;
    bool is_healthy() const;
    std::string get_connection_info() const;
    
    // Configuration
    void set_publish_timeout(std::chrono::milliseconds timeout);
    void set_max_retries(int retries);
    void enable_compression(bool enable);
    
private:
    struct Implementation;
    std::unique_ptr<Implementation> impl_;
    
    std::string serialize_ticker_for_redis(const types::Ticker& ticker) const;
    bool publish_with_retry(const std::string& channel, const std::string& message);
    void update_statistics(bool success, std::chrono::milliseconds latency);
};

// InfluxDB client for time series data
class InfluxDBClient {
public:
    InfluxDBClient();
    ~InfluxDBClient();
    
    // Connection management
    bool connect(const std::string& url, const std::string& database,
                const std::string& username = "", const std::string& password = "");
    bool disconnect();
    bool is_connected() const;
    
    // Data insertion
    bool write_ticker(const std::string& measurement, const types::Ticker& ticker);
    bool write_tickers_batch(const std::string& measurement, 
                            const std::vector<types::Ticker>& tickers);
    bool write_line_protocol(const std::string& line_protocol);
    
    // Query operations
    std::vector<types::Ticker> query_tickers(const std::string& query);
    std::string query_raw(const std::string& query);
    
    // Database management
    bool create_database(const std::string& database);
    bool drop_database(const std::string& database);
    std::vector<std::string> show_databases();
    
    // Statistics and monitoring
    size_t get_written_points() const;
    size_t get_failed_writes() const;
    std::chrono::milliseconds get_average_write_latency() const;
    bool is_healthy() const;
    std::string get_connection_info() const;
    
    // Configuration
    void set_write_timeout(std::chrono::milliseconds timeout);
    void set_batch_size(size_t batch_size);
    void set_retention_policy(const std::string& policy);
    void enable_compression(bool enable);
    
private:
    struct Implementation;
    std::unique_ptr<Implementation> impl_;
    
    std::string ticker_to_line_protocol(const std::string& measurement, 
                                       const types::Ticker& ticker) const;
    std::string escape_tag_value(const std::string& value) const;
    std::string escape_field_value(const std::string& value) const;
    uint64_t timestamp_to_nanoseconds(std::chrono::system_clock::time_point timestamp) const;
    
    bool write_batch_with_retry(const std::vector<std::string>& lines);
    void update_statistics(bool success, std::chrono::milliseconds latency);
};

// In-memory buffer for fast access to recent data
class MemoryBuffer {
public:
    MemoryBuffer(size_t max_size = 10000);
    ~MemoryBuffer();
    
    // Data operations
    void add_ticker(const types::Ticker& ticker);
    void add_tickers(const std::vector<types::Ticker>& tickers);
    
    // Retrieval operations
    types::Ticker get_latest_ticker(const std::string& exchange, const std::string& symbol) const;
    std::vector<types::Ticker> get_ticker_history(const std::string& exchange, 
                                                 const std::string& symbol,
                                                 std::chrono::system_clock::time_point from) const;
    std::vector<types::Ticker> get_all_latest_tickers() const;
    
    // Buffer management
    void clear();
    void clear_exchange(const std::string& exchange);
    void clear_symbol(const std::string& exchange, const std::string& symbol);
    
    // Statistics
    size_t get_size() const;
    size_t get_max_size() const;
    void set_max_size(size_t max_size);
    double get_utilization() const;
    
    // Time-based cleanup
    void cleanup_old_data(std::chrono::milliseconds max_age);
    void set_auto_cleanup_interval(std::chrono::milliseconds interval);
    
private:
    struct Implementation;
    std::unique_ptr<Implementation> impl_;
    
    std::string generate_key(const std::string& exchange, const std::string& symbol) const;
    void evict_oldest_if_needed();
    void cleanup_expired_data();
};

// Performance monitoring for storage operations
class StoragePerformanceMonitor {
public:
    StoragePerformanceMonitor();
    ~StoragePerformanceMonitor();
    
    // Metric recording
    void record_write_latency(const std::string& storage_type, std::chrono::milliseconds latency);
    void record_read_latency(const std::string& storage_type, std::chrono::milliseconds latency);
    void record_operation_count(const std::string& storage_type, const std::string& operation);
    void record_error(const std::string& storage_type, const std::string& error);
    
    // Statistics retrieval
    std::chrono::milliseconds get_average_write_latency(const std::string& storage_type) const;
    std::chrono::milliseconds get_average_read_latency(const std::string& storage_type) const;
    size_t get_operation_count(const std::string& storage_type, const std::string& operation) const;
    size_t get_error_count(const std::string& storage_type) const;
    
    // Performance metrics
    double get_throughput(const std::string& storage_type) const; // operations per second
    double get_error_rate(const std::string& storage_type) const; // errors per operation
    
    // Reporting
    std::string generate_performance_report() const;
    nlohmann::json get_metrics_json() const;
    
    // Configuration
    void set_metric_window(std::chrono::minutes window);
    void enable_detailed_metrics(bool enable);
    void reset_metrics();
    
private:
    struct Implementation;
    std::unique_ptr<Implementation> impl_;
    
    void cleanup_old_metrics();
    void update_aggregated_metrics();
};

// Storage factory for creating appropriate storage backends
class StorageFactory {
public:
    enum class StorageType {
        ROCKSDB,
        MEMORY_ONLY,
        HYBRID
    };
    
    static std::unique_ptr<MarketDataStorage> create_storage(StorageType type, 
                                                           const std::string& config);
    static std::unique_ptr<RedisPublisher> create_redis_publisher(const std::string& config);
    static std::unique_ptr<InfluxDBClient> create_influxdb_client(const std::string& config);
    
    static StorageType parse_storage_type(const std::string& type_string);
    static std::string storage_type_to_string(StorageType type);
};

// Utility functions for storage operations
namespace storage_utils {
    
    // Data validation
    bool validate_ticker_data(const types::Ticker& ticker);
    bool validate_timestamp(std::chrono::system_clock::time_point timestamp);
    bool validate_price_data(double price);
    
    // Key generation and formatting
    std::string generate_time_series_key(const std::string& exchange, const std::string& symbol,
                                        std::chrono::system_clock::time_point timestamp);
    std::string format_timestamp_iso(std::chrono::system_clock::time_point timestamp);
    std::chrono::system_clock::time_point parse_timestamp_iso(const std::string& iso_string);
    
    // Data serialization
    std::vector<uint8_t> serialize_ticker_binary(const types::Ticker& ticker);
    types::Ticker deserialize_ticker_binary(const std::vector<uint8_t>& data);
    std::string serialize_ticker_json(const types::Ticker& ticker);
    types::Ticker deserialize_ticker_json(const std::string& json_string);
    
    // Compression utilities
    std::vector<uint8_t> compress_ticker_data(const std::vector<types::Ticker>& tickers);
    std::vector<types::Ticker> decompress_ticker_data(const std::vector<uint8_t>& compressed_data);
    
    // Performance utilities
    class ScopedLatencyMeasurement {
    public:
        ScopedLatencyMeasurement(StoragePerformanceMonitor* monitor, 
                               const std::string& storage_type, 
                               const std::string& operation_type);
        ~ScopedLatencyMeasurement();
        
    private:
        StoragePerformanceMonitor* monitor_;
        std::string storage_type_;
        std::string operation_type_;
        std::chrono::high_resolution_clock::time_point start_time_;
    };
    
    // Health check utilities
    bool check_storage_health(const MarketDataStorage* storage);
    bool check_redis_health(const RedisPublisher* publisher);
    bool check_influxdb_health(const InfluxDBClient* client);
    
    // Configuration parsing
    struct StorageConfig {
        std::string type;
        std::string connection_string;
        std::unordered_map<std::string, std::string> options;
    };
    
    StorageConfig parse_storage_config(const std::string& config_string);
    std::string format_storage_config(const StorageConfig& config);
}

#define MEASURE_STORAGE_LATENCY(monitor, storage_type, operation) \
    storage_utils::ScopedLatencyMeasurement _measurement(monitor, storage_type, operation)

} // namespace price_collector
} // namespace ats