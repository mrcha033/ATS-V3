#include "arbitrage_engine.hpp"
#include <iostream>

namespace ats {

ArbitrageEngine::ArbitrageEngine(RiskManager* risk_manager, TradeExecutor* trade_executor)
    : risk_manager_(risk_manager), trade_executor_(trade_executor) {}

void ArbitrageEngine::start() {
    // Start the arbitrage engine
}

void ArbitrageEngine::stop() {
    // Stop the arbitrage engine
}

void ArbitrageEngine::evaluate_opportunity(const ArbitrageOpportunity& opportunity) {
    if (risk_manager_->IsTradeAllowed(opportunity)) {
        trade_executor_->execute_trade(opportunity);
    }
}

}