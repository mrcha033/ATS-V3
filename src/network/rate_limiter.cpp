#include "rate_limiter.hpp"
#include "../utils/logger.hpp"
#include <algorithm>

namespace ats {

RateLimiter::RateLimiter(int max_requests, std::chrono::milliseconds time_window)
    : max_requests_(max_requests), time_window_(time_window),
      available_tokens_(max_requests), total_requests_(0), 
      blocked_requests_(0), allowed_requests_(0) {
    
    last_refill_ = std::chrono::steady_clock::now();
}

bool RateLimiter::TryAcquire() {
    std::lock_guard<std::mutex> lock(mutex_);
    
    RefillTokens();
    total_requests_++;
    
    if (available_tokens_.load() > 0) {
        available_tokens_--;
        allowed_requests_++;
        CleanOldRequests();
        request_times_.push(std::chrono::steady_clock::now());
        return true;
    }
    
    blocked_requests_++;
    return false;
}

void RateLimiter::Acquire() {
    std::unique_lock<std::mutex> lock(mutex_);
    
    total_requests_++;
    
    cv_.wait(lock, [this] {
        RefillTokens();
        return available_tokens_.load() > 0;
    });
    
    available_tokens_--;
    allowed_requests_++;
    CleanOldRequests();
    request_times_.push(std::chrono::steady_clock::now());
}

bool RateLimiter::AcquireWithTimeout(std::chrono::milliseconds timeout) {
    std::unique_lock<std::mutex> lock(mutex_);
    
    total_requests_++;
    
    bool acquired = cv_.wait_for(lock, timeout, [this] {
        RefillTokens();
        return available_tokens_.load() > 0;
    });
    
    if (acquired) {
        available_tokens_--;
        allowed_requests_++;
        CleanOldRequests();
        request_times_.push(std::chrono::steady_clock::now());
        return true;
    }
    
    blocked_requests_++;
    return false;
}

void RateLimiter::SetLimit(int max_requests, std::chrono::milliseconds time_window) {
    std::lock_guard<std::mutex> lock(mutex_);
    max_requests_ = max_requests;
    time_window_ = time_window;
    available_tokens_ = std::min(available_tokens_.load(), max_requests);
}

int RateLimiter::GetAvailableTokens() const {
    std::lock_guard<std::mutex> lock(mutex_);
    const_cast<RateLimiter*>(this)->RefillTokens();
    return available_tokens_.load();
}

double RateLimiter::GetCurrentRate() const {
    std::lock_guard<std::mutex> lock(mutex_);
    
    if (request_times_.empty()) {
        return 0.0;
    }
    
    auto now = std::chrono::steady_clock::now();
    auto window_start = now - time_window_;
    
    // Count requests in current time window
    auto temp_queue = request_times_;
    int count = 0;
    
    while (!temp_queue.empty()) {
        if (temp_queue.front() >= window_start) {
            count++;
        }
        temp_queue.pop();
    }
    
    // Convert to requests per second
    double window_seconds = time_window_.count() / 1000.0;
    return count / window_seconds;
}

std::chrono::milliseconds RateLimiter::GetTimeUntilNextToken() const {
    std::lock_guard<std::mutex> lock(mutex_);
    
    if (available_tokens_.load() > 0) {
        return std::chrono::milliseconds(0);
    }
    
    if (request_times_.empty()) {
        return std::chrono::milliseconds(0);
    }
    
    auto oldest_request = request_times_.front();
    auto next_available = oldest_request + time_window_;
    auto now = std::chrono::steady_clock::now();
    
    if (next_available <= now) {
        return std::chrono::milliseconds(0);
    }
    
    return std::chrono::duration_cast<std::chrono::milliseconds>(next_available - now);
}

double RateLimiter::GetBlockRate() const {
    long long total = total_requests_.load();
    if (total == 0) return 0.0;
    return static_cast<double>(blocked_requests_.load()) / total * 100.0;
}

void RateLimiter::ResetStatistics() {
    total_requests_ = 0;
    blocked_requests_ = 0;
    allowed_requests_ = 0;
}

void RateLimiter::RefillTokens() {
    auto now = std::chrono::steady_clock::now();
    auto elapsed = now - last_refill_;
    
    if (elapsed >= time_window_) {
        // Full refill if enough time has passed
        available_tokens_ = max_requests_;
        last_refill_ = now;
        cv_.notify_all();
    } else {
        // Partial refill based on elapsed time
        double time_fraction = static_cast<double>(elapsed.count()) / time_window_.count();
        int tokens_to_add = static_cast<int>(max_requests_ * time_fraction);
        
        if (tokens_to_add > 0) {
            available_tokens_ = std::min(available_tokens_.load() + tokens_to_add, max_requests_);
            last_refill_ = now;
            cv_.notify_all();
        }
    }
}

void RateLimiter::CleanOldRequests() {
    auto now = std::chrono::steady_clock::now();
    auto cutoff = now - time_window_;
    
    while (!request_times_.empty() && request_times_.front() < cutoff) {
        request_times_.pop();
    }
}

bool RateLimiter::IsAllowed() {
    return available_tokens_.load() > 0;
}

// RateLimiterManager Implementation
void RateLimiterManager::AddLimiter(const std::string& exchange_name, 
                                   int max_requests, 
                                   std::chrono::milliseconds time_window) {
    std::lock_guard<std::mutex> lock(limiters_mutex_);
    limiters_[exchange_name] = std::make_shared<RateLimiter>(max_requests, time_window);
    LOG_INFO("Added rate limiter for {}: {} requests per {}ms", 
             exchange_name, max_requests, time_window.count());
}

void RateLimiterManager::RemoveLimiter(const std::string& exchange_name) {
    std::lock_guard<std::mutex> lock(limiters_mutex_);
    auto it = limiters_.find(exchange_name);
    if (it != limiters_.end()) {
        limiters_.erase(it);
        LOG_INFO("Removed rate limiter for {}", exchange_name);
    }
}

RateLimiter* RateLimiterManager::GetLimiter(const std::string& exchange_name) {
    std::lock_guard<std::mutex> lock(limiters_mutex_);
    auto it = limiters_.find(exchange_name);
    return (it != limiters_.end()) ? it->second.get() : nullptr;
}

bool RateLimiterManager::TryAcquire(const std::string& exchange_name) {
    std::shared_ptr<RateLimiter> limiter;
    {
        std::lock_guard<std::mutex> lock(limiters_mutex_);
        auto it = limiters_.find(exchange_name);
        if (it != limiters_.end()) {
            limiter = it->second;
        }
    }
    return limiter ? limiter->TryAcquire() : true; // Allow if no limiter
}

void RateLimiterManager::Acquire(const std::string& exchange_name) {
    std::shared_ptr<RateLimiter> limiter;
    {
        std::lock_guard<std::mutex> lock(limiters_mutex_);
        auto it = limiters_.find(exchange_name);
        if (it != limiters_.end()) {
            limiter = it->second;
        }
    }
    if (limiter) {
        limiter->Acquire();
    }
}

bool RateLimiterManager::AcquireWithTimeout(const std::string& exchange_name, 
                                          std::chrono::milliseconds timeout) {
    std::shared_ptr<RateLimiter> limiter;
    {
        std::lock_guard<std::mutex> lock(limiters_mutex_);
        auto it = limiters_.find(exchange_name);
        if (it != limiters_.end()) {
            limiter = it->second;
        }
    }
    return limiter ? limiter->AcquireWithTimeout(timeout) : true;
}

std::vector<std::string> RateLimiterManager::GetExchanges() const {
    std::lock_guard<std::mutex> lock(limiters_mutex_);
    std::vector<std::string> exchanges;
    exchanges.reserve(limiters_.size());
    
    for (const auto& pair : limiters_) {
        exchanges.push_back(pair.first);
    }
    
    return exchanges;
}

void RateLimiterManager::LogStatistics() const {
    std::lock_guard<std::mutex> lock(limiters_mutex_);
    
    LOG_INFO("=== Rate Limiter Statistics ===");
    for (const auto& pair : limiters_) {
        const auto& name = pair.first;
        const auto& limiter = pair.second;
        
        LOG_INFO("{}: {} total, {} allowed, {} blocked, {:.1f}% block rate",
                name,
                limiter->GetTotalRequests(),
                limiter->GetAllowedRequests(), 
                limiter->GetBlockedRequests(),
                limiter->GetBlockRate());
    }
}

} // namespace ats 