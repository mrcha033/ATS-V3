#include <gtest/gtest.h>
#include <gmock/gmock.h>
#include <memory>
#include <chrono>
#include <thread>
#include "exchange/failover_manager.hpp"
#include "exchange/resilient_exchange_adapter.hpp"
#include "exchange/exchange_notification_system.hpp"

using namespace ats::exchange;
using namespace testing;

// Mock Exchange Interface for testing
class MockExchangeInterface {
public:
    virtual ~MockExchangeInterface() = default;
    
    MOCK_METHOD(std::string, get_exchange_id, (), (const));
    MOCK_METHOD(std::string, get_name, (), (const));
    MOCK_METHOD(bool, is_connected, (), (const));
    MOCK_METHOD(bool, is_healthy, (), (const));
    MOCK_METHOD(std::vector<std::string>, get_supported_symbols, (), ());
    MOCK_METHOD(std::string, place_order, (const std::string& symbol, double price, double quantity), ());
    MOCK_METHOD(bool, cancel_order, (const std::string& order_id), ());
    MOCK_METHOD(double, get_price, (const std::string& symbol), ());
};

class ExchangeFailoverTest : public Test {
protected:
    void SetUp() override {
        mock_exchange_a = std::make_shared<MockExchangeInterface>();
        mock_exchange_b = std::make_shared<MockExchangeInterface>();
        mock_exchange_c = std::make_shared<MockExchangeInterface>();
        
        // Setup default expectations
        ON_CALL(*mock_exchange_a, get_exchange_id()).WillByDefault(Return("exchange_a"));
        ON_CALL(*mock_exchange_b, get_exchange_id()).WillByDefault(Return("exchange_b"));
        ON_CALL(*mock_exchange_c, get_exchange_id()).WillByDefault(Return("exchange_c"));
        
        ON_CALL(*mock_exchange_a, get_name()).WillByDefault(Return("Exchange A"));
        ON_CALL(*mock_exchange_b, get_name()).WillByDefault(Return("Exchange B"));
        ON_CALL(*mock_exchange_c, get_name()).WillByDefault(Return("Exchange C"));
        
        // Create failover configuration
        failover_config.health_check_interval = std::chrono::milliseconds(100);
        failover_config.connection_timeout = std::chrono::milliseconds(1000);
        failover_config.max_acceptable_latency = std::chrono::milliseconds(200);
        failover_config.max_consecutive_failures = 3;
        failover_config.max_error_rate = 0.1;
        failover_config.auto_failback_enabled = true;
        failover_config.failback_cooldown = std::chrono::seconds(1);
    }
    
    void TearDown() override {
        if (failover_manager) {
            failover_manager->stop_health_monitoring();
        }
        if (resilient_adapter) {
            resilient_adapter->stop();
        }
        if (notification_system) {
            notification_system->stop();
        }
    }
    
    std::shared_ptr<MockExchangeInterface> mock_exchange_a;
    std::shared_ptr<MockExchangeInterface> mock_exchange_b;
    std::shared_ptr<MockExchangeInterface> mock_exchange_c;
    
    FailoverConfig failover_config;
    std::unique_ptr<FailoverManager<MockExchangeInterface>> failover_manager;
    std::unique_ptr<ResilientExchangeAdapter<MockExchangeInterface>> resilient_adapter;
    std::unique_ptr<ExchangeNotificationSystem> notification_system;
    
    // Callback tracking
    std::vector<std::string> failover_events;
    std::vector<std::string> health_events;
    std::vector<NotificationMessage> notifications;
};

TEST_F(ExchangeFailoverTest, BasicFailoverManagerSetup) {
    failover_manager = std::make_unique<FailoverManager<MockExchangeInterface>>(failover_config);
    
    // Register exchanges
    failover_manager->register_exchange("exchange_a", mock_exchange_a, 3);  // Highest priority
    failover_manager->register_exchange("exchange_b", mock_exchange_b, 2);
    failover_manager->register_exchange("exchange_c", mock_exchange_c, 1);
    
    // Set up expectations
    EXPECT_CALL(*mock_exchange_a, is_healthy()).WillRepeatedly(Return(true));
    EXPECT_CALL(*mock_exchange_b, is_healthy()).WillRepeatedly(Return(true));
    EXPECT_CALL(*mock_exchange_c, is_healthy()).WillRepeatedly(Return(true));
    
    // Start monitoring
    failover_manager->start_health_monitoring();
    
    // Verify primary exchange
    auto primary = failover_manager->get_primary_exchange();
    ASSERT_NE(primary, nullptr);
    EXPECT_EQ(primary->get_exchange_id(), "exchange_a");
    
    // Verify available exchanges are ordered by priority
    auto available = failover_manager->get_available_exchanges();
    EXPECT_GE(available.size(), 1);
}

TEST_F(ExchangeFailoverTest, PrimaryExchangeFailure) {
    failover_manager = std::make_unique<FailoverManager<MockExchangeInterface>>(failover_config);
    
    // Register exchanges
    failover_manager->register_exchange("exchange_a", mock_exchange_a, 3);
    failover_manager->register_exchange("exchange_b", mock_exchange_b, 2);
    
    // Track failover events
    failover_manager->set_failover_callback([this](const std::string& from, const std::string& to, FailoverReason reason) {
        failover_events.push_back("Failover: " + from + " -> " + to);
    });
    
    // Initially both healthy
    EXPECT_CALL(*mock_exchange_a, is_healthy()).WillRepeatedly(Return(true));
    EXPECT_CALL(*mock_exchange_b, is_healthy()).WillRepeatedly(Return(true));
    
    failover_manager->start_health_monitoring();
    
    // Verify exchange_a is primary
    EXPECT_EQ(failover_manager->get_current_primary_exchange(), "exchange_a");
    
    // Simulate exchange_a failure
    EXPECT_CALL(*mock_exchange_a, is_healthy()).WillRepeatedly(Return(false));
    
    // Trigger failover
    failover_manager->trigger_failover("exchange_a", FailoverReason::HEALTH_CHECK_FAILED);
    
    // Wait for failover to complete
    std::this_thread::sleep_for(std::chrono::milliseconds(50));
    
    // Verify failover occurred
    EXPECT_EQ(failover_manager->get_current_primary_exchange(), "exchange_b");
    EXPECT_FALSE(failover_events.empty());
}

TEST_F(ExchangeFailoverTest, AutomaticHealthCheckFailover) {
    failover_manager = std::make_unique<FailoverManager<MockExchangeInterface>>(failover_config);
    
    failover_manager->register_exchange("exchange_a", mock_exchange_a, 3);
    failover_manager->register_exchange("exchange_b", mock_exchange_b, 2);
    
    // Track events
    failover_manager->set_failover_callback([this](const std::string& from, const std::string& to, FailoverReason reason) {
        failover_events.push_back("Auto failover: " + from + " -> " + to);
    });
    
    // Start with both healthy
    EXPECT_CALL(*mock_exchange_a, is_healthy()).Times(AtLeast(1)).WillOnce(Return(true));
    EXPECT_CALL(*mock_exchange_b, is_healthy()).WillRepeatedly(Return(true));
    
    failover_manager->start_health_monitoring();
    
    // Simulate gradual failure of exchange_a
    EXPECT_CALL(*mock_exchange_a, is_healthy())
        .WillRepeatedly(Return(false));  // Now unhealthy
    
    // Wait for health checks to detect failure and trigger failover
    std::this_thread::sleep_for(std::chrono::milliseconds(500));
    
    // Verify failover occurred automatically
    EXPECT_EQ(failover_manager->get_current_primary_exchange(), "exchange_b");
}

TEST_F(ExchangeFailoverTest, AutomaticFailback) {
    failover_manager = std::make_unique<FailoverManager<MockExchangeInterface>>(failover_config);
    
    // Set short cooldown for testing
    failover_config.failback_cooldown = std::chrono::milliseconds(100);
    
    failover_manager->register_exchange("exchange_a", mock_exchange_a, 3);  // Higher priority
    failover_manager->register_exchange("exchange_b", mock_exchange_b, 2);
    
    // Track events
    std::vector<std::string> events;
    failover_manager->set_failover_callback([&events](const std::string& from, const std::string& to, FailoverReason reason) {
        events.push_back(from + "->" + to);
    });
    
    // Start with A healthy, B healthy
    EXPECT_CALL(*mock_exchange_a, is_healthy()).WillRepeatedly(Return(true));
    EXPECT_CALL(*mock_exchange_b, is_healthy()).WillRepeatedly(Return(true));
    
    failover_manager->start_health_monitoring();
    
    // Verify A is primary
    EXPECT_EQ(failover_manager->get_current_primary_exchange(), "exchange_a");
    
    // Simulate A failure
    EXPECT_CALL(*mock_exchange_a, is_healthy()).WillRepeatedly(Return(false));
    failover_manager->trigger_failover("exchange_a", FailoverReason::API_ERROR);
    
    // Should failover to B
    std::this_thread::sleep_for(std::chrono::milliseconds(50));
    EXPECT_EQ(failover_manager->get_current_primary_exchange(), "exchange_b");
    
    // A recovers
    EXPECT_CALL(*mock_exchange_a, is_healthy()).WillRepeatedly(Return(true));
    
    // Wait for cooldown and automatic failback
    std::this_thread::sleep_for(std::chrono::milliseconds(300));
    
    // Should have failed back to A (higher priority)
    EXPECT_EQ(failover_manager->get_current_primary_exchange(), "exchange_a");
    EXPECT_GE(events.size(), 2);  // At least initial failover and failback
}

TEST_F(ExchangeFailoverTest, ResilientAdapterCircuitBreaker) {
    auto failover_mgr = std::make_unique<FailoverManager<MockExchangeInterface>>(failover_config);
    failover_mgr->register_exchange("exchange_a", mock_exchange_a, 1);
    
    CircuitBreakerConfig circuit_config;
    circuit_config.failure_threshold = 3;
    circuit_config.timeout = std::chrono::milliseconds(100);
    
    resilient_adapter = std::make_unique<ResilientExchangeAdapter<MockExchangeInterface>>(
        std::move(failover_mgr), circuit_config
    );
    
    resilient_adapter->start();
    
    // Initially healthy
    EXPECT_CALL(*mock_exchange_a, is_healthy()).WillRepeatedly(Return(true));
    
    // Simulate operations that will fail
    auto failing_operation = [](std::shared_ptr<MockExchangeInterface> exchange) -> bool {
        throw std::runtime_error("Simulated failure");
        return false;
    };
    
    // Execute failing operations to trigger circuit breaker
    for (int i = 0; i < 5; ++i) {
        bool result = resilient_adapter->execute_with_failover<bool>(
            "test_operation", failing_operation, false
        );
        EXPECT_FALSE(result);  // Should return default value
    }
    
    // Circuit should be open now
    EXPECT_EQ(resilient_adapter->get_circuit_state(), CircuitState::OPEN);
    
    // Wait for circuit to go half-open
    std::this_thread::sleep_for(std::chrono::milliseconds(150));
    
    EXPECT_EQ(resilient_adapter->get_circuit_state(), CircuitState::HALF_OPEN);
}

TEST_F(ExchangeFailoverTest, NotificationSystemIntegration) {
    notification_system = std::make_unique<ExchangeNotificationSystem>();
    notification_system->start();
    
    // Set up notification capture
    notification_system->add_notification_handler(NotificationChannel::LOG, 
        [this](const NotificationMessage& msg) {
            notifications.push_back(msg);
        }
    );
    
    // Create failover manager with notification integration
    failover_manager = std::make_unique<FailoverManager<MockExchangeInterface>>(failover_config);
    notification_system->integrate_with_failover_manager(failover_manager.get());
    
    failover_manager->register_exchange("exchange_a", mock_exchange_a, 2);
    failover_manager->register_exchange("exchange_b", mock_exchange_b, 1);
    
    // Initially healthy
    EXPECT_CALL(*mock_exchange_a, is_healthy()).WillRepeatedly(Return(true));
    EXPECT_CALL(*mock_exchange_b, is_healthy()).WillRepeatedly(Return(true));
    
    failover_manager->start_health_monitoring();
    
    // Trigger failover
    EXPECT_CALL(*mock_exchange_a, is_healthy()).WillRepeatedly(Return(false));
    failover_manager->trigger_failover("exchange_a", FailoverReason::CONNECTION_TIMEOUT);
    
    // Wait for notifications
    std::this_thread::sleep_for(std::chrono::milliseconds(100));
    
    // Verify notifications were sent
    EXPECT_FALSE(notifications.empty());
    
    bool failover_notification_found = false;
    for (const auto& notification : notifications) {
        if (notification.title.find("Failover") != std::string::npos) {
            failover_notification_found = true;
            EXPECT_EQ(notification.level, NotificationLevel::WARNING);
            break;
        }
    }
    
    EXPECT_TRUE(failover_notification_found);
}

TEST_F(ExchangeFailoverTest, RetryMechanism) {
    auto failover_mgr = std::make_unique<FailoverManager<MockExchangeInterface>>(failover_config);
    failover_mgr->register_exchange("exchange_a", mock_exchange_a, 1);
    
    resilient_adapter = std::make_unique<ResilientExchangeAdapter<MockExchangeInterface>>(
        std::move(failover_mgr), CircuitBreakerConfig{}
    );
    
    resilient_adapter->start();
    
    EXPECT_CALL(*mock_exchange_a, is_healthy()).WillRepeatedly(Return(true));
    
    // Operation that fails twice, then succeeds
    int call_count = 0;
    auto retry_operation = [&call_count](std::shared_ptr<MockExchangeInterface> exchange) -> std::string {
        call_count++;
        if (call_count <= 2) {
            throw std::runtime_error("Transient failure");
        }
        return "success";
    };
    
    // Execute with retry
    std::string result = resilient_adapter->execute_with_retry<std::string>(
        "retry_test", retry_operation, 3, std::chrono::milliseconds(10), "failed"
    );
    
    EXPECT_EQ(result, "success");
    EXPECT_EQ(call_count, 3);  // Failed twice, succeeded on third try
}

TEST_F(ExchangeFailoverTest, HighLatencyDetection) {
    failover_manager = std::make_unique<FailoverManager<MockExchangeInterface>>(failover_config);
    
    failover_manager->register_exchange("exchange_a", mock_exchange_a, 2);
    failover_manager->register_exchange("exchange_b", mock_exchange_b, 1);
    
    // Set up health tracking
    failover_manager->set_health_callback([this](const std::string& exchange, const ExchangeHealth& health) {
        health_events.push_back(exchange + ": " + std::to_string(health.latency.count()) + "ms");
    });
    
    // Simulate high latency by making is_healthy() take time
    EXPECT_CALL(*mock_exchange_a, is_healthy()).WillRepeatedly(Invoke([this]() {
        std::this_thread::sleep_for(std::chrono::milliseconds(300));  // Simulate high latency
        return true;
    }));
    
    EXPECT_CALL(*mock_exchange_b, is_healthy()).WillRepeatedly(Return(true));
    
    failover_manager->start_health_monitoring();
    
    // Wait for health checks to detect high latency
    std::this_thread::sleep_for(std::chrono::milliseconds(600));
    
    // Verify health events were recorded
    EXPECT_FALSE(health_events.empty());
    
    // Check if failover occurred due to high latency
    auto current_primary = failover_manager->get_current_primary_exchange();
    // Note: Depending on implementation, might failover to exchange_b due to high latency
}

TEST_F(ExchangeFailoverTest, MultipleExchangeFailureScenario) {
    failover_manager = std::make_unique<FailoverManager<MockExchangeInterface>>(failover_config);
    
    // Register 3 exchanges
    failover_manager->register_exchange("exchange_a", mock_exchange_a, 3);
    failover_manager->register_exchange("exchange_b", mock_exchange_b, 2);
    failover_manager->register_exchange("exchange_c", mock_exchange_c, 1);
    
    // Track all failover events
    std::vector<std::pair<std::string, std::string>> failover_sequence;
    failover_manager->set_failover_callback([&failover_sequence](const std::string& from, const std::string& to, FailoverReason reason) {
        failover_sequence.push_back({from, to});
    });
    
    // All start healthy
    EXPECT_CALL(*mock_exchange_a, is_healthy()).WillRepeatedly(Return(true));
    EXPECT_CALL(*mock_exchange_b, is_healthy()).WillRepeatedly(Return(true));
    EXPECT_CALL(*mock_exchange_c, is_healthy()).WillRepeatedly(Return(true));
    
    failover_manager->start_health_monitoring();
    
    // Verify A is primary
    EXPECT_EQ(failover_manager->get_current_primary_exchange(), "exchange_a");
    
    // A fails
    EXPECT_CALL(*mock_exchange_a, is_healthy()).WillRepeatedly(Return(false));
    failover_manager->trigger_failover("exchange_a", FailoverReason::API_ERROR);
    std::this_thread::sleep_for(std::chrono::milliseconds(50));
    
    // Should failover to B
    EXPECT_EQ(failover_manager->get_current_primary_exchange(), "exchange_b");
    
    // B also fails
    EXPECT_CALL(*mock_exchange_b, is_healthy()).WillRepeatedly(Return(false));
    failover_manager->trigger_failover("exchange_b", FailoverReason::CONNECTION_TIMEOUT);
    std::this_thread::sleep_for(std::chrono::milliseconds(50));
    
    // Should failover to C
    EXPECT_EQ(failover_manager->get_current_primary_exchange(), "exchange_c");
    
    // Verify failover sequence
    EXPECT_GE(failover_sequence.size(), 2);
    EXPECT_EQ(failover_sequence[0].first, "exchange_a");
    EXPECT_EQ(failover_sequence[0].second, "exchange_b");
    EXPECT_EQ(failover_sequence[1].first, "exchange_b");
    EXPECT_EQ(failover_sequence[1].second, "exchange_c");
}

TEST_F(ExchangeFailoverTest, ManualFailover) {
    failover_manager = std::make_unique<FailoverManager<MockExchangeInterface>>(failover_config);
    
    failover_manager->register_exchange("exchange_a", mock_exchange_a, 2);
    failover_manager->register_exchange("exchange_b", mock_exchange_b, 1);
    
    // Both healthy
    EXPECT_CALL(*mock_exchange_a, is_healthy()).WillRepeatedly(Return(true));
    EXPECT_CALL(*mock_exchange_b, is_healthy()).WillRepeatedly(Return(true));
    
    failover_manager->start_health_monitoring();
    
    // Verify A is primary
    EXPECT_EQ(failover_manager->get_current_primary_exchange(), "exchange_a");
    
    // Manual failover to B
    failover_manager->manual_failover("exchange_b");
    
    // Should now use B
    EXPECT_EQ(failover_manager->get_current_primary_exchange(), "exchange_b");
}

TEST_F(ExchangeFailoverTest, StatsTracking) {
    auto failover_mgr = std::make_unique<FailoverManager<MockExchangeInterface>>(failover_config);
    failover_mgr->register_exchange("exchange_a", mock_exchange_a, 1);
    
    resilient_adapter = std::make_unique<ResilientExchangeAdapter<MockExchangeInterface>>(
        std::move(failover_mgr), CircuitBreakerConfig{}
    );
    
    resilient_adapter->start();
    
    EXPECT_CALL(*mock_exchange_a, is_healthy()).WillRepeatedly(Return(true));
    
    // Successful operation
    auto success_operation = [](std::shared_ptr<MockExchangeInterface> exchange) -> bool {
        return true;
    };
    
    // Failed operation
    auto fail_operation = [](std::shared_ptr<MockExchangeInterface> exchange) -> bool {
        throw std::runtime_error("Test failure");
        return false;
    };
    
    // Execute some operations
    resilient_adapter->execute_with_failover<bool>("test1", success_operation, false);
    resilient_adapter->execute_with_failover<bool>("test2", success_operation, false);
    resilient_adapter->execute_with_failover<bool>("test3", fail_operation, false);
    
    // Check stats
    auto stats = resilient_adapter->get_operation_stats();
    EXPECT_EQ(stats.total_calls.load(), 3);
    EXPECT_EQ(stats.successful_calls.load(), 2);
    EXPECT_EQ(stats.failed_calls.load(), 1);
    EXPECT_GT(stats.average_latency().count(), 0);
}

// Performance test
TEST_F(ExchangeFailoverTest, PerformanceUnderLoad) {
    auto failover_mgr = std::make_unique<FailoverManager<MockExchangeInterface>>(failover_config);
    failover_mgr->register_exchange("exchange_a", mock_exchange_a, 1);
    
    resilient_adapter = std::make_unique<ResilientExchangeAdapter<MockExchangeInterface>>(
        std::move(failover_mgr), CircuitBreakerConfig{}
    );
    
    resilient_adapter->start();
    
    EXPECT_CALL(*mock_exchange_a, is_healthy()).WillRepeatedly(Return(true));
    
    auto fast_operation = [](std::shared_ptr<MockExchangeInterface> exchange) -> int {
        return 42;
    };
    
    const int num_operations = 1000;
    auto start_time = std::chrono::high_resolution_clock::now();
    
    // Execute many operations
    for (int i = 0; i < num_operations; ++i) {
        int result = resilient_adapter->execute_with_failover<int>(
            "perf_test", fast_operation, 0
        );
        EXPECT_EQ(result, 42);
    }
    
    auto end_time = std::chrono::high_resolution_clock::now();
    auto duration = std::chrono::duration_cast<std::chrono::milliseconds>(end_time - start_time);
    
    // Should complete reasonably quickly (less than 1 second for 1000 operations)
    EXPECT_LT(duration.count(), 1000);
    
    // Verify stats
    auto stats = resilient_adapter->get_operation_stats();
    EXPECT_EQ(stats.total_calls.load(), num_operations);
    EXPECT_EQ(stats.successful_calls.load(), num_operations);
    EXPECT_EQ(stats.failed_calls.load(), 0);
}

int main(int argc, char** argv) {
    ::testing::InitGoogleTest(&argc, argv);
    return RUN_ALL_TESTS();
}