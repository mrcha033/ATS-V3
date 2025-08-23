#include "../include/performance_metrics.hpp"
#include "utils/logger.hpp"
#include <algorithm>
#include <numeric>
#include <cmath>
#include <sstream>
#include <iomanip>
#include <random>

namespace ats {
namespace backtest {

PerformanceCalculator::PerformanceCalculator() = default;
PerformanceCalculator::~PerformanceCalculator() = default;

PerformanceMetrics PerformanceCalculator::calculate_metrics(
    const std::vector<TradeResult>& trades,
    const std::vector<PortfolioSnapshot>& portfolio_history,
    double initial_capital,
    double risk_free_rate) {
    
    PerformanceMetrics metrics;
    
    if (portfolio_history.empty()) {
        throw InsufficientDataException("Portfolio history is empty");
    }
    
    // Basic time period information
    metrics.start_date = portfolio_history.front().timestamp;
    metrics.end_date = portfolio_history.back().timestamp;
    metrics.trading_days = calculate_trading_days(metrics.start_date, metrics.end_date);
    
    if (metrics.trading_days == 0) {
        throw InsufficientDataException("No trading days found in the period");
    }
    
    // Portfolio values and returns
    std::vector<double> portfolio_values;
    for (const auto& snapshot : portfolio_history) {
        portfolio_values.push_back(snapshot.total_value);
    }
    
    std::vector<double> returns = calculate_returns_from_portfolio(portfolio_history);
    
    if (returns.empty()) {
        throw InsufficientDataException("Cannot calculate returns from portfolio history");
    }
    
    // Return metrics
    double final_value = portfolio_values.back();
    metrics.total_return = ((final_value - initial_capital) / initial_capital) * 100.0;
    metrics.annualized_return = annualize_return(metrics.total_return / 100.0, metrics.trading_days) * 100.0;
    
    // Calculate monthly returns for average
    std::vector<double> monthly_returns;
    auto current_month = std::chrono::system_clock::to_time_t(metrics.start_date);
    double month_start_value = initial_capital;
    
    for (size_t i = 0; i < portfolio_history.size(); ++i) {
        auto snapshot_time = std::chrono::system_clock::to_time_t(portfolio_history[i].timestamp);
        auto snapshot_tm = *std::localtime(&snapshot_time);
        auto current_tm = *std::localtime(&current_month);
        
        if (snapshot_tm.tm_mon != current_tm.tm_mon || i == portfolio_history.size() - 1) {
            if (month_start_value > 0) {
                double month_return = ((portfolio_history[i].total_value - month_start_value) / month_start_value) * 100.0;
                monthly_returns.push_back(month_return);
            }
            month_start_value = portfolio_history[i].total_value;
            current_month = snapshot_time;
        }
    }
    
    if (!monthly_returns.empty()) {
        metrics.average_monthly_return = mean(monthly_returns);
    }
    
    // Geometric mean return
    double product = 1.0;
    for (double ret : returns) {
        product *= (1.0 + ret);
    }
    metrics.geometric_mean_return = (std::pow(product, 1.0 / returns.size()) - 1.0) * 100.0;
    
    // Risk metrics
    metrics.volatility = calculate_volatility(returns, true) * 100.0;
    
    int dd_start, dd_end;
    metrics.max_drawdown = calculate_max_drawdown(portfolio_values, dd_start, dd_end) * 100.0;
    
    std::vector<double> drawdown_series = calculate_drawdown_series(portfolio_values);
    auto dd_duration = find_max_drawdown_duration(drawdown_series);
    metrics.max_drawdown_duration_days = dd_duration.second;
    
    metrics.value_at_risk_95 = calculate_value_at_risk(returns, 0.95) * 100.0;
    metrics.conditional_var_95 = calculate_conditional_var(returns, 0.95) * 100.0;
    
    // Risk-adjusted return metrics
    metrics.sharpe_ratio = calculate_sharpe_ratio(returns, risk_free_rate / 252.0); // Daily risk-free rate
    metrics.sortino_ratio = calculate_sortino_ratio(returns, 0.0);
    metrics.calmar_ratio = calculate_calmar_ratio(metrics.annualized_return / 100.0, metrics.max_drawdown / 100.0);
    
    // Trading metrics
    metrics.total_trades = static_cast<int>(trades.size());
    
    double total_profit = 0.0, total_loss = 0.0;
    double largest_win = 0.0, largest_loss = 0.0;
    
    for (const auto& trade : trades) {
        if (trade.is_profitable) {
            metrics.winning_trades++;
            total_profit += trade.net_pnl;
            largest_win = std::max(largest_win, trade.net_pnl);
        } else {
            metrics.losing_trades++;
            total_loss += std::abs(trade.net_pnl);
            largest_loss = std::min(largest_loss, trade.net_pnl);
        }
    }
    
    if (metrics.total_trades > 0) {
        metrics.win_rate = (static_cast<double>(metrics.winning_trades) / metrics.total_trades) * 100.0;
        metrics.average_trades_per_day = static_cast<double>(metrics.total_trades) / metrics.trading_days;
    }
    
    if (metrics.winning_trades > 0) {
        metrics.average_win = total_profit / metrics.winning_trades;
    }
    
    if (metrics.losing_trades > 0) {
        metrics.average_loss = total_loss / metrics.losing_trades;
    }
    
    if (total_loss > 0) {
        metrics.profit_factor = total_profit / total_loss;
    }
    
    metrics.largest_win = largest_win;
    metrics.largest_loss = largest_loss;
    
    // Additional metrics
    if (metrics.max_drawdown != 0) {
        metrics.recovery_factor = metrics.total_return / std::abs(metrics.max_drawdown);
    }
    
    metrics.kelly_criterion = calculate_kelly_criterion(trades) * 100.0;
    
    // Statistical metrics
    metrics.skewness = calculate_skewness(returns);
    metrics.kurtosis = calculate_kurtosis(returns);
    
    if (!returns.empty()) {
        std::vector<double> sorted_returns = returns;
        std::sort(sorted_returns.begin(), sorted_returns.end());
        
        double p95 = percentile(sorted_returns, 0.95);
        double p5 = percentile(sorted_returns, 0.05);
        
        if (p5 != 0) {
            metrics.tail_ratio = p95 / std::abs(p5);
        }
    }
    
    ATS_LOG_INFO("Calculated performance metrics: Total Return: {:.2f}%, Sharpe: {:.3f}, Max DD: {:.2f}%",
             metrics.total_return, metrics.sharpe_ratio, metrics.max_drawdown);
    
    return metrics;
}

double PerformanceCalculator::calculate_sharpe_ratio(const std::vector<double>& returns, 
                                                    double risk_free_rate) {
    if (returns.empty()) {
        return 0.0;
    }
    
    double mean_return = mean(returns);
    double excess_return = mean_return - risk_free_rate;
    double volatility = standard_deviation(returns);
    
    if (volatility == 0.0) {
        return 0.0;
    }
    
    return excess_return / volatility;
}

double PerformanceCalculator::calculate_sortino_ratio(const std::vector<double>& returns, 
                                                     double target_return) {
    if (returns.empty()) {
        return 0.0;
    }
    
    double mean_return = mean(returns);
    double excess_return = mean_return - target_return;
    
    // Calculate downside deviation
    double sum_negative_squared = 0.0;
    int negative_count = 0;
    
    for (double ret : returns) {
        if (ret < target_return) {
            double diff = ret - target_return;
            sum_negative_squared += diff * diff;
            negative_count++;
        }
    }
    
    if (negative_count == 0) {
        return std::numeric_limits<double>::infinity();
    }
    
    double downside_deviation = std::sqrt(sum_negative_squared / negative_count);
    
    if (downside_deviation == 0.0) {
        return 0.0;
    }
    
    return excess_return / downside_deviation;
}

double PerformanceCalculator::calculate_max_drawdown(const std::vector<double>& portfolio_values,
                                                    int& start_index, int& end_index) {
    if (portfolio_values.empty()) {
        return 0.0;
    }
    
    double max_drawdown = 0.0;
    double peak = portfolio_values[0];
    start_index = 0;
    end_index = 0;
    int temp_start = 0;
    
    for (size_t i = 1; i < portfolio_values.size(); ++i) {
        if (portfolio_values[i] > peak) {
            peak = portfolio_values[i];
            temp_start = static_cast<int>(i);
        } else {
            double drawdown = (peak - portfolio_values[i]) / peak;
            if (drawdown > max_drawdown) {
                max_drawdown = drawdown;
                start_index = temp_start;
                end_index = static_cast<int>(i);
            }
        }
    }
    
    return max_drawdown;
}

double PerformanceCalculator::calculate_calmar_ratio(double annual_return, double max_drawdown) {
    if (max_drawdown == 0.0) {
        return std::numeric_limits<double>::infinity();
    }
    
    return annual_return / std::abs(max_drawdown);
}

double PerformanceCalculator::calculate_value_at_risk(const std::vector<double>& returns, 
                                                     double confidence_level) {
    if (returns.empty()) {
        return 0.0;
    }
    
    std::vector<double> sorted_returns = returns;
    std::sort(sorted_returns.begin(), sorted_returns.end());
    
    return percentile(sorted_returns, 1.0 - confidence_level);
}

double PerformanceCalculator::calculate_conditional_var(const std::vector<double>& returns, 
                                                       double confidence_level) {
    if (returns.empty()) {
        return 0.0;
    }
    
    std::vector<double> sorted_returns = returns;
    std::sort(sorted_returns.begin(), sorted_returns.end());
    
    double var = percentile(sorted_returns, 1.0 - confidence_level);
    
    // Calculate mean of returns below VaR
    double sum = 0.0;
    int count = 0;
    
    for (double ret : sorted_returns) {
        if (ret <= var) {
            sum += ret;
            count++;
        }
    }
    
    return count > 0 ? sum / count : var;
}

double PerformanceCalculator::calculate_volatility(const std::vector<double>& returns, bool annualize) {
    if (returns.empty()) {
        return 0.0;
    }
    
    double vol = standard_deviation(returns);
    
    if (annualize) {
        vol = annualize_volatility(vol);
    }
    
    return vol;
}

double PerformanceCalculator::calculate_skewness(const std::vector<double>& returns) {
    if (returns.size() < 3) {
        return 0.0;
    }
    
    double mean_val = mean(returns);
    double std_dev = standard_deviation(returns);
    
    if (std_dev == 0.0) {
        return 0.0;
    }
    
    double sum_cubed = 0.0;
    for (double ret : returns) {
        double normalized = (ret - mean_val) / std_dev;
        sum_cubed += normalized * normalized * normalized;
    }
    
    return sum_cubed / returns.size();
}

double PerformanceCalculator::calculate_kurtosis(const std::vector<double>& returns) {
    if (returns.size() < 4) {
        return 0.0;
    }
    
    double mean_val = mean(returns);
    double std_dev = standard_deviation(returns);
    
    if (std_dev == 0.0) {
        return 0.0;
    }
    
    double sum_fourth = 0.0;
    for (double ret : returns) {
        double normalized = (ret - mean_val) / std_dev;
        sum_fourth += normalized * normalized * normalized * normalized;
    }
    
    return (sum_fourth / returns.size()) - 3.0; // Excess kurtosis
}

double PerformanceCalculator::calculate_kelly_criterion(const std::vector<TradeResult>& trades) {
    if (trades.empty()) {
        return 0.0;
    }
    
    int wins = 0;
    double total_win_return = 0.0;
    double total_loss_return = 0.0;
    
    for (const auto& trade : trades) {
        if (trade.is_profitable) {
            wins++;
            total_win_return += trade.pnl_percentage / 100.0;
        } else {
            total_loss_return += std::abs(trade.pnl_percentage / 100.0);
        }
    }
    
    if (wins == 0 || wins == static_cast<int>(trades.size())) {
        return 0.0;
    }
    
    double win_prob = static_cast<double>(wins) / trades.size();
    double loss_prob = 1.0 - win_prob;
    double avg_win = total_win_return / wins;
    double avg_loss = total_loss_return / (trades.size() - wins);
    
    if (avg_loss == 0.0) {
        return 0.0;
    }
    
    // Kelly formula: f = (bp - q) / b
    // where b = avg_win/avg_loss, p = win_prob, q = loss_prob
    double b = avg_win / avg_loss;
    return (b * win_prob - loss_prob) / b;
}

std::vector<double> PerformanceCalculator::calculate_returns_from_portfolio(
    const std::vector<PortfolioSnapshot>& portfolio_history) {
    
    std::vector<double> returns;
    
    if (portfolio_history.size() < 2) {
        return returns;
    }
    
    for (size_t i = 1; i < portfolio_history.size(); ++i) {
        double prev_value = portfolio_history[i-1].total_value;
        double curr_value = portfolio_history[i].total_value;
        
        if (prev_value > 0.0) {
            double ret = (curr_value - prev_value) / prev_value;
            returns.push_back(ret);
        }
    }
    
    return returns;
}

std::vector<double> PerformanceCalculator::calculate_drawdown_series(
    const std::vector<double>& portfolio_values) {
    
    std::vector<double> drawdowns;
    
    if (portfolio_values.empty()) {
        return drawdowns;
    }
    
    double peak = portfolio_values[0];
    drawdowns.push_back(0.0);
    
    for (size_t i = 1; i < portfolio_values.size(); ++i) {
        peak = std::max(peak, portfolio_values[i]);
        double drawdown = (peak - portfolio_values[i]) / peak;
        drawdowns.push_back(drawdown);
    }
    
    return drawdowns;
}

// Helper functions
double PerformanceCalculator::mean(const std::vector<double>& data) {
    if (data.empty()) {
        return 0.0;
    }
    
    return std::accumulate(data.begin(), data.end(), 0.0) / data.size();
}

double PerformanceCalculator::standard_deviation(const std::vector<double>& data) {
    if (data.size() < 2) {
        return 0.0;
    }
    
    double mean_val = mean(data);
    double sum_squared_diff = 0.0;
    
    for (double val : data) {
        double diff = val - mean_val;
        sum_squared_diff += diff * diff;
    }
    
    return std::sqrt(sum_squared_diff / (data.size() - 1));
}

double PerformanceCalculator::percentile(const std::vector<double>& data, double p) {
    if (data.empty()) {
        return 0.0;
    }
    
    if (p <= 0.0) {
        return data.front();
    }
    if (p >= 1.0) {
        return data.back();
    }
    
    double index = p * (data.size() - 1);
    size_t lower = static_cast<size_t>(std::floor(index));
    size_t upper = static_cast<size_t>(std::ceil(index));
    
    if (lower == upper) {
        return data[lower];
    }
    
    double weight = index - lower;
    return data[lower] * (1.0 - weight) + data[upper] * weight;
}

double PerformanceCalculator::annualize_return(double total_return, int num_days) {
    if (num_days <= 0) {
        return 0.0;
    }
    
    double years = static_cast<double>(num_days) / trading_days_per_year_;
    return std::pow(1.0 + total_return, 1.0 / years) - 1.0;
}

double PerformanceCalculator::annualize_volatility(double daily_volatility) {
    return daily_volatility * std::sqrt(static_cast<double>(trading_days_per_year_));
}

int PerformanceCalculator::calculate_trading_days(std::chrono::system_clock::time_point start,
                                                 std::chrono::system_clock::time_point end) {
    auto duration = end - start;
    int total_days = std::chrono::duration_cast<std::chrono::hours>(duration).count() / 24;
    
    // Approximate trading days (excluding weekends)
    // This is a simplified calculation - in practice would exclude holidays too
    return static_cast<int>(total_days * (5.0 / 7.0));
}

std::pair<double, int> PerformanceCalculator::find_max_drawdown_duration(
    const std::vector<double>& drawdown_series) {
    
    if (drawdown_series.empty()) {
        return {0.0, 0};
    }
    
    int max_duration = 0;
    int current_duration = 0;
    double max_dd_in_period = 0.0;
    
    for (double dd : drawdown_series) {
        if (dd > 0.0) {
            current_duration++;
            max_dd_in_period = std::max(max_dd_in_period, dd);
        } else {
            if (current_duration > max_duration) {
                max_duration = current_duration;
            }
            current_duration = 0;
            max_dd_in_period = 0.0;
        }
    }
    
    // Check final period
    if (current_duration > max_duration) {
        max_duration = current_duration;
    }
    
    return {max_dd_in_period, max_duration};
}

// Performance Attribution
PerformanceAttribution PerformanceCalculator::calculate_attribution(
    const std::vector<TradeResult>& trades) {
    
    PerformanceAttribution attribution;
    
    for (const auto& trade : trades) {
        // By symbol
        attribution.symbol_pnl[trade.symbol] += trade.net_pnl;
        
        // By exchange
        attribution.exchange_pnl[trade.exchange] += trade.net_pnl;
        
        // By strategy
        attribution.strategy_pnl[trade.strategy_name] += trade.net_pnl;
        
        // By month
        auto trade_time = std::chrono::system_clock::to_time_t(trade.exit_time);
        auto tm = *std::localtime(&trade_time);
        int month_key = tm.tm_year * 100 + tm.tm_mon;
        attribution.monthly_pnl[month_key] += trade.net_pnl;
        
        // By hour
        attribution.hourly_pnl[tm.tm_hour] += trade.net_pnl;
        
        // By weekday
        attribution.weekday_pnl[tm.tm_wday] += trade.net_pnl;
    }
    
    return attribution;
}

// PerformanceReporter Implementation
PerformanceReporter::PerformanceReporter() = default;
PerformanceReporter::~PerformanceReporter() = default;

std::string PerformanceReporter::generate_text_report(
    const PerformanceMetrics& metrics,
    const PerformanceAttribution& attribution) {
    
    std::ostringstream report;
    
    report << "=== BACKTEST PERFORMANCE REPORT ===\n\n";
    
    // Time period
    report << "Analysis Period:\n";
    report << "  Start Date: " << format_date(metrics.start_date) << "\n";
    report << "  End Date: " << format_date(metrics.end_date) << "\n";
    report << "  Trading Days: " << metrics.trading_days << "\n\n";
    
    // Return metrics
    report << "Return Metrics:\n";
    report << "  Total Return: " << format_percentage(metrics.total_return) << "\n";
    report << "  Annualized Return: " << format_percentage(metrics.annualized_return) << "\n";
    report << "  Average Monthly Return: " << format_percentage(metrics.average_monthly_return) << "\n\n";
    
    // Risk metrics
    report << "Risk Metrics:\n";
    report << "  Volatility (Annualized): " << format_percentage(metrics.volatility) << "\n";
    report << "  Maximum Drawdown: " << format_percentage(metrics.max_drawdown) << "\n";
    report << "  Max DD Duration: " << format_number(metrics.max_drawdown_duration_days, 0) << " days\n";
    report << "  Value at Risk (95%): " << format_percentage(metrics.value_at_risk_95) << "\n\n";
    
    // Risk-adjusted metrics
    report << "Risk-Adjusted Metrics:\n";
    report << "  Sharpe Ratio: " << format_number(metrics.sharpe_ratio, 3) << "\n";
    report << "  Sortino Ratio: " << format_number(metrics.sortino_ratio, 3) << "\n";
    report << "  Calmar Ratio: " << format_number(metrics.calmar_ratio, 3) << "\n\n";
    
    // Trading metrics
    report << "Trading Statistics:\n";
    report << "  Total Trades: " << metrics.total_trades << "\n";
    report << "  Winning Trades: " << metrics.winning_trades << "\n";
    report << "  Losing Trades: " << metrics.losing_trades << "\n";
    report << "  Win Rate: " << format_percentage(metrics.win_rate) << "\n";
    report << "  Profit Factor: " << format_number(metrics.profit_factor, 2) << "\n";
    report << "  Average Win: " << format_currency(metrics.average_win) << "\n";
    report << "  Average Loss: " << format_currency(metrics.average_loss) << "\n\n";
    
    return report.str();
}

std::string PerformanceReporter::format_percentage(double value, int decimal_places) {
    std::ostringstream ss;
    ss << std::fixed << std::setprecision(decimal_places) << value << "%";
    return ss.str();
}

std::string PerformanceReporter::format_currency(double value, const std::string& currency) {
    std::ostringstream ss;
    ss << "$" << std::fixed << std::setprecision(2) << value;
    return ss.str();
}

std::string PerformanceReporter::format_number(double value, int decimal_places) {
    std::ostringstream ss;
    ss << std::fixed << std::setprecision(decimal_places) << value;
    return ss.str();
}

std::string PerformanceReporter::format_date(std::chrono::system_clock::time_point date) {
    auto time_t_date = std::chrono::system_clock::to_time_t(date);
    auto tm = *std::localtime(&time_t_date);
    
    std::ostringstream ss;
    ss << std::put_time(&tm, "%Y-%m-%d %H:%M:%S");
    return ss.str();
}

} // namespace backtest
} // namespace ats