#pragma once

#include "types/common_types.hpp"
#include "trading_engine_service.hpp"
#include <memory>
#include <unordered_map>
#include <vector>
#include <chrono>
#include <functional>

namespace ats {
namespace trading_engine {

// Market depth data for spread calculation
struct MarketDepth {
    std::string symbol;
    std::string exchange;
    std::vector<std::pair<double, double>> bids;  // price, quantity
    std::vector<std::pair<double, double>> asks;  // price, quantity
    std::chrono::system_clock::time_point timestamp;
    
    MarketDepth() : timestamp(std::chrono::system_clock::now()) {}
};

// Spread analysis result
struct SpreadAnalysis {
    std::string symbol;
    std::string buy_exchange;
    std::string sell_exchange;
    double raw_spread;
    double effective_spread;
    double spread_percentage;
    double breakeven_spread;
    double profit_margin;
    double confidence_score;
    bool is_profitable;
    std::string analysis_notes;
    
    SpreadAnalysis() 
        : raw_spread(0), effective_spread(0), spread_percentage(0)
        , breakeven_spread(0), profit_margin(0), confidence_score(0)
        , is_profitable(false) {}
};

// Fee structure for exchanges
struct ExchangeFeeStructure {
    std::string exchange_id;
    double maker_fee;
    double taker_fee;
    double withdrawal_fee;
    std::unordered_map<std::string, double> symbol_specific_fees;
    bool has_volume_tiers;
    std::vector<std::pair<double, double>> volume_tiers; // volume, fee_rate
    
    ExchangeFeeStructure() 
        : maker_fee(0.001), taker_fee(0.001), withdrawal_fee(0.0001)
        , has_volume_tiers(false) {}
};

// Slippage model parameters
struct SlippageModel {
    std::string exchange_id;
    std::string symbol;
    double base_slippage;
    double liquidity_factor;
    double volatility_factor;
    double market_impact_coefficient;
    std::chrono::system_clock::time_point last_updated;
    
    SlippageModel()
        : base_slippage(0.0005), liquidity_factor(1.0)
        , volatility_factor(1.0), market_impact_coefficient(0.1)
        , last_updated(std::chrono::system_clock::now()) {}
};

// Market microstructure analysis
struct MarketMicrostructure {
    std::string symbol;
    std::string exchange;
    double bid_ask_spread;
    double order_book_depth;
    double price_volatility;
    double trading_volume_1h;
    double trading_volume_24h;
    double market_impact_coefficient;
    double liquidity_score;
    std::chrono::system_clock::time_point analysis_time;
    
    MarketMicrostructure()
        : bid_ask_spread(0), order_book_depth(0), price_volatility(0)
        , trading_volume_1h(0), trading_volume_24h(0)
        , market_impact_coefficient(0), liquidity_score(0)
        , analysis_time(std::chrono::system_clock::now()) {}
};

// Main spread calculator class
class SpreadCalculator {
public:
    SpreadCalculator();
    ~SpreadCalculator();
    
    // Initialization and configuration
    bool initialize(const config::ConfigManager& config);
    void update_fee_structures(const std::unordered_map<std::string, ExchangeFeeStructure>& fees);
    void update_slippage_models(const std::unordered_map<std::string, SlippageModel>& models);
    
    // Market data updates
    void update_market_depth(const MarketDepth& depth);
    void update_ticker(const types::Ticker& ticker);
    void update_trade_volume(const std::string& exchange, const std::string& symbol, double volume);
    
    // Spread analysis
    SpreadAnalysis analyze_spread(const std::string& symbol,
                                 const std::string& buy_exchange,
                                 const std::string& sell_exchange,
                                 double quantity) const;
    
    std::vector<SpreadAnalysis> find_best_opportunities(const std::string& symbol,
                                                       double min_spread_threshold = 0.005) const;
    
    std::vector<ArbitrageOpportunity> detect_arbitrage_opportunities(
        double min_profit_threshold = 100.0) const;
    
    // Fee calculations
    double calculate_trading_fee(const std::string& exchange, const std::string& symbol,
                               double quantity, double price, bool is_maker = false) const;
    
    double calculate_total_fees(const std::string& buy_exchange, const std::string& sell_exchange,
                              const std::string& symbol, double quantity, 
                              double buy_price, double sell_price) const;
    
    // Slippage estimation
    double estimate_slippage(const std::string& exchange, const std::string& symbol,
                           double quantity, types::OrderSide side) const;
    
    double estimate_market_impact(const std::string& exchange, const std::string& symbol,
                                double quantity) const;
    
    // Breakeven analysis
    double calculate_breakeven_spread(const std::string& buy_exchange, 
                                    const std::string& sell_exchange,
                                    const std::string& symbol, double quantity) const;
    
    double calculate_minimum_profitable_quantity(const std::string& symbol,
                                               const std::string& buy_exchange,
                                               const std::string& sell_exchange,
                                               double spread) const;
    
    // Market microstructure analysis
    MarketMicrostructure analyze_market_microstructure(const std::string& exchange,
                                                      const std::string& symbol) const;
    
    double calculate_liquidity_score(const std::string& exchange, const std::string& symbol) const;
    double calculate_volatility(const std::string& symbol, std::chrono::hours lookback) const;
    
    // Risk-adjusted calculations
    double calculate_risk_adjusted_profit(const ArbitrageOpportunity& opportunity) const;
    double calculate_confidence_score(const ArbitrageOpportunity& opportunity) const;
    double calculate_execution_probability(const ArbitrageOpportunity& opportunity) const;
    
    // Historical analysis
    std::vector<SpreadAnalysis> get_historical_spreads(const std::string& symbol,
                                                      std::chrono::hours lookback) const;
    
    double get_average_spread(const std::string& symbol, const std::string& buy_exchange,
                            const std::string& sell_exchange, std::chrono::hours lookback) const;
    
    // Statistics and monitoring
    size_t get_opportunities_detected() const;
    size_t get_opportunities_executed() const;
    double get_average_profit_margin() const;
    std::unordered_map<std::string, double> get_exchange_spreads() const;
    
    // Configuration and tuning
    void set_spread_threshold(double threshold);
    void set_slippage_tolerance(double tolerance);
    void enable_dynamic_fee_calculation(bool enable);
    void enable_advanced_slippage_modeling(bool enable);
    
    // Data management
    void clear_historical_data();
    void cleanup_old_data(std::chrono::hours max_age);
    size_t get_data_size() const;
    
private:
    struct Implementation;
    std::unique_ptr<Implementation> impl_;
    
    // Internal calculation methods
    double calculate_raw_spread(const types::Ticker& buy_ticker, const types::Ticker& sell_ticker) const;
    double calculate_effective_spread(const std::string& symbol, const std::string& buy_exchange,
                                    const std::string& sell_exchange, double quantity) const;
    
    double calculate_order_book_impact(const MarketDepth& depth, double quantity, 
                                     types::OrderSide side) const;
    
    double calculate_time_decay_factor(std::chrono::system_clock::time_point timestamp) const;
    
    // Fee calculation helpers
    double get_volume_based_fee(const std::string& exchange, double trading_volume) const;
    double get_symbol_specific_fee(const std::string& exchange, const std::string& symbol) const;
    
    // Slippage modeling
    double calculate_linear_slippage(const SlippageModel& model, double quantity) const;
    double calculate_nonlinear_slippage(const SlippageModel& model, double quantity,
                                      const MarketDepth& depth) const;
    
    void update_slippage_model(const std::string& exchange, const std::string& symbol,
                             double observed_slippage, double quantity);
    
    // Market data validation
    bool is_valid_market_depth(const MarketDepth& depth) const;
    bool is_valid_ticker(const types::Ticker& ticker) const;
    bool is_data_fresh(std::chrono::system_clock::time_point timestamp, 
                      std::chrono::milliseconds max_age) const;
    
    // Opportunity validation
    bool validate_opportunity(const ArbitrageOpportunity& opportunity) const;
    void enrich_opportunity(ArbitrageOpportunity& opportunity) const;
    
    // Statistical calculations
    double calculate_rolling_average(const std::vector<double>& values, size_t window_size) const;
    double calculate_standard_deviation(const std::vector<double>& values) const;
    double calculate_percentile(const std::vector<double>& values, double percentile) const;
};

// Fee calculator utility class
class FeeCalculator {
public:
    FeeCalculator();
    ~FeeCalculator();
    
    // Fee structure management
    void add_exchange_fees(const std::string& exchange_id, const ExchangeFeeStructure& fees);
    void update_trading_volume(const std::string& exchange_id, double volume);
    
    // Fee calculations
    double calculate_trading_fee(const std::string& exchange_id, const std::string& symbol,
                               double quantity, double price, bool is_maker = false) const;
    
    double calculate_withdrawal_fee(const std::string& exchange_id, const std::string& currency,
                                  double amount) const;
    
    // Volume tier calculations
    double get_current_fee_rate(const std::string& exchange_id, double current_volume) const;
    double get_fee_savings(const std::string& exchange_id, double additional_volume) const;
    
    // Fee optimization
    struct FeeOptimization {
        std::string recommended_exchange;
        double estimated_fee;
        double fee_savings;
        std::string reasoning;
    };
    
    FeeOptimization optimize_exchange_selection(const std::string& symbol, double quantity,
                                              double price, const std::vector<std::string>& exchanges) const;
    
private:
    std::unordered_map<std::string, ExchangeFeeStructure> fee_structures_;
    std::unordered_map<std::string, double> trading_volumes_;
    mutable std::shared_mutex mutex_;
};

// Slippage estimator utility class
class SlippageEstimator {
public:
    SlippageEstimator();
    ~SlippageEstimator();
    
    // Model management
    void add_slippage_model(const std::string& exchange_id, const std::string& symbol,
                           const SlippageModel& model);
    
    void update_model_from_execution(const std::string& exchange_id, const std::string& symbol,
                                   double quantity, double expected_price, double actual_price);
    
    // Slippage estimation
    double estimate_slippage(const std::string& exchange_id, const std::string& symbol,
                           double quantity, types::OrderSide side) const;
    
    double estimate_worst_case_slippage(const std::string& exchange_id, const std::string& symbol,
                                      double quantity, double confidence_level = 0.95) const;
    
    // Market impact modeling
    double estimate_market_impact(const std::string& exchange_id, const std::string& symbol,
                                double quantity, const MarketDepth& depth) const;
    
    double calculate_permanent_impact(const std::string& exchange_id, const std::string& symbol,
                                    double quantity) const;
    
    double calculate_temporary_impact(const std::string& exchange_id, const std::string& symbol,
                                    double quantity) const;
    
    // Model validation and tuning
    void validate_models();
    void retrain_models();
    double get_model_accuracy(const std::string& exchange_id, const std::string& symbol) const;
    
private:
    std::unordered_map<std::string, std::unordered_map<std::string, SlippageModel>> models_;
    std::unordered_map<std::string, std::vector<std::pair<double, double>>> execution_history_;
    mutable std::shared_mutex mutex_;
    
    void update_model_parameters(SlippageModel& model, double observed_slippage, double quantity);
    double calculate_model_error(const SlippageModel& model, 
                               const std::vector<std::pair<double, double>>& history) const;
};

// Utility functions for spread calculations
namespace spread_utils {
    
    // Basic spread calculations
    double calculate_percentage_spread(double buy_price, double sell_price);
    double calculate_absolute_spread(double buy_price, double sell_price);
    double calculate_mid_price(double bid, double ask);
    
    // Order book analysis
    double calculate_weighted_average_price(const std::vector<std::pair<double, double>>& levels,
                                          double quantity);
    double calculate_order_book_depth(const std::vector<std::pair<double, double>>& levels,
                                    double price_range_percentage = 0.01);
    
    // Statistical functions
    double calculate_z_score(double value, double mean, double std_dev);
    double calculate_correlation(const std::vector<double>& x, const std::vector<double>& y);
    double calculate_beta(const std::vector<double>& asset_returns, 
                        const std::vector<double>& market_returns);
    
    // Time-based calculations
    double calculate_time_weighted_average(const std::vector<std::pair<double, std::chrono::system_clock::time_point>>& data,
                                         std::chrono::hours window);
    
    std::chrono::milliseconds calculate_opportunity_staleness(std::chrono::system_clock::time_point detected_at);
    
    // Risk calculations
    double calculate_var(const std::vector<double>& returns, double confidence_level = 0.95);
    double calculate_expected_shortfall(const std::vector<double>& returns, double confidence_level = 0.95);
    double calculate_maximum_drawdown(const std::vector<double>& cumulative_returns);
    
    // Validation utilities
    bool is_reasonable_spread(double spread_percentage);
    bool is_reasonable_price(double price);
    bool is_reasonable_quantity(double quantity);
    bool is_market_hours(const std::string& exchange);
    
    // Data formatting
    std::string format_spread_percentage(double spread);
    std::string format_profit_amount(double profit, const std::string& currency = "USD");
    std::string format_analysis_summary(const SpreadAnalysis& analysis);
}

} // namespace trading_engine
} // namespace ats