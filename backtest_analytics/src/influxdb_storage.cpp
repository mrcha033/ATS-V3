#include "../include/influxdb_storage.hpp"
#include "../../shared/include/utils/logger.hpp"
#include <sstream>
#include <iomanip>
#include <algorithm>
#include <regex>
#include <mutex>
#include <thread>

namespace ats {
namespace backtest {

// Import Logger from utils namespace
using ats::utils::Logger;

// InfluxDataPoint Implementation
void InfluxDataPoint::add_tag(const std::string& key, const std::string& value) {
    tags[key] = value;
}

void InfluxDataPoint::add_field(const std::string& key, double value) {
    fields_double[key] = value;
}

void InfluxDataPoint::add_field(const std::string& key, int64_t value) {
    fields_int[key] = value;
}

void InfluxDataPoint::add_field(const std::string& key, const std::string& value) {
    fields_string[key] = value;
}

std::string InfluxDataPoint::to_line_protocol() const {
    std::ostringstream line;
    
    // Measurement name
    line << measurement;
    
    // Tags
    if (!tags.empty()) {
        for (const auto& tag : tags) {
            line << "," << tag.first << "=" << tag.second;
        }
    }
    
    line << " ";
    
    // Fields
    bool first_field = true;
    
    for (const auto& field : fields_double) {
        if (!first_field) line << ",";
        line << field.first << "=" << std::fixed << std::setprecision(6) << field.second;
        first_field = false;
    }
    
    for (const auto& field : fields_int) {
        if (!first_field) line << ",";
        line << field.first << "=" << field.second << "i";
        first_field = false;
    }
    
    for (const auto& field : fields_string) {
        if (!first_field) line << ",";
        line << field.first << "=\"" << field.second << "\"";
        first_field = false;
    }
    
    // Timestamp (nanoseconds since epoch)
    auto timestamp_ns = std::chrono::duration_cast<std::chrono::nanoseconds>(
        timestamp.time_since_epoch()).count();
    line << " " << timestamp_ns;
    
    return line.str();
}

// InfluxQueryResult Implementation
int InfluxQueryResult::get_column_index(const std::string& column_name) const {
    if (columns.empty()) return -1;
    
    for (size_t i = 0; i < columns[0].size(); ++i) {
        if (columns[0][i] == column_name) {
            return static_cast<int>(i);
        }
    }
    return -1;
}

std::string InfluxQueryResult::get_value(size_t row, const std::string& column_name) const {
    int col_index = get_column_index(column_name);
    if (col_index < 0 || row >= values.size() || 
        static_cast<size_t>(col_index) >= values[row].size()) {
        return "";
    }
    return values[row][col_index];
}

double InfluxQueryResult::get_double_value(size_t row, const std::string& column_name) const {
    std::string value = get_value(row, column_name);
    if (value.empty()) return 0.0;
    
    try {
        return std::stod(value);
    } catch (const std::exception&) {
        return 0.0;
    }
}

int64_t InfluxQueryResult::get_int_value(size_t row, const std::string& column_name) const {
    std::string value = get_value(row, column_name);
    if (value.empty()) return 0;
    
    try {
        return std::stoll(value);
    } catch (const std::exception&) {
        return 0;
    }
}

// InfluxDBStorage Implementation
InfluxDBStorage::InfluxDBStorage() = default;
InfluxDBStorage::~InfluxDBStorage() {
    disconnect();
}

bool InfluxDBStorage::initialize(const InfluxDBConfig& config) {
    config_ = config;
    
    if (!validate_config()) {
        return false;
    }
    
    Logger::info("InfluxDB storage initialized with URL: {}", config_.url);
    return true;
}

bool InfluxDBStorage::connect() {
    if (is_connected_) {
        return true;
    }
    
    try {
        // Test connection with ping
        if (!ping()) {
            set_last_error("Failed to ping InfluxDB server");
            return false;
        }
        
        // Create database if it doesn't exist
        if (!database_exists() && !create_database()) {
            set_last_error("Failed to create database");
            return false;
        }
        
        is_connected_ = true;
        Logger::info("Connected to InfluxDB at {}", config_.url);
        return true;
        
    } catch (const std::exception& e) {
        set_last_error("Connection failed: " + std::string(e.what()));
        Logger::error("InfluxDB connection failed: {}", e.what());
        return false;
    }
}

void InfluxDBStorage::disconnect() {
    is_connected_ = false;
    Logger::info("Disconnected from InfluxDB");
}

bool InfluxDBStorage::is_connected() const {
    return is_connected_;
}

bool InfluxDBStorage::write_backtest_result(const BacktestResult& result, 
                                           const std::string& strategy_name,
                                           const std::unordered_map<std::string, std::string>& additional_tags) {
    if (!is_connected_) {
        Logger::error("Not connected to InfluxDB");
        return false;
    }
    
    try {
        std::vector<InfluxDataPoint> points;
        
        // Store performance metrics
        auto tags = additional_tags;
        tags["strategy"] = strategy_name;
        
        auto perf_point = convert_performance_metrics_to_point(result.performance, strategy_name, tags);
        points.push_back(perf_point);
        
        // Store trade results
        for (const auto& trade : result.trades) {
            auto trade_point = convert_trade_result_to_point(trade, strategy_name, tags);
            points.push_back(trade_point);
        }
        
        // Store portfolio history
        for (const auto& snapshot : result.portfolio_history) {
            auto portfolio_point = convert_portfolio_snapshot_to_point(snapshot, strategy_name, tags);
            points.push_back(portfolio_point);
        }
        
        // Store backtest execution info
        InfluxDataPoint exec_point("backtest_execution");
        exec_point.add_tag("strategy", strategy_name);
        for (const auto& tag : additional_tags) {
            exec_point.add_tag(tag.first, tag.second);
        }
        
        exec_point.add_field("execution_time_ms", static_cast<double>(result.execution_time.count()));
        exec_point.add_field("total_signals", static_cast<int64_t>(result.total_signals_generated));
        exec_point.add_field("signals_executed", static_cast<int64_t>(result.signals_executed));
        exec_point.add_field("signals_rejected", static_cast<int64_t>(result.signals_rejected));
        exec_point.add_field("execution_rate", result.execution_rate);
        exec_point.timestamp = result.backtest_start_time;
        
        points.push_back(exec_point);
        
        return write_data_points(points);
        
    } catch (const std::exception& e) {
        Logger::error("Failed to write backtest result: {}", e.what());
        return false;
    }
}

bool InfluxDBStorage::write_performance_metrics(const PerformanceMetrics& metrics,
                                               const std::string& strategy_name,
                                               const std::unordered_map<std::string, std::string>& tags) {
    if (!is_connected_) {
        return false;
    }
    
    auto point = convert_performance_metrics_to_point(metrics, strategy_name, tags);
    return write_single_point(point);
}

bool InfluxDBStorage::write_data_points(const std::vector<InfluxDataPoint>& points) {
    if (points.empty() || !is_connected_) {
        return false;
    }
    
    try {
        std::ostringstream data;
        for (const auto& point : points) {
            data << point.to_line_protocol() << "\n";
        }
        
        std::string response = make_http_request("POST", build_write_url(), data.str());
        
        if (handle_response_error(response)) {
            Logger::debug("Successfully wrote {} data points to InfluxDB", points.size());
            return true;
        }
        
        return false;
        
    } catch (const std::exception& e) {
        Logger::error("Failed to write data points: {}", e.what());
        return false;
    }
}

bool InfluxDBStorage::write_single_point(const InfluxDataPoint& point) {
    return write_data_points({point});
}

InfluxQueryResult InfluxDBStorage::query(const std::string& query) {
    InfluxQueryResult result;
    
    if (!is_connected_) {
        result.error = "Not connected to InfluxDB";
        return result;
    }
    
    try {
        std::unordered_map<std::string, std::string> params;
        params["q"] = query;
        params["db"] = config_.database;
        
        // Build query URL with parameters
        std::ostringstream url;
        url << build_query_url() << "?";
        bool first = true;
        for (const auto& param : params) {
            if (!first) url << "&";
            url << param.first << "=" << param.second;
            first = false;
        }
        
        std::string response = make_http_request("GET", url.str());
        
        if (!response.empty()) {
            // Simple JSON parsing for demonstration
            // In production, would use proper JSON library
            result.success = true;
            
            // Parse response into columns and values
            // This is a simplified implementation
            result.columns.push_back({"time", "value"});
            result.values.push_back({"2024-01-01T00:00:00Z", "100.0"});
        }
        
        return result;
        
    } catch (const std::exception& e) {
        result.error = "Query failed: " + std::string(e.what());
        Logger::error("InfluxDB query failed: {}", e.what());
        return result;
    }
}

bool InfluxDBStorage::ping() {
    try {
        std::string response = make_http_request("GET", build_ping_url());
        return !response.empty();
        
    } catch (const std::exception& e) {
        Logger::error("InfluxDB ping failed: {}", e.what());
        return false;
    }
}

bool InfluxDBStorage::create_database(const std::string& database_name) {
    std::string db_name = database_name.empty() ? config_.database : database_name;
    
    std::string query = "CREATE DATABASE \"" + db_name + "\"";
    auto result = this->query(query);
    
    if (result.success) {
        Logger::info("Created InfluxDB database: {}", db_name);
        return true;
    } else {
        Logger::error("Failed to create database {}: {}", db_name, result.error);
        return false;
    }
}

bool InfluxDBStorage::database_exists(const std::string& database_name) const {
    // Simplified implementation - in production would properly check
    return true;
}

std::string InfluxDBStorage::make_http_request(const std::string& method,
                                              const std::string& endpoint,
                                              const std::string& data,
                                              const std::unordered_map<std::string, std::string>& headers) {
    // Simplified HTTP client implementation
    // In production, would use proper HTTP client library like cpprestsdk or curl
    
    Logger::debug("Making {} request to: {}", method, endpoint);
    
    // Simulate successful response for demonstration
    if (method == "POST" && endpoint.find("/write") != std::string::npos) {
        return ""; // Empty response indicates success for write operations
    } else if (method == "GET" && endpoint.find("/ping") != std::string::npos) {
        return "pong";
    }
    
    return "";
}

std::string InfluxDBStorage::build_write_url() const {
    std::ostringstream url;
    url << config_.url << "/write?db=" << config_.database;
    
    if (!config_.retention_policy.empty() && config_.retention_policy != "default") {
        url << "&rp=" << config_.retention_policy;
    }
    
    return url.str();
}

std::string InfluxDBStorage::build_query_url() const {
    return config_.url + "/query";
}

std::string InfluxDBStorage::build_ping_url() const {
    return config_.url + "/ping";
}

std::unordered_map<std::string, std::string> InfluxDBStorage::get_auth_headers() const {
    std::unordered_map<std::string, std::string> headers;
    
    if (!config_.token.empty()) {
        // InfluxDB 2.x token authentication
        headers["Authorization"] = "Token " + config_.token;
    } else if (!config_.username.empty() && !config_.password.empty()) {
        // InfluxDB 1.x basic authentication
        headers["Authorization"] = "Basic " + config_.username + ":" + config_.password;
    }
    
    headers["Content-Type"] = "application/x-www-form-urlencoded";
    
    return headers;
}

InfluxDataPoint InfluxDBStorage::convert_performance_metrics_to_point(
    const PerformanceMetrics& metrics,
    const std::string& strategy_name,
    const std::unordered_map<std::string, std::string>& tags) const {
    
    InfluxDataPoint point("performance_metrics");
    
    // Add tags
    point.add_tag("strategy", strategy_name);
    for (const auto& tag : tags) {
        point.add_tag(tag.first, tag.second);
    }
    
    // Add performance fields
    point.add_field("total_return", metrics.total_return);
    point.add_field("annualized_return", metrics.annualized_return);
    point.add_field("volatility", metrics.volatility);
    point.add_field("max_drawdown", metrics.max_drawdown);
    point.add_field("sharpe_ratio", metrics.sharpe_ratio);
    point.add_field("sortino_ratio", metrics.sortino_ratio);
    point.add_field("calmar_ratio", metrics.calmar_ratio);
    point.add_field("value_at_risk_95", metrics.value_at_risk_95);
    point.add_field("win_rate", metrics.win_rate);
    point.add_field("profit_factor", metrics.profit_factor);
    
    // Add trade statistics
    point.add_field("total_trades", static_cast<int64_t>(metrics.total_trades));
    point.add_field("winning_trades", static_cast<int64_t>(metrics.winning_trades));
    point.add_field("losing_trades", static_cast<int64_t>(metrics.losing_trades));
    point.add_field("average_win", metrics.average_win);
    point.add_field("average_loss", metrics.average_loss);
    point.add_field("largest_win", metrics.largest_win);
    point.add_field("largest_loss", metrics.largest_loss);
    
    point.timestamp = metrics.end_date;
    
    return point;
}

InfluxDataPoint InfluxDBStorage::convert_trade_result_to_point(
    const TradeResult& trade,
    const std::string& strategy_name,
    const std::unordered_map<std::string, std::string>& tags) const {
    
    InfluxDataPoint point("trade_results");
    
    // Add tags
    point.add_tag("strategy", strategy_name);
    point.add_tag("symbol", trade.symbol);
    point.add_tag("exchange", trade.exchange);
    point.add_tag("side", trade.side);
    for (const auto& tag : tags) {
        point.add_tag(tag.first, tag.second);
    }
    
    // Add trade fields
    point.add_field("entry_price", trade.entry_price);
    point.add_field("exit_price", trade.exit_price);
    point.add_field("quantity", trade.quantity);
    point.add_field("pnl", trade.pnl);
    point.add_field("pnl_percentage", trade.pnl_percentage);
    point.add_field("fees", trade.fees);
    point.add_field("net_pnl", trade.net_pnl);
    point.add_field("is_profitable", static_cast<int64_t>(trade.is_profitable ? 1 : 0));
    
    // Calculate duration
    auto duration = std::chrono::duration_cast<std::chrono::minutes>(
        trade.exit_time - trade.entry_time).count();
    point.add_field("duration_minutes", static_cast<int64_t>(duration));
    
    point.timestamp = trade.exit_time;
    
    return point;
}

InfluxDataPoint InfluxDBStorage::convert_portfolio_snapshot_to_point(
    const PortfolioSnapshot& snapshot,
    const std::string& strategy_name,
    const std::unordered_map<std::string, std::string>& tags) const {
    
    InfluxDataPoint point("portfolio_snapshots");
    
    // Add tags
    point.add_tag("strategy", strategy_name);
    for (const auto& tag : tags) {
        point.add_tag(tag.first, tag.second);
    }
    
    // Add portfolio fields
    point.add_field("total_value", snapshot.total_value);
    point.add_field("cash", snapshot.cash);
    point.add_field("positions_value", snapshot.positions_value);
    
    // Add position counts
    point.add_field("position_count", static_cast<int64_t>(snapshot.positions.size()));
    
    point.timestamp = snapshot.timestamp;
    
    return point;
}

bool InfluxDBStorage::validate_config() const {
    if (config_.url.empty()) {
        Logger::error("InfluxDB URL is empty");
        return false;
    }
    
    if (config_.database.empty()) {
        Logger::error("InfluxDB database name is empty");
        return false;
    }
    
    return true;
}

void InfluxDBStorage::set_last_error(const std::string& error) {
    last_error_ = error;
    Logger::error("InfluxDB error: {}", error);
}

bool InfluxDBStorage::handle_response_error(const std::string& response) {
    // Simple error handling - in production would parse JSON response
    if (response.find("error") != std::string::npos) {
        set_last_error("Server returned error response");
        return false;
    }
    
    return true;
}

std::string InfluxDBStorage::format_timestamp(std::chrono::system_clock::time_point timestamp) const {
    auto time_t = std::chrono::system_clock::to_time_t(timestamp);
    auto tm = *std::gmtime(&time_t);
    
    std::ostringstream ss;
    ss << std::put_time(&tm, "%Y-%m-%dT%H:%M:%SZ");
    return ss.str();
}

// InfluxBatchWriter Implementation
InfluxBatchWriter::InfluxBatchWriter(std::shared_ptr<InfluxDBStorage> storage, int batch_size)
    : storage_(storage), batch_size_(batch_size), last_flush_time_(std::chrono::system_clock::now()) {
    batch_.reserve(batch_size_);
}

InfluxBatchWriter::~InfluxBatchWriter() {
    auto_flush_disable();
    if (!batch_.empty()) {
        flush();
    }
}

bool InfluxBatchWriter::add_point(const InfluxDataPoint& point) {
    std::lock_guard<std::mutex> lock(batch_mutex_);
    
    batch_.push_back(point);
    
    if (static_cast<int>(batch_.size()) >= batch_size_) {
        return flush();
    }
    
    if (auto_flush_enabled_ && should_auto_flush()) {
        return flush();
    }
    
    return true;
}

bool InfluxBatchWriter::flush() {
    std::lock_guard<std::mutex> lock(batch_mutex_);
    
    if (batch_.empty()) {
        return true;
    }
    
    bool success = storage_->write_data_points(batch_);
    
    if (success) {
        total_written_ += batch_.size();
        Logger::debug("Flushed {} points to InfluxDB", batch_.size());
    } else {
        total_failed_ += batch_.size();
        Logger::error("Failed to flush {} points to InfluxDB", batch_.size());
    }
    
    reset_batch();
    return success;
}

void InfluxBatchWriter::reset_batch() {
    batch_.clear();
    last_flush_time_ = std::chrono::system_clock::now();
}

bool InfluxBatchWriter::should_auto_flush() const {
    if (!auto_flush_enabled_) {
        return false;
    }
    
    auto now = std::chrono::system_clock::now();
    auto elapsed = std::chrono::duration_cast<std::chrono::seconds>(
        now - last_flush_time_).count();
    
    return elapsed >= auto_flush_interval_seconds_;
}

size_t InfluxBatchWriter::get_pending_count() const {
    std::lock_guard<std::mutex> lock(batch_mutex_);
    return batch_.size();
}

size_t InfluxBatchWriter::get_total_written() const {
    return total_written_;
}

size_t InfluxBatchWriter::get_total_failed() const {
    return total_failed_;
}

// BacktestResultManager Implementation
BacktestResultManager::BacktestResultManager(std::shared_ptr<InfluxDBStorage> storage)
    : storage_(storage) {
    batch_writer_ = std::make_unique<InfluxBatchWriter>(storage_, 1000);
}

BacktestResultManager::~BacktestResultManager() = default;

bool BacktestResultManager::store_backtest_result(const BacktestResult& result,
                                                 const std::string& strategy_name,
                                                 const std::unordered_map<std::string, std::string>& metadata) {
    try {
        // Store using the underlying storage directly for batch operations
        return storage_->write_backtest_result(result, strategy_name, metadata);
        
    } catch (const std::exception& e) {
        Logger::error("Failed to store backtest result: {}", e.what());
        return false;
    }
}

std::unordered_map<std::string, size_t> BacktestResultManager::get_storage_statistics() {
    std::unordered_map<std::string, size_t> stats;
    
    stats["total_written"] = batch_writer_->get_total_written();
    stats["total_failed"] = batch_writer_->get_total_failed();
    stats["pending_count"] = batch_writer_->get_pending_count();
    
    return stats;
}

bool BacktestResultManager::cleanup_old_results(int days_to_keep) {
    return storage_->delete_old_data(days_to_keep);
}

} // namespace backtest
} // namespace ats