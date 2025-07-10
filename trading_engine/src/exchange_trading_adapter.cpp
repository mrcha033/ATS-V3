#include "exchange_trading_adapter.hpp"
#include "exchange/binance_exchange.hpp"
#include "exchange/upbit_exchange.hpp"
#include "utils/logger.hpp"
#include "utils/json_parser.hpp"
#include <nlohmann/json.hpp>
#include <curl/curl.h>
#include <openssl/hmac.h>
#include <openssl/sha.h>
#include <iomanip>
#include <sstream>
#include <thread>
#include <chrono>

namespace ats {
namespace trading_engine {

// ExchangeTradingAdapter implementation
ExchangeTradingAdapter::ExchangeTradingAdapter(std::shared_ptr<ats::ExchangeInterface> exchange)
    : exchange_(exchange) {
    utils::Logger::info("Created trading adapter for exchange: {}", exchange_->get_name());
}

ExchangeTradingAdapter::~ExchangeTradingAdapter() = default;

std::string ExchangeTradingAdapter::place_order(const types::Order& order) {
    try {
        ats::Order legacy_order = convert_to_legacy_order(order);
        ats::OrderResult result = exchange_->place_order(legacy_order);
        
        std::string order_id = "ORDER_" + std::to_string(std::chrono::duration_cast<std::chrono::milliseconds>(
            std::chrono::system_clock::now().time_since_epoch()).count());
        
        // Track the order
        {
            std::unique_lock<std::shared_mutex> lock(mutex_);
            order_tracking_[order_id] = create_order_details(order_id, order);
        }
        
        utils::Logger::info("Placed order: {} for symbol: {}", order_id, order.symbol);
        return order_id;
        
    } catch (const std::exception& e) {
        last_error_ = "Failed to place order: " + std::string(e.what());
        utils::Logger::error(last_error_);
        return "";
    }
}

bool ExchangeTradingAdapter::cancel_order(const std::string& order_id) {
    try {
        std::unique_lock<std::shared_mutex> lock(mutex_);
        auto it = order_tracking_.find(order_id);
        if (it != order_tracking_.end()) {
            it->second.status = OrderExecutionStatus::CANCELED;
            utils::Logger::info("Canceled order: {}", order_id);
            return true;
        }
        return false;
    } catch (const std::exception& e) {
        last_error_ = "Failed to cancel order: " + std::string(e.what());
        utils::Logger::error(last_error_);
        return false;
    }
}

OrderExecutionDetails ExchangeTradingAdapter::get_order_status(const std::string& order_id) {
    std::shared_lock<std::shared_mutex> lock(mutex_);
    
    auto it = order_tracking_.find(order_id);
    if (it != order_tracking_.end()) {
        return it->second;
    }
    
    OrderExecutionDetails details;
    details.order_id = order_id;
    details.status = OrderExecutionStatus::FAILED;
    details.error_message = "Order not found";
    return details;
}

std::vector<OrderExecutionDetails> ExchangeTradingAdapter::get_active_orders() {
    std::shared_lock<std::shared_mutex> lock(mutex_);
    
    std::vector<OrderExecutionDetails> active_orders;
    for (const auto& [order_id, details] : order_tracking_) {
        if (details.status == OrderExecutionStatus::PENDING ||
            details.status == OrderExecutionStatus::SUBMITTED ||
            details.status == OrderExecutionStatus::PARTIALLY_FILLED) {
            active_orders.push_back(details);
        }
    }
    
    return active_orders;
}

std::string ExchangeTradingAdapter::place_conditional_order(const types::Order& order, 
                                                          const std::string& condition) {
    // For now, just place a regular order (conditional orders would need exchange-specific implementation)
    return place_order(order);
}

bool ExchangeTradingAdapter::modify_order(const std::string& order_id, double new_price, double new_quantity) {
    // Mock implementation - would need exchange-specific support
    std::unique_lock<std::shared_mutex> lock(mutex_);
    
    auto it = order_tracking_.find(order_id);
    if (it != order_tracking_.end()) {
        it->second.original_order.price = new_price;
        it->second.original_order.quantity = new_quantity;
        it->second.remaining_quantity = new_quantity;
        utils::Logger::info("Modified order: {} - new price: {}, new quantity: {}", 
                          order_id, new_price, new_quantity);
        return true;
    }
    
    return false;
}

std::vector<types::Balance> ExchangeTradingAdapter::get_account_balances() {
    return mock_get_balances();
}

types::Balance ExchangeTradingAdapter::get_balance(const types::Currency& currency) {
    return mock_get_balance(currency);
}

double ExchangeTradingAdapter::get_available_balance(const types::Currency& currency) {
    auto balance = get_balance(currency);
    return balance.available;
}

double ExchangeTradingAdapter::get_minimum_order_size(const std::string& symbol) {
    // Default minimum order sizes
    if (symbol.find("BTC") != std::string::npos) return 0.0001;
    if (symbol.find("ETH") != std::string::npos) return 0.001;
    return 1.0; // Default for other symbols
}

double ExchangeTradingAdapter::get_maximum_order_size(const std::string& symbol) {
    return 1000000.0; // Default large maximum
}

double ExchangeTradingAdapter::get_trading_fee(const std::string& symbol, bool is_maker) {
    // Default fee structure
    return is_maker ? 0.001 : 0.001; // 0.1% for both maker and taker
}

types::Ticker ExchangeTradingAdapter::get_current_ticker(const std::string& symbol) {
    try {
        ats::Price price = exchange_->get_price(symbol);
        return convert_from_legacy_price(price, symbol);
    } catch (const std::exception& e) {
        last_error_ = "Failed to get ticker: " + std::string(e.what());
        utils::Logger::error(last_error_);
        return types::Ticker{};
    }
}

std::vector<std::pair<double, double>> ExchangeTradingAdapter::get_order_book(const std::string& symbol, int depth) {
    return mock_get_order_book(symbol);
}

std::string ExchangeTradingAdapter::get_exchange_id() const {
    return exchange_->get_name();
}

bool ExchangeTradingAdapter::is_connected() const {
    return true; // Assume connected for legacy interface
}

std::chrono::milliseconds ExchangeTradingAdapter::get_average_latency() const {
    return average_latency_;
}

bool ExchangeTradingAdapter::is_market_open() const {
    return true; // Crypto markets are always open
}

std::string ExchangeTradingAdapter::get_last_error() const {
    return last_error_;
}

void ExchangeTradingAdapter::clear_error() {
    last_error_.clear();
}

bool ExchangeTradingAdapter::is_healthy() const {
    return last_error_.empty() && is_connected();
}

// Helper methods
ats::Order ExchangeTradingAdapter::convert_to_legacy_order(const types::Order& order) {
    ats::Order legacy_order;
    legacy_order.symbol = order.symbol;
    legacy_order.quantity = order.quantity;
    legacy_order.price = order.price;
    // Note: Legacy order structure may be different - this is a simplified conversion
    return legacy_order;
}

types::Ticker ExchangeTradingAdapter::convert_from_legacy_price(const ats::Price& price, const std::string& symbol) {
    types::Ticker ticker;
    ticker.symbol = symbol;
    ticker.exchange = get_exchange_id();
    ticker.last = price.value;
    ticker.bid = price.value * 0.999; // Mock bid slightly lower
    ticker.ask = price.value * 1.001; // Mock ask slightly higher
    ticker.volume = 1000.0; // Mock volume
    ticker.timestamp = std::chrono::system_clock::now();
    return ticker;
}

OrderExecutionDetails ExchangeTradingAdapter::create_order_details(const std::string& order_id, const types::Order& order) {
    OrderExecutionDetails details;
    details.order_id = order_id;
    details.exchange_order_id = order_id + "_EXCHANGE";
    details.original_order = order;
    details.status = OrderExecutionStatus::SUBMITTED;
    details.remaining_quantity = order.quantity;
    details.submitted_at = std::chrono::system_clock::now();
    details.last_updated = details.submitted_at;
    return details;
}

std::vector<types::Balance> ExchangeTradingAdapter::mock_get_balances() {
    std::vector<types::Balance> balances;
    
    // Mock some common cryptocurrency balances
    types::Balance btc_balance;
    btc_balance.currency = "BTC";
    btc_balance.total = 1.5;
    btc_balance.available = 1.2;
    btc_balance.locked = 0.3;
    balances.push_back(btc_balance);
    
    types::Balance usdt_balance;
    usdt_balance.currency = "USDT";
    usdt_balance.total = 50000.0;
    usdt_balance.available = 45000.0;
    usdt_balance.locked = 5000.0;
    balances.push_back(usdt_balance);
    
    return balances;
}

types::Balance ExchangeTradingAdapter::mock_get_balance(const types::Currency& currency) {
    types::Balance balance;
    balance.currency = currency;
    
    if (currency == "BTC") {
        balance.total = 1.5;
        balance.available = 1.2;
        balance.locked = 0.3;
    } else if (currency == "USDT" || currency == "USD") {
        balance.total = 50000.0;
        balance.available = 45000.0;
        balance.locked = 5000.0;
    } else {
        balance.total = 100.0;
        balance.available = 80.0;
        balance.locked = 20.0;
    }
    
    return balance;
}

std::vector<std::pair<double, double>> ExchangeTradingAdapter::mock_get_order_book(const std::string& symbol) {
    std::vector<std::pair<double, double>> order_book;
    
    // Mock order book with 10 levels
    double base_price = 50000.0; // Default base price
    for (int i = 0; i < 10; ++i) {
        double price = base_price - (i * 10); // Decreasing prices for bids
        double quantity = 1.0 + (i * 0.5);
        order_book.emplace_back(price, quantity);
    }
    
    return order_book;
}

// ExchangeTradingAdapterFactory implementation
std::unique_ptr<ExchangeTradingAdapter> ExchangeTradingAdapterFactory::create_binance_adapter(
    const ExchangeConfig& config, AppState* app_state) {
    
    auto exchange = create_exchange_interface("binance", config, app_state);
    return std::make_unique<ExchangeTradingAdapter>(exchange);
}

std::unique_ptr<ExchangeTradingAdapter> ExchangeTradingAdapterFactory::create_upbit_adapter(
    const ExchangeConfig& config, AppState* app_state) {
    
    auto exchange = create_exchange_interface("upbit", config, app_state);
    return std::make_unique<ExchangeTradingAdapter>(exchange);
}

std::unique_ptr<ExchangeTradingAdapter> ExchangeTradingAdapterFactory::create_adapter(
    const std::string& exchange_name, 
    const ExchangeConfig& config, 
    AppState* app_state) {
    
    auto exchange = create_exchange_interface(exchange_name, config, app_state);
    if (exchange) {
        return std::make_unique<ExchangeTradingAdapter>(exchange);
    }
    
    return nullptr;
}

std::shared_ptr<ats::ExchangeInterface> ExchangeTradingAdapterFactory::create_exchange_interface(
    const std::string& exchange_name,
    const ExchangeConfig& config,
    AppState* app_state) {
    
    if (exchange_name == "binance") {
        return std::make_shared<ats::BinanceExchange>(config, app_state);
    } else if (exchange_name == "upbit") {
        return std::make_shared<ats::UpbitExchange>(config, app_state);
    }
    
    utils::Logger::error("Unknown exchange: {}", exchange_name);
    return nullptr;
}

// ExchangeRestClient implementation
struct ExchangeRestClient::Implementation {
    std::string base_url;
    std::string api_key;
    std::string secret;
    
    // Rate limiting
    int requests_per_second = 10;
    std::chrono::steady_clock::time_point last_request_time;
    std::chrono::milliseconds request_interval{100};
    
    // CURL handle
    CURL* curl_handle = nullptr;
    
    Implementation() {
        curl_handle = curl_easy_init();
        last_request_time = std::chrono::steady_clock::now();
    }
    
    ~Implementation() {
        if (curl_handle) {
            curl_easy_cleanup(curl_handle);
        }
    }
};

ExchangeRestClient::ExchangeRestClient(const std::string& base_url, const std::string& api_key, const std::string& secret)
    : impl_(std::make_unique<Implementation>()) {
    impl_->base_url = base_url;
    impl_->api_key = api_key;
    impl_->secret = secret;
}

ExchangeRestClient::~ExchangeRestClient() = default;

static size_t WriteCallback(void* contents, size_t size, size_t nmemb, std::string* response) {
    response->append(static_cast<char*>(contents), size * nmemb);
    return size * nmemb;
}

std::string ExchangeRestClient::get(const std::string& endpoint, const std::map<std::string, std::string>& params) {
    if (!check_rate_limit()) {
        wait_for_rate_limit();
    }
    
    std::string url = impl_->base_url + endpoint;
    
    // Add query parameters
    if (!params.empty()) {
        url += "?";
        bool first = true;
        for (const auto& [key, value] : params) {
            if (!first) url += "&";
            url += key + "=" + value;
            first = false;
        }
    }
    
    std::string response;
    
    if (impl_->curl_handle) {
        curl_easy_setopt(impl_->curl_handle, CURLOPT_URL, url.c_str());
        curl_easy_setopt(impl_->curl_handle, CURLOPT_WRITEFUNCTION, WriteCallback);
        curl_easy_setopt(impl_->curl_handle, CURLOPT_WRITEDATA, &response);
        curl_easy_setopt(impl_->curl_handle, CURLOPT_TIMEOUT, 30L);
        
        // Add API key header
        struct curl_slist* headers = nullptr;
        std::string auth_header = "X-MBX-APIKEY: " + impl_->api_key;
        headers = curl_slist_append(headers, auth_header.c_str());
        curl_easy_setopt(impl_->curl_handle, CURLOPT_HTTPHEADER, headers);
        
        CURLcode res = curl_easy_perform(impl_->curl_handle);
        curl_slist_free_all(headers);
        
        if (res != CURLE_OK) {
            utils::Logger::error("CURL error: {}", curl_easy_strerror(res));
            return "";
        }
    }
    
    return response;
}

std::string ExchangeRestClient::post(const std::string& endpoint, const std::string& body, 
                                   const std::map<std::string, std::string>& headers) {
    if (!check_rate_limit()) {
        wait_for_rate_limit();
    }
    
    std::string url = impl_->base_url + endpoint;
    std::string response;
    
    if (impl_->curl_handle) {
        curl_easy_setopt(impl_->curl_handle, CURLOPT_URL, url.c_str());
        curl_easy_setopt(impl_->curl_handle, CURLOPT_POSTFIELDS, body.c_str());
        curl_easy_setopt(impl_->curl_handle, CURLOPT_WRITEFUNCTION, WriteCallback);
        curl_easy_setopt(impl_->curl_handle, CURLOPT_WRITEDATA, &response);
        curl_easy_setopt(impl_->curl_handle, CURLOPT_TIMEOUT, 30L);
        
        // Add headers
        struct curl_slist* header_list = nullptr;
        for (const auto& [key, value] : headers) {
            std::string header = key + ": " + value;
            header_list = curl_slist_append(header_list, header.c_str());
        }
        
        // Add API key header
        std::string auth_header = "X-MBX-APIKEY: " + impl_->api_key;
        header_list = curl_slist_append(header_list, auth_header.c_str());
        
        curl_easy_setopt(impl_->curl_handle, CURLOPT_HTTPHEADER, header_list);
        
        CURLcode res = curl_easy_perform(impl_->curl_handle);
        curl_slist_free_all(header_list);
        
        if (res != CURLE_OK) {
            utils::Logger::error("CURL error: {}", curl_easy_strerror(res));
            return "";
        }
    }
    
    return response;
}

void ExchangeRestClient::set_rate_limit(int requests_per_second) {
    impl_->requests_per_second = requests_per_second;
    impl_->request_interval = std::chrono::milliseconds(1000 / requests_per_second);
}

bool ExchangeRestClient::check_rate_limit() {
    auto now = std::chrono::steady_clock::now();
    auto time_since_last = now - impl_->last_request_time;
    return time_since_last >= impl_->request_interval;
}

void ExchangeRestClient::wait_for_rate_limit() {
    auto now = std::chrono::steady_clock::now();
    auto time_since_last = now - impl_->last_request_time;
    
    if (time_since_last < impl_->request_interval) {
        auto wait_time = impl_->request_interval - time_since_last;
        std::this_thread::sleep_for(wait_time);
    }
    
    impl_->last_request_time = std::chrono::steady_clock::now();
}

std::string ExchangeRestClient::create_signature(const std::string& message) {
    unsigned char* digest = HMAC(EVP_sha256(), impl_->secret.c_str(), impl_->secret.length(),
                                reinterpret_cast<const unsigned char*>(message.c_str()), message.length(),
                                nullptr, nullptr);
    
    std::ostringstream oss;
    for (int i = 0; i < SHA256_DIGEST_LENGTH; ++i) {
        oss << std::hex << std::setw(2) << std::setfill('0') << static_cast<int>(digest[i]);
    }
    
    return oss.str();
}

std::string ExchangeRestClient::create_timestamp() {
    auto now = std::chrono::system_clock::now();
    auto timestamp = std::chrono::duration_cast<std::chrono::milliseconds>(
        now.time_since_epoch()).count();
    return std::to_string(timestamp);
}

// BinanceTradingInterface implementation
BinanceTradingInterface::BinanceTradingInterface(const std::string& api_key, const std::string& secret, bool testnet)
    : exchange_id_("binance") {
    
    std::string base_url = testnet ? "https://testnet.binance.vision" : "https://api.binance.com";
    rest_client_ = std::make_unique<ExchangeRestClient>(base_url, api_key, secret);
    rest_client_->set_rate_limit(10); // 10 requests per second
    connected_ = true;
    
    utils::Logger::info("Initialized Binance trading interface (testnet: {})", testnet);
}

BinanceTradingInterface::~BinanceTradingInterface() = default;

std::string BinanceTradingInterface::place_order(const types::Order& order) {
    try {
        return place_binance_order(order);
    } catch (const std::exception& e) {
        last_error_ = "Failed to place Binance order: " + std::string(e.what());
        utils::Logger::error(last_error_);
        return "";
    }
}

std::string BinanceTradingInterface::place_binance_order(const types::Order& order) {
    std::map<std::string, std::string> params;
    params["symbol"] = symbol_to_binance_format(order.symbol);
    params["side"] = order_side_to_string(order.side);
    params["type"] = order_type_to_string(order.type);
    params["quantity"] = std::to_string(order.quantity);
    
    if (order.type == types::OrderType::LIMIT) {
        params["price"] = std::to_string(order.price);
        params["timeInForce"] = "GTC";
    }
    
    params["timestamp"] = rest_client_->create_timestamp();
    
    // Create query string for signature
    std::string query_string;
    bool first = true;
    for (const auto& [key, value] : params) {
        if (!first) query_string += "&";
        query_string += key + "=" + value;
        first = false;
    }
    
    // Add signature
    auto signed_headers = rest_client_->create_signed_headers("POST", "/api/v3/order", query_string);
    
    std::string response = rest_client_->post("/api/v3/order", query_string, signed_headers);
    
    if (!response.empty()) {
        try {
            nlohmann::json j = nlohmann::json::parse(response);
            if (j.contains("orderId")) {
                return std::to_string(j["orderId"].get<long>());
            }
        } catch (const std::exception& e) {
            utils::Logger::error("Failed to parse Binance order response: {}", e.what());
        }
    }
    
    return "";
}

std::string BinanceTradingInterface::order_side_to_string(types::OrderSide side) {
    return (side == types::OrderSide::BUY) ? "BUY" : "SELL";
}

std::string BinanceTradingInterface::order_type_to_string(types::OrderType type) {
    switch (type) {
        case types::OrderType::MARKET: return "MARKET";
        case types::OrderType::LIMIT: return "LIMIT";
        default: return "MARKET";
    }
}

std::string BinanceTradingInterface::symbol_to_binance_format(const std::string& symbol) {
    // Convert "BTC/USDT" to "BTCUSDT"
    std::string binance_symbol = symbol;
    size_t slash_pos = binance_symbol.find('/');
    if (slash_pos != std::string::npos) {
        binance_symbol.erase(slash_pos, 1);
    }
    return binance_symbol;
}

// Implement other required methods with mock implementations for now
bool BinanceTradingInterface::cancel_order(const std::string& order_id) {
    // Mock implementation
    utils::Logger::info("Mock cancel order: {}", order_id);
    return true;
}

OrderExecutionDetails BinanceTradingInterface::get_order_status(const std::string& order_id) {
    OrderExecutionDetails details;
    details.order_id = order_id;
    details.status = OrderExecutionStatus::FILLED;
    details.filled_quantity = 1.0;
    return details;
}

std::vector<OrderExecutionDetails> BinanceTradingInterface::get_active_orders() {
    return {}; // Mock empty list
}

std::string BinanceTradingInterface::place_conditional_order(const types::Order& order, const std::string& condition) {
    return place_order(order); // Mock implementation
}

bool BinanceTradingInterface::modify_order(const std::string& order_id, double new_price, double new_quantity) {
    return true; // Mock implementation
}

std::vector<types::Balance> BinanceTradingInterface::get_account_balances() {
    // Mock implementation
    std::vector<types::Balance> balances;
    types::Balance btc;
    btc.currency = "BTC";
    btc.total = 1.0;
    btc.available = 0.8;
    btc.locked = 0.2;
    balances.push_back(btc);
    return balances;
}

types::Balance BinanceTradingInterface::get_balance(const types::Currency& currency) {
    types::Balance balance;
    balance.currency = currency;
    balance.total = 1000.0;
    balance.available = 800.0;
    balance.locked = 200.0;
    return balance;
}

double BinanceTradingInterface::get_available_balance(const types::Currency& currency) {
    return get_balance(currency).available;
}

double BinanceTradingInterface::get_minimum_order_size(const std::string& symbol) {
    return 0.001; // Mock minimum
}

double BinanceTradingInterface::get_maximum_order_size(const std::string& symbol) {
    return 1000000.0; // Mock maximum
}

double BinanceTradingInterface::get_trading_fee(const std::string& symbol, bool is_maker) {
    return 0.001; // 0.1%
}

types::Ticker BinanceTradingInterface::get_current_ticker(const std::string& symbol) {
    types::Ticker ticker;
    ticker.symbol = symbol;
    ticker.exchange = "binance";
    ticker.last = 50000.0;
    ticker.bid = 49999.0;
    ticker.ask = 50001.0;
    ticker.volume = 1000.0;
    ticker.timestamp = std::chrono::system_clock::now();
    return ticker;
}

std::vector<std::pair<double, double>> BinanceTradingInterface::get_order_book(const std::string& symbol, int depth) {
    std::vector<std::pair<double, double>> order_book;
    // Mock order book
    for (int i = 0; i < depth; ++i) {
        order_book.emplace_back(50000.0 - i, 1.0 + i * 0.1);
    }
    return order_book;
}

std::string BinanceTradingInterface::get_exchange_id() const {
    return exchange_id_;
}

bool BinanceTradingInterface::is_connected() const {
    return connected_;
}

std::chrono::milliseconds BinanceTradingInterface::get_average_latency() const {
    return average_latency_;
}

bool BinanceTradingInterface::is_market_open() const {
    return true; // Crypto markets are always open
}

std::string BinanceTradingInterface::get_last_error() const {
    return last_error_;
}

void BinanceTradingInterface::clear_error() {
    last_error_.clear();
}

bool BinanceTradingInterface::is_healthy() const {
    return connected_ && last_error_.empty();
}

// Similar implementation for UpbitTradingInterface (simplified for brevity)
UpbitTradingInterface::UpbitTradingInterface(const std::string& access_key, const std::string& secret_key)
    : exchange_id_("upbit") {
    
    rest_client_ = std::make_unique<ExchangeRestClient>("https://api.upbit.com", access_key, secret_key);
    rest_client_->set_rate_limit(8); // 8 requests per second for Upbit
    connected_ = true;
    
    utils::Logger::info("Initialized Upbit trading interface");
}

UpbitTradingInterface::~UpbitTradingInterface() = default;

// Implement Upbit methods (mostly mock implementations for now)
std::string UpbitTradingInterface::place_order(const types::Order& order) {
    return "UPBIT_ORDER_" + std::to_string(std::chrono::duration_cast<std::chrono::milliseconds>(
        std::chrono::system_clock::now().time_since_epoch()).count());
}

bool UpbitTradingInterface::cancel_order(const std::string& order_id) { return true; }
OrderExecutionDetails UpbitTradingInterface::get_order_status(const std::string& order_id) {
    OrderExecutionDetails details;
    details.order_id = order_id;
    details.status = OrderExecutionStatus::FILLED;
    return details;
}
std::vector<OrderExecutionDetails> UpbitTradingInterface::get_active_orders() { return {}; }
std::string UpbitTradingInterface::place_conditional_order(const types::Order& order, const std::string& condition) { return place_order(order); }
bool UpbitTradingInterface::modify_order(const std::string& order_id, double new_price, double new_quantity) { return true; }
std::vector<types::Balance> UpbitTradingInterface::get_account_balances() {
    std::vector<types::Balance> balances;
    types::Balance krw;
    krw.currency = "KRW";
    krw.total = 1000000.0;
    krw.available = 800000.0;
    krw.locked = 200000.0;
    balances.push_back(krw);
    return balances;
}
types::Balance UpbitTradingInterface::get_balance(const types::Currency& currency) {
    types::Balance balance;
    balance.currency = currency;
    balance.total = 1000000.0;
    balance.available = 800000.0;
    balance.locked = 200000.0;
    return balance;
}
double UpbitTradingInterface::get_available_balance(const types::Currency& currency) { return get_balance(currency).available; }
double UpbitTradingInterface::get_minimum_order_size(const std::string& symbol) { return 5000.0; } // 5000 KRW minimum
double UpbitTradingInterface::get_maximum_order_size(const std::string& symbol) { return 100000000.0; }
double UpbitTradingInterface::get_trading_fee(const std::string& symbol, bool is_maker) { return 0.0005; } // 0.05%
types::Ticker UpbitTradingInterface::get_current_ticker(const std::string& symbol) {
    types::Ticker ticker;
    ticker.symbol = symbol;
    ticker.exchange = "upbit";
    ticker.last = 65000000.0; // KRW price
    ticker.bid = 64995000.0;
    ticker.ask = 65005000.0;
    ticker.volume = 100.0;
    ticker.timestamp = std::chrono::system_clock::now();
    return ticker;
}
std::vector<std::pair<double, double>> UpbitTradingInterface::get_order_book(const std::string& symbol, int depth) {
    std::vector<std::pair<double, double>> order_book;
    for (int i = 0; i < depth; ++i) {
        order_book.emplace_back(65000000.0 - i * 1000, 0.1 + i * 0.01);
    }
    return order_book;
}
std::string UpbitTradingInterface::get_exchange_id() const { return exchange_id_; }
bool UpbitTradingInterface::is_connected() const { return connected_; }
std::chrono::milliseconds UpbitTradingInterface::get_average_latency() const { return average_latency_; }
bool UpbitTradingInterface::is_market_open() const { return true; }
std::string UpbitTradingInterface::get_last_error() const { return last_error_; }
void UpbitTradingInterface::clear_error() { last_error_.clear(); }
bool UpbitTradingInterface::is_healthy() const { return connected_ && last_error_.empty(); }

std::string UpbitTradingInterface::order_side_to_string(types::OrderSide side) {
    return (side == types::OrderSide::BUY) ? "bid" : "ask";
}

std::string UpbitTradingInterface::order_type_to_string(types::OrderType type) {
    switch (type) {
        case types::OrderType::MARKET: return "market";
        case types::OrderType::LIMIT: return "limit";
        default: return "market";
    }
}

std::string UpbitTradingInterface::symbol_to_upbit_format(const std::string& symbol) {
    // Convert "BTC/KRW" to "KRW-BTC"
    size_t slash_pos = symbol.find('/');
    if (slash_pos != std::string::npos) {
        std::string base = symbol.substr(0, slash_pos);
        std::string quote = symbol.substr(slash_pos + 1);
        return quote + "-" + base;
    }
    return symbol;
}

} // namespace trading_engine
} // namespace ats