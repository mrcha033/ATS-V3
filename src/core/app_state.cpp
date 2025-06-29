#include "app_state.hpp"

namespace ats {



void AppState::add_trade(const Trade& trade) {
    std::lock_guard<std::mutex> lock(mutex_);
    trades_.push_back(trade);
}

std::vector<Trade> AppState::get_trades() const {
    std::lock_guard<std::mutex> lock(mutex_);
    return trades_;
}

} // namespace ats
