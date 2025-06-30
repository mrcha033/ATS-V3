#include "price_monitor.hpp"
#include <iostream>
#include <chrono>
#include "../utils/logger.hpp"
#include "../exchange/exchange_exception.hpp"

namespace ats {

PriceMonitor::PriceMonitor(ConfigManager* config_manager, const std::vector<std::shared_ptr<ExchangeInterface>>& exchanges)
    : config_manager_(config_manager), exchanges_(exchanges), event_pusher_(nullptr), running_(false) {
    symbols_ = config_manager_->get_symbols();
}

PriceMonitor::~PriceMonitor() {
    stop();
}

void PriceMonitor::start() {
    running_ = true;
    thread_ = std::thread(&PriceMonitor::run, this);
}

void PriceMonitor::stop() {
    running_ = false;
    if (thread_.joinable()) {
        thread_.join();
    }
}

void PriceMonitor::set_event_pusher(EventPusher* event_pusher) {
    event_pusher_ = event_pusher;
}

void PriceMonitor::run() {
    while (running_) {
        check_prices();
        std::this_thread::sleep_for(std::chrono::milliseconds(1000));
    }
}

void PriceMonitor::check_prices() {
    if (exchanges_.size() < 2 || !event_pusher_) {
        return;
    }

    for (const auto& symbol : symbols_) {
        PriceComparison comparison;
        comparison.symbol = symbol;
        comparison.timestamp = std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::system_clock::now().time_since_epoch()).count();

        for (const auto& exchange : exchanges_) {
            try {
                comparison.exchange_prices[exchange->get_name()] = exchange->get_price(symbol);
            } catch (const ExchangeException& e) {
                Logger::error("Error getting price from " + exchange->get_name() + ": " + e.what());
            }
        }

        event_pusher_->push_event(PriceUpdateEvent{comparison});
    }
}

}