#pragma once

#include <string>
#include <vector>
#include <memory>
#include <functional>
#include <chrono>
#include "../core/types.hpp"

namespace ats {

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