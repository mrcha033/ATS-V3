#pragma once

#include "exchange_interface.hpp"
#include "../network/rest_client.hpp"
#include "../network/websocket_client.hpp"
#include "../utils/logger.hpp"
#include <memory>
#include <string>
#include <unordered_map>

namespace ats {

class BinanceExchange : public ExchangeInterface {
private:
    std::string api_key_;
    std::string secret_key_;
    std::string base_url_;
    std::string ws_url_;
    
    std::unique_ptr<RestClient> rest_client_;
    std::unique_ptr<WebSocketClient> ws_client_;
    
    // Exchange configuration
    double maker_fee_;
    double taker_fee_;
    int rate_limit_;
    
    // Symbol mapping (Binance format to standard format)
    std::unordered_map<std::string, std::string> symbol_map_;
    
    // WebSocket subscriptions
    std::unordered_map<std::string, std::function<void(const Price&)>> price_callbacks_;
    std::unordered_map<std::string, std::function<void(const OrderBook&)>> orderbook_callbacks_;
    
public:
    BinanceExchange(const std::string& api_key, const std::string& secret_key);
    virtual ~BinanceExchange();
    
    // Connection management
    bool Connect() override;
    void Disconnect() override;
    ExchangeStatus GetStatus() const override;
    std::string GetName() const override { return "binance"; }
    
    // Market data
    bool GetPrice(const std::string& symbol, Price& price) override;
    bool GetOrderBook(const std::string& symbol, OrderBook& orderbook) override;
    std::vector<std::string> GetSupportedSymbols() override;
    
    // Account information
    std::vector<Balance> GetBalances() override;
    double GetBalance(const std::string& asset) override;
    
    // Trading
    std::string PlaceOrder(const std::string& symbol, const std::string& side, 
                          const std::string& type, double quantity, double price = 0.0) override;
    bool CancelOrder(const std::string& order_id) override;
    Order GetOrder(const std::string& order_id) override;
    std::vector<Order> GetOpenOrders(const std::string& symbol = "") override;
    
    // WebSocket subscriptions
    bool SubscribeToPrice(const std::string& symbol, 
                         std::function<void(const Price&)> callback) override;
    bool SubscribeToOrderBook(const std::string& symbol,
                             std::function<void(const OrderBook&)> callback) override;
    bool UnsubscribeFromPrice(const std::string& symbol) override;
    bool UnsubscribeFromOrderBook(const std::string& symbol) override;
    
    // Exchange specific information
    double GetMakerFee() const override { return maker_fee_; }
    double GetTakerFee() const override { return taker_fee_; }
    int GetRateLimit() const override { return rate_limit_; }
    double GetMinOrderSize(const std::string& symbol) const override;
    double GetMaxOrderSize(const std::string& symbol) const override;
    
    // Health check
    bool IsHealthy() const override;
    std::string GetLastError() const override { return last_error_; }
    
private:
    // Helper methods
    std::string CreateSignature(const std::string& params) const;
    std::string GetTimestamp() const;
    std::string ConvertSymbol(const std::string& standard_symbol) const;
    std::string ConvertSymbolBack(const std::string& binance_symbol) const;
    
    // REST API methods
    std::string MakeAuthenticatedRequest(const std::string& endpoint, 
                                        const std::string& method = "GET",
                                        const std::map<std::string, std::string>& params = {});
    std::string MakePublicRequest(const std::string& endpoint,
                                 const std::map<std::string, std::string>& params = {});
    
    // WebSocket message handlers
    void OnWebSocketMessage(const std::string& message);
    void OnWebSocketStateChange(WebSocketState state);
    void OnWebSocketError(const std::string& error);
    
    // Data parsing methods
    Price ParsePrice(const std::string& json_data, const std::string& symbol);
    OrderBook ParseOrderBook(const std::string& json_data, const std::string& symbol);
    Order ParseOrder(const std::string& json_data);
    Balance ParseBalance(const std::string& json_data);
    
    // Initialize symbol mappings
    void InitializeSymbolMappings();
};

} // namespace ats 