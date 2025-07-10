#pragma once

#include <string>
#include <vector>
#include <unordered_map>
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
    double executed_quantity;
    double cummulative_quote_quantity;
    double commission;
    std::string commission_asset;
    long long transaction_time;
    bool is_working;
    std::string client_order_id;
};

struct OrderResult {
    std::string order_id;
    std::string client_order_id;
    std::string symbol;
    OrderSide side;
    double executed_quantity;
    double cummulative_quote_quantity;
    OrderStatus status;
    double commission;
    std::string commission_asset;
    long long transaction_time;
    std::string exchange_name;
};

struct Trade {
    std::string id;
    std::string symbol;
    double price;
    double quantity;
    long long timestamp;
    std::string buy_exchange;
    std::string sell_exchange;
    double buy_price;
    double sell_price;
    double volume;
    double profit;
    std::string buy_order_id;
    std::string sell_order_id;
    double executed_buy_quantity;
    double executed_sell_quantity;
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
    double buy_price = 0.0;
    double sell_price = 0.0;
    double volume = 0.0;
    double profit = 0.0;
    bool is_executable = false;
    double net_profit_percent = 0.0;
    double max_volume = 0.0;
    double buy_liquidity = 0.0;
    double sell_liquidity = 0.0;
    double buy_ask = 0.0;
    double buy_bid = 0.0;
    double sell_ask = 0.0;
    double sell_bid = 0.0;
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
