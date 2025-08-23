#include "../include/data_loader.hpp"
#include "utils/logger.hpp"
#include <set>
#include <sstream>
#include <fstream>
#include <algorithm>
#include <regex>
#include <filesystem>
#include <iomanip>
#include <ctime>

namespace ats {
namespace backtest {

// CsvDataLoader Implementation
CsvDataLoader::CsvDataLoader() {
    // Set default CSV format
    csv_format_.delimiter = ',';
    csv_format_.has_header = true;
    csv_format_.timestamp_format = "%Y-%m-%d %H:%M:%S";
    csv_format_.timezone = "UTC";
}

CsvDataLoader::~CsvDataLoader() = default;

bool CsvDataLoader::load_market_data(const std::string& file_path, 
                                    std::vector<MarketDataPoint>& data,
                                    const std::string& format) {
    try {
        if (!std::filesystem::exists(file_path)) {
            ATS_LOG_ERROR("CSV file does not exist: {}", file_path);
            return false;
        }
        
        std::ifstream file(file_path);
        if (!file.is_open()) {
            ATS_LOG_ERROR("Failed to open CSV file: {}", file_path);
            return false;
        }
        
        std::string line;
        std::vector<std::string> headers;
        bool first_line = true;
        size_t line_number = 0;
        
        data.clear();
        
        while (std::getline(file, line)) {
            line_number++;
            
            if (line.empty()) continue;
            
            auto columns = parse_csv_line(line);
            
            if (first_line && csv_format_.has_header) {
                headers = columns;
                first_line = false;
                continue;
            }
            first_line = false;
            
            try {
                MarketDataPoint point;
                
                if (format == "ohlcv") {
                    // Expected columns: timestamp, symbol, exchange, open, high, low, close, volume
                    if (columns.size() < 7) {
                        ATS_LOG_WARN("Insufficient columns in line {}: {}", line_number, line);
                        continue;
                    }
                    
                    point.timestamp = parse_timestamp(columns[0]);
                    point.symbol = columns[1];
                    point.exchange = columns[2];
                    point.open_price = parse_double(columns[3]);
                    point.high_price = parse_double(columns[4]);
                    point.low_price = parse_double(columns[5]);
                    point.close_price = parse_double(columns[6]);
                    if (columns.size() > 7) {
                        point.volume = parse_double(columns[7]);
                    }
                    
                } else if (format == "tick") {
                    // Expected columns: timestamp, symbol, exchange, price, volume, bid, ask
                    if (columns.size() < 5) {
                        ATS_LOG_WARN("Insufficient columns in line {}: {}", line_number, line);
                        continue;
                    }
                    
                    point.timestamp = parse_timestamp(columns[0]);
                    point.symbol = columns[1];
                    point.exchange = columns[2];
                    point.close_price = parse_double(columns[3]);
                    point.volume = parse_double(columns[4]);
                    
                    if (columns.size() > 5) {
                        point.bid_price = parse_double(columns[5]);
                    }
                    if (columns.size() > 6) {
                        point.ask_price = parse_double(columns[6]);
                    }
                    
                } else {
                    ATS_LOG_ERROR("Unsupported CSV format: {}", format);
                    return false;
                }
                
                data.push_back(point);
                
            } catch (const std::exception& e) {
                ATS_LOG_WARN("Error parsing line {}: {} ({})", line_number, line, e.what());
                continue;
            }
        }
        
        ATS_LOG_INFO("Loaded {} market data points from {}", data.size(), file_path);
        return true;
        
    } catch (const std::exception& e) {
        ATS_LOG_ERROR("Failed to load CSV data from {}: {}", file_path, e.what());
        return false;
    }
}

bool CsvDataLoader::load_trade_data(const std::string& file_path,
                                   std::vector<TradeData>& data) {
    try {
        std::ifstream file(file_path);
        if (!file.is_open()) {
            ATS_LOG_ERROR("Failed to open trade CSV file: {}", file_path);
            return false;
        }
        
        std::string line;
        bool first_line = true;
        size_t line_number = 0;
        
        data.clear();
        
        while (std::getline(file, line)) {
            line_number++;
            
            if (line.empty()) continue;
            
            if (first_line && csv_format_.has_header) {
                first_line = false;
                continue;
            }
            first_line = false;
            
            auto columns = parse_csv_line(line);
            
            // Expected columns: timestamp, symbol, exchange, side, price, quantity, fee
            if (columns.size() < 6) {
                ATS_LOG_WARN("Insufficient columns in trade line {}: {}", line_number, line);
                continue;
            }
            
            try {
                TradeData trade;
                trade.timestamp = parse_timestamp(columns[0]);
                trade.symbol = columns[1];
                trade.exchange = columns[2];
                trade.side = columns[3];
                trade.price = parse_double(columns[4]);
                trade.quantity = parse_double(columns[5]);
                
                if (columns.size() > 6) {
                    trade.fee = parse_double(columns[6]);
                }
                if (columns.size() > 7) {
                    trade.trade_id = columns[7];
                }
                
                data.push_back(trade);
                
            } catch (const std::exception& e) {
                ATS_LOG_WARN("Error parsing trade line {}: {} ({})", line_number, line, e.what());
                continue;
            }
        }
        
        ATS_LOG_INFO("Loaded {} trade data points from {}", data.size(), file_path);
        return true;
        
    } catch (const std::exception& e) {
        ATS_LOG_ERROR("Failed to load trade CSV data from {}: {}", file_path, e.what());
        return false;
    }
}

std::vector<std::string> CsvDataLoader::parse_csv_line(const std::string& line) {
    std::vector<std::string> result;
    std::stringstream ss(line);
    std::string cell;
    
    while (std::getline(ss, cell, csv_format_.delimiter)) {
        // Remove leading/trailing whitespace
        cell.erase(0, cell.find_first_not_of(" \t\r\n"));
        cell.erase(cell.find_last_not_of(" \t\r\n") + 1);
        
        // Remove quotes if present
        if (cell.front() == '"' && cell.back() == '"') {
            cell = cell.substr(1, cell.length() - 2);
        }
        
        result.push_back(cell);
    }
    
    return result;
}

std::chrono::system_clock::time_point CsvDataLoader::parse_timestamp(const std::string& timestamp_str) {
    std::tm tm = {};
    std::istringstream ss(timestamp_str);
    
    // Try different timestamp formats
    ss >> std::get_time(&tm, csv_format_.timestamp_format.c_str());
    
    if (ss.fail()) {
        // Try Unix timestamp
        try {
            int64_t unix_timestamp = std::stoll(timestamp_str);
            return std::chrono::system_clock::from_time_t(unix_timestamp);
        } catch (...) {
            throw CsvParsingException("Invalid timestamp format: " + timestamp_str);
        }
    }
    
    return std::chrono::system_clock::from_time_t(std::mktime(&tm));
}

double CsvDataLoader::parse_double(const std::string& value) {
    if (!is_valid_number(value)) {
        throw CsvParsingException("Invalid number format: " + value);
    }
    
    try {
        return std::stod(value);
    } catch (const std::exception& e) {
        throw CsvParsingException("Failed to parse number: " + value);
    }
}

bool CsvDataLoader::is_valid_number(const std::string& str) {
    if (str.empty()) return false;
    
    std::regex number_regex(R"(^[-+]?[0-9]*\.?[0-9]+([eE][-+]?[0-9]+)?$)");
    return std::regex_match(str, number_regex);
}

void CsvDataLoader::set_csv_format(const CsvFormat& format) {
    csv_format_ = format;
}

CsvDataLoader::CsvFormat CsvDataLoader::get_csv_format() const {
    return csv_format_;
}

bool CsvDataLoader::validate_csv_file(const std::string& file_path) {
    try {
        if (!std::filesystem::exists(file_path)) {
            return false;
        }
        
        std::ifstream file(file_path);
        if (!file.is_open()) {
            return false;
        }
        
        std::string line;
        if (!std::getline(file, line)) {
            return false; // Empty file
        }
        
        // Check if we can parse at least the first line
        auto columns = parse_csv_line(line);
        return !columns.empty();
        
    } catch (const std::exception& e) {
        ATS_LOG_ERROR("CSV validation failed for {}: {}", file_path, e.what());
        return false;
    }
}

// ApiDataLoader Implementation
ApiDataLoader::ApiDataLoader() = default;
ApiDataLoader::~ApiDataLoader() = default;

bool ApiDataLoader::initialize(const std::unordered_map<std::string, std::string>& api_configs) {
    api_configs_ = api_configs;
    
    ATS_LOG_INFO("ApiDataLoader initialized with {} exchange configurations", api_configs_.size());
    return true;
}

bool ApiDataLoader::load_historical_data(const std::string& exchange,
                                        const std::string& symbol,
                                        const std::string& interval,
                                        std::chrono::system_clock::time_point start_time,
                                        std::chrono::system_clock::time_point end_time,
                                        std::vector<MarketDataPoint>& data) {
    try {
        std::string exchange_lower = exchange;
        std::transform(exchange_lower.begin(), exchange_lower.end(), 
                      exchange_lower.begin(), ::tolower);
        
        if (exchange_lower == "binance") {
            return load_binance_data(symbol, interval, start_time, end_time, data);
        } else if (exchange_lower == "upbit") {
            return load_upbit_data(symbol, interval, start_time, end_time, data);
        } else {
            ATS_LOG_ERROR("Unsupported exchange: {}", exchange);
            return false;
        }
        
    } catch (const std::exception& e) {
        ATS_LOG_ERROR("Failed to load historical data from {}: {}", exchange, e.what());
        return false;
    }
}

bool ApiDataLoader::load_binance_data(const std::string& symbol, const std::string& interval,
                                     std::chrono::system_clock::time_point start_time,
                                     std::chrono::system_clock::time_point end_time,
                                     std::vector<MarketDataPoint>& data) {
    try {
        std::string url = build_binance_url(symbol, interval, start_time, end_time);
        std::string response = make_http_request(url);
        
        if (response.empty()) {
            ATS_LOG_ERROR("Empty response from Binance API");
            return false;
        }
        
        data = parse_binance_response(response, symbol);
        
        ATS_LOG_INFO("Loaded {} data points from Binance for {}", data.size(), symbol);
        return true;
        
    } catch (const std::exception& e) {
        ATS_LOG_ERROR("Failed to load Binance data for {}: {}", symbol, e.what());
        return false;
    }
}

std::string ApiDataLoader::build_binance_url(const std::string& symbol, const std::string& interval,
                                           std::chrono::system_clock::time_point start_time,
                                           std::chrono::system_clock::time_point end_time) {
    // Convert timestamps to milliseconds
    auto start_ms = std::chrono::duration_cast<std::chrono::milliseconds>(
        start_time.time_since_epoch()).count();
    auto end_ms = std::chrono::duration_cast<std::chrono::milliseconds>(
        end_time.time_since_epoch()).count();
    
    std::ostringstream url;
    url << "https://api.binance.com/api/v3/klines";
    url << "?symbol=" << symbol;
    url << "&interval=" << interval;
    url << "&startTime=" << start_ms;
    url << "&endTime=" << end_ms;
    url << "&limit=1000";
    
    return url.str();
}

std::string ApiDataLoader::make_http_request(const std::string& url, 
                                           const std::unordered_map<std::string, std::string>& headers) {
    // Simplified HTTP request implementation
    // In production, would use proper HTTP client library like cpprestsdk or curl
    
    // For now, return empty string - this would be implemented with actual HTTP client
    ATS_LOG_INFO("Making HTTP request to: {}", url);
    
    // TODO: Implement actual HTTP request using curl or similar library
    return ""; // Placeholder
}

std::vector<MarketDataPoint> ApiDataLoader::parse_binance_response(const std::string& json_response, 
                                                                  const std::string& symbol) {
    std::vector<MarketDataPoint> data;
    
    // TODO: Implement JSON parsing using nlohmann/json or similar library
    // Binance klines format: [timestamp, open, high, low, close, volume, ...]
    
    return data; // Placeholder
}

// DataLoader Implementation
DataLoader::DataLoader() {
    csv_loader_ = std::make_unique<CsvDataLoader>();
    api_loader_ = std::make_unique<ApiDataLoader>();
    db_loader_ = std::make_unique<DatabaseDataLoader>();
}

DataLoader::~DataLoader() = default;

bool DataLoader::initialize(const DataLoaderConfig& config) {
    config_ = config;
    
    ATS_LOG_INFO("DataLoader initialized with source: {}", config_.data_source);
    
    if (config_.data_source == "api") {
        // Initialize API loader with credentials if available
        std::unordered_map<std::string, std::string> api_configs;
        // TODO: Load API configurations from secure storage
        return api_loader_->initialize(api_configs);
    } else if (config_.data_source == "database") {
        return db_loader_->connect(config_.database_connection);
    }
    
    return true;
}

bool DataLoader::load_data(std::vector<MarketDataPoint>& market_data,
                          std::vector<TradeData>& trade_data) {
    try {
        market_data.clear();
        trade_data.clear();
        
        bool success = false;
        
        if (config_.data_source == "csv") {
            success = load_from_csv(market_data);
        } else if (config_.data_source == "api") {
            success = load_from_api(market_data);
        } else if (config_.data_source == "database") {
            success = load_from_database(market_data);
        } else {
            ATS_LOG_ERROR("Unsupported data source: {}", config_.data_source);
            return false;
        }
        
        if (success && !market_data.empty()) {
            // Clean and validate data
            market_data = clean_and_validate_data(market_data);
            
            // Apply filters if specified
            if (config_.start_date != std::chrono::system_clock::time_point{} ||
                config_.end_date != std::chrono::system_clock::time_point{}) {
                market_data = filter_by_time_range(market_data, config_.start_date, config_.end_date);
            }
            
            if (!config_.symbols.empty()) {
                market_data = filter_by_symbols(market_data, config_.symbols);
            }
            
            ATS_LOG_INFO("Loaded and processed {} market data points", market_data.size());
        }
        
        return success;
        
    } catch (const std::exception& e) {
        ATS_LOG_ERROR("Failed to load data: {}", e.what());
        return false;
    }
}

bool DataLoader::load_from_csv(std::vector<MarketDataPoint>& data) {
    if (config_.file_path.empty()) {
        ATS_LOG_ERROR("CSV file path not specified");
        return false;
    }
    
    return csv_loader_->load_market_data(config_.file_path, data);
}

bool DataLoader::load_from_api(std::vector<MarketDataPoint>& data) {
    if (config_.symbols.empty() || config_.exchanges.empty()) {
        ATS_LOG_ERROR("Symbols or exchanges not specified for API loading");
        return false;
    }
    
    // Load data for each symbol from each exchange
    for (const auto& exchange : config_.exchanges) {
        for (const auto& symbol : config_.symbols) {
            std::vector<MarketDataPoint> symbol_data;
            
            bool success = api_loader_->load_historical_data(
                exchange, symbol, config_.time_interval,
                config_.start_date, config_.end_date, symbol_data);
            
            if (success) {
                data.insert(data.end(), symbol_data.begin(), symbol_data.end());
            } else {
                ATS_LOG_WARN("Failed to load data for {}/{}", exchange, symbol);
            }
        }
    }
    
    return !data.empty();
}

bool DataLoader::load_from_database(std::vector<MarketDataPoint>& data) {
    // TODO: Implement database loading
    ATS_LOG_INFO("Database loading not yet implemented");
    return false;
}

std::vector<MarketDataPoint> DataLoader::filter_by_time_range(
    const std::vector<MarketDataPoint>& data,
    std::chrono::system_clock::time_point start_time,
    std::chrono::system_clock::time_point end_time) {
    
    std::vector<MarketDataPoint> filtered;
    
    for (const auto& point : data) {
        if ((start_time == std::chrono::system_clock::time_point{} || point.timestamp >= start_time) &&
            (end_time == std::chrono::system_clock::time_point{} || point.timestamp <= end_time)) {
            filtered.push_back(point);
        }
    }
    
    return filtered;
}

std::vector<MarketDataPoint> DataLoader::filter_by_symbols(
    const std::vector<MarketDataPoint>& data,
    const std::vector<std::string>& symbols) {
    
    std::vector<MarketDataPoint> filtered;
    
    for (const auto& point : data) {
        if (std::find(symbols.begin(), symbols.end(), point.symbol) != symbols.end()) {
            filtered.push_back(point);
        }
    }
    
    return filtered;
}

std::vector<MarketDataPoint> DataLoader::clean_and_validate_data(
    const std::vector<MarketDataPoint>& data) {
    
    std::vector<MarketDataPoint> cleaned;
    
    for (const auto& point : data) {
        if (is_valid_market_data_point(point)) {
            cleaned.push_back(point);
        }
    }
    
    // Remove duplicates
    cleaned = remove_duplicates(cleaned);
    
    // Sort by timestamp
    std::sort(cleaned.begin(), cleaned.end(), 
              [](const MarketDataPoint& a, const MarketDataPoint& b) {
                  return a.timestamp < b.timestamp;
              });
    
    return cleaned;
}

bool DataLoader::is_valid_market_data_point(const MarketDataPoint& point) {
    // Basic validation
    if (point.symbol.empty() || point.exchange.empty()) {
        return false;
    }
    
    if (point.close_price <= 0.0) {
        return false;
    }
    
    if (point.high_price > 0.0 && point.low_price > 0.0 && 
        point.high_price < point.low_price) {
        return false;
    }
    
    return true;
}

std::vector<MarketDataPoint> DataLoader::remove_duplicates(
    const std::vector<MarketDataPoint>& data) {
    
    std::vector<MarketDataPoint> unique_data;
    std::set<std::tuple<std::chrono::system_clock::time_point, std::string, std::string>> seen;
    
    for (const auto& point : data) {
        auto key = std::make_tuple(point.timestamp, point.symbol, point.exchange);
        if (seen.find(key) == seen.end()) {
            seen.insert(key);
            unique_data.push_back(point);
        }
    }
    
    return unique_data;
}

DataLoader::DataQualityReport DataLoader::analyze_data_quality(
    const std::vector<MarketDataPoint>& data) {
    
    DataQualityReport report;
    report.total_records = data.size();
    report.missing_records = 0;
    report.duplicate_records = 0;
    report.invalid_records = 0;
    
    if (data.empty()) {
        report.data_completeness_ratio = 0.0;
        return report;
    }
    
    // Find time range
    report.first_timestamp = data.front().timestamp;
    report.last_timestamp = data.back().timestamp;
    
    // Collect symbols and exchanges
    std::set<std::string> symbols, exchanges;
    std::set<std::tuple<std::chrono::system_clock::time_point, std::string, std::string>> timestamps;
    
    for (const auto& point : data) {
        symbols.insert(point.symbol);
        exchanges.insert(point.exchange);
        
        auto key = std::make_tuple(point.timestamp, point.symbol, point.exchange);
        if (timestamps.find(key) != timestamps.end()) {
            report.duplicate_records++;
        } else {
            timestamps.insert(key);
        }
        
        if (!is_valid_market_data_point(point)) {
            report.invalid_records++;
        }
    }
    
    report.symbols_found = std::vector<std::string>(symbols.begin(), symbols.end());
    report.exchanges_found = std::vector<std::string>(exchanges.begin(), exchanges.end());
    
    // Calculate completeness ratio
    size_t valid_records = report.total_records - report.duplicate_records - report.invalid_records;
    report.data_completeness_ratio = static_cast<double>(valid_records) / report.total_records;
    
    return report;
}

// DatabaseDataLoader stub implementation
DatabaseDataLoader::DatabaseDataLoader() = default;
DatabaseDataLoader::~DatabaseDataLoader() = default;

bool DatabaseDataLoader::connect(const std::string& connection_string) {
    connection_string_ = connection_string;
    is_connected_ = true;
    ATS_LOG_INFO("Database connection established (stub implementation)");
    return true;
}

void DatabaseDataLoader::disconnect() {
    is_connected_ = false;
    ATS_LOG_INFO("Database disconnected");
}

} // namespace backtest
} // namespace ats