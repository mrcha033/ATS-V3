#pragma once

#include <unordered_map>
#include <vector>
#include <mutex>
#include <atomic>
#include <chrono>
#include <memory>

#include "opportunity_detector.hpp"

namespace ats {

class ConfigManager;

// Risk limits and thresholds
struct RiskLimits {
    // Position limits
    double max_position_size_usd = 5000.0;        // Maximum position size per trade
    double max_total_exposure_usd = 20000.0;      // Maximum total exposure across all trades
    double max_daily_volume_usd = 50000.0;        // Maximum daily trading volume
    
    // Loss limits
    double max_daily_loss_usd = 1000.0;           // Maximum daily loss
    double max_weekly_loss_usd = 3000.0;          // Maximum weekly loss
    double max_monthly_loss_usd = 10000.0;        // Maximum monthly loss
    double stop_loss_threshold = 2.0;             // Stop loss threshold %
    
    // Rate limits
    int max_trades_per_minute = 10;               // Maximum trades per minute
    int max_trades_per_hour = 100;                // Maximum trades per hour
    int max_trades_per_day = 500;                 // Maximum trades per day
    
    // Risk ratios
    double max_risk_per_trade = 0.02;             // Maximum risk per trade (2% of capital)
    double min_reward_risk_ratio = 2.0;           // Minimum reward:risk ratio
    double max_correlation_exposure = 0.3;        // Maximum exposure to correlated assets
    
    // Market conditions
    double max_volatility_threshold = 5.0;        // Maximum volatility to trade in
    double min_liquidity_threshold = 10000.0;     // Minimum liquidity required
    double max_spread_threshold = 1.0;            // Maximum spread to trade
    
    // Emergency stops
    bool enable_kill_switch = true;               // Enable emergency stop
    double kill_switch_loss_threshold = 5000.0;   // Loss threshold for kill switch
    bool enable_market_hours_check = true;        // Only trade during market hours
};

// Trade tracking for risk assessment
struct TradeRecord {
    std::string trade_id;
    std::string symbol;
    std::string buy_exchange;
    std::string sell_exchange;
    
    double volume;
    double buy_price;
    double sell_price;
    double realized_pnl;
    double fees_paid;
    
    std::chrono::system_clock::time_point start_time;
    std::chrono::system_clock::time_point end_time;
    
    bool is_completed;
    bool is_profitable;
    std::string failure_reason;
};

// Risk assessment result
struct RiskAssessment {
    bool is_approved;
    double risk_score;              // 0.0 (low risk) to 1.0 (high risk)
    double position_size_limit;     // Maximum allowed position size
    std::vector<std::string> warnings;
    std::vector<std::string> rejections;
    
    RiskAssessment() : is_approved(false), risk_score(1.0), position_size_limit(0.0) {}
};

class RiskManager {
private:
    ConfigManager* config_manager_;
    RiskLimits limits_;
    
    // Trade tracking
    std::vector<TradeRecord> trade_history_;
    mutable std::mutex trades_mutex_;
    size_t max_trade_history_;
    
    // Current positions and exposure
    std::unordered_map<std::string, double> current_positions_;    // symbol -> position size
    std::unordered_map<std::string, double> exchange_exposures_;   // exchange -> total exposure
    mutable std::mutex positions_mutex_;
    
    // P&L tracking
    std::atomic<double> daily_pnl_;
    std::atomic<double> weekly_pnl_;
    std::atomic<double> monthly_pnl_;
    std::atomic<double> total_pnl_;
    
    // Trade rate tracking
    struct TradeRateTracker {
        std::vector<std::chrono::system_clock::time_point> trade_times;
        mutable std::mutex mutex;
        size_t max_size = 1000;
    };
    TradeRateTracker rate_tracker_;
    
    // Risk state
    std::atomic<bool> kill_switch_activated_;
    std::atomic<bool> trading_halted_;
    std::chrono::system_clock::time_point last_reset_time_;
    
    // Statistics
    std::atomic<long long> trades_approved_;
    std::atomic<long long> trades_rejected_;
    std::atomic<long long> risk_violations_;

public:
    explicit RiskManager(ConfigManager* config_manager);
    ~RiskManager() = default;
    
    // Lifecycle
    bool Initialize();
    void Reset();
    
    // Configuration
    void SetLimits(const RiskLimits& limits) { limits_ = limits; }
    RiskLimits GetLimits() const { return limits_; }
    
    // Core risk assessment
    RiskAssessment AssessOpportunity(const ArbitrageOpportunity& opportunity);
    bool IsTradeAllowed(const ArbitrageOpportunity& opportunity);
    double CalculateMaxPositionSize(const ArbitrageOpportunity& opportunity);
    
    // Position management
    void RecordTradeStart(const std::string& trade_id, const ArbitrageOpportunity& opportunity, double volume);
    void RecordTradeComplete(const std::string& trade_id, double realized_pnl, double fees);
    void RecordTradeFailed(const std::string& trade_id, const std::string& reason);
    
    void UpdatePosition(const std::string& symbol, double size_change);
    double GetCurrentPosition(const std::string& symbol) const;
    double GetTotalExposure() const;
    double GetExchangeExposure(const std::string& exchange) const;
    
    // P&L tracking
    void UpdatePnL(double pnl);
    double GetDailyPnL() const { return daily_pnl_.load(); }
    double GetWeeklyPnL() const { return weekly_pnl_.load(); }
    double GetMonthlyPnL() const { return monthly_pnl_.load(); }
    double GetTotalPnL() const { return total_pnl_.load(); }
    
    // Emergency controls
    void ActivateKillSwitch(const std::string& reason);
    void DeactivateKillSwitch();
    bool IsKillSwitchActive() const { return kill_switch_activated_.load(); }
    
    void HaltTrading(const std::string& reason);
    void ResumeTrading();
    bool IsTradingHalted() const { return trading_halted_.load(); }
    
    // Market condition checks
    bool IsMarketHoursActive() const;
    bool IsVolatilityAcceptable(const std::string& symbol) const;
    bool IsLiquidityAcceptable(const ArbitrageOpportunity& opportunity) const;
    bool IsSpreadAcceptable(const ArbitrageOpportunity& opportunity) const;
    
    // Rate limiting
    bool CheckTradeRate();
    int GetTradesInLastMinute() const;
    int GetTradesInLastHour() const;
    int GetTradesInLastDay() const;
    
    // Risk metrics
    double CalculateVaR(double confidence_level = 0.95) const;        // Value at Risk
    double CalculateMaxDrawdown() const;
    double CalculateSharpeRatio() const;
    double CalculateWinRate() const;
    double CalculateAvgTrade() const;
    
    // Correlation analysis
    double CalculateCorrelation(const std::string& symbol1, const std::string& symbol2) const;
    double GetCorrelationExposure(const std::string& symbol) const;
    
    // Trade history and analysis
    std::vector<TradeRecord> GetRecentTrades(size_t count = 50) const;
    std::vector<TradeRecord> GetTradesForSymbol(const std::string& symbol, size_t count = 20) const;
    TradeRecord* GetTrade(const std::string& trade_id);
    
    // Statistics
    long long GetTradesApproved() const { return trades_approved_.load(); }
    long long GetTradesRejected() const { return trades_rejected_.load(); }
    long long GetRiskViolations() const { return risk_violations_.load(); }
    double GetApprovalRate() const;
    
    // Health and status
    bool IsHealthy() const;
    std::string GetStatus() const;
    void LogStatistics() const;
    void ResetStatistics();
    
    // Periodic maintenance
    void PerformDailyReset();
    void PerformWeeklyReset();
    void PerformMonthlyReset();

    // External notification system
    void NotifyExternalSystems(const std::string& reason);

private:
    // Risk calculation methods
    double CalculatePositionRisk(const ArbitrageOpportunity& opportunity, double volume) const;
    double CalculateMarketRisk(const ArbitrageOpportunity& opportunity) const;
    double CalculateLiquidityRisk(const ArbitrageOpportunity& opportunity) const;
    double CalculateExecutionRisk(const ArbitrageOpportunity& opportunity) const;
    double CalculateConcentrationRisk(const std::string& symbol) const;
    
    // Validation methods
    bool CheckPositionLimits(const ArbitrageOpportunity& opportunity, double volume) const;
    bool CheckExposureLimits(const ArbitrageOpportunity& opportunity, double volume) const;
    bool CheckLossLimits() const;
    bool CheckVolumeLimits(double volume) const;
    
    // Helper methods
    void RecordTradeTime();
    void CleanupOldTrades();
    void CleanupOldRateData();
    double CalculateRewardRiskRatio(const ArbitrageOpportunity& opportunity, double volume) const;
    std::string GetAssetFromSymbol(const std::string& symbol) const;
    
    // Time-based utilities
    bool IsSameDay(const std::chrono::system_clock::time_point& time1,
                   const std::chrono::system_clock::time_point& time2) const;
    bool IsSameWeek(const std::chrono::system_clock::time_point& time1,
                    const std::chrono::system_clock::time_point& time2) const;
    bool IsSameMonth(const std::chrono::system_clock::time_point& time1,
                     const std::chrono::system_clock::time_point& time2) const;
};

} // namespace ats 