#pragma once

#include "data_loader.hpp"
#include <vector>
#include <chrono>
#include <string>
#include <unordered_map>
#include <cmath>

namespace ats {
namespace backtest {

// Trade result for performance calculation
struct TradeResult {
    std::chrono::system_clock::time_point entry_time;
    std::chrono::system_clock::time_point exit_time;
    std::string symbol;
    std::string exchange;
    std::string strategy_name;
    double entry_price = 0.0;
    double exit_price = 0.0;
    double quantity = 0.0;
    double pnl = 0.0;           // Profit and Loss
    double pnl_percentage = 0.0; // P&L as percentage
    double fees = 0.0;
    double net_pnl = 0.0;       // P&L after fees
    bool is_profitable = false;
    std::string side;           // "long" or "short"
    
    TradeResult() = default;
    TradeResult(std::chrono::system_clock::time_point entry, std::chrono::system_clock::time_point exit,
               const std::string& sym, double entry_px, double exit_px, double qty, 
               const std::string& side_val, double fee = 0.0)
        : entry_time(entry), exit_time(exit), symbol(sym), entry_price(entry_px), 
          exit_price(exit_px), quantity(qty), fees(fee), side(side_val) {
        calculate_pnl();
    }
    
    void calculate_pnl() {
        if (side == "long") {
            pnl = (exit_price - entry_price) * quantity;
        } else { // short
            pnl = (entry_price - exit_price) * quantity;
        }
        pnl_percentage = (pnl / (entry_price * quantity)) * 100.0;
        net_pnl = pnl - fees;
        is_profitable = net_pnl > 0.0;
    }
};

// Portfolio value over time for drawdown calculation
struct PortfolioSnapshot {
    std::chrono::system_clock::time_point timestamp;
    double total_value = 0.0;
    double cash = 0.0;
    double positions_value = 0.0;
    std::unordered_map<std::string, double> positions; // symbol -> quantity
    std::vector<TradeResult> pending_trades;
    
    PortfolioSnapshot() = default;
    PortfolioSnapshot(std::chrono::system_clock::time_point ts, double value)
        : timestamp(ts), total_value(value) {}
};

// Performance metrics result
struct PerformanceMetrics {
    // Return metrics
    double total_return = 0.0;              // Total return percentage
    double annualized_return = 0.0;         // Annualized return percentage
    double average_monthly_return = 0.0;    // Average monthly return
    double geometric_mean_return = 0.0;     // Geometric mean of returns
    
    // Risk metrics
    double volatility = 0.0;                // Annualized volatility (standard deviation)
    double max_drawdown = 0.0;              // Maximum drawdown percentage
    double max_drawdown_duration_days = 0.0; // Duration of max drawdown in days
    double value_at_risk_95 = 0.0;          // 95% Value at Risk
    double conditional_var_95 = 0.0;        // 95% Conditional Value at Risk
    
    // Risk-adjusted return metrics
    double sharpe_ratio = 0.0;              // Sharpe ratio (risk-free rate assumed 0)
    double sortino_ratio = 0.0;             // Sortino ratio
    double calmar_ratio = 0.0;              // Calmar ratio
    double information_ratio = 0.0;         // Information ratio vs benchmark
    
    // Trading metrics
    int total_trades = 0;                   // Total number of trades
    int winning_trades = 0;                 // Number of profitable trades
    int losing_trades = 0;                  // Number of losing trades
    double win_rate = 0.0;                  // Percentage of winning trades
    double average_win = 0.0;               // Average profit of winning trades
    double average_loss = 0.0;              // Average loss of losing trades
    double profit_factor = 0.0;             // Ratio of gross profit to gross loss
    double largest_win = 0.0;               // Largest single trade profit
    double largest_loss = 0.0;              // Largest single trade loss
    
    // Time-based metrics
    std::chrono::system_clock::time_point start_date;
    std::chrono::system_clock::time_point end_date;
    int trading_days = 0;                   // Number of trading days
    double average_trades_per_day = 0.0;    // Average trades per trading day
    
    // Benchmark comparison
    double benchmark_return = 0.0;          // Benchmark total return
    double alpha = 0.0;                     // Alpha vs benchmark
    double beta = 0.0;                      // Beta vs benchmark
    double correlation = 0.0;               // Correlation with benchmark
    
    // Additional metrics
    double recovery_factor = 0.0;           // Total return / Max Drawdown
    double kelly_criterion = 0.0;           // Optimal position size
    double tail_ratio = 0.0;               // 95th percentile return / 5th percentile return
    double skewness = 0.0;                  // Return distribution skewness
    double kurtosis = 0.0;                  // Return distribution kurtosis
};

// Rolling window performance metrics
struct RollingMetrics {
    std::vector<std::chrono::system_clock::time_point> timestamps;
    std::vector<double> rolling_returns;
    std::vector<double> rolling_sharpe;
    std::vector<double> rolling_volatility;
    std::vector<double> rolling_max_drawdown;
    int window_days = 30;
    
    RollingMetrics(int window = 30) : window_days(window) {}
};

// Performance attribution
struct PerformanceAttribution {
    std::unordered_map<std::string, double> symbol_pnl;      // P&L by symbol
    std::unordered_map<std::string, double> exchange_pnl;    // P&L by exchange
    std::unordered_map<std::string, double> strategy_pnl;    // P&L by strategy
    std::unordered_map<int, double> monthly_pnl;             // P&L by month
    std::unordered_map<int, double> hourly_pnl;              // P&L by hour of day
    std::unordered_map<int, double> weekday_pnl;             // P&L by day of week
};

// Performance metrics calculator
class PerformanceCalculator {
public:
    PerformanceCalculator();
    ~PerformanceCalculator();
    
    // Calculate comprehensive performance metrics
    PerformanceMetrics calculate_metrics(const std::vector<TradeResult>& trades,
                                        const std::vector<PortfolioSnapshot>& portfolio_history,
                                        double initial_capital = 100000.0,
                                        double risk_free_rate = 0.02);
    
    // Calculate metrics with benchmark comparison
    PerformanceMetrics calculate_metrics_with_benchmark(
        const std::vector<TradeResult>& trades,
        const std::vector<PortfolioSnapshot>& portfolio_history,
        const std::vector<double>& benchmark_returns,
        double initial_capital = 100000.0,
        double risk_free_rate = 0.02);
    
    // Calculate rolling performance metrics
    RollingMetrics calculate_rolling_metrics(
        const std::vector<PortfolioSnapshot>& portfolio_history,
        int window_days = 30);
    
    // Performance attribution analysis
    PerformanceAttribution calculate_attribution(const std::vector<TradeResult>& trades);
    
    // Individual metric calculations
    double calculate_sharpe_ratio(const std::vector<double>& returns, 
                                 double risk_free_rate = 0.0);
    double calculate_sortino_ratio(const std::vector<double>& returns, 
                                  double target_return = 0.0);
    double calculate_max_drawdown(const std::vector<double>& portfolio_values,
                                 int& start_index, int& end_index);
    double calculate_calmar_ratio(double annual_return, double max_drawdown);
    double calculate_value_at_risk(const std::vector<double>& returns, 
                                  double confidence_level = 0.95);
    double calculate_conditional_var(const std::vector<double>& returns, 
                                    double confidence_level = 0.95);
    
    // Statistical calculations
    double calculate_volatility(const std::vector<double>& returns, bool annualize = true);
    double calculate_skewness(const std::vector<double>& returns);
    double calculate_kurtosis(const std::vector<double>& returns);
    double calculate_correlation(const std::vector<double>& returns1,
                                const std::vector<double>& returns2);
    double calculate_beta(const std::vector<double>& portfolio_returns,
                         const std::vector<double>& benchmark_returns);
    
    // Utility functions
    std::vector<double> calculate_returns_from_portfolio(
        const std::vector<PortfolioSnapshot>& portfolio_history);
    std::vector<double> calculate_log_returns(const std::vector<double>& prices);
    std::vector<double> calculate_daily_returns(const std::vector<double>& prices);
    
    // Risk management metrics
    double calculate_kelly_criterion(const std::vector<TradeResult>& trades);
    double calculate_optimal_f(const std::vector<TradeResult>& trades);
    double calculate_profit_factor(const std::vector<TradeResult>& trades);
    
    // Time series analysis
    std::vector<double> moving_average(const std::vector<double>& data, int window);
    std::vector<double> exponential_moving_average(const std::vector<double>& data, double alpha);
    double calculate_autocorrelation(const std::vector<double>& returns, int lag = 1);
    
    // Monte Carlo analysis support
    struct MonteCarloResult {
        std::vector<double> simulated_returns;
        double confidence_interval_95_lower;
        double confidence_interval_95_upper;
        double expected_return;
        double probability_of_loss;
        double worst_case_scenario;
        double best_case_scenario;
    };
    
    MonteCarloResult run_monte_carlo_simulation(
        const std::vector<double>& historical_returns,
        int simulation_days = 252,
        int num_simulations = 10000);
    
    // Benchmark utilities
    std::vector<double> load_benchmark_data(const std::string& benchmark_symbol,
                                           std::chrono::system_clock::time_point start_date,
                                           std::chrono::system_clock::time_point end_date);
    
    // Configuration
    void set_risk_free_rate(double rate) { risk_free_rate_ = rate; }
    void set_trading_days_per_year(int days) { trading_days_per_year_ = days; }
    void set_confidence_level(double level) { confidence_level_ = level; }
    
private:
    double risk_free_rate_ = 0.02;      // 2% annual risk-free rate
    int trading_days_per_year_ = 252;   // Standard trading days per year
    double confidence_level_ = 0.95;    // 95% confidence level
    
    // Helper functions
    double annualize_return(double total_return, int num_days);
    double annualize_volatility(double daily_volatility);
    std::vector<double> calculate_drawdown_series(const std::vector<double>& portfolio_values);
    std::pair<double, int> find_max_drawdown_duration(const std::vector<double>& drawdown_series);
    
    // Statistical helper functions
    double mean(const std::vector<double>& data);
    double standard_deviation(const std::vector<double>& data);
    double percentile(const std::vector<double>& data, double p);
    std::pair<double, double> linear_regression(const std::vector<double>& x, 
                                              const std::vector<double>& y);
    
    // Date/time utilities
    int calculate_trading_days(std::chrono::system_clock::time_point start,
                             std::chrono::system_clock::time_point end);
    bool is_trading_day(std::chrono::system_clock::time_point date);
};

// Performance report generator
class PerformanceReporter {
public:
    PerformanceReporter();
    ~PerformanceReporter();
    
    // Generate comprehensive performance report
    std::string generate_text_report(const PerformanceMetrics& metrics,
                                    const PerformanceAttribution& attribution = {});
    
    // Generate JSON report for API/UI consumption
    std::string generate_json_report(const PerformanceMetrics& metrics,
                                    const RollingMetrics& rolling_metrics = {},
                                    const PerformanceAttribution& attribution = {});
    
    // Generate HTML report with charts
    std::string generate_html_report(const PerformanceMetrics& metrics,
                                    const std::vector<PortfolioSnapshot>& portfolio_history,
                                    const RollingMetrics& rolling_metrics = {},
                                    const PerformanceAttribution& attribution = {});
    
    // Export to CSV for external analysis
    bool export_trades_to_csv(const std::vector<TradeResult>& trades,
                             const std::string& file_path);
    bool export_portfolio_history_to_csv(const std::vector<PortfolioSnapshot>& history,
                                        const std::string& file_path);
    
    // Generate comparison report
    std::string generate_comparison_report(const std::vector<PerformanceMetrics>& metrics_list,
                                          const std::vector<std::string>& strategy_names);
    
    // Risk warning analysis
    struct RiskWarning {
        std::string warning_type;
        std::string severity; // "low", "medium", "high", "critical"
        std::string description;
        double value;
        std::string recommendation;
    };
    
    std::vector<RiskWarning> analyze_risk_warnings(const PerformanceMetrics& metrics);
    
private:
    // Report formatting helpers
    std::string format_percentage(double value, int decimal_places = 2);
    std::string format_currency(double value, const std::string& currency = "USD");
    std::string format_number(double value, int decimal_places = 2);
    std::string format_date(std::chrono::system_clock::time_point date);
    
    // Chart generation helpers (for HTML reports)
    std::string generate_equity_curve_chart(const std::vector<PortfolioSnapshot>& history);
    std::string generate_drawdown_chart(const std::vector<double>& drawdown_series);
    std::string generate_returns_histogram(const std::vector<double>& returns);
    std::string generate_rolling_metrics_chart(const RollingMetrics& rolling);
};

// Exception classes
class PerformanceCalculationException : public std::exception {
public:
    explicit PerformanceCalculationException(const std::string& message) : message_(message) {}
    const char* what() const noexcept override { return message_.c_str(); }
    
private:
    std::string message_;
};

class InsufficientDataException : public PerformanceCalculationException {
public:
    explicit InsufficientDataException(const std::string& message) 
        : PerformanceCalculationException("Insufficient Data: " + message) {}
};

} // namespace backtest
} // namespace ats