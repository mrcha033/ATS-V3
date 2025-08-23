#pragma once

#include "types/common_types.hpp"
#include "config/config_manager.hpp"
#include "utils/prometheus_exporter.hpp"
#include <memory>
#include <vector>
#include <unordered_map>
#include <thread>
#include <atomic>
#include <mutex>
#include <condition_variable>
#include <chrono>
#include <functional>

namespace ats {
namespace trading_engine {

// Forward declarations
class OrderRouter;
class SpreadCalculator;
class RiskManager;
class TradeLogger;
class RedisSubscriber;

// Trade execution result
enum class ExecutionResult {
    SUCCESS,
    PARTIAL_SUCCESS,
    FAILURE,
    TIMEOUT,
    INSUFFICIENT_BALANCE,
    MARKET_CLOSED,
    RISK_LIMIT_EXCEEDED,
    INVALID_ORDER
};

// Trade execution details
struct TradeExecution {
    std::string trade_id;
    std::string symbol;
    std::string buy_exchange;
    std::string sell_exchange;
    double buy_price;
    double sell_price;
    double quantity;
    double executed_quantity;
    double expected_profit;
    double actual_profit;
    double total_fees;
    ExecutionResult result;
    std::chrono::system_clock::time_point timestamp;
    std::chrono::milliseconds execution_latency;
    std::vector<types::Order> orders;
    std::string error_message;
    
    TradeExecution() 
        : quantity(0), executed_quantity(0), expected_profit(0)
        , actual_profit(0), total_fees(0), result(ExecutionResult::FAILURE)
        , timestamp(std::chrono::system_clock::now())
        , execution_latency(std::chrono::milliseconds(0)) {}
};

// Arbitrage opportunity
struct ArbitrageOpportunity {
    std::string symbol;
    std::string buy_exchange;
    std::string sell_exchange;
    double buy_price;
    double sell_price;
    double available_quantity;
    double spread_percentage;
    double expected_profit;
    double confidence_score;
    std::chrono::system_clock::time_point detected_at;
    std::chrono::milliseconds validity_window;
    
    // Risk assessment
    double max_position_size;
    double estimated_slippage;
    double total_fees;
    bool risk_approved;
    
    ArbitrageOpportunity()
        : buy_price(0), sell_price(0), available_quantity(0)
        , spread_percentage(0), expected_profit(0), confidence_score(0)
        , detected_at(std::chrono::system_clock::now())
        , validity_window(std::chrono::milliseconds(5000))
        , max_position_size(0), estimated_slippage(0)
        , total_fees(0), risk_approved(false) {}
};

// Trading engine configuration
struct TradingEngineConfig {
    bool enabled;
    double min_spread_threshold;
    double max_position_size;
    double max_daily_volume;
    int max_concurrent_trades;
    std::chrono::milliseconds execution_timeout;
    std::chrono::milliseconds opportunity_timeout;
    
    // Risk limits
    double max_portfolio_exposure;
    double max_single_trade_size;
    double emergency_stop_loss;
    
    // Fee configuration
    std::unordered_map<std::string, double> exchange_fees;
    double slippage_tolerance;
    
    // Performance settings
    int worker_thread_count;
    size_t max_queue_size;
    bool enable_paper_trading;
    bool enable_rollback_on_failure;
    
    TradingEngineConfig()
        : enabled(false), min_spread_threshold(0.005)
        , max_position_size(10000.0), max_daily_volume(100000.0)
        , max_concurrent_trades(5)
        , execution_timeout(std::chrono::milliseconds(30000))
        , opportunity_timeout(std::chrono::milliseconds(5000))
        , max_portfolio_exposure(0.8), max_single_trade_size(0.1)
        , emergency_stop_loss(0.05), slippage_tolerance(0.001)
        , worker_thread_count(4), max_queue_size(1000)
        , enable_paper_trading(false), enable_rollback_on_failure(true) {}
};

// Trading engine statistics
struct TradingStatistics {
    std::atomic<size_t> total_opportunities_detected{0};
    std::atomic<size_t> total_opportunities_executed{0};
    std::atomic<size_t> total_successful_trades{0};
    std::atomic<size_t> total_failed_trades{0};
    std::atomic<size_t> total_rollbacks{0};
    
    std::atomic<double> total_profit_loss{0.0};
    std::atomic<double> total_fees_paid{0.0};
    std::atomic<double> total_volume_traded{0.0};
    std::atomic<double> success_rate{0.0};
    std::atomic<double> average_profit_per_trade{0.0};
    
    std::atomic<std::chrono::milliseconds> average_execution_time{std::chrono::milliseconds(0)};
    std::atomic<std::chrono::milliseconds> fastest_execution{std::chrono::milliseconds(999999)};
    std::atomic<std::chrono::milliseconds> slowest_execution{std::chrono::milliseconds(0)};
    
    std::chrono::system_clock::time_point session_start_time;
    std::atomic<std::chrono::milliseconds> uptime{std::chrono::milliseconds(0)};
    
    TradingStatistics() {
        session_start_time = std::chrono::system_clock::now();
    }
};

// Main trading engine service
class TradingEngineService {
public:
    TradingEngineService();
    ~TradingEngineService();
    
    // Service lifecycle
    bool initialize(const config::ConfigManager& config);
    bool start();
    void stop();
    bool is_running() const;
    
    // Configuration management
    void update_config(const TradingEngineConfig& config);
    TradingEngineConfig get_config() const;
    
    // Trading operations
    bool execute_arbitrage(const ArbitrageOpportunity& opportunity);
    std::vector<TradeExecution> get_active_trades() const;
    std::vector<TradeExecution> get_completed_trades(std::chrono::hours lookback = std::chrono::hours(24)) const;
    
    // Manual trading (for testing/debugging)
    std::string submit_manual_trade(const std::string& symbol, 
                                   const std::string& buy_exchange,
                                   const std::string& sell_exchange,
                                   double quantity);
    bool cancel_trade(const std::string& trade_id);
    
    // Position and balance management
    types::Portfolio get_current_portfolio() const;
    std::unordered_map<std::string, types::Balance> get_exchange_balances() const;
    double get_available_balance(const std::string& exchange, const types::Currency& currency) const;
    
    // Risk management integration
    bool set_risk_manager(std::shared_ptr<RiskManager> risk_manager);
    bool is_trade_approved(const ArbitrageOpportunity& opportunity) const;
    void emergency_stop();
    bool is_emergency_stopped() const;
    
    // Statistics and monitoring
    TradingStatistics get_statistics() const;
    double get_current_profit_loss() const;
    double get_daily_volume() const;
    size_t get_active_trade_count() const;
    
    // Event callbacks
    using OpportunityCallback = std::function<void(const ArbitrageOpportunity&)>;
    using ExecutionCallback = std::function<void(const TradeExecution&)>;
    using ErrorCallback = std::function<void(const std::string& error)>;
    
    void set_opportunity_callback(OpportunityCallback callback);
    void set_execution_callback(ExecutionCallback callback);
    void set_error_callback(ErrorCallback callback);
    
    // Health and diagnostics
    bool is_healthy() const;
    std::vector<std::string> get_health_issues() const;
    std::string get_status_report() const;
    
private:
    // Configuration and state
    TradingEngineConfig config_;
    std::atomic<bool> running_{false};
    std::atomic<bool> initialized_{false};
    std::atomic<bool> emergency_stopped_{false};
    
    // Core components
    std::unique_ptr<RedisSubscriber> redis_subscriber_;
    std::unique_ptr<OrderRouter> order_router_;
    std::unique_ptr<SpreadCalculator> spread_calculator_;
    std::unique_ptr<TradeLogger> trade_logger_;
    std::shared_ptr<RiskManager> risk_manager_;
    std::unique_ptr<monitoring::PrometheusExporter> prometheus_exporter_;
    
    // Threading and queuing
    std::vector<std::thread> worker_threads_;
    std::thread price_monitoring_thread_;
    std::thread statistics_thread_;
    
    // Opportunity queue
    std::queue<ArbitrageOpportunity> opportunity_queue_;
    std::mutex queue_mutex_;
    std::condition_variable queue_condition_;
    
    // Active trades tracking
    std::unordered_map<std::string, TradeExecution> active_trades_;
    std::vector<TradeExecution> completed_trades_;
    mutable std::shared_mutex trades_mutex_;
    
    // Statistics
    mutable TradingStatistics statistics_;
    std::mutex statistics_mutex_;
    
    // Callbacks
    OpportunityCallback opportunity_callback_;
    ExecutionCallback execution_callback_;
    ErrorCallback error_callback_;
    
    // Event handlers
    void on_price_update(const types::Ticker& ticker);
    void on_arbitrage_opportunity_detected(const ArbitrageOpportunity& opportunity);
    void on_trade_execution_completed(const TradeExecution& execution);
    void on_error_occurred(const std::string& error);
    
    // Worker thread functions
    void worker_thread_main();
    void price_monitoring_thread_main();
    void statistics_thread_main();
    
    // Trading logic
    bool validate_opportunity(const ArbitrageOpportunity& opportunity) const;
    TradeExecution execute_trade_internal(const ArbitrageOpportunity& opportunity);
    bool rollback_trade(const TradeExecution& execution);
    
    // Order management
    std::string generate_trade_id() const;
    bool place_simultaneous_orders(const ArbitrageOpportunity& opportunity, TradeExecution& execution);
    bool monitor_order_execution(TradeExecution& execution);
    
    // Utility methods
    void update_statistics();
    void collect_system_metrics();
    double get_cpu_usage();
    double get_memory_usage();
    void cleanup_completed_trades();
    void log_trade_execution(const TradeExecution& execution);
    void handle_error(const std::string& error_message);
    
    // Initialization helpers
    bool initialize_redis_subscriber(const config::ConfigManager& config);
    bool initialize_order_router(const config::ConfigManager& config);
    bool initialize_spread_calculator(const config::ConfigManager& config);
    
    void start_worker_threads();
    void stop_worker_threads();
};

// Utility functions for trading operations
namespace trading_utils {
    
    // Opportunity validation
    bool is_valid_opportunity(const ArbitrageOpportunity& opportunity);
    bool is_opportunity_expired(const ArbitrageOpportunity& opportunity);
    double calculate_opportunity_score(const ArbitrageOpportunity& opportunity);
    
    // Profit calculations
    double calculate_expected_profit(double buy_price, double sell_price, double quantity,
                                   double buy_fee, double sell_fee, double slippage = 0.0);
    double calculate_actual_profit(const TradeExecution& execution);
    double calculate_total_fees(const std::vector<types::Order>& orders);
    
    // Risk assessment
    bool is_within_risk_limits(const ArbitrageOpportunity& opportunity, 
                              const TradingEngineConfig& config);
    double calculate_max_safe_quantity(const ArbitrageOpportunity& opportunity,
                                     const types::Portfolio& portfolio);
    
    // Order utilities
    types::Order create_buy_order(const std::string& exchange, const std::string& symbol,
                                double quantity, double price);
    types::Order create_sell_order(const std::string& exchange, const std::string& symbol,
                                 double quantity, double price);
    
    // Time and latency calculations
    std::chrono::milliseconds calculate_execution_latency(
        std::chrono::system_clock::time_point start_time);
    bool is_within_timeout(std::chrono::system_clock::time_point start_time,
                          std::chrono::milliseconds timeout);
    
    // Statistics helpers
    double calculate_success_rate(size_t successful_trades, size_t total_trades);
    double calculate_average_profit(double total_profit, size_t trade_count);
    double calculate_sharpe_ratio(const std::vector<double>& returns);
    
    // Configuration validation
    bool validate_trading_config(const TradingEngineConfig& config);
    std::vector<std::string> get_config_validation_errors(const TradingEngineConfig& config);
    
    // Error handling
    std::string format_execution_error(const TradeExecution& execution);
    std::string format_opportunity_error(const ArbitrageOpportunity& opportunity);
    
    // Performance optimization
    void optimize_worker_thread_count(TradingEngineConfig& config);
    void tune_queue_size(TradingEngineConfig& config, size_t expected_throughput);
    
    // Data conversion
    nlohmann::json trade_execution_to_json(const TradeExecution& execution);
    nlohmann::json arbitrage_opportunity_to_json(const ArbitrageOpportunity& opportunity);
    nlohmann::json trading_statistics_to_json(const TradingStatistics& stats);
}

} // namespace trading_engine
} // namespace ats