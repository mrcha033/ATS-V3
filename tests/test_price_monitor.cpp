#include <gtest/gtest.h>
#include "core/price_monitor.hpp"
#include "mocks/mock_exchange.hpp"
#include "mocks/mock_event_pusher.hpp"
#include "utils/config_manager.hpp"
#include "core/app_state.hpp"

using ::testing::Return;
using ::testing::_;

TEST(PriceMonitorTest, UpdateCallbackIsCalled) {
    ats::ExchangeConfig dummy_config;
    ats::AppState app_state;
    auto exchange1 = std::make_shared<ats::testing::MockExchange>(dummy_config, &app_state);
    auto exchange2 = std::make_shared<ats::testing::MockExchange>(dummy_config, &app_state);
    std::vector<std::shared_ptr<ats::ExchangeInterface>> exchanges = {exchange1, exchange2};
    
    ats::ConfigManager config_manager;
    ats::TradingConfig trading_config;
    trading_config.pairs = {"BTC/USDT"};
    config_manager.get_trading_config() = trading_config;

    ats::PriceMonitor monitor(&config_manager, exchanges);
    ats::mocks::MockEventPusher mock_event_pusher;
    monitor.set_event_pusher(&mock_event_pusher);

    EXPECT_CALL(*exchange1, get_name()).WillRepeatedly(Return("exchange1"));
    EXPECT_CALL(*exchange2, get_name()).WillRepeatedly(Return("exchange2"));
    EXPECT_CALL(*exchange1, get_price("BTC/USDT")).WillOnce(Return(ats::Price{"BTC/USDT", 100, 101, 100.5, 1000, 0}));
    EXPECT_CALL(*exchange2, get_price("BTC/USDT")).WillOnce(Return(ats::Price{"BTC/USDT", 100, 101, 100.5, 1000, 0}));
    EXPECT_CALL(mock_event_pusher, push_event(_)).Times(1);

    monitor.check_prices();
}
