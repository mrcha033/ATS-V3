#pragma once

#include <chrono>
#include <mutex>
#include <queue>
#include <atomic>
#include <condition_variable>
#include <unordered_map>
#include <memory>
#include <vector>
#include <string>

namespace ats {

class RateLimiter {
private:
    // Token bucket parameters
    int max_requests_;
    std::chrono::milliseconds time_window_;
    
    // State
    mutable std::mutex mutex_;
    std::queue<std::chrono::steady_clock::time_point> request_times_;
    std::atomic<int> available_tokens_;
    std::chrono::steady_clock::time_point last_refill_;
    
    // For blocking operations
    std::condition_variable cv_;
    
    // Statistics
    std::atomic<long long> total_requests_;
    std::atomic<long long> blocked_requests_;
    std::atomic<long long> allowed_requests_;
    
public:
    // Constructor: max_requests per time_window
    RateLimiter(int max_requests, std::chrono::milliseconds time_window);
    ~RateLimiter() = default;
    
    // Non-blocking: returns true if request is allowed
    bool TryAcquire();
    
    // Blocking: waits until request is allowed
    void Acquire();
    
    // Wait with timeout: returns true if acquired, false if timeout
    bool AcquireWithTimeout(std::chrono::milliseconds timeout);
    
    // Configuration
    void SetLimit(int max_requests, std::chrono::milliseconds time_window);
    int GetMaxRequests() const { return max_requests_; }
    std::chrono::milliseconds GetTimeWindow() const { return time_window_; }
    
    // Current state
    int GetAvailableTokens() const;
    double GetCurrentRate() const; // requests per second
    std::chrono::milliseconds GetTimeUntilNextToken() const;
    
    // Statistics
    long long GetTotalRequests() const { return total_requests_.load(); }
    long long GetBlockedRequests() const { return blocked_requests_.load(); }
    long long GetAllowedRequests() const { return allowed_requests_.load(); }
    double GetBlockRate() const;
    
    // Reset statistics
    void ResetStatistics();
    
private:
    void RefillTokens();
    void CleanOldRequests();
    bool IsAllowed();
};

// Multi-exchange rate limiter manager
class RateLimiterManager {
private:
    std::unordered_map<std::string, std::unique_ptr<RateLimiter>> limiters_;
    mutable std::mutex limiters_mutex_;
    
public:
    RateLimiterManager() = default;
    ~RateLimiterManager() = default;
    
    // Add rate limiter for an exchange
    void AddLimiter(const std::string& exchange_name, 
                   int max_requests, 
                   std::chrono::milliseconds time_window);
    
    // Remove rate limiter
    void RemoveLimiter(const std::string& exchange_name);
    
    // Get rate limiter for exchange
    RateLimiter* GetLimiter(const std::string& exchange_name);
    
    // Convenience methods
    bool TryAcquire(const std::string& exchange_name);
    void Acquire(const std::string& exchange_name);
    bool AcquireWithTimeout(const std::string& exchange_name, 
                           std::chrono::milliseconds timeout);
    
    // Get all exchanges
    std::vector<std::string> GetExchanges() const;
    
    // Statistics for all exchanges
    void LogStatistics() const;
};

} // namespace ats 