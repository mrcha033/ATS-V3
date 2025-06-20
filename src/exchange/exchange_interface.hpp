#pragma once

#include <string>
#include <vector>
#include <memory>
#include <functional>
#include <chrono>

namespace ats {

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

struct Price {
    std::string symbol;
    double bid;
    double ask;
    double last;
    double volume;    // Add volume field to match usage
    long long timestamp;
    
    Price() : bid(0.0), ask(0.0), last(0.0), volume(0.0), timestamp(0) {}
    Price(const std::string& sym, double b, double a, double l, double v, long long ts)
        : symbol(sym), bid(b), ask(a), last(l), volume(v), timestamp(ts) {}
        
    double GetSpread() const { return ask - bid; }
    double GetMidPrice() const { return (bid + ask) / 2.0; }
};

struct OrderBook {
    std::string symbol;
    std::vector<std::pair<double, double>> bids; // price, volume
    std::vector<std::pair<double, double>> asks; // price, volume
    long long timestamp;
    
    double GetBestBid() const { return bids.empty() ? 0.0 : bids[0].first; }
    double GetBestAsk() const { return asks.empty() ? 0.0 : asks[0].first; }
    double GetSpread() const { return GetBestAsk() - GetBestBid(); }
};

struct Balance {
    std::string asset;
    double free;
    double locked;
    double total() const { return free + locked; }
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
    
    Order() : quantity(0.0), price(0.0), filled_quantity(0.0), 
             avg_fill_price(0.0), status(OrderStatus::PENDING), 
             timestamp(0), filled_time(0) {
        // Keep id and order_id in sync
        id = order_id;
    }
};

enum class ExchangeStatus {
    DISCONNECTED,
    CONNECTING,
    CONNECTED,
    ERROR
};

class ExchangeInterface {
public:
    virtual ~ExchangeInterface() = default;
    
    // Connection management
    virtual bool Connect() = 0;
    virtual void Disconnect() = 0;
    virtual ExchangeStatus GetStatus() const = 0;
    virtual std::string GetName() const = 0;
    
    // Market data
    virtual bool GetPrice(const std::string& symbol, Price& price) = 0;
    virtual bool GetOrderBook(const std::string& symbol, OrderBook& orderbook) = 0;
    virtual std::vector<std::string> GetSupportedSymbols() = 0;
    
    // Account information
    virtual std::vector<Balance> GetBalances() = 0;
    virtual double GetBalance(const std::string& asset) = 0;
    
    // Trading
    virtual std::string PlaceOrder(const std::string& symbol, const std::string& side, 
                                 const std::string& type, double quantity, double price = 0.0) = 0;
    virtual bool CancelOrder(const std::string& order_id) = 0;
    virtual Order GetOrder(const std::string& order_id) = 0;
    virtual std::vector<Order> GetOpenOrders(const std::string& symbol = "") = 0;
    
    // WebSocket subscriptions
    virtual bool SubscribeToPrice(const std::string& symbol, 
                                std::function<void(const Price&)> callback) = 0;
    virtual bool SubscribeToOrderBook(const std::string& symbol,
                                    std::function<void(const OrderBook&)> callback) = 0;
    virtual bool UnsubscribeFromPrice(const std::string& symbol) = 0;
    virtual bool UnsubscribeFromOrderBook(const std::string& symbol) = 0;
    
    // Exchange specific information
    virtual double GetMakerFee() const = 0;
    virtual double GetTakerFee() const = 0;
    virtual int GetRateLimit() const = 0;
    virtual double GetMinOrderSize(const std::string& symbol) const = 0;
    virtual double GetMaxOrderSize(const std::string& symbol) const = 0;
    
    // Health check
    virtual bool IsHealthy() const = 0;
    virtual std::string GetLastError() const = 0;
    
protected:
    mutable std::string last_error_;
    ExchangeStatus status_ = ExchangeStatus::DISCONNECTED;
    
    void SetError(const std::string& error) const {
        last_error_ = error;
    }
    
    void SetStatus(ExchangeStatus status) {
        status_ = status;
    }
};

} // namespace ats 