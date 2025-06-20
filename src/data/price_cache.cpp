#include "price_cache.hpp"
#include "../utils/logger.hpp"
#include <thread>
#include <set>

namespace ats {

// PriceCache Implementation
PriceCache::PriceCache(size_t max_prices, size_t max_orderbooks)
    : price_cache_(max_prices), orderbook_cache_(max_orderbooks),
      price_ttl_(std::chrono::seconds(30)), orderbook_ttl_(std::chrono::seconds(10)),
      running_(true) {
    
    // Start cleanup thread
    cleanup_thread_ = std::thread(&PriceCache::CleanupLoop, this);
    LOG_INFO("PriceCache initialized with {} price slots, {} orderbook slots", max_prices, max_orderbooks);
}

PriceCache::~PriceCache() {
    running_ = false;
    cleanup_cv_.notify_all(); // Wake up cleanup thread immediately
    if (cleanup_thread_.joinable()) {
        cleanup_thread_.join();
    }
}

bool PriceCache::GetPrice(const std::string& exchange, const std::string& symbol, Price& price) {
    std::string key = MakeKey(exchange, symbol);
    return price_cache_.Get(key, price);
}

void PriceCache::SetPrice(const std::string& exchange, const std::string& symbol, const Price& price) {
    std::string key = MakeKey(exchange, symbol);
    price_cache_.Put(key, price);
}

bool PriceCache::IsPriceStale(const std::string& exchange, const std::string& symbol, 
                             std::chrono::seconds max_age) const {
    std::string key = MakeKey(exchange, symbol);
    Price price;
    if (!price_cache_.GetConst(key, price)) {
        return true; // No price data is considered stale
    }
    
    auto now = std::chrono::duration_cast<std::chrono::milliseconds>(
        std::chrono::system_clock::now().time_since_epoch()).count();
    auto age = std::chrono::milliseconds(now - price.timestamp);
    
    return age > max_age;
}

bool PriceCache::GetOrderBook(const std::string& exchange, const std::string& symbol, OrderBook& orderbook) {
    std::string key = MakeKey(exchange, symbol);
    return orderbook_cache_.Get(key, orderbook);
}

void PriceCache::SetOrderBook(const std::string& exchange, const std::string& symbol, const OrderBook& orderbook) {
    std::string key = MakeKey(exchange, symbol);
    orderbook_cache_.Put(key, orderbook);
}

bool PriceCache::IsOrderBookStale(const std::string& exchange, const std::string& symbol,
                                 std::chrono::seconds max_age) const {
    std::string key = MakeKey(exchange, symbol);
    OrderBook orderbook;
    if (!orderbook_cache_.GetConst(key, orderbook)) {
        return true; // No orderbook data is considered stale
    }
    
    auto now = std::chrono::duration_cast<std::chrono::milliseconds>(
        std::chrono::system_clock::now().time_since_epoch()).count();
    auto age = std::chrono::milliseconds(now - orderbook.timestamp);
    
    return age > max_age;
}

std::vector<Price> PriceCache::GetAllPrices(const std::string& symbol) {
    std::vector<Price> prices;
    
    // Get all exchanges that have this symbol cached
    std::vector<std::string> exchanges = GetCachedExchanges();
    
    for (const auto& exchange : exchanges) {
        std::string key = MakeKey(exchange, symbol);
        Price price;
        if (price_cache_.GetConst(key, price)) {
            prices.push_back(price);
        }
    }
    
    return prices;
}

std::vector<std::string> PriceCache::GetCachedSymbols() const {
    std::set<std::string> unique_symbols;
    
    // Extract symbols from all cache keys
    auto keys = price_cache_.GetAllKeys();
    for (const auto& key : keys) {
        size_t colon_pos = key.find(':');
        if (colon_pos != std::string::npos && colon_pos + 1 < key.length()) {
            std::string symbol = key.substr(colon_pos + 1);
            unique_symbols.insert(symbol);
        }
    }
    
    return std::vector<std::string>(unique_symbols.begin(), unique_symbols.end());
}

std::vector<std::string> PriceCache::GetCachedExchanges() const {
    std::set<std::string> unique_exchanges;
    
    // Extract exchanges from all cache keys
    auto keys = price_cache_.GetAllKeys();
    for (const auto& key : keys) {
        size_t colon_pos = key.find(':');
        if (colon_pos != std::string::npos) {
            std::string exchange = key.substr(0, colon_pos);
            unique_exchanges.insert(exchange);
        }
    }
    
    return std::vector<std::string>(unique_exchanges.begin(), unique_exchanges.end());
}

void PriceCache::ClearAll() {
    price_cache_.Clear();
    orderbook_cache_.Clear();
    LOG_INFO("Price cache cleared");
}

void PriceCache::ClearExchange(const std::string& exchange) {
    std::string prefix = exchange + ":";
    
    // Get all keys and remove those starting with the exchange prefix
    auto keys = price_cache_.GetAllKeys();
    for (const auto& key : keys) {
        if (key.substr(0, prefix.length()) == prefix) {
            price_cache_.Remove(key);
        }
    }
    
    // Also clear orderbook cache for this exchange
    auto orderbook_keys = orderbook_cache_.GetAllKeys();
    for (const auto& key : orderbook_keys) {
        if (key.substr(0, prefix.length()) == prefix) {
            orderbook_cache_.Remove(key);
        }
    }
    
    LOG_INFO("Cleared cache for exchange: {}", exchange);
}

void PriceCache::ClearSymbol(const std::string& symbol) {
    std::string suffix = ":" + symbol;
    
    // Get all keys and remove those ending with the symbol suffix
    auto keys = price_cache_.GetAllKeys();
    for (const auto& key : keys) {
        if (key.length() >= suffix.length() && 
            key.substr(key.length() - suffix.length()) == suffix) {
            price_cache_.Remove(key);
        }
    }
    
    // Also clear orderbook cache for this symbol
    auto orderbook_keys = orderbook_cache_.GetAllKeys();
    for (const auto& key : orderbook_keys) {
        if (key.length() >= suffix.length() && 
            key.substr(key.length() - suffix.length()) == suffix) {
            orderbook_cache_.Remove(key);
        }
    }
    
    LOG_INFO("Cleared cache for symbol: {}", symbol);
}

void PriceCache::ForceCleanup() {
    price_cache_.CleanupExpired(price_ttl_);
    orderbook_cache_.CleanupExpired(orderbook_ttl_);
    LOG_DEBUG("Forced cache cleanup completed");
}

size_t PriceCache::GetEstimatedMemoryUsage() const {
    size_t usage = 0;
    usage += GetPriceCacheSize() * (sizeof(Price) + 50); // Approximate overhead
    usage += GetOrderBookCacheSize() * (sizeof(OrderBook) + 200); // OrderBook is larger
    return usage;
}

void PriceCache::LogStatistics() const {
    LOG_INFO("=== Price Cache Statistics ===");
    LOG_INFO("Prices: {}/{} slots, {:.1f}% hit rate", 
             GetPriceCacheSize(), 1000, GetPriceHitRate());
    LOG_INFO("OrderBooks: {}/{} slots, {:.1f}% hit rate",
             GetOrderBookCacheSize(), 100, GetOrderBookHitRate());
    LOG_INFO("Estimated memory usage: {} KB", GetEstimatedMemoryUsage() / 1024);
}

std::string PriceCache::MakeKey(const std::string& exchange, const std::string& symbol) const {
    return exchange + ":" + symbol;
}

void PriceCache::CleanupLoop() {
    while (running_) {
        // Use condition variable to allow fast shutdown
        std::unique_lock<std::mutex> lock(cleanup_mutex_);
        cleanup_cv_.wait_for(lock, std::chrono::seconds(30), [this] { return !running_; });
        
        if (!running_) break;
        
        try {
            ForceCleanup();
        } catch (const std::exception& e) {
            LOG_ERROR("Error in cache cleanup: {}", e.what());
        }
    }
}

// PriceCacheManager Implementation
std::unique_ptr<PriceCache> PriceCacheManager::instance_ = nullptr;
std::mutex PriceCacheManager::instance_mutex_;

PriceCache& PriceCacheManager::Instance() {
    std::lock_guard<std::mutex> lock(instance_mutex_);
    if (!instance_) {
        instance_ = std::make_unique<PriceCache>();
    }
    return *instance_;
}

void PriceCacheManager::Initialize(size_t max_prices, size_t max_orderbooks) {
    std::lock_guard<std::mutex> lock(instance_mutex_);
    if (!instance_) {
        instance_ = std::make_unique<PriceCache>(max_prices, max_orderbooks);
    }
}

void PriceCacheManager::Cleanup() {
    std::lock_guard<std::mutex> lock(instance_mutex_);
    if (instance_) {
        instance_.reset();
    }
}

} // namespace ats 