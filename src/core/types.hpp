#pragma once

#include <string>
#include <vector>
#include <chrono>
#include <atomic>
#include <unordered_map>
#include <variant>

namespace ats {

// Forward declarations
class ConfigManager;
class ExchangeInterface;
class PriceMonitor;
class OpportunityDetector;
class TradeExecutor;
class RiskManager;
class PortfolioManager;

// Order enums
enum class OrderType {
    MARKET,
    LIMIT
};

enum class OrderSide {
    BUY,
    SELL
};

enum class OrderStatus {
    PENDING,
    NEW,
    PARTIAL,
    FILLED,
    CANCELLED,
    REJECTED
};

enum class TradeState {
    PENDING,         // Trade queued for execution
    BUYING,          // Executing buy order
    SELLING,         // Executing sell order
    COMPLETED,       // Trade completed successfully
    FAILED,          // Trade failed
    CANCELLED,       // Trade cancelled
    PARTIAL,         // Partially filled
    TIMEOUT          // Trade timed out
};

enum class ExchangeStatus {
    DISCONNECTED,
    CONNECTING,
    CONNECTED,
    ERROR
};

enum class WebSocketState {
    DISCONNECTED,
    CONNECTING,
    CONNECTED,
    RECONNECTING,
    ERROR
};

// Common data structures
struct Price {
    std::string symbol;
    double bid;
    double ask;
    double last;
    double volume;
    long long timestamp;
    
    Price() : bid(0.0), ask(0.0), last(0.0), volume(0.0), timestamp(0) {}
    Price(const std::string& sym, double b, double a, double l, double v, long long ts)
        : symbol(sym), bid(b), ask(a), last(l), volume(v), timestamp(ts) {}
        
    double GetSpread() const noexcept { return ask - bid; }
    double GetMidPrice() const noexcept { return (bid + ask) / 2.0; }
};

struct OrderBook {
    std::string symbol;
    std::vector<std::pair<double, double>> bids; // price, volume
    std::vector<std::pair<double, double>> asks; // price, volume
    long long timestamp;
    
    OrderBook() : timestamp(0) {}
    
    double GetBestBid() const noexcept { return bids.empty() ? 0.0 : bids[0].first; }
    double GetBestAsk() const noexcept { return asks.empty() ? 0.0 : asks[0].first; }
    double GetSpread() const noexcept { return GetBestAsk() - GetBestBid(); }
};

struct Balance {
    std::string asset;
    double free;
    double locked;
    
    Balance() : free(0.0), locked(0.0) {}
    Balance(const std::string& a, double f, double l) : asset(a), free(f), locked(l) {}
    
    double total() const noexcept { return free + locked; }
};

struct Order {
    std::string order_id;
    std::string id;              // Alias for order_id for compatibility
    std::string exchange;
    std::string symbol;
    OrderType type;              // MARKET, LIMIT
    OrderSide side;              // BUY, SELL
    double quantity;
    double price;                // For limit orders
    double filled_quantity;
    double avg_fill_price;
    OrderStatus status;
    std::string error_message;
    long long timestamp;
    long long filled_time;       // Time when order was filled
    
    Order() : type(OrderType::MARKET), side(OrderSide::BUY), quantity(0.0), price(0.0), 
             filled_quantity(0.0), avg_fill_price(0.0), status(OrderStatus::PENDING), 
             timestamp(0), filled_time(0) {
        // Keep id and order_id in sync
        id = order_id;
    }
};

// Arbitrage opportunity structure
struct ArbitrageOpportunity {
    std::string symbol;
    std::string buy_exchange;
    std::string sell_exchange;
    
    double buy_price;
    double sell_price;
    double profit_absolute;
    double profit_percent;
    
    // Order book bid/ask prices for spread analysis
    double buy_bid;             // Best bid price at buy exchange
    double buy_ask;             // Best ask price at buy exchange
    double sell_bid;            // Best bid price at sell exchange
    double sell_ask;            // Best ask price at sell exchange
    
    double max_volume;          // Maximum tradeable volume
    double estimated_fees;      // Total fees (trading + withdrawal)
    double net_profit_percent;  // Profit after fees
    
    long long timestamp;
    long long detection_latency_ms;
    
    // Market conditions
    double buy_liquidity;       // Available liquidity at buy exchange
    double sell_liquidity;      // Available liquidity at sell exchange
    double spread_stability;    // How stable the spread has been
    double execution_risk;      // Risk assessment score
    
    // Validation flags
    bool is_valid;
    bool has_sufficient_balance;
    bool meets_min_profit;
    bool within_risk_limits;
    
    ArbitrageOpportunity() 
        : profit_absolute(0.0), profit_percent(0.0), 
          buy_bid(0.0), buy_ask(0.0), sell_bid(0.0), sell_ask(0.0),
          max_volume(0.0), estimated_fees(0.0), net_profit_percent(0.0), 
          timestamp(0), detection_latency_ms(0), buy_liquidity(0.0), 
          sell_liquidity(0.0), spread_stability(0.0), execution_risk(0.0), 
          is_valid(false), has_sufficient_balance(false), meets_min_profit(false),
          within_risk_limits(false) {}
    
    bool IsExecutable() const noexcept {
        return is_valid && has_sufficient_balance && meets_min_profit && within_risk_limits;
    }
    
    double GetRiskAdjustedProfit() const noexcept {
        return net_profit_percent * (1.0 - execution_risk);
    }
};

// Trade execution result
struct ExecutionResult {
    std::string trade_id;
    TradeState final_state;
    
    // Financial results
    double realized_pnl;
    double gross_profit;
    double total_fees;
    double net_profit;
    double actual_profit_percent;
    
    // Execution metrics
    double total_execution_time_ms;
    double buy_execution_time_ms;
    double sell_execution_time_ms;
    double actual_slippage_percent;
    
    // Order details
    Order buy_order_result;
    Order sell_order_result;
    
    // Error information
    std::vector<std::string> errors;
    std::vector<std::string> warnings;
    
    ExecutionResult() : final_state(TradeState::PENDING), realized_pnl(0.0),
                       gross_profit(0.0), total_fees(0.0), net_profit(0.0),
                       actual_profit_percent(0.0), total_execution_time_ms(0.0),
                       buy_execution_time_ms(0.0), sell_execution_time_ms(0.0),
                       actual_slippage_percent(0.0) {}
    
    bool was_successful() const noexcept {
        return final_state == TradeState::COMPLETED && realized_pnl > 0;
    }
};

// Cross-exchange price comparison
struct PriceComparison {
    std::string symbol;
    std::unordered_map<std::string, Price> exchange_prices; // exchange_name -> Price
    std::string highest_bid_exchange;
    std::string lowest_ask_exchange;
    double max_spread_percent;
    long long timestamp;
    
    PriceComparison() : max_spread_percent(0.0), timestamp(0) {}
    
    bool HasArbitrageOpportunity(double min_profit_threshold) const noexcept {
        return max_spread_percent >= min_profit_threshold;
    }
};

// Forward declaration for recursive JsonValue
struct JsonValue;

// JSON Value type definition (must be at end to avoid circular dependencies)
struct JsonValue : public std::variant<
    std::nullptr_t,
    bool,
    int,
    double,
    std::string,
    std::vector<JsonValue>,
    std::unordered_map<std::string, JsonValue>
> {
    using variant::variant;
};

} // namespace ats 