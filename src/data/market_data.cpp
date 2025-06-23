#include "market_data.hpp"
#include "../utils/logger.hpp"
#include <shared_mutex>

namespace ats {

// MarketDataFeed Implementation
MarketDataFeed::MarketDataFeed() 
    : max_trade_history_(1000), stats_update_interval_(std::chrono::minutes(5)) {
}

void MarketDataFeed::UpdatePrice(const std::string& exchange, const Price& price) {
    unique_lock_type lock(data_mutex_);
    std::string key = MakeKey(exchange, price.symbol);
    latest_prices_[key] = price;
}

void MarketDataFeed::UpdateOrderBook(const std::string& exchange, const OrderBook& orderbook) {
    unique_lock_type lock(data_mutex_);
    std::string key = MakeKey(exchange, orderbook.symbol);
    latest_orderbooks_[key] = orderbook;
}

void MarketDataFeed::UpdateTicker(const std::string& exchange, const Ticker& ticker) {
    unique_lock_type lock(data_mutex_);
    std::string key = MakeKey(exchange, ticker.symbol);
    latest_tickers_[key] = ticker;
}

void MarketDataFeed::UpdateTrade(const std::string& exchange, const Trade& trade) {
    // TODO: Implement trade history tracking
    LOG_DEBUG("Trade update for {}: {} {} @ {}", exchange, trade.symbol, trade.quantity, trade.price);
}

bool MarketDataFeed::GetLatestPrice(const std::string& exchange, const std::string& symbol, Price& price) const {
    shared_lock_type lock(data_mutex_);
    std::string key = MakeKey(exchange, symbol);
    auto it = latest_prices_.find(key);
    if (it != latest_prices_.end()) {
        price = it->second;
        return true;
    }
    return false;
}

bool MarketDataFeed::GetLatestOrderBook(const std::string& exchange, const std::string& symbol, OrderBook& orderbook) const {
    shared_lock_type lock(data_mutex_);
    std::string key = MakeKey(exchange, symbol);
    auto it = latest_orderbooks_.find(key);
    if (it != latest_orderbooks_.end()) {
        orderbook = it->second;
        return true;
    }
    return false;
}

bool MarketDataFeed::GetLatestTicker(const std::string& exchange, const std::string& symbol, Ticker& ticker) const {
    shared_lock_type lock(data_mutex_);
    std::string key = MakeKey(exchange, symbol);
    auto it = latest_tickers_.find(key);
    if (it != latest_tickers_.end()) {
        ticker = it->second;
        return true;
    }
    return false;
}

PriceComparison MarketDataFeed::ComparePrices(const std::string& symbol, 
                                             const std::vector<std::string>& exchanges) const {
    shared_lock_type lock(data_mutex_);
    
    PriceComparison comparison;
    comparison.symbol = symbol;
    comparison.timestamp = std::chrono::duration_cast<std::chrono::milliseconds>(
        std::chrono::system_clock::now().time_since_epoch()).count();
    
    double highest_bid = 0.0;
    double lowest_ask = std::numeric_limits<double>::max();
    
    for (const auto& exchange : exchanges) {
        std::string key = MakeKey(exchange, symbol);
        auto it = latest_prices_.find(key);
        if (it != latest_prices_.end()) {
            const Price& price = it->second;
            comparison.exchange_prices[exchange] = price;
            
            if (price.bid > highest_bid) {
                highest_bid = price.bid;
                comparison.highest_bid_exchange = exchange;
            }
            
            if (price.ask < lowest_ask) {
                lowest_ask = price.ask;
                comparison.lowest_ask_exchange = exchange;
            }
        }
    }
    
    if (highest_bid > 0.0 && lowest_ask < std::numeric_limits<double>::max()) {
        comparison.max_spread_percent = ((highest_bid - lowest_ask) / lowest_ask) * 100.0;
    }
    
    return comparison;
}

std::vector<MarketDepth> MarketDataFeed::GetMarketDepth(const std::string& symbol,
                                                       const std::vector<std::string>& exchanges) const {
    std::vector<MarketDepth> depths;
    shared_lock_type lock(data_mutex_);
    
    for (const auto& exchange : exchanges) {
        std::string key = MakeKey(exchange, symbol);
        auto it = latest_orderbooks_.find(key);
        if (it != latest_orderbooks_.end()) {
            const OrderBook& orderbook = it->second;
            
            MarketDepth depth;
            depth.symbol = symbol;
            depth.exchange = exchange;
            depth.timestamp = orderbook.timestamp;
            
            // Calculate totals and weighted prices
            for (const auto& bid : orderbook.bids) {
                depth.total_bid_volume += bid.second;
                depth.weighted_bid_price += bid.first * bid.second;
                depth.bid_levels++;
            }
            
            for (const auto& ask : orderbook.asks) {
                depth.total_ask_volume += ask.second;
                depth.weighted_ask_price += ask.first * ask.second;
                depth.ask_levels++;
            }
            
            if (depth.total_bid_volume > 0) {
                depth.weighted_bid_price /= depth.total_bid_volume;
            }
            
            if (depth.total_ask_volume > 0) {
                depth.weighted_ask_price /= depth.total_ask_volume;
            }
            
            depths.push_back(depth);
        }
    }
    
    return depths;
}

MarketStats MarketDataFeed::GetMarketStats(const std::string& symbol) const {
    shared_lock_type lock(data_mutex_);
    auto it = market_stats_.find(symbol);
    return (it != market_stats_.end()) ? it->second : MarketStats();
}

void MarketDataFeed::UpdateMarketStats(const std::string& symbol) {
    unique_lock_type lock(data_mutex_);
    
    MarketStats stats;
    stats.symbol = symbol;
    stats.last_update = std::chrono::duration_cast<std::chrono::milliseconds>(
        std::chrono::system_clock::now().time_since_epoch()).count();
    
    // TODO: Implement statistical calculations
    CalculateVolatility(symbol, stats);
    CalculateSpread(symbol, stats);
    CalculateLiquidity(symbol, stats);
    
    market_stats_[symbol] = stats;
}

std::vector<std::string> MarketDataFeed::GetAvailableSymbols() const {
    shared_lock_type lock(data_mutex_);
    std::unordered_set<std::string> symbols;
    
    for (const auto& pair : latest_prices_) {
        size_t pos = pair.first.find(':');
        if (pos != std::string::npos) {
            symbols.insert(pair.first.substr(pos + 1));
        }
    }
    
    return std::vector<std::string>(symbols.begin(), symbols.end());
}

std::vector<std::string> MarketDataFeed::GetActiveExchanges() const {
    shared_lock_type lock(data_mutex_);
    std::unordered_set<std::string> exchanges;
    
    for (const auto& pair : latest_prices_) {
        size_t pos = pair.first.find(':');
        if (pos != std::string::npos) {
            exchanges.insert(pair.first.substr(0, pos));
        }
    }
    
    return std::vector<std::string>(exchanges.begin(), exchanges.end());
}

bool MarketDataFeed::IsDataStale(const std::string& exchange, const std::string& symbol, 
                                std::chrono::seconds max_age) const {
    shared_lock_type lock(data_mutex_);
    std::string key = MakeKey(exchange, symbol);
    auto it = latest_prices_.find(key);
    
    if (it == latest_prices_.end()) {
        return true; // No data is considered stale
    }
    
    auto now = std::chrono::duration_cast<std::chrono::milliseconds>(
        std::chrono::system_clock::now().time_since_epoch()).count();
    auto age = std::chrono::milliseconds(now - it->second.timestamp);
    
    return age > max_age;
}

void MarketDataFeed::CleanupOldData(std::chrono::minutes max_age) {
    unique_lock_type lock(data_mutex_);
    
    auto now = std::chrono::duration_cast<std::chrono::milliseconds>(
        std::chrono::system_clock::now().time_since_epoch()).count();
    auto cutoff = now - std::chrono::duration_cast<std::chrono::milliseconds>(max_age).count();
    
    // Clean old prices
    for (auto it = latest_prices_.begin(); it != latest_prices_.end();) {
        if (it->second.timestamp < cutoff) {
            it = latest_prices_.erase(it);
        } else {
            ++it;
        }
    }
    
    // Clean old orderbooks
    for (auto it = latest_orderbooks_.begin(); it != latest_orderbooks_.end();) {
        if (it->second.timestamp < cutoff) {
            it = latest_orderbooks_.erase(it);
        } else {
            ++it;
        }
    }
}

size_t MarketDataFeed::GetMemoryUsage() const {
    shared_lock_type lock(data_mutex_);
    
    size_t usage = 0;
    usage += latest_prices_.size() * (sizeof(Price) + 50); // Approximate key size
    usage += latest_orderbooks_.size() * (sizeof(OrderBook) + 50);
    usage += latest_tickers_.size() * (sizeof(Ticker) + 50);
    usage += market_stats_.size() * (sizeof(MarketStats) + 50);
    
    return usage;
}

std::string MarketDataFeed::MakeKey(const std::string& exchange, const std::string& symbol) const {
    return exchange + ":" + symbol;
}

void MarketDataFeed::CalculateVolatility(const std::string& symbol, MarketStats& stats) const {
    // TODO: Implement volatility calculation based on price history
    stats.volatility = 0.0;
}

void MarketDataFeed::CalculateSpread(const std::string& symbol, MarketStats& stats) const {
    // TODO: Implement average spread calculation
    stats.average_spread = 0.0;
}

void MarketDataFeed::CalculateLiquidity(const std::string& symbol, MarketStats& stats) const {
    // TODO: Implement liquidity score calculation
    stats.liquidity_score = 0.0;
}

} // namespace ats 
