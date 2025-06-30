#pragma once

#include "types.hpp"
#include <variant>

namespace ats {

struct PriceUpdateEvent {
    PriceComparison comparison;
};

struct ArbitrageOpportunityEvent {
    ArbitrageOpportunity opportunity;
};

struct TradeExecutionEvent {
    Trade trade;
};

using Event = std::variant<PriceUpdateEvent, ArbitrageOpportunityEvent, TradeExecutionEvent>;

}