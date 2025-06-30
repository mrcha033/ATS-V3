#include "opportunity_detector.hpp"
#include <iostream>

namespace ats {

OpportunityDetector::OpportunityDetector(ConfigManager* config_manager, const std::vector<std::string>& symbols)
    : config_manager_(config_manager), symbols_(symbols), event_pusher_(nullptr) {}

void OpportunityDetector::start() {
    // Start the opportunity detector
}

void OpportunityDetector::stop() {
    // Stop the opportunity detector
}

void OpportunityDetector::set_event_pusher(EventPusher* event_pusher) {
    event_pusher_ = event_pusher;
}

void OpportunityDetector::update_prices(const PriceComparison& comparison) {
    // Find the best buy and sell prices
    double best_bid = 0;
    double best_ask = 1e9;
    std::string best_bid_exchange;
    std::string best_ask_exchange;

    for (const auto& [exchange, price] : comparison.exchange_prices) {
        if (price.bid > best_bid) {
            best_bid = price.bid;
            best_bid_exchange = exchange;
        }
        if (price.ask < best_ask) {
            best_ask = price.ask;
            best_ask_exchange = exchange;
        }
    }

    // Check for an arbitrage opportunity
    if (best_bid > best_ask) {
        ExchangeFees buy_fees = config_manager_->get_exchange_fees(best_ask_exchange);
        ExchangeFees sell_fees = config_manager_->get_exchange_fees(best_bid_exchange);

        double buy_price_with_fee = best_ask * (1 + buy_fees.taker_fee);
        double sell_price_with_fee = best_bid * (1 - sell_fees.taker_fee);
        double profit = sell_price_with_fee - buy_price_with_fee;

        if (profit > 0) {
            ArbitrageOpportunity opportunity;
            opportunity.symbol = comparison.symbol;
            opportunity.buy_exchange = best_ask_exchange;
            opportunity.sell_exchange = best_bid_exchange;
            opportunity.buy_price = best_ask;
            opportunity.sell_price = best_bid;
            opportunity.profit = profit;
            opportunity.is_executable = true;

            if (event_pusher_) {
                event_pusher_->push_event(ArbitrageOpportunityEvent{opportunity});
            }
        }
    }
}

}