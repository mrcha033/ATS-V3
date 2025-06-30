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
        .With(Property(&ats::Event::get<ats::ArbitrageOpportunityEvent>,
                       Property(&ats::ArbitrageOpportunityEvent::opportunity,
                                AllOf(Property(&ats::ArbitrageOpportunity::symbol, "BTC/USDT"),
                                      Property(&ats::ArbitrageOpportunity::buy_exchange, "exchange2"),
                                      Property(&ats::ArbitrageOpportunity::sell_exchange, "exchange1"),
                                      Property(&ats::ArbitrageOpportunity::buy_price, 100),
                                      Property(&ats::ArbitrageOpportunity::sell_price, 101)))));

    ats::PriceComparison comparison;
    comparison.symbol = "BTC/USDT";
    comparison.exchange_prices["exchange1"] = { "BTC/USDT", 101, 102, 101.5, 1000, 0 };
    comparison.exchange_prices["exchange2"] = { "BTC/USDT", 99, 100, 99.5, 1000, 0 };

    detector.update_prices(comparison);
}
