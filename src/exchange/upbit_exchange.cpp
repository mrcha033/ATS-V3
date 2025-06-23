#include "upbit_exchange.hpp"

// Prevent Windows macro pollution before any other headers
#if defined(_WIN32) && !defined(WIN32_LEAN_AND_MEAN)
    #define WIN32_LEAN_AND_MEAN
#endif
#if defined(_WIN32) && !defined(NOMINMAX)
    #define NOMINMAX
#endif

#ifdef HAVE_JWT_CPP
    #include <jwt-cpp/jwt.h>
#endif
#ifdef HAVE_UUID
    #include <uuid/uuid.h>
#endif
#ifdef HAVE_OPENSSL
    #include <openssl/sha.h>
#endif
#include <iomanip>
#include <sstream>
#include <thread>
#include <chrono>

// For compatibility with old Json::Value usage
namespace Json {
    using Value = ats::json::JsonValue;
}

namespace ats {

// Static constants
const std::string UpbitExchange::BASE_URL = "https://api.upbit.com";
const std::string UpbitExchange::WS_URL = "wss://api.upbit.com/websocket/v1";
const int UpbitExchange::MAX_REQUESTS_PER_SECOND = 10;
const int UpbitExchange::MAX_REQUESTS_PER_MINUTE = 100;

UpbitExchange::UpbitExchange(const std::string& access_key, const std::string& secret_key)
    : access_key_(access_key)
    , secret_key_(secret_key)
    , rest_client_(std::make_unique<RestClient>())
    , ws_client_(std::make_unique<WebSocketClient>())
    , connected_(false)
    , requests_this_second_(0)
    , last_request_time_(std::chrono::steady_clock::now()) {
    
    InitializeConnection();
    LoadSymbolMappings();
}

bool UpbitExchange::Connect() {
    try {
        // Initialize REST client - Fixed: Use proper API
        rest_client_->SetBaseUrl(BASE_URL);
        LOG_INFO("Initialized REST client for Upbit");

        // Test connection with server time  
        auto response = rest_client_->Get(BASE_URL + "/v1/market/all");
        if (!response.IsSuccess()) {
            LOG_ERROR("Failed to connect to Upbit API: {}", response.error_message);
            return false;
        }

        // Initialize WebSocket if needed
        if (!access_key_.empty() && !secret_key_.empty()) {
            ws_client_->SetMessageCallback([this](const std::string& msg) {
                OnWebSocketMessage(msg);
            });
            ws_client_->SetErrorCallback([this](const std::string& error) {
                OnWebSocketError(error);
            });
        }

        connected_ = true;
        LOG_INFO("Successfully connected to Upbit exchange");
        return true;
        
    } catch (const std::exception& e) {
        LOG_ERROR("Exception in Upbit connection: {}", e.what());
        return false;
    }
}

void UpbitExchange::Disconnect() {
    try {
        if (ws_client_->IsConnected()) {
            ws_client_->Disconnect();
        }
        
        connected_ = false;
        LOG_INFO("Disconnected from Upbit exchange");
        
    } catch (const std::exception& e) {
        LOG_ERROR("Exception in Upbit disconnection: {}", e.what());
    }
}

bool UpbitExchange::IsConnected() const {
    return connected_.load();
}

bool UpbitExchange::InitializeConnection() {
    // Set default rate limiting
    last_request_time_ = std::chrono::steady_clock::now();
    requests_this_second_ = 0;
    return true;
}

void UpbitExchange::LoadSymbolMappings() {
    // Load market list to build symbol mappings
    auto response = rest_client_->Get(BASE_URL + "/v1/market/all");
    if (response.IsSuccess()) {
        try {
            JsonValue json_response = ats::json::ParseJson(response.body);
            if (ats::json::IsArray(json_response)) {
                auto markets = json_response;
                for (const auto& market : markets) {
                    if (ats::json::IsObject(market)) {
                        if (ats::json::HasKey(market, "market")) {
                            std::string upbit_symbol = ats::json::GetString(ats::json::GetValue(market, "market"));
                            // Convert KRW-BTC to BTCKRW format
                            size_t dash_pos = upbit_symbol.find('-');
                            if (dash_pos != std::string::npos) {
                                std::string quote = upbit_symbol.substr(0, dash_pos);
                                std::string base = upbit_symbol.substr(dash_pos + 1);
                                std::string normalized = base + quote;
                                
                                symbol_map_[normalized] = upbit_symbol;
                                reverse_symbol_map_[upbit_symbol] = normalized;
                            }
                        }
                    }
                }
            }
        } catch (const std::exception& e) {
            LOG_ERROR("Failed to parse market data: {}", e.what());
        }
    }
}

std::string UpbitExchange::MapSymbol(const std::string& symbol) {
    auto it = symbol_map_.find(symbol);
    return (it != symbol_map_.end()) ? it->second : symbol;
}

std::string UpbitExchange::UnmapSymbol(const std::string& upbit_symbol) {
    auto it = reverse_symbol_map_.find(upbit_symbol);
    return (it != reverse_symbol_map_.end()) ? it->second : upbit_symbol;
}

std::string UpbitExchange::GenerateJWT(const std::string& query_string) {
    if (access_key_.empty() || secret_key_.empty()) {
        return "";
    }

    try {
        // Generate UUID for nonce (simplified for Windows compatibility)
        auto now = std::chrono::high_resolution_clock::now();
        auto timestamp = std::chrono::duration_cast<std::chrono::nanoseconds>(now.time_since_epoch()).count();
        std::string uuid_str = "uuid-" + std::to_string(timestamp);

#ifdef HAVE_JWT_CPP
        auto token = jwt::create()
            .set_algorithm("HS256")
            .set_header_claim("typ", jwt::claim(std::string("JWT")))
            .set_payload_claim("access_key", jwt::claim(access_key_))
            .set_payload_claim("nonce", jwt::claim(uuid_str));

        if (!query_string.empty()) {
#ifdef HAVE_OPENSSL
            // Generate SHA512 hash of query string
            unsigned char hash[SHA512_DIGEST_LENGTH];
            SHA512(reinterpret_cast<const unsigned char*>(query_string.c_str()), 
                   query_string.length(), hash);
            
            std::stringstream ss;
            for (int i = 0; i < SHA512_DIGEST_LENGTH; ++i) {
                ss << std::hex << std::setw(2) << std::setfill('0') << (int)hash[i];
            }
            
            token.set_payload_claim("query_hash", jwt::claim(ss.str()));
            token.set_payload_claim("query_hash_alg", jwt::claim(std::string("SHA512")));
#endif
        }

        return token.sign(jwt::algorithm::hs256{secret_key_});
#else
        LOG_WARNING("JWT library not available - using placeholder token");
        return "placeholder_jwt_token";
#endif
        
    } catch (const std::exception& e) {
        LOG_ERROR("Failed to generate JWT: {}", e.what());
        return "";
    }
}

std::unordered_map<std::string, std::string> UpbitExchange::GetAuthHeaders(const std::string& query_string) {
    std::unordered_map<std::string, std::string> headers;
    
    if (!access_key_.empty() && !secret_key_.empty()) {
        std::string jwt_token = GenerateJWT(query_string);
        if (!jwt_token.empty()) {
            headers["Authorization"] = "Bearer " + jwt_token;
        }
    }
    
    headers["Content-Type"] = "application/json";
    headers["User-Agent"] = "ATS-V3/1.0";
    
    return headers;
}

bool UpbitExchange::CheckRateLimit() {
    std::lock_guard<std::mutex> lock(rate_limit_mutex_);
    
    auto now = std::chrono::steady_clock::now();
    auto time_diff = std::chrono::duration_cast<std::chrono::seconds>(now - last_request_time_);
    
    // Reset counter if more than 1 second has passed
    if (time_diff.count() >= 1) {
        requests_this_second_ = 0;
        last_request_time_ = now;
    }
    
    // Check if we're within rate limits
    if (requests_this_second_.load() >= MAX_REQUESTS_PER_SECOND) {
        return false;
    }
    
    return true;
}

void UpbitExchange::UpdateRateLimit() {
    std::lock_guard<std::mutex> lock(rate_limit_mutex_);
    requests_this_second_++;
}

bool UpbitExchange::MakeRequest(const std::string& endpoint, const std::string& method,
                               const std::string& params, JsonValue& response) {
    if (!CheckRateLimit()) {
        std::this_thread::sleep_for(std::chrono::milliseconds(100));
        return MakeRequest(endpoint, method, params, response);
    }
    
    try {
        std::unordered_map<std::string, std::string> headers = {
            {"Content-Type", "application/json"},
            {"User-Agent", "ATS-V3/1.0"}
        };
        
        std::string full_url = BASE_URL + endpoint;
        if (!params.empty() && method == "GET") {
            full_url += "?" + params;
        }
        
        HttpResponse http_response;
        
        if (method == "GET") {
            http_response = rest_client_->Get(full_url, headers);
        } else if (method == "POST") {
            http_response = rest_client_->Post(full_url, params, headers);
        } else if (method == "DELETE") {
            http_response = rest_client_->Delete(full_url, headers);
        }
        
        UpdateRateLimit();
        
        if (!http_response.IsSuccess()) {
            LOG_ERROR("HTTP request failed for endpoint: {}", endpoint);
            return false;
        }
        
        try {
            response = ats::json::ParseJson(http_response.body);
        } catch (const std::exception& e) {
            LOG_ERROR("Failed to parse JSON response from {}: {}", endpoint, e.what());
            return false;
        }
        
        // Check for API errors
        if (response.is_object()) {
            if (response.contains("error")) {
                LOG_ERROR("API error: {}", response["error"].get<std::string>());
                return false;
            }
        }
        
        return true;
        
    } catch (const std::exception& e) {
        LOG_ERROR("Exception in MakeRequest: {}", e.what());
        return false;
    }
}

bool UpbitExchange::MakeAuthenticatedRequest(const std::string& endpoint, const std::string& method,
                                           const std::string& params, JsonValue& response) {
    if (access_key_.empty() || secret_key_.empty()) {
        LOG_ERROR("Authentication credentials not provided");
        return false;
    }
    
    if (!CheckRateLimit()) {
        std::this_thread::sleep_for(std::chrono::milliseconds(100));
        return MakeAuthenticatedRequest(endpoint, method, params, response);
    }
    
    try {
        std::string query_string = (method == "GET" && !params.empty()) ? params : "";
        auto headers = GetAuthHeaders(query_string);
        
        std::string full_url = BASE_URL + endpoint;
        if (!params.empty() && method == "GET") {
            full_url += "?" + params;
        }
        
        HttpResponse http_response;
        
        if (method == "GET") {
            http_response = rest_client_->Get(full_url, headers);
        } else if (method == "POST") {
            http_response = rest_client_->Post(full_url, params, headers);
        } else if (method == "DELETE") {
            http_response = rest_client_->Delete(full_url, headers);
        }
        
        UpdateRateLimit();
        
        if (!http_response.IsSuccess()) {
            LOG_ERROR("Authenticated HTTP request failed for endpoint: {}", endpoint);
            return false;
        }
        
        try {
            response = ats::json::ParseJson(http_response.body);
        } catch (const std::exception& e) {
            LOG_ERROR("Failed to parse JSON response from {}: {}", endpoint, e.what());
            return false;
        }
        
        // Check for API errors
        if (response.is_object()) {
            if (response.contains("error")) {
                LOG_ERROR("API error: {}", response["error"].get<std::string>());
                return false;
            }
        }
        
        return true;
        
    } catch (const std::exception& e) {
        LOG_ERROR("Exception in MakeAuthenticatedRequest: {}", e.what());
        return false;
    }
}

std::string UpbitExchange::PlaceOrder(const std::string& symbol, const std::string& side, 
                                      const std::string& type, double quantity, double price) {
    if (!IsConnected()) {
        LOG_ERROR("Not connected to Upbit exchange");
        return "";
    }
    
    try {
        std::string upbit_symbol = MapSymbol(symbol);
        if (upbit_symbol.empty()) {
            LOG_ERROR("Invalid symbol: " + symbol);
            return "";
        }
        
        // Build JSON parameters string manually since we don't have jsoncpp
        std::ostringstream params_stream;
        params_stream << "{";
        params_stream << "\"market\":\"" << upbit_symbol << "\",";
        params_stream << "\"side\":\"" << side << "\",";
        params_stream << "\"ord_type\":\"" << type << "\"";
        
        if (type == "limit") {
            params_stream << ",\"price\":\"" << price << "\"";
            params_stream << ",\"volume\":\"" << quantity << "\"";
        } else if (type == "market") {
            if (side == "bid") {
                params_stream << ",\"price\":\"" << (price * quantity) << "\"";
            } else {
                params_stream << ",\"volume\":\"" << quantity << "\"";
            }
        }
        params_stream << "}";
        
        std::string params_str = params_stream.str();
        
        JsonValue response;
        if (!MakeAuthenticatedRequest("/v1/orders", "POST", params_str, response)) {
            LOG_ERROR("Failed to place order on Upbit");
            return "";
        }
        
        // Check if response contains uuid
        if (response.is_object()) {
            if (response.contains("uuid")) {
                std::string order_id = response["uuid"].get<std::string>();
                LOG_INFO("Order placed successfully. Order ID: {}", order_id);
                return order_id;
            }
        }
        
        return "";
        
    } catch (const std::exception& e) {
        LOG_ERROR("Exception in PlaceOrder: {}", e.what());
        return "";
    }
}

bool UpbitExchange::CancelOrder(const std::string& order_id) {
    if (!IsConnected()) {
        LOG_ERROR("Not connected to Upbit exchange");
        return false;
    }
    
    try {
        std::string params = "uuid=" + order_id;
        
        JsonValue response;
        if (!MakeAuthenticatedRequest("/v1/order", "DELETE", params, response)) {
            LOG_ERROR("Failed to cancel order on Upbit");
            return false;
        }
        
        if (response.contains("uuid")) {
            LOG_INFO("Order cancelled successfully. Order ID: " + order_id);
            return true;
        }
        
        return false;
        
    } catch (const std::exception& e) {
        LOG_ERROR("Exception in CancelOrder: " + std::string(e.what()));
        return false;
    }
}

bool UpbitExchange::GetOrderStatus(const std::string& order_id, OrderStatus& status) {
    if (!IsConnected()) {
        LOG_ERROR("Not connected to Upbit exchange");
        return false;
    }
    
    try {
        std::string params = "uuid=" + order_id;
        
        JsonValue response;
        if (!MakeAuthenticatedRequest("/v1/order", "GET", params, response)) {
            LOG_ERROR("Failed to get order status from Upbit");
            return false;
        }
        
        Order order = ParseOrder(response);
        // Convert Order to status info if needed
        status = order.status;
        return true;
        
    } catch (const std::exception& e) {
        LOG_ERROR("Exception in GetOrderStatus: " + std::string(e.what()));
        return false;
    }
}



std::vector<Trade> UpbitExchange::GetTradeHistory(const std::string& symbol, int limit) {
    std::vector<Trade> trades;
    
    if (!IsConnected()) {
        LOG_ERROR("Not connected to Upbit exchange");
        return trades;
    }
    
    try {
        std::string upbit_symbol = MapSymbol(symbol);
        if (upbit_symbol.empty()) {
            LOG_ERROR("Invalid symbol: " + symbol);
            return trades;
        }
        
        std::string params = "market=" + upbit_symbol;
        if (limit > 0) {
            params += "&limit=" + std::to_string(std::min(limit, 500));
        }
        
        JsonValue response;
        if (!MakeRequest("/v1/trades/ticks", "GET", params, response)) {
            LOG_ERROR("Failed to get trade history from Upbit");
            return trades;
        }
        
        if (response.is_array()) {
            for (const auto& trade_data : response) {
                trades.push_back(ParseTrade(trade_data));
            }
        }
        
        return trades;
        
    } catch (const std::exception& e) {
        LOG_ERROR("Exception in GetTradeHistory: " + std::string(e.what()));
        return trades;
    }
}

AccountInfo UpbitExchange::GetAccountInfo() {
    AccountInfo account_info;
    
    if (!IsConnected()) {
        LOG_ERROR("Not connected to Upbit exchange");
        return account_info;
    }
    
    try {
        JsonValue response;
        if (!MakeAuthenticatedRequest("/v1/accounts", "GET", "", response)) {
            LOG_ERROR("Failed to get account info from Upbit");
            return account_info;
        }
        
        account_info.total_value_usd = 0.0;  // Will be calculated below
        account_info.timestamp = std::chrono::duration_cast<std::chrono::milliseconds>(
            std::chrono::system_clock::now().time_since_epoch()).count();
        
        if (response.is_array()) {
            for (const auto& balance_data : response) {
                if (balance_data.contains("currency") && balance_data.contains("balance")) {
                    Balance balance;
                    balance.asset = balance_data["currency"].get<std::string>();
                    balance.free = std::stod(balance_data["balance"].get<std::string>());
                    balance.locked = balance_data.contains("locked") ? 
                                   std::stod(balance_data["locked"].get<std::string>()) : 0.0;
                    
                    account_info.balances.push_back(balance);
                }
            }
        }
        
        return account_info;
        
    } catch (const std::exception& e) {
        LOG_ERROR("Exception in GetAccountInfo: " + std::string(e.what()));
        return account_info;
    }
}

MarketData UpbitExchange::GetMarketData(const std::string& symbol) {
    MarketData market_data;
    
    if (!IsConnected()) {
        LOG_ERROR("Not connected to Upbit exchange");
        return market_data;
    }
    
    try {
        std::string upbit_symbol = MapSymbol(symbol);
        if (upbit_symbol.empty()) {
            LOG_ERROR("Invalid symbol: " + symbol);
            return market_data;
        }
        
        std::string params = "markets=" + upbit_symbol;
        
        JsonValue response;
        if (!MakeRequest("/v1/ticker", "GET", params, response)) {
            LOG_ERROR("Failed to get market data from Upbit");
            return market_data;
        }
        
        if (response.is_array() && !response.empty()) {
            market_data = ParseMarketData(response[0]);
        }
        
        return market_data;
        
    } catch (const std::exception& e) {
        LOG_ERROR("Exception in GetMarketData: " + std::string(e.what()));
        return market_data;
    }
}

OrderBook UpbitExchange::GetOrderBook(const std::string& symbol, int depth) {
    OrderBook orderbook;
    
    if (!IsConnected()) {
        LOG_ERROR("Not connected to Upbit exchange");
        return orderbook;
    }
    
    try {
        std::string upbit_symbol = MapSymbol(symbol);
        if (upbit_symbol.empty()) {
            LOG_ERROR("Invalid symbol: " + symbol);
            return orderbook;
        }
        
        std::string params = "markets=" + upbit_symbol;
        
        JsonValue response;
        if (!MakeRequest("/v1/orderbook", "GET", params, response)) {
            LOG_ERROR("Failed to get orderbook from Upbit");
            return orderbook;
        }
        
        if (response.is_array() && !response.empty()) {
            orderbook = ParseOrderBook(response[0]);
        }
        
        return orderbook;
        
    } catch (const std::exception& e) {
        LOG_ERROR("Exception in GetOrderBook: " + std::string(e.what()));
        return orderbook;
    }
}

bool UpbitExchange::SubscribeToMarketData(const std::string& symbol, 
                                         std::function<void(const MarketData&)> callback) {
    std::lock_guard<std::mutex> lock(callback_mutex_);
    market_data_callbacks_[symbol] = callback;
    
    // For Upbit, we can use WebSocket or periodic REST calls
    // Using periodic REST calls for simplicity
    return true;
}

bool UpbitExchange::SubscribeToOrderBook(const std::string& symbol, 
                                        std::function<void(const OrderBook&)> callback) {
    std::lock_guard<std::mutex> lock(callback_mutex_);
    orderbook_callbacks_[symbol] = callback;
    
    // For Upbit, we can use WebSocket or periodic REST calls
    return true;
}

bool UpbitExchange::SubscribeToTrades(const std::string& symbol, 
                                     std::function<void(const Trade&)> callback) {
    std::lock_guard<std::mutex> lock(callback_mutex_);
    trade_callbacks_[symbol] = callback;
    
    // For Upbit, we can use WebSocket or periodic REST calls
    return true;
}

std::vector<std::string> UpbitExchange::GetMarkets() {
    std::vector<std::string> markets;
    
    try {
        JsonValue response;
        if (!MakeRequest("/v1/market/all", "GET", "", response)) {
            LOG_ERROR("Failed to get markets from Upbit");
            return markets;
        }
        
        if (response.is_array()) {
            for (const auto& market : response) {
                if (market.contains("market")) {
                    markets.push_back(market["market"].get<std::string>());
                }
            }
        }
        
        return markets;
        
    } catch (const std::exception& e) {
        LOG_ERROR("Exception in GetMarkets: " + std::string(e.what()));
        return markets;
    }
}

bool UpbitExchange::GetCandles(const std::string& symbol, const std::string& interval, 
                              int count, std::vector<Candle>& candles) {
    try {
        std::string upbit_symbol = MapSymbol(symbol);
        if (upbit_symbol.empty()) {
            LOG_ERROR("Invalid symbol: " + symbol);
            return false;
        }
        
        std::string endpoint;
        if (interval == "1m") {
            endpoint = "/v1/candles/minutes/1";
        } else if (interval == "5m") {
            endpoint = "/v1/candles/minutes/5";
        } else if (interval == "1h") {
            endpoint = "/v1/candles/minutes/60";
        } else if (interval == "1d") {
            endpoint = "/v1/candles/days";
        } else {
            LOG_ERROR("Unsupported interval: " + interval);
            return false;
        }
        
        std::string params = "market=" + upbit_symbol;
        if (count > 0) {
            params += "&count=" + std::to_string(std::min(count, 200));
        }
        
        JsonValue response;
        if (!MakeRequest(endpoint, "GET", params, response)) {
            LOG_ERROR("Failed to get candles from Upbit");
            return false;
        }
        
        if (response.is_array()) {
            for (const auto& candle_data : response) {
                candles.push_back(ParseCandle(candle_data));
            }
        }
        
        return true;
        
    } catch (const std::exception& e) {
        LOG_ERROR("Exception in GetCandles: " + std::string(e.what()));
        return false;
    }
}

bool UpbitExchange::GetTicker(const std::string& symbol, Ticker& ticker) {
    try {
        std::string upbit_symbol = MapSymbol(symbol);
        if (upbit_symbol.empty()) {
            LOG_ERROR("Invalid symbol: " + symbol);
            return false;
        }
        
        std::string params = "markets=" + upbit_symbol;
        
        JsonValue response;
        if (!MakeRequest("/v1/ticker", "GET", params, response)) {
            LOG_ERROR("Failed to get ticker from Upbit");
            return false;
        }
        
        if (response.is_array() && !response.empty()) {
            ticker = ParseTicker(response[0]);
            return true;
        }
        
        return false;
        
    } catch (const std::exception& e) {
        LOG_ERROR("Exception in GetTicker: " + std::string(e.what()));
        return false;
    }
}

std::vector<Ticker> UpbitExchange::GetAllTickers() {
    std::vector<Ticker> tickers;
    
    try {
        JsonValue response;
        if (!MakeRequest("/v1/ticker", "GET", "markets=KRW-BTC,KRW-ETH,KRW-ADA", response)) {
            LOG_ERROR("Failed to get all tickers from Upbit");
            return tickers;
        }
        
        if (response.is_array()) {
            for (const auto& ticker_data : response) {
                tickers.push_back(ParseTicker(ticker_data));
            }
        }
        
        return tickers;
        
    } catch (const std::exception& e) {
        LOG_ERROR("Exception in GetAllTickers: " + std::string(e.what()));
        return tickers;
    }
}

// Parsing methods
Order UpbitExchange::ParseOrder(const JsonValue& order_data) {
    Order order;
    
    try {
        order.order_id = order_data.value("uuid", "");
        order.symbol = UnmapSymbol(order_data.value("market", ""));
        order.side = ParseOrderSide(order_data.value("side", ""));
        order.type = ParseOrderType(order_data.value("ord_type", ""));
        order.quantity = std::stod(order_data.value("volume", "0"));
        order.price = std::stod(order_data.value("price", "0"));
        order.filled_quantity = std::stod(order_data.value("executed_volume", "0"));
        
        std::string state = order_data.value("state", "");
        if (state == "wait") {
            order.status = OrderStatus::NEW;
        } else if (state == "done") {
            order.status = OrderStatus::FILLED;
        } else if (state == "cancel") {
            order.status = OrderStatus::CANCELLED;
        } else {
            order.status = OrderStatus::PENDING;
        }
        
        order.timestamp = std::chrono::duration_cast<std::chrono::milliseconds>(
            std::chrono::system_clock::now().time_since_epoch()).count();
            
    } catch (const std::exception& e) {
        LOG_ERROR("Exception in ParseOrder: " + std::string(e.what()));
    }
    
    return order;
}

Trade UpbitExchange::ParseTrade(const JsonValue& trade_data) {
    Trade trade;
    
    try {
        trade.symbol = UnmapSymbol(trade_data.value("market", ""));
        trade.price = std::stod(trade_data.value("trade_price", "0"));
        trade.quantity = std::stod(trade_data.value("trade_volume", "0"));
        trade.timestamp = trade_data.value("timestamp", 0);
        // Note: is_buyer_maker field not available in Trade struct
        trade.trade_id = std::to_string(trade_data.value("sequential_id", 0));
        
    } catch (const std::exception& e) {
        LOG_ERROR("Exception in ParseTrade: " + std::string(e.what()));
    }
    
    return trade;
}

MarketData UpbitExchange::ParseMarketData(const JsonValue& ticker_data) {
    MarketData market_data;
    
    try {
        market_data.symbol = UnmapSymbol(ticker_data.value("market", ""));
        market_data.last_price = std::stod(ticker_data.value("trade_price", "0"));
        market_data.bid_price = 0.0; // Not available in ticker
        market_data.ask_price = 0.0; // Not available in ticker
        market_data.volume_24h = std::stod(ticker_data.value("acc_trade_volume_24h", "0"));
        market_data.high_24h = std::stod(ticker_data.value("high_price", "0"));
        market_data.low_24h = std::stod(ticker_data.value("low_price", "0"));
        // Note: open_24h not available in MarketData struct, using change fields instead
        market_data.change_24h = std::stod(ticker_data.value("signed_change_price", "0"));
        market_data.change_percent_24h = std::stod(ticker_data.value("signed_change_rate", "0")) * 100.0;
        market_data.timestamp = ticker_data.value("timestamp", 0);
        
    } catch (const std::exception& e) {
        LOG_ERROR("Exception in ParseMarketData: " + std::string(e.what()));
    }
    
    return market_data;
}

OrderBook UpbitExchange::ParseOrderBook(const JsonValue& orderbook_data) {
    OrderBook orderbook;
    
    try {
        orderbook.symbol = UnmapSymbol(orderbook_data.value("market", ""));
        orderbook.timestamp = std::chrono::duration_cast<std::chrono::milliseconds>(
            std::chrono::system_clock::now().time_since_epoch()).count();
        
        if (orderbook_data.contains("orderbook_units") && orderbook_data["orderbook_units"].is_array()) {
            for (const auto& unit : orderbook_data["orderbook_units"]) {
                // Bids
                if (unit.contains("bid_price") && unit.contains("bid_size")) {
                    double price = std::stod(unit["bid_price"].get<std::string>());
                    double quantity = std::stod(unit["bid_size"].get<std::string>());
                    orderbook.bids.emplace_back(price, quantity);
                }
                
                // Asks
                if (unit.contains("ask_price") && unit.contains("ask_size")) {
                    double price = std::stod(unit["ask_price"].get<std::string>());
                    double quantity = std::stod(unit["ask_size"].get<std::string>());
                    orderbook.asks.emplace_back(price, quantity);
                }
            }
        }
        
    } catch (const std::exception& e) {
        LOG_ERROR("Exception in ParseOrderBook: " + std::string(e.what()));
    }
    
    return orderbook;
}

Candle UpbitExchange::ParseCandle(const JsonValue& candle_data) {
    Candle candle;
    
    try {
        candle.symbol = UnmapSymbol(candle_data.value("market", ""));
        candle.open_time = candle_data.value("timestamp", 0);
        candle.close_time = candle_data.value("timestamp", 0);
        candle.open = std::stod(candle_data.value("opening_price", "0"));
        candle.high = std::stod(candle_data.value("high_price", "0"));
        candle.low = std::stod(candle_data.value("low_price", "0"));
        candle.close = std::stod(candle_data.value("trade_price", "0"));
        candle.volume = std::stod(candle_data.value("candle_acc_trade_volume", "0"));
        
    } catch (const std::exception& e) {
        LOG_ERROR("Exception in ParseCandle: " + std::string(e.what()));
    }
    
    return candle;
}

Ticker UpbitExchange::ParseTicker(const JsonValue& ticker_data) {
    Ticker ticker;
    
    try {
        ticker.symbol = UnmapSymbol(ticker_data.value("market", ""));
        ticker.last_price = std::stod(ticker_data.value("trade_price", "0"));
        ticker.volume_24h = std::stod(ticker_data.value("acc_trade_volume_24h", "0"));
        ticker.price_change_24h = std::stod(ticker_data.value("signed_change_price", "0"));
        ticker.price_change_percent_24h = std::stod(ticker_data.value("signed_change_rate", "0")) * 100.0;
        ticker.high_24h = std::stod(ticker_data.value("high_price", "0"));
        ticker.low_24h = std::stod(ticker_data.value("low_price", "0"));
        ticker.timestamp = ticker_data.value("timestamp", 0);
        
    } catch (const std::exception& e) {
        LOG_ERROR("Exception in ParseTicker: " + std::string(e.what()));
    }
    
    return ticker;
}

// WebSocket handlers
void UpbitExchange::OnWebSocketMessage(const std::string& message) {
    try {
        auto json_data = ats::json::ParseJson(message);
        
        // Process different message types
        if (json_data.contains("type")) {
            std::string type = json_data["type"].get<std::string>();
            
            if (type == "ticker") {
                // Handle ticker updates
                MarketData market_data = ParseMarketData(json_data);
                std::lock_guard<std::mutex> lock(callback_mutex_);
                auto it = market_data_callbacks_.find(market_data.symbol);
                if (it != market_data_callbacks_.end() && it->second) {
                    it->second(market_data);
                }
            } else if (type == "orderbook") {
                // Handle orderbook updates
                OrderBook orderbook = ParseOrderBook(json_data);
                std::lock_guard<std::mutex> lock(callback_mutex_);
                auto it = orderbook_callbacks_.find(orderbook.symbol);
                if (it != orderbook_callbacks_.end() && it->second) {
                    it->second(orderbook);
                }
            } else if (type == "trade") {
                // Handle trade updates
                Trade trade = ParseTrade(json_data);
                std::lock_guard<std::mutex> lock(callback_mutex_);
                auto it = trade_callbacks_.find(trade.symbol);
                if (it != trade_callbacks_.end() && it->second) {
                    it->second(trade);
                }
            }
        }
        
    } catch (const std::exception& e) {
        LOG_ERROR("Exception in OnWebSocketMessage: " + std::string(e.what()));
    }
}

void UpbitExchange::OnWebSocketError(const std::string& error) {
    LOG_ERROR("Upbit WebSocket error: " + error);
}

void UpbitExchange::OnWebSocketClose() {
    LOG_INFO("Upbit WebSocket connection closed");
}

// Helper methods
std::string UpbitExchange::GetServerTime() {
    try {
        JsonValue response;
        if (MakeRequest("/v1/market/all", "GET", "", response)) {
            auto now = std::chrono::system_clock::now();
            auto timestamp = std::chrono::duration_cast<std::chrono::milliseconds>(
                now.time_since_epoch()).count();
            return std::to_string(timestamp);
        }
    } catch (const std::exception& e) {
        LOG_ERROR("Exception in GetServerTime: " + std::string(e.what()));
    }
    return "";
}

bool UpbitExchange::ValidateSymbol(const std::string& symbol) {
    return !MapSymbol(symbol).empty();
}

std::string UpbitExchange::FormatOrderSide(OrderSide side) {
    switch (side) {
        case OrderSide::BUY: return "bid";
        case OrderSide::SELL: return "ask";
        default: return "bid";
    }
}

std::string UpbitExchange::FormatOrderType(OrderType type) {
    switch (type) {
        case OrderType::LIMIT: return "limit";
        case OrderType::MARKET: return "price"; // Upbit uses "price" for market orders
        default: return "limit";
    }
}

OrderSide UpbitExchange::ParseOrderSide(const std::string& side) {
    if (side == "bid") return OrderSide::BUY;
    if (side == "ask") return OrderSide::SELL;
    return OrderSide::BUY;
}

OrderType UpbitExchange::ParseOrderType(const std::string& type) {
    if (type == "limit") return OrderType::LIMIT;
    if (type == "price" || type == "market") return OrderType::MARKET;
    return OrderType::LIMIT;
}

// Missing virtual functions implementation
ExchangeStatus UpbitExchange::GetStatus() const {
    return status_;
}

std::string UpbitExchange::GetName() const {
    return "Upbit";
}

bool UpbitExchange::GetPrice(const std::string& symbol, Price& price) {
    Ticker ticker;
    if (!GetTicker(symbol, ticker)) {
        return false;
    }
    
    price.symbol = symbol;
    price.bid = 0.0; // Not available from ticker
    price.ask = 0.0; // Not available from ticker  
    price.last = ticker.last_price;
    price.timestamp = std::chrono::duration_cast<std::chrono::milliseconds>(
        std::chrono::system_clock::now().time_since_epoch()).count();
    
    return true;
}

bool UpbitExchange::GetOrderBook(const std::string& symbol, OrderBook& orderbook) {
    orderbook = GetOrderBook(symbol, 10);
    return !orderbook.bids.empty() || !orderbook.asks.empty();
}

std::vector<std::string> UpbitExchange::GetSupportedSymbols() {
    return GetMarkets();
}

std::vector<Balance> UpbitExchange::GetBalances() {
    auto account_info = GetAccountInfo();
    return account_info.balances;
}

double UpbitExchange::GetBalance(const std::string& asset) {
    auto balances = GetBalances();
    for (const auto& balance : balances) {
        if (balance.asset == asset) {
            return balance.free + balance.locked;
        }
    }
    return 0.0;
}

Order UpbitExchange::GetOrder(const std::string& order_id) {
    JsonValue response;
    std::string params = "uuid=" + order_id;
    if (!MakeAuthenticatedRequest("/order", "GET", params, response)) {
        return Order{};
    }
    
    return ParseOrder(response);
}

std::vector<Order> UpbitExchange::GetOpenOrders(const std::string& symbol) {
    JsonValue response;
    std::string params;
    if (!symbol.empty()) {
        params = "market=" + MapSymbol(symbol);
    }
    if (!MakeAuthenticatedRequest("/orders", "GET", params, response)) {
        return {};
    }
    
    std::vector<Order> orders;
    if (response.is_array()) {
        for (const auto& order_data : response) {
            orders.push_back(ParseOrder(order_data));
        }
    }
    
    return orders;
}

bool UpbitExchange::SubscribeToPrice(const std::string& symbol, 
                                   std::function<void(const Price&)> callback) {
    // Simplified implementation - in real world would use WebSocket
    LOG_INFO("Subscribing to price updates for {}", symbol);
    return true;
}

bool UpbitExchange::UnsubscribeFromPrice(const std::string& symbol) {
    LOG_INFO("Unsubscribing from price updates for {}", symbol);
    return true;
}

bool UpbitExchange::UnsubscribeFromOrderBook(const std::string& symbol) {
    LOG_INFO("Unsubscribing from orderbook updates for {}", symbol);
    return true;
}

double UpbitExchange::GetMakerFee() const {
    return 0.0025; // 0.25% for Upbit
}

double UpbitExchange::GetTakerFee() const {
    return 0.0025; // 0.25% for Upbit
}

int UpbitExchange::GetRateLimit() const {
    return MAX_REQUESTS_PER_SECOND;
}

double UpbitExchange::GetMinOrderSize(const std::string& symbol) const {
    // Upbit minimum order sizes vary by symbol
    return 5000.0; // 5000 KRW minimum for most pairs
}

double UpbitExchange::GetMaxOrderSize(const std::string& symbol) const {
    // No specific maximum enforced by exchange
    return 1000000000.0; // 1 billion KRW
}

bool UpbitExchange::IsHealthy() const {
    return connected_.load() && status_ == ExchangeStatus::CONNECTED;
}

std::string UpbitExchange::GetLastError() const {
    return last_error_;
}

} // namespace ats 
