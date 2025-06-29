#include <gtest/gtest.h>
#include "core/opportunity_detector.hpp"

TEST(OpportunityDetectorTest, NoOpportunityWhenPricesAreTheSame) {
    ats::OpportunityDetector detector({"BTC/USDT"});
    bool opportunity_found = false;
    detector.set_opportunity_callback([&](const ats::ArbitrageOpportunity& opp) {
        opportunity_found = true;
    });

    ats::PriceComparison comparison;
    comparison.symbol = "BTC/USDT";
    comparison.exchange_prices["exchange1"] = { "BTC/USDT", 100, 101, 100.5, 1000, 0 };
    comparison.exchange_prices["exchange2"] = { "BTC/USDT", 100, 101, 100.5, 1000, 0 };

    detector.update_prices(comparison);
    ASSERT_FALSE(opportunity_found);
}

TEST(OpportunityDetectorTest, OpportunityFoundWhenBidIsHigherThanAsk) {
    ats::OpportunityDetector detector({"BTC/USDT"});
    bool opportunity_found = false;
    detector.set_opportunity_callback([&](const ats::ArbitrageOpportunity& opp) {
        opportunity_found = true;
        ASSERT_EQ(opp.symbol, "BTC/USDT");
        ASSERT_EQ(opp.buy_exchange, "exchange2");
        ASSERT_EQ(opp.sell_exchange, "exchange1");
        ASSERT_EQ(opp.buy_price, 100);
        ASSERT_EQ(opp.sell_price, 101);
    });

    ats::PriceComparison comparison;
    comparison.symbol = "BTC/USDT";
    comparison.exchange_prices["exchange1"] = { "BTC/USDT", 101, 102, 101.5, 1000, 0 };
    comparison.exchange_prices["exchange2"] = { "BTC/USDT", 99, 100, 99.5, 1000, 0 };

    detector.update_prices(comparison);
    ASSERT_TRUE(opportunity_found);
}
