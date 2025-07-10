#include <gtest/gtest.h>
#include <gmock/gmock.h>
#include "trading_engine_service.hpp"
#include "order_router.hpp"
#include "spread_calculator.hpp"
#include "exchange_trading_adapter.hpp"
#include "rollback_manager.hpp"
#include "redis_subscriber.hpp"
#include "utils/logger.hpp"
#include "config/config_manager.hpp"
#include <chrono>
#include <thread>
#include <vector>
#include <memory>

using namespace ats::trading_engine;
using namespace ats::types;
using namespace testing;

class TradingEngineIntegrationTest : public Test {
protected:
    void SetUp() override {
        // Initialize configuration
        config_ = std::make_unique<config::ConfigManager>();
        config_->set_value("trading_engine.enabled", true);
        config_->set_value("trading_engine.min_spread_threshold", 0.005);
        config_->set_value("trading_engine.max_concurrent_trades", 5);
        config_->set_value("trading_engine.enable_paper_trading", true);
        config_->set_value("trading_engine.enable_rollback_on_failure", true);
        
        // Initialize components
        trading_engine_ = std::make_unique<TradingEngineService>();
        order_router_ = std::make_unique<OrderRouter>();
        spread_calculator_ = std::make_unique<SpreadCalculator>();
        rollback_manager_ = std::make_unique<EnhancedRollbackManager>();
        
        // Initialize Redis subscriber (mock for testing)
        redis_subscriber_ = std::make_unique<RedisSubscriber>();
        
        ASSERT_TRUE(trading_engine_->initialize(*config_));
        ASSERT_TRUE(order_router_->initialize(OrderRouterConfig{}));
        ASSERT_TRUE(spread_calculator_->initialize(*config_));
        ASSERT_TRUE(rollback_manager_->initialize(RollbackPolicy{}));
        
        // Add mock exchanges
        setup_mock_exchanges();
    }
    
    void TearDown() override {
        if (trading_engine_ && trading_engine_->is_running()) {
            trading_engine_->stop();
        }
    }
    
    void setup_mock_exchanges() {
        // Add Binance adapter
        auto binance_adapter = std::make_unique<BinanceTradingInterface>("test_key", "test_secret", true);
        order_router_->add_exchange("binance", std::move(binance_adapter));
        
        // Add Upbit adapter
        auto upbit_adapter = std::make_unique<UpbitTradingInterface>("test_access", "test_secret");
        order_router_->add_exchange("upbit", std::move(upbit_adapter));
        
        // Set up mock market data
        setup_mock_market_data();
    }
    
    void setup_mock_market_data() {
        // Add mock ticker data for BTC/USDT
        Ticker binance_ticker;
        binance_ticker.symbol = "BTC/USDT";
        binance_ticker.exchange = "binance";
        binance_ticker.bid = 49950.0;
        binance_ticker.ask = 50050.0;
        binance_ticker.last = 50000.0;
        binance_ticker.volume = 1000.0;
        binance_ticker.timestamp = std::chrono::system_clock::now();
        
        Ticker upbit_ticker;
        upbit_ticker.symbol = "BTC/USDT";
        upbit_ticker.exchange = "upbit";
        upbit_ticker.bid = 50150.0;  // Higher price for arbitrage opportunity
        upbit_ticker.ask = 50250.0;
        upbit_ticker.last = 50200.0;
        upbit_ticker.volume = 800.0;
        upbit_ticker.timestamp = std::chrono::system_clock::now();
        
        spread_calculator_->update_ticker(binance_ticker);
        spread_calculator_->update_ticker(upbit_ticker);
    }
    
    ArbitrageOpportunity create_test_opportunity() {
        ArbitrageOpportunity opportunity;
        opportunity.symbol = "BTC/USDT";
        opportunity.buy_exchange = "binance";
        opportunity.sell_exchange = "upbit";
        opportunity.buy_price = 50050.0;
        opportunity.sell_price = 50150.0;
        opportunity.available_quantity = 0.1;
        opportunity.spread_percentage = 0.2;  // 0.2%
        opportunity.expected_profit = 10.0;   // $10
        opportunity.confidence_score = 0.9;
        opportunity.detected_at = std::chrono::system_clock::now();
        opportunity.validity_window = std::chrono::milliseconds(5000);
        opportunity.max_position_size = 0.1;
        opportunity.estimated_slippage = 0.001;
        opportunity.total_fees = 5.0;
        opportunity.risk_approved = true;
        
        return opportunity;
    }
    
    std::unique_ptr<config::ConfigManager> config_;
    std::unique_ptr<TradingEngineService> trading_engine_;
    std::unique_ptr<OrderRouter> order_router_;
    std::unique_ptr<SpreadCalculator> spread_calculator_;
    std::unique_ptr<EnhancedRollbackManager> rollback_manager_;
    std::unique_ptr<RedisSubscriber> redis_subscriber_;
};

// Test basic trading engine lifecycle
TEST_F(TradingEngineIntegrationTest, EngineLifecycle) {
    EXPECT_FALSE(trading_engine_->is_running());
    
    ASSERT_TRUE(trading_engine_->start());
    EXPECT_TRUE(trading_engine_->is_running());
    
    trading_engine_->stop();
    EXPECT_FALSE(trading_engine_->is_running());
}

// Test arbitrage opportunity detection and execution
TEST_F(TradingEngineIntegrationTest, ArbitrageExecution) {
    ASSERT_TRUE(trading_engine_->start());
    
    // Create and execute arbitrage opportunity
    auto opportunity = create_test_opportunity();
    
    // Test opportunity validation
    EXPECT_TRUE(trading_engine_->execute_arbitrage(opportunity));
    
    // Wait for execution
    std::this_thread::sleep_for(std::chrono::milliseconds(100));
    
    // Check statistics
    auto stats = trading_engine_->get_statistics();
    EXPECT_GT(stats.total_opportunities_detected.load(), 0);
    
    // In paper trading mode, should show successful execution
    EXPECT_GT(stats.total_successful_trades.load(), 0);
    
    trading_engine_->stop();
}

// Test order execution success rate
TEST_F(TradingEngineIntegrationTest, OrderExecutionSuccessRate) {
    ASSERT_TRUE(trading_engine_->start());
    
    const int num_trades = 10;
    std::vector<std::string> trade_ids;
    
    // Execute multiple arbitrage opportunities
    for (int i = 0; i < num_trades; ++i) {
        auto opportunity = create_test_opportunity();
        opportunity.available_quantity = 0.01 * (i + 1); // Vary quantities
        
        if (trading_engine_->execute_arbitrage(opportunity)) {
            // In a real implementation, we'd get the trade ID
            trade_ids.push_back("TRADE_" + std::to_string(i));
        }
    }
    
    // Wait for all executions to complete
    std::this_thread::sleep_for(std::chrono::milliseconds(500));
    
    // Check success rate
    auto stats = trading_engine_->get_statistics();
    double success_rate = stats.success_rate.load();
    
    // In paper trading mode, success rate should be high (≥98%)
    EXPECT_GE(success_rate, 0.98);
    EXPECT_GT(stats.total_opportunities_executed.load(), 0);
    
    utils::Logger::info("Order execution success rate: {:.2f}%", success_rate * 100);
    
    trading_engine_->stop();
}

// Test rollback logic for failed orders
TEST_F(TradingEngineIntegrationTest, RollbackLogic) {
    ASSERT_TRUE(trading_engine_->start());
    
    // Create a scenario that should trigger rollback
    auto opportunity = create_test_opportunity();
    opportunity.available_quantity = 100.0; // Large quantity to potentially trigger failure
    opportunity.expected_profit = -10.0;    // Negative profit to trigger failure
    
    // Execute the problematic opportunity
    bool execution_attempted = trading_engine_->execute_arbitrage(opportunity);
    
    // Wait for execution and potential rollback
    std::this_thread::sleep_for(std::chrono::milliseconds(200));
    
    // Check rollback statistics
    auto stats = trading_engine_->get_statistics();
    
    // Should have some rollback activity if failure occurred
    if (stats.total_failed_trades.load() > 0) {
        EXPECT_GT(stats.total_rollbacks.load(), 0);
        utils::Logger::info("Rollbacks executed: {}", stats.total_rollbacks.load());
    }
    
    trading_engine_->stop();
}

// Test spread calculation accuracy
TEST_F(TradingEngineIntegrationTest, SpreadCalculation) {
    // Test spread analysis
    auto analysis = spread_calculator_->analyze_spread("BTC/USDT", "binance", "upbit", 0.1);
    
    EXPECT_EQ(analysis.symbol, "BTC/USDT");
    EXPECT_EQ(analysis.buy_exchange, "binance");
    EXPECT_EQ(analysis.sell_exchange, "upbit");
    EXPECT_GT(analysis.raw_spread, 0);
    EXPECT_GT(analysis.spread_percentage, 0);
    
    // Test opportunity detection
    auto opportunities = spread_calculator_->detect_arbitrage_opportunities(5.0); // $5 minimum profit
    
    EXPECT_GT(opportunities.size(), 0);
    for (const auto& opp : opportunities) {
        EXPECT_GT(opp.expected_profit, 5.0);
        EXPECT_TRUE(opp.spread_percentage > 0);
    }
    
    utils::Logger::info("Detected {} arbitrage opportunities", opportunities.size());
}

// Test fee and slippage calculations
TEST_F(TradingEngineIntegrationTest, FeeAndSlippageCalculation) {
    // Test trading fee calculation
    double binance_fee = spread_calculator_->calculate_trading_fee("binance", "BTC/USDT", 0.1, 50000.0, false);
    double upbit_fee = spread_calculator_->calculate_trading_fee("upbit", "BTC/USDT", 0.1, 50000.0, false);
    
    EXPECT_GT(binance_fee, 0);
    EXPECT_GT(upbit_fee, 0);
    
    // Test slippage estimation
    double buy_slippage = spread_calculator_->estimate_slippage("binance", "BTC/USDT", 0.1, OrderSide::BUY);
    double sell_slippage = spread_calculator_->estimate_slippage("upbit", "BTC/USDT", 0.1, OrderSide::SELL);
    
    EXPECT_GE(buy_slippage, 0);
    EXPECT_GE(sell_slippage, 0);
    
    // Test breakeven spread calculation
    double breakeven = spread_calculator_->calculate_breakeven_spread("binance", "upbit", "BTC/USDT", 0.1);
    EXPECT_GT(breakeven, 0);
    
    utils::Logger::info("Binance fee: ${:.2f}, Upbit fee: ${:.2f}", binance_fee, upbit_fee);
    utils::Logger::info("Buy slippage: {:.4f}, Sell slippage: {:.4f}", buy_slippage, sell_slippage);
    utils::Logger::info("Breakeven spread: ${:.2f}", breakeven);
}

// Test order router performance
TEST_F(TradingEngineIntegrationTest, OrderRouterPerformance) {
    OrderRouterConfig config;
    config.order_timeout = std::chrono::milliseconds(5000);
    config.execution_timeout = std::chrono::milliseconds(10000);
    config.max_retry_attempts = 3;
    order_router_->update_config(config);
    
    // Test single order placement
    Order test_order;
    test_order.symbol = "BTC/USDT";
    test_order.exchange = "binance";
    test_order.side = OrderSide::BUY;
    test_order.type = OrderType::MARKET;
    test_order.quantity = 0.01;
    test_order.timestamp = std::chrono::system_clock::now();
    
    auto start_time = std::chrono::high_resolution_clock::now();
    auto future = order_router_->place_order_async(test_order);
    
    auto result = future.get();
    auto end_time = std::chrono::high_resolution_clock::now();
    
    auto latency = std::chrono::duration_cast<std::chrono::milliseconds>(end_time - start_time);
    
    EXPECT_LT(latency.count(), 1000); // Should complete within 1 second
    
    // Test simultaneous arbitrage execution
    auto opportunity = create_test_opportunity();
    auto arb_future = order_router_->execute_arbitrage_orders_async(opportunity);
    
    start_time = std::chrono::high_resolution_clock::now();
    auto arb_result = arb_future.get();
    end_time = std::chrono::high_resolution_clock::now();
    
    latency = std::chrono::duration_cast<std::chrono::milliseconds>(end_time - start_time);
    
    EXPECT_LT(latency.count(), 2000); // Should complete within 2 seconds
    
    // Check performance metrics
    auto metrics = order_router_->get_performance_metrics();
    EXPECT_GT(metrics.total_orders_placed.load(), 0);
    
    utils::Logger::info("Order placement latency: {} ms", latency.count());
    utils::Logger::info("Orders placed: {}, Success rate: {:.2f}%", 
                       metrics.total_orders_placed.load(), 
                       metrics.success_rate.load() * 100);
}

// Test configuration updates
TEST_F(TradingEngineIntegrationTest, ConfigurationUpdates) {
    ASSERT_TRUE(trading_engine_->start());
    
    // Test configuration retrieval
    auto current_config = trading_engine_->get_config();
    EXPECT_TRUE(current_config.enabled);
    EXPECT_EQ(current_config.min_spread_threshold, 0.005);
    
    // Test configuration update
    TradingEngineConfig new_config = current_config;
    new_config.min_spread_threshold = 0.01; // Increase threshold
    new_config.max_concurrent_trades = 3;    // Reduce concurrent trades
    
    trading_engine_->update_config(new_config);
    
    // Verify configuration was updated
    auto updated_config = trading_engine_->get_config();
    EXPECT_EQ(updated_config.min_spread_threshold, 0.01);
    EXPECT_EQ(updated_config.max_concurrent_trades, 3);
    
    trading_engine_->stop();
}

// Test emergency stop functionality
TEST_F(TradingEngineIntegrationTest, EmergencyStop) {
    ASSERT_TRUE(trading_engine_->start());
    
    // Execute some trades first
    auto opportunity = create_test_opportunity();
    trading_engine_->execute_arbitrage(opportunity);
    
    std::this_thread::sleep_for(std::chrono::milliseconds(50));
    
    // Trigger emergency stop
    trading_engine_->emergency_stop();
    
    EXPECT_TRUE(trading_engine_->is_emergency_stopped());
    
    // Try to execute another trade - should fail
    EXPECT_FALSE(trading_engine_->execute_arbitrage(opportunity));
    
    trading_engine_->stop();
}

// Test health monitoring
TEST_F(TradingEngineIntegrationTest, HealthMonitoring) {
    ASSERT_TRUE(trading_engine_->start());
    
    // Initially should be healthy
    EXPECT_TRUE(trading_engine_->is_healthy());
    
    auto health_issues = trading_engine_->get_health_issues();
    EXPECT_TRUE(health_issues.empty());
    
    // Get status report
    auto status_report = trading_engine_->get_status_report();
    EXPECT_FALSE(status_report.empty());
    EXPECT_NE(status_report.find("Running: Yes"), std::string::npos);
    
    utils::Logger::info("Trading engine status:\n{}", status_report);
    
    trading_engine_->stop();
}

// Performance benchmark test
TEST_F(TradingEngineIntegrationTest, PerformanceBenchmark) {
    ASSERT_TRUE(trading_engine_->start());
    
    const int num_opportunities = 100;
    auto start_time = std::chrono::high_resolution_clock::now();
    
    // Execute many opportunities rapidly
    for (int i = 0; i < num_opportunities; ++i) {
        auto opportunity = create_test_opportunity();
        opportunity.available_quantity = 0.001 * (i % 10 + 1); // Vary quantities
        trading_engine_->execute_arbitrage(opportunity);
    }
    
    // Wait for all executions
    std::this_thread::sleep_for(std::chrono::milliseconds(1000));
    
    auto end_time = std::chrono::high_resolution_clock::now();
    auto total_time = std::chrono::duration_cast<std::chrono::milliseconds>(end_time - start_time);
    
    auto stats = trading_engine_->get_statistics();
    double throughput = static_cast<double>(stats.total_opportunities_executed.load()) / 
                       (total_time.count() / 1000.0);
    
    utils::Logger::info("Performance benchmark:");
    utils::Logger::info("- Opportunities processed: {}", stats.total_opportunities_executed.load());
    utils::Logger::info("- Total time: {} ms", total_time.count());
    utils::Logger::info("- Throughput: {:.2f} opportunities/second", throughput);
    utils::Logger::info("- Average execution time: {} ms", stats.average_execution_time.load().count());
    utils::Logger::info("- Success rate: {:.2f}%", stats.success_rate.load() * 100);
    
    // Performance expectations
    EXPECT_GT(throughput, 10.0); // At least 10 opportunities per second
    EXPECT_LT(stats.average_execution_time.load().count(), 500); // Average execution under 500ms
    
    trading_engine_->stop();
}

// Integration test with real market data simulation
TEST_F(TradingEngineIntegrationTest, MarketDataIntegration) {
    ASSERT_TRUE(trading_engine_->start());
    
    // Simulate incoming market data updates
    std::vector<std::thread> data_threads;
    std::atomic<bool> feeding_data{true};
    
    // Start market data feeding threads
    data_threads.emplace_back([this, &feeding_data]() {
        int update_count = 0;
        while (feeding_data && update_count < 50) {
            Ticker ticker;
            ticker.symbol = "BTC/USDT";
            ticker.exchange = "binance";
            ticker.bid = 49900.0 + (update_count % 100);
            ticker.ask = 50000.0 + (update_count % 100);
            ticker.last = 49950.0 + (update_count % 100);
            ticker.volume = 1000.0 + (update_count * 10);
            ticker.timestamp = std::chrono::system_clock::now();
            
            spread_calculator_->update_ticker(ticker);
            
            std::this_thread::sleep_for(std::chrono::milliseconds(100));
            update_count++;
        }
    });
    
    data_threads.emplace_back([this, &feeding_data]() {
        int update_count = 0;
        while (feeding_data && update_count < 50) {
            Ticker ticker;
            ticker.symbol = "BTC/USDT";
            ticker.exchange = "upbit";
            ticker.bid = 50100.0 + (update_count % 150);
            ticker.ask = 50200.0 + (update_count % 150);
            ticker.last = 50150.0 + (update_count % 150);
            ticker.volume = 800.0 + (update_count * 8);
            ticker.timestamp = std::chrono::system_clock::now();
            
            spread_calculator_->update_ticker(ticker);
            
            std::this_thread::sleep_for(std::chrono::milliseconds(120));
            update_count++;
        }
    });
    
    // Let the data feed for a while
    std::this_thread::sleep_for(std::chrono::seconds(5));
    
    feeding_data = false;
    
    // Wait for threads to complete
    for (auto& thread : data_threads) {
        if (thread.joinable()) {
            thread.join();
        }
    }
    
    // Check if opportunities were detected and executed
    auto stats = trading_engine_->get_statistics();
    auto spread_stats = spread_calculator_->get_opportunities_detected();
    
    utils::Logger::info("Market data integration results:");
    utils::Logger::info("- Spread opportunities detected: {}", spread_stats);
    utils::Logger::info("- Trading opportunities executed: {}", stats.total_opportunities_executed.load());
    utils::Logger::info("- Successful trades: {}", stats.total_successful_trades.load());
    
    // Should have detected some opportunities with varying market data
    EXPECT_GT(spread_stats, 0);
    
    trading_engine_->stop();
}

int main(int argc, char** argv) {
    testing::InitGoogleTest(&argc, argv);
    
    // Initialize logging for tests
    utils::Logger::init("debug");
    
    utils::Logger::info("Starting Trading Engine Integration Tests");
    
    int result = RUN_ALL_TESTS();
    
    utils::Logger::info("Trading Engine Integration Tests completed with result: {}", result);
    
    return result;
}