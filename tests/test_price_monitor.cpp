#include <gtest/gtest.h>
#include "core/price_monitor.hpp"
#include "mocks/mock_exchange.hpp"

using ::testing::Return;

TEST(PriceMonitorTest, UpdateCallbackIsCalled) {
    auto exchange1 = std::make_shared<ats::testing::MockExchange>();
    auto exchange2 = std::make_shared<ats::testing::MockExchange>();
    std::vector<std::shared_ptr<ats::ExchangeInterface>> exchanges = {exchange1, exchange2};

    ats::PriceMonitor monitor(exchanges);
    bool callback_called = false;
    monitor.set_update_callback([&](const ats::PriceComparison& comp) {
        callback_called = true;
    });

    EXPECT_CALL(*exchange1, get_name()).WillRepeatedly(Return("exchange1"));
    EXPECT_CALL(*exchange2, get_name()).WillRepeatedly(Return("exchange2"));
    EXPECT_CALL(*exchange1, get_price("BTC/USDT")).WillOnce(Return(ats::Price{"BTC/USDT", 100, 101, 100.5, 1000, 0}));
    EXPECT_CALL(*exchange2, get_price("BTC/USDT")).WillOnce(Return(ats::Price{"BTC/USDT", 100, 101, 100.5, 1000, 0}));

    monitor.check_prices();

    ASSERT_TRUE(callback_called);
}
