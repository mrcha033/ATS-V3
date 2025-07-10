#include "utils/logger.hpp"
#include <filesystem>
#include <iostream>
#include <chrono>

namespace ats {
namespace utils {

std::shared_ptr<spdlog::logger> Logger::logger_ = nullptr;
LogLevel Logger::current_level_ = LogLevel::INFO;

void Logger::initialize(const std::string& log_file_path, LogLevel level,
                       size_t max_file_size, size_t max_files) {
    try {
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
        
        Logger::info("Logger initialized successfully");
        
    } catch (const std::exception& e) {
        std::cerr << "Logger initialization failed: " << e.what() << std::endl;
        throw;
    }
}

void Logger::shutdown() {
    if (logger_) {
        logger_->flush();
        spdlog::shutdown();
        logger_ = nullptr;
    }
}

void Logger::trace(const std::string& msg) {
    if (logger_) logger_->trace(msg);
}

void Logger::debug(const std::string& msg) {
    if (logger_) logger_->debug(msg);
}

void Logger::info(const std::string& msg) {
    if (logger_) logger_->info(msg);
}

void Logger::warn(const std::string& msg) {
    if (logger_) logger_->warn(msg);
}

void Logger::error(const std::string& msg) {
    if (logger_) logger_->error(msg);
}

void Logger::critical(const std::string& msg) {
    if (logger_) logger_->critical(msg);
}

void Logger::set_level(LogLevel level) {
    current_level_ = level;
    if (logger_) {
        logger_->set_level(to_spdlog_level(level));
    }
}

LogLevel Logger::get_level() {
    return current_level_;
}

bool Logger::is_enabled(LogLevel level) {
    return static_cast<int>(level) >= static_cast<int>(current_level_);
}

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

// TradingLogger implementation
void TradingLogger::log_order_created(const std::string& exchange, const std::string& symbol,
                                     const std::string& order_id, const std::string& side,
                                     double quantity, double price) {
    Logger::info("ORDER_CREATED | Exchange: {} | Symbol: {} | OrderID: {} | Side: {} | Qty: {} | Price: {}",
                exchange, symbol, order_id, side, quantity, price);
}

void TradingLogger::log_order_filled(const std::string& exchange, const std::string& symbol,
                                    const std::string& order_id, double filled_quantity,
                                    double avg_price) {
    Logger::info("ORDER_FILLED | Exchange: {} | Symbol: {} | OrderID: {} | FilledQty: {} | AvgPrice: {}",
                exchange, symbol, order_id, filled_quantity, avg_price);
}

void TradingLogger::log_order_canceled(const std::string& exchange, const std::string& symbol,
                                      const std::string& order_id, const std::string& reason) {
    Logger::warn("ORDER_CANCELED | Exchange: {} | Symbol: {} | OrderID: {} | Reason: {}",
                exchange, symbol, order_id, reason);
}

void TradingLogger::log_arbitrage_opportunity(const std::string& symbol,
                                             const std::string& buy_exchange,
                                             const std::string& sell_exchange,
                                             double buy_price, double sell_price,
                                             double spread_percentage, double potential_profit) {
    Logger::info("ARBITRAGE_OPPORTUNITY | Symbol: {} | Buy: {}@{} | Sell: {}@{} | Spread: {:.2f}% | Profit: {}",
                symbol, buy_exchange, buy_price, sell_exchange, sell_price, spread_percentage, potential_profit);
}

void TradingLogger::log_trade_executed(const std::string& trade_id, const std::string& symbol,
                                      double profit_loss, double total_fees) {
    Logger::info("TRADE_EXECUTED | TradeID: {} | Symbol: {} | PnL: {} | Fees: {}",
                trade_id, symbol, profit_loss, total_fees);
}

void TradingLogger::log_risk_alert(const std::string& alert_type, const std::string& description,
                                  double current_value, double threshold) {
    Logger::warn("RISK_ALERT | Type: {} | Description: {} | Current: {} | Threshold: {}",
                alert_type, description, current_value, threshold);
}

void TradingLogger::log_system_event(const std::string& event_type, const std::string& description) {
    Logger::info("SYSTEM_EVENT | Type: {} | Description: {}", event_type, description);
}

void TradingLogger::log_performance_metric(const std::string& metric_name, double value) {
    Logger::debug("PERFORMANCE_METRIC | Metric: {} | Value: {}", metric_name, value);
}

// ScopedTimer implementation
ScopedTimer::ScopedTimer(const std::string& operation_name)
    : operation_name_(operation_name), start_time_(std::chrono::high_resolution_clock::now()) {
}

ScopedTimer::~ScopedTimer() {
    auto end_time = std::chrono::high_resolution_clock::now();
    auto duration = std::chrono::duration_cast<std::chrono::microseconds>(end_time - start_time_);
    Logger::debug("TIMER | Operation: {} | Duration: {} μs", operation_name_, duration.count());
}

} // namespace utils
} // namespace ats