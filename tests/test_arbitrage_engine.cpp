#include <gtest/gtest.h>
#include "core/arbitrage_engine.hpp"
#include "mocks/mock_risk_manager.hpp"
#include "mocks/mock_trade_executor.hpp"

using ::testing::_;
using ::testing::Return;

TEST(ArbitrageEngineTest, OpportunityIsExecutedWhenRiskManagerApproves) {
    ats::testing::MockRiskManager risk_manager;
    ats::testing::MockTradeExecutor trade_executor;
    ats::ArbitrageEngine engine(&risk_manager, &trade_executor);

    ats::ArbitrageOpportunity opportunity;
    EXPECT_CALL(risk_manager, IsTradeAllowed(_)).WillOnce(Return(true));
    EXPECT_CALL(trade_executor, execute_trade(_)).Times(1);

    engine.evaluate_opportunity(opportunity);
}

TEST(ArbitrageEngineTest, OpportunityIsNotExecutedWhenRiskManagerDenies) {
    ats::testing::MockRiskManager risk_manager;
    ats::testing::MockTradeExecutor trade_executor;
    ats::ArbitrageEngine engine(&risk_manager, &trade_executor);

    ats::ArbitrageOpportunity opportunity;
    EXPECT_CALL(risk_manager, IsTradeAllowed(_)).WillOnce(Return(false));
    EXPECT_CALL(trade_executor, execute_trade(_)).Times(0);

    engine.evaluate_opportunity(opportunity);
}
