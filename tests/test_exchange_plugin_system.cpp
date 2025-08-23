#include <gtest/gtest.h>
#include "exchange/exchange_plugin_manager.hpp"
#include "exchange/base_exchange_plugin.hpp"
#include "types/common_types.hpp"
#include <thread>
#include <chrono>

using namespace ats::exchange;
using namespace ats::types;

class ExchangePluginSystemTest : public ::testing::Test {
protected:
    void SetUp() override {
        // Clear any existing plugins
        ExchangePluginManager::instance().unload_all_plugins();
        
        // Set up event callback for testing
        plugin_events_.clear();
        ExchangePluginManager::instance().set_event_callback(
            [this](const std::string& plugin_id, PluginEvent event, const std::string& message) {
                plugin_events_.push_back({plugin_id, event, message});
            }
        );
    }
    
    void TearDown() override {
        ExchangePluginManager::instance().stop_all_plugins();
        ExchangePluginManager::instance().unload_all_plugins();
        ExchangePluginManager::instance().clear_event_callback();
    }
    
    struct PluginEventRecord {
        std::string plugin_id;
        PluginEvent event;
        std::string message;
    };
    
    std::vector<PluginEventRecord> plugin_events_;
};

TEST_F(ExchangePluginSystemTest, BuiltinPluginRegistration) {
    // Load built-in plugins
    BuiltinPluginRegistry::instance().load_all_builtin_plugins();
    
    // Check that sample plugin was loaded
    EXPECT_TRUE(ExchangePluginManager::instance().is_plugin_loaded("sample_exchange"));
    
    // Check plugin metadata
    auto metadata = ExchangePluginManager::instance().get_plugin_metadata("sample_exchange");
    EXPECT_EQ(metadata.plugin_id, "sample_exchange");
    EXPECT_EQ(metadata.plugin_name, "Sample Exchange Plugin");
    EXPECT_EQ(metadata.version, "1.0.0");
    EXPECT_FALSE(metadata.supported_symbols.empty());
    
    // Check plugin status
    EXPECT_EQ(ExchangePluginManager::instance().get_plugin_status("sample_exchange"), 
              PluginStatus::LOADED);
}

TEST_F(ExchangePluginSystemTest, PluginLifecycle) {
    // Load built-in plugins
    BuiltinPluginRegistry::instance().load_all_builtin_plugins();
    
    // Get the plugin
    auto plugin = ExchangePluginManager::instance().get_plugin("sample_exchange");
    ASSERT_NE(plugin, nullptr);
    
    // Test initialization
    ExchangeConfig config;
    config.name = "sample_exchange";
    config.enabled = true;
    config.api_key = "test_key";
    config.api_secret = "test_secret";
    config.parameters["simulate_connection_issues"] = "false";
    config.parameters["update_interval_ms"] = "500";
    
    EXPECT_TRUE(ExchangePluginManager::instance().initialize_plugin("sample_exchange", config));
    EXPECT_EQ(ExchangePluginManager::instance().get_plugin_status("sample_exchange"), 
              PluginStatus::INITIALIZED);
    
    // Test start
    EXPECT_TRUE(ExchangePluginManager::instance().start_plugin("sample_exchange"));
    EXPECT_EQ(ExchangePluginManager::instance().get_plugin_status("sample_exchange"), 
              PluginStatus::RUNNING);
    
    // Test connection
    EXPECT_TRUE(plugin->connect());
    EXPECT_TRUE(plugin->is_connected());
    EXPECT_EQ(plugin->get_connection_status(), ConnectionStatus::CONNECTED);
    
    // Test stop
    EXPECT_TRUE(ExchangePluginManager::instance().stop_plugin("sample_exchange"));
    EXPECT_EQ(ExchangePluginManager::instance().get_plugin_status("sample_exchange"), 
              PluginStatus::STOPPED);
    
    // Verify events were fired
    EXPECT_GE(plugin_events_.size(), 2);  // At least LOADED and STARTED events
}

TEST_F(ExchangePluginSystemTest, MarketDataSubscription) {
    // Load and initialize plugin
    BuiltinPluginRegistry::instance().load_all_builtin_plugins();
    
    ExchangeConfig config;
    config.name = "sample_exchange";
    config.enabled = true;
    config.parameters["update_interval_ms"] = "100";  // Fast updates for testing
    
    ASSERT_TRUE(ExchangePluginManager::instance().initialize_plugin("sample_exchange", config));
    ASSERT_TRUE(ExchangePluginManager::instance().start_plugin("sample_exchange"));
    
    auto plugin = ExchangePluginManager::instance().get_plugin("sample_exchange");
    ASSERT_NE(plugin, nullptr);
    
    ASSERT_TRUE(plugin->connect());
    
    // Set up callbacks
    std::atomic<int> ticker_count(0);
    std::atomic<int> orderbook_count(0);
    std::atomic<int> trade_count(0);
    
    plugin->set_ticker_callback([&ticker_count](const Ticker& ticker) {
        ticker_count++;
        EXPECT_FALSE(ticker.symbol.empty());
        EXPECT_GT(ticker.price, 0.0);
    });
    
    plugin->set_orderbook_callback([&orderbook_count](const OrderBook& orderbook) {
        orderbook_count++;
        EXPECT_FALSE(orderbook.symbol.empty());
        EXPECT_FALSE(orderbook.bids.empty());
        EXPECT_FALSE(orderbook.asks.empty());
    });
    
    plugin->set_trade_callback([&trade_count](const Trade& trade) {
        trade_count++;
        EXPECT_FALSE(trade.symbol.empty());
        EXPECT_GT(trade.price, 0.0);
        EXPECT_GT(trade.quantity, 0.0);
    });
    
    // Subscribe to market data
    EXPECT_TRUE(plugin->subscribe_ticker("BTCUSDT"));
    EXPECT_TRUE(plugin->subscribe_orderbook("BTCUSDT", 10));
    EXPECT_TRUE(plugin->subscribe_trades("BTCUSDT"));
    
    // Wait for some updates
    std::this_thread::sleep_for(std::chrono::milliseconds(500));
    
    // Check that we received updates
    EXPECT_GT(ticker_count.load(), 0);
    EXPECT_GT(orderbook_count.load(), 0);
    EXPECT_GT(trade_count.load(), 0);
    
    // Test unsubscribe
    EXPECT_TRUE(plugin->unsubscribe_ticker("BTCUSDT"));
    EXPECT_TRUE(plugin->unsubscribe_orderbook("BTCUSDT"));
    EXPECT_TRUE(plugin->unsubscribe_trades("BTCUSDT"));
}

TEST_F(ExchangePluginSystemTest, RestApiOperations) {
    // Load and initialize plugin
    BuiltinPluginRegistry::instance().load_all_builtin_plugins();
    
    ExchangeConfig config;
    config.name = "sample_exchange";
    config.enabled = true;
    
    ASSERT_TRUE(ExchangePluginManager::instance().initialize_plugin("sample_exchange", config));
    ASSERT_TRUE(ExchangePluginManager::instance().start_plugin("sample_exchange"));
    
    auto plugin = ExchangePluginManager::instance().get_plugin("sample_exchange");
    ASSERT_NE(plugin, nullptr);
    
    ASSERT_TRUE(plugin->connect());
    
    // Test get supported symbols
    auto symbols = plugin->get_supported_symbols();
    EXPECT_FALSE(symbols.empty());
    EXPECT_TRUE(std::find(symbols.begin(), symbols.end(), "BTCUSDT") != symbols.end());
    
    // Test get single ticker
    auto ticker = plugin->get_ticker("BTCUSDT");
    EXPECT_EQ(ticker.symbol, "BTCUSDT");
    EXPECT_GT(ticker.price, 0.0);
    
    // Test get all tickers
    auto all_tickers = plugin->get_all_tickers();
    EXPECT_FALSE(all_tickers.empty());
    EXPECT_LE(all_tickers.size(), symbols.size());
    
    // Test get orderbook
    auto orderbook = plugin->get_orderbook("BTCUSDT", 5);
    EXPECT_EQ(orderbook.symbol, "BTCUSDT");
    EXPECT_EQ(orderbook.bids.size(), 5);
    EXPECT_EQ(orderbook.asks.size(), 5);
    
    // Verify bid/ask ordering
    for (size_t i = 1; i < orderbook.bids.size(); ++i) {
        EXPECT_GE(orderbook.bids[i-1].first, orderbook.bids[i].first); // Bids descending
    }
    for (size_t i = 1; i < orderbook.asks.size(); ++i) {
        EXPECT_LE(orderbook.asks[i-1].first, orderbook.asks[i].first); // Asks ascending
    }
}

TEST_F(ExchangePluginSystemTest, RateLimiting) {
    // Load and initialize plugin
    BuiltinPluginRegistry::instance().load_all_builtin_plugins();
    
    ExchangeConfig config;
    config.name = "sample_exchange";
    config.enabled = true;
    
    ASSERT_TRUE(ExchangePluginManager::instance().initialize_plugin("sample_exchange", config));
    ASSERT_TRUE(ExchangePluginManager::instance().start_plugin("sample_exchange"));
    
    auto plugin = ExchangePluginManager::instance().get_plugin("sample_exchange");
    ASSERT_NE(plugin, nullptr);
    
    ASSERT_TRUE(plugin->connect());
    
    // Initially should be able to make requests
    EXPECT_TRUE(plugin->can_make_request());
    
    // Make multiple requests to trigger rate limiting
    for (int i = 0; i < 10; ++i) {
        plugin->get_ticker("BTCUSDT");
    }
    
    // Should still be able to make requests (rate limit is high)
    EXPECT_TRUE(plugin->can_make_request());
    
    // Check delay is minimal
    auto delay = plugin->get_next_request_delay();
    EXPECT_LE(delay.count(), 100);  // Should be less than 100ms
}

TEST_F(ExchangePluginSystemTest, ErrorHandling) {
    // Load and initialize plugin with connection issues enabled
    BuiltinPluginRegistry::instance().load_all_builtin_plugins();
    
    ExchangeConfig config;
    config.name = "sample_exchange";
    config.enabled = true;
    config.parameters["simulate_connection_issues"] = "true";
    
    ASSERT_TRUE(ExchangePluginManager::instance().initialize_plugin("sample_exchange", config));
    ASSERT_TRUE(ExchangePluginManager::instance().start_plugin("sample_exchange"));
    
    auto plugin = ExchangePluginManager::instance().get_plugin("sample_exchange");
    ASSERT_NE(plugin, nullptr);
    
    // Set up error callback
    std::atomic<int> error_count(0);
    plugin->set_error_callback([&error_count](const std::string& plugin_id, const std::string& error) {
        error_count++;
        EXPECT_EQ(plugin_id, "sample_exchange");
        EXPECT_FALSE(error.empty());
    });
    
    // Try to connect multiple times (some may fail due to simulated issues)
    int successful_connections = 0;
    for (int i = 0; i < 20; ++i) {
        plugin->disconnect();
        if (plugin->connect()) {
            successful_connections++;
        }
        std::this_thread::sleep_for(std::chrono::milliseconds(10));
    }
    
    // Should have some successful connections
    EXPECT_GT(successful_connections, 0);
    EXPECT_LT(successful_connections, 20);  // Some should have failed
}

TEST_F(ExchangePluginSystemTest, MultiplePluginManagement) {
    // For this test, we'll just load the same plugin with different IDs
    // In a real scenario, these would be different exchange plugins
    
    ExchangePluginMetadata metadata1, metadata2;
    metadata1.plugin_id = "exchange1";
    metadata1.plugin_name = "Exchange 1";
    metadata1.version = "1.0.0";
    
    metadata2.plugin_id = "exchange2";
    metadata2.plugin_name = "Exchange 2";
    metadata2.version = "1.0.0";
    
    // Note: In a real test, we'd create actual different plugin instances
    // For now, just test the manager's capability to handle multiple plugins
    
    auto& manager = ExchangePluginManager::instance();
    
    // Get initial stats
    EXPECT_EQ(manager.get_total_plugins(), 0);
    EXPECT_EQ(manager.get_loaded_plugins_count(), 0);
    EXPECT_EQ(manager.get_running_plugins_count(), 0);
    
    // Load built-in plugin
    BuiltinPluginRegistry::instance().load_all_builtin_plugins();
    
    // Check updated stats
    EXPECT_EQ(manager.get_total_plugins(), 1);
    EXPECT_EQ(manager.get_loaded_plugins_count(), 1);
    EXPECT_EQ(manager.get_running_plugins_count(), 0);
    
    // Get list of plugins
    auto plugins = manager.get_loaded_plugins();
    EXPECT_EQ(plugins.size(), 1);
    EXPECT_TRUE(std::find(plugins.begin(), plugins.end(), "sample_exchange") != plugins.end());
}

TEST_F(ExchangePluginSystemTest, PluginMetadataValidation) {
    BuiltinPluginRegistry::instance().load_all_builtin_plugins();
    
    auto metadata = ExchangePluginManager::instance().get_plugin_metadata("sample_exchange");
    
    // Validate metadata completeness
    EXPECT_FALSE(metadata.plugin_id.empty());
    EXPECT_FALSE(metadata.plugin_name.empty());
    EXPECT_FALSE(metadata.version.empty());
    EXPECT_FALSE(metadata.description.empty());
    EXPECT_FALSE(metadata.author.empty());
    EXPECT_FALSE(metadata.supported_symbols.empty());
    EXPECT_FALSE(metadata.api_base_url.empty());
    EXPECT_FALSE(metadata.websocket_url.empty());
    EXPECT_GT(metadata.rate_limit_per_minute, 0);
    
    // Validate capabilities
    EXPECT_TRUE(metadata.supports_rest_api);
    EXPECT_TRUE(metadata.supports_websocket);
    EXPECT_TRUE(metadata.supports_orderbook);
    EXPECT_TRUE(metadata.supports_trades);
}