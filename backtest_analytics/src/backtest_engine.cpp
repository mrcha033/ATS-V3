#include "../include/backtest_engine.hpp"
#include "utils/logger.hpp"
#include <algorithm>
#include <execution>
#include <future>
#include <chrono>

namespace ats {
namespace backtest {

// Import Logger from utils namespace
using ats::utils::Logger;

// BacktestEngine Implementation
BacktestEngine::BacktestEngine() {
    config_.initial_capital = 100000.0;
    config_.commission_rate = 0.001;
    config_.spread_cost = 0.0001;
    config_.slippage_rate = 0.0001;
    config_.max_position_size = 0.1;
    config_.max_total_exposure = 0.8;
    config_.stop_loss_percentage = 0.02;
    config_.execution_model = "simple";
    config_.allow_short_selling = true;
    config_.compound_returns = true;
    config_.max_threads = std::thread::hardware_concurrency();
    config_.enable_progress_callback = true;
    config_.detailed_logging = false;
    config_.data_frequency = "1m";
    config_.require_complete_data = true;
    config_.fill_missing_data = true;
}

BacktestEngine::~BacktestEngine() {
    if (is_running_) {
        should_stop_ = true;
        for (auto& thread : worker_threads_) {
            if (thread.joinable()) {
                thread.join();
            }
        }
    }
}

void BacktestEngine::set_config(const BacktestConfig& config) {
    if (is_running_) {
        throw BacktestException("Cannot change configuration while backtest is running");
    }
    
    if (!validate_config()) {
        throw InvalidConfigurationException("Invalid backtest configuration");
    }
    
    config_ = config;
    Logger::info("Backtest configuration updated");
}

BacktestConfig BacktestEngine::get_config() const {
    return config_;
}

void BacktestEngine::add_strategy(std::shared_ptr<BacktestStrategy> strategy) {
    if (!strategy) {
        throw StrategyException("Strategy cannot be null");
    }
    
    strategies_.push_back(strategy);
    Logger::info("Added strategy: {}", strategy->get_strategy_name());
}

void BacktestEngine::remove_strategy(const std::string& strategy_name) {
    auto it = std::remove_if(strategies_.begin(), strategies_.end(),
        [&strategy_name](const std::shared_ptr<BacktestStrategy>& strategy) {
            return strategy->get_strategy_name() == strategy_name;
        });
    
    if (it != strategies_.end()) {
        strategies_.erase(it, strategies_.end());
        Logger::info("Removed strategy: {}", strategy_name);
    }
}

std::vector<std::string> BacktestEngine::get_strategy_names() const {
    std::vector<std::string> names;
    for (const auto& strategy : strategies_) {
        names.push_back(strategy->get_strategy_name());
    }
    return names;
}

void BacktestEngine::set_data_loader(std::shared_ptr<DataLoader> data_loader) {
    data_loader_ = data_loader;
    Logger::info("Data loader set");
}

bool BacktestEngine::load_market_data(const std::vector<std::string>& symbols,
                                     const std::vector<std::string>& exchanges) {
    if (!data_loader_) {
        Logger::error("Data loader not set");
        return false;
    }
    
    std::vector<TradeData> trade_data; // Not used in this context
    bool success = data_loader_->load_data(market_data_, trade_data);
    
    if (success) {
        preprocess_market_data();
        Logger::info("Loaded {} market data points", market_data_.size());
    }
    
    return success;
}

BacktestResult BacktestEngine::run_backtest() {
    if (config_.max_threads <= 1) {
        return execute_single_threaded();
    } else {
        return execute_multi_threaded();
    }
}

BacktestResult BacktestEngine::run_backtest_parallel() {
    return execute_multi_threaded();
}

BacktestResult BacktestEngine::execute_single_threaded() {
    is_running_ = true;
    should_stop_ = false;
    
    BacktestResult result;
    result.backtest_start_time = std::chrono::system_clock::now();
    
    try {
        if (!validate_data_integrity()) {
            throw InsufficientDataException("Data validation failed");
        }
        
        if (strategies_.empty()) {
            throw StrategyException("No strategies configured");
        }
        
        ExecutionContext context;
        context.available_capital = config_.initial_capital;
        context.total_portfolio_value = config_.initial_capital;
        context.processed_signals = 0;
        context.rejected_signals = 0;
        
        // Initialize portfolio snapshot
        PortfolioSnapshot initial_snapshot;
        initial_snapshot.timestamp = config_.start_date;
        initial_snapshot.total_value = config_.initial_capital;
        initial_snapshot.cash = config_.initial_capital;
        initial_snapshot.positions_value = 0.0;
        context.portfolio_snapshots.push_back(initial_snapshot);
        
        // Process market data chronologically
        size_t processed_points = 0;
        for (const auto& data_point : market_data_) {
            if (should_stop_) break;
            
            // Skip data outside configured date range
            if (data_point.timestamp < config_.start_date || 
                data_point.timestamp > config_.end_date) {
                continue;
            }
            
            // Update existing positions with current market data
            update_positions(data_point, context);
            
            // Check stop losses and take profits
            check_stop_losses_and_take_profits(data_point, context);
            
            // Generate signals from all strategies
            for (auto& strategy : strategies_) {
                try {
                    strategy->on_market_data(data_point);
                    
                    // Get historical window for strategy analysis
                    auto historical_data = get_historical_window(
                        data_point.symbol, data_point.timestamp, 100);
                    
                    auto signals = strategy->generate_signals(historical_data, data_point);
                    
                    // Process each signal
                    for (const auto& signal : signals) {
                        result.total_signals_generated++;
                        
                        if (execute_signal(signal, data_point, context)) {
                            result.signals_executed++;
                        } else {
                            result.signals_rejected++;
                        }
                    }
                    
                } catch (const std::exception& e) {
                    Logger::warn("Strategy {} error: {}", strategy->get_strategy_name(), e.what());
                    result.warnings.push_back("Strategy error: " + std::string(e.what()));
                }
            }
            
            // Create portfolio snapshot periodically
            if (processed_points % 100 == 0) {
                PortfolioSnapshot snapshot;
                snapshot.timestamp = data_point.timestamp;
                snapshot.total_value = context.total_portfolio_value;
                snapshot.cash = context.available_capital;
                snapshot.positions_value = context.total_portfolio_value - context.available_capital;
                
                for (const auto& position : context.positions) {
                    snapshot.positions[position.symbol] = position.quantity;
                }
                
                context.portfolio_snapshots.push_back(snapshot);
            }
            
            // Update progress
            if (config_.enable_progress_callback && progress_callback_ && processed_points % 1000 == 0) {
                BacktestProgress progress;
                progress.current_date = data_point.timestamp;
                progress.progress_percentage = (static_cast<double>(processed_points) / market_data_.size()) * 100.0;
                progress.processed_data_points = static_cast<int>(processed_points);
                progress.total_data_points = static_cast<int>(market_data_.size());
                progress.trades_executed = static_cast<int>(context.completed_trades.size());
                progress.current_portfolio_value = context.total_portfolio_value;
                progress.current_status = "Processing market data...";
                
                auto now = std::chrono::system_clock::now();
                progress.elapsed_time = std::chrono::duration_cast<std::chrono::milliseconds>(
                    now - result.backtest_start_time);
                progress.estimated_remaining = estimate_remaining_time(
                    progress.progress_percentage, progress.elapsed_time);
                
                update_progress(progress);
            }
            
            processed_points++;
        }
        
        // Final portfolio snapshot
        if (!context.portfolio_snapshots.empty()) {
            PortfolioSnapshot final_snapshot = context.portfolio_snapshots.back();
            final_snapshot.timestamp = config_.end_date;
            final_snapshot.total_value = context.total_portfolio_value;
            final_snapshot.cash = context.available_capital;
            final_snapshot.positions_value = context.total_portfolio_value - context.available_capital;
            context.portfolio_snapshots.push_back(final_snapshot);
        }
        
        // Calculate performance metrics
        PerformanceCalculator calc;
        result.performance = calc.calculate_metrics(
            context.completed_trades, context.portfolio_snapshots, 
            config_.initial_capital, 0.02);
        
        result.attribution = calc.calculate_attribution(context.completed_trades);
        
        // Copy results
        result.trades = std::move(context.completed_trades);
        result.portfolio_history = std::move(context.portfolio_snapshots);
        result.final_positions = std::move(context.positions);
        result.execution_rate = result.total_signals_generated > 0 ? 
            static_cast<double>(result.signals_executed) / result.total_signals_generated : 0.0;
        
        result.backtest_end_time = std::chrono::system_clock::now();
        result.execution_time = std::chrono::duration_cast<std::chrono::milliseconds>(
            result.backtest_end_time - result.backtest_start_time);
        
        // Data quality report
        if (data_loader_) {
            result.data_quality = data_loader_->analyze_data_quality(market_data_);
        }
        
        Logger::info("Backtest completed: {} trades, {:.2f}% total return, execution time: {}ms",
                 result.trades.size(), result.performance.total_return, result.execution_time.count());
        
    } catch (const std::exception& e) {
        result.errors.push_back("Backtest execution error: " + std::string(e.what()));
        Logger::error("Backtest execution failed: {}", e.what());
    }
    
    is_running_ = false;
    return result;
}

BacktestResult BacktestEngine::execute_multi_threaded() {
    // For now, delegate to single-threaded implementation
    // Multi-threading implementation would require more complex synchronization
    Logger::info("Multi-threaded execution requested, using single-threaded for stability");
    return execute_single_threaded();
}

bool BacktestEngine::execute_signal(const TradeSignal& signal, 
                                   const MarketDataPoint& market_data,
                                   ExecutionContext& context) {
    try {
        // Risk management checks
        if (!check_risk_limits(signal, context)) {
            context.rejected_signals++;
            return false;
        }
        
        // Calculate position size
        double position_size = calculate_position_size(signal, context.available_capital);
        if (position_size <= 0.0) {
            context.rejected_signals++;
            return false;
        }
        
        // Apply slippage and costs
        double execution_price = apply_slippage(signal.price, signal.type);
        double trade_value = position_size * execution_price;
        double transaction_costs = calculate_transaction_costs(trade_value);
        
        // Check if we have enough capital
        if (trade_value + transaction_costs > context.available_capital) {
            context.rejected_signals++;
            return false;
        }
        
        // Execute the trade
        using ats::types::SignalType;
        if (signal.type == SignalType::BUY) {
            // Open long position
            Position position("default_exchange", signal.symbol, signal.quantity, signal.price);
            
            context.positions.push_back(position);
            context.available_capital -= (trade_value + transaction_costs);
            
            if (config_.detailed_logging) {
                log_trade_execution(signal, nullptr);
            }
            
        } else if (signal.type == SignalType::SELL) {
            // Look for existing long position to close or open short position
            auto pos_it = std::find_if(context.positions.begin(), context.positions.end(),
                [&signal](const Position& pos) {
                    return pos.symbol == signal.symbol;
                });
            
            if (pos_it != context.positions.end()) {
                // Close long position
                TradeResult trade;
                trade.entry_time = pos_it->opened_at;
                trade.exit_time = signal.timestamp;
                trade.symbol = signal.symbol;
                trade.entry_price = pos_it->avg_price;
                trade.exit_price = execution_price;
                trade.quantity = pos_it->quantity;
                trade.side = "long";
                trade.fees = transaction_costs;
                trade.calculate_pnl();
                
                context.completed_trades.push_back(trade);
                context.available_capital += (pos_it->quantity * execution_price - transaction_costs);
                context.positions.erase(pos_it);
                
            } else if (config_.allow_short_selling) {
                // Open short position - simplified implementation
                Position position("default_exchange", signal.symbol, signal.quantity, signal.price);
                
                context.positions.push_back(position);
                context.available_capital -= transaction_costs; // Only costs for short
            }
        }
        
        // Update total portfolio value - simplified
        context.total_portfolio_value = context.available_capital;
        for (const auto& position : context.positions) {
            context.total_portfolio_value += position.quantity * market_data.close_price;
        }
        
        context.processed_signals++;
        return true;
        
    } catch (const std::exception& e) {
        Logger::error("Failed to execute signal: {}", e.what());
        return false;
    }
}

void BacktestEngine::update_positions(const MarketDataPoint& market_data, 
                                     ExecutionContext& context) {
    for (auto& position : context.positions) {
        if (position.symbol == market_data.symbol && position.exchange == market_data.exchange) {
            position.update_unrealized_pnl(market_data.close_price);
            
            if (config_.detailed_logging) {
                log_position_update(position, market_data);
            }
        }
    }
}

void BacktestEngine::check_stop_losses_and_take_profits(const MarketDataPoint& market_data,
                                                       ExecutionContext& context) {
    auto it = context.positions.begin();
    while (it != context.positions.end()) {
        bool should_close = false;
        double exit_price = market_data.close_price;
        
        if (it->symbol == market_data.symbol && it->exchange == market_data.exchange) {
            // Check stop loss
            if (it->stop_loss > 0.0) {
                if ((it->side == "long" && market_data.close_price <= it->stop_loss) ||
                    (it->side == "short" && market_data.close_price >= it->stop_loss)) {
                    should_close = true;
                    exit_price = it->stop_loss;
                }
            }
            
            // Check take profit
            if (it->take_profit > 0.0 && !should_close) {
                if ((it->side == "long" && market_data.close_price >= it->take_profit) ||
                    (it->side == "short" && market_data.close_price <= it->take_profit)) {
                    should_close = true;
                    exit_price = it->take_profit;
                }
            }
        }
        
        if (should_close) {
            // Create trade result
            TradeResult trade;
            trade.entry_time = it->entry_time;
            trade.exit_time = market_data.timestamp;
            trade.symbol = it->symbol;
            trade.exchange = it->exchange;
            trade.entry_price = it->entry_price;
            trade.exit_price = exit_price;
            trade.quantity = it->quantity;
            trade.side = it->side;
            trade.fees = calculate_transaction_costs(it->quantity * exit_price);
            trade.calculate_pnl();
            
            context.completed_trades.push_back(trade);
            
            // Return capital
            if (it->side == "long") {
                context.available_capital += (it->quantity * exit_price - trade.fees);
            }
            
            it = context.positions.erase(it);
        } else {
            ++it;
        }
    }
}

double BacktestEngine::calculate_position_size(const TradeSignal& signal, double available_capital) {
    double max_position_value = available_capital * config_.max_position_size;
    double position_value = std::min(max_position_value, signal.price * 1000.0); // Default quantity logic
    return position_value / signal.price;
}

double BacktestEngine::calculate_transaction_costs(double trade_value) {
    return trade_value * (config_.commission_rate + config_.spread_cost);
}

double BacktestEngine::apply_slippage(double target_price, ats::types::SignalType signal_type) {
    using ats::types::SignalType;
    double slippage_factor = 1.0 + (signal_type == SignalType::BUY ? config_.slippage_rate : -config_.slippage_rate);
    return target_price * slippage_factor;
}

bool BacktestEngine::check_risk_limits(const TradeSignal& signal, const ExecutionContext& context) {
    // Check position limit
    if (exceeds_position_limit(signal, context)) {
        return false;
    }
    
    // Check exposure limit
    if (exceeds_exposure_limit(signal, context)) {
        return false;
    }
    
    return true;
}

bool BacktestEngine::exceeds_position_limit(const TradeSignal& signal, const ExecutionContext& context) {
    double position_value = signal.price * 1000.0; // Simplified calculation
    double max_position_value = context.total_portfolio_value * config_.max_position_size;
    return position_value > max_position_value;
}

bool BacktestEngine::exceeds_exposure_limit(const TradeSignal& signal, const ExecutionContext& context) {
    double current_exposure = 0.0;
    for (const auto& position : context.positions) {
        current_exposure += position.quantity * position.entry_price;
    }
    
    double max_exposure = context.total_portfolio_value * config_.max_total_exposure;
    double new_position_value = signal.price * 1000.0; // Simplified calculation
    
    return (current_exposure + new_position_value) > max_exposure;
}

void BacktestEngine::preprocess_market_data() {
    if (market_data_.empty()) return;
    
    // Sort by timestamp
    std::sort(market_data_.begin(), market_data_.end(),
              [](const MarketDataPoint& a, const MarketDataPoint& b) {
                  return a.timestamp < b.timestamp;
              });
    
    // Group by symbol for efficient lookup
    symbol_data_.clear();
    for (const auto& data_point : market_data_) {
        symbol_data_[data_point.symbol].push_back(data_point);
    }
    
    Logger::info("Preprocessed {} data points for {} symbols", 
             market_data_.size(), symbol_data_.size());
}

std::vector<MarketDataPoint> BacktestEngine::get_historical_window(
    const std::string& symbol, 
    std::chrono::system_clock::time_point end_time,
    int window_size) const {
    
    std::vector<MarketDataPoint> window;
    
    auto it = symbol_data_.find(symbol);
    if (it == symbol_data_.end()) {
        return window;
    }
    
    // Find data points before end_time
    for (auto rit = it->second.rbegin(); rit != it->second.rend(); ++rit) {
        if (rit->timestamp < end_time) {
            window.insert(window.begin(), *rit);
            if (static_cast<int>(window.size()) >= window_size) {
                break;
            }
        }
    }
    
    return window;
}

void BacktestEngine::update_progress(const BacktestProgress& progress) {
    if (progress_callback_) {
        progress_callback_(progress);
    }
}

std::chrono::milliseconds BacktestEngine::estimate_remaining_time(double progress_percentage,
                                                                 std::chrono::milliseconds elapsed) {
    if (progress_percentage <= 0.0) {
        return std::chrono::milliseconds(0);
    }
    
    double remaining_percentage = 100.0 - progress_percentage;
    double time_per_percent = static_cast<double>(elapsed.count()) / progress_percentage;
    
    return std::chrono::milliseconds(static_cast<long long>(remaining_percentage * time_per_percent));
}

bool BacktestEngine::validate_config() const {
    if (config_.initial_capital <= 0.0) {
        Logger::error("Initial capital must be positive");
        return false;
    }
    
    if (config_.max_position_size <= 0.0 || config_.max_position_size > 1.0) {
        Logger::error("Max position size must be between 0 and 1");
        return false;
    }
    
    if (config_.max_total_exposure <= 0.0 || config_.max_total_exposure > 1.0) {
        Logger::error("Max total exposure must be between 0 and 1");
        return false;
    }
    
    return true;
}

bool BacktestEngine::validate_data_integrity() {
    if (market_data_.empty()) {
        Logger::error("No market data available for backtesting");
        return false;
    }
    
    // Check for sufficient data points
    if (market_data_.size() < 100) {
        Logger::warn("Limited market data available: {} points", market_data_.size());
    }
    
    return true;
}

bool BacktestEngine::validate_strategies() const {
    if (strategies_.empty()) {
        Logger::error("No strategies configured for backtesting");
        return false;
    }
    
    for (const auto& strategy : strategies_) {
        if (!strategy) {
            Logger::error("Null strategy found");
            return false;
        }
    }
    
    return true;
}

void BacktestEngine::log_trade_execution(const TradeSignal& signal, const TradeResult* result) {
    if (config_.detailed_logging) {
        Logger::debug("Signal executed: {} {} @ {}", 
                  static_cast<int>(signal.type), signal.symbol, signal.price);
    }
}

void BacktestEngine::log_position_update(const Position& position, const MarketDataPoint& data) {
    if (config_.detailed_logging) {
        Logger::debug("Position updated: {} {} P&L: {}", 
                  position.symbol, position.side, position.unrealized_pnl);
    }
}

void BacktestEngine::set_progress_callback(ProgressCallback callback) {
    progress_callback_ = callback;
}

// ArbitrageStrategy Implementation
ArbitrageStrategy::ArbitrageStrategy() {
    strategy_name_ = "Arbitrage";
}

bool ArbitrageStrategy::initialize(const std::unordered_map<std::string, std::string>& parameters) {
    parameters_ = parameters;
    
    auto it = parameters.find("min_spread_threshold");
    if (it != parameters.end()) {
        min_spread_threshold_ = std::stod(it->second);
    }
    
    it = parameters.find("max_position_size");
    if (it != parameters.end()) {
        max_position_size_ = std::stod(it->second);
    }
    
    it = parameters.find("max_hold_time_ms");
    if (it != parameters.end()) {
        max_hold_time_ = std::chrono::milliseconds(std::stoll(it->second));
    }
    
    Logger::info("ArbitrageStrategy initialized with min_spread: {}, max_position: {}", 
             min_spread_threshold_, max_position_size_);
    
    return true;
}

void ArbitrageStrategy::on_market_data(const MarketDataPoint& data) {
    // Update latest prices
    latest_prices_[data.symbol][data.exchange] = data.close_price;
    last_update_time_[data.symbol] = data.timestamp;
}

std::vector<TradeSignal> ArbitrageStrategy::generate_signals(
    const std::vector<MarketDataPoint>& historical_data,
    const MarketDataPoint& current_data) {
    
    std::vector<TradeSignal> signals;
    
    std::string buy_exchange, sell_exchange;
    double spread_percentage;
    
    if (detect_arbitrage_opportunity(current_data, buy_exchange, sell_exchange, spread_percentage)) {
        // Generate buy signal for lower-priced exchange
        TradeSignal buy_signal(current_data.timestamp, current_data.symbol, 
                              buy_exchange, "buy", 
                              latest_prices_[current_data.symbol][buy_exchange]);
        buy_signal.confidence = spread_percentage / min_spread_threshold_;
        buy_signal.reason = "Arbitrage opportunity detected";
        buy_signal.metadata["spread_percentage"] = spread_percentage;
        buy_signal.metadata["target_exchange"] = sell_exchange;
        
        // Generate sell signal for higher-priced exchange
        TradeSignal sell_signal(current_data.timestamp, current_data.symbol,
                               sell_exchange, "sell",
                               latest_prices_[current_data.symbol][sell_exchange]);
        sell_signal.confidence = spread_percentage / min_spread_threshold_;
        sell_signal.reason = "Arbitrage opportunity detected";
        sell_signal.metadata["spread_percentage"] = spread_percentage;
        sell_signal.metadata["source_exchange"] = buy_exchange;
        
        signals.push_back(buy_signal);
        signals.push_back(sell_signal);
    }
    
    return signals;
}

std::vector<std::string> ArbitrageStrategy::get_required_parameters() const {
    return {"min_spread_threshold", "max_position_size", "max_hold_time_ms"};
}

double ArbitrageStrategy::calculate_position_size(const TradeSignal& signal, 
                                                 double available_capital,
                                                 double current_price) {
    double max_value = available_capital * max_position_size_;
    return max_value / current_price;
}

bool ArbitrageStrategy::should_exit_position(const Position& position, 
                                           const MarketDataPoint& current_data) {
    // Exit if holding time exceeded
    auto holding_time = std::chrono::duration_cast<std::chrono::milliseconds>(
        current_data.timestamp - position.entry_time);
    
    return holding_time >= max_hold_time_;
}

bool ArbitrageStrategy::detect_arbitrage_opportunity(const MarketDataPoint& data,
                                                   std::string& buy_exchange,
                                                   std::string& sell_exchange,
                                                   double& spread_percentage) {
    auto symbol_prices = latest_prices_.find(data.symbol);
    if (symbol_prices == latest_prices_.end() || symbol_prices->second.size() < 2) {
        return false;
    }
    
    double min_price = std::numeric_limits<double>::max();
    double max_price = 0.0;
    
    for (const auto& exchange_price : symbol_prices->second) {
        if (exchange_price.second < min_price) {
            min_price = exchange_price.second;
            buy_exchange = exchange_price.first;
        }
        if (exchange_price.second > max_price) {
            max_price = exchange_price.second;
            sell_exchange = exchange_price.first;
        }
    }
    
    if (buy_exchange == sell_exchange || min_price <= 0.0) {
        return false;
    }
    
    spread_percentage = (max_price - min_price) / min_price;
    return spread_percentage >= min_spread_threshold_;
}

} // namespace backtest
} // namespace ats