#pragma once

#include "exchange_plugin_interface.hpp"
#include "utils/logger.hpp"
#include <atomic>
#include <chrono>
#include <thread>
#include <mutex>
#include <condition_variable>
#include <deque>

namespace ats {
namespace exchange {

// Rate limiter for exchange requests
class RateLimiter {
public:
    explicit RateLimiter(int requests_per_minute = 1200);
    
    bool can_make_request() const;
    void record_request();
    std::chrono::milliseconds get_next_request_delay() const;
    void set_rate_limit(int requests_per_minute);
    
private:
    mutable std::mutex mutex_;
    mutable std::deque<std::chrono::steady_clock::time_point> request_times_;
    int max_requests_;
    std::chrono::minutes window_duration_;
};

// Base implementation of IExchangePlugin with common functionality
class BaseExchangePlugin : public IExchangePlugin {
public:
    BaseExchangePlugin();
    virtual ~BaseExchangePlugin();
    
    // IExchangePlugin implementation - lifecycle
    bool initialize(const types::ExchangeConfig& config) override;
    bool start() override;
    void stop() override;
    void cleanup() override;
    
    // IExchangePlugin implementation - metadata
    std::string get_plugin_id() const override;
    std::string get_version() const override;
    
    // IExchangePlugin implementation - connection
    bool connect() override;
    void disconnect() override;
    bool is_connected() const override;
    types::ConnectionStatus get_connection_status() const override;
    
    // IExchangePlugin implementation - subscriptions  
    bool subscribe_ticker(const std::string& symbol) override;
    bool subscribe_orderbook(const std::string& symbol, int depth = 20) override;
    bool subscribe_trades(const std::string& symbol) override;
    bool unsubscribe_ticker(const std::string& symbol) override;
    bool unsubscribe_orderbook(const std::string& symbol) override;
    bool unsubscribe_trades(const std::string& symbol) override;
    bool unsubscribe_all() override;
    
    // IExchangePlugin implementation - callbacks
    void set_ticker_callback(TickerCallback callback) override;
    void set_orderbook_callback(OrderBookCallback callback) override;
    void set_trade_callback(TradeCallback callback) override;
    void set_connection_callback(ConnectionCallback callback) override;
    void set_error_callback(ErrorCallback callback) override;
    
    // IExchangePlugin implementation - monitoring
    size_t get_messages_received() const override;
    size_t get_messages_per_second() const override;
    std::chrono::milliseconds get_average_latency() const override;
    std::string get_last_error() const override;
    void clear_error() override;
    
    // IExchangePlugin implementation - rate limiting
    bool can_make_request() const override;
    void record_request() override;
    std::chrono::milliseconds get_next_request_delay() const override;
    
    // IExchangePlugin implementation - data retrieval
    std::vector<types::Ticker> get_all_tickers() override;
    types::Ticker get_ticker(const std::string& symbol) override;
    std::vector<std::string> get_supported_symbols() override;
    types::OrderBook get_orderbook(const std::string& symbol, int depth = 20) override;
    
    // IExchangePlugin implementation - trading (default no-op implementations)
    bool supports_trading() const override { return false; }
    types::OrderResult place_order(const types::Order& order) override {
        return types::OrderResult{false, "Trading not supported by this plugin", ""};
    }
    bool cancel_order(const std::string& order_id) override { return false; }
    std::vector<types::Order> get_active_orders() override { return {}; }
    types::Balance get_balance() override { return {}; }
    
protected:
    // Derived classes must implement these abstract methods
    virtual ExchangePluginMetadata create_metadata() const = 0;
    virtual bool do_connect() = 0;
    virtual void do_disconnect() = 0;
    virtual bool do_subscribe_ticker(const std::string& symbol) = 0;
    virtual bool do_subscribe_orderbook(const std::string& symbol, int depth) = 0;
    virtual bool do_subscribe_trades(const std::string& symbol) = 0;
    virtual bool do_unsubscribe_ticker(const std::string& symbol) = 0;
    virtual bool do_unsubscribe_orderbook(const std::string& symbol) = 0;
    virtual bool do_unsubscribe_trades(const std::string& symbol) = 0;
    virtual bool do_unsubscribe_all() = 0;
    virtual std::vector<types::Ticker> do_get_all_tickers() = 0;
    virtual types::Ticker do_get_ticker(const std::string& symbol) = 0;
    virtual std::vector<std::string> do_get_supported_symbols() = 0;
    virtual types::OrderBook do_get_orderbook(const std::string& symbol, int depth) = 0;
    
    // Optional overrides for derived classes
    virtual bool do_initialize(const types::ExchangeConfig& config) { return true; }
    virtual bool do_start() { return true; }
    virtual void do_stop() {}
    virtual void do_cleanup() {}
    
    // Helper methods for derived classes
    void set_connection_status(types::ConnectionStatus status);
    void set_error(const std::string& error);
    void increment_message_count();
    void update_latency(std::chrono::milliseconds latency);
    
    // Callback notification helpers
    void notify_ticker(const types::Ticker& ticker);
    void notify_orderbook(const types::OrderBook& orderbook);
    void notify_trade(const types::Trade& trade);
    void notify_connection_change(bool connected);
    void notify_error(const std::string& error);
    
    // Configuration access
    const types::ExchangeConfig& get_config() const { return config_; }
    
    // Thread-safe logging
    void log_info(const std::string& message) const;
    void log_warning(const std::string& message) const;
    void log_error(const std::string& message) const;
    void log_debug(const std::string& message) const;
    
    // Utility methods
    std::string format_symbol(const std::string& symbol) const;
    bool validate_symbol(const std::string& symbol) const;
    std::string get_plugin_name() const;
    
private:
    // Configuration and state
    types::ExchangeConfig config_;
    std::atomic<types::ConnectionStatus> connection_status_;
    std::atomic<bool> initialized_;
    std::atomic<bool> started_;
    
    // Error handling
    mutable std::mutex error_mutex_;
    std::string last_error_;
    
    // Callbacks
    std::mutex callback_mutex_;
    TickerCallback ticker_callback_;
    OrderBookCallback orderbook_callback_;
    TradeCallback trade_callback_;
    ConnectionCallback connection_callback_;
    ErrorCallback error_callback_;
    
    // Statistics
    std::atomic<size_t> message_count_;
    std::atomic<int64_t> last_message_time_ns_;  // Use nanoseconds since epoch as atomic int64_t
    mutable std::mutex latency_mutex_;
    std::deque<std::chrono::milliseconds> latency_samples_;
    static constexpr size_t MAX_LATENCY_SAMPLES = 100;
    
    // Rate limiting
    std::unique_ptr<RateLimiter> rate_limiter_;
    
    // Cached metadata
    mutable std::once_flag metadata_flag_;
    mutable ExchangePluginMetadata cached_metadata_;
    
    // Helper methods
    void update_message_statistics();
    std::chrono::milliseconds calculate_average_latency() const;
    const ExchangePluginMetadata& get_cached_metadata() const;
};

// Utility template for creating plugin metadata
template<typename PluginClass>
ExchangePluginMetadata create_plugin_metadata(
    const std::string& plugin_id,
    const std::string& plugin_name,
    const std::string& version,
    const std::string& description,
    const std::string& author,
    const std::vector<std::string>& supported_symbols,
    const std::string& api_base_url,
    const std::string& websocket_url,
    bool supports_rest_api = true,
    bool supports_websocket = true,
    bool supports_orderbook = true,
    bool supports_trades = true,
    int rate_limit_per_minute = 1200
) {
    ExchangePluginMetadata metadata;
    metadata.plugin_id = plugin_id;
    metadata.plugin_name = plugin_name;
    metadata.version = version;
    metadata.description = description;
    metadata.author = author;
    metadata.supported_symbols = supported_symbols;
    metadata.api_base_url = api_base_url;
    metadata.websocket_url = websocket_url;
    metadata.supports_rest_api = supports_rest_api;
    metadata.supports_websocket = supports_websocket;
    metadata.supports_orderbook = supports_orderbook;
    metadata.supports_trades = supports_trades;
    metadata.rate_limit_per_minute = rate_limit_per_minute;
    return metadata;
}

} // namespace exchange
} // namespace ats