#include <gtest/gtest.h>
#include <gmock/gmock.h>
#include "price_collector_service.hpp"
#include "exchange_interface.hpp"
#include "market_data_storage.hpp"
#include "performance_monitor.hpp"
#include "config/config_manager.hpp"
#include "utils/logger.hpp"
#include <memory>
#include <thread>
#include <chrono>

using namespace ats;
using namespace ats::price_collector;
using namespace ats::types;

// Mock exchange adapter for testing
class MockExchangeAdapter : public ExchangeInterface {
public:
    MockExchangeAdapter(const std::string& exchange_id) : exchange_id_(exchange_id) {
        capabilities_.supports_rest_api = true;
        capabilities_.supports_websocket = true;
        capabilities_.supports_ticker_stream = true;
        capabilities_.rate_limit_per_minute = 1200;
    }
    
    std::string get_exchange_id() const override { return exchange_id_; }
    std::string get_exchange_name() const override { return exchange_id_ + "_exchange"; }
    ExchangeCapabilities get_capabilities() const override { return capabilities_; }
    
    bool initialize(const ExchangeConfig& config) override {
        config_ = config;
        return true;
    }
    
    bool connect() override {
        connected_ = true;
        if (connection_callback_) connection_callback_(exchange_id_, true);
        return true;
    }
    
    void disconnect() override {
        connected_ = false;
        if (connection_callback_) connection_callback_(exchange_id_, false);
    }
    
    ConnectionStatus get_connection_status() const override {
        return connected_ ? ConnectionStatus::CONNECTED : ConnectionStatus::DISCONNECTED;
    }
    
    bool is_connected() const override { return connected_; }
    
    bool subscribe_ticker(const std::string& symbol) override {
        subscribed_symbols_.insert(symbol);
        // Simulate ticker data
        if (ticker_callback_) {
            simulate_ticker_data(symbol);
        }
        return true;
    }
    
    bool subscribe_orderbook(const std::string& symbol, int depth = 20) override {
        subscribed_symbols_.insert(symbol);
        return true;
    }
    
    bool subscribe_trades(const std::string& symbol) override {
        subscribed_symbols_.insert(symbol);
        return true;
    }
    
    bool subscribe_multiple(const std::vector<SubscriptionRequest>& requests) override {
        for (const auto& req : requests) {
            subscribe_ticker(req.symbol);
        }
        return true;
    }
    
    bool unsubscribe_ticker(const std::string& symbol) override {
        subscribed_symbols_.erase(symbol);
        return true;
    }
    
    bool unsubscribe_orderbook(const std::string& symbol) override {
        subscribed_symbols_.erase(symbol);
        return true;
    }
    
    bool unsubscribe_trades(const std::string& symbol) override {
        subscribed_symbols_.erase(symbol);
        return true;
    }
    
    bool unsubscribe_all() override {
        subscribed_symbols_.clear();
        return true;
    }
    
    std::vector<Ticker> get_all_tickers() override {
        std::vector<Ticker> tickers;
        for (const std::string& symbol : {"BTC/USDT", "ETH/USDT", "BNB/USDT"}) {
            tickers.push_back(create_mock_ticker(symbol));
        }
        return tickers;
    }
    
    Ticker get_ticker(const std::string& symbol) override {
        return create_mock_ticker(symbol);
    }
    
    std::vector<std::string> get_supported_symbols() override {
        return {"BTC/USDT", "ETH/USDT", "BNB/USDT", "ADA/USDT", "SOL/USDT"};
    }
    
    void set_ticker_callback(TickerCallback callback) override {
        ticker_callback_ = callback;
    }
    
    void set_orderbook_callback(OrderBookCallback callback) override {
        orderbook_callback_ = callback;
    }
    
    void set_trade_callback(TradeCallback callback) override {
        trade_callback_ = callback;
    }
    
    void set_connection_status_callback(ConnectionStatusCallback callback) override {
        connection_callback_ = callback;
    }
    
    size_t get_messages_received() const override { return messages_received_; }
    size_t get_messages_per_second() const override { return 10; }
    std::chrono::milliseconds get_average_latency() const override { return std::chrono::milliseconds(50); }
    std::chrono::milliseconds get_last_message_time() const override { return std::chrono::milliseconds(0); }
    size_t get_subscribed_symbols_count() const override { return subscribed_symbols_.size(); }
    
    std::string get_last_error() const override { return last_error_; }
    void clear_error() override { last_error_.clear(); }
    
    bool can_make_request() const override { return true; }
    void record_request() override { ++requests_made_; }
    std::chrono::milliseconds get_next_request_delay() const override { return std::chrono::milliseconds(0); }
    
    // Mock-specific methods
    void simulate_ticker_data(const std::string& symbol) {
        if (ticker_callback_) {
            ticker_callback_(create_mock_ticker(symbol));
        }
        ++messages_received_;
    }
    
    void simulate_error(const std::string& error) {
        last_error_ = error;
    }
    
    size_t get_requests_made() const { return requests_made_; }
    
protected:
    void notify_ticker_update(const Ticker& ticker) override {
        if (ticker_callback_) ticker_callback_(ticker);
    }
    
    void notify_connection_status_change(bool connected) override {
        if (connection_callback_) connection_callback_(exchange_id_, connected);
    }
    
    void handle_error(const std::string& error_message) override {
        last_error_ = error_message;
    }
    
private:
    std::string exchange_id_;
    ExchangeConfig config_;
    ExchangeCapabilities capabilities_;
    bool connected_ = false;
    std::unordered_set<std::string> subscribed_symbols_;
    std::atomic<size_t> messages_received_{0};
    std::atomic<size_t> requests_made_{0};
    std::string last_error_;
    
    TickerCallback ticker_callback_;
    OrderBookCallback orderbook_callback_;
    TradeCallback trade_callback_;
    ConnectionStatusCallback connection_callback_;
    
    Ticker create_mock_ticker(const std::string& symbol) {
        Ticker ticker;
        ticker.symbol = symbol;
        ticker.exchange = exchange_id_;
        ticker.bid = 50000.0 + (rand() % 1000);
        ticker.ask = ticker.bid + 10.0;
        ticker.last = ticker.bid + 5.0;
        ticker.volume_24h = 1000.0 + (rand() % 5000);
        ticker.timestamp = std::chrono::system_clock::now();
        return ticker;
    }
};

// Test fixtures
class PriceCollectorServiceTest : public ::testing::Test {
protected:
    void SetUp() override {
        utils::Logger::initialize("test_logs/price_collector_test.log", utils::LogLevel::DEBUG);
        
        // Create service
        service_ = std::make_unique<PriceCollectorService>();
        
        // Create mock config
        config_manager_ = std::make_unique<config::ConfigManager>();
        setup_test_config();
    }
    
    void TearDown() override {
        if (service_ && service_->is_running()) {
            service_->stop();
        }
        utils::Logger::shutdown();
    }
    
    void setup_test_config() {
        // Set up test configuration
        config_manager_->set_value("price_collector.enable_redis_publishing", false);
        config_manager_->set_value("price_collector.enable_influxdb_storage", false);
        config_manager_->set_value("price_collector.enable_local_storage", true);
        config_manager_->set_value("price_collector.max_queue_size", 1000);
        config_manager_->set_value("price_collector.worker_thread_count", 2);
        
        // Set up exchange configs
        nlohmann::json exchanges = nlohmann::json::array();
        nlohmann::json binance_config = {
            {"id", "binance"},
            {"name", "Binance Test"},
            {"api_key", "test_key"},
            {"secret_key", "test_secret"},
            {"sandbox_mode", true},
            {"rate_limit", 1200},
            {"timeout_ms", 5000}
        };
        exchanges.push_back(binance_config);
        config_manager_->set_value("exchanges", exchanges);
    }
    
    std::unique_ptr<PriceCollectorService> service_;
    std::unique_ptr<config::ConfigManager> config_manager_;
};

class MemoryBufferTest : public ::testing::Test {
protected:
    void SetUp() override {
        buffer_ = std::make_unique<MemoryBuffer>(100);
    }
    
    std::unique_ptr<MemoryBuffer> buffer_;
};

class PerformanceMonitorTest : public ::testing::Test {
protected:
    void SetUp() override {
        monitor_ = std::make_unique<PerformanceMonitor>();
        ASSERT_TRUE(monitor_->start());
    }
    
    void TearDown() override {
        if (monitor_->is_running()) {
            monitor_->stop();
        }
    }
    
    std::unique_ptr<PerformanceMonitor> monitor_;
};

// PriceCollectorService Tests
TEST_F(PriceCollectorServiceTest, InitializationAndStartup) {
    EXPECT_TRUE(service_->initialize(*config_manager_));
    EXPECT_TRUE(service_->start());
    EXPECT_TRUE(service_->is_running());
    
    service_->stop();
    EXPECT_FALSE(service_->is_running());
}

TEST_F(PriceCollectorServiceTest, ExchangeManagement) {
    EXPECT_TRUE(service_->initialize(*config_manager_));
    
    // Add mock exchange
    auto mock_exchange = std::make_unique<MockExchangeAdapter>("test_exchange");
    auto* mock_ptr = mock_exchange.get();
    
    EXPECT_TRUE(service_->add_exchange(std::move(mock_exchange)));
    
    auto connected_exchanges = service_->get_connected_exchanges();
    EXPECT_EQ(connected_exchanges.size(), 1);
    EXPECT_EQ(connected_exchanges[0], "test_exchange");
    
    // Test subscription
    EXPECT_TRUE(service_->subscribe_to_symbol("test_exchange", "BTC/USDT", true, false, false));
    EXPECT_EQ(mock_ptr->get_subscribed_symbols_count(), 1);
    
    // Test removal
    EXPECT_TRUE(service_->remove_exchange("test_exchange"));
    connected_exchanges = service_->get_connected_exchanges();
    EXPECT_EQ(connected_exchanges.size(), 0);
}

TEST_F(PriceCollectorServiceTest, DataCollection) {
    EXPECT_TRUE(service_->initialize(*config_manager_));
    EXPECT_TRUE(service_->start());
    
    // Add mock exchange
    auto mock_exchange = std::make_unique<MockExchangeAdapter>("test_exchange");
    auto* mock_ptr = mock_exchange.get();
    service_->add_exchange(std::move(mock_exchange));
    
    // Subscribe to symbols
    EXPECT_TRUE(service_->subscribe_to_symbol("test_exchange", "BTC/USDT"));
    EXPECT_TRUE(service_->subscribe_to_symbol("test_exchange", "ETH/USDT"));
    
    // Simulate some ticker data
    std::this_thread::sleep_for(std::chrono::milliseconds(100));
    mock_ptr->simulate_ticker_data("BTC/USDT");
    mock_ptr->simulate_ticker_data("ETH/USDT");
    
    // Wait for processing
    std::this_thread::sleep_for(std::chrono::milliseconds(200));
    
    // Check collected data
    auto latest_tickers = service_->get_latest_tickers();
    EXPECT_GE(latest_tickers.size(), 2);
    
    auto btc_ticker = service_->get_latest_ticker("test_exchange", "BTC/USDT");
    EXPECT_EQ(btc_ticker.symbol, "BTC/USDT");
    EXPECT_EQ(btc_ticker.exchange, "test_exchange");
    EXPECT_GT(btc_ticker.bid, 0);
}

TEST_F(PriceCollectorServiceTest, ErrorHandling) {
    EXPECT_TRUE(service_->initialize(*config_manager_));
    EXPECT_TRUE(service_->start());
    
    auto mock_exchange = std::make_unique<MockExchangeAdapter>("test_exchange");
    auto* mock_ptr = mock_exchange.get();
    service_->add_exchange(std::move(mock_exchange));
    
    // Simulate error
    mock_ptr->simulate_error("Connection timeout");
    EXPECT_FALSE(mock_ptr->get_last_error().empty());
    
    // Service should still be healthy despite exchange error
    EXPECT_TRUE(service_->is_running());
}

TEST_F(PriceCollectorServiceTest, MultipleExchanges) {
    EXPECT_TRUE(service_->initialize(*config_manager_));
    EXPECT_TRUE(service_->start());
    
    // Add multiple exchanges
    service_->add_exchange(std::make_unique<MockExchangeAdapter>("exchange1"));
    service_->add_exchange(std::make_unique<MockExchangeAdapter>("exchange2"));
    service_->add_exchange(std::make_unique<MockExchangeAdapter>("exchange3"));
    
    auto connected_exchanges = service_->get_connected_exchanges();
    EXPECT_EQ(connected_exchanges.size(), 3);
    
    // Subscribe to same symbol on different exchanges
    EXPECT_TRUE(service_->subscribe_to_symbol("exchange1", "BTC/USDT"));
    EXPECT_TRUE(service_->subscribe_to_symbol("exchange2", "BTC/USDT"));
    EXPECT_TRUE(service_->subscribe_to_symbol("exchange3", "BTC/USDT"));
    
    // Get market snapshot
    auto snapshot = service_->get_market_snapshot();
    EXPECT_EQ(snapshot.tickers.size(), 3);
}

// MemoryBuffer Tests
TEST_F(MemoryBufferTest, BasicOperations) {
    Ticker ticker;
    ticker.symbol = "BTC/USDT";
    ticker.exchange = "test_exchange";
    ticker.bid = 50000.0;
    ticker.ask = 50010.0;
    ticker.last = 50005.0;
    ticker.volume_24h = 1000.0;
    ticker.timestamp = std::chrono::system_clock::now();
    
    buffer_->add_ticker(ticker);
    EXPECT_EQ(buffer_->get_size(), 1);
    
    auto retrieved = buffer_->get_latest_ticker("test_exchange", "BTC/USDT");
    EXPECT_EQ(retrieved.symbol, "BTC/USDT");
    EXPECT_EQ(retrieved.bid, 50000.0);
}

TEST_F(MemoryBufferTest, MultipleTickersAndHistory) {
    std::vector<Ticker> tickers;
    auto now = std::chrono::system_clock::now();
    
    // Add multiple tickers with different timestamps
    for (int i = 0; i < 10; ++i) {
        Ticker ticker;
        ticker.symbol = "BTC/USDT";
        ticker.exchange = "test_exchange";
        ticker.bid = 50000.0 + i * 10;
        ticker.ask = ticker.bid + 10;
        ticker.last = ticker.bid + 5;
        ticker.volume_24h = 1000.0;
        ticker.timestamp = now - std::chrono::minutes(10 - i);
        
        tickers.push_back(ticker);
    }
    
    buffer_->add_tickers(tickers);
    EXPECT_EQ(buffer_->get_size(), 10);
    
    // Get latest ticker (should be the one with most recent timestamp)
    auto latest = buffer_->get_latest_ticker("test_exchange", "BTC/USDT");
    EXPECT_EQ(latest.bid, 50090.0); // Last ticker has bid = 50000 + 9*10
    
    // Get history from 5 minutes ago
    auto history = buffer_->get_ticker_history("test_exchange", "BTC/USDT", 
                                              now - std::chrono::minutes(5));
    EXPECT_GE(history.size(), 5); // Should get at least 5 recent tickers
}

TEST_F(MemoryBufferTest, BufferSizeLimit) {
    // Set small buffer size
    buffer_->set_max_size(5);
    
    // Add more tickers than buffer size
    for (int i = 0; i < 10; ++i) {
        Ticker ticker;
        ticker.symbol = "BTC/USDT";
        ticker.exchange = "test_exchange";
        ticker.bid = 50000.0 + i;
        ticker.timestamp = std::chrono::system_clock::now();
        
        buffer_->add_ticker(ticker);
    }
    
    // Buffer should not exceed max size
    EXPECT_LE(buffer_->get_size(), 5);
    EXPECT_EQ(buffer_->get_utilization(), 1.0); // 100% utilization
}

// PerformanceMonitor Tests
TEST_F(PerformanceMonitorTest, BasicMetricsRecording) {
    EXPECT_TRUE(monitor_->is_running());
    
    // Record some metrics
    monitor_->record_message_received("test_exchange");
    monitor_->record_message_processed("test_exchange");
    monitor_->record_processing_latency(std::chrono::milliseconds(10), "test_exchange");
    monitor_->record_network_latency(std::chrono::milliseconds(50), "test_exchange");
    
    // Wait for metrics to be processed
    std::this_thread::sleep_for(std::chrono::milliseconds(100));
    
    auto metrics = monitor_->get_current_metrics();
    EXPECT_GE(metrics.messages_received, 1);
    EXPECT_GE(metrics.messages_processed, 1);
    EXPECT_GT(metrics.avg_processing_latency, 0);
    EXPECT_GT(metrics.avg_network_latency, 0);
}

TEST_F(PerformanceMonitorTest, ErrorRecording) {
    monitor_->record_error("network_error", "test_exchange");
    monitor_->record_error("parsing_error", "test_exchange");
    monitor_->record_parsing_error("test_exchange");
    
    std::this_thread::sleep_for(std::chrono::milliseconds(100));
    
    auto metrics = monitor_->get_current_metrics();
    EXPECT_GE(metrics.total_errors, 3);
    EXPECT_GE(metrics.parsing_errors, 1);
}

TEST_F(PerformanceMonitorTest, ExchangeSpecificMetrics) {
    const std::string exchange1 = "exchange1";
    const std::string exchange2 = "exchange2";
    
    // Record metrics for different exchanges
    monitor_->record_message_received(exchange1);
    monitor_->record_message_received(exchange1);
    monitor_->record_message_received(exchange2);
    
    monitor_->record_processing_latency(std::chrono::milliseconds(10), exchange1);
    monitor_->record_processing_latency(std::chrono::milliseconds(20), exchange2);
    
    std::this_thread::sleep_for(std::chrono::milliseconds(100));
    
    auto exchange1_metrics = monitor_->get_exchange_metrics(exchange1);
    auto exchange2_metrics = monitor_->get_exchange_metrics(exchange2);
    
    EXPECT_TRUE(exchange1_metrics.contains("messages_received"));
    EXPECT_TRUE(exchange2_metrics.contains("messages_received"));
    
    // Exchange1 should have more messages
    EXPECT_GE(exchange1_metrics["messages_received"].get<size_t>(), 2);
    EXPECT_GE(exchange2_metrics["messages_received"].get<size_t>(), 1);
}

TEST_F(PerformanceMonitorTest, QueueMonitoring) {
    monitor_->update_queue_size(500, 1000); // 50% utilization
    
    std::this_thread::sleep_for(std::chrono::milliseconds(100));
    
    auto metrics = monitor_->get_current_metrics();
    EXPECT_EQ(metrics.queue_size, 500);
    EXPECT_DOUBLE_EQ(metrics.queue_utilization_percent, 50.0);
}

TEST_F(PerformanceMonitorTest, HealthCheck) {
    EXPECT_TRUE(monitor_->is_healthy());
    
    // Record many errors to trigger unhealthy state
    for (int i = 0; i < 100; ++i) {
        monitor_->record_error("test_error");
    }
    
    std::this_thread::sleep_for(std::chrono::milliseconds(200));
    
    // Depending on error rate threshold, monitor might become unhealthy
    auto health_issues = monitor_->get_health_issues();
    // At minimum, there should be some metrics about errors
    EXPECT_FALSE(health_issues.empty());
}

// Integration Test
TEST_F(PriceCollectorServiceTest, FullIntegrationTest) {
    // Initialize and start service
    EXPECT_TRUE(service_->initialize(*config_manager_));
    EXPECT_TRUE(service_->start());
    
    // Add multiple exchanges
    auto exchange1 = std::make_unique<MockExchangeAdapter>("binance");
    auto exchange2 = std::make_unique<MockExchangeAdapter>("upbit");
    auto* exchange1_ptr = exchange1.get();
    auto* exchange2_ptr = exchange2.get();
    
    EXPECT_TRUE(service_->add_exchange(std::move(exchange1)));
    EXPECT_TRUE(service_->add_exchange(std::move(exchange2)));
    
    // Subscribe to multiple symbols on both exchanges
    std::vector<std::string> symbols = {"BTC/USDT", "ETH/USDT", "BNB/USDT"};
    for (const auto& symbol : symbols) {
        EXPECT_TRUE(service_->subscribe_to_symbol("binance", symbol));
        EXPECT_TRUE(service_->subscribe_to_symbol("upbit", symbol));
    }
    
    // Simulate market data for 1 second
    auto start_time = std::chrono::steady_clock::now();
    while (std::chrono::steady_clock::now() - start_time < std::chrono::seconds(1)) {
        for (const auto& symbol : symbols) {
            exchange1_ptr->simulate_ticker_data(symbol);
            exchange2_ptr->simulate_ticker_data(symbol);
        }
        std::this_thread::sleep_for(std::chrono::milliseconds(50));
    }
    
    // Wait for processing
    std::this_thread::sleep_for(std::chrono::milliseconds(200));
    
    // Verify data collection
    auto snapshot = service_->get_market_snapshot();
    EXPECT_EQ(snapshot.tickers.size(), 2); // 2 exchanges
    
    for (const auto& [exchange_id, exchange_tickers] : snapshot.tickers) {
        EXPECT_GE(exchange_tickers.size(), symbols.size());
        for (const auto& [symbol, ticker] : exchange_tickers) {
            EXPECT_EQ(ticker.exchange, exchange_id);
            EXPECT_GT(ticker.bid, 0);
            EXPECT_GT(ticker.ask, ticker.bid);
        }
    }
    
    // Check statistics
    auto stats = service_->get_statistics();
    EXPECT_GT(stats.total_messages_received, 0);
    EXPECT_GT(stats.total_messages_processed, 0);
    EXPECT_GT(stats.messages_per_second, 0);
    
    // Verify service health
    EXPECT_TRUE(service_->is_healthy());
    auto health_issues = service_->get_health_issues();
    EXPECT_TRUE(health_issues.empty());
}

// Performance Benchmark Test
TEST_F(PriceCollectorServiceTest, PerformanceBenchmark) {
    // This test verifies that the system can handle the required load
    // Requirement: 5 exchanges simultaneously with CPU usage ≤ 60%
    
    EXPECT_TRUE(service_->initialize(*config_manager_));
    EXPECT_TRUE(service_->start());
    
    // Add 5 mock exchanges
    std::vector<MockExchangeAdapter*> exchanges;
    for (int i = 0; i < 5; ++i) {
        auto exchange = std::make_unique<MockExchangeAdapter>("exchange" + std::to_string(i));
        exchanges.push_back(exchange.get());
        EXPECT_TRUE(service_->add_exchange(std::move(exchange)));
    }
    
    // Subscribe to multiple symbols on all exchanges
    std::vector<std::string> symbols = {"BTC/USDT", "ETH/USDT", "BNB/USDT", "ADA/USDT", "SOL/USDT"};
    for (size_t i = 0; i < exchanges.size(); ++i) {
        for (const auto& symbol : symbols) {
            EXPECT_TRUE(service_->subscribe_to_symbol("exchange" + std::to_string(i), symbol));
        }
    }
    
    // Simulate high-frequency data for 5 seconds
    auto start_time = std::chrono::steady_clock::now();
    size_t total_messages = 0;
    
    while (std::chrono::steady_clock::now() - start_time < std::chrono::seconds(5)) {
        for (size_t i = 0; i < exchanges.size(); ++i) {
            for (const auto& symbol : symbols) {
                exchanges[i]->simulate_ticker_data(symbol);
                ++total_messages;
            }
        }
        std::this_thread::sleep_for(std::chrono::milliseconds(10)); // 100Hz update rate
    }
    
    // Wait for processing to complete
    std::this_thread::sleep_for(std::chrono::milliseconds(500));
    
    // Check performance metrics
    auto stats = service_->get_statistics();
    
    // Verify message throughput
    EXPECT_GE(stats.total_messages_received, total_messages * 0.9); // Allow 10% loss
    EXPECT_GT(stats.messages_per_second, 100); // Should handle at least 100 msg/s
    
    // Verify latency requirements (should be ≤ 100ms for processing)
    EXPECT_LE(stats.average_processing_latency.load(), std::chrono::milliseconds(100));
    
    // Note: CPU usage check would require actual system monitoring
    // In a real test environment, you would check that CPU usage ≤ 60%
    
    utils::Logger::info("Performance test completed:");
    utils::Logger::info("  Total messages received: {}", stats.total_messages_received.load());
    utils::Logger::info("  Messages per second: {}", stats.messages_per_second.load());
    utils::Logger::info("  Average latency: {} ms", stats.average_processing_latency.load().count());
}

int main(int argc, char **argv) {
    ::testing::InitGoogleTest(&argc, argv);
    
    // Create test directories
    std::filesystem::create_directories("test_logs");
    std::filesystem::create_directories("test_data");
    
    int result = RUN_ALL_TESTS();
    
    // Cleanup
    std::filesystem::remove_all("test_logs");
    std::filesystem::remove_all("test_data");
    
    return result;
}