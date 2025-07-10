#include "structured_logger.hpp"
#include <fstream>
#include <iostream>
#include <thread>
#include <mutex>
#include <cstring>

// Conditional filesystem include
#if __cplusplus >= 201703L && __has_include(<filesystem>)
    #include <filesystem>
    namespace fs = std::filesystem;
    #define HAS_FILESYSTEM
#elif __has_include(<experimental/filesystem>)
    #include <experimental/filesystem>
    namespace fs = std::experimental::filesystem;
    #define HAS_FILESYSTEM
#endif

namespace ats {

std::unique_ptr<std::ofstream> StructuredLogger::log_file_;
LogLevel StructuredLogger::min_level_ = LogLevel::INFO;
std::string StructuredLogger::current_component_ = "ATS-V3";
std::unordered_map<std::string, std::string> StructuredLogger::global_context_;
std::mutex StructuredLogger::log_mutex_;

void StructuredLogger::init(const std::string& log_file_path, LogLevel min_level) {
    std::lock_guard<std::mutex> lock(log_mutex_);
    
    min_level_ = min_level;
    
    // Create log directory if it doesn't exist (if filesystem is available)
#ifdef HAS_FILESYSTEM
    fs::path log_path(log_file_path);
    if (log_path.has_parent_path()) {
        std::error_code ec;
        fs::create_directories(log_path.parent_path(), ec);
        if (ec) {
            std::cerr << "Warning: Could not create log directory: " << ec.message() << std::endl;
        }
    }
#endif
    
    log_file_ = std::make_unique<std::ofstream>(log_file_path, std::ios::app);
    if (!log_file_->is_open()) {
        std::cerr << "Failed to open log file: " << log_file_path << std::endl;
    }
    
    // Log initialization
    info("StructuredLogger initialized", {
        {"log_file", log_file_path},
        {"min_level", level_to_string(min_level)}
    });
}

void StructuredLogger::set_component(const std::string& component) {
    std::lock_guard<std::mutex> lock(log_mutex_);
    current_component_ = component;
}

void StructuredLogger::set_global_context(const std::string& key, const std::string& value) {
    std::lock_guard<std::mutex> lock(log_mutex_);
    global_context_[key] = value;
}

void StructuredLogger::remove_global_context(const std::string& key) {
    std::lock_guard<std::mutex> lock(log_mutex_);
    global_context_.erase(key);
}

void StructuredLogger::debug(const std::string& message, const std::unordered_map<std::string, std::string>& context) {
    log(LogLevel::DEBUG, message, context);
}

void StructuredLogger::info(const std::string& message, const std::unordered_map<std::string, std::string>& context) {
    log(LogLevel::INFO, message, context);
}

void StructuredLogger::warning(const std::string& message, const std::unordered_map<std::string, std::string>& context) {
    log(LogLevel::WARNING, message, context);
}

void StructuredLogger::error(const std::string& message, const std::unordered_map<std::string, std::string>& context) {
    log(LogLevel::ERROR, message, context);
}

void StructuredLogger::critical(const std::string& message, const std::unordered_map<std::string, std::string>& context) {
    log(LogLevel::CRITICAL, message, context);
}

void StructuredLogger::trade_executed(const std::string& symbol, const std::string& side, 
                                     double price, double quantity, const std::string& order_id) {
    info("Trade executed", {
        {"event_type", "trade_executed"},
        {"symbol", symbol},
        {"side", side},
        {"price", std::to_string(price)},
        {"quantity", std::to_string(quantity)},
        {"order_id", order_id}
    });
}

void StructuredLogger::opportunity_detected(const std::string& symbol, double profit_percent, 
                                          const std::string& buy_exchange, const std::string& sell_exchange) {
    info("Arbitrage opportunity detected", {
        {"event_type", "opportunity_detected"},
        {"symbol", symbol},
        {"profit_percent", std::to_string(profit_percent)},
        {"buy_exchange", buy_exchange},
        {"sell_exchange", sell_exchange}
    });
}

void StructuredLogger::risk_violation(const std::string& rule, const std::string& details) {
    warning("Risk management violation", {
        {"event_type", "risk_violation"},
        {"rule", rule},
        {"details", details}
    });
}

void StructuredLogger::performance_metric(const std::string& metric, double value, const std::string& unit) {
    debug("Performance metric", {
        {"event_type", "performance_metric"},
        {"metric", metric},
        {"value", std::to_string(value)},
        {"unit", unit}
    });
}

void StructuredLogger::flush() {
    std::lock_guard<std::mutex> lock(log_mutex_);
    if (log_file_ && log_file_->is_open()) {
        log_file_->flush();
    }
}

void StructuredLogger::log(LogLevel level, const std::string& message, 
                          const std::unordered_map<std::string, std::string>& context) {
    if (level < min_level_) {
        return;
    }
    
    std::lock_guard<std::mutex> lock(log_mutex_);
    
    LogEntry entry;
    entry.timestamp = std::chrono::system_clock::now();
    entry.level = level;
    entry.message = message;
    entry.component = current_component_;
    entry.thread_id = get_thread_id();
    entry.context = global_context_;
    
    // Merge local context with global context
    for (const auto& [key, value] : context) {
        entry.context[key] = value;
    }
    
    // Output to console
    std::cout << entry.to_string() << std::endl;
    
    // Output to file
    if (log_file_ && log_file_->is_open()) {
        *log_file_ << entry.to_json().dump() << std::endl;
    }
}

std::string StructuredLogger::level_to_string(LogLevel level) {
    switch (level) {
        case LogLevel::DEBUG: return "DEBUG";
        case LogLevel::INFO: return "INFO";
        case LogLevel::WARNING: return "WARNING";
        case LogLevel::ERROR: return "ERROR";
        case LogLevel::CRITICAL: return "CRITICAL";
        default: return "UNKNOWN";
    }
}

std::string StructuredLogger::get_thread_id() {
    std::ostringstream oss;
    oss << std::this_thread::get_id();
    return oss.str();
}

nlohmann::json StructuredLogger::LogEntry::to_json() const {
    nlohmann::json j;
    
    auto time_t = std::chrono::system_clock::to_time_t(timestamp);
    auto ms = std::chrono::duration_cast<std::chrono::milliseconds>(
        timestamp.time_since_epoch()
    ).count() % 1000;
    
    std::ostringstream oss;
    oss << std::put_time(std::gmtime(&time_t), "%Y-%m-%dT%H:%M:%S");
    oss << '.' << std::setfill('0') << std::setw(3) << ms << 'Z';
    
    j["timestamp"] = oss.str();
    j["level"] = StructuredLogger::level_to_string(level);
    j["message"] = message;
    j["component"] = component;
    j["thread_id"] = thread_id;
    j["context"] = context;
    
    return j;
}

std::string StructuredLogger::LogEntry::to_string() const {
    auto time_t = std::chrono::system_clock::to_time_t(timestamp);
    auto ms = std::chrono::duration_cast<std::chrono::milliseconds>(
        timestamp.time_since_epoch()
    ).count() % 1000;
    
    std::ostringstream oss;
    oss << std::put_time(std::localtime(&time_t), "%Y-%m-%d %H:%M:%S");
    oss << '.' << std::setfill('0') << std::setw(3) << ms;
    oss << " [" << StructuredLogger::level_to_string(level) << "]";
    oss << " [" << component << "]";
    oss << " " << message;
    
    if (!context.empty()) {
        oss << " {";
        bool first = true;
        for (const auto& [key, value] : context) {
            if (!first) oss << ", ";
            oss << key << "=" << value;
            first = false;
        }
        oss << "}";
    }
    
    return oss.str();
}

} // namespace ats