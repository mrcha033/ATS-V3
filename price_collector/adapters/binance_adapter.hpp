#pragma once

#include "exchange_interface.hpp"
#include "http_client.hpp"
#include "websocket_client.hpp"
#include <nlohmann/json.hpp>
#include <memory>
#include <unordered_map>
#include <unordered_set>

namespace ats {
namespace price_collector {

class BinanceAdapter : public ExchangeInterface {
public:
    BinanceAdapter();
    ~BinanceAdapter() override;
    
    // ExchangeInterface implementation
    std::string get_exchange_id() const override;
    std::string get_exchange_name() const override;
    ExchangeCapabilities get_capabilities() const override;
    
    bool initialize(const types::ExchangeConfig& config) override;
    bool connect() override;
    void disconnect() override;
    ConnectionStatus get_connection_status() const override;
    bool is_connected() const override;
    
    bool subscribe_ticker(const std::string& symbol) override;
    bool subscribe_orderbook(const std::string& symbol, int depth = 20) override;
    bool subscribe_trades(const std::string& symbol) override;
    bool subscribe_multiple(const std::vector<SubscriptionRequest>& requests) override;
    
    bool unsubscribe_ticker(const std::string& symbol) override;
    bool unsubscribe_orderbook(const std::string& symbol) override;
    bool unsubscribe_trades(const std::string& symbol) override;
    bool unsubscribe_all() override;
    
    std::vector<types::Ticker> get_all_tickers() override;
    types::Ticker get_ticker(const std::string& symbol) override;
    std::vector<std::string> get_supported_symbols() override;
    
    void set_ticker_callback(TickerCallback callback) override;
    void set_orderbook_callback(OrderBookCallback callback) override;
    void set_trade_callback(TradeCallback callback) override;
    void set_connection_status_callback(ConnectionStatusCallback callback) override;
    
    size_t get_messages_received() const override;
    size_t get_messages_per_second() const override;
    std::chrono::milliseconds get_average_latency() const override;
    std::chrono::milliseconds get_last_message_time() const override;
    size_t get_subscribed_symbols_count() const override;
    
    std::string get_last_error() const override;
    void clear_error() override;
    
    bool can_make_request() const override;
    void record_request() override;
    std::chrono::milliseconds get_next_request_delay() const override;
    
protected:
    void notify_ticker_update(const types::Ticker& ticker) override;
    void notify_connection_status_change(bool connected) override;
    void handle_error(const std::string& error_message) override;
    
private:
    // Configuration and connection
    types::ExchangeConfig config_;
    std::shared_ptr<HttpClient> http_client_;
    std::shared_ptr<WebSocketClient> ws_client_;
    std::unique_ptr<RateLimiter> rate_limiter_;
    
    // WebSocket streams
    std::unordered_set<std::string> subscribed_symbols_;
    std::unordered_map<std::string, int> orderbook_depths_;
    
    // Callbacks
    TickerCallback ticker_callback_;
    OrderBookCallback orderbook_callback_;
    TradeCallback trade_callback_;
    ConnectionStatusCallback connection_callback_;
    
    // State
    ConnectionStatus connection_status_;
    std::string last_error_;
    std::atomic<size_t> messages_received_{0};
    std::atomic<std::chrono::milliseconds> last_message_time_{std::chrono::milliseconds(0)};
    
    // Message processing
    void on_websocket_message(const WebSocketMessage& message);
    void on_websocket_connection(WebSocketStatus status, const std::string& reason);
    void on_websocket_error(const std::string& error);
    
    // Message parsing
    void parse_ticker_message(const nlohmann::json& json);
    void parse_orderbook_message(const nlohmann::json& json);
    void parse_trade_message(const nlohmann::json& json);
    void parse_error_message(const nlohmann::json& json);
    
    // Symbol conversion
    std::string to_binance_symbol(const std::string& symbol) const;
    std::string from_binance_symbol(const std::string& binance_symbol) const;
    
    // REST API helpers
    HttpResponse make_rest_request(const std::string& endpoint, 
                                  const std::unordered_map<std::string, std::string>& params = {});
    std::string build_signed_params(const std::unordered_map<std::string, std::string>& params) const;
    
    // WebSocket helpers
    bool send_subscribe_message(const std::string& stream);
    bool send_unsubscribe_message(const std::string& stream);
    std::string create_stream_name(const std::string& symbol, const std::string& stream_type, int depth = 0);
    
    // Constants
    static const std::string BASE_URL_REST;
    static const std::string BASE_URL_WS;
    static const std::unordered_map<std::string, std::string> SYMBOL_MAPPING;
    static const int DEFAULT_RATE_LIMIT;
    static const std::chrono::milliseconds DEFAULT_TIMEOUT;
};

// Symbol mapping utilities for Binance
namespace binance_utils {
    
    // Symbol conversion
    std::string normalize_symbol(const std::string& symbol);
    std::string to_binance_format(const std::string& symbol);
    std::string from_binance_format(const std::string& binance_symbol);
    
    // Price/quantity formatting
    std::string format_price(double price, int precision = 8);
    std::string format_quantity(double quantity, int precision = 8);
    
    // Timestamp utilities
    uint64_t get_server_time();
    std::chrono::system_clock::time_point parse_timestamp(uint64_t timestamp);
    
    // Signature generation
    std::string generate_signature(const std::string& query_string, const std::string& secret_key);
    
    // Stream name builders
    std::string build_ticker_stream(const std::string& symbol);
    std::string build_orderbook_stream(const std::string& symbol, int depth = 20);
    std::string build_trade_stream(const std::string& symbol);
    std::string build_combined_stream(const std::vector<std::string>& streams);
    
    // Message validation
    bool is_valid_ticker_message(const nlohmann::json& json);
    bool is_valid_orderbook_message(const nlohmann::json& json);
    bool is_valid_trade_message(const nlohmann::json& json);
    bool is_error_message(const nlohmann::json& json);
    
    // Error handling
    std::string extract_error_message(const nlohmann::json& json);
    int extract_error_code(const nlohmann::json& json);
    
    // Rate limiting helpers
    bool should_throttle_request(const HttpResponse& response);
    std::chrono::milliseconds get_retry_delay(const HttpResponse& response);
    
    // Data validation
    bool is_valid_symbol(const std::string& symbol);
    bool is_supported_stream_type(const std::string& stream_type);
    int validate_orderbook_depth(int depth);
    
    // JSON helpers
    template<typename T>
    T safe_get(const nlohmann::json& json, const std::string& key, const T& default_value = T{});
    
    std::string safe_get_string(const nlohmann::json& json, const std::string& key, 
                               const std::string& default_value = "");
    double safe_get_double(const nlohmann::json& json, const std::string& key, double default_value = 0.0);
    uint64_t safe_get_uint64(const nlohmann::json& json, const std::string& key, uint64_t default_value = 0);
}

} // namespace price_collector
} // namespace ats