#pragma once

#include <string>
#include <chrono>
#include <unordered_map>
#include <vector>
#include <memory>

namespace ats {
namespace types {

// Basic numeric types
using Price = double;
using Quantity = double;
using Volume = double;
using Amount = double;
using Timestamp = std::chrono::system_clock::time_point;
using Duration = std::chrono::milliseconds;

// Exchange and trading pair types
using ExchangeId = std::string;
using Symbol = std::string;
using TradingPair = std::string;
using Currency = std::string;
using OrderId = std::string;
using TradeId = std::string;

// Order types
enum class OrderType {
    MARKET,
    LIMIT,
    STOP,
    STOP_LIMIT
};

enum class OrderSide {
    BUY,
    SELL
};

enum class OrderStatus {
    PENDING,
    OPEN,
    PARTIALLY_FILLED,
    FILLED,
    CANCELED,
    REJECTED,
    EXPIRED
};

// Time in force
enum class TimeInForce {
    GTC,  // Good Till Canceled
    IOC,  // Immediate Or Cancel
    FOK   // Fill Or Kill
};

// Market data structure
struct Ticker {
    Symbol symbol;
    ExchangeId exchange;
    Price bid;
    Price ask;
    Price last;
    Volume volume_24h;
    Timestamp timestamp;
    
    Ticker() = default;
    Ticker(const Symbol& sym, const ExchangeId& ex, Price b, Price a, Price l, Volume v, Timestamp ts)
        : symbol(sym), exchange(ex), bid(b), ask(a), last(l), volume_24h(v), timestamp(ts) {}
};

// Order structure
struct Order {
    OrderId id;
    ExchangeId exchange;
    Symbol symbol;
    OrderType type;
    OrderSide side;
    Quantity quantity;
    Price price;
    OrderStatus status;
    TimeInForce time_in_force;
    Timestamp created_at;
    Timestamp updated_at;
    Quantity filled_quantity;
    Price avg_fill_price;
    
    Order() = default;
    Order(const OrderId& order_id, const ExchangeId& ex, const Symbol& sym, 
          OrderType t, OrderSide s, Quantity qty, Price p)
        : id(order_id), exchange(ex), symbol(sym), type(t), side(s), 
          quantity(qty), price(p), status(OrderStatus::PENDING),
          time_in_force(TimeInForce::GTC), filled_quantity(0.0), avg_fill_price(0.0) {
        created_at = std::chrono::system_clock::now();
        updated_at = created_at;
    }
};

// Trade structure
struct Trade {
    TradeId id;
    OrderId order_id;
    ExchangeId exchange;
    Symbol symbol;
    OrderSide side;
    Quantity quantity;
    Price price;
    Amount fee;
    Currency fee_currency;
    Timestamp timestamp;
    
    Trade() = default;
    Trade(const TradeId& trade_id, const OrderId& order_id, const ExchangeId& ex,
          const Symbol& sym, OrderSide s, Quantity qty, Price p, Amount f, const Currency& fc)
        : id(trade_id), order_id(order_id), exchange(ex), symbol(sym), side(s),
          quantity(qty), price(p), fee(f), fee_currency(fc) {
        timestamp = std::chrono::system_clock::now();
    }
};

// Balance structure
struct Balance {
    Currency currency;
    ExchangeId exchange;
    Amount total;
    Amount available;
    Amount locked;
    Timestamp updated_at;
    
    Balance() = default;
    Balance(const Currency& cur, const ExchangeId& ex, Amount t, Amount a, Amount l)
        : currency(cur), exchange(ex), total(t), available(a), locked(l) {
        updated_at = std::chrono::system_clock::now();
    }
};

// Position structure
struct Position {
    ExchangeId exchange;
    Symbol symbol;
    Quantity quantity;
    Price avg_price;
    Amount unrealized_pnl;
    Amount realized_pnl;
    Timestamp opened_at;
    Timestamp updated_at;
    
    Position() = default;
    Position(const ExchangeId& ex, const Symbol& sym, Quantity qty, Price avg_p)
        : exchange(ex), symbol(sym), quantity(qty), avg_price(avg_p),
          unrealized_pnl(0.0), realized_pnl(0.0) {
        opened_at = std::chrono::system_clock::now();
        updated_at = opened_at;
    }
};

// Portfolio structure
struct Portfolio {
    std::unordered_map<Currency, Balance> balances;
    std::vector<Position> positions;
    Amount total_value;
    Amount unrealized_pnl;
    Amount realized_pnl;
    Timestamp updated_at;
    
    Portfolio() : total_value(0.0), unrealized_pnl(0.0), realized_pnl(0.0) {
        updated_at = std::chrono::system_clock::now();
    }
};

// Arbitrage opportunity structure
struct ArbitrageOpportunity {
    Symbol symbol;
    ExchangeId buy_exchange;
    ExchangeId sell_exchange;
    Price buy_price;
    Price sell_price;
    Quantity max_quantity;
    double spread_percentage;
    Amount potential_profit;
    Timestamp detected_at;
    Duration validity_duration;
    
    ArbitrageOpportunity() = default;
    ArbitrageOpportunity(const Symbol& sym, const ExchangeId& buy_ex, const ExchangeId& sell_ex,
                        Price buy_p, Price sell_p, Quantity max_qty, double spread_pct, Amount profit)
        : symbol(sym), buy_exchange(buy_ex), sell_exchange(sell_ex), buy_price(buy_p),
          sell_price(sell_p), max_quantity(max_qty), spread_percentage(spread_pct),
          potential_profit(profit) {
        detected_at = std::chrono::system_clock::now();
        validity_duration = std::chrono::milliseconds(5000); // 5 seconds default
    }
};

// Risk metrics structure
struct RiskMetrics {
    Amount max_drawdown;
    Amount current_drawdown;
    double sharpe_ratio;
    double sortino_ratio;
    double var_95;  // Value at Risk 95%
    double var_99;  // Value at Risk 99%
    Amount daily_pnl;
    Amount weekly_pnl;
    Amount monthly_pnl;
    Timestamp calculated_at;
    
    RiskMetrics() : max_drawdown(0.0), current_drawdown(0.0), sharpe_ratio(0.0),
                   sortino_ratio(0.0), var_95(0.0), var_99(0.0), daily_pnl(0.0),
                   weekly_pnl(0.0), monthly_pnl(0.0) {
        calculated_at = std::chrono::system_clock::now();
    }
};

// Market data snapshot
struct MarketSnapshot {
    std::unordered_map<ExchangeId, std::unordered_map<Symbol, Ticker>> tickers;
    Timestamp snapshot_time;
    
    MarketSnapshot() {
        snapshot_time = std::chrono::system_clock::now();
    }
};

// Configuration types
struct ExchangeConfig {
    ExchangeId id;
    std::string name;
    std::string api_key;
    std::string secret_key;
    std::string passphrase;  // for some exchanges
    bool sandbox_mode;
    int rate_limit;
    int timeout_ms;
    std::vector<Symbol> supported_symbols;
    
    ExchangeConfig() : sandbox_mode(false), rate_limit(1000), timeout_ms(5000) {}
};

struct TradingConfig {
    bool enabled;
    double min_spread_threshold;
    Amount max_position_size;
    Amount max_daily_volume;
    int max_daily_trades;
    double emergency_stop_loss;
    double commission_rate;
    std::vector<Symbol> allowed_symbols;
    
    TradingConfig() : enabled(false), min_spread_threshold(0.005), max_position_size(1000.0),
                     max_daily_volume(10000.0), max_daily_trades(100), emergency_stop_loss(0.02),
                     commission_rate(0.001) {}
};

struct RiskConfig {
    double max_portfolio_risk;
    double max_single_trade_risk;
    double stop_loss_percentage;
    double take_profit_percentage;
    double max_drawdown;
    Amount max_daily_loss;
    Amount max_position_concentration;
    
    RiskConfig() : max_portfolio_risk(0.05), max_single_trade_risk(0.01),
                  stop_loss_percentage(0.02), take_profit_percentage(0.01),
                  max_drawdown(0.05), max_daily_loss(1000.0), max_position_concentration(0.3) {}
};

} // namespace types
} // namespace ats