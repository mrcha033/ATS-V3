#pragma once

#include <string>
#include <vector>
#include <unordered_map>
#include <chrono>
#include <memory>
#include <shared_mutex>
#include <unordered_set>
#include <limits>

#include "../exchange/exchange_interface.hpp"

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

struct Trade {
    std::string symbol;
    std::string trade_id;
    double price;
    double quantity;
    long long timestamp;
    bool is_buyer_maker;
    
    Trade() : price(0.0), quantity(0.0), timestamp(0), is_buyer_maker(false) {}
};

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

// Cross-exchange price comparison
struct PriceComparison {
    std::string symbol;
    std::unordered_map<std::string, Price> exchange_prices; // exchange_name -> Price
    std::string highest_bid_exchange;
    std::string lowest_ask_exchange;
    double max_spread_percent;
    long long timestamp;
    
    PriceComparison() : max_spread_percent(0.0), timestamp(0) {}
    
    bool HasArbitrageOpportunity(double min_profit_threshold) const {
        return max_spread_percent >= min_profit_threshold;
    }
};

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
    
    mutable std::shared_mutex data_mutex_;
    
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
    PriceComparison ComparePrices(const std::string& symbol, 
                                 const std::vector<std::string>& exchanges) const;
    
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
    void CalculateVolatility(const std::string& symbol, MarketStats& stats) const;
    void CalculateSpread(const std::string& symbol, MarketStats& stats) const;
    void CalculateLiquidity(const std::string& symbol, MarketStats& stats) const;
};

} // namespace ats 