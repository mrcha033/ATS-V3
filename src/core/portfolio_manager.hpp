#pragma once

#include <unordered_map>
#include <vector>
#include <string>
#include <mutex>
#include <atomic>
#include <chrono>
#include <memory>
#include <set>

namespace ats {

class ConfigManager;
class ExchangeInterface;

// Asset balance on a specific exchange
struct AssetBalance {
    std::string asset;
    std::string exchange;
    double available;           // Available for trading
    double locked;              // Locked in orders
    double total;               // Total balance
    double usd_value;           // Current USD value
    std::chrono::system_clock::time_point last_update;
    
    AssetBalance() : available(0.0), locked(0.0), total(0.0), usd_value(0.0) {}
    
    bool IsStale(std::chrono::seconds max_age) const {
        auto now = std::chrono::system_clock::now();
        return (now - last_update) > max_age;
    }
};

// Portfolio allocation target
struct AllocationTarget {
    std::string asset;
    double target_percentage;   // Target allocation percentage
    double min_percentage;      // Minimum allocation
    double max_percentage;      // Maximum allocation
    double rebalance_threshold; // Threshold to trigger rebalancing
    bool enable_rebalancing;
    
    AllocationTarget() : target_percentage(0.0), min_percentage(0.0),
                        max_percentage(100.0), rebalance_threshold(5.0),
                        enable_rebalancing(false) {}
};

// Portfolio performance metrics
struct PortfolioMetrics {
    double total_value_usd;
    double daily_pnl;
    double daily_pnl_percent;
    double total_pnl;
    double total_pnl_percent;
    
    // Risk metrics
    double max_drawdown;
    double volatility;
    double sharpe_ratio;
    double sortino_ratio;
    
    // Asset allocation
    std::unordered_map<std::string, double> asset_allocations;  // asset -> percentage
    std::unordered_map<std::string, double> exchange_allocations; // exchange -> percentage
    
    std::chrono::system_clock::time_point last_update;
    
    PortfolioMetrics() : total_value_usd(0.0), daily_pnl(0.0), daily_pnl_percent(0.0),
                        total_pnl(0.0), total_pnl_percent(0.0), max_drawdown(0.0),
                        volatility(0.0), sharpe_ratio(0.0), sortino_ratio(0.0) {}
};

// Rebalancing recommendation
struct RebalanceAction {
    std::string asset;
    std::string from_exchange;
    std::string to_exchange;
    double amount;
    double usd_value;
    std::string reason;
    double priority;            // Higher priority = more urgent
    
    RebalanceAction() : amount(0.0), usd_value(0.0), priority(0.0) {}
};

class PortfolioManager {
private:
    ConfigManager* config_manager_;
    
    // Balance tracking
    std::unordered_map<std::string, AssetBalance> balances_;  // "exchange:asset" -> balance
    mutable std::mutex balances_mutex_;
    
    // Exchange connections
    std::unordered_map<std::string, ExchangeInterface*> exchanges_;
    mutable std::mutex exchanges_mutex_;
    
    // Allocation targets
    std::unordered_map<std::string, AllocationTarget> allocation_targets_;
    mutable std::mutex targets_mutex_;
    
    // Historical data for performance calculation
    struct HistoricalSnapshot {
        double total_value;
        std::chrono::system_clock::time_point timestamp;
    };
    std::vector<HistoricalSnapshot> value_history_;
    mutable std::mutex history_mutex_;
    size_t max_history_size_;
    
    // Portfolio metrics
    PortfolioMetrics current_metrics_;
    mutable std::mutex metrics_mutex_;
    
    // Update scheduling
    std::chrono::system_clock::time_point last_balance_update_;
    std::chrono::system_clock::time_point last_metrics_update_;
    std::chrono::seconds balance_update_interval_;
    std::chrono::seconds metrics_update_interval_;
    
    // Asset price cache for USD conversion
    std::unordered_map<std::string, double> asset_prices_usd_;
    mutable std::mutex prices_mutex_;
    std::chrono::system_clock::time_point last_price_update_;

public:
    explicit PortfolioManager(ConfigManager* config_manager);
    ~PortfolioManager() = default;
    
    // Lifecycle
    bool Initialize();
    void UpdateAll();
    
    // Exchange management
    void AddExchange(const std::string& name, ExchangeInterface* exchange);
    void RemoveExchange(const std::string& name);
    ExchangeInterface* GetExchange(const std::string& name) const;
    std::vector<std::string> GetExchanges() const;
    
    // Balance management
    void UpdateBalance(const std::string& exchange, const std::string& asset, 
                      double available, double locked);
    void UpdateAllBalances();
    void UpdateExchangeBalances(const std::string& exchange);
    
    AssetBalance GetBalance(const std::string& exchange, const std::string& asset) const;
    double GetTotalAssetBalance(const std::string& asset) const;
    double GetAvailableBalance(const std::string& exchange, const std::string& asset) const;
    double GetTotalAvailableBalance(const std::string& asset) const;
    
    // Portfolio queries
    std::vector<AssetBalance> GetAllBalances() const;
    std::vector<AssetBalance> GetExchangeBalances(const std::string& exchange) const;
    std::vector<AssetBalance> GetAssetBalances(const std::string& asset) const;
    
    // USD value calculations
    double GetPortfolioValueUSD() const;
    double GetExchangeValueUSD(const std::string& exchange) const;
    double GetAssetValueUSD(const std::string& asset) const;
    void UpdateAssetPrices();
    
    // Allocation management
    void SetAllocationTarget(const std::string& asset, const AllocationTarget& target);
    AllocationTarget GetAllocationTarget(const std::string& asset) const;
    void RemoveAllocationTarget(const std::string& asset);
    
    double GetCurrentAllocation(const std::string& asset) const;
    std::unordered_map<std::string, double> GetAllAllocations() const;
    
    // Rebalancing
    std::vector<RebalanceAction> GetRebalanceRecommendations() const;
    bool IsRebalancingNeeded() const;
    void ExecuteRebalancing(const std::vector<RebalanceAction>& actions);
    
    // Performance metrics
    PortfolioMetrics GetMetrics() const;
    void UpdateMetrics();
    double GetTotalPnL() const;
    double GetDailyPnL() const;
    double GetMaxDrawdown() const;
    
    // Risk analysis
    double CalculatePortfolioRisk() const;
    double CalculateConcentrationRisk() const;
    double CalculateExchangeRisk() const;
    std::unordered_map<std::string, double> GetRiskByAsset() const;
    
    // Historical analysis
    void RecordSnapshot();
    std::vector<HistoricalSnapshot> GetValueHistory(size_t days = 30) const;
    double CalculateVolatility(size_t days = 30) const;
    double CalculateSharpeRatio(size_t days = 30) const;
    
    // Utility functions
    bool HasSufficientBalance(const std::string& exchange, const std::string& asset, double amount) const;
    double GetMaxTradeAmount(const std::string& exchange, const std::string& asset) const;
    std::vector<std::string> GetAvailableAssets() const;
    
    // Balance validation
    bool ValidateBalances() const;
    std::vector<std::string> GetBalanceWarnings() const;
    void LogPortfolioSummary() const;
    
    // Configuration
    void SetBalanceUpdateInterval(std::chrono::seconds interval) { balance_update_interval_ = interval; }
    void SetMetricsUpdateInterval(std::chrono::seconds interval) { metrics_update_interval_ = interval; }
    void SetMaxHistorySize(size_t size) { max_history_size_ = size; }
    
    // Health and status
    bool IsHealthy() const;
    std::string GetStatus() const;

private:
    // Helper functions
    std::string MakeBalanceKey(const std::string& exchange, const std::string& asset) const;
    double ConvertToUSD(const std::string& asset, double amount) const;
    void UpdateMetricsInternal();
    
    // Performance calculations
    double CalculateDailyPnL() const;
    double CalculateMaxDrawdownInternal() const;
    double CalculateVolatilityInternal(size_t days) const;
    double CalculateSharpeRatioInternal(size_t days) const;
    
    // Rebalancing logic
    RebalanceAction CreateRebalanceAction(const std::string& asset, 
                                        const AllocationTarget& target,
                                        double current_allocation) const;
    double CalculateRebalancePriority(const std::string& asset, double deviation) const;
    
    // Cleanup and maintenance
    void CleanupOldHistory();
    void ValidateAndRepairBalances();
    
    // Asset price management
    void UpdateAssetPrice(const std::string& asset, double usd_price);
    double GetAssetPrice(const std::string& asset) const;
    bool IsAssetPriceStale(const std::string& asset) const;
};

} // namespace ats 