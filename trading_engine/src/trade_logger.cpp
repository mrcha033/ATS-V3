#include "redis_subscriber.hpp"
#include "utils/logger.hpp"
#include <curl/curl.h>
#include <fstream>
#include <iomanip>
#include <sstream>
#include <filesystem>
#include <thread>
#include <queue>

namespace ats {
namespace trading_engine {

// TradeLogger Implementation
struct TradeLogger::Implementation {
    std::string influxdb_url;
    std::string database_name;
    std::string log_directory;
    
    // Configuration
    size_t batch_size = 100;
    std::chrono::seconds flush_interval{10};
    bool file_logging_enabled = true;
    bool database_logging_enabled = true;
    
    // Buffering
    std::queue<std::string> pending_logs;
    std::mutex pending_mutex;
    std::condition_variable pending_cv;
    
    // Background processing
    std::thread background_thread;
    std::atomic<bool> running{false};
    
    // Statistics
    std::atomic<size_t> total_logs_written{0};
    std::atomic<bool> healthy{true};
    
    // File handles
    std::unordered_map<std::string, std::unique_ptr<std::ofstream>> log_files;
    mutable std::shared_mutex mutex;
};

TradeLogger::TradeLogger() : impl_(std::make_unique<Implementation>()) {}

TradeLogger::~TradeLogger() {
    if (impl_->running) {
        impl_->running = false;
        impl_->pending_cv.notify_all();
        if (impl_->background_thread.joinable()) {
            impl_->background_thread.join();
        }
    }
}

bool TradeLogger::initialize(const std::string& influxdb_url, const std::string& database) {
    std::unique_lock<std::shared_mutex> lock(impl_->mutex);
    
    impl_->influxdb_url = influxdb_url;
    impl_->database_name = database;
    impl_->running = true;
    
    // Start background processing thread
    impl_->background_thread = std::thread([this]() {
        process_pending_logs();
    });
    
    utils::Logger::info("TradeLogger initialized with InfluxDB: {}", influxdb_url);
    return true;
}

bool TradeLogger::initialize_file_logging(const std::string& log_directory) {
    std::unique_lock<std::shared_mutex> lock(impl_->mutex);
    
    impl_->log_directory = log_directory;
    
    // Create directory if it doesn't exist
    if (!std::filesystem::exists(log_directory)) {
        std::filesystem::create_directories(log_directory);
    }
    
    utils::Logger::info("File logging initialized in directory: {}", log_directory);
    return true;
}

bool TradeLogger::log_trade_execution(const TradeExecution& execution) {
    if (!impl_->database_logging_enabled && !impl_->file_logging_enabled) {
        return true; // Nothing to do
    }
    
    try {
        std::string log_entry;
        
        if (impl_->database_logging_enabled) {
            log_entry = trade_execution_to_line_protocol(execution);
            
            std::lock_guard<std::mutex> lock(impl_->pending_mutex);
            impl_->pending_logs.push(log_entry);
        }
        
        if (impl_->file_logging_enabled) {
            std::string csv_entry = trade_execution_to_csv(execution);
            write_to_file(csv_entry);
        }
        
        impl_->pending_cv.notify_one();
        return true;
        
    } catch (const std::exception& e) {
        utils::Logger::error("Failed to log trade execution: {}", e.what());
        return false;
    }
}

bool TradeLogger::log_arbitrage_opportunity(const ArbitrageOpportunity& opportunity) {
    try {
        std::string line_protocol = arbitrage_opportunity_to_line_protocol(opportunity);
        
        std::lock_guard<std::mutex> lock(impl_->pending_mutex);
        impl_->pending_logs.push(line_protocol);
        impl_->pending_cv.notify_one();
        
        return true;
    } catch (const std::exception& e) {
        utils::Logger::error("Failed to log arbitrage opportunity: {}", e.what());
        return false;
    }
}

bool TradeLogger::log_order_execution(const OrderExecutionDetails& order_details) {
    try {
        std::string line_protocol = order_execution_to_line_protocol(order_details);
        
        std::lock_guard<std::mutex> lock(impl_->pending_mutex);
        impl_->pending_logs.push(line_protocol);
        impl_->pending_cv.notify_one();
        
        return true;
    } catch (const std::exception& e) {
        utils::Logger::error("Failed to log order execution: {}", e.what());
        return false;
    }
}

bool TradeLogger::log_trade_executions_batch(const std::vector<TradeExecution>& executions) {
    if (executions.empty()) return true;
    
    try {
        std::vector<std::string> batch_logs;
        batch_logs.reserve(executions.size());
        
        for (const auto& execution : executions) {
            if (impl_->database_logging_enabled) {
                batch_logs.push_back(trade_execution_to_line_protocol(execution));
            }
            
            if (impl_->file_logging_enabled) {
                write_to_file(trade_execution_to_csv(execution));
            }
        }
        
        if (!batch_logs.empty()) {
            std::lock_guard<std::mutex> lock(impl_->pending_mutex);
            for (const auto& log : batch_logs) {
                impl_->pending_logs.push(log);
            }
            impl_->pending_cv.notify_one();
        }
        
        return true;
    } catch (const std::exception& e) {
        utils::Logger::error("Failed to log trade executions batch: {}", e.what());
        return false;
    }
}

bool TradeLogger::log_performance_metrics(const TradingStatistics& stats) {
    try {
        std::ostringstream oss;
        auto timestamp = std::chrono::duration_cast<std::chrono::nanoseconds>(
            std::chrono::system_clock::now().time_since_epoch()).count();
        
        oss << "trading_performance"
            << " total_opportunities=" << stats.total_opportunities_detected.load()
            << ",total_executed=" << stats.total_opportunities_executed.load()
            << ",successful_trades=" << stats.total_successful_trades.load()
            << ",failed_trades=" << stats.total_failed_trades.load()
            << ",total_profit=" << stats.total_profit_loss.load()
            << ",total_fees=" << stats.total_fees_paid.load()
            << ",success_rate=" << stats.success_rate.load()
            << ",avg_execution_time=" << stats.average_execution_time.load().count()
            << " " << timestamp;
        
        std::lock_guard<std::mutex> lock(impl_->pending_mutex);
        impl_->pending_logs.push(oss.str());
        impl_->pending_cv.notify_one();
        
        return true;
    } catch (const std::exception& e) {
        utils::Logger::error("Failed to log performance metrics: {}", e.what());
        return false;
    }
}

std::vector<TradeExecution> TradeLogger::query_trade_history(std::chrono::hours lookback) {
    // In a real implementation, this would query InfluxDB
    // For now, return empty vector as this is a complex query operation
    utils::Logger::warn("Trade history queries not implemented yet");
    return {};
}

double TradeLogger::calculate_total_profit(std::chrono::hours period) {
    // This would query InfluxDB for profit calculations
    utils::Logger::warn("Profit calculations from database not implemented yet");
    return 0.0;
}

double TradeLogger::calculate_success_rate(std::chrono::hours period) {
    // This would query InfluxDB for success rate calculations  
    utils::Logger::warn("Success rate calculations from database not implemented yet");
    return 0.0;
}

bool TradeLogger::flush_pending_logs() {
    std::lock_guard<std::mutex> lock(impl_->pending_mutex);
    
    if (impl_->pending_logs.empty()) {
        return true;
    }
    
    // Process all pending logs
    std::vector<std::string> batch;
    while (!impl_->pending_logs.empty()) {
        batch.push_back(impl_->pending_logs.front());
        impl_->pending_logs.pop();
    }
    
    // Write batch to InfluxDB
    for (const auto& log : batch) {
        if (!write_to_influxdb("trade_data", log)) {
            utils::Logger::error("Failed to write log to InfluxDB");
            return false;
        }
    }
    
    flush_file_buffers();
    return true;
}

size_t TradeLogger::get_pending_log_count() const {
    std::lock_guard<std::mutex> lock(impl_->pending_mutex);
    return impl_->pending_logs.size();
}

void TradeLogger::set_batch_size(size_t batch_size) {
    impl_->batch_size = batch_size;
}

void TradeLogger::set_flush_interval(std::chrono::seconds interval) {
    impl_->flush_interval = interval;
}

bool TradeLogger::is_healthy() const {
    return impl_->healthy.load();
}

std::string TradeLogger::get_status() const {
    std::ostringstream oss;
    oss << "TradeLogger Status:\n";
    oss << "  Database logging: " << (impl_->database_logging_enabled ? "enabled" : "disabled") << "\n";
    oss << "  File logging: " << (impl_->file_logging_enabled ? "enabled" : "disabled") << "\n";
    oss << "  Pending logs: " << get_pending_log_count() << "\n";
    oss << "  Total logs written: " << impl_->total_logs_written.load() << "\n";
    oss << "  Healthy: " << (impl_->healthy.load() ? "yes" : "no");
    return oss.str();
}

size_t TradeLogger::get_total_logs_written() const {
    return impl_->total_logs_written.load();
}

// Private method implementations
bool TradeLogger::write_to_influxdb(const std::string& measurement, const std::string& line_protocol) {
    if (impl_->influxdb_url.empty()) {
        return false;
    }
    
    CURL* curl = curl_easy_init();
    if (!curl) {
        utils::Logger::error("Failed to initialize CURL for InfluxDB");
        return false;
    }
    
    // Construct InfluxDB write URL
    std::string url = impl_->influxdb_url + "/write?db=" + impl_->database_name;
    
    curl_easy_setopt(curl, CURLOPT_URL, url.c_str());
    curl_easy_setopt(curl, CURLOPT_POSTFIELDS, line_protocol.c_str());
    curl_easy_setopt(curl, CURLOPT_POSTFIELDSIZE, line_protocol.length());
    curl_easy_setopt(curl, CURLOPT_TIMEOUT, 10L);
    
    struct curl_slist* headers = nullptr;
    headers = curl_slist_append(headers, "Content-Type: text/plain");
    curl_easy_setopt(curl, CURLOPT_HTTPHEADER, headers);
    
    CURLcode res = curl_easy_perform(curl);
    long response_code;
    curl_easy_getinfo(curl, CURLINFO_RESPONSE_CODE, &response_code);
    
    curl_slist_free_all(headers);
    curl_easy_cleanup(curl);
    
    if (res != CURLE_OK || response_code >= 400) {
        utils::Logger::error("InfluxDB write failed: {} (HTTP {})", curl_easy_strerror(res), response_code);
        return false;
    }
    
    impl_->total_logs_written++;
    return true;
}

std::string TradeLogger::trade_execution_to_line_protocol(const TradeExecution& execution) {
    std::ostringstream oss;
    auto timestamp = std::chrono::duration_cast<std::chrono::nanoseconds>(
        execution.timestamp.time_since_epoch()).count();
    
    oss << "trade_execution"
        << ",trade_id=" << escape_string_for_influx(execution.trade_id)
        << ",symbol=" << escape_string_for_influx(execution.symbol)
        << ",buy_exchange=" << escape_string_for_influx(execution.buy_exchange)
        << ",sell_exchange=" << escape_string_for_influx(execution.sell_exchange)
        << ",result=" << static_cast<int>(execution.result)
        << " buy_price=" << execution.buy_price
        << ",sell_price=" << execution.sell_price
        << ",quantity=" << execution.quantity
        << ",executed_quantity=" << execution.executed_quantity
        << ",expected_profit=" << execution.expected_profit
        << ",actual_profit=" << execution.actual_profit
        << ",total_fees=" << execution.total_fees
        << ",execution_latency=" << execution.execution_latency.count()
        << " " << timestamp;
    
    return oss.str();
}

std::string TradeLogger::order_execution_to_line_protocol(const OrderExecutionDetails& order) {
    std::ostringstream oss;
    auto timestamp = std::chrono::duration_cast<std::chrono::nanoseconds>(
        order.submitted_at.time_since_epoch()).count();
    
    oss << "order_execution"
        << ",order_id=" << escape_string_for_influx(order.order_id)
        << ",exchange_order_id=" << escape_string_for_influx(order.exchange_order_id)
        << ",exchange=" << escape_string_for_influx(order.original_order.exchange)
        << ",symbol=" << escape_string_for_influx(order.original_order.symbol)
        << ",side=" << static_cast<int>(order.original_order.side)
        << ",status=" << static_cast<int>(order.status)
        << " filled_quantity=" << order.filled_quantity
        << ",remaining_quantity=" << order.remaining_quantity
        << ",average_fill_price=" << order.average_fill_price
        << ",total_fees=" << order.total_fees
        << ",execution_latency=" << order.execution_latency.count()
        << " " << timestamp;
    
    return oss.str();
}

std::string TradeLogger::arbitrage_opportunity_to_line_protocol(const ArbitrageOpportunity& opportunity) {
    std::ostringstream oss;
    auto timestamp = std::chrono::duration_cast<std::chrono::nanoseconds>(
        opportunity.detected_at.time_since_epoch()).count();
    
    oss << "arbitrage_opportunity"
        << ",symbol=" << escape_string_for_influx(opportunity.symbol)
        << ",buy_exchange=" << escape_string_for_influx(opportunity.buy_exchange)
        << ",sell_exchange=" << escape_string_for_influx(opportunity.sell_exchange)
        << " buy_price=" << opportunity.buy_price
        << ",sell_price=" << opportunity.sell_price
        << ",available_quantity=" << opportunity.available_quantity
        << ",spread_percentage=" << opportunity.spread_percentage
        << ",expected_profit=" << opportunity.expected_profit
        << ",confidence_score=" << opportunity.confidence_score
        << ",estimated_slippage=" << opportunity.estimated_slippage
        << ",total_fees=" << opportunity.total_fees
        << " " << timestamp;
    
    return oss.str();
}

bool TradeLogger::write_to_file(const std::string& log_entry) {
    if (impl_->log_directory.empty()) {
        return false;
    }
    
    try {
        std::string filename = create_log_filename("trades");
        std::string filepath = impl_->log_directory + "/" + filename;
        
        std::unique_lock<std::shared_mutex> lock(impl_->mutex);
        
        auto& file_ptr = impl_->log_files[filename];
        if (!file_ptr) {
            file_ptr = std::make_unique<std::ofstream>(filepath, std::ios::app);
            if (!file_ptr->is_open()) {
                utils::Logger::error("Failed to open log file: {}", filepath);
                return false;
            }
        }
        
        *file_ptr << log_entry << std::endl;
        file_ptr->flush();
        
        return true;
    } catch (const std::exception& e) {
        utils::Logger::error("Failed to write to file: {}", e.what());
        return false;
    }
}

std::string TradeLogger::trade_execution_to_csv(const TradeExecution& execution) {
    std::ostringstream oss;
    
    oss << format_timestamp(execution.timestamp) << ","
        << execution.trade_id << ","
        << execution.symbol << ","
        << execution.buy_exchange << ","
        << execution.sell_exchange << ","
        << execution.buy_price << ","
        << execution.sell_price << ","
        << execution.quantity << ","
        << execution.executed_quantity << ","
        << execution.expected_profit << ","
        << execution.actual_profit << ","
        << execution.total_fees << ","
        << static_cast<int>(execution.result) << ","
        << execution.execution_latency.count();
    
    return oss.str();
}

std::string TradeLogger::create_log_filename(const std::string& prefix) {
    auto now = std::chrono::system_clock::now();
    auto time_t = std::chrono::system_clock::to_time_t(now);
    
    std::ostringstream oss;
    oss << prefix << "_" << std::put_time(std::gmtime(&time_t), "%Y%m%d") << ".csv";
    return oss.str();
}

void TradeLogger::process_pending_logs() {
    while (impl_->running) {
        std::unique_lock<std::mutex> lock(impl_->pending_mutex);
        
        // Wait for logs or shutdown
        impl_->pending_cv.wait_for(lock, impl_->flush_interval, [this]() {
            return !impl_->pending_logs.empty() || !impl_->running;
        });
        
        if (!impl_->running) break;
        
        // Process logs in batches
        std::vector<std::string> batch;
        size_t batch_count = 0;
        
        while (!impl_->pending_logs.empty() && batch_count < impl_->batch_size) {
            batch.push_back(impl_->pending_logs.front());
            impl_->pending_logs.pop();
            batch_count++;
        }
        
        lock.unlock();
        
        // Write batch to InfluxDB
        if (!batch.empty()) {
            for (const auto& log : batch) {
                if (!write_to_influxdb("trade_data", log)) {
                    impl_->healthy = false;
                    utils::Logger::error("Failed to write batch to InfluxDB");
                    break;
                }
            }
        }
    }
}

void TradeLogger::flush_file_buffers() {
    std::shared_lock<std::shared_mutex> lock(impl_->mutex);
    
    for (auto& [filename, file_ptr] : impl_->log_files) {
        if (file_ptr && file_ptr->is_open()) {
            file_ptr->flush();
        }
    }
}

std::string TradeLogger::format_timestamp(std::chrono::system_clock::time_point timestamp) {
    auto time_t = std::chrono::system_clock::to_time_t(timestamp);
    std::ostringstream oss;
    oss << std::put_time(std::gmtime(&time_t), "%Y-%m-%d %H:%M:%S");
    return oss.str();
}

std::string TradeLogger::escape_string_for_influx(const std::string& str) {
    std::string escaped = str;
    
    // Escape spaces, commas, and equals signs
    size_t pos = 0;
    while ((pos = escaped.find(' ', pos)) != std::string::npos) {
        escaped.replace(pos, 1, "\\ ");
        pos += 2;
    }
    
    pos = 0;
    while ((pos = escaped.find(',', pos)) != std::string::npos) {
        escaped.replace(pos, 1, "\\,");
        pos += 2;
    }
    
    pos = 0;
    while ((pos = escaped.find('=', pos)) != std::string::npos) {
        escaped.replace(pos, 1, "\\=");
        pos += 2;
    }
    
    return escaped;
}

std::string TradeLogger::format_currency_amount(double amount, const std::string& currency) {
    std::ostringstream oss;
    oss << std::fixed << std::setprecision(8) << amount << " " << currency;
    return oss.str();
}

} // namespace trading_engine
} // namespace ats