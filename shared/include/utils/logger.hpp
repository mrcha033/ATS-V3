#pragma once

#include <string>
#include <memory>
#include <chrono>

// Check if spdlog is available
#ifdef HAS_SPDLOG
#include <spdlog/spdlog.h>
#include <spdlog/sinks/stdout_color_sinks.h>
#include <spdlog/sinks/rotating_file_sink.h>
#include <spdlog/fmt/ostr.h>
#else
#include <iostream>
#include <fstream>
#include <mutex>
#include <sstream>
#include <iomanip>
#include <ctime>
#endif

namespace ats {
namespace utils {

enum class LogLevel {
    TRACE = 0,
    DEBUG = 1,
    INFO = 2,
    WARN = 3,
    ERROR = 4,
    CRITICAL = 5
};

class Logger {
public:
    static void initialize(const std::string& log_file_path = "logs/ats.log",
                          LogLevel level = LogLevel::INFO,
                          size_t max_file_size = 1024 * 1024 * 10,  // 10MB
                          size_t max_files = 3);
    
    static void shutdown();
    
    // Template logging functions
    template<typename... Args>
    static void trace(const std::string& fmt, Args&&... args) {
        if (logger_) {
            logger_->trace(fmt, std::forward<Args>(args)...);
        }
    }
    
    template<typename... Args>
    static void debug(const std::string& fmt, Args&&... args) {
        if (logger_) {
            logger_->debug(fmt, std::forward<Args>(args)...);
        }
    }
    
    template<typename... Args>
    static void info(const std::string& fmt, Args&&... args) {
        if (logger_) {
            logger_->info(fmt, std::forward<Args>(args)...);
        }
    }
    
    template<typename... Args>
    static void warn(const std::string& fmt, Args&&... args) {
        if (logger_) {
            logger_->warn(fmt, std::forward<Args>(args)...);
        }
    }
    
    template<typename... Args>
    static void error(const std::string& fmt, Args&&... args) {
        if (logger_) {
            logger_->error(fmt, std::forward<Args>(args)...);
        }
    }
    
    template<typename... Args>
    static void critical(const std::string& fmt, Args&&... args) {
        if (logger_) {
            logger_->critical(fmt, std::forward<Args>(args)...);
        }
    }
    
    // Convenience methods for single string logging
    static void trace(const std::string& msg);
    static void debug(const std::string& msg);
    static void info(const std::string& msg);
    static void warn(const std::string& msg);
    static void error(const std::string& msg);
    static void critical(const std::string& msg);
    
    // Set log level dynamically
    static void set_level(LogLevel level);
    
    // Get current log level
    static LogLevel get_level();
    
    // Check if a log level is enabled
    static bool is_enabled(LogLevel level);

private:
#ifdef HAS_SPDLOG
    static std::shared_ptr<spdlog::logger> logger_;
    static spdlog::level::level_enum to_spdlog_level(LogLevel level);
    static LogLevel from_spdlog_level(spdlog::level::level_enum level);
#else
    class FallbackLogger {
    public:
        template<typename... Args>
        void trace(const std::string& fmt, Args&&... args) { Logger::write_log(LogLevel::TRACE, fmt); }
        template<typename... Args>
        void debug(const std::string& fmt, Args&&... args) { Logger::write_log(LogLevel::DEBUG, fmt); }
        template<typename... Args>
        void info(const std::string& fmt, Args&&... args) { Logger::write_log(LogLevel::INFO, fmt); }
        template<typename... Args>
        void warn(const std::string& fmt, Args&&... args) { Logger::write_log(LogLevel::WARN, fmt); }
        template<typename... Args>
        void error(const std::string& fmt, Args&&... args) { Logger::write_log(LogLevel::ERROR, fmt); }
        template<typename... Args>
        void critical(const std::string& fmt, Args&&... args) { Logger::write_log(LogLevel::CRITICAL, fmt); }
    };
    
    static std::shared_ptr<FallbackLogger> logger_;
    static std::ofstream log_file_;
    static std::mutex log_mutex_;
    static void write_log(LogLevel level, const std::string& message);
    static std::string get_timestamp();
    static std::string level_to_string(LogLevel level);
#endif
    static LogLevel current_level_;
};

// Structured logging for trading events
class TradingLogger {
public:
    static void log_order_created(const std::string& exchange, const std::string& symbol,
                                 const std::string& order_id, const std::string& side,
                                 double quantity, double price);
    
    static void log_order_filled(const std::string& exchange, const std::string& symbol,
                                const std::string& order_id, double filled_quantity,
                                double avg_price);
    
    static void log_order_canceled(const std::string& exchange, const std::string& symbol,
                                  const std::string& order_id, const std::string& reason);
    
    static void log_arbitrage_opportunity(const std::string& symbol,
                                         const std::string& buy_exchange,
                                         const std::string& sell_exchange,
                                         double buy_price, double sell_price,
                                         double spread_percentage, double potential_profit);
    
    static void log_trade_executed(const std::string& trade_id, const std::string& symbol,
                                  double profit_loss, double total_fees);
    
    static void log_risk_alert(const std::string& alert_type, const std::string& description,
                              double current_value, double threshold);
    
    static void log_system_event(const std::string& event_type, const std::string& description);
    
    static void log_performance_metric(const std::string& metric_name, double value);
};

// RAII logging scope for performance measurement
class ScopedTimer {
public:
    explicit ScopedTimer(const std::string& operation_name);
    ~ScopedTimer();
    
private:
    std::string operation_name_;
    std::chrono::high_resolution_clock::time_point start_time_;
};

// Macros for convenient logging
#define ATS_LOG_TRACE(...) ats::utils::Logger::trace(__VA_ARGS__)
#define ATS_LOG_DEBUG(...) ats::utils::Logger::debug(__VA_ARGS__)
#define ATS_LOG_INFO(...) ats::utils::Logger::info(__VA_ARGS__)
#define ATS_LOG_WARN(...) ats::utils::Logger::warn(__VA_ARGS__)
#define ATS_LOG_ERROR(...) ats::utils::Logger::error(__VA_ARGS__)
#define ATS_LOG_CRITICAL(...) ats::utils::Logger::critical(__VA_ARGS__)

#define ATS_SCOPED_TIMER(name) ats::utils::ScopedTimer timer(name)

} // namespace utils
} // namespace ats