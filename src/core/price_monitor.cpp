#include "price_monitor.hpp"
#include <iostream>

namespace ats {

PriceMonitor::PriceMonitor(const std::vector<std::shared_ptr<ExchangeInterface>>& exchanges)
    : exchanges_(exchanges) {}

void PriceMonitor::start() {
    // Start the price monitor
}

void PriceMonitor::stop() {
    // Stop the price monitor
}

void PriceMonitor::set_update_callback(UpdateCallback callback) {
    update_callback_ = callback;
}

void PriceMonitor::check_prices() {
    if (exchanges_.size() < 2) {
        return;
    }

    // For simplicity, we'll just check the price of the first symbol on all exchanges.
    std::string symbol = "BTC/USDT";
    PriceComparison comparison;
    comparison.symbol = symbol;
    comparison.timestamp = std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::system_clock::now().time_since_epoch()).count();

    for (const auto& exchange : exchanges_) {
        comparison.exchange_prices[exchange->get_name()] = exchange->get_price(symbol);
    }

    if (update_callback_) {
        update_callback_(comparison);
    }
}

}