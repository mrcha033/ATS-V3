#pragma once

#include <vector>
#include <unordered_map>
#include <memory>
#include <functional>
#include <atomic>
#include <mutex>
#include <thread>

#include "../data/market_data.hpp"
#include "../core/types.hpp"

namespace ats {

// Note: ArbitrageOpportunity moved to types.hpp to avoid duplication

// Detection configuration
struct DetectionConfig {
    double min_profit_threshold = 0.5;     // Minimum profit % to consider
    double min_profit_after_fees = 0.2;    // Minimum profit % after fees
    double max_execution_risk = 0.3;       // Maximum acceptable risk score
    double min_volume_usd = 100.0;         // Minimum trade volume in USD
    double max_volume_usd = 10000.0;       // Maximum trade volume in USD
    
    std::chrono::milliseconds max_price_age{5000};     // Max age of price data
    std::chrono::milliseconds detection_interval{100}; // Detection frequency
    std::chrono::seconds spread_analysis_window{60};   // Time window for spread analysis
    
    bool require_balance_check = true;
    bool enable_risk_assessment = true;
    bool enable_spread_filtering = true;
    
    // Exchange-specific settings
    std::unordered_map<std::string, double> exchange_fees;        // Trading fees by exchange
    std::unordered_map<std::string, double> withdrawal_fees;      // Withdrawal fees by exchange
    std::unordered_map<std::string, double> min_trade_amounts;    // Minimum trade amounts
};

class OpportunityDetector {
public:
    using OpportunityCallback = std::function<void(const ArbitrageOpportunity&)>;
    using AlertCallback = std::function<void(const std::string& message)>;

private:
    ConfigManager* config_manager_;
    PriceMonitor* price_monitor_;
    DetectionConfig config_;
    
    // Threading
    std::thread detection_thread_;
    std::atomic<bool> running_;
    
    // Callbacks
    OpportunityCallback opportunity_callback_;
    AlertCallback alert_callback_;
    
    // Opportunity tracking
    std::vector<ArbitrageOpportunity> recent_opportunities_;
    mutable std::mutex opportunities_mutex_;
    size_t max_opportunity_history_;
    
    // Performance tracking
    std::atomic<long long> opportunities_detected_;
    std::atomic<long long> opportunities_filtered_;
    std::atomic<long long> valid_opportunities_;
    std::atomic<double> avg_detection_time_ms_;
    
    // Detection rate tracking
    mutable std::mutex detection_rate_mutex_;
    std::vector<std::chrono::steady_clock::time_point> detection_timestamps_;
    static constexpr size_t max_detection_timestamps_ = 100;
    
    // Price history for spread analysis
    struct PriceHistory {
        std::vector<PriceComparison> history;
        std::mutex mutex;
        size_t max_size = 100;
    };
    std::unordered_map<std::string, PriceHistory> price_histories_;
    
    // Exchange data cache
    mutable std::mutex exchanges_mutex_;
    std::vector<std::string> active_exchanges_;
    std::unordered_map<std::string, double> exchange_balances_;

public:
    explicit OpportunityDetector(ConfigManager* config_manager, PriceMonitor* price_monitor);
    ~OpportunityDetector();
    
    // Lifecycle
    bool Initialize();
    void Start();
    void Stop();
    bool IsRunning() const { return running_.load(); }
    
    // Configuration
    void SetConfig(const DetectionConfig& config) { config_ = config; }
    DetectionConfig GetConfig() const { return config_; }
    
    void SetOpportunityCallback(OpportunityCallback callback) { opportunity_callback_ = callback; }
    void SetAlertCallback(AlertCallback callback) { alert_callback_ = callback; }
    
    // Manual detection
    std::vector<ArbitrageOpportunity> DetectOpportunities(const std::string& symbol);
    ArbitrageOpportunity AnalyzePair(const std::string& symbol, 
                                    const std::string& exchange1, 
                                    const std::string& exchange2);
    
    // Exchange management
    void UpdateExchangeBalance(const std::string& exchange, const std::string& asset, double balance);
    double GetExchangeBalance(const std::string& exchange, const std::string& asset) const;
    
    // Opportunity history
    std::vector<ArbitrageOpportunity> GetRecentOpportunities(size_t count = 10) const;
    std::vector<ArbitrageOpportunity> GetOpportunitiesForSymbol(const std::string& symbol, 
                                                               size_t count = 10) const;
    
    // Statistics
    long long GetOpportunitiesDetected() const { return opportunities_detected_.load(); }
    long long GetOpportunitiesFiltered() const { return opportunities_filtered_.load(); }
    long long GetValidOpportunities() const { return valid_opportunities_.load(); }
    double GetDetectionRate() const; // opportunities per second
    double GetValidationRate() const; // % of detected opportunities that are valid
    double GetAvgDetectionTime() const { return avg_detection_time_ms_.load(); }
    
    // Health and status
    bool IsHealthy() const;
    std::string GetStatus() const;
    void LogStatistics() const;
    void ResetStatistics();

private:
    // Main detection loop
    void DetectionLoop();
    
    // Detection algorithms
    std::vector<ArbitrageOpportunity> ScanAllPairs();
    ArbitrageOpportunity EvaluateOpportunity(const std::string& symbol,
                                            const PriceComparison& comparison);
    
    // Validation and filtering
    bool ValidateOpportunity(ArbitrageOpportunity& opportunity);
    bool CheckPriceAge(const Price& price) const;
    bool CheckMinimumProfit(const ArbitrageOpportunity& opportunity) const;
    bool CheckLiquidity(ArbitrageOpportunity& opportunity);
    bool CheckBalances(ArbitrageOpportunity& opportunity);
    
    // Risk assessment
    double CalculateExecutionRisk(const ArbitrageOpportunity& opportunity);
    double AnalyzeSpreadStability(const std::string& symbol);
    double EstimateTotalFees(const std::string& symbol, 
                           const std::string& buy_exchange,
                           const std::string& sell_exchange, 
                           double volume);
    
    // Market analysis
    void UpdatePriceHistory(const std::string& symbol, const PriceComparison& comparison);
    double CalculateVolatility(const std::string& symbol) const;
    double EstimateSlippage(const std::string& exchange, const std::string& symbol, 
                          double volume, bool is_buy) const;
    
    // Utility functions
    std::vector<std::string> GetMonitoredSymbols() const;
    std::vector<std::string> GetActiveExchanges() const;
    void RecordOpportunity(const ArbitrageOpportunity& opportunity);
    void UpdateStatistics(const ArbitrageOpportunity& opportunity, bool is_valid);
    void RecordDetectionTime();
    
    // Helper methods
    std::string MakeBalanceKey(const std::string& exchange, const std::string& asset) const;
    double GetMinTradeAmount(const std::string& exchange, const std::string& symbol) const;
    double ConvertToUSD(const std::string& symbol, double amount) const;
};

} // namespace ats 