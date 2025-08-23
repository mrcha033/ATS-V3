#include "utils/logger.hpp"
#include <filesystem>
#include <iostream>
#include <chrono>

namespace ats {
namespace utils {

#ifdef HAS_SPDLOG
std::shared_ptr<spdlog::logger> Logger::logger_ = nullptr;
#else
std::shared_ptr<Logger::FallbackLogger> Logger::logger_ = nullptr;
std::ofstream Logger::log_file_;
std::mutex Logger::log_mutex_;
#endif

LogLevel Logger::current_level_ = LogLevel::INFO;

void Logger::initialize(const std::string& log_file_path, LogLevel level,
                       size_t max_file_size, size_t max_files) {
    try {
#ifdef HAS_SPDLOG
        // Create logs directory if it doesn't exist
        std::filesystem::path log_path(log_file_path);
        std::filesystem::create_directories(log_path.parent_path());
        
        // Create console sink
        auto console_sink = std::make_shared<spdlog::sinks::stdout_color_sink_mt>();
        console_sink->set_level(to_spdlog_level(level));
        console_sink->set_pattern("[%Y-%m-%d %H:%M:%S.%e] [%^%l%$] [%t] %v");
        
        // Create rotating file sink
        auto file_sink = std::make_shared<spdlog::sinks::rotating_file_sink_mt>(
            log_file_path, max_file_size, max_files);
        file_sink->set_level(to_spdlog_level(level));
        file_sink->set_pattern("[%Y-%m-%d %H:%M:%S.%e] [%l] [%t] %v");
        
        // Create multi-sink logger
        std::vector<spdlog::sink_ptr> sinks{console_sink, file_sink};
        logger_ = std::make_shared<spdlog::logger>("ats", sinks.begin(), sinks.end());
        
        // Set log level
        logger_->set_level(to_spdlog_level(level));
        current_level_ = level;
        
        // Register as default logger
        spdlog::register_logger(logger_);
        spdlog::set_default_logger(logger_);
        
        // Set flush policy
        logger_->flush_on(spdlog::level::warn);
        spdlog::flush_every(std::chrono::seconds(3));
        
        Logger::info("Logger initialized successfully with spdlog");
#else
        // Fallback implementation without spdlog
        // Create logs directory if it doesn't exist
        try {
            std::filesystem::path log_path(log_file_path);
            std::filesystem::create_directories(log_path.parent_path());
        } catch (...) {
            // Ignore filesystem errors for fallback
        }
        
        // Open log file
        std::lock_guard<std::mutex> lock(log_mutex_);
        log_file_.open(log_file_path, std::ios::out | std::ios::app);
        current_level_ = level;
        
        // Initialize fallback logger
        logger_ = std::make_shared<FallbackLogger>();
        
        write_log(LogLevel::INFO, "Logger initialized successfully (fallback mode)");
#endif
        
    } catch (const std::exception& e) {
        std::cerr << "Logger initialization failed: " << e.what() << std::endl;
        throw;
    }
}

void Logger::shutdown() {
#ifdef HAS_SPDLOG
    if (logger_) {
        logger_->flush();
        spdlog::shutdown();
        logger_ = nullptr;
    }
#else
    std::lock_guard<std::mutex> lock(log_mutex_);
    if (log_file_.is_open()) {
        write_log(LogLevel::INFO, "Logger shutting down");
        log_file_.close();
    }
    logger_ = nullptr;
#endif
}

void Logger::trace(const std::string& msg) {
#ifdef HAS_SPDLOG
    if (logger_) logger_->trace(msg);
#else
    if (is_enabled(LogLevel::TRACE)) {
        write_log(LogLevel::TRACE, msg);
    }
#endif
}

void Logger::debug(const std::string& msg) {
#ifdef HAS_SPDLOG
    if (logger_) logger_->debug(msg);
#else
    if (is_enabled(LogLevel::DEBUG)) {
        write_log(LogLevel::DEBUG, msg);
    }
#endif
}

void Logger::info(const std::string& msg) {
#ifdef HAS_SPDLOG
    if (logger_) logger_->info(msg);
#else
    if (is_enabled(LogLevel::INFO)) {
        write_log(LogLevel::INFO, msg);
    }
#endif
}

void Logger::warn(const std::string& msg) {
#ifdef HAS_SPDLOG
    if (logger_) logger_->warn(msg);
#else
    if (is_enabled(LogLevel::WARN)) {
        write_log(LogLevel::WARN, msg);
    }
#endif
}

void Logger::error(const std::string& msg) {
#ifdef HAS_SPDLOG
    if (logger_) logger_->error(msg);
#else
    if (is_enabled(LogLevel::ERROR)) {
        write_log(LogLevel::ERROR, msg);
    }
#endif
}

void Logger::critical(const std::string& msg) {
#ifdef HAS_SPDLOG
    if (logger_) logger_->critical(msg);
#else
    if (is_enabled(LogLevel::CRITICAL)) {
        write_log(LogLevel::CRITICAL, msg);
    }
#endif
}

void Logger::set_level(LogLevel level) {
    current_level_ = level;
#ifdef HAS_SPDLOG
    if (logger_) {
        logger_->set_level(to_spdlog_level(level));
    }
#endif
}

LogLevel Logger::get_level() {
    return current_level_;
}

bool Logger::is_enabled(LogLevel level) {
    return static_cast<int>(level) >= static_cast<int>(current_level_);
}

#ifdef HAS_SPDLOG
spdlog::level::level_enum Logger::to_spdlog_level(LogLevel level) {
    switch (level) {
        case LogLevel::TRACE: return spdlog::level::trace;
        case LogLevel::DEBUG: return spdlog::level::debug;
        case LogLevel::INFO: return spdlog::level::info;
        case LogLevel::WARN: return spdlog::level::warn;
        case LogLevel::ERROR: return spdlog::level::err;
        case LogLevel::CRITICAL: return spdlog::level::critical;
        default: return spdlog::level::info;
    }
}

LogLevel Logger::from_spdlog_level(spdlog::level::level_enum level) {
    switch (level) {
        case spdlog::level::trace: return LogLevel::TRACE;
        case spdlog::level::debug: return LogLevel::DEBUG;
        case spdlog::level::info: return LogLevel::INFO;
        case spdlog::level::warn: return LogLevel::WARN;
        case spdlog::level::err: return LogLevel::ERROR;
        case spdlog::level::critical: return LogLevel::CRITICAL;
        default: return LogLevel::INFO;
    }
}
#else
void Logger::write_log(LogLevel level, const std::string& message) {
    std::string timestamp = get_timestamp();
    std::string level_str = level_to_string(level);
    
    // Write to console
    std::cout << "[" << timestamp << "] [" << level_str << "] " << message << std::endl;
    
    // Write to file if open
    if (log_file_.is_open()) {
        log_file_ << "[" << timestamp << "] [" << level_str << "] " << message << std::endl;
        log_file_.flush();
    }
}

std::string Logger::get_timestamp() {
    auto now = std::chrono::system_clock::now();
    auto time_t = std::chrono::system_clock::to_time_t(now);
    auto ms = std::chrono::duration_cast<std::chrono::milliseconds>(
        now.time_since_epoch()) % 1000;
    
    std::stringstream ss;
    ss << std::put_time(std::localtime(&time_t), "%Y-%m-%d %H:%M:%S");
    ss << '.' << std::setfill('0') << std::setw(3) << ms.count();
    return ss.str();
}

std::string Logger::level_to_string(LogLevel level) {
    switch (level) {
        case LogLevel::TRACE: return "TRACE";
        case LogLevel::DEBUG: return "DEBUG";
        case LogLevel::INFO: return "INFO";
        case LogLevel::WARN: return "WARN";
        case LogLevel::ERROR: return "ERROR";
        case LogLevel::CRITICAL: return "CRITICAL";
        default: return "INFO";
    }
}
#endif

// TradingLogger implementation
void TradingLogger::log_order_created(const std::string& exchange, const std::string& symbol,
                                     const std::string& order_id, const std::string& side,
                                     double quantity, double price) {
    std::stringstream ss;
    ss << "ORDER_CREATED | Exchange: " << exchange << " | Symbol: " << symbol 
       << " | OrderID: " << order_id << " | Side: " << side 
       << " | Qty: " << quantity << " | Price: " << price;
    Logger::info(ss.str());
}

void TradingLogger::log_order_filled(const std::string& exchange, const std::string& symbol,
                                    const std::string& order_id, double filled_quantity,
                                    double avg_price) {
    std::stringstream ss;
    ss << "ORDER_FILLED | Exchange: " << exchange << " | Symbol: " << symbol 
       << " | OrderID: " << order_id << " | FilledQty: " << filled_quantity 
       << " | AvgPrice: " << avg_price;
    Logger::info(ss.str());
}

void TradingLogger::log_order_canceled(const std::string& exchange, const std::string& symbol,
                                      const std::string& order_id, const std::string& reason) {
    std::stringstream ss;
    ss << "ORDER_CANCELED | Exchange: " << exchange << " | Symbol: " << symbol 
       << " | OrderID: " << order_id << " | Reason: " << reason;
    Logger::warn(ss.str());
}

void TradingLogger::log_arbitrage_opportunity(const std::string& symbol,
                                             const std::string& buy_exchange,
                                             const std::string& sell_exchange,
                                             double buy_price, double sell_price,
                                             double spread_percentage, double potential_profit) {
    std::stringstream ss;
    ss << "ARBITRAGE_OPPORTUNITY | Symbol: " << symbol 
       << " | Buy: " << buy_exchange << "@" << buy_price 
       << " | Sell: " << sell_exchange << "@" << sell_price 
       << " | Spread: " << std::fixed << std::setprecision(2) << spread_percentage 
       << "% | Profit: " << potential_profit;
    Logger::info(ss.str());
}

void TradingLogger::log_trade_executed(const std::string& trade_id, const std::string& symbol,
                                      double profit_loss, double total_fees) {
    std::stringstream ss;
    ss << "TRADE_EXECUTED | TradeID: " << trade_id << " | Symbol: " << symbol 
       << " | PnL: " << profit_loss << " | Fees: " << total_fees;
    Logger::info(ss.str());
}

void TradingLogger::log_risk_alert(const std::string& alert_type, const std::string& description,
                                  double current_value, double threshold) {
    std::stringstream ss;
    ss << "RISK_ALERT | Type: " << alert_type << " | Description: " << description 
       << " | Current: " << current_value << " | Threshold: " << threshold;
    Logger::warn(ss.str());
}

void TradingLogger::log_system_event(const std::string& event_type, const std::string& description) {
    std::stringstream ss;
    ss << "SYSTEM_EVENT | Type: " << event_type << " | Description: " << description;
    Logger::info(ss.str());
}

void TradingLogger::log_performance_metric(const std::string& metric_name, double value) {
    std::stringstream ss;
    ss << "PERFORMANCE_METRIC | Metric: " << metric_name << " | Value: " << value;
    Logger::debug(ss.str());
}

// ScopedTimer implementation
ScopedTimer::ScopedTimer(const std::string& operation_name)
    : operation_name_(operation_name), start_time_(std::chrono::high_resolution_clock::now()) {
}

ScopedTimer::~ScopedTimer() {
    auto end_time = std::chrono::high_resolution_clock::now();
    auto duration = std::chrono::duration_cast<std::chrono::microseconds>(end_time - start_time_);
    std::stringstream ss;
    ss << "TIMER | Operation: " << operation_name_ << " | Duration: " << duration.count() << " μs";
    Logger::debug(ss.str());
}

} // namespace utils
} // namespace ats