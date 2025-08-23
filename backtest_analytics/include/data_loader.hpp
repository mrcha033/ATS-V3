#pragma once

#include "../../shared/include/types/common_types.hpp"
#include <string>
#include <vector>
#include <chrono>
#include <unordered_map>
#include <memory>
#include <fstream>
#include <functional>

namespace ats {
namespace backtest {

// Market data structures for backtesting
struct MarketDataPoint {
    std::chrono::system_clock::time_point timestamp;
    std::string symbol;
    std::string exchange;
    double open_price = 0.0;
    double high_price = 0.0;
    double low_price = 0.0;
    double close_price = 0.0;
    double volume = 0.0;
    double bid_price = 0.0;
    double ask_price = 0.0;
    double bid_volume = 0.0;
    double ask_volume = 0.0;
    
    // Constructor
    MarketDataPoint() = default;
    MarketDataPoint(std::chrono::system_clock::time_point ts, const std::string& sym, 
                   const std::string& exch, double close, double vol = 0.0)
        : timestamp(ts), symbol(sym), exchange(exch), close_price(close), volume(vol) {}
};

struct TradeData {
    std::chrono::system_clock::time_point timestamp;
    std::string symbol;
    std::string exchange;
    std::string side; // "buy" or "sell"
    double price = 0.0;
    double quantity = 0.0;
    double fee = 0.0;
    std::string trade_id;
    
    TradeData() = default;
    TradeData(std::chrono::system_clock::time_point ts, const std::string& sym,
              const std::string& exch, const std::string& side_val, 
              double price_val, double qty, double fee_val = 0.0)
        : timestamp(ts), symbol(sym), exchange(exch), side(side_val),
          price(price_val), quantity(qty), fee(fee_val) {}
};

// Data loading configuration
struct DataLoaderConfig {
    std::string data_source = "csv"; // "csv", "api", "database"
    std::string file_path;
    std::string api_endpoint;
    std::string database_connection;
    std::chrono::system_clock::time_point start_date;
    std::chrono::system_clock::time_point end_date;
    std::vector<std::string> symbols;
    std::vector<std::string> exchanges;
    std::string time_interval = "1m"; // 1m, 5m, 1h, 1d
    bool include_orderbook = false;
    bool include_trades = false;
    int max_records = 0; // 0 = no limit
};

// CSV data loader for historical market data
class CsvDataLoader {
public:
    CsvDataLoader();
    ~CsvDataLoader();
    
    // Load market data from CSV file
    bool load_market_data(const std::string& file_path, 
                         std::vector<MarketDataPoint>& data,
                         const std::string& format = "ohlcv"); // "ohlcv", "tick", "custom"
    
    // Load trade data from CSV file
    bool load_trade_data(const std::string& file_path,
                        std::vector<TradeData>& data);
    
    // Configure CSV format
    struct CsvFormat {
        char delimiter = ',';
        bool has_header = true;
        std::vector<std::string> column_names;
        std::string timestamp_format = "%Y-%m-%d %H:%M:%S";
        std::string timezone = "UTC";
    };
    
    void set_csv_format(const CsvFormat& format);
    CsvFormat get_csv_format() const;
    
    // Validation
    bool validate_csv_file(const std::string& file_path);
    std::vector<std::string> get_csv_columns(const std::string& file_path);
    
private:
    CsvFormat csv_format_;
    
    // Helper methods
    std::vector<std::string> parse_csv_line(const std::string& line);
    std::chrono::system_clock::time_point parse_timestamp(const std::string& timestamp_str);
    double parse_double(const std::string& value);
    bool is_valid_number(const std::string& str);
};

// API data loader for fetching historical data from exchanges
class ApiDataLoader {
public:
    ApiDataLoader();
    ~ApiDataLoader();
    
    // Initialize with API credentials
    bool initialize(const std::unordered_map<std::string, std::string>& api_configs);
    
    // Load data from exchange APIs
    bool load_historical_data(const std::string& exchange,
                             const std::string& symbol,
                             const std::string& interval,
                             std::chrono::system_clock::time_point start_time,
                             std::chrono::system_clock::time_point end_time,
                             std::vector<MarketDataPoint>& data);
    
    // Exchange-specific implementations
    bool load_binance_data(const std::string& symbol, const std::string& interval,
                          std::chrono::system_clock::time_point start_time,
                          std::chrono::system_clock::time_point end_time,
                          std::vector<MarketDataPoint>& data);
    
    bool load_upbit_data(const std::string& symbol, const std::string& interval,
                        std::chrono::system_clock::time_point start_time,
                        std::chrono::system_clock::time_point end_time,
                        std::vector<MarketDataPoint>& data);
    
    // Rate limiting and retry logic
    void set_rate_limit(int requests_per_second);
    void set_retry_config(int max_retries, int retry_delay_ms);
    
    // Data validation and cleaning
    std::vector<MarketDataPoint> clean_data(const std::vector<MarketDataPoint>& raw_data);
    
private:
    std::unordered_map<std::string, std::string> api_configs_;
    int rate_limit_rps_ = 10;
    int max_retries_ = 3;
    int retry_delay_ms_ = 1000;
    
    // HTTP client for API calls
    std::string make_http_request(const std::string& url, 
                                 const std::unordered_map<std::string, std::string>& headers = {});
    
    // Exchange-specific URL builders
    std::string build_binance_url(const std::string& symbol, const std::string& interval,
                                 std::chrono::system_clock::time_point start_time,
                                 std::chrono::system_clock::time_point end_time);
    
    std::string build_upbit_url(const std::string& symbol, const std::string& interval,
                               std::chrono::system_clock::time_point start_time,
                               std::chrono::system_clock::time_point end_time);
    
    // JSON parsing helpers
    std::vector<MarketDataPoint> parse_binance_response(const std::string& json_response, 
                                                       const std::string& symbol);
    std::vector<MarketDataPoint> parse_upbit_response(const std::string& json_response,
                                                     const std::string& symbol);
};

// Database data loader for loading from existing data stores
class DatabaseDataLoader {
public:
    DatabaseDataLoader();
    ~DatabaseDataLoader();
    
    // Database connection
    bool connect(const std::string& connection_string);
    void disconnect();
    
    // Load from InfluxDB (time series database)
    bool load_from_influxdb(const std::string& measurement,
                           const std::string& database,
                           std::chrono::system_clock::time_point start_time,
                           std::chrono::system_clock::time_point end_time,
                           const std::vector<std::string>& tags,
                           std::vector<MarketDataPoint>& data);
    
    // Load from RocksDB (tick database)
    bool load_from_rocksdb(const std::string& db_path,
                          const std::string& symbol,
                          std::chrono::system_clock::time_point start_time,
                          std::chrono::system_clock::time_point end_time,
                          std::vector<MarketDataPoint>& data);
    
    // Load from SQLite
    bool load_from_sqlite(const std::string& db_path,
                         const std::string& table_name,
                         const std::string& query,
                         std::vector<MarketDataPoint>& data);
    
private:
    std::string connection_string_;
    bool is_connected_ = false;
    
    // Database-specific helpers
    std::string build_influx_query(const std::string& measurement,
                                  std::chrono::system_clock::time_point start_time,
                                  std::chrono::system_clock::time_point end_time,
                                  const std::vector<std::string>& tags);
};

// Unified data loader that can handle multiple sources
class DataLoader {
public:
    DataLoader();
    ~DataLoader();
    
    // Initialize with configuration
    bool initialize(const DataLoaderConfig& config);
    
    // Load data based on configuration
    bool load_data(std::vector<MarketDataPoint>& market_data);
    bool load_data(std::vector<MarketDataPoint>& market_data,
                  std::vector<TradeData>& trade_data);
    
    // Load specific data ranges
    bool load_data_range(std::chrono::system_clock::time_point start_time,
                        std::chrono::system_clock::time_point end_time,
                        const std::vector<std::string>& symbols,
                        std::vector<MarketDataPoint>& data);
    
    // Data preprocessing and filtering
    std::vector<MarketDataPoint> filter_by_time_range(
        const std::vector<MarketDataPoint>& data,
        std::chrono::system_clock::time_point start_time,
        std::chrono::system_clock::time_point end_time);
    
    std::vector<MarketDataPoint> filter_by_symbols(
        const std::vector<MarketDataPoint>& data,
        const std::vector<std::string>& symbols);
    
    std::vector<MarketDataPoint> resample_data(
        const std::vector<MarketDataPoint>& data,
        const std::string& target_interval); // "1m" -> "5m", "1h", etc.
    
    // Data quality checks
    struct DataQualityReport {
        size_t total_records;
        size_t missing_records;
        size_t duplicate_records;
        size_t invalid_records;
        std::chrono::system_clock::time_point first_timestamp;
        std::chrono::system_clock::time_point last_timestamp;
        std::vector<std::string> symbols_found;
        std::vector<std::string> exchanges_found;
        double data_completeness_ratio; // 0.0 to 1.0
    };
    
    DataQualityReport analyze_data_quality(const std::vector<MarketDataPoint>& data);
    std::vector<MarketDataPoint> clean_and_validate_data(const std::vector<MarketDataPoint>& data);
    
    // Progress monitoring
    struct LoadProgress {
        size_t total_expected;
        size_t current_loaded;
        std::string current_status;
        std::chrono::system_clock::time_point start_time;
        double progress_percentage;
    };
    
    using ProgressCallback = std::function<void(const LoadProgress&)>;
    void set_progress_callback(ProgressCallback callback);
    
    // Caching for performance
    void enable_caching(const std::string& cache_dir);
    void clear_cache();
    
private:
    DataLoaderConfig config_;
    std::unique_ptr<CsvDataLoader> csv_loader_;
    std::unique_ptr<ApiDataLoader> api_loader_;
    std::unique_ptr<DatabaseDataLoader> db_loader_;
    
    ProgressCallback progress_callback_;
    std::string cache_dir_;
    bool caching_enabled_ = false;
    
    // Helper methods
    bool load_from_csv(std::vector<MarketDataPoint>& data);
    bool load_from_api(std::vector<MarketDataPoint>& data);
    bool load_from_database(std::vector<MarketDataPoint>& data);
    
    // Data validation helpers
    bool is_valid_market_data_point(const MarketDataPoint& point);
    std::vector<MarketDataPoint> remove_duplicates(const std::vector<MarketDataPoint>& data);
    std::vector<MarketDataPoint> fill_missing_data(const std::vector<MarketDataPoint>& data);
    
    // Caching helpers
    std::string generate_cache_key(const DataLoaderConfig& config);
    bool load_from_cache(const std::string& cache_key, std::vector<MarketDataPoint>& data);
    bool save_to_cache(const std::string& cache_key, const std::vector<MarketDataPoint>& data);
};

// Exception classes for data loading
class DataLoaderException : public std::exception {
public:
    explicit DataLoaderException(const std::string& message) : message_(message) {}
    const char* what() const noexcept override { return message_.c_str(); }
    
private:
    std::string message_;
};

class CsvParsingException : public DataLoaderException {
public:
    explicit CsvParsingException(const std::string& message) 
        : DataLoaderException("CSV Parsing Error: " + message) {}
};

class ApiLoadingException : public DataLoaderException {
public:
    explicit ApiLoadingException(const std::string& message) 
        : DataLoaderException("API Loading Error: " + message) {}
};

class DatabaseConnectionException : public DataLoaderException {
public:
    explicit DatabaseConnectionException(const std::string& message) 
        : DataLoaderException("Database Connection Error: " + message) {}
};

} // namespace backtest
} // namespace ats