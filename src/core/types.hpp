#pragma once

#include <string>
#include <vector>
#include <nlohmann/json.hpp>

namespace ats {

// Forward declarations
class ExchangeInterface;

enum class OrderType {
    LIMIT,
    MARKET
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

struct Price {
    std::string symbol;
    double bid;
    double ask;
    double last;
    double volume;
    long long timestamp;
};

struct OrderBook {
    std::string symbol;
    std::vector<std::pair<double, double>> bids; // price, volume
    std::vector<std::pair<double, double>> asks; // price, volume
    long long timestamp;
};

struct Balance {
    std::string asset;
    double free;
    double locked;
};

struct PriceInfo {
    std::string symbol;
    double price;
    long long timestamp;
};

struct Order {
    std::string id;
    std::string symbol;
    OrderType type;
    OrderSide side;
    double price;
    double quantity;
    OrderStatus status;
};

struct Trade {
    std::string id;
    std::string symbol;
    double price;
    double quantity;
    long long timestamp;
};

struct Opportunity {
    std::string symbol;
    double profit;
    // Other details like which exchanges to buy/sell on
};

struct SymbolInfo {
    std::string symbol;
    std::string base_asset;
    std::string quote_asset;
    int base_asset_precision;
    int quote_asset_precision;
    double min_price;
    double max_price;
    double tick_size;
    double min_quantity;
    double max_quantity;
    double step_size;
};

struct PriceComparison {
    std::string symbol;
    std::string highest_bid_exchange;
    std::string lowest_ask_exchange;
    double max_spread_percent;
    long long timestamp;
    std::unordered_map<std::string, Price> exchange_prices;
};

struct ArbitrageOpportunity {
    std::string symbol;
    std::string buy_exchange;
    std::string sell_exchange;
    double buy_price;
    double sell_price;
    double volume;
    double profit;
    bool is_executable;
    double net_profit_percent;
    double max_volume;
    double buy_liquidity;
    double sell_liquidity;
    double buy_ask;
    double buy_bid;
    double sell_ask;
    double sell_bid;
    bool is_valid;
    bool has_sufficient_balance;
    bool meets_min_profit;
    bool within_risk_limits;
};

struct Notification {
    enum class Level {
        INFO,
        WARNING,
        CRITICAL
    };
    Level level;
    std::string title;
    std::string message;
    long long timestamp;
};

} // namespace ats
