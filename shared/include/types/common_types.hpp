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

// Connection status for exchanges
enum class ConnectionStatus {
    DISCONNECTED,
    CONNECTING,
    CONNECTED,
    RECONNECTING,
    ERROR
};

// Order book entry
struct OrderBookEntry {
    Price price;
    Quantity quantity;
    
    OrderBookEntry() = default;
    OrderBookEntry(Price p, Quantity q) : price(p), quantity(q) {}
};

// Order book structure
struct OrderBook {
    Symbol symbol;
    ExchangeId exchange;
    std::vector<OrderBookEntry> bids;  // Sorted by price descending
    std::vector<OrderBookEntry> asks;  // Sorted by price ascending
    Timestamp timestamp;
    
    OrderBook() = default;
    OrderBook(const Symbol& sym, const ExchangeId& ex)
        : symbol(sym), exchange(ex) {
        timestamp = std::chrono::system_clock::now();
    }
};

// Order result structure
struct OrderResult {
    bool success;
    std::string message;
    OrderId order_id;
    OrderStatus status;
    Quantity filled_quantity;
    Price avg_fill_price;
    
    OrderResult() : success(false), status(OrderStatus::REJECTED), filled_quantity(0.0), avg_fill_price(0.0) {}
    OrderResult(bool s, const std::string& msg, const OrderId& id)
        : success(s), message(msg), order_id(id), status(s ? OrderStatus::OPEN : OrderStatus::REJECTED),
          filled_quantity(0.0), avg_fill_price(0.0) {}
};

// Market data structure
struct Ticker {
    Symbol symbol;
    ExchangeId exchange;
    Price bid;
    Price ask;
    Price price;  // Current/last price
    Price last;   // Last trade price
    Volume volume;
    Volume volume_24h;
    int64_t timestamp;  // Unix timestamp in milliseconds
    
    Ticker() = default;
    Ticker(const Symbol& sym, const ExchangeId& ex, Price b, Price a, Price l, Volume v, int64_t ts)
        : symbol(sym), exchange(ex), bid(b), ask(a), price(l), last(l), volume(v), volume_24h(v), timestamp(ts) {}
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
    int64_t timestamp;  // Unix timestamp in milliseconds
    bool is_buyer_maker;
    
    Trade() : is_buyer_maker(false) {}
    Trade(const TradeId& trade_id, const OrderId& order_id, const ExchangeId& ex,
          const Symbol& sym, OrderSide s, Quantity qty, Price p, Amount f, const Currency& fc)
        : id(trade_id), order_id(order_id), exchange(ex), symbol(sym), side(s),
          quantity(qty), price(p), fee(f), fee_currency(fc), is_buyer_maker(false) {
        timestamp = std::chrono::duration_cast<std::chrono::milliseconds>(
            std::chrono::system_clock::now().time_since_epoch()).count();
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
    std::string side;
    Quantity quantity;
    Price avg_price;
    Price entry_price;
    Timestamp entry_time;
    Amount unrealized_pnl;
    Amount realized_pnl;
    Timestamp opened_at;
    Timestamp updated_at;
    Price stop_loss;
    Price take_profit;

    void update_unrealized_pnl(Price current_price) {
        if (side == "long") {
            unrealized_pnl = (current_price - avg_price) * quantity;
        } else if (side == "short") {
            unrealized_pnl = (avg_price - current_price) * quantity;
        }
        updated_at = std::chrono::system_clock::now();
    }

    Position() = default;
    Position(const ExchangeId& ex, const Symbol& sym, Quantity qty, Price avg_p)
        : exchange(ex), symbol(sym), quantity(qty), avg_price(avg_p),
          unrealized_pnl(0.0), realized_pnl(0.0) {
        opened_at = std::chrono::system_clock::now();
        updated_at = opened_at;
    }
    Position(const ExchangeId& ex, const Symbol& sym, const std::string& s, Quantity qty, Price price)
        : exchange(ex), symbol(sym), side(s), quantity(qty), avg_price(price), entry_price(price),
          unrealized_pnl(0.0), realized_pnl(0.0), stop_loss(0.0), take_profit(0.0) {
        opened_at = std::chrono::system_clock::now();
        updated_at = opened_at;
        entry_time = opened_at;
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

// Trade signal structure for backtesting
enum class SignalType {
    BUY,
    SELL,
    HOLD,
    CLOSE_LONG,
    CLOSE_SHORT
};

struct TradeSignal {
    Symbol symbol;
    ExchangeId exchange;
    SignalType type;
    Price price;
    Quantity quantity;
    double confidence;
    std::string reason;
    Timestamp timestamp;
    std::unordered_map<std::string, std::string> metadata;
    
    TradeSignal() : confidence(0.0) {
        timestamp = std::chrono::system_clock::now();
    }
    
    TradeSignal(const Symbol& sym, SignalType t, Price p, Quantity qty, double conf, const std::string& r)
        : symbol(sym), type(t), price(p), quantity(qty), confidence(conf), reason(r) {
        timestamp = std::chrono::system_clock::now();
    }

    TradeSignal(Timestamp ts, const Symbol& sym, const ExchangeId& ex, const std::string& s, Price p)
        : timestamp(ts), symbol(sym), exchange(ex), price(p), quantity(0.0), confidence(0.0) {
        if (s == "buy") {
            type = SignalType::BUY;
        } else if (s == "sell") {
            type = SignalType::SELL;
        }
    }
};

// Configuration types
struct ExchangeConfig {
    ExchangeId id;
    std::string name;
    std::string api_key;
    std::string secret_key;
    std::string passphrase;  // for some exchanges
    bool enabled;
    bool sandbox_mode;
    int rate_limit;
    int timeout_ms;
    std::vector<Symbol> supported_symbols;
    std::unordered_map<std::string, std::string> parameters;  // Additional configuration parameters
    
    ExchangeConfig() : enabled(true), sandbox_mode(false), rate_limit(1000), timeout_ms(5000) {}
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