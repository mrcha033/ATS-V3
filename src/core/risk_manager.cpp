#include "risk_manager.hpp"
#include "../utils/config_manager.hpp"
#include "../utils/logger.hpp"

namespace ats {

RiskManager::RiskManager(ConfigManager* config_manager)
    : config_manager_(config_manager), max_trade_history_(1000),
      daily_pnl_(0.0), weekly_pnl_(0.0), monthly_pnl_(0.0), total_pnl_(0.0),
      kill_switch_activated_(false), trading_halted_(false),
      trades_approved_(0), trades_rejected_(0), risk_violations_(0) {
    
    last_reset_time_ = std::chrono::system_clock::now();
}

bool RiskManager::Initialize() {
    try {
        LOG_INFO("Initializing Risk Manager...");
        
        // Set default risk limits
        limits_.max_position_size_usd = 5000.0;
        limits_.max_total_exposure_usd = 20000.0;
        limits_.max_daily_loss_usd = 1000.0;
        limits_.max_daily_volume_usd = 50000.0;
        limits_.max_trades_per_minute = 10;
        limits_.max_trades_per_hour = 100;
        limits_.max_trades_per_day = 500;
        limits_.max_risk_per_trade = 0.02;
        limits_.min_reward_risk_ratio = 2.0;
        limits_.stop_loss_threshold = 2.0;
        limits_.kill_switch_loss_threshold = 5000.0;
        
        LOG_INFO("Risk Manager initialized with max position size: ${:.0f}", 
                limits_.max_position_size_usd);
        return true;
        
    } catch (const std::exception& e) {
        LOG_ERROR("Failed to initialize Risk Manager: {}", e.what());
        return false;
    }
}

void RiskManager::Reset() {
    std::lock_guard<std::mutex> lock1(trades_mutex_);
    std::lock_guard<std::mutex> lock2(positions_mutex_);
    
    daily_pnl_ = 0.0;
    weekly_pnl_ = 0.0;
    monthly_pnl_ = 0.0;
    kill_switch_activated_ = false;
    trading_halted_ = false;
    
    trade_history_.clear();
    current_positions_.clear();
    exchange_exposures_.clear();
    
    last_reset_time_ = std::chrono::system_clock::now();
    
    LOG_INFO("Risk Manager reset completed");
}

RiskAssessment RiskManager::AssessOpportunity(const ArbitrageOpportunity& opportunity) {
    RiskAssessment assessment;
    
    try {
        // Check if trading is allowed
        if (kill_switch_activated_.load() || trading_halted_.load()) {
            assessment.rejections.push_back("Trading is halted");
            return assessment;
        }
        
        // Check basic opportunity validity
        if (!opportunity.IsExecutable()) {
            assessment.rejections.push_back("Opportunity is not executable");
            return assessment;
        }
        
        // Check minimum profit
        if (opportunity.net_profit_percent < limits_.min_reward_risk_ratio) {
            assessment.rejections.push_back("Profit below minimum threshold");
            return assessment;
        }
        
        // Check position size limits
        double max_position = CalculateMaxPositionSize(opportunity);
        if (max_position <= 0) {
            assessment.rejections.push_back("Position size limit exceeded");
            return assessment;
        }
        
        // Check daily loss limits
        if (!CheckLossLimits()) {
            assessment.rejections.push_back("Daily loss limit reached");
            return assessment;
        }
        
        // Check trade rate limits
        if (!CheckTradeRate()) {
            assessment.rejections.push_back("Trade rate limit exceeded");
            return assessment;
        }
        
        // Calculate risk score
        assessment.risk_score = CalculatePositionRisk(opportunity, max_position);
        assessment.position_size_limit = max_position;
        assessment.is_approved = true;
        
        trades_approved_++;
        
    } catch (const std::exception& e) {
        LOG_ERROR("Error assessing opportunity: {}", e.what());
        assessment.rejections.push_back("Assessment error");
        trades_rejected_++;
    }
    
    return assessment;
}

bool RiskManager::IsTradeAllowed(const ArbitrageOpportunity& opportunity) {
    auto assessment = AssessOpportunity(opportunity);
    return assessment.is_approved;
}

double RiskManager::CalculateMaxPositionSize(const ArbitrageOpportunity& opportunity) {
    // Check position size limits
    double max_size = limits_.max_position_size_usd;
    
    // Check total exposure limit
    double current_exposure = GetTotalExposure();
    double remaining_exposure = limits_.max_total_exposure_usd - current_exposure;
    max_size = std::min(max_size, remaining_exposure);
    
    // Check opportunity constraints
    max_size = std::min(max_size, opportunity.max_volume);
    
    return std::max(0.0, max_size);
}

void RiskManager::RecordTradeStart(const std::string& trade_id, 
                                  const ArbitrageOpportunity& opportunity, 
                                  double volume) {
    std::lock_guard<std::mutex> lock(trades_mutex_);
    
    TradeRecord record;
    record.trade_id = trade_id;
    record.symbol = opportunity.symbol;
    record.buy_exchange = opportunity.buy_exchange;
    record.sell_exchange = opportunity.sell_exchange;
    record.volume = volume;
    record.buy_price = opportunity.buy_price;
    record.sell_price = opportunity.sell_price;
    record.start_time = std::chrono::system_clock::now();
    record.is_completed = false;
    
    trade_history_.push_back(record);
    
    // Update position tracking
    UpdatePosition(opportunity.symbol, volume);
    
    RecordTradeTime();
}

void RiskManager::RecordTradeComplete(const std::string& trade_id, double realized_pnl, double fees) {
    std::lock_guard<std::mutex> lock(trades_mutex_);
    
    for (auto& trade : trade_history_) {
        if (trade.trade_id == trade_id) {
            trade.realized_pnl = realized_pnl;
            trade.fees_paid = fees;
            trade.end_time = std::chrono::system_clock::now();
            trade.is_completed = true;
            trade.is_profitable = (realized_pnl > 0);
            break;
        }
    }
    
    // Update P&L
    UpdatePnL(realized_pnl);
}

void RiskManager::RecordTradeFailed(const std::string& trade_id, const std::string& reason) {
    std::lock_guard<std::mutex> lock(trades_mutex_);
    
    for (auto& trade : trade_history_) {
        if (trade.trade_id == trade_id) {
            trade.failure_reason = reason;
            trade.end_time = std::chrono::system_clock::now();
            trade.is_completed = true;
            trade.is_profitable = false;
            break;
        }
    }
    
    risk_violations_++;
}

void RiskManager::UpdatePosition(const std::string& symbol, double size_change) {
    std::lock_guard<std::mutex> lock(positions_mutex_);
    current_positions_[symbol] += size_change;
    
    // Remove zero positions
    if (std::abs(current_positions_[symbol]) < 1e-8) {
        current_positions_.erase(symbol);
    }
}

double RiskManager::GetCurrentPosition(const std::string& symbol) const {
    std::lock_guard<std::mutex> lock(positions_mutex_);
    auto it = current_positions_.find(symbol);
    return (it != current_positions_.end()) ? it->second : 0.0;
}

double RiskManager::GetTotalExposure() const {
    std::lock_guard<std::mutex> lock(positions_mutex_);
    
    double total = 0.0;
    for (const auto& position : current_positions_) {
        total += std::abs(position.second); // Use absolute value
    }
    return total;
}

double RiskManager::GetExchangeExposure(const std::string& exchange) const {
    std::lock_guard<std::mutex> lock(positions_mutex_);
    auto it = exchange_exposures_.find(exchange);
    return (it != exchange_exposures_.end()) ? it->second : 0.0;
}

void RiskManager::UpdatePnL(double pnl) {
    daily_pnl_ += pnl;
    weekly_pnl_ += pnl;
    monthly_pnl_ += pnl;
    total_pnl_ += pnl;
    
    // Check kill switch
    if (limits_.enable_kill_switch && daily_pnl_.load() < -limits_.kill_switch_loss_threshold) {
        ActivateKillSwitch("Daily loss threshold exceeded");
    }
}

void RiskManager::ActivateKillSwitch(const std::string& reason) {
    kill_switch_activated_ = true;
    LOG_CRITICAL("KILL SWITCH ACTIVATED: {}", reason);
    // TODO: Notify external systems
}

void RiskManager::DeactivateKillSwitch() {
    kill_switch_activated_ = false;
    LOG_INFO("Kill switch deactivated");
}

void RiskManager::HaltTrading(const std::string& reason) {
    trading_halted_ = true;
    LOG_WARNING("Trading halted: {}", reason);
}

void RiskManager::ResumeTrading() {
    trading_halted_ = false;
    LOG_INFO("Trading resumed");
}

bool RiskManager::CheckTradeRate() {
    std::lock_guard<std::mutex> lock(rate_tracker_.mutex);
    
    auto now = std::chrono::system_clock::now();
    auto one_minute_ago = now - std::chrono::minutes(1);
    auto one_hour_ago = now - std::chrono::hours(1);
    auto one_day_ago = now - std::chrono::hours(24);
    
    // Count trades in different time windows
    int trades_last_minute = 0;
    int trades_last_hour = 0;
    int trades_last_day = 0;
    
    for (const auto& trade_time : rate_tracker_.trade_times) {
        if (trade_time >= one_minute_ago) trades_last_minute++;
        if (trade_time >= one_hour_ago) trades_last_hour++;
        if (trade_time >= one_day_ago) trades_last_day++;
    }
    
    return (trades_last_minute < limits_.max_trades_per_minute &&
            trades_last_hour < limits_.max_trades_per_hour &&
            trades_last_day < limits_.max_trades_per_day);
}

bool RiskManager::CheckLossLimits() const {
    return (daily_pnl_.load() > -limits_.max_daily_loss_usd &&
            weekly_pnl_.load() > -limits_.max_weekly_loss_usd &&
            monthly_pnl_.load() > -limits_.max_monthly_loss_usd);
}

bool RiskManager::IsHealthy() const {
    return !kill_switch_activated_.load() && 
           CheckLossLimits() && 
           GetTotalExposure() < limits_.max_total_exposure_usd;
}

std::string RiskManager::GetStatus() const {
    if (kill_switch_activated_.load()) {
        return "KILL_SWITCH_ACTIVE";
    }
    if (trading_halted_.load()) {
        return "TRADING_HALTED";
    }
    if (!CheckLossLimits()) {
        return "LOSS_LIMIT_EXCEEDED";
    }
    return "ACTIVE";
}

void RiskManager::LogStatistics() const {
    LOG_INFO("=== Risk Manager Statistics ===");
    LOG_INFO("Trades approved: {}", trades_approved_.load());
    LOG_INFO("Trades rejected: {}", trades_rejected_.load());
    LOG_INFO("Risk violations: {}", risk_violations_.load());
    LOG_INFO("Daily P&L: ${:.2f}", daily_pnl_.load());
    LOG_INFO("Weekly P&L: ${:.2f}", weekly_pnl_.load());
    LOG_INFO("Total P&L: ${:.2f}", total_pnl_.load());
    LOG_INFO("Total exposure: ${:.2f}", GetTotalExposure());
    LOG_INFO("Kill switch: {}", kill_switch_activated_.load() ? "ACTIVE" : "INACTIVE");
}

void RiskManager::ResetStatistics() {
    trades_approved_ = 0;
    trades_rejected_ = 0;
    risk_violations_ = 0;
}

double RiskManager::GetApprovalRate() const {
    long long total = trades_approved_.load() + trades_rejected_.load();
    if (total == 0) return 0.0;
    return static_cast<double>(trades_approved_.load()) / total * 100.0;
}

// Helper methods with basic implementations
double RiskManager::CalculatePositionRisk(const ArbitrageOpportunity& opportunity, double volume) const {
    // Simple risk calculation based on volume and profit
    double risk_score = volume / limits_.max_position_size_usd;
    risk_score *= (1.0 - opportunity.net_profit_percent / 100.0);
    return std::min(1.0, std::max(0.0, risk_score));
}

void RiskManager::RecordTradeTime() {
    std::lock_guard<std::mutex> lock(rate_tracker_.mutex);
    
    auto now = std::chrono::system_clock::now();
    rate_tracker_.trade_times.push_back(now);
    
    // Keep only recent trades
    auto cutoff = now - std::chrono::hours(24);
    rate_tracker_.trade_times.erase(
        std::remove_if(rate_tracker_.trade_times.begin(), rate_tracker_.trade_times.end(),
                      [cutoff](const auto& time) { return time < cutoff; }),
        rate_tracker_.trade_times.end()
    );
}

// Placeholder implementations for remaining methods
bool RiskManager::IsMarketHoursActive() const {
    return true; // TODO: Implement market hours check
}

bool RiskManager::IsVolatilityAcceptable(const std::string& symbol) const {
    return true; // TODO: Implement volatility check
}

bool RiskManager::IsLiquidityAcceptable(const ArbitrageOpportunity& opportunity) const {
    return opportunity.buy_liquidity >= limits_.min_liquidity_threshold &&
           opportunity.sell_liquidity >= limits_.min_liquidity_threshold;
}

bool RiskManager::IsSpreadAcceptable(const ArbitrageOpportunity& opportunity) const {
    // Calculate actual spread percentage for both exchanges
    double buy_spread_percent = 0.0;
    double sell_spread_percent = 0.0;
    
    // Calculate buy exchange spread (ask - bid) / mid-price
    if (opportunity.buy_ask > 0 && opportunity.buy_bid > 0) {
        double mid_price = (opportunity.buy_ask + opportunity.buy_bid) / 2.0;
        buy_spread_percent = ((opportunity.buy_ask - opportunity.buy_bid) / mid_price) * 100.0;
    }
    
    // Calculate sell exchange spread (ask - bid) / mid-price  
    if (opportunity.sell_ask > 0 && opportunity.sell_bid > 0) {
        double mid_price = (opportunity.sell_ask + opportunity.sell_bid) / 2.0;
        sell_spread_percent = ((opportunity.sell_ask - opportunity.sell_bid) / mid_price) * 100.0;
    }
    
    // Check if both spreads are within acceptable limits
    return (buy_spread_percent <= limits_.max_spread_threshold && 
            sell_spread_percent <= limits_.max_spread_threshold);
}

double RiskManager::CalculateVaR(double confidence_level) const {
    // TODO: Implement Value at Risk calculation
    return 0.0;
}

double RiskManager::CalculateMaxDrawdown() const {
    // TODO: Implement drawdown calculation
    return 0.0;
}

double RiskManager::CalculateSharpeRatio() const {
    // TODO: Implement Sharpe ratio calculation
    return 0.0;
}

double RiskManager::CalculateWinRate() const {
    std::lock_guard<std::mutex> lock(trades_mutex_);
    
    if (trade_history_.empty()) return 0.0;
    
    int winning_trades = 0;
    for (const auto& trade : trade_history_) {
        if (trade.is_completed && trade.is_profitable) {
            winning_trades++;
        }
    }
    
    return static_cast<double>(winning_trades) / trade_history_.size() * 100.0;
}

double RiskManager::CalculateAvgTrade() const {
    std::lock_guard<std::mutex> lock(trades_mutex_);
    
    if (trade_history_.empty()) return 0.0;
    
    double total_pnl = 0.0;
    int completed_trades = 0;
    
    for (const auto& trade : trade_history_) {
        if (trade.is_completed) {
            total_pnl += trade.realized_pnl;
            completed_trades++;
        }
    }
    
    return completed_trades > 0 ? total_pnl / completed_trades : 0.0;
}

std::vector<TradeRecord> RiskManager::GetRecentTrades(size_t count) const {
    std::lock_guard<std::mutex> lock(trades_mutex_);
    
    std::vector<TradeRecord> result;
    size_t start = trade_history_.size() > count ? trade_history_.size() - count : 0;
    
    for (size_t i = start; i < trade_history_.size(); ++i) {
        result.push_back(trade_history_[i]);
    }
    
    return result;
}

int RiskManager::GetTradesInLastMinute() const {
    std::lock_guard<std::mutex> lock(rate_tracker_.mutex);
    
    auto now = std::chrono::system_clock::now();
    auto one_minute_ago = now - std::chrono::minutes(1);
    
    int count = 0;
    for (const auto& trade_time : rate_tracker_.trade_times) {
        if (trade_time >= one_minute_ago) count++;
    }
    
    return count;
}

int RiskManager::GetTradesInLastHour() const {
    std::lock_guard<std::mutex> lock(rate_tracker_.mutex);
    
    auto now = std::chrono::system_clock::now();
    auto one_hour_ago = now - std::chrono::hours(1);
    
    int count = 0;
    for (const auto& trade_time : rate_tracker_.trade_times) {
        if (trade_time >= one_hour_ago) count++;
    }
    
    return count;
}

int RiskManager::GetTradesInLastDay() const {
    return static_cast<int>(rate_tracker_.trade_times.size());
}

void RiskManager::PerformDailyReset() {
    daily_pnl_ = 0.0;
    // TODO: Implement daily cleanup
}

void RiskManager::PerformWeeklyReset() {
    weekly_pnl_ = 0.0;
    // TODO: Implement weekly cleanup
}

void RiskManager::PerformMonthlyReset() {
    monthly_pnl_ = 0.0;
    // TODO: Implement monthly cleanup
}

// Private helper method implementations
void RiskManager::CleanupOldTrades() {
    std::lock_guard<std::mutex> lock(trades_mutex_);
    
    auto now = std::chrono::system_clock::now();
    auto cutoff = now - std::chrono::hours(24 * 30); // Keep trades for 30 days
    
    trade_history_.erase(
        std::remove_if(trade_history_.begin(), trade_history_.end(),
            [cutoff](const TradeRecord& trade) {
                return trade.start_time < cutoff;
            }),
        trade_history_.end()
    );
    
    // Limit maximum history size
    if (trade_history_.size() > max_trade_history_) {
        trade_history_.erase(trade_history_.begin(), 
                           trade_history_.begin() + (trade_history_.size() - max_trade_history_));
    }
}

void RiskManager::CleanupOldRateData() {
    std::lock_guard<std::mutex> lock(rate_tracker_.mutex);
    
    auto now = std::chrono::system_clock::now();
    auto cutoff = now - std::chrono::hours(24); // Keep rate data for 24 hours
    
    rate_tracker_.trade_times.erase(
        std::remove_if(rate_tracker_.trade_times.begin(), rate_tracker_.trade_times.end(),
            [cutoff](const std::chrono::system_clock::time_point& time) {
                return time < cutoff;
            }),
        rate_tracker_.trade_times.end()
    );
    
    // Limit maximum size
    if (rate_tracker_.trade_times.size() > rate_tracker_.max_size) {
        rate_tracker_.trade_times.erase(
            rate_tracker_.trade_times.begin(),
            rate_tracker_.trade_times.begin() + (rate_tracker_.trade_times.size() - rate_tracker_.max_size)
        );
    }
}

double RiskManager::CalculateRewardRiskRatio(const ArbitrageOpportunity& opportunity, double volume) const {
    if (volume <= 0.0) return 0.0;
    
    // Calculate potential reward (net profit)
    double potential_reward = opportunity.net_profit_percent * volume / 100.0;
    
    // Calculate potential risk (maximum loss)
    double potential_risk = volume * limits_.max_risk_per_trade;
    
    // Consider execution risk and slippage
    double execution_risk = volume * 0.001; // 0.1% execution risk
    double total_risk = potential_risk + execution_risk;
    
    if (total_risk <= 0.0) return 0.0;
    
    return potential_reward / total_risk;
}

bool RiskManager::IsSameDay(const std::chrono::system_clock::time_point& time1,
                           const std::chrono::system_clock::time_point& time2) const {
    auto tt1 = std::chrono::system_clock::to_time_t(time1);
    auto tt2 = std::chrono::system_clock::to_time_t(time2);
    
    std::tm tm1 = *std::gmtime(&tt1);
    std::tm tm2 = *std::gmtime(&tt2);
    
    return (tm1.tm_year == tm2.tm_year && 
            tm1.tm_yday == tm2.tm_yday);
}

bool RiskManager::IsSameWeek(const std::chrono::system_clock::time_point& time1,
                            const std::chrono::system_clock::time_point& time2) const {
    auto tt1 = std::chrono::system_clock::to_time_t(time1);
    auto tt2 = std::chrono::system_clock::to_time_t(time2);
    
    std::tm tm1 = *std::gmtime(&tt1);
    std::tm tm2 = *std::gmtime(&tt2);
    
    // Calculate week number
    int week1 = tm1.tm_yday / 7;
    int week2 = tm2.tm_yday / 7;
    
    return (tm1.tm_year == tm2.tm_year && week1 == week2);
}

bool RiskManager::IsSameMonth(const std::chrono::system_clock::time_point& time1,
                             const std::chrono::system_clock::time_point& time2) const {
    auto tt1 = std::chrono::system_clock::to_time_t(time1);
    auto tt2 = std::chrono::system_clock::to_time_t(time2);
    
    std::tm tm1 = *std::gmtime(&tt1);
    std::tm tm2 = *std::gmtime(&tt2);
    
    return (tm1.tm_year == tm2.tm_year && 
            tm1.tm_mon == tm2.tm_mon);
}

} // namespace ats 