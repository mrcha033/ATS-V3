#pragma once

#include "core/risk_manager.hpp"
#include <gmock/gmock.h>

namespace ats {
namespace testing {

class MockRiskManager : public ats::RiskManager {
public:
    MockRiskManager() : ats::RiskManager(nullptr, nullptr) {}
    MOCK_METHOD(bool, IsTradeAllowed, (const ats::ArbitrageOpportunity&), (override));
};

} // namespace testing
} // namespace ats
