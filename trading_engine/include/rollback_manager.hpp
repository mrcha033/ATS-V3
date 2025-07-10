#pragma once

#include "order_router.hpp"
#include "trading_engine_service.hpp"
#include "types/common_types.hpp"
#include <memory>
#include <vector>
#include <unordered_map>
#include <chrono>
#include <functional>
#include <queue>

namespace ats {
namespace trading_engine {

// Enhanced rollback strategies
enum class RollbackStrategy {
    IMMEDIATE_CANCEL,      // Cancel unfilled orders immediately
    MARKET_CLOSE,          // Close positions at market price
    GRADUAL_LIQUIDATION,   // Slowly liquidate over time
    HEDGE_POSITION,        // Create offsetting positions
    SMART_LIQUIDATION,     // Use order book analysis for optimal liquidation
    STOP_LOSS_ROLLBACK,    // Execute stop-loss orders
    PARTIAL_ROLLBACK       // Only rollback partially filled orders
};

// Rollback trigger conditions
enum class RollbackTrigger {
    ORDER_FAILURE,         // Order placement failed
    EXECUTION_TIMEOUT,     // Order execution timeout
    PARTIAL_FILL_TIMEOUT,  // Partial fill timeout
    RISK_LIMIT_BREACH,     // Risk limits exceeded
    MARKET_DISRUPTION,     // Market conditions deteriorated
    MANUAL_TRIGGER,        // Manual intervention
    EMERGENCY_STOP         // System emergency stop
};

// Rollback severity levels
enum class RollbackSeverity {
    LOW,       // Minor issue, gentle rollback
    MEDIUM,    // Moderate issue, standard rollback
    HIGH,      // Serious issue, aggressive rollback
    CRITICAL   // Emergency, immediate liquidation
};

// Enhanced rollback result with detailed information
struct EnhancedRollbackResult {
    std::string rollback_id;
    std::string trade_id;
    RollbackStrategy strategy_used;
    RollbackTrigger trigger;
    RollbackSeverity severity;
    
    bool success;
    double initial_exposure;
    double recovered_amount;
    double remaining_exposure;
    double rollback_cost;
    double slippage_incurred;
    
    std::vector<types::Order> rollback_orders;
    std::vector<OrderExecutionDetails> executed_rollbacks;
    std::chrono::milliseconds rollback_time;
    std::chrono::system_clock::time_point initiated_at;
    std::chrono::system_clock::time_point completed_at;
    
    std::string error_message;
    std::string rollback_notes;
    std::unordered_map<std::string, double> metrics;
    
    EnhancedRollbackResult()
        : strategy_used(RollbackStrategy::IMMEDIATE_CANCEL)
        , trigger(RollbackTrigger::ORDER_FAILURE)
        , severity(RollbackSeverity::MEDIUM)
        , success(false), initial_exposure(0), recovered_amount(0)
        , remaining_exposure(0), rollback_cost(0), slippage_incurred(0)
        , rollback_time(std::chrono::milliseconds(0))
        , initiated_at(std::chrono::system_clock::now())
        , completed_at(std::chrono::system_clock::now()) {}
};

// Rollback configuration and policies
struct RollbackPolicy {
    std::unordered_map<RollbackTrigger, RollbackStrategy> default_strategies;
    std::unordered_map<RollbackSeverity, std::chrono::milliseconds> max_rollback_times;
    
    double max_acceptable_slippage = 0.05;  // 5%
    double emergency_liquidation_threshold = 0.1;  // 10%
    std::chrono::milliseconds partial_fill_timeout = std::chrono::milliseconds(30000);
    std::chrono::milliseconds rollback_timeout = std::chrono::milliseconds(60000);
    
    bool enable_smart_liquidation = true;
    bool enable_hedging = true;
    bool enable_gradual_liquidation = true;
    int max_rollback_attempts = 3;
    
    RollbackPolicy() {
        // Set default strategies for different triggers
        default_strategies[RollbackTrigger::ORDER_FAILURE] = RollbackStrategy::IMMEDIATE_CANCEL;
        default_strategies[RollbackTrigger::EXECUTION_TIMEOUT] = RollbackStrategy::MARKET_CLOSE;
        default_strategies[RollbackTrigger::PARTIAL_FILL_TIMEOUT] = RollbackStrategy::PARTIAL_ROLLBACK;
        default_strategies[RollbackTrigger::RISK_LIMIT_BREACH] = RollbackStrategy::SMART_LIQUIDATION;
        default_strategies[RollbackTrigger::MARKET_DISRUPTION] = RollbackStrategy::HEDGE_POSITION;
        default_strategies[RollbackTrigger::EMERGENCY_STOP] = RollbackStrategy::MARKET_CLOSE;
        
        // Set maximum rollback times by severity
        max_rollback_times[RollbackSeverity::LOW] = std::chrono::milliseconds(120000);      // 2 minutes
        max_rollback_times[RollbackSeverity::MEDIUM] = std::chrono::milliseconds(60000);    // 1 minute
        max_rollback_times[RollbackSeverity::HIGH] = std::chrono::milliseconds(30000);      // 30 seconds
        max_rollback_times[RollbackSeverity::CRITICAL] = std::chrono::milliseconds(10000);  // 10 seconds
    }
};

// Risk assessment for rollback decisions
struct RollbackRiskAssessment {
    double market_volatility;
    double liquidity_score;
    double price_stability;
    double order_book_depth;
    double rollback_urgency;
    
    bool is_market_hours;
    bool has_sufficient_liquidity;
    bool is_volatile_period;
    
    std::string assessment_notes;
    std::chrono::system_clock::time_point assessed_at;
    
    RollbackRiskAssessment()
        : market_volatility(0), liquidity_score(0), price_stability(0)
        , order_book_depth(0), rollback_urgency(0)
        , is_market_hours(true), has_sufficient_liquidity(true)
        , is_volatile_period(false)
        , assessed_at(std::chrono::system_clock::now()) {}
};

// Enhanced rollback manager
class EnhancedRollbackManager {
public:
    EnhancedRollbackManager();
    ~EnhancedRollbackManager();
    
    // Initialization and configuration
    bool initialize(const RollbackPolicy& policy);
    void set_order_router(std::shared_ptr<OrderRouter> order_router);
    void set_spread_calculator(std::shared_ptr<SpreadCalculator> spread_calculator);
    
    // Main rollback operations
    EnhancedRollbackResult rollback_trade(const SimultaneousExecutionResult& failed_execution,
                                         RollbackTrigger trigger = RollbackTrigger::ORDER_FAILURE,
                                         RollbackSeverity severity = RollbackSeverity::MEDIUM);
    
    EnhancedRollbackResult rollback_partial_execution(const std::vector<OrderExecutionDetails>& executions,
                                                     RollbackTrigger trigger,
                                                     RollbackSeverity severity);
    
    // Strategy-specific rollback methods
    EnhancedRollbackResult execute_immediate_cancel(const std::vector<OrderExecutionDetails>& executions);
    EnhancedRollbackResult execute_market_close(const std::vector<OrderExecutionDetails>& executions);
    EnhancedRollbackResult execute_gradual_liquidation(const std::vector<OrderExecutionDetails>& executions);
    EnhancedRollbackResult execute_hedge_position(const std::vector<OrderExecutionDetails>& executions);
    EnhancedRollbackResult execute_smart_liquidation(const std::vector<OrderExecutionDetails>& executions);
    EnhancedRollbackResult execute_stop_loss_rollback(const std::vector<OrderExecutionDetails>& executions);
    EnhancedRollbackResult execute_partial_rollback(const std::vector<OrderExecutionDetails>& executions);
    
    // Risk assessment and strategy selection
    RollbackRiskAssessment assess_rollback_risk(const std::vector<OrderExecutionDetails>& executions);
    RollbackStrategy select_optimal_strategy(const std::vector<OrderExecutionDetails>& executions,
                                            RollbackTrigger trigger,
                                            const RollbackRiskAssessment& risk_assessment);
    
    // Monitoring and management
    std::vector<EnhancedRollbackResult> get_rollback_history(std::chrono::hours lookback = std::chrono::hours(24)) const;
    EnhancedRollbackResult get_rollback_status(const std::string& rollback_id) const;
    std::vector<EnhancedRollbackResult> get_active_rollbacks() const;
    
    // Performance metrics
    struct RollbackStatistics {
        std::atomic<size_t> total_rollbacks{0};
        std::atomic<size_t> successful_rollbacks{0};
        std::atomic<size_t> failed_rollbacks{0};
        std::atomic<double> average_rollback_time_ms{0.0};
        std::atomic<double> average_recovery_rate{0.0};
        std::atomic<double> total_rollback_cost{0.0};
        
        std::unordered_map<RollbackStrategy, size_t> strategy_usage_count;
        std::unordered_map<RollbackTrigger, size_t> trigger_count;
        
        std::chrono::system_clock::time_point last_rollback;
        std::atomic<std::chrono::milliseconds> uptime{std::chrono::milliseconds(0)};
    };
    
    RollbackStatistics get_rollback_statistics() const;
    void reset_rollback_statistics();
    
    // Configuration management
    void update_rollback_policy(const RollbackPolicy& policy);
    RollbackPolicy get_rollback_policy() const;
    void set_emergency_rollback_enabled(bool enabled);
    
    // Event handlers and callbacks
    using RollbackCompletedCallback = std::function<void(const EnhancedRollbackResult&)>;
    using RollbackProgressCallback = std::function<void(const std::string& rollback_id, double progress)>;
    using RollbackErrorCallback = std::function<void(const std::string& error)>;
    
    void set_rollback_completed_callback(RollbackCompletedCallback callback);
    void set_rollback_progress_callback(RollbackProgressCallback callback);
    void set_rollback_error_callback(RollbackErrorCallback callback);
    
    // Advanced features
    bool schedule_delayed_rollback(const std::string& trade_id, 
                                  std::chrono::milliseconds delay,
                                  RollbackStrategy strategy);
    
    bool cancel_scheduled_rollback(const std::string& trade_id);
    
    void enable_automatic_rollback_learning(bool enabled);
    void train_rollback_strategy_selector();
    
    // Health and diagnostics
    bool is_healthy() const;
    std::vector<std::string> get_health_issues() const;
    std::string get_status_report() const;

private:
    struct Implementation;
    std::unique_ptr<Implementation> impl_;
    
    // Strategy implementation helpers
    std::vector<types::Order> create_immediate_cancel_orders(const std::vector<OrderExecutionDetails>& executions);
    std::vector<types::Order> create_market_close_orders(const std::vector<OrderExecutionDetails>& executions);
    std::vector<types::Order> create_hedge_orders(const std::vector<OrderExecutionDetails>& executions);
    std::vector<types::Order> create_smart_liquidation_orders(const std::vector<OrderExecutionDetails>& executions,
                                                             const RollbackRiskAssessment& risk_assessment);
    
    // Execution helpers
    bool execute_rollback_orders(const std::vector<types::Order>& orders,
                                EnhancedRollbackResult& result);
    
    bool monitor_rollback_execution(const std::string& rollback_id,
                                  const std::vector<std::string>& order_ids,
                                  EnhancedRollbackResult& result);
    
    // Risk and market analysis
    double calculate_market_impact(const std::vector<OrderExecutionDetails>& executions);
    double estimate_rollback_slippage(const std::vector<types::Order>& rollback_orders);
    double calculate_optimal_liquidation_rate(const OrderExecutionDetails& execution);
    
    // Strategy selection ML/heuristics
    double score_rollback_strategy(RollbackStrategy strategy,
                                  const std::vector<OrderExecutionDetails>& executions,
                                  const RollbackRiskAssessment& risk_assessment);
    
    void learn_from_rollback_result(const EnhancedRollbackResult& result);
    
    // Utility methods
    std::string generate_rollback_id() const;
    RollbackSeverity determine_severity(const std::vector<OrderExecutionDetails>& executions,
                                       RollbackTrigger trigger);
    
    double calculate_total_exposure(const std::vector<OrderExecutionDetails>& executions);
    double calculate_recovery_rate(const EnhancedRollbackResult& result);
    
    void update_rollback_statistics(const EnhancedRollbackResult& result);
    void cleanup_old_rollback_history();
    
    // Error handling
    void handle_rollback_error(const std::string& rollback_id, const std::string& error);
    bool is_rollback_recoverable(const EnhancedRollbackResult& failed_result);
    EnhancedRollbackResult retry_failed_rollback(const EnhancedRollbackResult& failed_result);
};

// Rollback queue manager for handling multiple concurrent rollbacks
class RollbackQueueManager {
public:
    RollbackQueueManager();
    ~RollbackQueueManager();
    
    // Queue management
    void enqueue_rollback(const std::string& trade_id,
                         const std::vector<OrderExecutionDetails>& executions,
                         RollbackTrigger trigger,
                         RollbackSeverity severity);
    
    bool cancel_queued_rollback(const std::string& trade_id);
    size_t get_queue_size() const;
    std::vector<std::string> get_queued_trade_ids() const;
    
    // Processing control
    void start_processing();
    void stop_processing();
    void pause_processing();
    void resume_processing();
    
    // Priority management
    void set_rollback_priority(const std::string& trade_id, int priority);
    void promote_emergency_rollbacks();
    
    // Statistics
    size_t get_processed_count() const;
    double get_average_processing_time() const;
    
private:
    struct QueuedRollback {
        std::string trade_id;
        std::vector<OrderExecutionDetails> executions;
        RollbackTrigger trigger;
        RollbackSeverity severity;
        int priority;
        std::chrono::system_clock::time_point queued_at;
        
        bool operator<(const QueuedRollback& other) const {
            // Higher priority and critical severity processed first
            if (severity != other.severity) {
                return severity < other.severity; // CRITICAL < HIGH < MEDIUM < LOW
            }
            if (priority != other.priority) {
                return priority < other.priority; // Higher priority number = lower priority
            }
            return queued_at > other.queued_at; // Earlier queued items first
        }
    };
    
    std::priority_queue<QueuedRollback> rollback_queue_;
    std::unordered_map<std::string, QueuedRollback> trade_id_map_;
    
    std::atomic<bool> processing_enabled_{false};
    std::atomic<bool> processing_paused_{false};
    std::thread processing_thread_;
    
    std::mutex queue_mutex_;
    std::condition_variable queue_cv_;
    
    std::shared_ptr<EnhancedRollbackManager> rollback_manager_;
    
    void processing_loop();
    void process_next_rollback();
};

// Utility functions for rollback operations
namespace rollback_utils {
    
    // Strategy evaluation
    double calculate_strategy_effectiveness(RollbackStrategy strategy,
                                          const std::vector<EnhancedRollbackResult>& historical_results);
    
    std::vector<RollbackStrategy> rank_strategies_by_performance(
        const std::vector<EnhancedRollbackResult>& historical_results);
    
    // Risk calculations
    double calculate_rollback_urgency_score(const std::vector<OrderExecutionDetails>& executions,
                                           RollbackTrigger trigger);
    
    double estimate_market_impact_cost(const std::vector<types::Order>& rollback_orders);
    
    // Time calculations
    std::chrono::milliseconds estimate_rollback_time(RollbackStrategy strategy,
                                                    const std::vector<OrderExecutionDetails>& executions);
    
    std::chrono::milliseconds calculate_time_to_market_close();
    
    // Order analysis
    bool is_order_partially_filled(const OrderExecutionDetails& order);
    bool is_order_execution_stale(const OrderExecutionDetails& order,
                                 std::chrono::milliseconds staleness_threshold);
    
    double calculate_fill_ratio(const OrderExecutionDetails& order);
    double calculate_average_fill_price(const std::vector<OrderExecutionDetails>& orders);
    
    // Market condition analysis
    bool is_market_volatile(const std::string& symbol, double volatility_threshold = 0.05);
    bool has_sufficient_liquidity(const std::string& exchange, const std::string& symbol, double quantity);
    
    double calculate_bid_ask_spread(const std::string& exchange, const std::string& symbol);
    double calculate_order_book_imbalance(const std::string& exchange, const std::string& symbol);
    
    // Formatting and reporting
    std::string format_rollback_summary(const EnhancedRollbackResult& result);
    std::string format_rollback_statistics(const EnhancedRollbackManager::RollbackStatistics& stats);
    
    nlohmann::json rollback_result_to_json(const EnhancedRollbackResult& result);
    nlohmann::json rollback_statistics_to_json(const EnhancedRollbackManager::RollbackStatistics& stats);
    
    // Configuration helpers
    bool validate_rollback_policy(const RollbackPolicy& policy);
    std::vector<std::string> get_policy_validation_errors(const RollbackPolicy& policy);
    
    RollbackPolicy create_conservative_policy();
    RollbackPolicy create_aggressive_policy();
    RollbackPolicy create_balanced_policy();
}

} // namespace trading_engine
} // namespace ats