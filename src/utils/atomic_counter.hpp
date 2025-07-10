#pragma once

#include <atomic>
#include <chrono>
#include <vector>
#include <mutex>

namespace ats {

// Thread-safe counter with rate limiting capabilities
class AtomicCounter {
public:
    explicit AtomicCounter(size_t initial_value = 0) : count_(initial_value) {}

    // Increment and return new value
    size_t increment() {
        return ++count_;
    }

    // Decrement and return new value
    size_t decrement() {
        return --count_;
    }

    // Add value and return new total
    size_t add(size_t value) {
        return count_ += value;
    }

    // Subtract value and return new total
    size_t subtract(size_t value) {
        return count_ -= value;
    }

    // Get current value
    size_t get() const {
        return count_.load();
    }

    // Set value
    void set(size_t value) {
        count_.store(value);
    }

    // Reset to zero
    void reset() {
        count_.store(0);
    }

    // Compare and swap
    bool compare_exchange(size_t expected, size_t desired) {
        return count_.compare_exchange_weak(expected, desired);
    }

private:
    std::atomic<size_t> count_;
};

// Rate limiter using sliding window
class RateLimiter {
public:
    RateLimiter(size_t max_requests, std::chrono::milliseconds window_size)
        : max_requests_(max_requests), window_size_(window_size) {}

    // Check if request is allowed
    bool try_acquire() {
        auto now = std::chrono::steady_clock::now();
        
        std::lock_guard<std::mutex> lock(mutex_);
        
        // Remove old timestamps
        auto cutoff = now - window_size_;
        while (!timestamps_.empty() && timestamps_.front() < cutoff) {
            timestamps_.erase(timestamps_.begin());
        }
        
        // Check if we can make another request
        if (timestamps_.size() < max_requests_) {
            timestamps_.push_back(now);
            return true;
        }
        
        return false;
    }

    // Get current request count in window
    size_t current_count() {
        auto now = std::chrono::steady_clock::now();
        
        std::lock_guard<std::mutex> lock(mutex_);
        
        auto cutoff = now - window_size_;
        while (!timestamps_.empty() && timestamps_.front() < cutoff) {
            timestamps_.erase(timestamps_.begin());
        }
        
        return timestamps_.size();
    }

    // Reset the rate limiter
    void reset() {
        std::lock_guard<std::mutex> lock(mutex_);
        timestamps_.clear();
    }

    // Get time until next request is allowed
    std::chrono::milliseconds time_until_next_request() {
        auto now = std::chrono::steady_clock::now();
        
        std::lock_guard<std::mutex> lock(mutex_);
        
        if (timestamps_.size() < max_requests_) {
            return std::chrono::milliseconds(0);
        }
        
        auto oldest = timestamps_.front();
        auto next_available = oldest + window_size_;
        
        if (next_available <= now) {
            return std::chrono::milliseconds(0);
        }
        
        return std::chrono::duration_cast<std::chrono::milliseconds>(next_available - now);
    }

private:
    const size_t max_requests_;
    const std::chrono::milliseconds window_size_;
    std::vector<std::chrono::steady_clock::time_point> timestamps_;
    std::mutex mutex_;
};

// Thread-safe statistics tracker
class StatsTracker {
public:
    void record_value(double value) {
        std::lock_guard<std::mutex> lock(mutex_);
        count_++;
        sum_ += value;
        sum_squares_ += value * value;
        
        if (count_ == 1 || value < min_) {
            min_ = value;
        }
        if (count_ == 1 || value > max_) {
            max_ = value;
        }
    }

    struct Statistics {
        size_t count = 0;
        double sum = 0.0;
        double mean = 0.0;
        double variance = 0.0;
        double std_dev = 0.0;
        double min = 0.0;
        double max = 0.0;
    };

    Statistics get_statistics() const {
        std::lock_guard<std::mutex> lock(mutex_);
        
        Statistics stats;
        stats.count = count_;
        stats.sum = sum_;
        stats.min = min_;
        stats.max = max_;
        
        if (count_ > 0) {
            stats.mean = sum_ / count_;
            
            if (count_ > 1) {
                stats.variance = (sum_squares_ - (sum_ * sum_) / count_) / (count_ - 1);
                stats.std_dev = std::sqrt(stats.variance);
            }
        }
        
        return stats;
    }

    void reset() {
        std::lock_guard<std::mutex> lock(mutex_);
        count_ = 0;
        sum_ = 0.0;
        sum_squares_ = 0.0;
        min_ = 0.0;
        max_ = 0.0;
    }

private:
    mutable std::mutex mutex_;
    size_t count_ = 0;
    double sum_ = 0.0;
    double sum_squares_ = 0.0;
    double min_ = 0.0;
    double max_ = 0.0;
};

} // namespace ats