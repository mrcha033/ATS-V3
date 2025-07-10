#include "spread_calculator.hpp"
#include "utils/logger.hpp"
#include "utils/json_parser.hpp"
#include <nlohmann/json.hpp>
#include <algorithm>
#include <numeric>
#include <cmath>
#include <map>

namespace ats {
namespace trading_engine {

struct SpreadCalculator::Implementation {
    config::ConfigManager config;
    
    // Market data storage
    std::unordered_map<std::string, std::unordered_map<std::string, types::Ticker>> ticker_cache; // exchange -> symbol -> ticker
    std::unordered_map<std::string, std::unordered_map<std::string, MarketDepth>> depth_cache; // exchange -> symbol -> depth
    std::unordered_map<std::string, ExchangeFeeStructure> fee_structures; // exchange -> fees
    std::unordered_map<std::string, std::unordered_map<std::string, SlippageModel>> slippage_models; // exchange -> symbol -> model
    
    // Historical data for analysis
    std::unordered_map<std::string, std::vector<SpreadAnalysis>> spread_history; // symbol -> history
    std::unordered_map<std::string, std::unordered_map<std::string, std::vector<double>>> price_history; // exchange -> symbol -> prices
    
    // Statistics
    std::atomic<size_t> opportunities_detected{0};
    std::atomic<size_t> opportunities_executed{0};
    std::atomic<double> average_profit_margin{0.0};
    
    // Configuration
    double spread_threshold = 0.005; // 0.5%
    double slippage_tolerance = 0.001; // 0.1%
    bool dynamic_fee_calculation = true;
    bool advanced_slippage_modeling = true;
    
    // Thread safety
    mutable std::shared_mutex mutex;
    mutable std::shared_mutex history_mutex;
    mutable std::shared_mutex stats_mutex;
};

SpreadCalculator::SpreadCalculator() : impl_(std::make_unique<Implementation>()) {}

SpreadCalculator::~SpreadCalculator() = default;

bool SpreadCalculator::initialize(const config::ConfigManager& config) {
    impl_->config = config;
    
    // Load configuration
    impl_->spread_threshold = config.get_value<double>("spread_calculator.min_spread_threshold", 0.005);
    impl_->slippage_tolerance = config.get_value<double>("spread_calculator.slippage_tolerance", 0.001);
    impl_->dynamic_fee_calculation = config.get_value<bool>("spread_calculator.dynamic_fee_calculation", true);
    impl_->advanced_slippage_modeling = config.get_value<bool>("spread_calculator.advanced_slippage_modeling", true);
    
    // Initialize default fee structures for common exchanges
    ExchangeFeeStructure binance_fees;
    binance_fees.exchange_id = "binance";
    binance_fees.maker_fee = 0.001;
    binance_fees.taker_fee = 0.001;
    binance_fees.withdrawal_fee = 0.0005;
    
    ExchangeFeeStructure upbit_fees;
    upbit_fees.exchange_id = "upbit";
    upbit_fees.maker_fee = 0.0005;
    upbit_fees.taker_fee = 0.0005;
    upbit_fees.withdrawal_fee = 0.001;
    
    impl_->fee_structures["binance"] = binance_fees;
    impl_->fee_structures["upbit"] = upbit_fees;
    
    utils::Logger::info("SpreadCalculator initialized successfully");
    return true;
}

void SpreadCalculator::update_fee_structures(const std::unordered_map<std::string, ExchangeFeeStructure>& fees) {
    std::unique_lock<std::shared_mutex> lock(impl_->mutex);
    
    for (const auto& [exchange_id, fee_structure] : fees) {
        impl_->fee_structures[exchange_id] = fee_structure;
        utils::Logger::debug("Updated fee structure for exchange: {}", exchange_id);
    }
}

void SpreadCalculator::update_slippage_models(const std::unordered_map<std::string, SlippageModel>& models) {
    std::unique_lock<std::shared_mutex> lock(impl_->mutex);
    
    for (const auto& [key, model] : models) {
        // Extract exchange and symbol from key (format: "exchange:symbol")
        size_t colon_pos = key.find(':');
        if (colon_pos != std::string::npos) {
            std::string exchange = key.substr(0, colon_pos);
            std::string symbol = key.substr(colon_pos + 1);
            
            impl_->slippage_models[exchange][symbol] = model;
            utils::Logger::debug("Updated slippage model for {}:{}", exchange, symbol);
        }
    }
}

void SpreadCalculator::update_market_depth(const MarketDepth& depth) {
    std::unique_lock<std::shared_mutex> lock(impl_->mutex);
    
    if (is_valid_market_depth(depth)) {
        impl_->depth_cache[depth.exchange][depth.symbol] = depth;
        utils::Logger::debug("Updated market depth for {}:{}", depth.exchange, depth.symbol);
    }
}

void SpreadCalculator::update_ticker(const types::Ticker& ticker) {
    std::unique_lock<std::shared_mutex> lock(impl_->mutex);
    
    if (is_valid_ticker(ticker)) {
        impl_->ticker_cache[ticker.exchange][ticker.symbol] = ticker;
        
        // Update price history
        impl_->price_history[ticker.exchange][ticker.symbol].push_back(ticker.last);
        
        // Keep only recent price history (last 1000 prices)
        auto& history = impl_->price_history[ticker.exchange][ticker.symbol];
        if (history.size() > 1000) {
            history.erase(history.begin(), history.begin() + 100);
        }
        
        utils::Logger::debug("Updated ticker for {}:{} - last: {}", 
                           ticker.exchange, ticker.symbol, ticker.last);
    }
}

SpreadAnalysis SpreadCalculator::analyze_spread(const std::string& symbol,
                                               const std::string& buy_exchange,
                                               const std::string& sell_exchange,
                                               double quantity) const {
    std::shared_lock<std::shared_mutex> lock(impl_->mutex);
    
    SpreadAnalysis analysis;
    analysis.symbol = symbol;
    analysis.buy_exchange = buy_exchange;
    analysis.sell_exchange = sell_exchange;
    
    // Get ticker data
    auto buy_ticker_it = impl_->ticker_cache.find(buy_exchange);
    auto sell_ticker_it = impl_->ticker_cache.find(sell_exchange);
    
    if (buy_ticker_it == impl_->ticker_cache.end() ||
        sell_ticker_it == impl_->ticker_cache.end() ||
        buy_ticker_it->second.find(symbol) == buy_ticker_it->second.end() ||
        sell_ticker_it->second.find(symbol) == sell_ticker_it->second.end()) {
        
        analysis.analysis_notes = "Missing ticker data";
        return analysis;
    }
    
    const auto& buy_ticker = buy_ticker_it->second.at(symbol);
    const auto& sell_ticker = sell_ticker_it->second.at(symbol);
    
    // Calculate raw spread
    analysis.raw_spread = calculate_raw_spread(buy_ticker, sell_ticker);
    analysis.spread_percentage = (analysis.raw_spread / buy_ticker.ask) * 100.0;
    
    // Calculate fees
    double total_fees = calculate_total_fees(buy_exchange, sell_exchange, symbol, 
                                           quantity, buy_ticker.ask, sell_ticker.bid);
    
    // Estimate slippage
    double buy_slippage = estimate_slippage(buy_exchange, symbol, quantity, types::OrderSide::BUY);
    double sell_slippage = estimate_slippage(sell_exchange, symbol, quantity, types::OrderSide::SELL);
    double total_slippage = (buy_slippage + sell_slippage) * quantity;
    
    // Calculate effective spread (after fees and slippage)
    analysis.effective_spread = analysis.raw_spread - total_fees - total_slippage;
    
    // Calculate breakeven spread
    analysis.breakeven_spread = calculate_breakeven_spread(buy_exchange, sell_exchange, symbol, quantity);
    
    // Calculate profit margin
    analysis.profit_margin = analysis.effective_spread * quantity;
    
    // Determine if profitable
    analysis.is_profitable = analysis.profit_margin > 0 && 
                            analysis.spread_percentage >= impl_->spread_threshold;
    
    // Calculate confidence score
    analysis.confidence_score = calculate_confidence_score_internal(buy_ticker, sell_ticker, quantity);
    
    if (analysis.is_profitable) {
        analysis.analysis_notes = "Profitable opportunity detected";
    } else {
        analysis.analysis_notes = "Not profitable after fees and slippage";
    }
    
    return analysis;
}

std::vector<SpreadAnalysis> SpreadCalculator::find_best_opportunities(const std::string& symbol,
                                                                     double min_spread_threshold) const {
    std::shared_lock<std::shared_mutex> lock(impl_->mutex);
    
    std::vector<SpreadAnalysis> opportunities;
    
    // Find all exchanges that have this symbol
    std::vector<std::string> exchanges;
    for (const auto& [exchange, symbols] : impl_->ticker_cache) {
        if (symbols.find(symbol) != symbols.end()) {
            exchanges.push_back(exchange);
        }
    }
    
    // Compare all exchange pairs
    for (size_t i = 0; i < exchanges.size(); ++i) {
        for (size_t j = i + 1; j < exchanges.size(); ++j) {
            const std::string& exchange1 = exchanges[i];
            const std::string& exchange2 = exchanges[j];
            
            // Analyze both directions
            double quantity = 1.0; // Default quantity for analysis
            
            auto analysis1 = analyze_spread(symbol, exchange1, exchange2, quantity);
            if (analysis1.spread_percentage >= min_spread_threshold) {
                opportunities.push_back(analysis1);
            }
            
            auto analysis2 = analyze_spread(symbol, exchange2, exchange1, quantity);
            if (analysis2.spread_percentage >= min_spread_threshold) {
                opportunities.push_back(analysis2);
            }
        }
    }
    
    // Sort by profit margin descending
    std::sort(opportunities.begin(), opportunities.end(),
              [](const SpreadAnalysis& a, const SpreadAnalysis& b) {
                  return a.profit_margin > b.profit_margin;
              });
    
    return opportunities;
}

std::vector<ArbitrageOpportunity> SpreadCalculator::detect_arbitrage_opportunities(
    double min_profit_threshold) const {
    
    std::shared_lock<std::shared_mutex> lock(impl_->mutex);
    
    std::vector<ArbitrageOpportunity> opportunities;
    
    // Get all unique symbols across all exchanges
    std::set<std::string> all_symbols;
    for (const auto& [exchange, symbols] : impl_->ticker_cache) {
        for (const auto& [symbol, ticker] : symbols) {
            all_symbols.insert(symbol);
        }
    }
    
    // Analyze each symbol
    for (const std::string& symbol : all_symbols) {
        auto spread_opportunities = find_best_opportunities(symbol, impl_->spread_threshold);
        
        for (const auto& spread_analysis : spread_opportunities) {
            if (spread_analysis.profit_margin >= min_profit_threshold) {
                ArbitrageOpportunity opportunity;
                opportunity.symbol = spread_analysis.symbol;
                opportunity.buy_exchange = spread_analysis.buy_exchange;
                opportunity.sell_exchange = spread_analysis.sell_exchange;
                
                // Get current prices
                const auto& buy_ticker = impl_->ticker_cache.at(spread_analysis.buy_exchange).at(symbol);
                const auto& sell_ticker = impl_->ticker_cache.at(spread_analysis.sell_exchange).at(symbol);
                
                opportunity.buy_price = buy_ticker.ask;
                opportunity.sell_price = sell_ticker.bid;
                opportunity.spread_percentage = spread_analysis.spread_percentage;
                opportunity.expected_profit = spread_analysis.profit_margin;
                opportunity.confidence_score = spread_analysis.confidence_score;
                opportunity.detected_at = std::chrono::system_clock::now();
                
                // Estimate available quantity based on order book depth
                opportunity.available_quantity = std::min(buy_ticker.volume, sell_ticker.volume) * 0.01; // 1% of volume
                opportunity.available_quantity = std::min(opportunity.available_quantity, 10.0); // Max 10 units
                
                // Calculate fees and slippage
                opportunity.total_fees = calculate_total_fees(opportunity.buy_exchange, 
                                                            opportunity.sell_exchange,
                                                            opportunity.symbol,
                                                            opportunity.available_quantity,
                                                            opportunity.buy_price,
                                                            opportunity.sell_price);
                
                opportunity.estimated_slippage = estimate_slippage(opportunity.buy_exchange, symbol, 
                                                                 opportunity.available_quantity, types::OrderSide::BUY) +
                                               estimate_slippage(opportunity.sell_exchange, symbol,
                                                                 opportunity.available_quantity, types::OrderSide::SELL);
                
                // Risk assessment
                opportunity.max_position_size = opportunity.available_quantity;
                opportunity.risk_approved = true; // Simplified - would normally check with risk manager
                
                enrich_opportunity(opportunity);
                
                if (validate_opportunity(opportunity)) {
                    opportunities.push_back(opportunity);
                }
            }
        }
    }
    
    // Update statistics
    const_cast<SpreadCalculator*>(this)->impl_->opportunities_detected += opportunities.size();
    
    return opportunities;
}

double SpreadCalculator::calculate_trading_fee(const std::string& exchange, const std::string& symbol,
                                             double quantity, double price, bool is_maker) const {
    std::shared_lock<std::shared_mutex> lock(impl_->mutex);
    
    auto fee_it = impl_->fee_structures.find(exchange);
    if (fee_it == impl_->fee_structures.end()) {
        return quantity * price * 0.001; // Default 0.1% fee
    }
    
    const auto& fee_structure = fee_it->second;
    double fee_rate = is_maker ? fee_structure.maker_fee : fee_structure.taker_fee;
    
    // Check for symbol-specific fees
    auto symbol_fee_it = fee_structure.symbol_specific_fees.find(symbol);
    if (symbol_fee_it != fee_structure.symbol_specific_fees.end()) {
        fee_rate = symbol_fee_it->second;
    }
    
    // Apply volume-based discounts if enabled
    if (impl_->dynamic_fee_calculation && fee_structure.has_volume_tiers) {
        // In a real implementation, would track user's trading volume
        double user_volume = 1000000.0; // Placeholder
        fee_rate = get_volume_based_fee(exchange, user_volume);
    }
    
    return quantity * price * fee_rate;
}

double SpreadCalculator::calculate_total_fees(const std::string& buy_exchange, 
                                            const std::string& sell_exchange,
                                            const std::string& symbol, 
                                            double quantity, 
                                            double buy_price, 
                                            double sell_price) const {
    double buy_fee = calculate_trading_fee(buy_exchange, symbol, quantity, buy_price, false);
    double sell_fee = calculate_trading_fee(sell_exchange, symbol, quantity, sell_price, false);
    
    return buy_fee + sell_fee;
}

double SpreadCalculator::estimate_slippage(const std::string& exchange, const std::string& symbol,
                                         double quantity, types::OrderSide side) const {
    std::shared_lock<std::shared_mutex> lock(impl_->mutex);
    
    // Check if we have a specific slippage model
    auto exchange_it = impl_->slippage_models.find(exchange);
    if (exchange_it != impl_->slippage_models.end()) {
        auto symbol_it = exchange_it->second.find(symbol);
        if (symbol_it != exchange_it->second.end()) {
            const auto& model = symbol_it->second;
            
            if (impl_->advanced_slippage_modeling) {
                // Use order book data if available
                auto depth_it = impl_->depth_cache.find(exchange);
                if (depth_it != impl_->depth_cache.end()) {
                    auto symbol_depth_it = depth_it->second.find(symbol);
                    if (symbol_depth_it != depth_it->second.end()) {
                        return calculate_nonlinear_slippage(model, quantity, symbol_depth_it->second);
                    }
                }
            }
            
            return calculate_linear_slippage(model, quantity);
        }
    }
    
    // Default slippage estimate
    return quantity * 0.001; // 0.1% default slippage
}

double SpreadCalculator::calculate_breakeven_spread(const std::string& buy_exchange, 
                                                  const std::string& sell_exchange,
                                                  const std::string& symbol, 
                                                  double quantity) const {
    // Calculate total costs
    double total_fees = calculate_total_fees(buy_exchange, sell_exchange, symbol, 
                                           quantity, 100.0, 100.0); // Use 100 as reference price
    
    double buy_slippage = estimate_slippage(buy_exchange, symbol, quantity, types::OrderSide::BUY);
    double sell_slippage = estimate_slippage(sell_exchange, symbol, quantity, types::OrderSide::SELL);
    
    // Breakeven spread = (total_fees + total_slippage) / quantity
    return (total_fees + (buy_slippage + sell_slippage) * quantity) / quantity;
}

double SpreadCalculator::calculate_risk_adjusted_profit(const ArbitrageOpportunity& opportunity) const {
    double raw_profit = opportunity.expected_profit;
    
    // Apply risk adjustments
    double confidence_adjustment = opportunity.confidence_score;
    double volatility_adjustment = calculate_volatility_adjustment(opportunity.symbol);
    double liquidity_adjustment = calculate_liquidity_adjustment(opportunity);
    
    return raw_profit * confidence_adjustment * volatility_adjustment * liquidity_adjustment;
}

double SpreadCalculator::calculate_confidence_score(const ArbitrageOpportunity& opportunity) const {
    std::shared_lock<std::shared_mutex> lock(impl_->mutex);
    
    double score = 1.0;
    
    // Check data freshness
    auto buy_ticker_it = impl_->ticker_cache.find(opportunity.buy_exchange);
    auto sell_ticker_it = impl_->ticker_cache.find(opportunity.sell_exchange);
    
    if (buy_ticker_it != impl_->ticker_cache.end() && sell_ticker_it != impl_->ticker_cache.end()) {
        auto buy_symbol_it = buy_ticker_it->second.find(opportunity.symbol);
        auto sell_symbol_it = sell_ticker_it->second.find(opportunity.symbol);
        
        if (buy_symbol_it != buy_ticker_it->second.end() && 
            sell_symbol_it != sell_ticker_it->second.end()) {
            
            const auto& buy_ticker = buy_symbol_it->second;
            const auto& sell_ticker = sell_symbol_it->second;
            
            return calculate_confidence_score_internal(buy_ticker, sell_ticker, opportunity.available_quantity);
        }
    }
    
    return score * 0.5; // Reduce confidence if data is missing
}

size_t SpreadCalculator::get_opportunities_detected() const {
    return impl_->opportunities_detected;
}

size_t SpreadCalculator::get_opportunities_executed() const {
    return impl_->opportunities_executed;
}

double SpreadCalculator::get_average_profit_margin() const {
    return impl_->average_profit_margin;
}

void SpreadCalculator::set_spread_threshold(double threshold) {
    std::unique_lock<std::shared_mutex> lock(impl_->mutex);
    impl_->spread_threshold = threshold;
}

void SpreadCalculator::set_slippage_tolerance(double tolerance) {
    std::unique_lock<std::shared_mutex> lock(impl_->mutex);
    impl_->slippage_tolerance = tolerance;
}

// Private helper methods

double SpreadCalculator::calculate_raw_spread(const types::Ticker& buy_ticker, 
                                            const types::Ticker& sell_ticker) const {
    return sell_ticker.bid - buy_ticker.ask;
}

double SpreadCalculator::calculate_linear_slippage(const SlippageModel& model, double quantity) const {
    return model.base_slippage + (model.market_impact_coefficient * quantity);
}

double SpreadCalculator::calculate_nonlinear_slippage(const SlippageModel& model, double quantity,
                                                    const MarketDepth& depth) const {
    // Calculate order book impact
    double order_book_impact = calculate_order_book_impact(depth, quantity, types::OrderSide::BUY);
    
    // Combine with linear model
    double linear_slippage = calculate_linear_slippage(model, quantity);
    
    return linear_slippage + (order_book_impact * model.liquidity_factor);
}

double SpreadCalculator::calculate_order_book_impact(const MarketDepth& depth, double quantity, 
                                                   types::OrderSide side) const {
    const auto& levels = (side == types::OrderSide::BUY) ? depth.asks : depth.bids;
    
    if (levels.empty()) {
        return 0.01; // 1% impact if no order book data
    }
    
    double remaining_quantity = quantity;
    double weighted_price = 0.0;
    double total_quantity = 0.0;
    
    for (const auto& [price, available_qty] : levels) {
        if (remaining_quantity <= 0) break;
        
        double qty_to_take = std::min(remaining_quantity, available_qty);
        weighted_price += price * qty_to_take;
        total_quantity += qty_to_take;
        remaining_quantity -= qty_to_take;
    }
    
    if (total_quantity > 0) {
        double average_price = weighted_price / total_quantity;
        double reference_price = levels[0].first; // Best price
        return std::abs(average_price - reference_price) / reference_price;
    }
    
    return 0.01; // Default 1% impact
}

double SpreadCalculator::get_volume_based_fee(const std::string& exchange, double trading_volume) const {
    auto fee_it = impl_->fee_structures.find(exchange);
    if (fee_it == impl_->fee_structures.end()) {
        return 0.001; // Default fee
    }
    
    const auto& fee_structure = fee_it->second;
    if (!fee_structure.has_volume_tiers || fee_structure.volume_tiers.empty()) {
        return fee_structure.taker_fee;
    }
    
    // Find appropriate tier
    for (const auto& [volume_threshold, fee_rate] : fee_structure.volume_tiers) {
        if (trading_volume >= volume_threshold) {
            return fee_rate;
        }
    }
    
    return fee_structure.taker_fee; // Default if no tier matches
}

bool SpreadCalculator::is_valid_market_depth(const MarketDepth& depth) const {
    return !depth.symbol.empty() && 
           !depth.exchange.empty() && 
           !depth.bids.empty() && 
           !depth.asks.empty();
}

bool SpreadCalculator::is_valid_ticker(const types::Ticker& ticker) const {
    return !ticker.symbol.empty() && 
           !ticker.exchange.empty() && 
           ticker.bid > 0 && 
           ticker.ask > 0 && 
           ticker.ask >= ticker.bid;
}

bool SpreadCalculator::validate_opportunity(const ArbitrageOpportunity& opportunity) const {
    return !opportunity.symbol.empty() &&
           !opportunity.buy_exchange.empty() &&
           !opportunity.sell_exchange.empty() &&
           opportunity.buy_exchange != opportunity.sell_exchange &&
           opportunity.available_quantity > 0 &&
           opportunity.buy_price > 0 &&
           opportunity.sell_price > 0 &&
           opportunity.sell_price > opportunity.buy_price &&
           opportunity.expected_profit > 0;
}

void SpreadCalculator::enrich_opportunity(ArbitrageOpportunity& opportunity) const {
    // Set reasonable validity window
    opportunity.validity_window = std::chrono::milliseconds(5000);
    
    // Calculate additional metrics
    opportunity.confidence_score = calculate_confidence_score(opportunity);
    
    // Set risk parameters
    opportunity.max_position_size = opportunity.available_quantity * 0.8; // 80% of available
}

double SpreadCalculator::calculate_confidence_score_internal(const types::Ticker& buy_ticker, 
                                                           const types::Ticker& sell_ticker,
                                                           double quantity) const {
    double score = 1.0;
    
    // Data freshness factor
    auto now = std::chrono::system_clock::now();
    auto buy_age = std::chrono::duration_cast<std::chrono::seconds>(now - buy_ticker.timestamp);
    auto sell_age = std::chrono::duration_cast<std::chrono::seconds>(now - sell_ticker.timestamp);
    
    if (buy_age > std::chrono::seconds(30) || sell_age > std::chrono::seconds(30)) {
        score *= 0.8; // Reduce confidence for stale data
    }
    
    // Volume factor
    double min_volume = std::min(buy_ticker.volume, sell_ticker.volume);
    if (quantity > min_volume * 0.1) {
        score *= 0.7; // Reduce confidence if quantity is large relative to volume
    }
    
    // Spread stability (simplified)
    if (buy_ticker.high - buy_ticker.low > buy_ticker.last * 0.05) {
        score *= 0.9; // Reduce confidence in highly volatile markets
    }
    
    return std::max(0.1, std::min(1.0, score));
}

double SpreadCalculator::calculate_volatility_adjustment(const std::string& symbol) const {
    // Simplified volatility calculation
    return 0.95; // 5% volatility discount
}

double SpreadCalculator::calculate_liquidity_adjustment(const ArbitrageOpportunity& opportunity) const {
    // Simplified liquidity adjustment based on quantity
    if (opportunity.available_quantity > 10.0) {
        return 0.9; // Large quantities have liquidity risk
    }
    return 1.0;
}

// Utility functions implementation
namespace spread_utils {

double calculate_percentage_spread(double buy_price, double sell_price) {
    if (buy_price <= 0) return 0.0;
    return ((sell_price - buy_price) / buy_price) * 100.0;
}

double calculate_absolute_spread(double buy_price, double sell_price) {
    return sell_price - buy_price;
}

double calculate_mid_price(double bid, double ask) {
    return (bid + ask) / 2.0;
}

double calculate_weighted_average_price(const std::vector<std::pair<double, double>>& levels,
                                      double quantity) {
    if (levels.empty() || quantity <= 0) return 0.0;
    
    double remaining_quantity = quantity;
    double weighted_price = 0.0;
    double total_quantity = 0.0;
    
    for (const auto& [price, available_qty] : levels) {
        if (remaining_quantity <= 0) break;
        
        double qty_to_take = std::min(remaining_quantity, available_qty);
        weighted_price += price * qty_to_take;
        total_quantity += qty_to_take;
        remaining_quantity -= qty_to_take;
    }
    
    return total_quantity > 0 ? weighted_price / total_quantity : 0.0;
}

double calculate_order_book_depth(const std::vector<std::pair<double, double>>& levels,
                                double price_range_percentage) {
    if (levels.empty()) return 0.0;
    
    double reference_price = levels[0].first;
    double price_range = reference_price * price_range_percentage;
    double total_depth = 0.0;
    
    for (const auto& [price, quantity] : levels) {
        if (std::abs(price - reference_price) <= price_range) {
            total_depth += quantity;
        } else {
            break;
        }
    }
    
    return total_depth;
}

bool is_reasonable_spread(double spread_percentage) {
    return spread_percentage >= -10.0 && spread_percentage <= 50.0;
}

bool is_reasonable_price(double price) {
    return price > 0 && price < 1000000.0; // Basic sanity check
}

bool is_reasonable_quantity(double quantity) {
    return quantity > 0 && quantity <= 1000000.0; // Basic sanity check
}

std::string format_spread_percentage(double spread) {
    std::ostringstream oss;
    oss << std::fixed << std::setprecision(3) << spread << "%";
    return oss.str();
}

std::string format_profit_amount(double profit, const std::string& currency) {
    std::ostringstream oss;
    oss << std::fixed << std::setprecision(2) << profit << " " << currency;
    return oss.str();
}

} // namespace spread_utils

} // namespace trading_engine
} // namespace ats