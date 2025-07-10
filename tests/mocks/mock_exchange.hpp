#pragma once

#include "exchange/exchange_interface.hpp"
#include <gmock/gmock.h>

namespace ats {
namespace testing {

class MockExchange : public ats::ExchangeInterface {
public:
    MockExchange(const ExchangeConfig& config, AppState* app_state)
        : ExchangeInterface(config, app_state) {}
    MOCK_METHOD(std::string, get_name, (), (const, override));
    MOCK_METHOD(void, connect, (), (override));
    MOCK_METHOD(void, disconnect, (), (override));
    MOCK_METHOD(ats::Price, get_price, (const std::string&), (override));
    MOCK_METHOD(ats::OrderResult, place_order, (const ats::Order&), (override));
};

} // namespace testing
} // namespace ats
