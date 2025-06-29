#include "opportunity_detector.hpp"
#include <iostream>

namespace ats {

OpportunityDetector::OpportunityDetector(const std::vector<std::string>& symbols)
    : symbols_(symbols) {}

void OpportunityDetector::start() {
    // Start the opportunity detector
}

void OpportunityDetector::stop() {
    // Stop the opportunity detector
}

void OpportunityDetector::set_opportunity_callback(OpportunityCallback callback) {
    opportunity_callback_ = callback;
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
        ArbitrageOpportunity opportunity;
        opportunity.symbol = comparison.symbol;
        opportunity.buy_exchange = best_ask_exchange;
        opportunity.sell_exchange = best_bid_exchange;
        opportunity.buy_price = best_ask;
        opportunity.sell_price = best_bid;
        opportunity.profit = best_bid - best_ask;
        opportunity.is_executable = true;

        if (opportunity_callback_) {
            opportunity_callback_(opportunity);
        }
    }
}

}