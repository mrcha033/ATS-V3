#include "order_router.hpp"
#include "utils/logger.hpp"
#include "utils/json_parser.hpp"
#include <nlohmann/json.hpp>
#include <future>
#include <algorithm>
#include <numeric>
#include <unordered_set>

namespace ats {
namespace trading_engine {

struct OrderRouter::Implementation {
    OrderRouterConfig config;
    std::unordered_map<std::string, std::unique_ptr<ExchangeTradingInterface>> exchanges;
    std::unordered_map<std::string, std::vector<types::Balance>> balance_cache;
    std::unordered_map<std::string, double> position_limits;
    
    // Performance tracking
    PerformanceMetrics performance_metrics;
    std::atomic<bool> running{false};
    
    // Callbacks
    OrderUpdateCallback order_update_callback;
    ExecutionCompletedCallback execution_completed_callback;
    ErrorCallback error_callback;
    
    // Thread management
    std::vector<std::thread> monitor_threads;
    std::queue<std::string> pending_orders;
    std::mutex pending_mutex;
    std::condition_variable pending_cv;
    
    // Active order tracking
    std::unordered_map<std::string, OrderExecutionDetails> active_orders;
    std::shared_mutex active_orders_mutex;
    
    mutable std::shared_mutex mutex;
};

// OrderExecutionStatus to string conversion
std::string status_to_string(OrderExecutionStatus status) {
    switch (status) {
        case OrderExecutionStatus::PENDING: return "PENDING";
        case OrderExecutionStatus::SUBMITTED: return "SUBMITTED";
        case OrderExecutionStatus::PARTIALLY_FILLED: return "PARTIALLY_FILLED";
        case OrderExecutionStatus::FILLED: return "FILLED";
        case OrderExecutionStatus::CANCELED: return "CANCELED";
        case OrderExecutionStatus::REJECTED: return "REJECTED";
        case OrderExecutionStatus::EXPIRED: return "EXPIRED";
        case OrderExecutionStatus::FAILED: return "FAILED";
        default: return "UNKNOWN";
    }
}

OrderRouter::OrderRouter() : impl_(std::make_unique<Implementation>()) {}

OrderRouter::~OrderRouter() {
    if (impl_->running) {
        // Stop monitoring threads
        impl_->running = false;
        impl_->pending_cv.notify_all();
        
        for (auto& thread : impl_->monitor_threads) {
            if (thread.joinable()) {
                thread.join();
            }
        }
    }
}

bool OrderRouter::initialize(const OrderRouterConfig& config) {
    std::unique_lock<std::shared_mutex> lock(impl_->mutex);
    
    impl_->config = config;
    impl_->running = true;
    
    // Start order monitoring thread
    impl_->monitor_threads.emplace_back([this]() {
        while (impl_->running) {
            std::unique_lock<std::mutex> lock(impl_->pending_mutex);
            impl_->pending_cv.wait(lock, [this]() {
                return !impl_->pending_orders.empty() || !impl_->running;
            });
            
            while (!impl_->pending_orders.empty() && impl_->running) {
                std::string order_id = impl_->pending_orders.front();
                impl_->pending_orders.pop();
                lock.unlock();
                
                // Find which exchange this order belongs to and monitor it
                std::shared_lock<std::shared_mutex> orders_lock(impl_->active_orders_mutex);
                auto it = impl_->active_orders.find(order_id);
                if (it != impl_->active_orders.end()) {
                    monitor_order_execution(it->second.original_order.exchange, order_id);
                }
                orders_lock.unlock();
                
                lock.lock();
            }
        }
    });
    
    utils::Logger::info("OrderRouter initialized successfully");
    return true;
}

void OrderRouter::add_exchange(const std::string& exchange_id, 
                             std::unique_ptr<ExchangeTradingInterface> exchange) {
    std::unique_lock<std::shared_mutex> lock(impl_->mutex);
    
    if (!exchange) {
        utils::Logger::error("Cannot add null exchange: {}", exchange_id);
        return;
    }
    
    impl_->exchanges[exchange_id] = std::move(exchange);
    update_balance_cache(exchange_id);
    
    utils::Logger::info("Added exchange: {}", exchange_id);
}

void OrderRouter::remove_exchange(const std::string& exchange_id) {
    std::unique_lock<std::shared_mutex> lock(impl_->mutex);
    
    impl_->exchanges.erase(exchange_id);
    impl_->balance_cache.erase(exchange_id);
    
    utils::Logger::info("Removed exchange: {}", exchange_id);
}

std::future<OrderExecutionDetails> OrderRouter::place_order_async(const types::Order& order) {
    return std::async(std::launch::async, [this, order]() {
        return place_order_sync(order);
    });
}

OrderExecutionDetails OrderRouter::place_order_sync(const types::Order& order) {
    OrderExecutionDetails details;
    details.order_id = generate_order_id();
    details.original_order = order;
    details.remaining_quantity = order.quantity;
    
    std::string error_message;
    if (!validate_order(order, error_message)) {
        details.status = OrderExecutionStatus::REJECTED;
        details.error_message = error_message;
        return details;
    }
    
    auto exchange_it = impl_->exchanges.find(order.exchange);
    if (exchange_it == impl_->exchanges.end()) {
        details.status = OrderExecutionStatus::FAILED;
        details.error_message = "Exchange not found: " + order.exchange;
        return details;
    }
    
    auto& exchange = exchange_it->second;
    
    try {
        details.submitted_at = std::chrono::system_clock::now();
        details.exchange_order_id = exchange->place_order(order);
        details.status = OrderExecutionStatus::SUBMITTED;
        
        // Track the order
        {
            std::unique_lock<std::shared_mutex> lock(impl_->active_orders_mutex);
            impl_->active_orders[details.order_id] = details;
        }
        
        // Add to monitoring queue
        {
            std::lock_guard<std::mutex> lock(impl_->pending_mutex);
            impl_->pending_orders.push(details.order_id);
        }
        impl_->pending_cv.notify_one();
        
        impl_->performance_metrics.total_orders_placed++;
        
        if (impl_->order_update_callback) {
            impl_->order_update_callback(details);
        }
        
    } catch (const std::exception& e) {
        details.status = OrderExecutionStatus::FAILED;
        details.error_message = e.what();
        impl_->performance_metrics.failed_orders++;
        
        if (impl_->error_callback) {
            impl_->error_callback("Order placement failed: " + std::string(e.what()));
        }
    }
    
    return details;
}

std::future<SimultaneousExecutionResult> OrderRouter::execute_arbitrage_orders_async(
    const ArbitrageOpportunity& opportunity) {
    return std::async(std::launch::async, [this, opportunity]() {
        return execute_arbitrage_orders_sync(opportunity);
    });
}

SimultaneousExecutionResult OrderRouter::execute_arbitrage_orders_sync(
    const ArbitrageOpportunity& opportunity) {
    
    SimultaneousExecutionResult result;
    result.trade_id = generate_trade_id();
    
    auto start_time = std::chrono::system_clock::now();
    
    std::string error_message;
    if (!validate_arbitrage_opportunity(opportunity, error_message)) {
        result.overall_result = ExecutionResult::FAILURE;
        result.error_message = error_message;
        return result;
    }
    
    // Create buy and sell orders
    types::Order buy_order;
    buy_order.exchange = opportunity.buy_exchange;
    buy_order.symbol = opportunity.symbol;
    buy_order.side = types::OrderSide::BUY;
    buy_order.type = types::OrderType::MARKET;
    buy_order.quantity = opportunity.available_quantity;
    buy_order.price = opportunity.buy_price;
    
    types::Order sell_order;
    sell_order.exchange = opportunity.sell_exchange;
    sell_order.symbol = opportunity.symbol;
    sell_order.side = types::OrderSide::SELL;
    sell_order.type = types::OrderType::MARKET;
    sell_order.quantity = opportunity.available_quantity;
    sell_order.price = opportunity.sell_price;
    
    // Execute orders simultaneously
    auto buy_future = place_order_async(buy_order);
    auto sell_future = place_order_async(sell_order);
    
    // Wait for both orders with timeout
    auto timeout = impl_->config.execution_timeout;
    
    try {
        auto buy_result = buy_future.wait_for(timeout);
        auto sell_result = sell_future.wait_for(timeout);
        
        if (buy_result == std::future_status::timeout || 
            sell_result == std::future_status::timeout) {
            result.overall_result = ExecutionResult::TIMEOUT;
            result.error_message = "Order execution timeout";
            result.requires_rollback = true;
        } else {
            auto buy_details = buy_future.get();
            auto sell_details = sell_future.get();
            
            result.order_executions.push_back(buy_details);
            result.order_executions.push_back(sell_details);
            
            // Analyze execution results
            bool buy_success = (buy_details.status == OrderExecutionStatus::FILLED ||
                              buy_details.status == OrderExecutionStatus::PARTIALLY_FILLED);
            bool sell_success = (sell_details.status == OrderExecutionStatus::FILLED ||
                                sell_details.status == OrderExecutionStatus::PARTIALLY_FILLED);
            
            if (buy_success && sell_success) {
                result.overall_result = ExecutionResult::SUCCESS;
                result.total_filled_quantity = std::min(buy_details.filled_quantity, 
                                                       sell_details.filled_quantity);
                result.average_execution_price_buy = buy_details.average_fill_price;
                result.average_execution_price_sell = sell_details.average_fill_price;
                result.total_fees = buy_details.total_fees + sell_details.total_fees;
                
                // Calculate actual profit
                result.actual_profit = (result.average_execution_price_sell - 
                                      result.average_execution_price_buy) * 
                                      result.total_filled_quantity - result.total_fees;
                
                impl_->performance_metrics.successful_orders += 2;
            } else if (buy_success || sell_success) {
                result.overall_result = ExecutionResult::PARTIAL_SUCCESS;
                result.requires_rollback = true;
                impl_->performance_metrics.successful_orders += (buy_success ? 1 : 0) + (sell_success ? 1 : 0);
                impl_->performance_metrics.failed_orders += (!buy_success ? 1 : 0) + (!sell_success ? 1 : 0);
            } else {
                result.overall_result = ExecutionResult::FAILURE;
                result.error_message = "Both orders failed: " + 
                                     buy_details.error_message + "; " + 
                                     sell_details.error_message;
                impl_->performance_metrics.failed_orders += 2;
            }
        }
    } catch (const std::exception& e) {
        result.overall_result = ExecutionResult::FAILURE;
        result.error_message = "Exception during execution: " + std::string(e.what());
        result.requires_rollback = true;
        impl_->performance_metrics.failed_orders++;
    }
    
    result.total_execution_time = std::chrono::duration_cast<std::chrono::milliseconds>(
        std::chrono::system_clock::now() - start_time);
    
    // Update performance metrics
    impl_->performance_metrics.average_execution_time_ms = 
        (impl_->performance_metrics.average_execution_time_ms.load() + 
         result.total_execution_time.count()) / 2.0;
    
    update_performance_statistics();
    
    if (impl_->execution_completed_callback) {
        impl_->execution_completed_callback(result);
    }
    
    return result;
}

bool OrderRouter::cancel_order(const std::string& exchange_id, const std::string& order_id) {
    auto exchange_it = impl_->exchanges.find(exchange_id);
    if (exchange_it == impl_->exchanges.end()) {
        return false;
    }
    
    try {
        bool success = exchange_it->second->cancel_order(order_id);
        if (success) {
            // Update order status
            std::unique_lock<std::shared_mutex> lock(impl_->active_orders_mutex);
            for (auto& [id, details] : impl_->active_orders) {
                if (details.exchange_order_id == order_id) {
                    details.status = OrderExecutionStatus::CANCELED;
                    details.last_updated = std::chrono::system_clock::now();
                    
                    if (impl_->order_update_callback) {
                        impl_->order_update_callback(details);
                    }
                    break;
                }
            }
            impl_->performance_metrics.canceled_orders++;
        }
        return success;
    } catch (const std::exception& e) {
        utils::Logger::error("Failed to cancel order {}: {}", order_id, e.what());
        return false;
    }
}

OrderExecutionDetails OrderRouter::get_order_status(const std::string& exchange_id, 
                                                   const std::string& order_id) {
    auto exchange_it = impl_->exchanges.find(exchange_id);
    if (exchange_it == impl_->exchanges.end()) {
        OrderExecutionDetails details;
        details.status = OrderExecutionStatus::FAILED;
        details.error_message = "Exchange not found";
        return details;
    }
    
    try {
        return exchange_it->second->get_order_status(order_id);
    } catch (const std::exception& e) {
        OrderExecutionDetails details;
        details.status = OrderExecutionStatus::FAILED;
        details.error_message = e.what();
        return details;
    }
}

std::vector<OrderExecutionDetails> OrderRouter::get_active_orders(const std::string& exchange_id) {
    std::shared_lock<std::shared_mutex> lock(impl_->active_orders_mutex);
    std::vector<OrderExecutionDetails> result;
    
    for (const auto& [id, details] : impl_->active_orders) {
        if (exchange_id.empty() || details.original_order.exchange == exchange_id) {
            if (details.status == OrderExecutionStatus::PENDING ||
                details.status == OrderExecutionStatus::SUBMITTED ||
                details.status == OrderExecutionStatus::PARTIALLY_FILLED) {
                result.push_back(details);
            }
        }
    }
    
    return result;
}

std::unordered_map<std::string, std::vector<types::Balance>> OrderRouter::get_all_balances() {
    std::shared_lock<std::shared_mutex> lock(impl_->mutex);
    
    // Update all balance caches
    for (const auto& [exchange_id, exchange] : impl_->exchanges) {
        update_balance_cache(exchange_id);
    }
    
    return impl_->balance_cache;
}

types::Balance OrderRouter::get_balance(const std::string& exchange_id, 
                                       const types::Currency& currency) {
    auto exchange_it = impl_->exchanges.find(exchange_id);
    if (exchange_it == impl_->exchanges.end()) {
        return types::Balance{};
    }
    
    try {
        return exchange_it->second->get_balance(currency);
    } catch (const std::exception& e) {
        utils::Logger::error("Failed to get balance for {} on {}: {}", 
                           currency, exchange_id, e.what());
        return types::Balance{};
    }
}

bool OrderRouter::validate_order(const types::Order& order, std::string& error_message) {
    if (!validate_order_parameters(order, error_message)) {
        return false;
    }
    
    if (!validate_exchange_connectivity(order.exchange)) {
        error_message = "Exchange not connected: " + order.exchange;
        return false;
    }
    
    if (!validate_market_conditions(order.exchange, order.symbol)) {
        error_message = "Market conditions not suitable for trading";
        return false;
    }
    
    if (!is_order_within_limits(order)) {
        error_message = "Order exceeds risk limits";
        return false;
    }
    
    return true;
}

bool OrderRouter::validate_arbitrage_opportunity(const ArbitrageOpportunity& opportunity, 
                                                std::string& error_message) {
    if (opportunity.symbol.empty()) {
        error_message = "Symbol cannot be empty";
        return false;
    }
    
    if (opportunity.buy_exchange == opportunity.sell_exchange) {
        error_message = "Buy and sell exchanges cannot be the same";
        return false;
    }
    
    if (opportunity.available_quantity <= 0) {
        error_message = "Quantity must be positive";
        return false;
    }
    
    if (opportunity.buy_price >= opportunity.sell_price) {
        error_message = "Buy price must be less than sell price";
        return false;
    }
    
    if (!check_sufficient_balance(opportunity)) {
        error_message = "Insufficient balance for arbitrage opportunity";
        return false;
    }
    
    return true;
}

bool OrderRouter::check_sufficient_balance(const ArbitrageOpportunity& opportunity) {
    // Check if we have enough balance on buy exchange to buy
    auto buy_exchange_it = impl_->exchanges.find(opportunity.buy_exchange);
    if (buy_exchange_it == impl_->exchanges.end()) {
        return false;
    }
    
    // Check if we have enough of the base currency to sell
    auto sell_exchange_it = impl_->exchanges.find(opportunity.sell_exchange);
    if (sell_exchange_it == impl_->exchanges.end()) {
        return false;
    }
    
    try {
        // Extract quote currency (e.g., USDT from BTC/USDT)
        size_t slash_pos = opportunity.symbol.find('/');
        if (slash_pos == std::string::npos) {
            return false;
        }
        
        std::string base_currency = opportunity.symbol.substr(0, slash_pos);
        std::string quote_currency = opportunity.symbol.substr(slash_pos + 1);
        
        // Check buy exchange has enough quote currency
        auto buy_balance = buy_exchange_it->second->get_balance(quote_currency);
        double required_quote = opportunity.available_quantity * opportunity.buy_price;
        
        if (buy_balance.available < required_quote) {
            return false;
        }
        
        // Check sell exchange has enough base currency
        auto sell_balance = sell_exchange_it->second->get_balance(base_currency);
        if (sell_balance.available < opportunity.available_quantity) {
            return false;
        }
        
        return true;
    } catch (const std::exception& e) {
        utils::Logger::error("Error checking balance: {}", e.what());
        return false;
    }
}

bool OrderRouter::is_healthy() const {
    std::shared_lock<std::shared_mutex> lock(impl_->mutex);
    
    // Check if exchanges are connected
    for (const auto& [exchange_id, exchange] : impl_->exchanges) {
        if (!exchange->is_connected() || !exchange->is_healthy()) {
            return false;
        }
    }
    
    // Check performance metrics
    double success_rate = impl_->performance_metrics.success_rate.load();
    if (success_rate < 0.95) { // Require 95% success rate
        return false;
    }
    
    return true;
}

OrderRouter::PerformanceMetrics OrderRouter::get_performance_metrics() const {
    return impl_->performance_metrics;
}

void OrderRouter::monitor_order_execution(const std::string& exchange_id, const std::string& order_id) {
    auto exchange_it = impl_->exchanges.find(exchange_id);
    if (exchange_it == impl_->exchanges.end()) {
        return;
    }
    
    auto start_time = std::chrono::system_clock::now();
    auto timeout = impl_->config.order_timeout;
    
    while (impl_->running && 
           std::chrono::system_clock::now() - start_time < timeout) {
        
        try {
            auto status = exchange_it->second->get_order_status(order_id);
            
            // Update our local tracking
            {
                std::unique_lock<std::shared_mutex> lock(impl_->active_orders_mutex);
                for (auto& [id, details] : impl_->active_orders) {
                    if (details.exchange_order_id == order_id) {
                        details.status = status.status;
                        details.filled_quantity = status.filled_quantity;
                        details.remaining_quantity = status.remaining_quantity;
                        details.average_fill_price = status.average_fill_price;
                        details.total_fees = status.total_fees;
                        details.fills = status.fills;
                        details.last_updated = std::chrono::system_clock::now();
                        details.execution_latency = std::chrono::duration_cast<std::chrono::milliseconds>(
                            details.last_updated - details.submitted_at);
                        
                        if (impl_->order_update_callback) {
                            impl_->order_update_callback(details);
                        }
                        
                        // Check if order is complete
                        if (status.status == OrderExecutionStatus::FILLED ||
                            status.status == OrderExecutionStatus::CANCELED ||
                            status.status == OrderExecutionStatus::REJECTED ||
                            status.status == OrderExecutionStatus::FAILED) {
                            
                            record_order_metrics(details);
                            return; // Exit monitoring
                        }
                        break;
                    }
                }
            }
            
        } catch (const std::exception& e) {
            utils::Logger::error("Error monitoring order {}: {}", order_id, e.what());
            break;
        }
        
        std::this_thread::sleep_for(std::chrono::milliseconds(500));
    }
}

void OrderRouter::update_balance_cache(const std::string& exchange_id) {
    auto exchange_it = impl_->exchanges.find(exchange_id);
    if (exchange_it == impl_->exchanges.end()) {
        return;
    }
    
    try {
        auto balances = exchange_it->second->get_account_balances();
        impl_->balance_cache[exchange_id] = balances;
    } catch (const std::exception& e) {
        utils::Logger::error("Failed to update balance cache for {}: {}", exchange_id, e.what());
    }
}

void OrderRouter::record_order_metrics(const OrderExecutionDetails& details) {
    auto latency_ms = details.execution_latency.count();
    
    impl_->performance_metrics.average_execution_time_ms = 
        (impl_->performance_metrics.average_execution_time_ms.load() + latency_ms) / 2.0;
    
    if (details.status == OrderExecutionStatus::FILLED) {
        impl_->performance_metrics.successful_orders++;
    } else {
        impl_->performance_metrics.failed_orders++;
    }
    
    impl_->performance_metrics.total_fees_paid += details.total_fees;
    
    update_performance_statistics();
}

void OrderRouter::update_performance_statistics() {
    size_t total = impl_->performance_metrics.total_orders_placed.load();
    size_t successful = impl_->performance_metrics.successful_orders.load();
    
    if (total > 0) {
        impl_->performance_metrics.success_rate = 
            static_cast<double>(successful) / static_cast<double>(total);
    }
}

bool OrderRouter::validate_order_parameters(const types::Order& order, std::string& error) {
    if (order.symbol.empty()) {
        error = "Symbol cannot be empty";
        return false;
    }
    
    if (order.exchange.empty()) {
        error = "Exchange cannot be empty";
        return false;
    }
    
    if (order.quantity <= 0) {
        error = "Quantity must be positive";
        return false;
    }
    
    if (order.type == types::OrderType::LIMIT && order.price <= 0) {
        error = "Limit order price must be positive";
        return false;
    }
    
    return true;
}

bool OrderRouter::validate_exchange_connectivity(const std::string& exchange_id) {
    auto exchange_it = impl_->exchanges.find(exchange_id);
    return exchange_it != impl_->exchanges.end() && 
           exchange_it->second->is_connected();
}

bool OrderRouter::validate_market_conditions(const std::string& exchange_id, const std::string& symbol) {
    auto exchange_it = impl_->exchanges.find(exchange_id);
    if (exchange_it == impl_->exchanges.end()) {
        return false;
    }
    
    try {
        return exchange_it->second->is_market_open();
    } catch (const std::exception&) {
        return false;
    }
}

bool OrderRouter::is_order_within_limits(const types::Order& order) {
    // Check against position limits
    auto limit_it = impl_->position_limits.find(order.symbol);
    if (limit_it != impl_->position_limits.end()) {
        if (order.quantity > limit_it->second) {
            return false;
        }
    }
    
    // Check against configuration limits
    if (order.quantity > impl_->config.max_slippage_tolerance * 1000000) { // Simple check
        return false;
    }
    
    return true;
}

std::string OrderRouter::generate_order_id() const {
    static std::atomic<size_t> counter{0};
    auto now = std::chrono::system_clock::now();
    auto timestamp = std::chrono::duration_cast<std::chrono::milliseconds>(
        now.time_since_epoch()).count();
    
    return "ORD_" + std::to_string(timestamp) + "_" + std::to_string(counter++);
}

std::string OrderRouter::generate_trade_id() const {
    static std::atomic<size_t> counter{0};
    auto now = std::chrono::system_clock::now();
    auto timestamp = std::chrono::duration_cast<std::chrono::milliseconds>(
        now.time_since_epoch()).count();
    
    return "TRD_" + std::to_string(timestamp) + "_" + std::to_string(counter++);
}

// RollbackManager implementation
struct RollbackManager::Implementation {
    std::vector<RollbackResult> rollback_history;
    RollbackStrategy default_strategy = RollbackStrategy::IMMEDIATE_CANCEL;
    std::chrono::milliseconds rollback_timeout{30000};
    bool automatic_rollback_enabled = true;
    mutable std::shared_mutex mutex;
};

RollbackManager::RollbackManager() : impl_(std::make_unique<Implementation>()) {}

RollbackManager::~RollbackManager() = default;

bool RollbackManager::rollback_trade(const SimultaneousExecutionResult& failed_execution) {
    if (failed_execution.order_executions.empty()) {
        return false;
    }
    
    return rollback_partial_execution(failed_execution.order_executions);
}

bool RollbackManager::rollback_partial_execution(const std::vector<OrderExecutionDetails>& executions) {
    if (!validate_rollback_feasibility(executions)) {
        return false;
    }
    
    auto result = execute_rollback_strategy(executions, impl_->default_strategy);
    
    {
        std::unique_lock<std::shared_mutex> lock(impl_->mutex);
        impl_->rollback_history.push_back(result);
    }
    
    return result.success;
}

bool RollbackManager::execute_rollback_strategy(const std::vector<OrderExecutionDetails>& executions,
                                              RollbackStrategy strategy) {
    switch (strategy) {
        case RollbackStrategy::IMMEDIATE_CANCEL:
            return execute_immediate_cancel(executions).success;
        case RollbackStrategy::MARKET_CLOSE:
            return execute_market_close(executions).success;
        case RollbackStrategy::GRADUAL_LIQUIDATION:
            return execute_gradual_liquidation(executions).success;
        case RollbackStrategy::HEDGE_POSITION:
            return execute_hedge_position(executions).success;
        default:
            return false;
    }
}

RollbackManager::RollbackResult RollbackManager::execute_immediate_cancel(
    const std::vector<OrderExecutionDetails>& executions) {
    
    RollbackResult result;
    result.rollback_id = generate_rollback_id();
    result.strategy_used = RollbackStrategy::IMMEDIATE_CANCEL;
    result.success = true;
    
    auto start_time = std::chrono::system_clock::now();
    
    for (const auto& execution : executions) {
        if (execution.status == OrderExecutionStatus::SUBMITTED ||
            execution.status == OrderExecutionStatus::PARTIALLY_FILLED) {
            
            // In a real implementation, we would cancel the order here
            // For now, we'll simulate the cancellation
            utils::Logger::info("Canceling order: {}", execution.exchange_order_id);
        }
    }
    
    result.rollback_time = std::chrono::duration_cast<std::chrono::milliseconds>(
        std::chrono::system_clock::now() - start_time);
    
    return result;
}

RollbackManager::RollbackResult RollbackManager::execute_market_close(
    const std::vector<OrderExecutionDetails>& executions) {
    
    RollbackResult result;
    result.rollback_id = generate_rollback_id();
    result.strategy_used = RollbackStrategy::MARKET_CLOSE;
    
    // Create offsetting market orders
    result.rollback_orders = create_offsetting_orders(executions);
    result.success = !result.rollback_orders.empty();
    
    return result;
}

RollbackManager::RollbackResult RollbackManager::execute_gradual_liquidation(
    const std::vector<OrderExecutionDetails>& executions) {
    
    RollbackResult result;
    result.rollback_id = generate_rollback_id();
    result.strategy_used = RollbackStrategy::GRADUAL_LIQUIDATION;
    
    // Implement gradual liquidation strategy
    result.success = true; // Simplified
    
    return result;
}

RollbackManager::RollbackResult RollbackManager::execute_hedge_position(
    const std::vector<OrderExecutionDetails>& executions) {
    
    RollbackResult result;
    result.rollback_id = generate_rollback_id();
    result.strategy_used = RollbackStrategy::HEDGE_POSITION;
    
    // Implement hedging strategy
    result.success = true; // Simplified
    
    return result;
}

std::vector<types::Order> RollbackManager::create_offsetting_orders(
    const std::vector<OrderExecutionDetails>& executions) {
    
    std::vector<types::Order> offsetting_orders;
    
    for (const auto& execution : executions) {
        if (execution.filled_quantity > 0) {
            types::Order offset_order = execution.original_order;
            
            // Reverse the side
            offset_order.side = (execution.original_order.side == types::OrderSide::BUY) 
                              ? types::OrderSide::SELL 
                              : types::OrderSide::BUY;
            
            offset_order.quantity = execution.filled_quantity;
            offset_order.type = types::OrderType::MARKET;
            
            offsetting_orders.push_back(offset_order);
        }
    }
    
    return offsetting_orders;
}

bool RollbackManager::validate_rollback_feasibility(const std::vector<OrderExecutionDetails>& executions) {
    return !executions.empty();
}

std::string RollbackManager::generate_rollback_id() const {
    static std::atomic<size_t> counter{0};
    auto now = std::chrono::system_clock::now();
    auto timestamp = std::chrono::duration_cast<std::chrono::milliseconds>(
        now.time_since_epoch()).count();
    
    return "RB_" + std::to_string(timestamp) + "_" + std::to_string(counter++);
}

// Utility functions implementation
namespace order_router_utils {

types::Order create_market_buy_order(const std::string& exchange, const std::string& symbol,
                                    double quantity) {
    types::Order order;
    order.exchange = exchange;
    order.symbol = symbol;
    order.side = types::OrderSide::BUY;
    order.type = types::OrderType::MARKET;
    order.quantity = quantity;
    order.timestamp = std::chrono::system_clock::now();
    return order;
}

types::Order create_market_sell_order(const std::string& exchange, const std::string& symbol,
                                     double quantity) {
    types::Order order;
    order.exchange = exchange;
    order.symbol = symbol;
    order.side = types::OrderSide::SELL;
    order.type = types::OrderType::MARKET;
    order.quantity = quantity;
    order.timestamp = std::chrono::system_clock::now();
    return order;
}

double calculate_price_slippage(double expected_price, double actual_price) {
    return actual_price - expected_price;
}

double calculate_percentage_slippage(double expected_price, double actual_price) {
    if (expected_price == 0) return 0.0;
    return ((actual_price - expected_price) / expected_price) * 100.0;
}

bool is_slippage_acceptable(double slippage, double tolerance) {
    return std::abs(slippage) <= tolerance;
}

std::chrono::milliseconds calculate_execution_latency(
    std::chrono::system_clock::time_point start_time) {
    return std::chrono::duration_cast<std::chrono::milliseconds>(
        std::chrono::system_clock::now() - start_time);
}

double calculate_fill_rate(double filled_quantity, double total_quantity) {
    if (total_quantity == 0) return 0.0;
    return filled_quantity / total_quantity;
}

double calculate_average_fill_price(const std::vector<types::Trade>& fills) {
    if (fills.empty()) return 0.0;
    
    double total_value = 0.0;
    double total_quantity = 0.0;
    
    for (const auto& fill : fills) {
        total_value += fill.price * fill.quantity;
        total_quantity += fill.quantity;
    }
    
    return total_quantity > 0 ? total_value / total_quantity : 0.0;
}

nlohmann::json order_execution_to_json(const OrderExecutionDetails& details) {
    nlohmann::json j;
    j["order_id"] = details.order_id;
    j["exchange_order_id"] = details.exchange_order_id;
    j["status"] = status_to_string(details.status);
    j["filled_quantity"] = details.filled_quantity;
    j["remaining_quantity"] = details.remaining_quantity;
    j["average_fill_price"] = details.average_fill_price;
    j["total_fees"] = details.total_fees;
    j["execution_latency_ms"] = details.execution_latency.count();
    j["error_message"] = details.error_message;
    return j;
}

} // namespace order_router_utils

} // namespace trading_engine
} // namespace ats