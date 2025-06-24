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
    {
        unique_lock_type lock(data_mutex_);
        std::string key = MakeKey(exchange, trade.symbol);
        
        // Store trade in history
        auto& trade_history = trade_histories_[key];
        trade_history.push_back(trade);
        
        // Keep only recent trades (last 1000)
        if (trade_history.size() > max_trade_history_) {
            trade_history.erase(trade_history.begin());
        }
        
        // Update trade statistics
        UpdateTradeStatistics(key, trade);
    }
    
    LOG_DEBUG("Trade update for {}: {} {} @ {}", exchange, trade.symbol, trade.quantity, trade.price);
}

void MarketDataFeed::UpdateTradeStatistics(const std::string& key, const Trade& trade) {
    auto& stats = trade_stats_[key];
    
    auto now = std::chrono::system_clock::now();
    auto timestamp = std::chrono::duration_cast<std::chrono::milliseconds>(now.time_since_epoch()).count();
    
    // Update volume statistics
    stats.volume_1h += trade.quantity;
    stats.volume_24h += trade.quantity;
    stats.last_trade_time = timestamp;
    stats.last_price = trade.price;
    
    // Calculate VWAP (Volume Weighted Average Price)
    stats.vwap_total_value += trade.price * trade.quantity;
    stats.vwap_total_volume += trade.quantity;
    if (stats.vwap_total_volume > 0) {
        stats.vwap = stats.vwap_total_value / stats.vwap_total_volume;
    }
    
    // Update price statistics for volatility calculation
    stats.price_history.push_back({trade.price, timestamp});
    if (stats.price_history.size() > 200) { // Keep last 200 price points
        stats.price_history.erase(stats.price_history.begin());
    }
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
    
    // Implement statistical calculations
    CalculateVolatility(symbol, stats);
    CalculateSpread(symbol, stats);
    CalculateLiquidity(symbol, stats);
    
    market_stats_[symbol] = stats;
}

void MarketDataFeed::CalculateVolatility(const std::string& symbol, MarketStats& stats) const {
    // Calculate volatility based on price history from all exchanges
    std::vector<double> all_prices;
    auto now = std::chrono::system_clock::now();
    auto cutoff_time = std::chrono::duration_cast<std::chrono::milliseconds>(
        (now - std::chrono::hours(24)).time_since_epoch()).count();
    
    // Collect prices from all exchanges for this symbol
    for (const auto& pair : trade_stats_) {
        if (pair.first.find(":" + symbol) != std::string::npos) {
            const auto& trade_stat = pair.second;
            for (const auto& price_point : trade_stat.price_history) {
                if (price_point.second >= cutoff_time) {
                    all_prices.push_back(price_point.first);
                }
            }
        }
    }
    
    if (all_prices.size() < 2) {
        stats.volatility = 0.0;
        return;
    }
    
    // Calculate returns
    std::vector<double> returns;
    for (size_t i = 1; i < all_prices.size(); ++i) {
        if (all_prices[i-1] > 0) {
            double return_rate = (all_prices[i] - all_prices[i-1]) / all_prices[i-1];
            returns.push_back(return_rate);
        }
    }
    
    if (returns.empty()) {
        stats.volatility = 0.0;
        return;
    }
    
    // Calculate standard deviation of returns
    double mean_return = 0.0;
    for (double ret : returns) {
        mean_return += ret;
    }
    mean_return /= returns.size();
    
    double variance = 0.0;
    for (double ret : returns) {
        variance += (ret - mean_return) * (ret - mean_return);
    }
    variance /= returns.size();
    
    // Convert to annualized volatility percentage
    double daily_volatility = std::sqrt(variance);
    stats.volatility = daily_volatility * std::sqrt(365) * 100.0;
}

void MarketDataFeed::CalculateSpread(const std::string& symbol, MarketStats& stats) const {
    // Calculate average spread across all exchanges
    double total_spread = 0.0;
    int spread_count = 0;
    
    for (const auto& pair : latest_prices_) {
        if (pair.first.find(":" + symbol) != std::string::npos) {
            const Price& price = pair.second;
            if (price.ask > 0 && price.bid > 0) {
                double spread_percent = (price.ask - price.bid) / price.bid * 100.0;
                total_spread += spread_percent;
                spread_count++;
            }
        }
    }
    
    stats.average_spread = spread_count > 0 ? total_spread / spread_count : 0.0;
}

void MarketDataFeed::CalculateLiquidity(const std::string& symbol, MarketStats& stats) const {
    // Calculate liquidity score based on multiple factors
    double volume_score = 0.0;
    double depth_score = 0.0;
    double spread_score = 0.0;
    int exchange_count = 0;
    
    // Collect data from all exchanges
    for (const auto& pair : trade_stats_) {
        if (pair.first.find(":" + symbol) != std::string::npos) {
            const auto& trade_stat = pair.second;
            exchange_count++;
            
            // Volume component (normalized)
            volume_score += std::min(100.0, std::log10(trade_stat.volume_24h + 1) * 20.0);
        }
    }
    
    // Order book depth component
    for (const auto& pair : latest_orderbooks_) {
        if (pair.first.find(":" + symbol) != std::string::npos) {
            const OrderBook& orderbook = pair.second;
            
            double bid_depth = 0.0;
            double ask_depth = 0.0;
            
            // Sum top 5 levels
            for (size_t i = 0; i < std::min(size_t(5), orderbook.bids.size()); ++i) {
                bid_depth += orderbook.bids[i].second;
            }
            for (size_t i = 0; i < std::min(size_t(5), orderbook.asks.size()); ++i) {
                ask_depth += orderbook.asks[i].second;
            }
            
            double total_depth = bid_depth + ask_depth;
            depth_score += std::min(100.0, std::log10(total_depth + 1) * 25.0);
        }
    }
    
    // Spread component (tighter spread = better liquidity)
    if (stats.average_spread > 0) {
        spread_score = std::max(0.0, 100.0 - stats.average_spread * 50.0);
    }
    
    // Average across exchanges and weight components
    if (exchange_count > 0) {
        volume_score /= exchange_count;
        depth_score /= exchange_count;
        
        // Weighted combination: volume 40%, depth 40%, spread 20%
        stats.liquidity_score = (volume_score * 0.4) + (depth_score * 0.4) + (spread_score * 0.2);
    } else {
        stats.liquidity_score = 0.0;
    }
    
    stats.liquidity_score = std::max(0.0, std::min(100.0, stats.liquidity_score));
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

} // namespace ats 
