#include "exchange_interface.hpp"
#include "../utils/logger.hpp"
#include <chrono>
#include <memory>

namespace ats {

// This file contains stub implementations for testing
// Real exchange implementations would be in separate files (binance.cpp, upbit.cpp, etc.)

class StubExchange : public ExchangeInterface {
private:
    std::string name_;
    
public:
    explicit StubExchange(const std::string& name) : name_(name) {}
    
    bool Connect() override {
        LOG_INFO("Connecting to exchange: {}", name_);
        SetStatus(ExchangeStatus::CONNECTED);
        return true;
    }
    
    void Disconnect() override {
        LOG_INFO("Disconnecting from exchange: {}", name_);
        SetStatus(ExchangeStatus::DISCONNECTED);
    }
    
    ExchangeStatus GetStatus() const override {
        return status_;
    }
    
    std::string GetName() const override {
        return name_;
    }

    // Market data
    bool GetPrice(const std::string& symbol, Price& price) override {
        price.symbol = symbol;
        price.bid = 50000.0;  // Placeholder values
        price.ask = 50010.0;
        price.last = 50005.0;
        price.volume = 100.0;
        price.timestamp = std::chrono::duration_cast<std::chrono::milliseconds>(
            std::chrono::system_clock::now().time_since_epoch()).count();
        return true;
    }
    
    bool GetOrderBook(const std::string& symbol, OrderBook& orderbook) override {
        orderbook.symbol = symbol;
        orderbook.bids = {{50000.0, 1.5}, {49995.0, 2.0}};
        orderbook.asks = {{50010.0, 1.2}, {50015.0, 1.8}};
        orderbook.timestamp = std::chrono::duration_cast<std::chrono::milliseconds>(
            std::chrono::system_clock::now().time_since_epoch()).count();
        return true;
    }
    
    std::vector<std::string> GetSupportedSymbols() override {
        return {"BTC/USDT", "ETH/USDT", "BNB/USDT"};
    }
    
    // Account information
    std::vector<Balance> GetBalances() override {
        std::vector<Balance> balances;
        
        Balance btc;
        btc.asset = "BTC";
        btc.free = 0.1;
        btc.locked = 0.0;
        balances.push_back(btc);
        
        Balance usdt;
        usdt.asset = "USDT";
        usdt.free = 5000.0;
        usdt.locked = 0.0;
        balances.push_back(usdt);
        
        return balances;
    }
    
    double GetBalance(const std::string& asset) override {
        auto balances = GetBalances();
        for (const auto& balance : balances) {
            if (balance.asset == asset) {
                return balance.total();
            }
        }
        return 0.0;
    }
    
    // Trading
    std::string PlaceOrder(const std::string& symbol, const std::string& side, 
                          const std::string& type, double quantity, double price = 0.0) override {
        std::string order_id = "ORDER_" + std::to_string(rand());
        LOG_INFO("Placed order: {} {} {} {} @ {}", 
                order_id, symbol, side, quantity, price);
        return order_id;
    }
    
    bool CancelOrder(const std::string& order_id) override {
        LOG_INFO("Cancelled order: {}", order_id);
        return true;
    }
    
    Order GetOrder(const std::string& order_id) override {
        Order order;
        order.order_id = order_id;
        order.id = order_id;
        order.symbol = "BTC/USDT";
        order.side = OrderSide::BUY;
        order.type = OrderType::LIMIT;
        order.price = 50000.0;
        order.quantity = 0.1;
        order.filled_quantity = 0.1;
        order.status = OrderStatus::FILLED;
        order.timestamp = std::chrono::duration_cast<std::chrono::milliseconds>(
            std::chrono::system_clock::now().time_since_epoch()).count();
        return order;
    }
    
    std::vector<Order> GetOpenOrders(const std::string& symbol = "") override {
        return {}; // No open orders in stub
    }
    
    // WebSocket subscriptions
    bool SubscribeToPrice(const std::string& symbol, 
                         std::function<void(const Price&)> callback) override {
        LOG_INFO("Subscribed to price updates for {}", symbol);
        return true;
    }
    
    bool SubscribeToOrderBook(const std::string& symbol,
                             std::function<void(const OrderBook&)> callback) override {
        LOG_INFO("Subscribed to orderbook updates for {}", symbol);
        return true;
    }
    
    bool UnsubscribeFromPrice(const std::string& symbol) override {
        LOG_INFO("Unsubscribed from price updates for {}", symbol);
        return true;
    }
    
    bool UnsubscribeFromOrderBook(const std::string& symbol) override {
        LOG_INFO("Unsubscribed from orderbook updates for {}", symbol);
        return true;
    }
    
    // Exchange specific information
    double GetMakerFee() const override { return 0.001; }  // 0.1%
    double GetTakerFee() const override { return 0.001; }  // 0.1%
    int GetRateLimit() const override { return 1200; }     // requests per minute
    double GetMinOrderSize(const std::string& symbol) const override { return 0.001; }
    double GetMaxOrderSize(const std::string& symbol) const override { return 1000.0; }
    
    // Health check
    bool IsHealthy() const override {
        return status_ == ExchangeStatus::CONNECTED && last_error_.empty();
    }
    
    std::string GetLastError() const override {
        return last_error_;
    }
};

// Factory function to create stub exchanges for testing
std::unique_ptr<ExchangeInterface> CreateStubExchange(const std::string& name) {
    return std::make_unique<StubExchange>(name);
}

} // namespace ats 