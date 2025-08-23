#include "trading_engine_service.hpp"
#include "order_router.hpp"
#include "spread_calculator.hpp"
#include "redis_subscriber.hpp"
#include "utils/logger.hpp"
#include <algorithm>
#include <sstream>
#include <iomanip>
#include <fstream>

#ifdef _WIN32
#include <windows.h>
#include <psapi.h>
#elif defined(__linux__)
#include <sys/resource.h>
#include <fstream>
#include <sstream>
#elif defined(__APPLE__)
#include <sys/types.h>
#include <sys/sysctl.h>
#include <mach/mach.h>
#endif

namespace ats {
namespace trading_engine {

TradingEngineService::TradingEngineService() {
    statistics_.session_start_time = std::chrono::system_clock::now();
}

TradingEngineService::~TradingEngineService() {
    if (running_) {
        stop();
    }
}

bool TradingEngineService::initialize(const config::ConfigManager& config) {
    if (initialized_) {
        utils::Logger::warn("TradingEngineService already initialized");
        return true;
    }
    
    try {
        // Load configuration
        config_.enabled = config.get_value<bool>("trading_engine.enabled", false);
        config_.min_spread_threshold = config.get_value<double>("trading_engine.min_spread_threshold", 0.005);
        config_.max_position_size = config.get_value<double>("trading_engine.max_position_size", 10000.0);
        config_.max_daily_volume = config.get_value<double>("trading_engine.max_daily_volume", 100000.0);
        config_.max_concurrent_trades = config.get_value<int>("trading_engine.max_concurrent_trades", 5);
        config_.worker_thread_count = config.get_value<int>("trading_engine.worker_thread_count", 4);
        config_.max_queue_size = config.get_value<size_t>("trading_engine.max_queue_size", 1000);
        config_.enable_paper_trading = config.get_value<bool>("trading_engine.enable_paper_trading", false);
        config_.enable_rollback_on_failure = config.get_value<bool>("trading_engine.enable_rollback_on_failure", true);
        
        // Initialize core components
        if (!initialize_redis_subscriber(config)) {
            utils::Logger::error("Failed to initialize Redis subscriber");
            return false;
        }
        
        if (!initialize_order_router(config)) {
            utils::Logger::error("Failed to initialize order router");
            return false;
        }
        
        if (!initialize_spread_calculator(config)) {
            utils::Logger::error("Failed to initialize spread calculator");
            return false;
        }
        
        // Initialize Prometheus exporter
        int metrics_port = config.get_value<int>("trading_engine.metrics_port", 8082);
        prometheus_exporter_ = std::make_unique<monitoring::PrometheusExporter>("trading-engine", metrics_port);
        
        // Initialize trade logger
        trade_logger_ = std::make_unique<TradeLogger>();
        std::string influxdb_url = config.get_value<std::string>("influxdb.url", "http://localhost:8086");
        std::string database = config.get_value<std::string>("influxdb.database", "ats_trades");
        
        if (!trade_logger_->initialize(influxdb_url, database)) {
            utils::Logger::error("Failed to initialize trade logger");
            return false;
        }
        
        // Initialize file logging as backup
        std::string log_dir = config.get_value<std::string>("trading_engine.log_directory", "logs/trades");
        trade_logger_->initialize_file_logging(log_dir);
        
        initialized_ = true;
        utils::Logger::info("TradingEngineService initialized successfully");
        
        return true;
        
    } catch (const std::exception& e) {
        utils::Logger::error("Failed to initialize TradingEngineService: {}", e.what());
        return false;
    }
}

bool TradingEngineService::start() {
    if (!initialized_) {
        utils::Logger::error("TradingEngineService not initialized");
        return false;
    }
    
    if (running_) {
        utils::Logger::warn("TradingEngineService already running");
        return true;
    }
    
    if (!config_.enabled) {
        utils::Logger::warn("TradingEngineService is disabled in configuration");
        return false;
    }
    
    try {
        running_ = true;
        emergency_stopped_ = false;
        
        // Start Prometheus metrics exporter
        prometheus_exporter_->start();
        prometheus_exporter_->set_health_status(true);
        
        // Start core components
        if (!redis_subscriber_->start()) {
            utils::Logger::error("Failed to start Redis subscriber");
            running_ = false;
            return false;
        }
        
        // Set up callbacks
        redis_subscriber_->set_price_update_callback([this](const PriceUpdateEvent& event) {
            on_price_update(event.ticker);
        });
        
        order_router_->set_execution_completed_callback([this](const SimultaneousExecutionResult& result) {
            TradeExecution execution;
            execution.trade_id = result.trade_id;
            execution.result = result.overall_result;
            execution.actual_profit = result.actual_profit;
            execution.total_fees = result.total_fees;
            execution.execution_latency = result.total_execution_time;
            execution.timestamp = std::chrono::system_clock::now();
            
            on_trade_execution_completed(execution);
        });
        
        order_router_->set_error_callback([this](const std::string& error) {
            on_error_occurred(error);
        });
        
        // Start worker threads
        start_worker_threads();
        
        utils::Logger::info("TradingEngineService started successfully");
        return true;
        
    } catch (const std::exception& e) {
        utils::Logger::error("Failed to start TradingEngineService: {}", e.what());
        running_ = false;
        return false;
    }
}

void TradingEngineService::stop() {
    if (!running_) {
        return;
    }
    
    utils::Logger::info("Stopping TradingEngineService...");
    
    // Stop Prometheus exporter
    if (prometheus_exporter_) {
        prometheus_exporter_->set_health_status(false);
        prometheus_exporter_->stop();
    }
    
    running_ = false;
    queue_condition_.notify_all();
    
    // Stop worker threads
    stop_worker_threads();
    
    // Stop components
    if (redis_subscriber_) {
        redis_subscriber_->stop();
    }
    
    // Flush any pending logs
    if (trade_logger_) {
        trade_logger_->flush_pending_logs();
    }
    
    utils::Logger::info("TradingEngineService stopped");
}

bool TradingEngineService::is_running() const {
    return running_;
}

void TradingEngineService::update_config(const TradingEngineConfig& config) {
    std::lock_guard<std::mutex> lock(statistics_mutex_);
    config_ = config;
    
    if (order_router_) {
        OrderRouterConfig router_config;
        router_config.order_timeout = config.execution_timeout;
        router_config.execution_timeout = config.execution_timeout;
        router_config.max_slippage_tolerance = config.slippage_tolerance;
        order_router_->update_config(router_config);
    }
    
    utils::Logger::info("TradingEngineService configuration updated");
}

TradingEngineConfig TradingEngineService::get_config() const {
    std::lock_guard<std::mutex> lock(statistics_mutex_);
    return config_;
}

bool TradingEngineService::execute_arbitrage(const ArbitrageOpportunity& opportunity) {
    if (!running_ || emergency_stopped_) {
        utils::Logger::warn("Cannot execute arbitrage: service not running or emergency stopped");
        return false;
    }
    
    if (!validate_opportunity(opportunity)) {
        utils::Logger::warn("Invalid arbitrage opportunity");
        return false;
    }
    
    if (!is_trade_approved(opportunity)) {
        utils::Logger::warn("Trade not approved by risk manager");
        return false;
    }
    
    // Check concurrent trade limit
    {
        std::shared_lock<std::shared_mutex> lock(trades_mutex_);
        if (active_trades_.size() >= static_cast<size_t>(config_.max_concurrent_trades)) {
            utils::Logger::warn("Maximum concurrent trades limit reached");
            return false;
        }
    }
    
    // Add to opportunity queue
    {
        std::lock_guard<std::mutex> lock(queue_mutex_);
        if (opportunity_queue_.size() >= config_.max_queue_size) {
            utils::Logger::warn("Opportunity queue is full");
            return false;
        }
        
        opportunity_queue_.push(opportunity);
    }
    
    queue_condition_.notify_one();
    statistics_.total_opportunities_detected++;
    
    if (opportunity_callback_) {
        opportunity_callback_(opportunity);
    }
    
    return true;
}

std::vector<TradeExecution> TradingEngineService::get_active_trades() const {
    std::shared_lock<std::shared_mutex> lock(trades_mutex_);
    
    std::vector<TradeExecution> result;
    result.reserve(active_trades_.size());
    
    for (const auto& [trade_id, execution] : active_trades_) {
        result.push_back(execution);
    }
    
    return result;
}

std::vector<TradeExecution> TradingEngineService::get_completed_trades(std::chrono::hours lookback) const {
    std::shared_lock<std::shared_mutex> lock(trades_mutex_);
    
    auto cutoff_time = std::chrono::system_clock::now() - lookback;
    std::vector<TradeExecution> result;
    
    std::copy_if(completed_trades_.begin(), completed_trades_.end(),
                 std::back_inserter(result),
                 [cutoff_time](const TradeExecution& trade) {
                     return trade.timestamp >= cutoff_time;
                 });
    
    return result;
}

std::string TradingEngineService::submit_manual_trade(const std::string& symbol,
                                                     const std::string& buy_exchange,
                                                     const std::string& sell_exchange,
                                                     double quantity) {
    if (!running_ || emergency_stopped_) {
        return "";
    }
    
    // Create manual arbitrage opportunity
    ArbitrageOpportunity opportunity;
    opportunity.symbol = symbol;
    opportunity.buy_exchange = buy_exchange;
    opportunity.sell_exchange = sell_exchange;
    opportunity.available_quantity = quantity;
    opportunity.detected_at = std::chrono::system_clock::now();
    
    // Get current prices (simplified - in real implementation would get from spread calculator)
    opportunity.buy_price = 50000.0;  // Placeholder
    opportunity.sell_price = 50100.0; // Placeholder
    opportunity.spread_percentage = (opportunity.sell_price - opportunity.buy_price) / opportunity.buy_price * 100.0;
    opportunity.expected_profit = (opportunity.sell_price - opportunity.buy_price) * quantity;
    
    if (execute_arbitrage(opportunity)) {
        return generate_trade_id();
    }
    
    return "";
}

bool TradingEngineService::cancel_trade(const std::string& trade_id) {
    std::unique_lock<std::shared_mutex> lock(trades_mutex_);
    
    auto it = active_trades_.find(trade_id);
    if (it == active_trades_.end()) {
        return false;
    }
    
    // In a real implementation, we would cancel the associated orders
    // For now, just mark as canceled
    it->second.result = ExecutionResult::FAILURE;
    it->second.error_message = "Trade canceled manually";
    
    // Move to completed trades
    completed_trades_.push_back(it->second);
    active_trades_.erase(it);
    
    utils::Logger::info("Trade {} canceled manually", trade_id);
    return true;
}

types::Portfolio TradingEngineService::get_current_portfolio() const {
    // This would aggregate balances across all exchanges
    // For now, return empty portfolio
    return types::Portfolio{};
}

std::unordered_map<std::string, types::Balance> TradingEngineService::get_exchange_balances() const {
    if (!order_router_) {
        return {};
    }
    
    // Get balances from all exchanges via order router
    auto all_balances = order_router_->get_all_balances();
    std::unordered_map<std::string, types::Balance> result;
    
    // Flatten the structure (simplified)
    for (const auto& [exchange_id, balances] : all_balances) {
        if (!balances.empty()) {
            result[exchange_id] = balances[0]; // Take first balance as example
        }
    }
    
    return result;
}

double TradingEngineService::get_available_balance(const std::string& exchange, 
                                                  const types::Currency& currency) const {
    if (!order_router_) {
        return 0.0;
    }
    
    return order_router_->get_available_balance(exchange, currency);
}

bool TradingEngineService::set_risk_manager(std::shared_ptr<RiskManager> risk_manager) {
    risk_manager_ = risk_manager;
    utils::Logger::info("Risk manager set for TradingEngineService");
    return true;
}

bool TradingEngineService::is_trade_approved(const ArbitrageOpportunity& opportunity) const {
    if (!risk_manager_) {
        // If no risk manager is set, approve all trades (simplified)
        return true;
    }
    
    // In a real implementation, this would call risk_manager_->approve_trade(opportunity)
    return opportunity.risk_approved;
}

void TradingEngineService::emergency_stop() {
    emergency_stopped_ = true;
    
    // Cancel all active trades
    std::unique_lock<std::shared_mutex> lock(trades_mutex_);
    for (auto& [trade_id, execution] : active_trades_) {
        if (execution.result == ExecutionResult::SUCCESS) {
            // In a real implementation, would initiate emergency liquidation
            execution.result = ExecutionResult::FAILURE;
            execution.error_message = "Emergency stop activated";
        }
    }
    
    utils::Logger::critical("EMERGENCY STOP ACTIVATED - All trading halted");
    
    if (error_callback_) {
        error_callback_("Emergency stop activated");
    }
}

bool TradingEngineService::is_emergency_stopped() const {
    return emergency_stopped_;
}

TradingStatistics TradingEngineService::get_statistics() const {
    std::lock_guard<std::mutex> lock(statistics_mutex_);
    
    // Update uptime
    auto now = std::chrono::system_clock::now();
    auto uptime = std::chrono::duration_cast<std::chrono::milliseconds>(
        now - statistics_.session_start_time);
    const_cast<TradingStatistics&>(statistics_).uptime = uptime;
    
    return statistics_;
}

double TradingEngineService::get_current_profit_loss() const {
    return statistics_.total_profit_loss.load();
}

double TradingEngineService::get_daily_volume() const {
    return statistics_.total_volume_traded.load();
}

size_t TradingEngineService::get_active_trade_count() const {
    std::shared_lock<std::shared_mutex> lock(trades_mutex_);
    return active_trades_.size();
}

void TradingEngineService::set_opportunity_callback(OpportunityCallback callback) {
    opportunity_callback_ = callback;
}

void TradingEngineService::set_execution_callback(ExecutionCallback callback) {
    execution_callback_ = callback;
}

void TradingEngineService::set_error_callback(ErrorCallback callback) {
    error_callback_ = callback;
}

bool TradingEngineService::is_healthy() const {
    if (!running_ || emergency_stopped_) {
        return false;
    }
    
    // Check component health
    if (redis_subscriber_ && !redis_subscriber_->is_healthy()) {
        return false;
    }
    
    if (order_router_ && !order_router_->is_healthy()) {
        return false;
    }
    
    if (trade_logger_ && !trade_logger_->is_healthy()) {
        return false;
    }
    
    // Check performance metrics
    double success_rate = statistics_.success_rate.load();
    if (success_rate < 0.90) { // Require 90% success rate for health
        return false;
    }
    
    return true;
}

std::vector<std::string> TradingEngineService::get_health_issues() const {
    std::vector<std::string> issues;
    
    if (!running_) {
        issues.push_back("Service not running");
    }
    
    if (emergency_stopped_) {
        issues.push_back("Emergency stop activated");
    }
    
    if (redis_subscriber_ && !redis_subscriber_->is_healthy()) {
        issues.push_back("Redis subscriber unhealthy");
    }
    
    if (order_router_ && !order_router_->is_healthy()) {
        issues.push_back("Order router unhealthy");
    }
    
    if (trade_logger_ && !trade_logger_->is_healthy()) {
        issues.push_back("Trade logger unhealthy");
    }
    
    double success_rate = statistics_.success_rate.load();
    if (success_rate < 0.90) {
        issues.push_back("Low success rate: " + std::to_string(success_rate * 100) + "%");
    }
    
    size_t pending_logs = trade_logger_ ? trade_logger_->get_pending_log_count() : 0;
    if (pending_logs > 1000) {
        issues.push_back("High pending log count: " + std::to_string(pending_logs));
    }
    
    return issues;
}

std::string TradingEngineService::get_status_report() const {
    std::ostringstream oss;
    auto stats = get_statistics();
    
    oss << "=== Trading Engine Status Report ===\n";
    oss << "Running: " << (running_ ? "Yes" : "No") << "\n";
    oss << "Emergency Stopped: " << (emergency_stopped_ ? "Yes" : "No") << "\n";
    oss << "Healthy: " << (is_healthy() ? "Yes" : "No") << "\n";
    oss << "Active Trades: " << get_active_trade_count() << "\n";
    oss << "Total Opportunities: " << stats.total_opportunities_detected.load() << "\n";
    oss << "Executed Trades: " << stats.total_opportunities_executed.load() << "\n";
    oss << "Success Rate: " << std::fixed << std::setprecision(2) 
        << (stats.success_rate.load() * 100) << "%\n";
    oss << "Total P&L: " << std::fixed << std::setprecision(2) 
        << stats.total_profit_loss.load() << " USD\n";
    oss << "Average Execution Time: " << stats.average_execution_time.load().count() << " ms\n";
    oss << "Uptime: " << stats.uptime.load().count() / 1000 << " seconds\n";
    
    auto health_issues = get_health_issues();
    if (!health_issues.empty()) {
        oss << "Health Issues:\n";
        for (const auto& issue : health_issues) {
            oss << "  - " << issue << "\n";
        }
    }
    
    return oss.str();
}

// Event handlers
void TradingEngineService::on_price_update(const types::Ticker& ticker) {
    if (!running_ || emergency_stopped_) {
        return;
    }
    
    // Update spread calculator with new price data
    if (spread_calculator_) {
        spread_calculator_->update_ticker(ticker);
        
        // Detect arbitrage opportunities
        auto opportunities = spread_calculator_->detect_arbitrage_opportunities(
            config_.min_spread_threshold);
        
        for (const auto& opportunity : opportunities) {
            on_arbitrage_opportunity_detected(opportunity);
        }
    }
}

void TradingEngineService::on_arbitrage_opportunity_detected(const ArbitrageOpportunity& opportunity) {
    if (!running_ || emergency_stopped_) {
        return;
    }
    
    // Log the opportunity
    if (trade_logger_) {
        trade_logger_->log_arbitrage_opportunity(opportunity);
    }
    
    // Update Prometheus metrics
    if (prometheus_exporter_) {
        prometheus_exporter_->increment_arbitrage_opportunities(opportunity.symbol);
    }
    
    // Execute if profitable and within limits
    if (opportunity.expected_profit > 0 && 
        opportunity.spread_percentage >= config_.min_spread_threshold) {
        execute_arbitrage(opportunity);
    }
}

void TradingEngineService::on_trade_execution_completed(const TradeExecution& execution) {
    // Update statistics
    statistics_.total_opportunities_executed++;
    
    if (execution.result == ExecutionResult::SUCCESS) {
        statistics_.total_successful_trades++;
        statistics_.total_profit_loss += execution.actual_profit;
        
        // Update Prometheus metrics for successful trade
        if (prometheus_exporter_) {
            prometheus_exporter_->increment_successful_trades();
            prometheus_exporter_->record_profit_per_trade(execution.actual_profit);
            prometheus_exporter_->record_order_latency(execution.buy_exchange, execution.execution_latency.count());
            prometheus_exporter_->record_order_latency(execution.sell_exchange, execution.execution_latency.count());
            prometheus_exporter_->set_total_pnl(statistics_.total_profit_loss.load());
        }
    } else {
        statistics_.total_failed_trades++;
        
        // Update Prometheus metrics for failed trade
        if (prometheus_exporter_) {
            prometheus_exporter_->increment_failed_trades();
        }
        
        if (config_.enable_rollback_on_failure) {
            // Trigger rollback logic
            statistics_.total_rollbacks++;
        }
    }
    
    statistics_.total_fees_paid += execution.total_fees;
    statistics_.total_volume_traded += execution.executed_quantity;
    
    // Update timing statistics
    auto latency_ms = execution.execution_latency.count();
    statistics_.average_execution_time = std::chrono::milliseconds(
        (statistics_.average_execution_time.load().count() + latency_ms) / 2);
    
    if (latency_ms < statistics_.fastest_execution.load().count()) {
        statistics_.fastest_execution = execution.execution_latency;
    }
    if (latency_ms > statistics_.slowest_execution.load().count()) {
        statistics_.slowest_execution = execution.execution_latency;
    }
    
    update_statistics();
    
    // Move from active to completed trades
    {
        std::unique_lock<std::shared_mutex> lock(trades_mutex_);
        active_trades_.erase(execution.trade_id);
        completed_trades_.push_back(execution);
        
        // Keep only recent completed trades to avoid memory bloat
        if (completed_trades_.size() > 1000) {
            completed_trades_.erase(completed_trades_.begin(), 
                                   completed_trades_.begin() + 100);
        }
    }
    
    // Log the execution
    if (trade_logger_) {
        trade_logger_->log_trade_execution(execution);
    }
    
    if (execution_callback_) {
        execution_callback_(execution);
    }
    
    utils::Logger::info("Trade execution completed: {} ({})", 
                       execution.trade_id, 
                       execution.result == ExecutionResult::SUCCESS ? "SUCCESS" : "FAILED");
}

void TradingEngineService::on_error_occurred(const std::string& error) {
    utils::Logger::error("Trading engine error: {}", error);
    
    if (error_callback_) {
        error_callback_(error);
    }
}

// Worker thread functions
void TradingEngineService::worker_thread_main() {
    utils::Logger::debug("Trading engine worker thread started");
    
    while (running_) {
        std::unique_lock<std::mutex> lock(queue_mutex_);
        
        queue_condition_.wait(lock, [this]() {
            return !opportunity_queue_.empty() || !running_;
        });
        
        if (!running_) break;
        
        if (!opportunity_queue_.empty()) {
            auto opportunity = opportunity_queue_.front();
            opportunity_queue_.pop();
            lock.unlock();
            
            // Execute the trade
            auto execution = execute_trade_internal(opportunity);
            
            // Track the execution
            {
                std::unique_lock<std::shared_mutex> trades_lock(trades_mutex_);
                active_trades_[execution.trade_id] = execution;
            }
            
            // If it's not already completed, the order router will handle monitoring
            if (execution.result != ExecutionResult::SUCCESS && 
                execution.result != ExecutionResult::FAILURE) {
                // The order router will call our completion callback when done
            } else {
                // Immediate completion
                on_trade_execution_completed(execution);
            }
            
            lock.lock();
        }
    }
    
    utils::Logger::debug("Trading engine worker thread stopped");
}

void TradingEngineService::price_monitoring_thread_main() {
    utils::Logger::debug("Price monitoring thread started");
    
    while (running_) {
        // This thread would monitor price feed health
        // For now, just sleep and check periodically
        std::this_thread::sleep_for(std::chrono::seconds(10));
        
        if (redis_subscriber_ && !redis_subscriber_->is_healthy()) {
            utils::Logger::warn("Redis subscriber health check failed");
        }
    }
    
    utils::Logger::debug("Price monitoring thread stopped");
}

void TradingEngineService::statistics_thread_main() {
    utils::Logger::debug("Statistics thread started");
    
    while (running_) {
        std::this_thread::sleep_for(std::chrono::seconds(60));
        
        if (running_) {
            update_statistics();
            
            // Update system metrics for Prometheus
            if (prometheus_exporter_) {
                collect_system_metrics();
            }
            
            // Log periodic statistics
            if (trade_logger_) {
                trade_logger_->log_performance_metrics(statistics_);
            }
        }
    }
    
    utils::Logger::debug("Statistics thread stopped");
}

// Trading logic
bool TradingEngineService::validate_opportunity(const ArbitrageOpportunity& opportunity) const {
    if (opportunity.symbol.empty()) return false;
    if (opportunity.buy_exchange.empty()) return false;
    if (opportunity.sell_exchange.empty()) return false;
    if (opportunity.buy_exchange == opportunity.sell_exchange) return false;
    if (opportunity.available_quantity <= 0) return false;
    if (opportunity.buy_price >= opportunity.sell_price) return false;
    if (opportunity.expected_profit <= 0) return false;
    
    return true;
}

TradeExecution TradingEngineService::execute_trade_internal(const ArbitrageOpportunity& opportunity) {
    TradeExecution execution;
    execution.trade_id = generate_trade_id();
    execution.symbol = opportunity.symbol;
    execution.buy_exchange = opportunity.buy_exchange;
    execution.sell_exchange = opportunity.sell_exchange;
    execution.buy_price = opportunity.buy_price;
    execution.sell_price = opportunity.sell_price;
    execution.quantity = opportunity.available_quantity;
    execution.expected_profit = opportunity.expected_profit;
    execution.timestamp = std::chrono::system_clock::now();
    
    if (config_.enable_paper_trading) {
        // Simulate successful execution
        execution.result = ExecutionResult::SUCCESS;
        execution.executed_quantity = opportunity.available_quantity;
        execution.actual_profit = opportunity.expected_profit * 0.95; // Simulate some slippage
        execution.total_fees = opportunity.total_fees;
        execution.execution_latency = std::chrono::milliseconds(rand() % 100 + 50);
        
        utils::Logger::info("Paper trade executed: {}", execution.trade_id);
        return execution;
    }
    
    if (!order_router_) {
        execution.result = ExecutionResult::FAILURE;
        execution.error_message = "Order router not available";
        return execution;
    }
    
    // Execute through order router
    auto future = order_router_->execute_arbitrage_orders_async(opportunity);
    
    try {
        auto timeout = config_.execution_timeout;
        auto status = future.wait_for(timeout);
        
        if (status == std::future_status::timeout) {
            execution.result = ExecutionResult::TIMEOUT;
            execution.error_message = "Execution timeout";
        } else {
            auto result = future.get();
            execution.result = result.overall_result;
            execution.executed_quantity = result.total_filled_quantity;
            execution.actual_profit = result.actual_profit;
            execution.total_fees = result.total_fees;
            execution.execution_latency = result.total_execution_time;
            execution.error_message = result.error_message;
        }
    } catch (const std::exception& e) {
        execution.result = ExecutionResult::FAILURE;
        execution.error_message = e.what();
    }
    
    return execution;
}

bool TradingEngineService::rollback_trade(const TradeExecution& execution) {
    if (!config_.enable_rollback_on_failure) {
        return false;
    }
    
    // Implementation would depend on having a rollback manager
    utils::Logger::info("Rollback requested for trade: {}", execution.trade_id);
    return true; // Simplified
}

// Initialization helpers
bool TradingEngineService::initialize_redis_subscriber(const config::ConfigManager& config) {
    redis_subscriber_ = std::make_unique<RedisSubscriber>();
    
    RedisSubscriberConfig redis_config;
    redis_config.host = config.get_value<std::string>("redis.host", "localhost");
    redis_config.port = config.get_value<int>("redis.port", 6379);
    redis_config.password = config.get_value<std::string>("redis.password", "");
    
    // Subscribe to price channels
    redis_config.channels = {
        "price_updates",
        "arbitrage_opportunities"
    };
    
    return redis_subscriber_->initialize(redis_config);
}

bool TradingEngineService::initialize_order_router(const config::ConfigManager& config) {
    order_router_ = std::make_unique<OrderRouter>();
    
    OrderRouterConfig router_config;
    router_config.order_timeout = config_.execution_timeout;
    router_config.execution_timeout = config_.execution_timeout;
    router_config.max_slippage_tolerance = config_.slippage_tolerance;
    router_config.enable_rollback_on_failure = config_.enable_rollback_on_failure;
    
    return order_router_->initialize(router_config);
}

bool TradingEngineService::initialize_spread_calculator(const config::ConfigManager& config) {
    spread_calculator_ = std::make_unique<SpreadCalculator>();
    return spread_calculator_->initialize(config);
}

void TradingEngineService::start_worker_threads() {
    // Start worker threads
    for (int i = 0; i < config_.worker_thread_count; ++i) {
        worker_threads_.emplace_back([this]() {
            worker_thread_main();
        });
    }
    
    // Start monitoring threads
    price_monitoring_thread_ = std::thread([this]() {
        price_monitoring_thread_main();
    });
    
    statistics_thread_ = std::thread([this]() {
        statistics_thread_main();
    });
    
    utils::Logger::info("Started {} worker threads plus monitoring threads", 
                       config_.worker_thread_count);
}

void TradingEngineService::stop_worker_threads() {
    // Wait for worker threads to finish
    for (auto& thread : worker_threads_) {
        if (thread.joinable()) {
            thread.join();
        }
    }
    worker_threads_.clear();
    
    // Stop monitoring threads
    if (price_monitoring_thread_.joinable()) {
        price_monitoring_thread_.join();
    }
    
    if (statistics_thread_.joinable()) {
        statistics_thread_.join();
    }
    
    utils::Logger::info("All worker threads stopped");
}

void TradingEngineService::update_statistics() {
    std::lock_guard<std::mutex> lock(statistics_mutex_);
    
    size_t total_executed = statistics_.total_opportunities_executed.load();
    size_t successful = statistics_.total_successful_trades.load();
    
    if (total_executed > 0) {
        statistics_.success_rate = static_cast<double>(successful) / static_cast<double>(total_executed);
        statistics_.average_profit_per_trade = statistics_.total_profit_loss.load() / static_cast<double>(total_executed);
    }
}

void TradingEngineService::collect_system_metrics() {
    if (!prometheus_exporter_) return;
    
    // Collect CPU usage
    double cpu_usage = get_cpu_usage();
    prometheus_exporter_->set_cpu_usage(cpu_usage);
    
    // Collect memory usage
    double memory_mb = get_memory_usage();
    prometheus_exporter_->set_memory_usage(memory_mb);
    
    // Update service uptime
    auto uptime = std::chrono::duration_cast<std::chrono::seconds>(
        std::chrono::steady_clock::now() - statistics_.session_start_time.load()
    ).count();
    prometheus_exporter_->set_service_uptime(static_cast<double>(uptime));
}

double TradingEngineService::get_cpu_usage() {
#ifdef _WIN32
    static ULARGE_INTEGER lastCPU, lastSysCPU, lastUserCPU;
    static int numProcessors;
    static bool initialized = false;
    
    if (!initialized) {
        SYSTEM_INFO sysInfo;
        FILETIME ftime, fsys, fuser;
        
        GetSystemInfo(&sysInfo);
        numProcessors = sysInfo.dwNumberOfProcessors;
        
        GetSystemTimeAsFileTime(&ftime);
        memcpy(&lastCPU, &ftime, sizeof(FILETIME));
        
        GetProcessTimes(GetCurrentProcess(), &ftime, &ftime, &fsys, &fuser);
        memcpy(&lastSysCPU, &fsys, sizeof(FILETIME));
        memcpy(&lastUserCPU, &fuser, sizeof(FILETIME));
        
        initialized = true;
        return 0.0; // First call returns 0
    }
    
    FILETIME ftime, fsys, fuser;
    ULARGE_INTEGER now, sys, user;
    double percent = 0.0;
    
    GetSystemTimeAsFileTime(&ftime);
    memcpy(&now, &ftime, sizeof(FILETIME));
    
    GetProcessTimes(GetCurrentProcess(), &ftime, &ftime, &fsys, &fuser);
    memcpy(&sys, &fsys, sizeof(FILETIME));
    memcpy(&user, &fuser, sizeof(FILETIME));
    
    if (now.QuadPart > lastCPU.QuadPart) {
        percent = (sys.QuadPart - lastSysCPU.QuadPart) + (user.QuadPart - lastUserCPU.QuadPart);
        percent /= (now.QuadPart - lastCPU.QuadPart);
        percent /= numProcessors;
        percent *= 100;
    }
    
    lastCPU = now;
    lastUserCPU = user;
    lastSysCPU = sys;
    
    return percent;
#elif defined(__linux__)
    static unsigned long long lastTotalUser, lastTotalUserLow, lastTotalSys, lastTotalIdle;
    static bool initialized = false;
    
    std::ifstream file("/proc/stat");
    if (!file.is_open()) return 0.0;
    
    std::string line;
    std::getline(file, line);
    
    unsigned long long totalUser, totalUserLow, totalSys, totalIdle, total;
    
    std::sscanf(line.c_str(), "cpu %llu %llu %llu %llu",
                &totalUser, &totalUserLow, &totalSys, &totalIdle);
    
    if (!initialized) {
        lastTotalUser = totalUser;
        lastTotalUserLow = totalUserLow;
        lastTotalSys = totalSys;
        lastTotalIdle = totalIdle;
        initialized = true;
        return 0.0;
    }
    
    total = (totalUser - lastTotalUser) + (totalUserLow - lastTotalUserLow) + 
            (totalSys - lastTotalSys);
    
    double percent = total;
    total += (totalIdle - lastTotalIdle);
    percent /= total;
    percent *= 100;
    
    lastTotalUser = totalUser;
    lastTotalUserLow = totalUserLow;
    lastTotalSys = totalSys;
    lastTotalIdle = totalIdle;
    
    return percent;
#else
    return 0.0; // Unsupported platform
#endif
}

double TradingEngineService::get_memory_usage() {
#ifdef _WIN32
    PROCESS_MEMORY_COUNTERS memCounter;
    if (GetProcessMemoryInfo(GetCurrentProcess(), &memCounter, sizeof(memCounter))) {
        return static_cast<double>(memCounter.WorkingSetSize) / (1024.0 * 1024.0); // MB
    }
    return 0.0;
#elif defined(__linux__)
    std::ifstream file("/proc/self/status");
    std::string line;
    while (std::getline(file, line)) {
        if (line.substr(0, 6) == "VmRSS:") {
            std::istringstream iss(line);
            std::string key, value, unit;
            iss >> key >> value >> unit;
            return std::stod(value) / 1024.0; // Convert from KB to MB
        }
    }
    return 0.0;
#else
    return 0.0; // Unsupported platform
#endif
}

std::string TradingEngineService::generate_trade_id() const {
    static std::atomic<size_t> counter{0};
    auto now = std::chrono::system_clock::now();
    auto timestamp = std::chrono::duration_cast<std::chrono::milliseconds>(
        now.time_since_epoch()).count();
    
    return "TRADE_" + std::to_string(timestamp) + "_" + std::to_string(counter++);
}

// Utility functions implementation
namespace trading_utils {

bool is_valid_opportunity(const ArbitrageOpportunity& opportunity) {
    return !opportunity.symbol.empty() &&
           !opportunity.buy_exchange.empty() &&
           !opportunity.sell_exchange.empty() &&
           opportunity.buy_exchange != opportunity.sell_exchange &&
           opportunity.available_quantity > 0 &&
           opportunity.buy_price < opportunity.sell_price &&
           opportunity.expected_profit > 0;
}

bool is_opportunity_expired(const ArbitrageOpportunity& opportunity) {
    auto now = std::chrono::system_clock::now();
    return (now - opportunity.detected_at) > opportunity.validity_window;
}

double calculate_expected_profit(double buy_price, double sell_price, double quantity,
                               double buy_fee, double sell_fee, double slippage) {
    double gross_profit = (sell_price - buy_price) * quantity;
    double total_fees = buy_fee + sell_fee;
    double slippage_cost = slippage * quantity * (buy_price + sell_price) / 2.0;
    
    return gross_profit - total_fees - slippage_cost;
}

double calculate_actual_profit(const TradeExecution& execution) {
    return execution.actual_profit;
}

std::chrono::milliseconds calculate_execution_latency(
    std::chrono::system_clock::time_point start_time) {
    return std::chrono::duration_cast<std::chrono::milliseconds>(
        std::chrono::system_clock::now() - start_time);
}

double calculate_success_rate(size_t successful_trades, size_t total_trades) {
    if (total_trades == 0) return 0.0;
    return static_cast<double>(successful_trades) / static_cast<double>(total_trades);
}

nlohmann::json trading_statistics_to_json(const TradingStatistics& stats) {
    nlohmann::json j;
    j["total_opportunities_detected"] = stats.total_opportunities_detected.load();
    j["total_opportunities_executed"] = stats.total_opportunities_executed.load();
    j["total_successful_trades"] = stats.total_successful_trades.load();
    j["total_failed_trades"] = stats.total_failed_trades.load();
    j["total_profit_loss"] = stats.total_profit_loss.load();
    j["total_fees_paid"] = stats.total_fees_paid.load();
    j["success_rate"] = stats.success_rate.load();
    j["average_execution_time_ms"] = stats.average_execution_time.load().count();
    j["uptime_ms"] = stats.uptime.load().count();
    return j;
}

} // namespace trading_utils

} // namespace trading_engine
} // namespace ats