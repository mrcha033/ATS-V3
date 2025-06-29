#pragma once

#include "exchange/exchange_interface.hpp"
#include <gmock/gmock.h>

namespace ats {
namespace testing {

class MockExchange : public ats::ExchangeInterface {
public:
    MOCK_METHOD(std::string, get_name, (), (const, override));
    MOCK_METHOD(void, connect, (), (override));
    MOCK_METHOD(void, disconnect, (), (override));
    MOCK_METHOD(ats::Price, get_price, (const std::string&), (override));
};

} // namespace testing
} // namespace ats
