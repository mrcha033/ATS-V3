#pragma once

#include <string>
#include <unordered_map>
#include <chrono>
#include <memory>
#include <sstream>
#include <iomanip>
#include <mutex>
#include <fstream>
#include <nlohmann/json.hpp>

namespace ats {

enum class LogLevel {
    DEBUG = 0,
    INFO = 1,
    WARNING = 2,
    ERROR = 3,
    CRITICAL = 4
};

class StructuredLogger {
public:
    struct LogEntry {
        std::chrono::system_clock::time_point timestamp;
        LogLevel level;
        std::string message;
        std::string component;
        std::string thread_id;
        std::unordered_map<std::string, std::string> context;
        
        nlohmann::json to_json() const;
        std::string to_string() const;
    };

    static void init(const std::string& log_file_path, LogLevel min_level = LogLevel::INFO);
    static void set_component(const std::string& component);
    static void set_global_context(const std::string& key, const std::string& value);
    static void remove_global_context(const std::string& key);
    
    // Structured logging methods
    static void debug(const std::string& message, const std::unordered_map<std::string, std::string>& context = {});
    static void info(const std::string& message, const std::unordered_map<std::string, std::string>& context = {});
    static void warning(const std::string& message, const std::unordered_map<std::string, std::string>& context = {});
    static void error(const std::string& message, const std::unordered_map<std::string, std::string>& context = {});
    static void critical(const std::string& message, const std::unordered_map<std::string, std::string>& context = {});
    
    // Trading-specific logging
    static void trade_executed(const std::string& symbol, const std::string& side, 
                              double price, double quantity, const std::string& order_id);
    static void opportunity_detected(const std::string& symbol, double profit_percent, 
                                   const std::string& buy_exchange, const std::string& sell_exchange);
    static void risk_violation(const std::string& rule, const std::string& details);
    static void performance_metric(const std::string& metric, double value, const std::string& unit = "");
    
    // Flush logs
    static void flush();
    
private:
    static void log(LogLevel level, const std::string& message, 
                   const std::unordered_map<std::string, std::string>& context);
    static std::string level_to_string(LogLevel level);
    static std::string get_thread_id();
    
    static std::unique_ptr<std::ofstream> log_file_;
    static LogLevel min_level_;
    static std::string current_component_;
    static std::unordered_map<std::string, std::string> global_context_;
    static std::mutex log_mutex_;
};

// Convenient macros for structured logging
#define SLOG_DEBUG(msg, ...) ats::StructuredLogger::debug(msg, ##__VA_ARGS__)
#define SLOG_INFO(msg, ...) ats::StructuredLogger::info(msg, ##__VA_ARGS__)
#define SLOG_WARNING(msg, ...) ats::StructuredLogger::warning(msg, ##__VA_ARGS__)
#define SLOG_ERROR(msg, ...) ats::StructuredLogger::error(msg, ##__VA_ARGS__)
#define SLOG_CRITICAL(msg, ...) ats::StructuredLogger::critical(msg, ##__VA_ARGS__)

// Trading-specific macros
#define SLOG_TRADE(symbol, side, price, qty, order_id) \
    ats::StructuredLogger::trade_executed(symbol, side, price, qty, order_id)

#define SLOG_OPPORTUNITY(symbol, profit, buy_ex, sell_ex) \
    ats::StructuredLogger::opportunity_detected(symbol, profit, buy_ex, sell_ex)

#define SLOG_RISK_VIOLATION(rule, details) \
    ats::StructuredLogger::risk_violation(rule, details)

#define SLOG_PERFORMANCE(metric, value, unit) \
    ats::StructuredLogger::performance_metric(metric, value, unit)

} // namespace ats