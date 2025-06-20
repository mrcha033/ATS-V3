#pragma once

#include <memory>
#include <vector>
#include <thread>
#include <atomic>
#include <mutex>
#include <unordered_map>
#include <functional>

#include "../exchange/exchange_interface.hpp"
#include "../network/websocket_client.hpp"
#include "../data/price_cache.hpp"
#include "../data/market_data.hpp"

namespace ats {

class ConfigManager;

// Price update notification
struct PriceUpdate {
    std::string exchange;
    std::string symbol;
    Price price;
    std::chrono::steady_clock::time_point timestamp;
    
    PriceUpdate(const std::string& ex, const std::string& sym, const Price& p)
        : exchange(ex), symbol(sym), price(p), timestamp(std::chrono::steady_clock::now()) {}
};

// Price monitoring configuration
struct MonitorConfig {
    std::vector<std::string> symbols;
    std::chrono::milliseconds update_interval{100};
    std::chrono::milliseconds ws_timeout{30000};
    bool use_websocket{true};
    bool use_rest_fallback{true};
    int max_price_age_seconds{5};
    bool enable_caching{true};
};

class PriceMonitor {
public:
    using PriceUpdateCallback = std::function<void(const PriceUpdate&)>;
    using ErrorCallback = std::function<void(const std::string& exchange, const std::string& error)>;

private:
    ConfigManager* config_manager_;
    MonitorConfig config_;
    
    // Exchange connections
    std::vector<ExchangeInterface*> exchanges_;
    std::unordered_map<std::string, std::unique_ptr<WebSocketClient>> websocket_clients_;
    
    // Data management
    std::unique_ptr<PriceCache> price_cache_;
    std::unique_ptr<MarketDataFeed> market_data_feed_;
    
    // Threading
    std::thread monitor_thread_;
    std::thread websocket_thread_;
    std::atomic<bool> running_;
    mutable std::mutex exchanges_mutex_;
    
    // Callbacks
    PriceUpdateCallback price_callback_;
    ErrorCallback error_callback_;
    
    // Statistics
    std::atomic<long long> total_updates_;
    std::atomic<long long> websocket_updates_;
    std::atomic<long long> rest_updates_;
    std::atomic<long long> failed_updates_;
    std::chrono::steady_clock::time_point start_time_;
    
    // Monitoring state
    std::unordered_map<std::string, std::chrono::steady_clock::time_point> last_update_times_;
    std::mutex update_times_mutex_;
    
public:
    explicit PriceMonitor(ConfigManager* config_manager);
    ~PriceMonitor();
    
    // Lifecycle management
    bool Initialize();
    void Start();
    void Stop();
    bool IsRunning() const { return running_.load(); }
    
    // Configuration
    void SetConfig(const MonitorConfig& config) { config_ = config; }
    MonitorConfig GetConfig() const { return config_; }
    
    // Exchange management
    void AddExchange(ExchangeInterface* exchange);
    void RemoveExchange(const std::string& exchange_name);
    std::vector<std::string> GetActiveExchanges() const;
    
    // Symbol management
    void AddSymbol(const std::string& symbol);
    void RemoveSymbol(const std::string& symbol);
    std::vector<std::string> GetMonitoredSymbols() const;
    
    // Callbacks
    void SetPriceUpdateCallback(PriceUpdateCallback callback) { price_callback_ = callback; }
    void SetErrorCallback(ErrorCallback callback) { error_callback_ = callback; }
    
    // Data access
    bool GetLatestPrice(const std::string& exchange, const std::string& symbol, Price& price);
    bool GetLatestOrderBook(const std::string& exchange, const std::string& symbol, OrderBook& orderbook);
    PriceComparison ComparePrices(const std::string& symbol);
    
    // Health and status
    bool IsHealthy() const;
    std::string GetStatus() const;
    
    // Statistics
    long long GetTotalUpdates() const { return total_updates_.load(); }
    long long GetWebSocketUpdates() const { return websocket_updates_.load(); }
    long long GetRestUpdates() const { return rest_updates_.load(); }
    long long GetFailedUpdates() const { return failed_updates_.load(); }
    double GetUpdateRate() const; // updates per second
    double GetSuccessRate() const;
    
    void LogStatistics() const;
    void ResetStatistics();
    
private:
    // Main monitoring loops
    void MonitorLoop();
    void WebSocketLoop();
    
    // Data collection methods
    void CollectPricesViaRest();
    void CollectPricesViaWebSocket();
    void SetupWebSocketSubscriptions();
    
    // WebSocket event handlers
    void OnWebSocketMessage(const std::string& exchange, const std::string& message);
    void OnWebSocketStateChange(const std::string& exchange, WebSocketState state);
    void OnWebSocketError(const std::string& exchange, const std::string& error);
    
    // Data processing
    void ProcessPriceUpdate(const std::string& exchange, const Price& price);
    void ProcessOrderBookUpdate(const std::string& exchange, const OrderBook& orderbook);
    bool ParseWebSocketMessage(const std::string& exchange, const std::string& message, 
                              Price& price, OrderBook& orderbook);
    
    // Fallback and recovery
    void HandleWebSocketFailure(const std::string& exchange);
    void AttemptRestFallback(const std::string& exchange, const std::string& symbol);
    
    // Utility functions
    std::string GetWebSocketUrl(const std::string& exchange) const;
    std::string BuildSubscriptionMessage(const std::string& exchange, const std::vector<std::string>& symbols) const;
    bool IsDataStale(const std::string& exchange, const std::string& symbol) const;
    void UpdateLastUpdateTime(const std::string& key);
    
    // Exchange-specific message parsing
    bool ParseBinanceMessage(const std::string& message, Price& price, OrderBook& orderbook);
    bool ParseUpbitMessage(const std::string& message, Price& price, OrderBook& orderbook);
};

} // namespace ats 