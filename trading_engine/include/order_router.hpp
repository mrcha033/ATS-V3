#pragma once

#include "types/common_types.hpp"
#include "trading_engine_service.hpp"
#include <memory>
#include <unordered_map>
#include <vector>
#include <future>
#include <chrono>
#include <functional>

namespace ats {
namespace trading_engine {

// Order execution status
enum class OrderExecutionStatus {
    PENDING,
    SUBMITTED,
    PARTIALLY_FILLED,
    FILLED,
    CANCELED,
    REJECTED,
    EXPIRED,
    FAILED
};

// Order execution details
struct OrderExecutionDetails {
    std::string order_id;
    std::string exchange_order_id;
    types::Order original_order;
    OrderExecutionStatus status;
    double filled_quantity;
    double remaining_quantity;
    double average_fill_price;
    double total_fees;
    std::vector<types::Trade> fills;
    std::string error_message;
    std::chrono::system_clock::time_point submitted_at;
    std::chrono::system_clock::time_point last_updated;
    std::chrono::milliseconds execution_latency;
    
    OrderExecutionDetails()
        : status(OrderExecutionStatus::PENDING)
        , filled_quantity(0), remaining_quantity(0)
        , average_fill_price(0), total_fees(0)
        , submitted_at(std::chrono::system_clock::now())
        , last_updated(std::chrono::system_clock::now())
        , execution_latency(std::chrono::milliseconds(0)) {}
};

// Simultaneous order execution result
struct SimultaneousExecutionResult {
    std::string trade_id;
    std::vector<OrderExecutionDetails> order_executions;
    ExecutionResult overall_result;
    double total_filled_quantity;
    double average_execution_price_buy;
    double average_execution_price_sell;
    double actual_profit;
    double total_fees;
    std::chrono::milliseconds total_execution_time;
    std::string error_message;
    bool requires_rollback;
    
    SimultaneousExecutionResult()
        : overall_result(ExecutionResult::FAILURE)
        , total_filled_quantity(0), average_execution_price_buy(0)
        , average_execution_price_sell(0), actual_profit(0)
        , total_fees(0), total_execution_time(std::chrono::milliseconds(0))
        , requires_rollback(false) {}
};

// Exchange API interface for trading operations
class ExchangeTradingInterface {
public:
    virtual ~ExchangeTradingInterface() = default;
    
    // Basic trading operations
    virtual std::string place_order(const types::Order& order) = 0;
    virtual bool cancel_order(const std::string& order_id) = 0;
    virtual OrderExecutionDetails get_order_status(const std::string& order_id) = 0;
    virtual std::vector<OrderExecutionDetails> get_active_orders() = 0;
    
    // Advanced operations
    virtual std::string place_conditional_order(const types::Order& order, 
                                               const std::string& condition) = 0;
    virtual bool modify_order(const std::string& order_id, double new_price, double new_quantity) = 0;
    
    // Account information
    virtual std::vector<types::Balance> get_account_balances() = 0;
    virtual types::Balance get_balance(const types::Currency& currency) = 0;
    virtual double get_available_balance(const types::Currency& currency) = 0;
    
    // Trading limits and fees
    virtual double get_minimum_order_size(const std::string& symbol) = 0;
    virtual double get_maximum_order_size(const std::string& symbol) = 0;
    virtual double get_trading_fee(const std::string& symbol, bool is_maker = false) = 0;
    
    // Market data for trading
    virtual types::Ticker get_current_ticker(const std::string& symbol) = 0;
    virtual std::vector<std::pair<double, double>> get_order_book(const std::string& symbol, int depth = 20) = 0;
    
    // Exchange specific information
    virtual std::string get_exchange_id() const = 0;
    virtual bool is_connected() const = 0;
    virtual std::chrono::milliseconds get_average_latency() const = 0;
    virtual bool is_market_open() const = 0;
    
    // Error handling and diagnostics
    virtual std::string get_last_error() const = 0;
    virtual void clear_error() = 0;
    virtual bool is_healthy() const = 0;
};

// Order router configuration
struct OrderRouterConfig {
    std::chrono::milliseconds order_timeout;
    std::chrono::milliseconds execution_timeout;
    int max_retry_attempts;
    std::chrono::milliseconds retry_delay;
    bool enable_partial_fills;
    bool enable_aggressive_fills;
    double max_slippage_tolerance;
    bool enable_pre_trade_validation;
    bool enable_post_trade_validation;
    
    OrderRouterConfig()
        : order_timeout(std::chrono::milliseconds(30000))
        , execution_timeout(std::chrono::milliseconds(60000))
        , max_retry_attempts(3)
        , retry_delay(std::chrono::milliseconds(1000))
        , enable_partial_fills(true)
        , enable_aggressive_fills(false)
        , max_slippage_tolerance(0.01)
        , enable_pre_trade_validation(true)
        , enable_post_trade_validation(true) {}
};

// Main order router class
class OrderRouter {
public:
    OrderRouter();
    ~OrderRouter();
    
    // Initialization and configuration
    bool initialize(const OrderRouterConfig& config);
    void add_exchange(const std::string& exchange_id, 
                     std::unique_ptr<ExchangeTradingInterface> exchange);
    void remove_exchange(const std::string& exchange_id);
    
    // Single order operations
    std::future<OrderExecutionDetails> place_order_async(const types::Order& order);
    OrderExecutionDetails place_order_sync(const types::Order& order);
    bool cancel_order(const std::string& exchange_id, const std::string& order_id);
    
    // Simultaneous order execution (core arbitrage functionality)
    std::future<SimultaneousExecutionResult> execute_arbitrage_orders_async(
        const ArbitrageOpportunity& opportunity);
    
    SimultaneousExecutionResult execute_arbitrage_orders_sync(
        const ArbitrageOpportunity& opportunity);
    
    // Order monitoring and management
    OrderExecutionDetails get_order_status(const std::string& exchange_id, 
                                          const std::string& order_id);
    std::vector<OrderExecutionDetails> get_active_orders(const std::string& exchange_id = "");
    std::vector<OrderExecutionDetails> get_completed_orders(std::chrono::hours lookback);
    
    // Portfolio and balance management
    std::unordered_map<std::string, std::vector<types::Balance>> get_all_balances();
    types::Balance get_balance(const std::string& exchange_id, const types::Currency& currency);
    double get_available_balance(const std::string& exchange_id, const types::Currency& currency);
    
    // Pre-trade validation
    bool validate_order(const types::Order& order, std::string& error_message);
    bool validate_arbitrage_opportunity(const ArbitrageOpportunity& opportunity, 
                                      std::string& error_message);
    bool check_sufficient_balance(const ArbitrageOpportunity& opportunity);
    
    // Risk management integration
    bool is_order_within_limits(const types::Order& order);
    bool is_opportunity_within_limits(const ArbitrageOpportunity& opportunity);
    void set_position_limits(const std::unordered_map<std::string, double>& limits);
    
    // Performance monitoring
    struct PerformanceMetrics {
        std::atomic<size_t> total_orders_placed{0};
        std::atomic<size_t> successful_orders{0};
        std::atomic<size_t> failed_orders{0};
        std::atomic<size_t> canceled_orders{0};
        std::atomic<double> average_execution_time_ms{0.0};
        std::atomic<double> success_rate{0.0};
        std::atomic<double> average_slippage{0.0};
        std::atomic<double> total_fees_paid{0.0};
    };
    
    PerformanceMetrics get_performance_metrics() const;
    void reset_performance_metrics();
    
    // Health and diagnostics
    bool is_healthy() const;
    std::vector<std::string> get_health_issues() const;
    std::unordered_map<std::string, bool> get_exchange_statuses() const;
    
    // Configuration updates
    void update_config(const OrderRouterConfig& config);
    OrderRouterConfig get_config() const;
    
    // Event callbacks
    using OrderUpdateCallback = std::function<void(const OrderExecutionDetails&)>;
    using ExecutionCompletedCallback = std::function<void(const SimultaneousExecutionResult&)>;
    using ErrorCallback = std::function<void(const std::string& error)>;
    
    void set_order_update_callback(OrderUpdateCallback callback);
    void set_execution_completed_callback(ExecutionCompletedCallback callback);
    void set_error_callback(ErrorCallback callback);
    
private:
    struct Implementation;
    std::unique_ptr<Implementation> impl_;
    
    // Internal execution methods
    OrderExecutionDetails execute_single_order(const types::Order& order);
    SimultaneousExecutionResult execute_simultaneous_orders(
        const std::vector<types::Order>& orders);
    
    // Order lifecycle management
    void monitor_order_execution(const std::string& exchange_id, const std::string& order_id);
    void handle_order_update(const OrderExecutionDetails& details);
    void handle_execution_timeout(const std::string& order_id);
    
    // Retry and recovery logic
    bool should_retry_order(const OrderExecutionDetails& details);
    OrderExecutionDetails retry_order_execution(const types::Order& order, int attempt);
    bool attempt_order_recovery(const std::string& exchange_id, const std::string& order_id);
    
    // Validation helpers
    bool validate_order_parameters(const types::Order& order, std::string& error);
    bool validate_exchange_connectivity(const std::string& exchange_id);
    bool validate_market_conditions(const std::string& exchange_id, const std::string& symbol);
    
    // Balance and position tracking
    void update_balance_cache(const std::string& exchange_id);
    void track_position_changes(const OrderExecutionDetails& details);
    
    // Performance tracking
    void record_order_metrics(const OrderExecutionDetails& details);
    void update_performance_statistics();
    
    // Error handling
    void handle_exchange_error(const std::string& exchange_id, const std::string& error);
    void handle_order_rejection(const OrderExecutionDetails& details);
    void handle_execution_failure(const SimultaneousExecutionResult& result);
};

// Rollback manager for failed arbitrage trades
class RollbackManager {
public:
    RollbackManager();
    ~RollbackManager();
    
    // Rollback operations
    bool rollback_trade(const SimultaneousExecutionResult& failed_execution);
    bool rollback_partial_execution(const std::vector<OrderExecutionDetails>& executions);
    
    // Rollback strategies
    enum class RollbackStrategy {
        IMMEDIATE_CANCEL,
        MARKET_CLOSE,
        GRADUAL_LIQUIDATION,
        HEDGE_POSITION
    };
    
    bool execute_rollback_strategy(const SimultaneousExecutionResult& execution,
                                  RollbackStrategy strategy);
    
    // Rollback monitoring
    struct RollbackResult {
        std::string rollback_id;
        RollbackStrategy strategy_used;
        bool success;
        double recovered_amount;
        double remaining_exposure;
        std::vector<types::Order> rollback_orders;
        std::chrono::milliseconds rollback_time;
        std::string error_message;
    };
    
    std::vector<RollbackResult> get_rollback_history() const;
    RollbackResult get_rollback_status(const std::string& rollback_id) const;
    
    // Configuration
    void set_default_rollback_strategy(RollbackStrategy strategy);
    void set_rollback_timeout(std::chrono::milliseconds timeout);
    void enable_automatic_rollback(bool enable);
    
private:
    struct Implementation;
    std::unique_ptr<Implementation> impl_;
    
    // Rollback strategy implementations
    RollbackResult execute_immediate_cancel(const std::vector<OrderExecutionDetails>& executions);
    RollbackResult execute_market_close(const std::vector<OrderExecutionDetails>& executions);
    RollbackResult execute_gradual_liquidation(const std::vector<OrderExecutionDetails>& executions);
    RollbackResult execute_hedge_position(const std::vector<OrderExecutionDetails>& executions);
    
    // Helper methods
    std::vector<types::Order> create_offsetting_orders(const std::vector<OrderExecutionDetails>& executions);
    double calculate_rollback_quantity(const OrderExecutionDetails& execution);
    bool validate_rollback_feasibility(const std::vector<OrderExecutionDetails>& executions);
};

// Utility functions for order routing
namespace order_router_utils {
    
    // Order creation helpers
    types::Order create_market_buy_order(const std::string& exchange, const std::string& symbol,
                                        double quantity);
    types::Order create_market_sell_order(const std::string& exchange, const std::string& symbol,
                                         double quantity);
    types::Order create_limit_buy_order(const std::string& exchange, const std::string& symbol,
                                       double quantity, double price);
    types::Order create_limit_sell_order(const std::string& exchange, const std::string& symbol,
                                        double quantity, double price);
    
    // Order validation
    bool is_valid_order_size(double quantity, double min_size, double max_size);
    bool is_valid_price(double price, double tick_size);
    bool is_reasonable_execution_time(std::chrono::milliseconds execution_time);
    
    // Slippage calculations
    double calculate_price_slippage(double expected_price, double actual_price);
    double calculate_percentage_slippage(double expected_price, double actual_price);
    bool is_slippage_acceptable(double slippage, double tolerance);
    
    // Timing utilities
    std::chrono::milliseconds calculate_execution_latency(
        std::chrono::system_clock::time_point start_time);
    bool is_within_timeout(std::chrono::system_clock::time_point start_time,
                          std::chrono::milliseconds timeout);
    
    // Performance calculations
    double calculate_fill_rate(double filled_quantity, double total_quantity);
    double calculate_average_fill_price(const std::vector<types::Trade>& fills);
    double calculate_total_trading_fees(const std::vector<types::Trade>& fills);
    
    // Error handling
    std::string format_order_error(const OrderExecutionDetails& details);
    std::string format_execution_error(const SimultaneousExecutionResult& result);
    bool is_recoverable_error(const std::string& error_message);
    
    // Data conversion
    nlohmann::json order_execution_to_json(const OrderExecutionDetails& details);
    nlohmann::json simultaneous_execution_to_json(const SimultaneousExecutionResult& result);
    
    // Risk assessment
    double calculate_order_risk(const types::Order& order, const types::Portfolio& portfolio);
    double calculate_execution_risk(const ArbitrageOpportunity& opportunity);
    bool is_execution_safe(const SimultaneousExecutionResult& result);
}

} // namespace trading_engine
} // namespace ats