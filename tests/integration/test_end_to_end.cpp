#include <gtest/gtest.h>
#include <gmock/gmock.h>
#include <chrono>
#include <thread>

#include "core/arbitrage_engine.hpp"
#include "core/dependency_container.hpp"
#include "utils/config_manager.hpp"
#include "utils/structured_logger.hpp"
#include "monitoring/performance_monitor.hpp"
#include "mocks/mock_exchange.hpp"
#include "mocks/mock_risk_manager.hpp"
#include "mocks/mock_trade_executor.hpp"

using ::testing::_;
using ::testing::Return;
using ::testing::AtLeast;
using ::testing::InSequence;

namespace ats {
namespace integration {

class EndToEndTest : public ::testing::Test {
protected:
    void SetUp() override {
        // Initialize logging for tests
        StructuredLogger::init("test_logs/integration.log", LogLevel::DEBUG);
        
        // Clear container
        container.clear();
        
        // Setup mock exchanges
        binance_mock_ = std::make_shared<testing::MockExchange>();
        upbit_mock_ = std::make_shared<testing::MockExchange>();
        
        // Setup other mocks
        risk_manager_mock_ = std::make_shared<testing::MockRiskManager>();
        trade_executor_mock_ = std::make_shared<testing::MockTradeExecutor>();
        
        // Register in container
        container.register_instance<ExchangeInterface>(binance_mock_);
        container.register_instance<RiskManager>(risk_manager_mock_);
        container.register_instance<TradeExecutor>(trade_executor_mock_);
        
        // Reset performance monitor
        PerformanceMonitor::instance().reset_all_metrics();
    }
    
    void TearDown() override {
        container.clear();
        StructuredLogger::flush();
    }
    
    std::shared_ptr<testing::MockExchange> binance_mock_;
    std::shared_ptr<testing::MockExchange> upbit_mock_;
    std::shared_ptr<testing::MockRiskManager> risk_manager_mock_;
    std::shared_ptr<testing::MockTradeExecutor> trade_executor_mock_;
};

TEST_F(EndToEndTest, SuccessfulArbitrageFlow) {
    // Setup price difference between exchanges
    Price binance_price{"BTC/USDT", 45000.0, 45100.0, 45050.0, 1000.0, 1234567890};
    Price upbit_price{"BTC/USDT", 45200.0, 45300.0, 45250.0, 1000.0, 1234567890};
    
    EXPECT_CALL(*binance_mock_, get_name())
        .WillRepeatedly(Return("binance"));
    EXPECT_CALL(*upbit_mock_, get_name())
        .WillRepeatedly(Return("upbit"));
        
    EXPECT_CALL(*binance_mock_, get_price("BTC/USDT"))
        .WillRepeatedly(Return(binance_price));
    EXPECT_CALL(*upbit_mock_, get_price("BTC/USDT"))
        .WillRepeatedly(Return(upbit_price));
    
    // Risk manager approves the trade
    EXPECT_CALL(*risk_manager_mock_, IsTradeAllowed(_))
        .WillOnce(Return(true));
    
    // Trade executor executes successfully
    OrderResult success_result;
    success_result.order_id = "test_order_123";
    success_result.status = OrderStatus::FILLED;
    success_result.executed_quantity = 0.001;
    
    EXPECT_CALL(*trade_executor_mock_, execute_trade(_))
        .WillOnce(Return(success_result));
    
    // Create arbitrage opportunity
    ArbitrageOpportunity opportunity;
    opportunity.symbol = "BTC/USDT";
    opportunity.buy_exchange = "binance";
    opportunity.sell_exchange = "upbit";
    opportunity.buy_price = 45100.0;
    opportunity.sell_price = 45200.0;
    opportunity.volume = 0.001;
    opportunity.profit = 0.1; // $0.10 profit
    opportunity.is_executable = true;
    
    // Create and run arbitrage engine
    ArbitrageEngine engine(risk_manager_mock_.get(), trade_executor_mock_.get());
    
    // Process the opportunity
    engine.evaluate_opportunity(opportunity);
    
    // Verify metrics were recorded
    auto metrics = PerformanceMonitor::instance().get_trading_metrics();
    EXPECT_EQ(metrics.successful_trades.get(), 1);
    EXPECT_GT(metrics.arbitrage_opportunities.get(), 0);
}

TEST_F(EndToEndTest, RiskManagerRejectsHighRiskTrade) {
    // Create high-risk opportunity
    ArbitrageOpportunity risky_opportunity;
    risky_opportunity.symbol = "BTC/USDT";
    risky_opportunity.buy_exchange = "binance";
    risky_opportunity.sell_exchange = "upbit";
    risky_opportunity.volume = 10.0; // Large volume
    risky_opportunity.profit = 5000.0; // Unrealistic profit
    risky_opportunity.is_executable = true;
    
    // Risk manager rejects the trade
    EXPECT_CALL(*risk_manager_mock_, IsTradeAllowed(_))
        .WillOnce(Return(false));
    
    // Trade executor should not be called
    EXPECT_CALL(*trade_executor_mock_, execute_trade(_))
        .Times(0);
    
    ArbitrageEngine engine(risk_manager_mock_.get(), trade_executor_mock_.get());
    engine.evaluate_opportunity(risky_opportunity);
    
    // Verify no trades were executed
    auto metrics = PerformanceMonitor::instance().get_trading_metrics();
    EXPECT_EQ(metrics.successful_trades.get(), 0);
    EXPECT_GT(metrics.risk_violations.get(), 0);
}

TEST_F(EndToEndTest, TradeExecutionFailure) {
    ArbitrageOpportunity opportunity;
    opportunity.symbol = "BTC/USDT";
    opportunity.buy_exchange = "binance";
    opportunity.sell_exchange = "upbit";
    opportunity.volume = 0.001;
    opportunity.profit = 0.1;
    opportunity.is_executable = true;
    
    // Risk manager approves
    EXPECT_CALL(*risk_manager_mock_, IsTradeAllowed(_))
        .WillOnce(Return(true));
    
    // Trade execution fails
    OrderResult failed_result;
    failed_result.order_id = "";
    failed_result.status = OrderStatus::REJECTED;
    failed_result.executed_quantity = 0.0;
    
    EXPECT_CALL(*trade_executor_mock_, execute_trade(_))
        .WillOnce(Return(failed_result));
    
    ArbitrageEngine engine(risk_manager_mock_.get(), trade_executor_mock_.get());
    engine.evaluate_opportunity(opportunity);
    
    // Verify failure was recorded
    auto metrics = PerformanceMonitor::instance().get_trading_metrics();
    EXPECT_EQ(metrics.failed_trades.get(), 1);
    EXPECT_EQ(metrics.successful_trades.get(), 0);
}

TEST_F(EndToEndTest, MultipleOpportunitiesProcessing) {
    const int num_opportunities = 5;
    
    // Setup expectations for multiple opportunities
    EXPECT_CALL(*risk_manager_mock_, IsTradeAllowed(_))
        .Times(num_opportunities)
        .WillRepeatedly(Return(true));
    
    OrderResult success_result;
    success_result.order_id = "test_order";
    success_result.status = OrderStatus::FILLED;
    success_result.executed_quantity = 0.001;
    
    EXPECT_CALL(*trade_executor_mock_, execute_trade(_))
        .Times(num_opportunities)
        .WillRepeatedly(Return(success_result));
    
    ArbitrageEngine engine(risk_manager_mock_.get(), trade_executor_mock_.get());
    
    // Process multiple opportunities
    for (int i = 0; i < num_opportunities; ++i) {
        ArbitrageOpportunity opportunity;
        opportunity.symbol = "BTC/USDT";
        opportunity.buy_exchange = "binance";
        opportunity.sell_exchange = "upbit";
        opportunity.volume = 0.001;
        opportunity.profit = 0.1 * (i + 1); // Varying profits
        opportunity.is_executable = true;
        
        engine.evaluate_opportunity(opportunity);
    }
    
    // Verify all opportunities were processed
    auto metrics = PerformanceMonitor::instance().get_trading_metrics();
    EXPECT_EQ(metrics.successful_trades.get(), num_opportunities);
    EXPECT_EQ(metrics.arbitrage_opportunities.get(), num_opportunities);
}

TEST_F(EndToEndTest, PerformanceMonitoringIntegration) {
    // Test that performance monitoring correctly tracks system health
    
    // Record some system metrics
    PerformanceMonitor::instance().record_cpu_usage(45.0);
    PerformanceMonitor::instance().record_memory_usage(1024.0);
    PerformanceMonitor::instance().record_network_latency("binance", 150.0);
    PerformanceMonitor::instance().update_heartbeat();
    
    // System should be healthy
    EXPECT_TRUE(PerformanceMonitor::instance().is_system_healthy());
    
    // Record high CPU usage
    PerformanceMonitor::instance().record_cpu_usage(95.0);
    
    // System should still be healthy (average is still low)
    EXPECT_TRUE(PerformanceMonitor::instance().is_system_healthy());
    
    // Get metrics JSON
    auto metrics_json = PerformanceMonitor::instance().get_metrics_json();
    EXPECT_TRUE(metrics_json.contains("system"));
    EXPECT_TRUE(metrics_json.contains("trading"));
    EXPECT_TRUE(metrics_json["system"]["is_healthy"].is_boolean());
}

TEST_F(EndToEndTest, ConfigurationValidationIntegration) {
    // Test configuration validation with invalid config
    nlohmann::json invalid_config = {
        {"app", {
            {"name", ""},  // Invalid: empty name
            {"version", "1.0.0"}
        }},
        {"exchanges", {
            {"binance", {
                {"name", "binance"},
                {"enabled", true},
                {"base_url", "not_a_valid_url"},  // Invalid URL
                {"rate_limit_per_second", -5}    // Invalid: negative value
            }}
        }},
        {"trading", {
            {"pairs", {}},  // Invalid: empty array
            {"base_currency", "USDT"}
        }}
    };
    
    auto result = ConfigValidator::validate_config(invalid_config);
    EXPECT_TRUE(result.is_error());
    
    auto errors = ConfigValidator::get_errors();
    EXPECT_FALSE(errors.empty());
    
    // Check that specific errors were caught
    bool found_name_error = false;
    bool found_url_error = false;
    bool found_rate_limit_error = false;
    bool found_pairs_error = false;
    
    for (const auto& error : errors) {
        if (error.field == "name") found_name_error = true;
        if (error.field == "base_url") found_url_error = true;
        if (error.field == "rate_limit_per_second") found_rate_limit_error = true;
        if (error.field == "pairs") found_pairs_error = true;
    }
    
    EXPECT_TRUE(found_name_error);
    EXPECT_TRUE(found_url_error);
    EXPECT_TRUE(found_rate_limit_error);
    EXPECT_TRUE(found_pairs_error);
}

} // namespace integration
} // namespace ats