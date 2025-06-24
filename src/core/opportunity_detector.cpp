#include "opportunity_detector.hpp"
#include "../utils/config_manager.hpp"
#include "../utils/logger.hpp"
#include "price_monitor.hpp"

#include <algorithm>

namespace ats {

OpportunityDetector::OpportunityDetector(ConfigManager* config_manager, PriceMonitor* price_monitor)
    : config_manager_(config_manager), price_monitor_(price_monitor), running_(false),
      max_opportunity_history_(100), opportunities_detected_(0), opportunities_filtered_(0),
      valid_opportunities_(0), avg_detection_time_ms_(0.0) {
}

OpportunityDetector::~OpportunityDetector() {
    Stop();
}

bool OpportunityDetector::Initialize() {
    try {
        LOG_INFO("Initializing Opportunity Detector...");
        
        // Load configuration
        config_.min_profit_threshold = 0.5;
        config_.min_profit_after_fees = 0.2;
        config_.max_execution_risk = 0.3;
        config_.min_volume_usd = 100.0;
        config_.max_volume_usd = 10000.0;
        
        // Set default exchange fees (these would come from config in production)
        config_.exchange_fees["binance"] = 0.001;   // 0.1%
        config_.exchange_fees["upbit"] = 0.0025;    // 0.25%
        config_.withdrawal_fees["binance"] = 0.0005;
        config_.withdrawal_fees["upbit"] = 0.001;
        
        LOG_INFO("Opportunity Detector initialized with {:.1f}% min profit threshold", 
                config_.min_profit_threshold);
        return true;
        
    } catch (const std::exception& e) {
        LOG_ERROR("Failed to initialize Opportunity Detector: {}", e.what());
        return false;
    }
}

void OpportunityDetector::Start() {
    if (running_.load()) {
        LOG_WARNING("Opportunity Detector is already running");
        return;
    }
    
    running_ = true;
    detection_thread_ = std::thread(&OpportunityDetector::DetectionLoop, this);
    LOG_INFO("Opportunity Detector started");
}

void OpportunityDetector::Stop() {
    if (!running_.load()) {
        return;
    }
    
    running_ = false;
    
    if (detection_thread_.joinable()) {
        detection_thread_.join();
    }
    
    LOG_INFO("Opportunity Detector stopped");
}

std::vector<ArbitrageOpportunity> OpportunityDetector::DetectOpportunities(const std::string& symbol) {
    if (!price_monitor_) {
        return {};
    }
    
    auto comparison = price_monitor_->ComparePrices(symbol);
    std::vector<ArbitrageOpportunity> opportunities;
    
    if (comparison.HasArbitrageOpportunity(config_.min_profit_threshold)) {
        auto opportunity = EvaluateOpportunity(symbol, comparison);
        if (ValidateOpportunity(opportunity)) {
            opportunities.push_back(opportunity);
        }
    }
    
    return opportunities;
}

ArbitrageOpportunity OpportunityDetector::AnalyzePair(const std::string& symbol, 
                                                     const std::string& exchange1, 
                                                     const std::string& exchange2) {
    ArbitrageOpportunity opportunity;
    opportunity.symbol = symbol;
    
    if (!price_monitor_) {
        return opportunity;
    }
    
    Price price1, price2;
    if (!price_monitor_->GetLatestPrice(exchange1, symbol, price1) ||
        !price_monitor_->GetLatestPrice(exchange2, symbol, price2)) {
        return opportunity;
    }
    
    // Determine buy and sell exchanges
    if (price1.ask < price2.bid) {
        opportunity.buy_exchange = exchange1;
        opportunity.sell_exchange = exchange2;
        opportunity.buy_price = price1.ask;
        opportunity.sell_price = price2.bid;
    } else if (price2.ask < price1.bid) {
        opportunity.buy_exchange = exchange2;
        opportunity.sell_exchange = exchange1;
        opportunity.buy_price = price2.ask;
        opportunity.sell_price = price1.bid;
    } else {
        return opportunity; // No arbitrage opportunity
    }
    
    // Calculate profit
    opportunity.profit_absolute = opportunity.sell_price - opportunity.buy_price;
    opportunity.profit_percent = (opportunity.profit_absolute / opportunity.buy_price) * 100.0;
    
    // Estimate maximum volume (simplified)
    opportunity.max_volume = std::min(price1.volume, price2.volume);
    
    // Validate the opportunity
    ValidateOpportunity(opportunity);
    
    return opportunity;
}

bool OpportunityDetector::IsHealthy() const {
    return running_.load() && price_monitor_ && price_monitor_->IsHealthy();
}

std::string OpportunityDetector::GetStatus() const {
    if (!running_.load()) {
        return "STOPPED";
    }
    
    if (IsHealthy()) {
        return "DETECTING";
    }
    
    return "UNHEALTHY";
}

double OpportunityDetector::GetDetectionRate() const {
    std::lock_guard<std::mutex> lock(detection_rate_mutex_);
    
    if (detection_timestamps_.empty()) {
        return 0.0;
    }
    
    auto now = std::chrono::steady_clock::now();
    auto one_minute_ago = now - std::chrono::minutes(1);
    
    // Count detections in the last minute
    size_t recent_detections = 0;
    for (const auto& timestamp : detection_timestamps_) {
        if (timestamp >= one_minute_ago) {
            recent_detections++;
        }
    }
    
    // Calculate rate as detections per second
    return static_cast<double>(recent_detections) / 60.0;
}

double OpportunityDetector::GetValidationRate() const {
    long long total = opportunities_detected_.load();
    if (total == 0) return 0.0;
    return static_cast<double>(valid_opportunities_.load()) / total * 100.0;
}

void OpportunityDetector::LogStatistics() const {
    LOG_INFO("=== Opportunity Detector Statistics ===");
    LOG_INFO("Opportunities detected: {}", opportunities_detected_.load());
    LOG_INFO("Opportunities filtered: {}", opportunities_filtered_.load());
    LOG_INFO("Valid opportunities: {}", valid_opportunities_.load());
    LOG_INFO("Validation rate: {:.1f}%", GetValidationRate());
    LOG_INFO("Average detection time: {:.1f}ms", avg_detection_time_ms_.load());
}

void OpportunityDetector::ResetStatistics() {
    opportunities_detected_ = 0;
    opportunities_filtered_ = 0;
    valid_opportunities_ = 0;
    avg_detection_time_ms_ = 0.0;
}

void OpportunityDetector::DetectionLoop() {
    LOG_INFO("Opportunity detection loop started");
    
    while (running_.load()) {
        try {
            auto start_time = std::chrono::steady_clock::now();
            
            // Scan for opportunities
            auto opportunities = ScanAllPairs();
            
            // Process each opportunity
            for (const auto& opportunity : opportunities) {
                if (opportunity_callback_) {
                    opportunity_callback_(opportunity);
                }
                RecordOpportunity(opportunity);
            }
            
            auto end_time = std::chrono::steady_clock::now();
            auto duration = std::chrono::duration_cast<std::chrono::milliseconds>(end_time - start_time);
            avg_detection_time_ms_ = static_cast<double>(duration.count());
            
            std::this_thread::sleep_for(config_.detection_interval);
            
        } catch (const std::exception& e) {
            LOG_ERROR("Error in detection loop: {}", e.what());
            std::this_thread::sleep_for(std::chrono::seconds(1));
        }
    }
    
    LOG_INFO("Opportunity detection loop stopped");
}

std::vector<ArbitrageOpportunity> OpportunityDetector::ScanAllPairs() {
    std::vector<ArbitrageOpportunity> opportunities;
    
    if (!price_monitor_) {
        return opportunities;
    }
    
    auto symbols = GetMonitoredSymbols();
    auto exchanges = GetActiveExchanges();
    
    for (const auto& symbol : symbols) {
        auto comparison = price_monitor_->ComparePrices(symbol);
        
        if (comparison.HasArbitrageOpportunity(config_.min_profit_threshold)) {
            auto opportunity = EvaluateOpportunity(symbol, comparison);
            if (ValidateOpportunity(opportunity)) {
                opportunities.push_back(opportunity);
            }
        }
    }
    
    return opportunities;
}

ArbitrageOpportunity OpportunityDetector::EvaluateOpportunity(const std::string& symbol,
                                                             const PriceComparison& comparison) {
    ArbitrageOpportunity opportunity;
    opportunity.symbol = symbol;
    opportunity.buy_exchange = comparison.lowest_ask_exchange;
    opportunity.sell_exchange = comparison.highest_bid_exchange;
    opportunity.timestamp = comparison.timestamp;
    
    // Get prices from comparison
    if (comparison.exchange_prices.count(opportunity.buy_exchange) &&
        comparison.exchange_prices.count(opportunity.sell_exchange)) {
        
        const auto& buy_price = comparison.exchange_prices.at(opportunity.buy_exchange);
        const auto& sell_price = comparison.exchange_prices.at(opportunity.sell_exchange);
        
        opportunity.buy_price = buy_price.ask;
        opportunity.sell_price = sell_price.bid;
        
        // Set bid/ask prices for spread analysis
        opportunity.buy_bid = buy_price.bid;
        opportunity.buy_ask = buy_price.ask;
        opportunity.sell_bid = sell_price.bid;
        opportunity.sell_ask = sell_price.ask;
        
        opportunity.profit_absolute = opportunity.sell_price - opportunity.buy_price;
        opportunity.profit_percent = (opportunity.profit_absolute / opportunity.buy_price) * 100.0;
        
        // Calculate volumes and liquidity
        opportunity.max_volume = std::min(buy_price.volume, sell_price.volume);
        opportunity.buy_liquidity = buy_price.volume;
        opportunity.sell_liquidity = sell_price.volume;
        
        // Estimate fees and net profit
        opportunity.estimated_fees = EstimateTotalFees(symbol, opportunity.buy_exchange, 
                                                      opportunity.sell_exchange, opportunity.max_volume);
        opportunity.net_profit_percent = opportunity.profit_percent - 
                                       (opportunity.estimated_fees / opportunity.buy_price) * 100.0;
        
        // Risk assessment
        opportunity.execution_risk = CalculateExecutionRisk(opportunity);
        opportunity.spread_stability = AnalyzeSpreadStability(symbol);
    }
    
    return opportunity;
}

bool OpportunityDetector::ValidateOpportunity(ArbitrageOpportunity& opportunity) {
    opportunities_detected_++;
    
    // Record detection timestamp for rate calculation
    RecordDetectionTime();
    
    // Check minimum profit after fees
    if (opportunity.net_profit_percent < config_.min_profit_after_fees) {
        opportunity.meets_min_profit = false;
        opportunities_filtered_++;
        return false;
    }
    opportunity.meets_min_profit = true;
    
    // Check execution risk
    if (opportunity.execution_risk > config_.max_execution_risk) {
        opportunity.within_risk_limits = false;
        opportunities_filtered_++;
        return false;
    }
    opportunity.within_risk_limits = true;
    
    // Check minimum volume
    double volume_usd = ConvertToUSD(opportunity.symbol, opportunity.max_volume);
    if (volume_usd < config_.min_volume_usd) {
        opportunities_filtered_++;
        return false;
    }
    
    // Check balances for both exchanges
    if (config_.require_balance_check) {
        // Check buy exchange balance for quote currency (e.g., USDT for BTC/USDT)
        std::string quote_currency = ExtractQuoteCurrency(opportunity.symbol);
        double required_balance = opportunity.max_volume * opportunity.buy_price;
        double available_balance = GetExchangeBalance(opportunity.buy_exchange, quote_currency);
        
        if (available_balance < required_balance) {
            opportunity.has_sufficient_balance = false;
            opportunities_filtered_++;
            return false;
        }
        
        // Check sell exchange balance for base currency (e.g., BTC for BTC/USDT)  
        std::string base_currency = ExtractBaseCurrency(opportunity.symbol);
        double required_base = opportunity.max_volume;
        double available_base = GetExchangeBalance(opportunity.sell_exchange, base_currency);
        
        if (available_base < required_base) {
            opportunity.has_sufficient_balance = false;
            opportunities_filtered_++;
            return false;
        }
    }
    
    opportunity.has_sufficient_balance = true;
    
    opportunity.is_valid = true;
    valid_opportunities_++;
    
    return true;
}

// Risk calculation implementation
double OpportunityDetector::CalculateExecutionRisk(const ArbitrageOpportunity& opportunity) {
    double risk_score = 0.0;
    
    // Factor 1: Liquidity risk (30% weight)
    double liquidity_risk = 0.0;
    double min_liquidity = std::min(opportunity.buy_liquidity, opportunity.sell_liquidity);
    if (min_liquidity < config_.min_volume_usd) {
        liquidity_risk = 0.5; // High risk if liquidity is low
    } else if (min_liquidity < config_.min_volume_usd * 2.0) {
        liquidity_risk = 0.3; // Medium risk
    } else {
        liquidity_risk = 0.1; // Low risk
    }
    risk_score += liquidity_risk * 0.3;
    
    // Factor 2: Spread risk (25% weight)
    double spread_risk = 0.0;
    if (opportunity.buy_ask > 0 && opportunity.buy_bid > 0) {
        double buy_spread = (opportunity.buy_ask - opportunity.buy_bid) / opportunity.buy_price;
        spread_risk += buy_spread * 0.5; // Half weight for buy spread
    }
    if (opportunity.sell_ask > 0 && opportunity.sell_bid > 0) {
        double sell_spread = (opportunity.sell_ask - opportunity.sell_bid) / opportunity.sell_price;
        spread_risk += sell_spread * 0.5; // Half weight for sell spread
    }
    risk_score += std::min(spread_risk, 0.5) * 0.25; // Cap spread risk contribution
    
    // Factor 3: Profit margin risk (25% weight)
    double profit_risk = 0.0;
    if (opportunity.net_profit_percent < 0.5) {
        profit_risk = 0.8; // High risk for low profit
    } else if (opportunity.net_profit_percent < 1.0) {
        profit_risk = 0.4; // Medium risk
    } else {
        profit_risk = 0.1; // Low risk for higher profit
    }
    risk_score += profit_risk * 0.25;
    
    // Factor 4: Market volatility (20% weight)
    double volatility_risk = CalculateVolatility(opportunity.symbol);
    risk_score += volatility_risk * 0.2;
    
    return std::min(1.0, std::max(0.0, risk_score));
}

double OpportunityDetector::AnalyzeSpreadStability(const std::string& symbol) {
    auto it = price_histories_.find(symbol);
    if (it == price_histories_.end() || it->second.history.empty()) {
        return 0.5; // Default stability if no history
    }
    
    std::lock_guard<std::mutex> lock(it->second.mutex);
    const auto& history = it->second.history;
    
    if (history.size() < 3) {
        return 0.6; // Limited data
    }
    
    // Calculate spread stability over recent history
    std::vector<double> recent_spreads;
    for (const auto& comparison : history) {
        if (!comparison.exchange_prices.empty()) {
            double spread = comparison.highest_bid - comparison.lowest_ask;
            if (spread > 0) {
                recent_spreads.push_back(spread);
            }
        }
    }
    
    if (recent_spreads.size() < 2) {
        return 0.5;
    }
    
    // Calculate coefficient of variation (stability measure)
    double sum = 0.0;
    for (double spread : recent_spreads) {
        sum += spread;
    }
    double mean = sum / recent_spreads.size();
    
    double variance = 0.0;
    for (double spread : recent_spreads) {
        variance += (spread - mean) * (spread - mean);
    }
    variance /= recent_spreads.size();
    
    double coefficient_of_variation = (mean > 0) ? sqrt(variance) / mean : 1.0;
    
    // Convert to stability score (lower CV = higher stability)
    double stability = std::max(0.0, 1.0 - coefficient_of_variation);
    return std::min(1.0, stability);
}

double OpportunityDetector::EstimateTotalFees(const std::string& symbol, 
                                             const std::string& buy_exchange,
                                             const std::string& sell_exchange, 
                                             double volume) {
    double total_fees = 0.0;
    
    // Trading fees
    if (config_.exchange_fees.count(buy_exchange)) {
        total_fees += volume * config_.exchange_fees.at(buy_exchange);
    }
    
    if (config_.exchange_fees.count(sell_exchange)) {
        total_fees += volume * config_.exchange_fees.at(sell_exchange);
    }
    
    // Withdrawal fees (simplified)
    if (config_.withdrawal_fees.count(buy_exchange)) {
        total_fees += config_.withdrawal_fees.at(buy_exchange);
    }
    
    return total_fees;
}

std::vector<std::string> OpportunityDetector::GetMonitoredSymbols() const {
    if (price_monitor_) {
        return price_monitor_->GetMonitoredSymbols();
    }
    return {"BTC/USDT", "ETH/USDT"}; // Default symbols
}

std::vector<std::string> OpportunityDetector::GetActiveExchanges() const {
    if (price_monitor_) {
        return price_monitor_->GetActiveExchanges();
    }
    return {"binance", "upbit"}; // Default exchanges
}

void OpportunityDetector::RecordOpportunity(const ArbitrageOpportunity& opportunity) {
    std::lock_guard<std::mutex> lock(opportunities_mutex_);
    
    recent_opportunities_.push_back(opportunity);
    
    // Keep only the most recent opportunities
    if (recent_opportunities_.size() > max_opportunity_history_) {
        recent_opportunities_.erase(recent_opportunities_.begin());
    }
}

double OpportunityDetector::ConvertToUSD(const std::string& symbol, double amount) const {
    // Extract base currency from symbol (e.g., "BTC" from "BTC/USDT")
    std::string base_currency = ExtractBaseCurrency(symbol);
    
    // If already USD or USDT, return as-is
    if (base_currency == "USD" || base_currency == "USDT" || base_currency == "USDC") {
        return amount;
    }
    
    // Try to get price from price monitor
    if (price_monitor_) {
        // Look for USD pair first
        std::string usd_symbol = base_currency + "/USDT";
        Price price;
        
        // Try each active exchange to get USD price
        auto exchanges = GetActiveExchanges();
        for (const auto& exchange : exchanges) {
            if (price_monitor_->GetLatestPrice(exchange, usd_symbol, price)) {
                return amount * price.last;
            }
        }
        
        // Fallback: try with different USD pairs
        std::vector<std::string> usd_pairs = {
            base_currency + "/USD",
            base_currency + "/USDC"
        };
        
        for (const auto& pair : usd_pairs) {
            for (const auto& exchange : exchanges) {
                if (price_monitor_->GetLatestPrice(exchange, pair, price)) {
                    return amount * price.last;
                }
            }
        }
    }
    
    // Fallback: use approximate values for major cryptocurrencies
    static const std::unordered_map<std::string, double> approximate_prices = {
        {"BTC", 45000.0},
        {"ETH", 3000.0},
        {"BNB", 300.0},
        {"ADA", 0.5},
        {"SOL", 100.0},
        {"DOT", 7.0},
        {"LINK", 15.0},
        {"UNI", 6.0},
        {"LTC", 150.0},
        {"BCH", 250.0}
    };
    
    auto it = approximate_prices.find(base_currency);
    if (it != approximate_prices.end()) {
        return amount * it->second;
    }
    
    // Last resort: assume 1:1 ratio
    LOG_WARNING("Could not convert {} to USD, using 1:1 ratio", base_currency);
    return amount;
}

std::vector<ArbitrageOpportunity> OpportunityDetector::GetRecentOpportunities(size_t count) const {
    std::lock_guard<std::mutex> lock(opportunities_mutex_);
    
    std::vector<ArbitrageOpportunity> result;
    size_t start = recent_opportunities_.size() > count ? recent_opportunities_.size() - count : 0;
    
    for (size_t i = start; i < recent_opportunities_.size(); ++i) {
        result.push_back(recent_opportunities_[i]);
    }
    
    return result;
}

// Placeholder implementations for remaining methods
void OpportunityDetector::UpdateExchangeBalance(const std::string& exchange, const std::string& asset, double balance) {
    std::lock_guard<std::mutex> lock(exchanges_mutex_);
    std::string key = exchange + ":" + asset;
    exchange_balances_[key] = balance;
}

double OpportunityDetector::GetExchangeBalance(const std::string& exchange, const std::string& asset) const {
    std::lock_guard<std::mutex> lock(exchanges_mutex_);
    std::string key = exchange + ":" + asset;
    auto it = exchange_balances_.find(key);
    return (it != exchange_balances_.end()) ? it->second : 0.0;
}

void OpportunityDetector::RecordDetectionTime() {
    std::lock_guard<std::mutex> lock(detection_rate_mutex_);
    
    auto now = std::chrono::steady_clock::now();
    detection_timestamps_.push_back(now);
    
    // Keep only recent timestamps to limit memory usage
    if (detection_timestamps_.size() > max_detection_timestamps_) {
        detection_timestamps_.erase(detection_timestamps_.begin());
    }
    
    // Clean up old timestamps (older than 5 minutes)
    auto five_minutes_ago = now - std::chrono::minutes(5);
    detection_timestamps_.erase(
        std::remove_if(detection_timestamps_.begin(), detection_timestamps_.end(),
                      [five_minutes_ago](const auto& timestamp) {
                          return timestamp < five_minutes_ago;
                      }),
        detection_timestamps_.end()
    );
}

std::string OpportunityDetector::ExtractBaseCurrency(const std::string& symbol) const {
    size_t slash_pos = symbol.find('/');
    if (slash_pos != std::string::npos) {
        return symbol.substr(0, slash_pos);
    }
    return symbol; // Return as-is if no slash found
}

std::string OpportunityDetector::ExtractQuoteCurrency(const std::string& symbol) const {
    size_t slash_pos = symbol.find('/');
    if (slash_pos != std::string::npos && slash_pos + 1 < symbol.length()) {
        return symbol.substr(slash_pos + 1);
    }
    return "USDT"; // Default to USDT if no slash found
}

double OpportunityDetector::CalculateVolatility(const std::string& symbol) const {
    auto it = price_histories_.find(symbol);
    if (it == price_histories_.end() || it->second.history.empty()) {
        return 0.3; // Default moderate volatility
    }
    
    std::lock_guard<std::mutex> lock(it->second.mutex);
    const auto& history = it->second.history;
    
    if (history.size() < 3) {
        return 0.3; // Default for limited data
    }
    
    // Calculate price volatility from recent comparisons
    std::vector<double> mid_prices;
    for (const auto& comparison : history) {
        if (comparison.highest_bid > 0 && comparison.lowest_ask > 0) {
            double mid_price = (comparison.highest_bid + comparison.lowest_ask) / 2.0;
            mid_prices.push_back(mid_price);
        }
    }
    
    if (mid_prices.size() < 2) {
        return 0.3;
    }
    
    // Calculate returns and volatility
    std::vector<double> returns;
    for (size_t i = 1; i < mid_prices.size(); ++i) {
        if (mid_prices[i-1] > 0) {
            double return_rate = (mid_prices[i] - mid_prices[i-1]) / mid_prices[i-1];
            returns.push_back(return_rate);
        }
    }
    
    if (returns.empty()) {
        return 0.3;
    }
    
    // Calculate standard deviation of returns
    double sum = 0.0;
    for (double ret : returns) {
        sum += ret;
    }
    double mean = sum / returns.size();
    
    double variance = 0.0;
    for (double ret : returns) {
        variance += (ret - mean) * (ret - mean);
    }
    variance /= returns.size();
    
    double volatility = sqrt(variance);
    
    // Normalize to 0-1 range (values above 10% volatility are considered very high)
    return std::min(1.0, volatility / 0.1);
}

} // namespace ats 
