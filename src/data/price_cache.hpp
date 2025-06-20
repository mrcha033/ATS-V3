#pragma once

#include <unordered_map>
#include <list>
#include <mutex>
#include <chrono>
#include <atomic>
#include <condition_variable>
#include "../exchange/exchange_interface.hpp"

namespace ats {

// Cache entry with timestamp and access tracking
template<typename T>
struct CacheEntry {
    T data;
    std::chrono::steady_clock::time_point timestamp;
    std::chrono::steady_clock::time_point last_access;
    std::atomic<long long> access_count;
    
    CacheEntry() : data(), 
                  timestamp(std::chrono::steady_clock::now()),
                  last_access(std::chrono::steady_clock::now()),
                  access_count(0) {}
    
    CacheEntry(const T& d) : data(d), 
                            timestamp(std::chrono::steady_clock::now()),
                            last_access(std::chrono::steady_clock::now()),
                            access_count(1) {}
    
    CacheEntry(T&& d) : data(std::move(d)), 
                       timestamp(std::chrono::steady_clock::now()),
                       last_access(std::chrono::steady_clock::now()),
                       access_count(1) {}
    
    // Copy constructor
    CacheEntry(const CacheEntry& other) : data(other.data),
                                         timestamp(other.timestamp),
                                         last_access(other.last_access),
                                         access_count(other.access_count.load()) {}
    
    // Move constructor
    CacheEntry(CacheEntry&& other) noexcept : data(std::move(other.data)),
                                             timestamp(other.timestamp),
                                             last_access(other.last_access),
                                             access_count(other.access_count.load()) {}
    
    // Copy assignment
    CacheEntry& operator=(const CacheEntry& other) {
        if (this != &other) {
            data = other.data;
            timestamp = other.timestamp;
            last_access = other.last_access;
            access_count = other.access_count.load();
        }
        return *this;
    }
    
    // Move assignment
    CacheEntry& operator=(CacheEntry&& other) noexcept {
        if (this != &other) {
            data = std::move(other.data);
            timestamp = other.timestamp;
            last_access = other.last_access;
            access_count = other.access_count.load();
        }
        return *this;
    }
};

// LRU Cache implementation optimized for memory usage
template<typename Key, typename Value>
class LRUCache {
private:
    using CacheEntryType = CacheEntry<Value>;
    using ListIterator = typename std::list<std::pair<Key, CacheEntryType>>::iterator;
    
    size_t max_size_;
    std::list<std::pair<Key, CacheEntryType>> cache_list_;
    std::unordered_map<Key, ListIterator> cache_map_;
    mutable std::mutex cache_mutex_;
    
    // Statistics
    std::atomic<long long> hits_;
    std::atomic<long long> misses_;
    std::atomic<long long> evictions_;
    
public:
    explicit LRUCache(size_t max_size) : max_size_(max_size), hits_(0), misses_(0), evictions_(0) {}
    
    // Get value from cache
    bool Get(const Key& key, Value& value) {
        std::lock_guard<std::mutex> lock(cache_mutex_);
        
        auto it = cache_map_.find(key);
        if (it == cache_map_.end()) {
            misses_++;
            return false;
        }
        
        // Move to front (most recently used)
        cache_list_.splice(cache_list_.begin(), cache_list_, it->second);
        
        // Update access info
        it->second->second.last_access = std::chrono::steady_clock::now();
        it->second->second.access_count++;
        
        value = it->second->second.data;
        hits_++;
        return true;
    }
    
    // Const version that doesn't update access info (for checking staleness)
    bool GetConst(const Key& key, Value& value) const {
        std::lock_guard<std::mutex> lock(cache_mutex_);
        
        auto it = cache_map_.find(key);
        if (it == cache_map_.end()) {
            misses_++;
            return false;
        }
        
        value = it->second->second.data;
        return true;
    }
    
    // Put value in cache
    void Put(const Key& key, const Value& value) {
        std::lock_guard<std::mutex> lock(cache_mutex_);
        
        auto it = cache_map_.find(key);
        if (it != cache_map_.end()) {
            // Update existing entry
            it->second->second.data = value;
            it->second->second.timestamp = std::chrono::steady_clock::now();
            it->second->second.last_access = std::chrono::steady_clock::now();
            it->second->second.access_count++;
            
            // Move to front
            cache_list_.splice(cache_list_.begin(), cache_list_, it->second);
            return;
        }
        
        // Add new entry
        cache_list_.emplace_front(key, CacheEntryType(value));
        cache_map_[key] = cache_list_.begin();
        
        // Evict if necessary
        if (cache_list_.size() > max_size_) {
            auto last = cache_list_.end();
            --last;
            cache_map_.erase(last->first);
            cache_list_.pop_back();
            evictions_++;
        }
    }
    
    // Remove entry
    bool Remove(const Key& key) {
        std::lock_guard<std::mutex> lock(cache_mutex_);
        
        auto it = cache_map_.find(key);
        if (it == cache_map_.end()) {
            return false;
        }
        
        cache_list_.erase(it->second);
        cache_map_.erase(it);
        return true;
    }
    
    // Check if key exists
    bool Contains(const Key& key) const {
        std::lock_guard<std::mutex> lock(cache_mutex_);
        return cache_map_.find(key) != cache_map_.end();
    }
    
    // Clear all entries
    void Clear() {
        std::lock_guard<std::mutex> lock(cache_mutex_);
        cache_list_.clear();
        cache_map_.clear();
    }
    
    // Cleanup expired entries
    void CleanupExpired(std::chrono::seconds max_age) {
        std::lock_guard<std::mutex> lock(cache_mutex_);
        
        auto now = std::chrono::steady_clock::now();
        auto it = cache_list_.begin();
        
        while (it != cache_list_.end()) {
            if (now - it->second.timestamp > max_age) {
                cache_map_.erase(it->first);
                it = cache_list_.erase(it);
            } else {
                ++it;
            }
        }
    }
    
    // Statistics
    size_t Size() const {
        std::lock_guard<std::mutex> lock(cache_mutex_);
        return cache_list_.size();
    }
    
    // Get all keys in cache
    std::vector<Key> GetAllKeys() const {
        std::lock_guard<std::mutex> lock(cache_mutex_);
        std::vector<Key> keys;
        keys.reserve(cache_list_.size());
        
        for (const auto& entry : cache_list_) {
            keys.push_back(entry.first);
        }
        return keys;
    }
    
    double GetHitRate() const {
        long long total = hits_.load() + misses_.load();
        return total > 0 ? static_cast<double>(hits_.load()) / total * 100.0 : 0.0;
    }
    
    long long GetHits() const { return hits_.load(); }
    long long GetMisses() const { return misses_.load(); }
    long long GetEvictions() const { return evictions_.load(); }
    
    void ResetStatistics() {
        hits_ = 0;
        misses_ = 0;
        evictions_ = 0;
    }
};

// Specialized price cache for arbitrage system
class PriceCache {
private:
    LRUCache<std::string, Price> price_cache_;
    LRUCache<std::string, OrderBook> orderbook_cache_;
    
    // Configuration
    std::chrono::seconds price_ttl_;
    std::chrono::seconds orderbook_ttl_;
    
    // Automatic cleanup
    std::thread cleanup_thread_;
    std::atomic<bool> running_;
    std::condition_variable cleanup_cv_;
    std::mutex cleanup_mutex_;
    
public:
    PriceCache(size_t max_prices = 1000, size_t max_orderbooks = 100);
    ~PriceCache();
    
    // Configuration
    void SetPriceTTL(std::chrono::seconds ttl) { price_ttl_ = ttl; }
    void SetOrderBookTTL(std::chrono::seconds ttl) { orderbook_ttl_ = ttl; }
    
    // Price operations
    bool GetPrice(const std::string& exchange, const std::string& symbol, Price& price);
    void SetPrice(const std::string& exchange, const std::string& symbol, const Price& price);
    bool IsPriceStale(const std::string& exchange, const std::string& symbol, 
                     std::chrono::seconds max_age) const;
    
    // OrderBook operations
    bool GetOrderBook(const std::string& exchange, const std::string& symbol, OrderBook& orderbook);
    void SetOrderBook(const std::string& exchange, const std::string& symbol, const OrderBook& orderbook);
    bool IsOrderBookStale(const std::string& exchange, const std::string& symbol,
                         std::chrono::seconds max_age) const;
    
    // Bulk operations
    std::vector<Price> GetAllPrices(const std::string& symbol);
    std::vector<std::string> GetCachedSymbols() const;
    std::vector<std::string> GetCachedExchanges() const;
    
    // Cache management
    void ClearAll();
    void ClearExchange(const std::string& exchange);
    void ClearSymbol(const std::string& symbol);
    void ForceCleanup();
    
    // Statistics and monitoring
    size_t GetPriceCacheSize() const { return price_cache_.Size(); }
    size_t GetOrderBookCacheSize() const { return orderbook_cache_.Size(); }
    double GetPriceHitRate() const { return price_cache_.GetHitRate(); }
    double GetOrderBookHitRate() const { return orderbook_cache_.GetHitRate(); }
    
    // Memory usage estimation
    size_t GetEstimatedMemoryUsage() const;
    void LogStatistics() const;
    
private:
    std::string MakeKey(const std::string& exchange, const std::string& symbol) const;
    void CleanupLoop();
};

// Global price cache instance
class PriceCacheManager {
private:
    static std::unique_ptr<PriceCache> instance_;
    static std::mutex instance_mutex_;
    
public:
    static PriceCache& Instance();
    static void Initialize(size_t max_prices = 1000, size_t max_orderbooks = 100);
    static void Cleanup();
};

} // namespace ats 