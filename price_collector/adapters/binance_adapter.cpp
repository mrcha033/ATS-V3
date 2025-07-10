#include "binance_adapter.hpp"
#include "utils/logger.hpp"
#include "utils/crypto_utils.hpp"
#include <boost/asio/io_context.hpp>
#include <boost/asio/ssl/context.hpp>
#include <algorithm>
#include <sstream>

namespace ats {
namespace price_collector {

// Constants
const std::string BinanceAdapter::BASE_URL_REST = "api.binance.com";
const std::string BinanceAdapter::BASE_URL_WS = "stream.binance.com:9443";
const int BinanceAdapter::DEFAULT_RATE_LIMIT = 1200; // requests per minute
const std::chrono::milliseconds BinanceAdapter::DEFAULT_TIMEOUT = std::chrono::milliseconds(5000);

// Symbol mapping from standard format to Binance format
const std::unordered_map<std::string, std::string> BinanceAdapter::SYMBOL_MAPPING = {
    {"BTC/USDT", "BTCUSDT"},
    {"ETH/USDT", "ETHUSDT"},
    {"BNB/USDT", "BNBUSDT"},
    {"ADA/USDT", "ADAUSDT"},
    {"SOL/USDT", "SOLUSDT"},
    {"XRP/USDT", "XRPUSDT"},
    {"DOT/USDT", "DOTUSDT"},
    {"AVAX/USDT", "AVAXUSDT"},
    {"LUNA/USDT", "LUNAUSDT"},
    {"MATIC/USDT", "MATICUSDT"}
};

BinanceAdapter::BinanceAdapter() 
    : connection_status_(ConnectionStatus::DISCONNECTED) {
    
    rate_limiter_ = std::make_unique<RateLimiter>(DEFAULT_RATE_LIMIT);
}

BinanceAdapter::~BinanceAdapter() {
    disconnect();
}

std::string BinanceAdapter::get_exchange_id() const {
    return "binance";
}

std::string BinanceAdapter::get_exchange_name() const {
    return "Binance";
}

ExchangeCapabilities BinanceAdapter::get_capabilities() const {
    ExchangeCapabilities caps;
    caps.supports_rest_api = true;
    caps.supports_websocket = true;
    caps.supports_ticker_stream = true;
    caps.supports_orderbook_stream = true;
    caps.supports_trade_stream = true;
    caps.max_symbols_per_connection = 1024;
    caps.rate_limit_per_minute = DEFAULT_RATE_LIMIT;
    caps.min_request_interval = std::chrono::milliseconds(50);
    
    // Add supported symbols
    for (const auto& [standard, binance] : SYMBOL_MAPPING) {
        caps.supported_symbols.push_back(standard);
    }
    
    return caps;
}

bool BinanceAdapter::initialize(const types::ExchangeConfig& config) {
    config_ = config;
    
    try {
        // Create IO context and SSL context (these would typically be managed by a higher-level service)
        static boost::asio::io_context ioc;
        static boost::asio::ssl::context ssl_ctx(boost::asio::ssl::context::sslv23_client);
        
        // Initialize HTTP client
        http_client_ = std::make_shared<HttpClient>(ioc, ssl_ctx, BASE_URL_REST, "443", true);
        http_client_->set_user_agent("ATS-V3/1.0 Binance-Adapter");
        http_client_->set_default_headers(http_utils::json_headers());
        http_client_->set_rate_limiter(std::make_unique<RateLimiter>(config.rate_limit));
        
        // Initialize WebSocket client
        ws_client_ = std::make_shared<WebSocketClient>(ioc, ssl_ctx);
        
        WebSocketConfig ws_config;
        ws_config.host = "stream.binance.com";
        ws_config.port = "9443";
        ws_config.target = "/ws";
        ws_config.use_ssl = true;
        ws_config.ping_interval = std::chrono::seconds(30);
        ws_config.pong_timeout = std::chrono::seconds(10);
        ws_config.reconnect_delay = std::chrono::seconds(5);
        ws_config.max_reconnect_attempts = 10;
        
        ws_client_->configure(ws_config);
        
        // Set WebSocket callbacks
        ws_client_->set_message_callback(
            [this](const WebSocketMessage& msg) { on_websocket_message(msg); });
        ws_client_->set_connection_callback(
            [this](WebSocketStatus status, const std::string& reason) { 
                on_websocket_connection(status, reason); });
        ws_client_->set_error_callback(
            [this](const std::string& error) { on_websocket_error(error); });
        
        utils::Logger::info("Binance adapter initialized successfully");
        return true;
        
    } catch (const std::exception& e) {
        handle_error("Failed to initialize Binance adapter: " + std::string(e.what()));
        return false;
    }
}

bool BinanceAdapter::connect() {
    if (connection_status_ == ConnectionStatus::CONNECTED) {
        return true;
    }
    
    try {
        connection_status_ = ConnectionStatus::CONNECTING;
        notify_connection_status_change(false);
        
        // Connect HTTP client
        if (!http_client_->connect()) {
            handle_error("Failed to connect HTTP client");
            return false;
        }
        
        // Connect WebSocket client
        ws_client_->connect();
        ws_client_->enable_auto_reconnect(true);
        
        utils::Logger::info("Binance adapter connected successfully");
        return true;
        
    } catch (const std::exception& e) {
        handle_error("Failed to connect to Binance: " + std::string(e.what()));
        connection_status_ = ConnectionStatus::ERROR;
        notify_connection_status_change(false);
        return false;
    }
}

void BinanceAdapter::disconnect() {
    if (connection_status_ == ConnectionStatus::DISCONNECTED) {
        return;
    }
    
    connection_status_ = ConnectionStatus::DISCONNECTED;
    
    // Unsubscribe from all streams
    unsubscribe_all();
    
    // Disconnect clients
    if (ws_client_) {
        ws_client_->disconnect();
    }
    
    if (http_client_) {
        http_client_->disconnect();
    }
    
    subscribed_symbols_.clear();
    orderbook_depths_.clear();
    
    notify_connection_status_change(false);
    utils::Logger::info("Binance adapter disconnected");
}

ConnectionStatus BinanceAdapter::get_connection_status() const {
    return connection_status_;
}

bool BinanceAdapter::is_connected() const {
    return connection_status_ == ConnectionStatus::CONNECTED && 
           ws_client_ && ws_client_->is_connected();
}

bool BinanceAdapter::subscribe_ticker(const std::string& symbol) {
    std::string binance_symbol = to_binance_symbol(symbol);
    if (binance_symbol.empty()) {
        handle_error("Unsupported symbol: " + symbol);
        return false;
    }
    
    std::string stream = binance_utils::build_ticker_stream(binance_symbol);
    if (send_subscribe_message(stream)) {
        subscribed_symbols_.insert(symbol);
        utils::Logger::debug("Subscribed to ticker for {}", symbol);
        return true;
    }
    
    return false;
}

bool BinanceAdapter::subscribe_orderbook(const std::string& symbol, int depth) {
    std::string binance_symbol = to_binance_symbol(symbol);
    if (binance_symbol.empty()) {
        handle_error("Unsupported symbol: " + symbol);
        return false;
    }
    
    depth = binance_utils::validate_orderbook_depth(depth);
    std::string stream = binance_utils::build_orderbook_stream(binance_symbol, depth);
    
    if (send_subscribe_message(stream)) {
        subscribed_symbols_.insert(symbol);
        orderbook_depths_[symbol] = depth;
        utils::Logger::debug("Subscribed to orderbook for {} with depth {}", symbol, depth);
        return true;
    }
    
    return false;
}

bool BinanceAdapter::subscribe_trades(const std::string& symbol) {
    std::string binance_symbol = to_binance_symbol(symbol);
    if (binance_symbol.empty()) {
        handle_error("Unsupported symbol: " + symbol);
        return false;
    }
    
    std::string stream = binance_utils::build_trade_stream(binance_symbol);
    if (send_subscribe_message(stream)) {
        subscribed_symbols_.insert(symbol);
        utils::Logger::debug("Subscribed to trades for {}", symbol);
        return true;
    }
    
    return false;
}

bool BinanceAdapter::subscribe_multiple(const std::vector<SubscriptionRequest>& requests) {
    std::vector<std::string> streams;
    
    for (const auto& req : requests) {
        std::string binance_symbol = to_binance_symbol(req.symbol);
        if (binance_symbol.empty()) {
            utils::Logger::warn("Skipping unsupported symbol: {}", req.symbol);
            continue;
        }
        
        if (req.ticker) {
            streams.push_back(binance_utils::build_ticker_stream(binance_symbol));
        }
        
        if (req.orderbook) {
            int depth = binance_utils::validate_orderbook_depth(req.orderbook_depth);
            streams.push_back(binance_utils::build_orderbook_stream(binance_symbol, depth));
            orderbook_depths_[req.symbol] = depth;
        }
        
        if (req.trades) {
            streams.push_back(binance_utils::build_trade_stream(binance_symbol));
        }
        
        subscribed_symbols_.insert(req.symbol);
    }
    
    if (streams.empty()) {
        handle_error("No valid streams to subscribe to");
        return false;
    }
    
    // Subscribe to combined stream
    std::string combined_stream = binance_utils::build_combined_stream(streams);
    return send_subscribe_message(combined_stream);
}

std::vector<types::Ticker> BinanceAdapter::get_all_tickers() {
    std::vector<types::Ticker> tickers;
    
    try {
        auto response = make_rest_request("/api/v3/ticker/24hr");
        if (!response.success) {
            handle_error("Failed to get all tickers: " + response.error_message);
            return tickers;
        }
        
        auto json = nlohmann::json::parse(response.body);
        if (!json.is_array()) {
            handle_error("Invalid response format for all tickers");
            return tickers;
        }
        
        for (const auto& ticker_json : json) {
            try {
                std::string binance_symbol = binance_utils::safe_get_string(ticker_json, "symbol");
                std::string standard_symbol = from_binance_symbol(binance_symbol);
                
                if (standard_symbol.empty()) continue; // Skip unsupported symbols
                
                types::Ticker ticker;
                ticker.symbol = standard_symbol;
                ticker.exchange = get_exchange_id();
                ticker.bid = std::stod(binance_utils::safe_get_string(ticker_json, "bidPrice", "0"));
                ticker.ask = std::stod(binance_utils::safe_get_string(ticker_json, "askPrice", "0"));
                ticker.last = std::stod(binance_utils::safe_get_string(ticker_json, "lastPrice", "0"));
                ticker.volume_24h = std::stod(binance_utils::safe_get_string(ticker_json, "volume", "0"));
                ticker.timestamp = std::chrono::system_clock::now();
                
                tickers.push_back(ticker);
                
            } catch (const std::exception& e) {
                utils::Logger::warn("Failed to parse ticker data: {}", e.what());
            }
        }
        
        utils::Logger::debug("Retrieved {} tickers from Binance", tickers.size());
        
    } catch (const std::exception& e) {
        handle_error("Exception getting all tickers: " + std::string(e.what()));
    }
    
    return tickers;
}

types::Ticker BinanceAdapter::get_ticker(const std::string& symbol) {
    types::Ticker ticker;
    
    try {
        std::string binance_symbol = to_binance_symbol(symbol);
        if (binance_symbol.empty()) {
            handle_error("Unsupported symbol: " + symbol);
            return ticker;
        }
        
        std::unordered_map<std::string, std::string> params = {{"symbol", binance_symbol}};
        auto response = make_rest_request("/api/v3/ticker/24hr", params);
        
        if (!response.success) {
            handle_error("Failed to get ticker for " + symbol + ": " + response.error_message);
            return ticker;
        }
        
        auto json = nlohmann::json::parse(response.body);
        
        ticker.symbol = symbol;
        ticker.exchange = get_exchange_id();
        ticker.bid = std::stod(binance_utils::safe_get_string(json, "bidPrice", "0"));
        ticker.ask = std::stod(binance_utils::safe_get_string(json, "askPrice", "0"));
        ticker.last = std::stod(binance_utils::safe_get_string(json, "lastPrice", "0"));
        ticker.volume_24h = std::stod(binance_utils::safe_get_string(json, "volume", "0"));
        ticker.timestamp = std::chrono::system_clock::now();
        
        utils::Logger::debug("Retrieved ticker for {}: last={}, bid={}, ask={}", 
                           symbol, ticker.last, ticker.bid, ticker.ask);
        
    } catch (const std::exception& e) {
        handle_error("Exception getting ticker for " + symbol + ": " + std::string(e.what()));
    }
    
    return ticker;
}

std::vector<std::string> BinanceAdapter::get_supported_symbols() {
    std::vector<std::string> symbols;
    for (const auto& [standard, binance] : SYMBOL_MAPPING) {
        symbols.push_back(standard);
    }
    return symbols;
}

// Callback setters
void BinanceAdapter::set_ticker_callback(TickerCallback callback) {
    ticker_callback_ = callback;
}

void BinanceAdapter::set_orderbook_callback(OrderBookCallback callback) {
    orderbook_callback_ = callback;
}

void BinanceAdapter::set_trade_callback(TradeCallback callback) {
    trade_callback_ = callback;
}

void BinanceAdapter::set_connection_status_callback(ConnectionStatusCallback callback) {
    connection_callback_ = callback;
}

// Statistics
size_t BinanceAdapter::get_messages_received() const {
    return messages_received_;
}

size_t BinanceAdapter::get_messages_per_second() const {
    // This would need proper implementation with time-window tracking
    return 0;
}

std::chrono::milliseconds BinanceAdapter::get_average_latency() const {
    if (http_client_) {
        return http_client_->get_average_latency();
    }
    return std::chrono::milliseconds(0);
}

std::chrono::milliseconds BinanceAdapter::get_last_message_time() const {
    return last_message_time_;
}

size_t BinanceAdapter::get_subscribed_symbols_count() const {
    return subscribed_symbols_.size();
}

std::string BinanceAdapter::get_last_error() const {
    return last_error_;
}

void BinanceAdapter::clear_error() {
    last_error_.clear();
}

bool BinanceAdapter::can_make_request() const {
    return rate_limiter_ ? rate_limiter_->can_make_request() : true;
}

void BinanceAdapter::record_request() {
    if (rate_limiter_) {
        rate_limiter_->record_request();
    }
}

std::chrono::milliseconds BinanceAdapter::get_next_request_delay() const {
    return rate_limiter_ ? rate_limiter_->get_delay_until_next_request() : 
                          std::chrono::milliseconds(0);
}

// Protected methods
void BinanceAdapter::notify_ticker_update(const types::Ticker& ticker) {
    if (ticker_callback_) {
        ticker_callback_(ticker);
    }
}

void BinanceAdapter::notify_connection_status_change(bool connected) {
    if (connection_callback_) {
        connection_callback_(get_exchange_id(), connected);
    }
}

void BinanceAdapter::handle_error(const std::string& error_message) {
    last_error_ = error_message;
    utils::Logger::error("Binance adapter error: {}", error_message);
}

// Private helper methods
std::string BinanceAdapter::to_binance_symbol(const std::string& symbol) const {
    auto it = SYMBOL_MAPPING.find(symbol);
    return it != SYMBOL_MAPPING.end() ? it->second : "";
}

std::string BinanceAdapter::from_binance_symbol(const std::string& binance_symbol) const {
    for (const auto& [standard, binance] : SYMBOL_MAPPING) {
        if (binance == binance_symbol) {
            return standard;
        }
    }
    return "";
}

// Register the adapter
REGISTER_EXCHANGE("binance", BinanceAdapter);

// Utility functions implementation
namespace binance_utils {

std::string normalize_symbol(const std::string& symbol) {
    std::string normalized = symbol;
    std::transform(normalized.begin(), normalized.end(), normalized.begin(), ::toupper);
    return normalized;
}

std::string build_ticker_stream(const std::string& symbol) {
    return symbol + "@ticker";
}

std::string build_orderbook_stream(const std::string& symbol, int depth) {
    return symbol + "@depth" + std::to_string(depth);
}

std::string build_trade_stream(const std::string& symbol) {
    return symbol + "@trade";
}

int validate_orderbook_depth(int depth) {
    std::vector<int> valid_depths = {5, 10, 20, 50, 100, 500, 1000};
    auto it = std::lower_bound(valid_depths.begin(), valid_depths.end(), depth);
    return it != valid_depths.end() ? *it : 20; // Default to 20
}

std::string safe_get_string(const nlohmann::json& json, const std::string& key, 
                           const std::string& default_value) {
    return json.contains(key) && json[key].is_string() ? json[key].get<std::string>() : default_value;
}

double safe_get_double(const nlohmann::json& json, const std::string& key, double default_value) {
    if (json.contains(key)) {
        if (json[key].is_number()) {
            return json[key].get<double>();
        } else if (json[key].is_string()) {
            try {
                return std::stod(json[key].get<std::string>());
            } catch (...) {
                return default_value;
            }
        }
    }
    return default_value;
}

} // namespace binance_utils

} // namespace price_collector
} // namespace ats