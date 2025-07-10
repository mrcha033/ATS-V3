#pragma once

#include <string>
#include <vector>
#include <functional>
#include <memory>
#include <chrono>
#include "types/common_types.hpp"

namespace ats {
namespace price_collector {

// Forward declarations
class MarketDataReceiver;

// Callback types for market data events
using TickerCallback = std::function<void(const types::Ticker&)>;
using OrderBookCallback = std::function<void(const std::string& symbol, const std::string& exchange, 
                                           const std::vector<std::pair<double, double>>& bids,
                                           const std::vector<std::pair<double, double>>& asks)>;
using TradeCallback = std::function<void(const std::string& symbol, const std::string& exchange,
                                       double price, double quantity, types::Timestamp timestamp)>;
using ConnectionStatusCallback = std::function<void(const std::string& exchange, bool connected)>;

// Exchange capabilities
struct ExchangeCapabilities {
    bool supports_rest_api;
    bool supports_websocket;
    bool supports_ticker_stream;
    bool supports_orderbook_stream;
    bool supports_trade_stream;
    std::vector<std::string> supported_symbols;
    int max_symbols_per_connection;
    int rate_limit_per_minute;
    std::chrono::milliseconds min_request_interval;
    
    ExchangeCapabilities() 
        : supports_rest_api(true), supports_websocket(false), supports_ticker_stream(false)
        , supports_orderbook_stream(false), supports_trade_stream(false)
        , max_symbols_per_connection(100), rate_limit_per_minute(1200)
        , min_request_interval(std::chrono::milliseconds(100)) {}
};

// Exchange connection status
enum class ConnectionStatus {
    DISCONNECTED,
    CONNECTING,
    CONNECTED,
    RECONNECTING,
    ERROR
};

// Market data subscription request
struct SubscriptionRequest {
    std::string symbol;
    bool ticker;
    bool orderbook;
    bool trades;
    int orderbook_depth;
    
    SubscriptionRequest(const std::string& sym, bool t = true, bool ob = false, bool tr = false, int depth = 20)
        : symbol(sym), ticker(t), orderbook(ob), trades(tr), orderbook_depth(depth) {}
};

// Abstract base class for exchange adapters
class ExchangeInterface {
public:
    virtual ~ExchangeInterface() = default;
    
    // Basic information
    virtual std::string get_exchange_id() const = 0;
    virtual std::string get_exchange_name() const = 0;
    virtual ExchangeCapabilities get_capabilities() const = 0;
    
    // Connection management
    virtual bool initialize(const types::ExchangeConfig& config) = 0;
    virtual bool connect() = 0;
    virtual void disconnect() = 0;
    virtual ConnectionStatus get_connection_status() const = 0;
    virtual bool is_connected() const = 0;
    
    // Market data subscriptions
    virtual bool subscribe_ticker(const std::string& symbol) = 0;
    virtual bool subscribe_orderbook(const std::string& symbol, int depth = 20) = 0;
    virtual bool subscribe_trades(const std::string& symbol) = 0;
    virtual bool subscribe_multiple(const std::vector<SubscriptionRequest>& requests) = 0;
    
    virtual bool unsubscribe_ticker(const std::string& symbol) = 0;
    virtual bool unsubscribe_orderbook(const std::string& symbol) = 0;
    virtual bool unsubscribe_trades(const std::string& symbol) = 0;
    virtual bool unsubscribe_all() = 0;
    
    // REST API methods for initial data
    virtual std::vector<types::Ticker> get_all_tickers() = 0;
    virtual types::Ticker get_ticker(const std::string& symbol) = 0;
    virtual std::vector<std::string> get_supported_symbols() = 0;
    
    // Callback registration
    virtual void set_ticker_callback(TickerCallback callback) = 0;
    virtual void set_orderbook_callback(OrderBookCallback callback) = 0;
    virtual void set_trade_callback(TradeCallback callback) = 0;
    virtual void set_connection_status_callback(ConnectionStatusCallback callback) = 0;
    
    // Statistics and monitoring
    virtual size_t get_messages_received() const = 0;
    virtual size_t get_messages_per_second() const = 0;
    virtual std::chrono::milliseconds get_average_latency() const = 0;
    virtual std::chrono::milliseconds get_last_message_time() const = 0;
    virtual size_t get_subscribed_symbols_count() const = 0;
    
    // Error handling
    virtual std::string get_last_error() const = 0;
    virtual void clear_error() = 0;
    
    // Rate limiting
    virtual bool can_make_request() const = 0;
    virtual void record_request() = 0;
    virtual std::chrono::milliseconds get_next_request_delay() const = 0;
    
protected:
    // Helper methods for derived classes
    virtual void notify_ticker_update(const types::Ticker& ticker) = 0;
    virtual void notify_connection_status_change(bool connected) = 0;
    virtual void handle_error(const std::string& error_message) = 0;
};

// Factory for creating exchange adapters
class ExchangeFactory {
public:
    using CreateFunction = std::function<std::unique_ptr<ExchangeInterface>()>;
    
    static std::unique_ptr<ExchangeInterface> create_exchange(const std::string& exchange_id);
    static std::vector<std::string> get_supported_exchanges();
    static void register_exchange(const std::string& exchange_id, CreateFunction create_func);
    
private:
    static std::unordered_map<std::string, CreateFunction>& get_creators();
};

// Macro for easy exchange registration
#define REGISTER_EXCHANGE(exchange_id, class_name) \
    namespace { \
        bool registered_##class_name = []() { \
            ExchangeFactory::register_exchange(exchange_id, []() { \
                return std::make_unique<class_name>(); \
            }); \
            return true; \
        }(); \
    }

} // namespace price_collector
} // namespace ats