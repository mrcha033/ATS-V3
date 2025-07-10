#include <gtest/gtest.h>
#include "core/opportunity_detector.hpp"
#include "mocks/mock_event_pusher.hpp"
#include "utils/config_manager.hpp"

using ::testing::_;
using ::testing::A;
using ::testing::Property;

TEST(OpportunityDetectorTest, NoOpportunityWhenPricesAreTheSame) {
    ats::ConfigManager config_manager;
    config_manager.load("config/settings.json");
    ats::OpportunityDetector detector(&config_manager, {"BTC/USDT"});
    ats::mocks::MockEventPusher mock_event_pusher;
    detector.set_event_pusher(&mock_event_pusher);

    EXPECT_CALL(mock_event_pusher, push_event(_)).Times(0);

    ats::PriceComparison comparison;
    comparison.symbol = "BTC/USDT";
    comparison.exchange_prices["exchange1"] = { "BTC/USDT", 100, 101, 100.5, 1000, 0 };
    comparison.exchange_prices["exchange2"] = { "BTC/USDT", 100, 101, 100.5, 1000, 0 };

    detector.update_prices(comparison);
}

TEST(OpportunityDetectorTest, OpportunityFoundWhenBidIsHigherThanAsk) {
    ats::ConfigManager config_manager;
    config_manager.load("config/settings.json");
    ats::OpportunityDetector detector(&config_manager, {"BTC/USDT"});
    ats::mocks::MockEventPusher mock_event_pusher;
    detector.set_event_pusher(&mock_event_pusher);

    EXPECT_CALL(mock_event_pusher, push_event(A<ats::Event>()))
        .WillOnce([](const ats::Event& event) {
            ASSERT_TRUE(std::holds_alternative<ats::ArbitrageOpportunityEvent>(event));
            auto opportunity_event = std::get<ats::ArbitrageOpportunityEvent>(event);
            EXPECT_EQ(opportunity_event.opportunity.symbol, "BTC/USDT");
            EXPECT_EQ(opportunity_event.opportunity.buy_exchange, "exchange2");
            EXPECT_EQ(opportunity_event.opportunity.sell_exchange, "exchange1");
            EXPECT_EQ(opportunity_event.opportunity.buy_price, 100);
            EXPECT_EQ(opportunity_event.opportunity.sell_price, 102);
        });

    ats::PriceComparison comparison;
    comparison.symbol = "BTC/USDT";
    comparison.exchange_prices["exchange1"] = { "BTC/USDT", 102, 103, 102.5, 1000, 0 };
    comparison.exchange_prices["exchange2"] = { "BTC/USDT", 99, 100, 99.5, 1000, 0 };

    detector.update_prices(comparison);
}
