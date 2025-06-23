#include "risk_manager.hpp"
#include "../utils/config_manager.hpp"
#include "../utils/logger.hpp"
#include <algorithm>
#include <iomanip>
#include <sstream>
#include <cmath>

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
        
        // Check reward:risk ratio
        double max_position = CalculateMaxPositionSize(opportunity);
        if (max_position <= 0) {
            assessment.rejections.push_back("Position size limit exceeded");
            return assessment;
        }
        
        double reward_risk_ratio = CalculateRewardRiskRatio(opportunity, max_position);
        if (reward_risk_ratio < limits_.min_reward_risk_ratio) {
            assessment.rejections.push_back("Reward:risk ratio below minimum threshold");
            LOG_DEBUG("Opportunity rejected: reward:risk ratio {} < minimum {}", 
                     reward_risk_ratio, limits_.min_reward_risk_ratio);
            return assessment;
        }
        
        // Check minimum profit threshold after reward:risk validation
        if (opportunity.net_profit_percent < 0.05) { // 0.05% minimum profit
            assessment.rejections.push_back("Profit below minimum threshold");
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
    auto now = std::chrono::system_clock::now();
    auto time_t = std::chrono::system_clock::to_time_t(now);
    auto tm = *std::gmtime(&time_t);
    
    // Crypto markets are 24/7, but we can still have maintenance windows
    // For traditional markets, implement proper market hours
    
    // Check for maintenance windows (e.g., Sunday 00:00-02:00 UTC)
    if (tm.tm_wday == 0 && tm.tm_hour >= 0 && tm.tm_hour < 2) {
        return false; // Maintenance window
    }
    
    // Allow trading during all other times for crypto
    return true;
}

bool RiskManager::IsVolatilityAcceptable(const std::string& symbol) const {
    // Simple volatility check based on recent price movements
    // In production, this would analyze historical price data
    
    std::lock_guard<std::mutex> lock(trades_mutex_);
    
    // Count recent trades for this symbol to gauge activity
    auto now = std::chrono::system_clock::now();
    auto one_hour_ago = now - std::chrono::hours(1);
    
    int recent_trades = 0;
    double price_variance = 0.0;
    std::vector<double> recent_prices;
    
    for (const auto& trade : trade_history_) {
        if (trade.symbol == symbol && trade.start_time >= one_hour_ago) {
            recent_trades++;
            recent_prices.push_back(trade.buy_price);
        }
    }
    
    if (recent_prices.size() < 2) {
        return true; // Not enough data, allow trading
    }
    
    // Calculate simple price variance
    double sum = 0.0;
    for (double price : recent_prices) {
        sum += price;
    }
    double mean = sum / recent_prices.size();
    
    double variance_sum = 0.0;
    for (double price : recent_prices) {
        variance_sum += (price - mean) * (price - mean);
    }
    price_variance = variance_sum / recent_prices.size();
    
    // Calculate coefficient of variation (volatility measure)
    double cv = (mean > 0) ? sqrt(price_variance) / mean : 0.0;
    
    // Reject if volatility is too high (coefficient of variation > 5%)
    return cv <= limits_.max_volatility_threshold / 100.0;
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
    std::lock_guard<std::mutex> lock(trades_mutex_);
    
    if (trade_history_.empty()) return 0.0;
    
    // Collect daily returns
    std::vector<double> daily_returns;
    std::unordered_map<std::string, double> daily_pnl; // date -> total pnl
    
    for (const auto& trade : trade_history_) {
        if (!trade.is_completed) continue;
        
        // Convert timestamp to date string (YYYY-MM-DD)
        auto trade_time = trade.end_time;
        auto time_t = std::chrono::system_clock::to_time_t(trade_time);
        auto tm = *std::gmtime(&time_t);
        
        std::ostringstream date_stream;
        date_stream << std::put_time(&tm, "%Y-%m-%d");
        std::string date = date_stream.str();
        
        daily_pnl[date] += trade.realized_pnl;
    }
    
    // Convert to daily returns vector
    for (const auto& pair : daily_pnl) {
        daily_returns.push_back(pair.second);
    }
    
    if (daily_returns.size() < 2) return 0.0;
    
    // Sort returns in ascending order
    std::sort(daily_returns.begin(), daily_returns.end());
    
    // Calculate VaR at given confidence level
    size_t var_index = static_cast<size_t>((1.0 - confidence_level) * daily_returns.size());
    if (var_index >= daily_returns.size()) var_index = daily_returns.size() - 1;
    
    return std::abs(daily_returns[var_index]); // Return positive VaR
}

double RiskManager::CalculateMaxDrawdown() const {
    std::lock_guard<std::mutex> lock(trades_mutex_);
    
    if (trade_history_.empty()) return 0.0;
    
    // Build cumulative P&L curve
    std::vector<std::pair<std::chrono::system_clock::time_point, double>> pnl_curve;
    double cumulative_pnl = 0.0;
    
    for (const auto& trade : trade_history_) {
        if (trade.is_completed) {
            cumulative_pnl += trade.realized_pnl;
            pnl_curve.push_back({trade.end_time, cumulative_pnl});
        }
    }
    
    if (pnl_curve.size() < 2) return 0.0;
    
    // Sort by time
    std::sort(pnl_curve.begin(), pnl_curve.end());
    
    // Calculate maximum drawdown
    double max_drawdown = 0.0;
    double peak = pnl_curve[0].second;
    
    for (const auto& point : pnl_curve) {
        if (point.second > peak) {
            peak = point.second;
        } else {
            double drawdown = peak - point.second;
            max_drawdown = std::max(max_drawdown, drawdown);
        }
    }
    
    return max_drawdown;
}

double RiskManager::CalculateSharpeRatio() const {
    std::lock_guard<std::mutex> lock(trades_mutex_);
    
    if (trade_history_.empty()) return 0.0;
    
    // Collect completed trade returns
    std::vector<double> returns;
    for (const auto& trade : trade_history_) {
        if (trade.is_completed && trade.volume > 0) {
            // Calculate return as percentage
            double return_pct = (trade.realized_pnl / trade.volume) * 100.0;
            returns.push_back(return_pct);
        }
    }
    
    if (returns.size() < 2) return 0.0;
    
    // Calculate mean return
    double mean_return = 0.0;
    for (double ret : returns) {
        mean_return += ret;
    }
    mean_return /= returns.size();
    
    // Calculate standard deviation
    double variance = 0.0;
    for (double ret : returns) {
        variance += (ret - mean_return) * (ret - mean_return);
    }
    variance /= (returns.size() - 1);
    double std_dev = std::sqrt(variance);
    
    if (std_dev == 0.0) return 0.0;
    
    // Assume risk-free rate of 0 for simplicity
    return mean_return / std_dev;
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
    std::lock_guard<std::mutex> lock1(trades_mutex_);
    std::lock_guard<std::mutex> lock2(positions_mutex_);
    
    daily_pnl_ = 0.0;
    
    // Reset daily statistics
    auto now = std::chrono::system_clock::now();
    auto midnight = now - std::chrono::hours(24);
    
    // Keep only today's trades for daily calculations
    auto it = std::remove_if(trade_history_.begin(), trade_history_.end(),
        [this, midnight](const TradeRecord& trade) {
            return trade.start_time < midnight && !IsSameDay(trade.start_time, std::chrono::system_clock::now());
        });
    
    // Don't actually remove them, just mark for daily reset
    LOG_INFO("Daily reset performed - cleared daily P&L");
}

void RiskManager::PerformWeeklyReset() {
    std::lock_guard<std::mutex> lock1(trades_mutex_);
    std::lock_guard<std::mutex> lock2(positions_mutex_);
    
    weekly_pnl_ = 0.0;
    
    // Reset weekly statistics
    auto now = std::chrono::system_clock::now();
    auto week_ago = now - std::chrono::hours(24 * 7);
    
    LOG_INFO("Weekly reset performed - cleared weekly P&L");
}

void RiskManager::PerformMonthlyReset() {
    std::lock_guard<std::mutex> lock1(trades_mutex_);
    std::lock_guard<std::mutex> lock2(positions_mutex_);
    
    monthly_pnl_ = 0.0;
    
    // Reset monthly statistics
    auto now = std::chrono::system_clock::now();
    auto month_ago = now - std::chrono::hours(24 * 30);
    
    LOG_INFO("Monthly reset performed - cleared monthly P&L");
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