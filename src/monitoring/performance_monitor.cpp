#include "performance_monitor.hpp"
#include <cmath>
#include <algorithm>
#include <nlohmann/json.hpp>

namespace ats {

PerformanceMonitor& PerformanceMonitor::instance() {
    static PerformanceMonitor instance;
    return instance;
}

void PerformanceMonitor::record_order_placed(const std::string& exchange) {
    trading_metrics_.orders_placed.increment();
    SLOG_PERFORMANCE("orders_placed", trading_metrics_.orders_placed.get(), "count");
}

void PerformanceMonitor::record_order_filled(const std::string& exchange, double latency_ms) {
    trading_metrics_.orders_filled.increment();
    trading_metrics_.order_latency_ms.record_value(latency_ms);
    
    {
        std::lock_guard<std::mutex> lock(metrics_mutex_);
        exchange_latency_[exchange].record_value(latency_ms);
    }
    
    SLOG_PERFORMANCE("order_filled", 1, "count");
    SLOG_PERFORMANCE("order_latency", latency_ms, "ms");
    
    if (latency_ms > LATENCY_WARNING_THRESHOLD) {
        SLOG_WARNING("High order latency detected", {
            {"exchange", exchange},
            {"latency_ms", std::to_string(latency_ms)},
            {"threshold_ms", std::to_string(LATENCY_WARNING_THRESHOLD)}
        });
    }
}

void PerformanceMonitor::record_order_cancelled(const std::string& exchange) {
    trading_metrics_.orders_cancelled.increment();
    SLOG_PERFORMANCE("orders_cancelled", trading_metrics_.orders_cancelled.get(), "count");
}

void PerformanceMonitor::record_arbitrage_opportunity(const std::string& symbol, double profit_percent) {
    trading_metrics_.arbitrage_opportunities.increment();
    SLOG_OPPORTUNITY(symbol, profit_percent, "", "");
    SLOG_PERFORMANCE("arbitrage_opportunities", trading_metrics_.arbitrage_opportunities.get(), "count");
}

void PerformanceMonitor::record_successful_trade(double profit, double slippage_percent) {
    trading_metrics_.successful_trades.increment();
    trading_metrics_.profit_per_trade.record_value(profit);
    trading_metrics_.slippage_percent.record_value(slippage_percent);
    
    SLOG_PERFORMANCE("successful_trades", trading_metrics_.successful_trades.get(), "count");
    SLOG_PERFORMANCE("trade_profit", profit, "USD");
    SLOG_PERFORMANCE("trade_slippage", slippage_percent, "percent");
}

void PerformanceMonitor::record_failed_trade(const std::string& reason) {
    trading_metrics_.failed_trades.increment();
    SLOG_ERROR("Trade failed", {{"reason", reason}});
    SLOG_PERFORMANCE("failed_trades", trading_metrics_.failed_trades.get(), "count");
}

void PerformanceMonitor::update_pnl(double daily_pnl, double total_pnl) {
    trading_metrics_.daily_pnl.store(daily_pnl);
    trading_metrics_.total_pnl.store(total_pnl);
    
    SLOG_PERFORMANCE("daily_pnl", daily_pnl, "USD");
    SLOG_PERFORMANCE("total_pnl", total_pnl, "USD");
}

void PerformanceMonitor::update_active_positions(size_t count) {
    trading_metrics_.active_positions.store(count);
    SLOG_PERFORMANCE("active_positions", count, "count");
}

void PerformanceMonitor::record_cpu_usage(double percent) {
    system_metrics_.cpu_usage_percent.record_value(percent);
    SLOG_PERFORMANCE("cpu_usage", percent, "percent");
    
    if (percent > CPU_WARNING_THRESHOLD) {
        SLOG_WARNING("High CPU usage detected", {
            {"cpu_percent", std::to_string(percent)},
            {"threshold", std::to_string(CPU_WARNING_THRESHOLD)}
        });
    }
}

void PerformanceMonitor::record_memory_usage(double mb) {
    system_metrics_.memory_usage_mb.record_value(mb);
    SLOG_PERFORMANCE("memory_usage", mb, "MB");
    
    if (mb > MEMORY_WARNING_THRESHOLD) {
        SLOG_WARNING("High memory usage detected", {
            {"memory_mb", std::to_string(mb)},
            {"threshold", std::to_string(MEMORY_WARNING_THRESHOLD)}
        });
    }
}

void PerformanceMonitor::record_network_latency(const std::string& endpoint, double ms) {
    system_metrics_.network_latency_ms.record_value(ms);
    SLOG_PERFORMANCE("network_latency_" + endpoint, ms, "ms");
    
    if (ms > LATENCY_WARNING_THRESHOLD) {
        SLOG_WARNING("High network latency detected", {
            {"endpoint", endpoint},
            {"latency_ms", std::to_string(ms)},
            {"threshold_ms", std::to_string(LATENCY_WARNING_THRESHOLD)}
        });
    }
}

void PerformanceMonitor::record_websocket_reconnection(const std::string& exchange) {
    system_metrics_.websocket_reconnections.increment();
    SLOG_WARNING("WebSocket reconnection", {{"exchange", exchange}});
    SLOG_PERFORMANCE("websocket_reconnections", system_metrics_.websocket_reconnections.get(), "count");
}

void PerformanceMonitor::record_api_error(const std::string& exchange, const std::string& error) {
    system_metrics_.api_errors.increment();
    SLOG_ERROR("API error", {{"exchange", exchange}, {"error", error}});
    SLOG_PERFORMANCE("api_errors", system_metrics_.api_errors.get(), "count");
}

void PerformanceMonitor::record_risk_violation(const std::string& rule) {
    system_metrics_.risk_violations.increment();
    SLOG_RISK_VIOLATION(rule, "Risk management rule violated");
    SLOG_PERFORMANCE("risk_violations", system_metrics_.risk_violations.get(), "count");
}

void PerformanceMonitor::update_heartbeat() {
    system_metrics_.last_heartbeat.store(std::chrono::steady_clock::now());
}

TradingMetrics PerformanceMonitor::get_trading_metrics() const {
    std::lock_guard<std::mutex> lock(metrics_mutex_);
    return trading_metrics_;
}

SystemMetrics PerformanceMonitor::get_system_metrics() const {
    std::lock_guard<std::mutex> lock(metrics_mutex_);
    return system_metrics_;
}

double PerformanceMonitor::get_success_rate() const {
    auto successful = trading_metrics_.successful_trades.get();
    auto failed = trading_metrics_.failed_trades.get();
    auto total = successful + failed;
    
    return total > 0 ? static_cast<double>(successful) / total : 0.0;
}

double PerformanceMonitor::get_average_profit_per_trade() const {
    auto stats = trading_metrics_.profit_per_trade.get_statistics();
    return stats.mean;
}

double PerformanceMonitor::get_sharpe_ratio() const {
    auto stats = trading_metrics_.profit_per_trade.get_statistics();
    
    if (stats.std_dev == 0.0 || stats.count < 2) {
        return 0.0;
    }
    
    // Simplified Sharpe ratio (assuming risk-free rate = 0)
    return stats.mean / stats.std_dev * std::sqrt(stats.count);
}

bool PerformanceMonitor::is_system_healthy() const {
    auto cpu_stats = system_metrics_.cpu_usage_percent.get_statistics();
    auto memory_stats = system_metrics_.memory_usage_mb.get_statistics();
    auto latency_stats = system_metrics_.network_latency_ms.get_statistics();
    
    auto now = std::chrono::steady_clock::now();
    auto last_heartbeat = system_metrics_.last_heartbeat.load();
    auto heartbeat_age = now - last_heartbeat;
    
    bool cpu_healthy = cpu_stats.count == 0 || cpu_stats.mean < CPU_WARNING_THRESHOLD;
    bool memory_healthy = memory_stats.count == 0 || memory_stats.mean < MEMORY_WARNING_THRESHOLD;
    bool latency_healthy = latency_stats.count == 0 || latency_stats.mean < LATENCY_WARNING_THRESHOLD;
    bool heartbeat_healthy = heartbeat_age < HEARTBEAT_WARNING_THRESHOLD;
    
    return cpu_healthy && memory_healthy && latency_healthy && heartbeat_healthy;
}

void PerformanceMonitor::log_performance_summary() const {
    auto trading = get_trading_metrics();
    auto system = get_system_metrics();
    
    auto success_rate = get_success_rate();
    auto avg_profit = get_average_profit_per_trade();
    auto sharpe = get_sharpe_ratio();
    
    SLOG_INFO("Performance Summary", {
        {"orders_placed", std::to_string(trading.orders_placed.get())},
        {"orders_filled", std::to_string(trading.orders_filled.get())},
        {"success_rate", std::to_string(success_rate * 100) + "%"},
        {"avg_profit_per_trade", std::to_string(avg_profit)},
        {"sharpe_ratio", std::to_string(sharpe)},
        {"daily_pnl", std::to_string(trading.daily_pnl.load())},
        {"total_pnl", std::to_string(trading.total_pnl.load())},
        {"active_positions", std::to_string(trading.active_positions.load())},
        {"system_healthy", is_system_healthy() ? "true" : "false"}
    });
}

nlohmann::json PerformanceMonitor::get_metrics_json() const {
    nlohmann::json j;
    
    auto trading = get_trading_metrics();
    auto system = get_system_metrics();
    
    // Trading metrics
    j["trading"]["orders_placed"] = trading.orders_placed.get();
    j["trading"]["orders_filled"] = trading.orders_filled.get();
    j["trading"]["orders_cancelled"] = trading.orders_cancelled.get();
    j["trading"]["arbitrage_opportunities"] = trading.arbitrage_opportunities.get();
    j["trading"]["successful_trades"] = trading.successful_trades.get();
    j["trading"]["failed_trades"] = trading.failed_trades.get();
    j["trading"]["daily_pnl"] = trading.daily_pnl.load();
    j["trading"]["total_pnl"] = trading.total_pnl.load();
    j["trading"]["active_positions"] = trading.active_positions.load();
    j["trading"]["success_rate"] = get_success_rate();
    j["trading"]["avg_profit_per_trade"] = get_average_profit_per_trade();
    j["trading"]["sharpe_ratio"] = get_sharpe_ratio();
    
    // System metrics
    auto cpu_stats = system.cpu_usage_percent.get_statistics();
    auto memory_stats = system.memory_usage_mb.get_statistics();
    auto latency_stats = system.network_latency_ms.get_statistics();
    
    j["system"]["cpu_usage_avg"] = cpu_stats.mean;
    j["system"]["memory_usage_avg"] = memory_stats.mean;
    j["system"]["network_latency_avg"] = latency_stats.mean;
    j["system"]["websocket_reconnections"] = system.websocket_reconnections.get();
    j["system"]["api_errors"] = system.api_errors.get();
    j["system"]["risk_violations"] = system.risk_violations.get();
    j["system"]["is_healthy"] = is_system_healthy();
    
    return j;
}

void PerformanceMonitor::reset_daily_metrics() {
    trading_metrics_.daily_pnl.store(0.0);
    // Note: We don't reset counters as they represent cumulative values
    SLOG_INFO("Daily metrics reset");
}

void PerformanceMonitor::reset_all_metrics() {
    std::lock_guard<std::mutex> lock(metrics_mutex_);
    
    trading_metrics_ = TradingMetrics{};
    system_metrics_ = SystemMetrics{};
    exchange_latency_.clear();
    
    SLOG_INFO("All performance metrics reset");
}

} // namespace ats