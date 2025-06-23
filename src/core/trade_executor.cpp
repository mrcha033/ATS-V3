#include "trade_executor.hpp"
#include "../utils/config_manager.hpp"
#include "../utils/logger.hpp"

namespace ats {

TradeExecutor::TradeExecutor(ConfigManager* config_manager, RiskManager* risk_manager)
    : config_manager_(config_manager), risk_manager_(risk_manager), running_(false),
      num_execution_threads_(2), max_history_size_(1000), trades_executed_(0),
      trades_successful_(0), trades_failed_(0), total_profit_(0.0), total_volume_(0.0) {
    
    start_time_ = std::chrono::system_clock::now();
}

TradeExecutor::~TradeExecutor() {
    Stop();
}

bool TradeExecutor::Initialize() {
    try {
        LOG_INFO("Initializing Trade Executor...");
        
        // Initialize thread pool
        execution_threads_.reserve(num_execution_threads_);
        
        LOG_INFO("Trade Executor initialized with {} execution threads", num_execution_threads_);
        return true;
        
    } catch (const std::exception& e) {
        LOG_ERROR("Failed to initialize Trade Executor: {}", e.what());
        return false;
    }
}

void TradeExecutor::Start() {
    if (running_.load()) {
        LOG_WARNING("Trade Executor is already running");
        return;
    }
    
    running_ = true;
    
    // Start execution threads
    for (size_t i = 0; i < num_execution_threads_; ++i) {
        execution_threads_.emplace_back(&TradeExecutor::ExecutionLoop, this);
    }
    
    // Start monitoring thread
    monitoring_thread_ = std::thread(&TradeExecutor::MonitoringLoop, this);
    
    LOG_INFO("Trade Executor started with {} threads", num_execution_threads_);
}

void TradeExecutor::Stop() {
    if (!running_.load()) {
        return;
    }
    
    running_ = false;
    trade_queue_cv_.notify_all();
    
    // Join execution threads
    for (auto& thread : execution_threads_) {
        if (thread.joinable()) {
            thread.join();
        }
    }
    execution_threads_.clear();
    
    // Join monitoring thread
    if (monitoring_thread_.joinable()) {
        monitoring_thread_.join();
    }
    
    LOG_INFO("Trade Executor stopped");
}

void TradeExecutor::AddExchange(const std::string& name, ExchangeInterface* exchange) {
    std::lock_guard<std::mutex> lock(exchanges_mutex_);
    exchanges_[name] = exchange;
    LOG_INFO("Added exchange: {}", name);
}

void TradeExecutor::RemoveExchange(const std::string& name) {
    std::lock_guard<std::mutex> lock(exchanges_mutex_);
    exchanges_.erase(name);
    LOG_INFO("Removed exchange: {}", name);
}

ExchangeInterface* TradeExecutor::GetExchange(const std::string& name) const {
    std::lock_guard<std::mutex> lock(exchanges_mutex_);
    auto it = exchanges_.find(name);
    return (it != exchanges_.end()) ? it->second : nullptr;
}

std::vector<std::string> TradeExecutor::GetAvailableExchanges() const {
    std::lock_guard<std::mutex> lock(exchanges_mutex_);
    std::vector<std::string> names;
    for (const auto& pair : exchanges_) {
        names.push_back(pair.first);
    }
    return names;
}

std::string TradeExecutor::ExecuteTrade(const ArbitrageOpportunity& opportunity, double volume) {
    auto plan = CreateExecutionPlan(opportunity, volume);
    return ExecuteTradeWithPlan(plan);
}

std::string TradeExecutor::ExecuteTradeWithPlan(const ExecutionPlan& plan) {
    if (!ValidateExecution(plan)) {
        LOG_ERROR("Execution plan validation failed for {}", plan.opportunity.symbol);
        return "";
    }
    
    std::string trade_id = GenerateTradeId();
    
    // Add to pending queue
    {
        std::lock_guard<std::mutex> lock(trade_queue_mutex_);
        ExecutionPlan plan_copy = plan;
        plan_copy.trade_id = trade_id;
        pending_trades_.push(plan_copy);
    }
    trade_queue_cv_.notify_one();
    
    LOG_INFO("Trade {} queued for execution", trade_id);
    return trade_id;
}

bool TradeExecutor::CancelTrade(const std::string& trade_id) {
    std::lock_guard<std::mutex> lock(active_trades_mutex_);
    
    auto it = active_trades_.find(trade_id);
    if (it != active_trades_.end()) {
        it->second.current_state = TradeState::CANCELLED;
        LOG_INFO("Trade {} cancelled", trade_id);
        return true;
    }
    
    return false;
}

ActiveTrade* TradeExecutor::GetActiveTrade(const std::string& trade_id) {
    std::lock_guard<std::mutex> lock(active_trades_mutex_);
    auto it = active_trades_.find(trade_id);
    return (it != active_trades_.end()) ? &it->second : nullptr;
}

std::vector<ActiveTrade> TradeExecutor::GetActiveTrades() const {
    std::lock_guard<std::mutex> lock(active_trades_mutex_);
    
    std::vector<ActiveTrade> trades;
    for (const auto& pair : active_trades_) {
        trades.push_back(pair.second);
    }
    return trades;
}

TradeState TradeExecutor::GetTradeState(const std::string& trade_id) const {
    std::lock_guard<std::mutex> lock(active_trades_mutex_);
    auto it = active_trades_.find(trade_id);
    return (it != active_trades_.end()) ? it->second.current_state : TradeState::FAILED;
}

bool TradeExecutor::IsTradeActive(const std::string& trade_id) const {
    auto state = GetTradeState(trade_id);
    return (state != TradeState::COMPLETED && 
            state != TradeState::FAILED && 
            state != TradeState::CANCELLED);
}

ExecutionPlan TradeExecutor::CreateExecutionPlan(const ArbitrageOpportunity& opportunity, double volume) {
    ExecutionPlan plan;
    plan.opportunity = opportunity;
    plan.execution_volume = volume;
    
    // Set up buy order
    plan.buy_order.exchange = opportunity.buy_exchange;
    plan.buy_order.symbol = opportunity.symbol;
    plan.buy_order.side = OrderSide::BUY;
    plan.buy_order.type = OrderType::MARKET;
    plan.buy_order.quantity = volume;
    plan.buy_order.price = opportunity.buy_price;
    
    // Set up sell order  
    plan.sell_order.exchange = opportunity.sell_exchange;
    plan.sell_order.symbol = opportunity.symbol;
    plan.sell_order.side = OrderSide::SELL;
    plan.sell_order.type = OrderType::MARKET;
    plan.sell_order.quantity = volume;
    plan.sell_order.price = opportunity.sell_price;
    
    // Configuration
    plan.use_market_orders = true;
    plan.enable_partial_fills = false;
    plan.max_slippage_percent = 0.5;
    plan.buy_timeout_seconds = 30.0;
    plan.sell_timeout_seconds = 30.0;
    plan.max_execution_time_seconds = 120.0;
    
    return plan;
}

bool TradeExecutor::IsHealthy() const {
    if (!running_.load()) {
        return false; // Not healthy if not running
    }
    
    // Check if exchanges are available
    {
        std::lock_guard<std::mutex> lock(exchanges_mutex_);
        if (exchanges_.empty()) {
            return false; // Not healthy without exchanges
        }
    }
    
    // Check if success rate is reasonable (if we have executed trades)
    if (trades_executed_.load() > 10 && GetSuccessRate() < 10.0) {
        return false; // Not healthy with very low success rate
    }
    
    return true; // Healthy if running, has exchanges, and reasonable performance
}

std::string TradeExecutor::GetStatus() const {
    if (!running_.load()) {
        return "STOPPED";
    }
    
    std::lock_guard<std::mutex> lock(active_trades_mutex_);
    return "EXECUTING (" + std::to_string(active_trades_.size()) + " active)";
}

double TradeExecutor::GetSuccessRate() const {
    long long total = trades_executed_.load();
    if (total == 0) return 0.0;
    return static_cast<double>(trades_successful_.load()) / total * 100.0;
}

double TradeExecutor::GetAvgExecutionTime() const {
    // TODO: Calculate from historical data
    return 0.0;
}

double TradeExecutor::GetAvgProfitPerTrade() const {
    long long total = trades_executed_.load();
    if (total == 0) return 0.0;
    return total_profit_.load() / total;
}

void TradeExecutor::LogStatistics() const {
    LOG_INFO("=== Trade Executor Statistics ===");
    LOG_INFO("Trades executed: {}", trades_executed_.load());
    LOG_INFO("Trades successful: {}", trades_successful_.load());
    LOG_INFO("Trades failed: {}", trades_failed_.load());
    LOG_INFO("Success rate: {:.1f}%", GetSuccessRate());
    LOG_INFO("Total profit: ${:.2f}", total_profit_.load());
    LOG_INFO("Total volume: ${:.2f}", total_volume_.load());
    LOG_INFO("Average profit per trade: ${:.2f}", GetAvgProfitPerTrade());
}

void TradeExecutor::ResetStatistics() {
    trades_executed_ = 0;
    trades_successful_ = 0;
    trades_failed_ = 0;
    total_profit_ = 0.0;
    total_volume_ = 0.0;
}

void TradeExecutor::ExecutionLoop() {
    LOG_INFO("Trade execution loop started");
    
    while (running_.load()) {
        try {
            ExecutionPlan plan;
            
            // Wait for trade to execute
            {
                std::unique_lock<std::mutex> lock(trade_queue_mutex_);
                trade_queue_cv_.wait(lock, [this] { 
                    return !pending_trades_.empty() || !running_.load(); 
                });
                
                if (!running_.load()) break;
                
                if (!pending_trades_.empty()) {
                    plan = pending_trades_.front();
                    pending_trades_.pop();
                } else {
                    continue;
                }
            }
            
            // Execute the trade
            auto result = ExecuteTradeInternal(plan);
            
            // Record result
            RecordExecutionResult(result);
            
            // Notify callback
            if (execution_callback_) {
                execution_callback_(result);
            }
            
        } catch (const std::exception& e) {
            LOG_ERROR("Error in execution loop: {}", e.what());
            std::this_thread::sleep_for(std::chrono::seconds(1));
        }
    }
    
    LOG_INFO("Trade execution loop stopped");
}

void TradeExecutor::MonitoringLoop() {
    LOG_INFO("Trade monitoring loop started");
    
    while (running_.load()) {
        try {
            // Monitor active trades for timeouts and risk conditions
            std::vector<std::string> trades_to_cleanup;
            
            {
                std::lock_guard<std::mutex> lock(active_trades_mutex_);
                auto now = std::chrono::system_clock::now();
                
                for (auto& pair : active_trades_) {
                    auto& trade = pair.second;
                    
                    // Check for timeout
                    auto elapsed = std::chrono::duration_cast<std::chrono::seconds>(
                        now - trade.start_time).count();
                    
                    if (elapsed > trade.plan.max_execution_time_seconds) {
                        trade.timeout_reached = true;
                        trade.current_state = TradeState::TIMEOUT;
                        trades_to_cleanup.push_back(pair.first);
                        continue;
                    }
                    
                    // Check risk conditions
                    if (CheckExecutionRisks(trade)) {
                        trades_to_cleanup.push_back(pair.first);
                    }
                }
            }
            
            // Cleanup completed/failed trades periodically (not per trade)
            static auto last_cleanup = std::chrono::system_clock::now();
            auto now_time = std::chrono::system_clock::now();
            if (now_time - last_cleanup > std::chrono::minutes(5)) { // Cleanup every 5 minutes
                CleanupCompletedTrades();
                last_cleanup = now_time;
            }
            
            std::this_thread::sleep_for(std::chrono::seconds(1));
            
        } catch (const std::exception& e) {
            LOG_ERROR("Error in monitoring loop: {}", e.what());
            std::this_thread::sleep_for(std::chrono::seconds(1));
        }
    }
    
    LOG_INFO("Trade monitoring loop stopped");
}

ExecutionResult TradeExecutor::ExecuteTradeInternal(const ExecutionPlan& plan) {
    ExecutionResult result;
    result.trade_id = plan.trade_id;
    result.final_state = TradeState::PENDING;
    
    auto start_time = std::chrono::steady_clock::now();
    
    try {
        // Create active trade record
        ActiveTrade active_trade;
        active_trade.plan = plan;
        active_trade.current_state = TradeState::PENDING;
        active_trade.start_time = std::chrono::system_clock::now();
        active_trade.last_update = active_trade.start_time;
        
        {
            std::lock_guard<std::mutex> lock(active_trades_mutex_);
            active_trades_[plan.trade_id] = active_trade;
        }
        
        // Update state
        UpdateTradeState(plan.trade_id, TradeState::BUYING);
        
        // Execute buy order
        bool buy_success = ExecuteBuyOrder(active_trades_[plan.trade_id]);
        if (!buy_success) {
            result.final_state = TradeState::FAILED;
            result.errors.push_back("Buy order failed");
            return result;
        }
        
        // Update state
        UpdateTradeState(plan.trade_id, TradeState::SELLING);
        
        // Execute sell order
        bool sell_success = ExecuteSellOrder(active_trades_[plan.trade_id]);
        if (!sell_success) {
            result.final_state = TradeState::FAILED;
            result.errors.push_back("Sell order failed");
            return result;
        }
        
        // Calculate results
        result.final_state = TradeState::COMPLETED;
        result.buy_order_result = active_trades_[plan.trade_id].plan.buy_order;
        result.sell_order_result = active_trades_[plan.trade_id].plan.sell_order;
        
        result.gross_profit = (result.sell_order_result.avg_fill_price - 
                              result.buy_order_result.avg_fill_price) * 
                              result.buy_order_result.filled_quantity;
        
        result.total_fees = CalculateFees(result.buy_order_result, nullptr) +
                           CalculateFees(result.sell_order_result, nullptr);
        
        result.net_profit = result.gross_profit - result.total_fees;
        result.realized_pnl = result.net_profit;
        
        if (result.buy_order_result.avg_fill_price > 0) {
            result.actual_profit_percent = (result.net_profit / 
                (result.buy_order_result.avg_fill_price * result.buy_order_result.filled_quantity)) * 100.0;
        }
        
        LOG_INFO("Trade {} completed successfully: profit=${:.2f}", 
                plan.trade_id, result.realized_pnl);
        
    } catch (const std::exception& e) {
        result.final_state = TradeState::FAILED;
        result.errors.push_back(e.what());
        LOG_ERROR("Trade execution failed: {}", e.what());
    }
    
    auto end_time = std::chrono::steady_clock::now();
    result.total_execution_time_ms = static_cast<double>(std::chrono::duration_cast<std::chrono::milliseconds>(
        end_time - start_time).count());
    
    return result;
}

bool TradeExecutor::ExecuteBuyOrder(ActiveTrade& trade) {
    // Simplified buy order execution
    auto exchange = GetExchange(trade.plan.buy_order.exchange);
    if (!exchange) {
        LOG_ERROR("Exchange {} not found for buy order", trade.plan.buy_order.exchange);
        return false;
    }
    
    // Simulate order execution (replace with actual exchange API calls)
    trade.plan.buy_order.order_id = GenerateOrderId();
    trade.plan.buy_order.status = OrderStatus::FILLED;
    trade.plan.buy_order.filled_quantity = trade.plan.buy_order.quantity;
    trade.plan.buy_order.avg_fill_price = trade.plan.buy_order.price;
    trade.plan.buy_order.filled_time = std::chrono::duration_cast<std::chrono::milliseconds>(
        std::chrono::system_clock::now().time_since_epoch()).count();
    
    trade.buy_completed = true;
    trade.buy_fill_percentage = 100.0;
    
    LOG_DEBUG("Buy order completed for trade {}", trade.plan.trade_id);
    return true;
}

bool TradeExecutor::ExecuteSellOrder(ActiveTrade& trade) {
    // Simplified sell order execution
    auto exchange = GetExchange(trade.plan.sell_order.exchange);
    if (!exchange) {
        LOG_ERROR("Exchange {} not found for sell order", trade.plan.sell_order.exchange);
        return false;
    }
    
    // Simulate order execution (replace with actual exchange API calls)
    trade.plan.sell_order.order_id = GenerateOrderId();
    trade.plan.sell_order.status = OrderStatus::FILLED;
    trade.plan.sell_order.filled_quantity = trade.plan.sell_order.quantity;
    trade.plan.sell_order.avg_fill_price = trade.plan.sell_order.price;
    trade.plan.sell_order.filled_time = std::chrono::duration_cast<std::chrono::milliseconds>(
        std::chrono::system_clock::now().time_since_epoch()).count();
    
    trade.sell_completed = true;
    trade.sell_fill_percentage = 100.0;
    
    LOG_DEBUG("Sell order completed for trade {}", trade.plan.trade_id);
    return true;
}

bool TradeExecutor::ValidateExecution(const ExecutionPlan& plan) {
    // Check if required exchanges are available
    if (!GetExchange(plan.buy_order.exchange)) {
        LOG_ERROR("Buy exchange {} not available", plan.buy_order.exchange);
        return false;
    }
    
    if (!GetExchange(plan.sell_order.exchange)) {
        LOG_ERROR("Sell exchange {} not available", plan.sell_order.exchange);
        return false;
    }
    
    // Check volume constraints
    if (plan.execution_volume <= 0) {
        LOG_ERROR("Invalid execution volume: {}", plan.execution_volume);
        return false;
    }
    
    // Additional validation can be added here
    return true;
}

bool TradeExecutor::CheckExecutionRisks(const ActiveTrade& trade) {
    // Check if risk manager signals to abort
    if (risk_manager_ && risk_manager_->IsKillSwitchActive()) {
        LOG_WARNING("Kill switch active - aborting trade {}", trade.plan.trade_id);
        return true;
    }
    
    // Check for stop loss conditions
    if (trade.current_state == TradeState::BUYING || trade.current_state == TradeState::SELLING) {
        // Check if the opportunity is still profitable
        double current_profit_percent = 0.0;
        
        // Simplified profit check - in production, get real-time prices
        if (trade.plan.opportunity.buy_price > 0) {
            current_profit_percent = ((trade.plan.opportunity.sell_price - trade.plan.opportunity.buy_price) 
                                    / trade.plan.opportunity.buy_price) * 100.0;
        }
        
        // Stop loss threshold: abort if profit dropped below -2%
        if (current_profit_percent < -2.0) {
            LOG_WARNING("Stop loss triggered for trade {} - profit: {:.2f}%", 
                       trade.plan.trade_id, current_profit_percent);
            return true;
        }
        
        // Also check if profit dropped significantly from original estimate
        double profit_degradation = trade.plan.opportunity.profit_percent - current_profit_percent;
        if (profit_degradation > 5.0) { // 5% degradation threshold
            LOG_WARNING("Profit degradation detected for trade {} - degradation: {:.2f}%",
                       trade.plan.trade_id, profit_degradation);
            return true;
        }
    }
    
    return false;
}

void TradeExecutor::UpdateTradeState(const std::string& trade_id, TradeState new_state) {
    {
        std::lock_guard<std::mutex> lock(active_trades_mutex_);
        auto it = active_trades_.find(trade_id);
        if (it != active_trades_.end()) {
            it->second.current_state = new_state;
            it->second.last_update = std::chrono::system_clock::now();
        }
    }
    
    if (state_callback_) {
        state_callback_(trade_id, new_state);
    }
}

void TradeExecutor::RecordExecutionResult(const ExecutionResult& result) {
    {
        std::lock_guard<std::mutex> lock(history_mutex_);
        execution_history_.push_back(result);
        
        // Keep only recent history
        if (execution_history_.size() > max_history_size_) {
            execution_history_.erase(execution_history_.begin());
        }
    }
    
    // Update statistics
    UpdatePerformanceMetrics(result);
    
    // Remove from active trades
    {
        std::lock_guard<std::mutex> lock(active_trades_mutex_);
        active_trades_.erase(result.trade_id);
    }
}

void TradeExecutor::UpdatePerformanceMetrics(const ExecutionResult& result) {
    trades_executed_++;
    
    if (result.final_state == TradeState::COMPLETED) {
        trades_successful_++;
        total_profit_ += result.realized_pnl;
    } else {
        trades_failed_++;
    }
    
    total_volume_ += result.buy_order_result.filled_quantity * result.buy_order_result.avg_fill_price;
}

std::string TradeExecutor::GenerateTradeId() {
    static std::atomic<long long> counter{0};
    auto now = std::chrono::system_clock::now();
    auto timestamp = std::chrono::duration_cast<std::chrono::milliseconds>(now.time_since_epoch()).count();
    
    return "TRADE_" + std::to_string(timestamp) + "_" + std::to_string(counter++);
}

std::string TradeExecutor::GenerateOrderId() {
    static std::atomic<long long> counter{0};
    auto now = std::chrono::system_clock::now();
    auto timestamp = std::chrono::duration_cast<std::chrono::milliseconds>(now.time_since_epoch()).count();
    
    return "ORDER_" + std::to_string(timestamp) + "_" + std::to_string(counter++);
}

double TradeExecutor::CalculateFees(const Order& order, ExchangeInterface* exchange) {
    // Simplified fee calculation
    return order.filled_quantity * order.avg_fill_price * 0.001; // 0.1% fee
}

void TradeExecutor::CleanupCompletedTrades() {
    auto now = std::chrono::system_clock::now();
    auto cleanup_threshold = now - std::chrono::hours(1); // Keep completed trades for 1 hour
    
    std::lock_guard<std::mutex> lock(active_trades_mutex_);
    
    auto it = active_trades_.begin();
    while (it != active_trades_.end()) {
        const auto& trade = it->second;
        
        // Check if trade is completed and old enough to cleanup
        bool should_cleanup = false;
        
        if (trade.current_state == TradeState::COMPLETED || 
            trade.current_state == TradeState::FAILED || 
            trade.current_state == TradeState::CANCELLED) {
            
            if (trade.last_update < cleanup_threshold) {
                should_cleanup = true;
            }
        }
        
        // Also cleanup trades that have been stuck for too long
        auto trade_age = now - trade.start_time;
        if (trade_age > std::chrono::hours(6)) { // 6 hour timeout
            should_cleanup = true;
            LOG_WARNING("Cleaning up stuck trade: {} (age: {} hours)", 
                       it->first, 
                       std::chrono::duration_cast<std::chrono::hours>(trade_age).count());
        }
        
        if (should_cleanup) {
            LOG_DEBUG("Cleaning up completed trade: {}", it->first);
            it = active_trades_.erase(it);
        } else {
            ++it;
        }
    }
}

std::vector<ExecutionResult> TradeExecutor::GetExecutionHistory(size_t count) const {
    std::lock_guard<std::mutex> lock(history_mutex_);
    
    std::vector<ExecutionResult> result;
    size_t start = execution_history_.size() > count ? execution_history_.size() - count : 0;
    
    for (size_t i = start; i < execution_history_.size(); ++i) {
        result.push_back(execution_history_[i]);
    }
    
    return result;
}

} // namespace ats 
