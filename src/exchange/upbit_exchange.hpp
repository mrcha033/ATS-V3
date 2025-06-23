#pragma once

#include "exchange_interface.hpp"
#include "../core/types.hpp"
#include "../data/market_data.hpp"
#include "../network/rest_client.hpp"
#include "../network/websocket_client.hpp"
#include "../utils/json_parser.hpp"
#include "../utils/logger.hpp"
#include <unordered_map>
#include <string>
#include <memory>
#include <chrono>
#include <future>

namespace ats {

// JSON type alias for compatibility
using JsonValue = ats::json::JsonValue;

class UpbitExchange : public ExchangeInterface {
public:
    UpbitExchange(const std::string& access_key = "", const std::string& secret_key = "");
    virtual ~UpbitExchange() = default;

    // ExchangeInterface implementation
    bool Connect() override;
    void Disconnect() override;
    ExchangeStatus GetStatus() const override;
    std::string GetName() const override;
    
    // Market data
    bool GetPrice(const std::string& symbol, Price& price) override;
    bool GetOrderBook(const std::string& symbol, OrderBook& orderbook) override;
    OrderBook GetOrderBook(const std::string& symbol, int depth);
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
    double GetMakerFee() const override;
    double GetTakerFee() const override;
    int GetRateLimit() const override;
    double GetMinOrderSize(const std::string& symbol) const override;
    double GetMaxOrderSize(const std::string& symbol) const override;
    
    // Health check
    bool IsHealthy() const override;
    std::string GetLastError() const override;
    
    // Connection status
    bool IsConnected() const;

    // Upbit specific methods
    std::vector<std::string> GetMarkets();
    bool GetCandles(const std::string& symbol, const std::string& interval, 
                   int count, std::vector<Candle>& candles);
    bool GetTicker(const std::string& symbol, Ticker& ticker);
    std::vector<Ticker> GetAllTickers();
    
    // Additional methods from implementation
    bool GetOrderStatus(const std::string& order_id, OrderStatus& status);
    std::vector<Trade> GetTradeHistory(const std::string& symbol, int limit = 100);
    AccountInfo GetAccountInfo();
    MarketData GetMarketData(const std::string& symbol);
    bool SubscribeToMarketData(const std::string& symbol, 
                              std::function<void(const MarketData&)> callback);
    bool SubscribeToTrades(const std::string& symbol,
                          std::function<void(const Trade&)> callback);

private:
    std::string access_key_;
    std::string secret_key_;
    std::unique_ptr<RestClient> rest_client_;
    std::unique_ptr<WebSocketClient> ws_client_;
    std::atomic<bool> connected_;
    
    // Rate limiting
    std::chrono::time_point<std::chrono::steady_clock> last_request_time_;
    std::atomic<int> requests_this_second_;
    std::mutex rate_limit_mutex_;
    
    // Symbol mapping
    std::unordered_map<std::string, std::string> symbol_map_;
    std::unordered_map<std::string, std::string> reverse_symbol_map_;
    
    // Callbacks
    std::unordered_map<std::string, std::function<void(const MarketData&)>> market_data_callbacks_;
    std::unordered_map<std::string, std::function<void(const OrderBook&)>> orderbook_callbacks_;
    std::unordered_map<std::string, std::function<void(const Trade&)>> trade_callbacks_;
    std::mutex callback_mutex_;
    
    // Private methods
    bool InitializeConnection();
    void LoadSymbolMappings();
    std::string MapSymbol(const std::string& symbol);
    std::string UnmapSymbol(const std::string& upbit_symbol);
    
    // Authentication
    std::string GenerateJWT(const std::string& query_string = "");
    std::unordered_map<std::string, std::string> GetAuthHeaders(const std::string& query_string = "");
    
    // Rate limiting
    bool CheckRateLimit();
    void UpdateRateLimit();
    
    // API methods
    bool MakeRequest(const std::string& endpoint, const std::string& method,
                    const std::string& params, JsonValue& response);
    bool MakeAuthenticatedRequest(const std::string& endpoint, const std::string& method,
                                 const std::string& params, JsonValue& response);
    
    // Data parsing  
    Order ParseOrder(const JsonValue& order_data);
    Trade ParseTrade(const JsonValue& trade_data);
    MarketData ParseMarketData(const JsonValue& ticker_data);
    OrderBook ParseOrderBook(const JsonValue& orderbook_data);
    Candle ParseCandle(const JsonValue& candle_data);
    Ticker ParseTicker(const JsonValue& ticker_data);
    
    // WebSocket handlers
    void OnWebSocketMessage(const std::string& message);
    void OnWebSocketError(const std::string& error);
    void OnWebSocketClose();
    
    // Helper methods
    std::string GetServerTime();
    bool ValidateSymbol(const std::string& symbol);
    std::string FormatOrderSide(OrderSide side);
    std::string FormatOrderType(OrderType type);
    OrderSide ParseOrderSide(const std::string& side);
    OrderType ParseOrderType(const std::string& type);
    
    // Constants
    static const std::string BASE_URL;
    static const std::string WS_URL;
    static const int MAX_REQUESTS_PER_SECOND;
    static const int MAX_REQUESTS_PER_MINUTE;
};

} // namespace ats 