#pragma once

#include <string>
#include <vector>
#include <unordered_map>
#include <chrono>
#include <memory>
#include <shared_mutex>
#include <unordered_set>
#include <limits>

#include "../core/types.hpp"

namespace ats {

// Market data structures optimized for memory efficiency
struct Ticker {
    std::string symbol;
    double last_price;
    double volume_24h;
    double price_change_24h;
    double price_change_percent_24h;
    double high_24h;
    double low_24h;
    long long timestamp;
    
    Ticker() : last_price(0.0), volume_24h(0.0), price_change_24h(0.0),
               price_change_percent_24h(0.0), high_24h(0.0), low_24h(0.0), timestamp(0) {}
};

// Trade struct is defined in core/types.hpp

struct Candle {
    std::string symbol;
    long long open_time;
    long long close_time;
    double open;
    double high;
    double low;
    double close;
    double volume;
    
    Candle() : open_time(0), close_time(0), open(0.0), high(0.0), 
               low(0.0), close(0.0), volume(0.0) {}
};

// Market statistics for arbitrage analysis
struct MarketStats {
    std::string symbol;
    double volatility;          // Price volatility
    double average_spread;      // Average bid-ask spread
    double liquidity_score;     // Liquidity indicator
    double correlation;         // Price correlation with other markets
    long long last_update;
    
    MarketStats() : volatility(0.0), average_spread(0.0), liquidity_score(0.0),
                   correlation(0.0), last_update(0) {}
};

// Note: PriceComparison moved to types.hpp to avoid duplication

// Market depth analysis
struct MarketDepth {
    std::string symbol;
    std::string exchange;
    double total_bid_volume;
    double total_ask_volume;
    double weighted_bid_price;
    double weighted_ask_price;
    int bid_levels;
    int ask_levels;
    long long timestamp;
    
    MarketDepth() : total_bid_volume(0.0), total_ask_volume(0.0),
                   weighted_bid_price(0.0), weighted_ask_price(0.0),
                   bid_levels(0), ask_levels(0), timestamp(0) {}
    
    double GetImbalance() const {
        if (total_ask_volume == 0.0) return 1.0;
        if (total_bid_volume == 0.0) return -1.0;
        return (total_bid_volume - total_ask_volume) / (total_bid_volume + total_ask_volume);
    }
};

// Real-time market data feed
class MarketDataFeed {
private:
    // Current market data
    std::unordered_map<std::string, Price> latest_prices_;
    std::unordered_map<std::string, OrderBook> latest_orderbooks_;
    std::unordered_map<std::string, Ticker> latest_tickers_;
    std::unordered_map<std::string, MarketStats> market_stats_;
    
    // Trade history and statistics
    std::unordered_map<std::string, std::vector<Trade>> trade_histories_;
    
    struct TradeStatistics {
        double volume_1h = 0.0;
        double volume_24h = 0.0;
        double vwap = 0.0;
        double vwap_total_value = 0.0;
        double vwap_total_volume = 0.0;
        double last_price = 0.0;
        long long last_trade_time = 0;
        std::vector<std::pair<double, long long>> price_history; // price, timestamp
        
        TradeStatistics() = default;
    };
    std::unordered_map<std::string, TradeStatistics> trade_stats_;
    
    mutable std::shared_mutex data_mutex_;
    
    // Lock type aliases for convenience
    using unique_lock_type = std::unique_lock<std::shared_mutex>;
    using shared_lock_type = std::shared_lock<std::shared_mutex>;
    
    // Configuration
    size_t max_trade_history_;
    std::chrono::minutes stats_update_interval_;
    
public:
    MarketDataFeed();
    ~MarketDataFeed() = default;
    
    // Configuration
    void SetMaxTradeHistory(size_t max_history) { max_trade_history_ = max_history; }
    void SetStatsUpdateInterval(std::chrono::minutes interval) { stats_update_interval_ = interval; }
    
    // Data updates (thread-safe)
    void UpdatePrice(const std::string& exchange, const Price& price);
    void UpdateOrderBook(const std::string& exchange, const OrderBook& orderbook);
    void UpdateTicker(const std::string& exchange, const Ticker& ticker);
    void UpdateTrade(const std::string& exchange, const Trade& trade);
    
    // Data retrieval (thread-safe)
    bool GetLatestPrice(const std::string& exchange, const std::string& symbol, Price& price) const;
    bool GetLatestOrderBook(const std::string& exchange, const std::string& symbol, OrderBook& orderbook) const;
    bool GetLatestTicker(const std::string& exchange, const std::string& symbol, Ticker& ticker) const;
    
    // Cross-exchange analysis
    void ComparePrices(const std::string& symbol, 
                                 const std::vector<std::string>& exchanges,
                                 PriceComparison& comparison) const;
    
    std::vector<MarketDepth> GetMarketDepth(const std::string& symbol,
                                           const std::vector<std::string>& exchanges) const;
    
    // Statistics
    MarketStats GetMarketStats(const std::string& symbol) const;
    void UpdateMarketStats(const std::string& symbol);
    
    // Utility functions
    std::vector<std::string> GetAvailableSymbols() const;
    std::vector<std::string> GetActiveExchanges() const;
    bool IsDataStale(const std::string& exchange, const std::string& symbol, 
                    std::chrono::seconds max_age) const;
    
    // Memory management
    void CleanupOldData(std::chrono::minutes max_age);
    size_t GetMemoryUsage() const;
    
private:
    std::string MakeKey(const std::string& exchange, const std::string& symbol) const;
    void UpdateTradeStatistics(const std::string& key, const Trade& trade);
    void CalculateVolatility(const std::string& symbol, MarketStats& stats) const;
    void CalculateSpread(const std::string& symbol, MarketStats& stats) const;
    void CalculateLiquidity(const std::string& symbol, MarketStats& stats) const;
};

} // namespace ats 