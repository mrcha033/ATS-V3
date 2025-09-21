#pragma once

#include <string>
#include <memory>
#include <vector>
#include <functional>
#include <unordered_map>
#include "types/common_types.hpp"

namespace ats {
namespace exchange {

// Plugin metadata structure
struct ExchangePluginMetadata {
    std::string plugin_id;           // Unique plugin identifier
    std::string plugin_name;         // Human-readable plugin name
    std::string version;             // Plugin version (e.g., "1.0.0")
    std::string description;         // Plugin description
    std::string author;              // Plugin author
    std::vector<std::string> supported_symbols;  // Supported trading pairs
    std::string api_base_url;        // Exchange API base URL
    std::string websocket_url;       // Exchange WebSocket URL
    bool supports_rest_api;
    bool supports_websocket;
    bool supports_orderbook;
    bool supports_trades;
    int rate_limit_per_minute;

    ExchangePluginMetadata()
        : supports_rest_api(true), supports_websocket(false)
        , supports_orderbook(false), supports_trades(false)
        , rate_limit_per_minute(1200) {}
};

// Unified exchange interface for plugins
class IExchangePlugin {
public:
    virtual ~IExchangePlugin() = default;

    // Plugin lifecycle
    virtual bool initialize(const types::ExchangeConfig& config) = 0;
    virtual bool start() = 0;
    virtual void stop() = 0;
    virtual void cleanup() = 0;

    // Metadata access
    virtual ExchangePluginMetadata get_metadata() const = 0;
    virtual std::string get_plugin_id() const = 0;
    virtual std::string get_version() const = 0;

    // Connection management
    virtual bool connect() = 0;
    virtual void disconnect() = 0;
    virtual bool is_connected() const = 0;
    virtual types::ConnectionStatus get_connection_status() const = 0;

    // Market data operations
    virtual bool subscribe_ticker(const std::string& symbol) = 0;
    virtual bool subscribe_orderbook(const std::string& symbol, int depth = 20) = 0;
    virtual bool subscribe_trades(const std::string& symbol) = 0;
    virtual bool unsubscribe_ticker(const std::string& symbol) = 0;
    virtual bool unsubscribe_orderbook(const std::string& symbol) = 0;
    virtual bool unsubscribe_trades(const std::string& symbol) = 0;
    virtual bool unsubscribe_all() = 0;

    // Data retrieval
    virtual std::vector<types::Ticker> get_all_tickers() = 0;
    virtual types::Ticker get_ticker(const std::string& symbol) = 0;
    virtual std::vector<std::string> get_supported_symbols() = 0;
    virtual types::OrderBook get_orderbook(const std::string& symbol, int depth = 20) = 0;

    // Trading operations (optional - not all plugins need to support trading)
    virtual bool supports_trading() const { return false; }
    virtual types::OrderResult place_order(const types::Order& order) {
        return types::OrderResult{false, "Trading not supported", ""};
    }
    virtual bool cancel_order(const std::string& order_id) { return false; }
    virtual std::vector<types::Order> get_active_orders() { return {}; }
    virtual types::Balance get_balance() { return {}; }

    // Callbacks for real-time data
    using TickerCallback = std::function<void(const types::Ticker&)>;
    using OrderBookCallback = std::function<void(const types::OrderBook&)>;
    using TradeCallback = std::function<void(const types::Trade&)>;
    using ConnectionCallback = std::function<void(const std::string&, bool)>;
    using ErrorCallback = std::function<void(const std::string&, const std::string&)>;

    virtual void set_ticker_callback(TickerCallback callback) = 0;
    virtual void set_orderbook_callback(OrderBookCallback callback) = 0;
    virtual void set_trade_callback(TradeCallback callback) = 0;
    virtual void set_connection_callback(ConnectionCallback callback) = 0;
    virtual void set_error_callback(ErrorCallback callback) = 0;

    // Monitoring and statistics
    virtual size_t get_messages_received() const = 0;
    virtual size_t get_messages_per_second() const = 0;
    virtual std::chrono::milliseconds get_average_latency() const = 0;
    virtual std::string get_last_error() const = 0;
    virtual void clear_error() = 0;

    // Rate limiting
    virtual bool can_make_request() const = 0;
    virtual void record_request() = 0;
    virtual std::chrono::milliseconds get_next_request_delay() const = 0;
};

// Plugin factory function type - each plugin DLL exports this
using CreatePluginFunction = std::function<std::unique_ptr<IExchangePlugin>()>;
using GetMetadataFunction = std::function<ExchangePluginMetadata()>;

// Plugin descriptor for loaded plugins
struct PluginDescriptor {
    std::string plugin_path;
    void* library_handle;
    ExchangePluginMetadata metadata;
    CreatePluginFunction create_function;
    GetMetadataFunction metadata_function;
    bool is_loaded;
    std::chrono::system_clock::time_point loaded_at;

    PluginDescriptor() : library_handle(nullptr), is_loaded(false) {}
};

} // namespace exchange
} // namespace ats

// Plugin export macros - used in plugin implementations
#define EXCHANGE_PLUGIN_EXPORT extern "C" __declspec(dllexport)

// Standard plugin export functions that each plugin DLL must implement
#define IMPLEMENT_EXCHANGE_PLUGIN(plugin_class) \
    EXCHANGE_PLUGIN_EXPORT ats::exchange::ExchangePluginMetadata get_plugin_metadata() { \
        static plugin_class instance; \
        return instance.get_metadata(); \
    } \
    EXCHANGE_PLUGIN_EXPORT std::unique_ptr<ats::exchange::IExchangePlugin> create_plugin_instance() { \
        return std::make_unique<plugin_class>(); \
    } \
    EXCHANGE_PLUGIN_EXPORT const char* get_plugin_api_version() { \
        return "1.0.0"; \
    }