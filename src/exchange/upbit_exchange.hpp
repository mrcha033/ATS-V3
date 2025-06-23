#pragma once

#include "exchange_interface.hpp"
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

class UpbitExchange : public ExchangeInterface {
public:
    UpbitExchange(const std::string& access_key = "", const std::string& secret_key = "");
    virtual ~UpbitExchange() = default;

    // ExchangeInterface implementation
    bool Connect() override;
    bool Disconnect() override;
    bool IsConnected() const override;
    
    bool PlaceOrder(const OrderRequest& request) override;
    bool CancelOrder(const std::string& order_id) override;
    bool GetOrderStatus(const std::string& order_id, OrderStatus& status) override;
    std::vector<OrderStatus> GetOpenOrders() override;
    std::vector<Trade> GetTradeHistory(const std::string& symbol, int limit) override;
    
    AccountInfo GetAccountInfo() override;
    MarketData GetMarketData(const std::string& symbol) override;
    OrderBook GetOrderBook(const std::string& symbol, int depth) override;
    
    bool SubscribeToMarketData(const std::string& symbol, 
                              std::function<void(const MarketData&)> callback) override;
    bool SubscribeToOrderBook(const std::string& symbol, 
                             std::function<void(const OrderBook&)> callback) override;
    bool SubscribeToTrades(const std::string& symbol, 
                          std::function<void(const Trade&)> callback) override;

    // Upbit specific methods
    std::vector<std::string> GetMarkets();
    bool GetCandles(const std::string& symbol, const std::string& interval, 
                   int count, std::vector<Candle>& candles);
    bool GetTicker(const std::string& symbol, Ticker& ticker);
    std::vector<Ticker> GetAllTickers();

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
                    const std::string& params, Json::Value& response);
    bool MakeAuthenticatedRequest(const std::string& endpoint, const std::string& method,
                                 const std::string& params, Json::Value& response);
    
    // Data parsing
    OrderStatus ParseOrderStatus(const Json::Value& order_data);
    Trade ParseTrade(const Json::Value& trade_data);
    MarketData ParseMarketData(const Json::Value& ticker_data);
    OrderBook ParseOrderBook(const Json::Value& orderbook_data);
    Candle ParseCandle(const Json::Value& candle_data);
    Ticker ParseTicker(const Json::Value& ticker_data);
    
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

// Helper structures for Upbit specific data
struct Candle {
    std::string market;
    std::string candle_date_time_utc;
    std::string candle_date_time_kst;
    double opening_price;
    double high_price;
    double low_price;
    double trade_price;
    uint64_t timestamp;
    double candle_acc_trade_price;
    double candle_acc_trade_volume;
    int unit;
};

struct Ticker {
    std::string market;
    std::string trade_date;
    std::string trade_time;
    std::string trade_date_kst;
    std::string trade_time_kst;
    uint64_t trade_timestamp;
    double opening_price;
    double high_price;
    double low_price;
    double trade_price;
    double prev_closing_price;
    std::string change;
    double change_price;
    double change_rate;
    double signed_change_price;
    double signed_change_rate;
    double trade_volume;
    double acc_trade_price;
    double acc_trade_price_24h;
    double acc_trade_volume;
    double acc_trade_volume_24h;
    double highest_52_week_price;
    std::string highest_52_week_date;
    double lowest_52_week_price;
    std::string lowest_52_week_date;
    uint64_t timestamp;
};

} // namespace ats 