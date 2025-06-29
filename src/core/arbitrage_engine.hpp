#pragma once

#include <memory>
#include "risk_manager.hpp"
#include "trade_executor.hpp"

namespace ats {

class ArbitrageEngine {
public:
    ArbitrageEngine(RiskManager* risk_manager, TradeExecutor* trade_executor);

    void start();
    void stop();

    void evaluate_opportunity(const ArbitrageOpportunity& opportunity);

private:
    RiskManager* risk_manager_;
    TradeExecutor* trade_executor_;
};

} 