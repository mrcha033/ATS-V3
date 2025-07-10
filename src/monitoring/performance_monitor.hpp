#pragma once

#include <chrono>
#include <memory>
#include <unordered_map>
#include <string>
#include <atomic>
#include <mutex>
#include "../utils/atomic_counter.hpp"
#include "../utils/structured_logger.hpp"

namespace ats {

// Performance metrics for trading operations
struct TradingMetrics {
    AtomicCounter orders_placed;
    AtomicCounter orders_filled;
    AtomicCounter orders_cancelled;
    AtomicCounter arbitrage_opportunities;
    AtomicCounter successful_trades;
    AtomicCounter failed_trades;
    StatsTracker order_latency_ms;
    StatsTracker profit_per_trade;
    StatsTracker slippage_percent;
    std::atomic<double> total_pnl{0.0};
    std::atomic<double> daily_pnl{0.0};
    std::atomic<size_t> active_positions{0};
};

// System performance metrics
struct SystemMetrics {
    StatsTracker cpu_usage_percent;
    StatsTracker memory_usage_mb;
    StatsTracker network_latency_ms;
    AtomicCounter websocket_reconnections;
    AtomicCounter api_errors;
    AtomicCounter risk_violations;
    std::atomic<std::chrono::steady_clock::time_point> last_heartbeat{std::chrono::steady_clock::now()};
};

class PerformanceMonitor {
public:
    static PerformanceMonitor& instance();
    
    // Trading metrics
    void record_order_placed(const std::string& exchange);
    void record_order_filled(const std::string& exchange, double latency_ms);
    void record_order_cancelled(const std::string& exchange);
    void record_arbitrage_opportunity(const std::string& symbol, double profit_percent);
    void record_successful_trade(double profit, double slippage_percent);
    void record_failed_trade(const std::string& reason);
    void update_pnl(double daily_pnl, double total_pnl);
    void update_active_positions(size_t count);
    
    // System metrics
    void record_cpu_usage(double percent);
    void record_memory_usage(double mb);
    void record_network_latency(const std::string& endpoint, double ms);
    void record_websocket_reconnection(const std::string& exchange);
    void record_api_error(const std::string& exchange, const std::string& error);
    void record_risk_violation(const std::string& rule);
    void update_heartbeat();
    
    // Getters
    TradingMetrics get_trading_metrics() const;
    SystemMetrics get_system_metrics() const;
    
    // Performance analysis
    double get_success_rate() const;
    double get_average_profit_per_trade() const;
    double get_sharpe_ratio() const;
    bool is_system_healthy() const;
    
    // Reporting
    void log_performance_summary() const;
    nlohmann::json get_metrics_json() const;
    
    // Reset metrics
    void reset_daily_metrics();
    void reset_all_metrics();

private:
    PerformanceMonitor() = default;
    
    mutable std::mutex metrics_mutex_;
    TradingMetrics trading_metrics_;
    SystemMetrics system_metrics_;
    std::unordered_map<std::string, StatsTracker> exchange_latency_;
    
    // Performance thresholds
    static constexpr double CPU_WARNING_THRESHOLD = 80.0;
    static constexpr double MEMORY_WARNING_THRESHOLD = 85.0;
    static constexpr double LATENCY_WARNING_THRESHOLD = 1000.0; // ms
    static constexpr std::chrono::seconds HEARTBEAT_WARNING_THRESHOLD{30};
};

// RAII timer for measuring operation duration
class ScopedTimer {
public:
    explicit ScopedTimer(const std::string& operation_name)
        : operation_name_(operation_name)
        , start_time_(std::chrono::high_resolution_clock::now()) {}
    
    ~ScopedTimer() {
        auto end_time = std::chrono::high_resolution_clock::now();
        auto duration = std::chrono::duration_cast<std::chrono::microseconds>(
            end_time - start_time_).count();
        
        SLOG_PERFORMANCE(operation_name_ + "_duration", duration / 1000.0, "ms");
    }

private:
    std::string operation_name_;
    std::chrono::high_resolution_clock::time_point start_time_;
};

// Convenient macros for performance monitoring
#define MONITOR_TRADE_PLACED(exchange) \
    ats::PerformanceMonitor::instance().record_order_placed(exchange)

#define MONITOR_TRADE_FILLED(exchange, latency) \
    ats::PerformanceMonitor::instance().record_order_filled(exchange, latency)

#define MONITOR_OPPORTUNITY(symbol, profit) \
    ats::PerformanceMonitor::instance().record_arbitrage_opportunity(symbol, profit)

#define MONITOR_SUCCESSFUL_TRADE(profit, slippage) \
    ats::PerformanceMonitor::instance().record_successful_trade(profit, slippage)

#define MONITOR_FAILED_TRADE(reason) \
    ats::PerformanceMonitor::instance().record_failed_trade(reason)

#define MONITOR_SYSTEM_CPU(percent) \
    ats::PerformanceMonitor::instance().record_cpu_usage(percent)

#define MONITOR_SYSTEM_MEMORY(mb) \
    ats::PerformanceMonitor::instance().record_memory_usage(mb)

#define MONITOR_LATENCY(endpoint, ms) \
    ats::PerformanceMonitor::instance().record_network_latency(endpoint, ms)

#define SCOPED_TIMER(name) \
    ats::ScopedTimer timer(name)

} // namespace ats