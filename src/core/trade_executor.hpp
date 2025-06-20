#pragma once

#include <string>
#include <vector>
#include <queue>
#include <unordered_map>
#include <memory>
#include <atomic>
#include <mutex>
#include <thread>
#include <condition_variable>
#include <functional>
#include <chrono>

#include "opportunity_detector.hpp"
#include "risk_manager.hpp"
#include "../exchange/exchange_interface.hpp"
#include "../core/types.hpp"

namespace ats {

// Note: TradeState moved to types.hpp to avoid duplication

// Order time tracking additions
struct OrderTiming {
    std::chrono::system_clock::time_point created_time;
    std::chrono::system_clock::time_point filled_time;
};

// Trade execution plan
struct ExecutionPlan {
    std::string trade_id;
    ArbitrageOpportunity opportunity;
    double execution_volume;
    
    // Buy side
    Order buy_order;
    double buy_timeout_seconds;
    
    // Sell side  
    Order sell_order;
    double sell_timeout_seconds;
    
    // Execution strategy
    bool use_market_orders;
    bool enable_partial_fills;
    double max_slippage_percent;
    double price_improvement_threshold;
    
    // Risk parameters
    double stop_loss_price;
    double take_profit_price;
    double max_execution_time_seconds;
    
    ExecutionPlan() : execution_volume(0.0), buy_timeout_seconds(30.0),
                     sell_timeout_seconds(30.0), use_market_orders(true),
                     enable_partial_fills(false), max_slippage_percent(0.5),
                     price_improvement_threshold(0.1), stop_loss_price(0.0),
                     take_profit_price(0.0), max_execution_time_seconds(120.0) {}
};

// Note: ExecutionResult moved to types.hpp to avoid duplication

// Active trade tracking
struct ActiveTrade {
    ExecutionPlan plan;
    TradeState current_state;
    std::chrono::system_clock::time_point start_time;
    std::chrono::system_clock::time_point last_update;
    
    // Progress tracking
    bool buy_completed;
    bool sell_completed;
    double buy_fill_percentage;
    double sell_fill_percentage;
    
    // Risk monitoring
    bool stop_loss_triggered;
    bool take_profit_triggered;
    bool timeout_reached;
    
    ActiveTrade() : current_state(TradeState::PENDING), buy_completed(false),
                   sell_completed(false), buy_fill_percentage(0.0),
                   sell_fill_percentage(0.0), stop_loss_triggered(false),
                   take_profit_triggered(false), timeout_reached(false) {}
};

class TradeExecutor {
public:
    using ExecutionCallback = std::function<void(const ExecutionResult&)>;
    using StateCallback = std::function<void(const std::string& trade_id, TradeState state)>;
    using ErrorCallback = std::function<void(const std::string& trade_id, const std::string& error)>;

private:
    ConfigManager* config_manager_;
    RiskManager* risk_manager_;
    
    // Exchange connections
    std::unordered_map<std::string, ExchangeInterface*> exchanges_;
    mutable std::mutex exchanges_mutex_;
    
    // Trade queue and execution
    std::queue<ExecutionPlan> pending_trades_;
    std::unordered_map<std::string, ActiveTrade> active_trades_;
    mutable std::mutex trade_queue_mutex_;
    mutable std::mutex active_trades_mutex_;
    std::condition_variable trade_queue_cv_;
    
    // Threading
    std::vector<std::thread> execution_threads_;
    std::thread monitoring_thread_;
    std::atomic<bool> running_;
    size_t num_execution_threads_;
    
    // Callbacks
    ExecutionCallback execution_callback_;
    StateCallback state_callback_;
    ErrorCallback error_callback_;
    
    // Execution results history
    std::vector<ExecutionResult> execution_history_;
    mutable std::mutex history_mutex_;
    size_t max_history_size_;
    
    // Performance tracking
    std::atomic<long long> trades_executed_;
    std::atomic<long long> trades_successful_;
    std::atomic<long long> trades_failed_;
    std::atomic<double> total_profit_;
    std::atomic<double> total_volume_;
    std::chrono::system_clock::time_point start_time_;

public:
    explicit TradeExecutor(ConfigManager* config_manager, RiskManager* risk_manager);
    ~TradeExecutor();
    
    // Lifecycle
    bool Initialize();
    void Start();
    void Stop();
    bool IsRunning() const { return running_.load(); }
    
    // Exchange management
    void AddExchange(const std::string& name, ExchangeInterface* exchange);
    void RemoveExchange(const std::string& name);
    ExchangeInterface* GetExchange(const std::string& name) const;
    std::vector<std::string> GetAvailableExchanges() const;
    
    // Trade execution
    std::string ExecuteTrade(const ArbitrageOpportunity& opportunity, double volume);
    std::string ExecuteTradeWithPlan(const ExecutionPlan& plan);
    bool CancelTrade(const std::string& trade_id);
    
    // Trade monitoring
    ActiveTrade* GetActiveTrade(const std::string& trade_id);
    std::vector<ActiveTrade> GetActiveTrades() const;
    TradeState GetTradeState(const std::string& trade_id) const;
    bool IsTradeActive(const std::string& trade_id) const;
    
    // Configuration
    void SetExecutionCallback(ExecutionCallback callback) { execution_callback_ = callback; }
    void SetStateCallback(StateCallback callback) { state_callback_ = callback; }
    void SetErrorCallback(ErrorCallback callback) { error_callback_ = callback; }
    
    void SetNumExecutionThreads(size_t num_threads) { num_execution_threads_ = num_threads; }
    void SetMaxHistorySize(size_t max_size) { max_history_size_ = max_size; }
    
    // Execution plan creation
    ExecutionPlan CreateExecutionPlan(const ArbitrageOpportunity& opportunity, double volume);
    ExecutionPlan OptimizeExecutionPlan(const ExecutionPlan& base_plan);
    
    // Order management
    bool PlaceOrder(Order& order, ExchangeInterface* exchange);
    bool CancelOrder(const std::string& order_id, ExchangeInterface* exchange);
    bool UpdateOrderStatus(Order& order, ExchangeInterface* exchange);
    
    // Execution history
    std::vector<ExecutionResult> GetExecutionHistory(size_t count = 100) const;
    std::vector<ExecutionResult> GetHistoryForSymbol(const std::string& symbol, size_t count = 50) const;
    ExecutionResult* GetExecutionResult(const std::string& trade_id);
    
    // Statistics
    long long GetTradesExecuted() const { return trades_executed_.load(); }
    long long GetTradesSuccessful() const { return trades_successful_.load(); }
    long long GetTradesFailed() const { return trades_failed_.load(); }
    double GetSuccessRate() const;
    double GetTotalProfit() const { return total_profit_.load(); }
    double GetTotalVolume() const { return total_volume_.load(); }
    double GetAvgExecutionTime() const;
    double GetAvgProfitPerTrade() const;
    
    // Health and status
    bool IsHealthy() const;
    std::string GetStatus() const;
    void LogStatistics() const;
    void ResetStatistics();

private:
    // Main execution loops
    void ExecutionLoop();
    void MonitoringLoop();
    
    // Trade execution workflow
    ExecutionResult ExecuteTradeInternal(const ExecutionPlan& plan);
    bool ExecuteBuyOrder(ActiveTrade& trade);
    bool ExecuteSellOrder(ActiveTrade& trade);
    bool WaitForOrderFill(Order& order, ExchangeInterface* exchange, double timeout_seconds);
    
    // Order execution strategies
    bool ExecuteMarketOrder(Order& order, ExchangeInterface* exchange);
    bool ExecuteLimitOrder(Order& order, ExchangeInterface* exchange);
    bool ExecuteSmartOrder(Order& order, ExchangeInterface* exchange, double max_slippage);
    
    // Risk and validation
    bool ValidateExecution(const ExecutionPlan& plan);
    bool CheckExecutionRisks(const ActiveTrade& trade);
    void HandleStopLoss(ActiveTrade& trade);
    void HandleTakeProfit(ActiveTrade& trade);
    void HandleTimeout(ActiveTrade& trade);
    
    // State management
    void UpdateTradeState(const std::string& trade_id, TradeState new_state);
    void RecordExecutionResult(const ExecutionResult& result);
    void NotifyStateChange(const std::string& trade_id, TradeState state);
    void NotifyError(const std::string& trade_id, const std::string& error);
    
    // Helper functions
    std::string GenerateTradeId();
    std::string GenerateOrderId();
    double CalculateSlippage(double expected_price, double actual_price, OrderSide side);
    double CalculateFees(const Order& order, ExchangeInterface* exchange);
    double CalculateNetProfit(const ExecutionResult& result);
    
    // Cleanup and maintenance
    void CleanupCompletedTrades();
    void CleanupOldHistory();
    void UpdatePerformanceMetrics(const ExecutionResult& result);
    
    // Error handling
    void HandleExecutionError(ActiveTrade& trade, const std::string& error);
    void HandleExchangeError(const std::string& exchange_name, const std::string& error);
    bool ShouldRetryOrder(const Order& order, const std::string& error);
};

} // namespace ats 