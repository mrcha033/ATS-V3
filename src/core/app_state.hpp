#pragma once

#include <atomic>
#include <mutex>
#include <vector>
#include "types.hpp"

namespace ats {

class AppState {
public:
    AppState() : running_(true) {}

    void shutdown() { running_ = false; }
    bool is_running() const { return running_; }

    void add_trade(const Trade& trade);
    std::vector<Trade> get_trades() const;

private:
    std::atomic<bool> running_;
    mutable std::mutex mutex_;
    std::vector<Trade> trades_;
};

} // namespace ats
