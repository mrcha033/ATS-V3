#include "opportunity_detector.hpp"
#include "../utils/config_manager.hpp"
#include "../utils/logger.hpp"
#include "price_monitor.hpp"

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
    // TODO: Implement detection rate calculation
    return 0.0;
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
    
    // TODO: Check balances (requires exchange integration)
    opportunity.has_sufficient_balance = true; // Assume true for now
    
    opportunity.is_valid = true;
    valid_opportunities_++;
    
    return true;
}

// Placeholder implementations for complex methods
double OpportunityDetector::CalculateExecutionRisk(const ArbitrageOpportunity& opportunity) {
    // Simple risk calculation based on spread and volatility
    return 0.1; // TODO: Implement proper risk calculation
}

double OpportunityDetector::AnalyzeSpreadStability(const std::string& symbol) {
    // TODO: Implement spread stability analysis
    return 0.8; // 80% stability
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
    // TODO: Implement USD conversion using price data
    // For now, assume 1:1 conversion for simplicity
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

} // namespace ats 