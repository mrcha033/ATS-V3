#include "exchange/base_exchange_plugin.hpp"
#include <algorithm>
#include <numeric>
#include <deque>
#include <shared_mutex>

namespace ats {
namespace exchange {

// RateLimiter implementation

RateLimiter::RateLimiter(int requests_per_minute)
    : max_requests_(requests_per_minute), window_duration_(1) {
}

bool RateLimiter::can_make_request() const {
    std::lock_guard<std::mutex> lock(mutex_);
    
    auto now = std::chrono::steady_clock::now();
    auto cutoff = now - window_duration_;
    
    // Remove old entries
    while (!request_times_.empty() && request_times_.front() < cutoff) {
        request_times_.pop_front();
    }
    
    return request_times_.size() < static_cast<size_t>(max_requests_);
}

void RateLimiter::record_request() {
    std::lock_guard<std::mutex> lock(mutex_);
    request_times_.push_back(std::chrono::steady_clock::now());
}

std::chrono::milliseconds RateLimiter::get_next_request_delay() const {
    std::lock_guard<std::mutex> lock(mutex_);
    
    if (request_times_.size() < static_cast<size_t>(max_requests_)) {
        return std::chrono::milliseconds(0);
    }
    
    auto now = std::chrono::steady_clock::now();
    auto oldest_request = request_times_.front();
    auto elapsed = std::chrono::duration_cast<std::chrono::milliseconds>(now - oldest_request);
    auto window_ms = std::chrono::duration_cast<std::chrono::milliseconds>(window_duration_);
    
    if (elapsed >= window_ms) {
        return std::chrono::milliseconds(0);
    }
    
    return window_ms - elapsed;
}

void RateLimiter::set_rate_limit(int requests_per_minute) {
    std::lock_guard<std::mutex> lock(mutex_);
    max_requests_ = requests_per_minute;
}

// BaseExchangePlugin implementation

BaseExchangePlugin::BaseExchangePlugin()
    : connection_status_(types::ConnectionStatus::DISCONNECTED)
    , initialized_(false)
    , started_(false)
    , message_count_(0)
    , last_message_time_ns_(std::chrono::steady_clock::now().time_since_epoch().count())
    , rate_limiter_(std::make_unique<RateLimiter>()) {
}

BaseExchangePlugin::~BaseExchangePlugin() {
    if (started_) {
        stop();
    }
    cleanup();
}

bool BaseExchangePlugin::initialize(const types::ExchangeConfig& config) {
    if (initialized_) {
        return true;
    }
    
    try {
        config_ = config;
        
        // Configure rate limiter
        const auto& metadata = get_cached_metadata();
        rate_limiter_->set_rate_limit(metadata.rate_limit_per_minute);
        
        // Call derived class initialization
        if (!do_initialize(config)) {
            set_error("Plugin-specific initialization failed");
            return false;
        }
        
        initialized_ = true;
        log_info("Plugin initialized successfully");
        return true;
        
    } catch (const std::exception& e) {
        std::string error = "Exception during initialization: " + std::string(e.what());
        set_error(error);
        log_error(error);
        return false;
    }
}

bool BaseExchangePlugin::start() {
    if (!initialized_) {
        set_error("Plugin not initialized");
        return false;
    }
    
    if (started_) {
        return true;
    }
    
    try {
        if (!do_start()) {
            set_error("Plugin-specific start failed");
            return false;
        }
        
        started_ = true;
        log_info("Plugin started successfully");
        return true;
        
    } catch (const std::exception& e) {
        std::string error = "Exception during start: " + std::string(e.what());
        set_error(error);
        log_error(error);
        return false;
    }
}

void BaseExchangePlugin::stop() {
    if (!started_) {
        return;
    }
    
    try {
        do_stop();
        started_ = false;
        set_connection_status(types::ConnectionStatus::DISCONNECTED);
        log_info("Plugin stopped");
        
    } catch (const std::exception& e) {
        std::string error = "Exception during stop: " + std::string(e.what());
        set_error(error);
        log_error(error);
    }
}

void BaseExchangePlugin::cleanup() {
    try {
        do_cleanup();
        initialized_ = false;
        log_info("Plugin cleaned up");
        
    } catch (const std::exception& e) {
        std::string error = "Exception during cleanup: " + std::string(e.what());
        log_error(error);
    }
}

std::string BaseExchangePlugin::get_plugin_id() const {
    return get_cached_metadata().plugin_id;
}

std::string BaseExchangePlugin::get_version() const {
    return get_cached_metadata().version;
}

bool BaseExchangePlugin::connect() {
    if (!started_) {
        set_error("Plugin not started");
        return false;
    }
    
    if (connection_status_ == types::ConnectionStatus::CONNECTED) {
        return true;
    }
    
    try {
        set_connection_status(types::ConnectionStatus::CONNECTING);
        
        if (!do_connect()) {
            set_connection_status(types::ConnectionStatus::DISCONNECTED);
            set_error("Connection failed");
            return false;
        }
        
        set_connection_status(types::ConnectionStatus::CONNECTED);
        notify_connection_change(true);
        log_info("Connected successfully");
        return true;
        
    } catch (const std::exception& e) {
        set_connection_status(types::ConnectionStatus::ERROR);
        std::string error = "Exception during connection: " + std::string(e.what());
        set_error(error);
        log_error(error);
        return false;
    }
}

void BaseExchangePlugin::disconnect() {
    if (connection_status_ == types::ConnectionStatus::DISCONNECTED) {
        return;
    }
    
    try {
        do_disconnect();
        set_connection_status(types::ConnectionStatus::DISCONNECTED);
        notify_connection_change(false);
        log_info("Disconnected");
        
    } catch (const std::exception& e) {
        std::string error = "Exception during disconnection: " + std::string(e.what());
        set_error(error);
        log_error(error);
    }
}

bool BaseExchangePlugin::is_connected() const {
    return connection_status_ == types::ConnectionStatus::CONNECTED;
}

types::ConnectionStatus BaseExchangePlugin::get_connection_status() const {
    return connection_status_;
}

bool BaseExchangePlugin::subscribe_ticker(const std::string& symbol) {
    if (!is_connected()) {
        set_error("Not connected");
        return false;
    }
    
    try {
        return do_subscribe_ticker(symbol);
    } catch (const std::exception& e) {
        std::string error = "Exception subscribing to ticker: " + std::string(e.what());
        set_error(error);
        log_error(error);
        return false;
    }
}

bool BaseExchangePlugin::subscribe_orderbook(const std::string& symbol, int depth) {
    if (!is_connected()) {
        set_error("Not connected");
        return false;
    }
    
    try {
        return do_subscribe_orderbook(symbol, depth);
    } catch (const std::exception& e) {
        std::string error = "Exception subscribing to orderbook: " + std::string(e.what());
        set_error(error);
        log_error(error);
        return false;
    }
}

bool BaseExchangePlugin::subscribe_trades(const std::string& symbol) {
    if (!is_connected()) {
        set_error("Not connected");
        return false;
    }
    
    try {
        return do_subscribe_trades(symbol);
    } catch (const std::exception& e) {
        std::string error = "Exception subscribing to trades: " + std::string(e.what());
        set_error(error);
        log_error(error);
        return false;
    }
}

bool BaseExchangePlugin::unsubscribe_ticker(const std::string& symbol) {
    try {
        return do_unsubscribe_ticker(symbol);
    } catch (const std::exception& e) {
        std::string error = "Exception unsubscribing from ticker: " + std::string(e.what());
        set_error(error);
        log_error(error);
        return false;
    }
}

bool BaseExchangePlugin::unsubscribe_orderbook(const std::string& symbol) {
    try {
        return do_unsubscribe_orderbook(symbol);
    } catch (const std::exception& e) {
        std::string error = "Exception unsubscribing from orderbook: " + std::string(e.what());
        set_error(error);
        log_error(error);
        return false;
    }
}

bool BaseExchangePlugin::unsubscribe_trades(const std::string& symbol) {
    try {
        return do_unsubscribe_trades(symbol);
    } catch (const std::exception& e) {
        std::string error = "Exception unsubscribing from trades: " + std::string(e.what());
        set_error(error);
        log_error(error);
        return false;
    }
}

bool BaseExchangePlugin::unsubscribe_all() {
    try {
        return do_unsubscribe_all();
    } catch (const std::exception& e) {
        std::string error = "Exception unsubscribing from all: " + std::string(e.what());
        set_error(error);
        log_error(error);
        return false;
    }
}

std::vector<types::Ticker> BaseExchangePlugin::get_all_tickers() {
    try {
        record_request();
        return do_get_all_tickers();
    } catch (const std::exception& e) {
        std::string error = "Exception getting all tickers: " + std::string(e.what());
        set_error(error);
        log_error(error);
        return {};
    }
}

types::Ticker BaseExchangePlugin::get_ticker(const std::string& symbol) {
    try {
        record_request();
        return do_get_ticker(symbol);
    } catch (const std::exception& e) {
        std::string error = "Exception getting ticker: " + std::string(e.what());
        set_error(error);
        log_error(error);
        return {};
    }
}

std::vector<std::string> BaseExchangePlugin::get_supported_symbols() {
    try {
        record_request();
        return do_get_supported_symbols();
    } catch (const std::exception& e) {
        std::string error = "Exception getting supported symbols: " + std::string(e.what());
        set_error(error);
        log_error(error);
        return {};
    }
}

types::OrderBook BaseExchangePlugin::get_orderbook(const std::string& symbol, int depth) {
    try {
        record_request();
        return do_get_orderbook(symbol, depth);
    } catch (const std::exception& e) {
        std::string error = "Exception getting orderbook: " + std::string(e.what());
        set_error(error);
        log_error(error);
        return {};
    }
}

void BaseExchangePlugin::set_ticker_callback(TickerCallback callback) {
    std::lock_guard<std::mutex> lock(callback_mutex_);
    ticker_callback_ = callback;
}

void BaseExchangePlugin::set_orderbook_callback(OrderBookCallback callback) {
    std::lock_guard<std::mutex> lock(callback_mutex_);
    orderbook_callback_ = callback;
}

void BaseExchangePlugin::set_trade_callback(TradeCallback callback) {
    std::lock_guard<std::mutex> lock(callback_mutex_);
    trade_callback_ = callback;
}

void BaseExchangePlugin::set_connection_callback(ConnectionCallback callback) {
    std::lock_guard<std::mutex> lock(callback_mutex_);
    connection_callback_ = callback;
}

void BaseExchangePlugin::set_error_callback(ErrorCallback callback) {
    std::lock_guard<std::mutex> lock(callback_mutex_);
    error_callback_ = callback;
}

size_t BaseExchangePlugin::get_messages_received() const {
    return message_count_;
}

size_t BaseExchangePlugin::get_messages_per_second() const {
    auto now_ns = std::chrono::steady_clock::now().time_since_epoch().count();
    auto last_ns = last_message_time_ns_.load();
    auto elapsed_ns = now_ns - last_ns;
    auto elapsed_seconds = elapsed_ns / 1000000000;  // Convert nanoseconds to seconds
    
    if (elapsed_seconds > 0) {
        return message_count_ / elapsed_seconds;
    }
    
    return 0;
}

std::chrono::milliseconds BaseExchangePlugin::get_average_latency() const {
    return calculate_average_latency();
}

std::string BaseExchangePlugin::get_last_error() const {
    std::lock_guard<std::mutex> lock(error_mutex_);
    return last_error_;
}

void BaseExchangePlugin::clear_error() {
    std::lock_guard<std::mutex> lock(error_mutex_);
    last_error_.clear();
}

bool BaseExchangePlugin::can_make_request() const {
    return rate_limiter_->can_make_request();
}

void BaseExchangePlugin::record_request() {
    rate_limiter_->record_request();
}

std::chrono::milliseconds BaseExchangePlugin::get_next_request_delay() const {
    return rate_limiter_->get_next_request_delay();
}

// Protected helper methods

void BaseExchangePlugin::set_connection_status(types::ConnectionStatus status) {
    connection_status_ = status;
}

void BaseExchangePlugin::set_error(const std::string& error) {
    std::lock_guard<std::mutex> lock(error_mutex_);
    last_error_ = error;
}

void BaseExchangePlugin::increment_message_count() {
    message_count_++;
    update_message_statistics();
}

void BaseExchangePlugin::update_latency(std::chrono::milliseconds latency) {
    std::lock_guard<std::mutex> lock(latency_mutex_);
    latency_samples_.push_back(latency);
    if (latency_samples_.size() > MAX_LATENCY_SAMPLES) {
        latency_samples_.pop_front();
    }
}

void BaseExchangePlugin::notify_ticker(const types::Ticker& ticker) {
    increment_message_count();
    
    std::lock_guard<std::mutex> lock(callback_mutex_);
    if (ticker_callback_) {
        try {
            ticker_callback_(ticker);
        } catch (const std::exception& e) {
            log_error("Exception in ticker callback: " + std::string(e.what()));
        }
    }
}

void BaseExchangePlugin::notify_orderbook(const types::OrderBook& orderbook) {
    increment_message_count();
    
    std::lock_guard<std::mutex> lock(callback_mutex_);
    if (orderbook_callback_) {
        try {
            orderbook_callback_(orderbook);
        } catch (const std::exception& e) {
            log_error("Exception in orderbook callback: " + std::string(e.what()));
        }
    }
}

void BaseExchangePlugin::notify_trade(const types::Trade& trade) {
    increment_message_count();
    
    std::lock_guard<std::mutex> lock(callback_mutex_);
    if (trade_callback_) {
        try {
            trade_callback_(trade);
        } catch (const std::exception& e) {
            log_error("Exception in trade callback: " + std::string(e.what()));
        }
    }
}

void BaseExchangePlugin::notify_connection_change(bool connected) {
    std::lock_guard<std::mutex> lock(callback_mutex_);
    if (connection_callback_) {
        try {
            connection_callback_(get_plugin_id(), connected);
        } catch (const std::exception& e) {
            log_error("Exception in connection callback: " + std::string(e.what()));
        }
    }
}

void BaseExchangePlugin::notify_error(const std::string& error) {
    set_error(error);
    
    std::lock_guard<std::mutex> lock(callback_mutex_);
    if (error_callback_) {
        try {
            error_callback_(get_plugin_id(), error);
        } catch (const std::exception& e) {
            log_error("Exception in error callback: " + std::string(e.what()));
        }
    }
}

void BaseExchangePlugin::log_info(const std::string& message) const {
    utils::Logger::info("[" + get_plugin_name() + "] " + message);
}

void BaseExchangePlugin::log_warning(const std::string& message) const {
    utils::Logger::warn("[" + get_plugin_name() + "] " + message);
}

void BaseExchangePlugin::log_error(const std::string& message) const {
    utils::Logger::error("[" + get_plugin_name() + "] " + message);
}

void BaseExchangePlugin::log_debug(const std::string& message) const {
    utils::Logger::debug("[" + get_plugin_name() + "] " + message);
}

std::string BaseExchangePlugin::format_symbol(const std::string& symbol) const {
    // Basic symbol formatting - can be overridden by derived classes
    std::string formatted = symbol;
    std::transform(formatted.begin(), formatted.end(), formatted.begin(), ::toupper);
    return formatted;
}

bool BaseExchangePlugin::validate_symbol(const std::string& symbol) const {
    if (symbol.empty()) {
        return false;
    }
    
    // Check if symbol is in supported list
    const auto& supported = get_cached_metadata().supported_symbols;
    if (!supported.empty()) {
        return std::find(supported.begin(), supported.end(), format_symbol(symbol)) != supported.end();
    }
    
    return true;
}

std::string BaseExchangePlugin::get_plugin_name() const {
    const auto& metadata = get_cached_metadata();
    return metadata.plugin_name.empty() ? metadata.plugin_id : metadata.plugin_name;
}

// Private helper methods

void BaseExchangePlugin::update_message_statistics() {
    last_message_time_ns_ = std::chrono::steady_clock::now().time_since_epoch().count();
    message_count_++;
}

std::chrono::milliseconds BaseExchangePlugin::calculate_average_latency() const {
    std::lock_guard<std::mutex> lock(latency_mutex_);
    
    if (latency_samples_.empty()) {
        return std::chrono::milliseconds(0);
    }
    
    auto total = std::accumulate(latency_samples_.begin(), latency_samples_.end(), 
                                std::chrono::milliseconds(0),
                                [](const std::chrono::milliseconds& a, const std::chrono::milliseconds& b) {
                                    return a + b;
                                });
    
    return std::chrono::milliseconds(total.count() / latency_samples_.size());
}

const ExchangePluginMetadata& BaseExchangePlugin::get_cached_metadata() const {
    std::call_once(metadata_flag_, [this]() {
        cached_metadata_ = create_metadata();
    });
    return cached_metadata_;
}

} // namespace exchange
} // namespace ats