#pragma once

#include "core/trade_executor.hpp"
#include <gmock/gmock.h>

namespace ats {
namespace testing {

class MockTradeExecutor : public ats::TradeExecutor {
public:
    MockTradeExecutor() : ats::TradeExecutor(nullptr, nullptr, nullptr) {}
    MOCK_METHOD(void, execute_trade, (const ats::ArbitrageOpportunity&), (override));
};

} // namespace testing
} // namespace ats
