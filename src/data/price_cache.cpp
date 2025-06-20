#include "price_cache.hpp"
#include "../utils/logger.hpp"
#include <thread>

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
    if (!const_cast<PriceCache*>(this)->price_cache_.Get(key, price)) {
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
    if (!const_cast<PriceCache*>(this)->orderbook_cache_.Get(key, orderbook)) {
        return true; // No orderbook data is considered stale
    }
    
    auto now = std::chrono::duration_cast<std::chrono::milliseconds>(
        std::chrono::system_clock::now().time_since_epoch()).count();
    auto age = std::chrono::milliseconds(now - orderbook.timestamp);
    
    return age > max_age;
}

std::vector<Price> PriceCache::GetAllPrices(const std::string& symbol) {
    // TODO: Implement iteration through cache to find all prices for symbol
    std::vector<Price> prices;
    return prices;
}

std::vector<std::string> PriceCache::GetCachedSymbols() const {
    // TODO: Implement symbol extraction from cache keys
    std::vector<std::string> symbols;
    return symbols;
}

std::vector<std::string> PriceCache::GetCachedExchanges() const {
    // TODO: Implement exchange extraction from cache keys  
    std::vector<std::string> exchanges;
    return exchanges;
}

void PriceCache::ClearAll() {
    price_cache_.Clear();
    orderbook_cache_.Clear();
    LOG_INFO("Price cache cleared");
}

void PriceCache::ClearExchange(const std::string& exchange) {
    // TODO: Implement selective clearing by exchange prefix
    LOG_INFO("Cleared cache for exchange: {}", exchange);
}

void PriceCache::ClearSymbol(const std::string& symbol) {
    // TODO: Implement selective clearing by symbol suffix
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
        std::this_thread::sleep_for(std::chrono::seconds(30));
        
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