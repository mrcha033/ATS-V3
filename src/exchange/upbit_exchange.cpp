#include "upbit_exchange.hpp"
#include <jwt-cpp/jwt.h>
#include <uuid/uuid.h>
#include <openssl/sha.h>
#include <iomanip>
#include <sstream>
#include <thread>
#include <chrono>

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
        // Initialize REST client
        if (!rest_client_->Initialize(BASE_URL)) {
            Logger::Error("Failed to initialize REST client for Upbit");
            return false;
        }

        // Test connection with server time
        Json::Value response;
        if (!MakeRequest("/v1/market/all", "GET", "", response)) {
            Logger::Error("Failed to connect to Upbit API");
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
            ws_client_->SetCloseCallback([this]() {
                OnWebSocketClose();
            });
        }

        connected_ = true;
        Logger::Info("Successfully connected to Upbit exchange");
        return true;
        
    } catch (const std::exception& e) {
        Logger::Error("Exception in Upbit connection: " + std::string(e.what()));
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
    Json::Value response;
    if (MakeRequest("/v1/market/all", "GET", "", response)) {
        if (response.isArray()) {
            for (const auto& market : response) {
                if (market.isMember("market")) {
                    std::string upbit_symbol = market["market"].asString();
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
        // Generate UUID for nonce
        uuid_t uuid;
        uuid_generate(uuid);
        char uuid_str[37];
        uuid_unparse(uuid, uuid_str);

        auto token = jwt::create()
            .set_algorithm("HS256")
            .set_header_claim("typ", jwt::claim(std::string("JWT")))
            .set_payload_claim("access_key", jwt::claim(access_key_))
            .set_payload_claim("nonce", jwt::claim(std::string(uuid_str)));

        if (!query_string.empty()) {
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
        }

        return token.sign(jwt::algorithm::hs256{secret_key_});
        
    } catch (const std::exception& e) {
        Logger::Error("Failed to generate JWT: " + std::string(e.what()));
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
                               const std::string& params, Json::Value& response) {
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
        
        std::string response_str;
        bool success = false;
        
        if (method == "GET") {
            success = rest_client_->Get(full_url, headers, response_str);
        } else if (method == "POST") {
            success = rest_client_->Post(full_url, params, headers, response_str);
        } else if (method == "DELETE") {
            success = rest_client_->Delete(full_url, headers, response_str);
        }
        
        UpdateRateLimit();
        
        if (!success) {
            Logger::Error("HTTP request failed for endpoint: " + endpoint);
            return false;
        }
        
        JsonParser parser;
        if (!parser.Parse(response_str, response)) {
            Logger::Error("Failed to parse JSON response from: " + endpoint);
            return false;
        }
        
        // Check for API errors
        if (response.isMember("error")) {
            Logger::Error("API error: " + response["error"].asString());
            return false;
        }
        
        return true;
        
    } catch (const std::exception& e) {
        Logger::Error("Exception in MakeRequest: " + std::string(e.what()));
        return false;
    }
}

bool UpbitExchange::MakeAuthenticatedRequest(const std::string& endpoint, const std::string& method,
                                           const std::string& params, Json::Value& response) {
    if (access_key_.empty() || secret_key_.empty()) {
        Logger::Error("Authentication credentials not provided");
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
        
        std::string response_str;
        bool success = false;
        
        if (method == "GET") {
            success = rest_client_->Get(full_url, headers, response_str);
        } else if (method == "POST") {
            success = rest_client_->Post(full_url, params, headers, response_str);
        } else if (method == "DELETE") {
            success = rest_client_->Delete(full_url, headers, response_str);
        }
        
        UpdateRateLimit();
        
        if (!success) {
            Logger::Error("Authenticated HTTP request failed for endpoint: " + endpoint);
            return false;
        }
        
        JsonParser parser;
        if (!parser.Parse(response_str, response)) {
            Logger::Error("Failed to parse JSON response from: " + endpoint);
            return false;
        }
        
        // Check for API errors
        if (response.isMember("error")) {
            Logger::Error("API error: " + response["error"].asString());
            return false;
        }
        
        return true;
        
    } catch (const std::exception& e) {
        Logger::Error("Exception in MakeAuthenticatedRequest: " + std::string(e.what()));
        return false;
    }
}

bool UpbitExchange::PlaceOrder(const OrderRequest& request) {
    if (!IsConnected()) {
        Logger::Error("Not connected to Upbit exchange");
        return false;
    }
    
    try {
        std::string upbit_symbol = MapSymbol(request.symbol);
        if (upbit_symbol.empty()) {
            Logger::Error("Invalid symbol: " + request.symbol);
            return false;
        }
        
        Json::Value params;
        params["market"] = upbit_symbol;
        params["side"] = FormatOrderSide(request.side);
        params["ord_type"] = FormatOrderType(request.type);
        
        if (request.type == OrderType::LIMIT) {
            params["price"] = std::to_string(request.price);
            params["volume"] = std::to_string(request.quantity);
        } else if (request.type == OrderType::MARKET) {
            if (request.side == OrderSide::BUY) {
                params["price"] = std::to_string(request.price * request.quantity);
            } else {
                params["volume"] = std::to_string(request.quantity);
            }
        }
        
        Json::StreamWriterBuilder builder;
        std::string params_str = Json::writeString(builder, params);
        
        Json::Value response;
        if (!MakeAuthenticatedRequest("/v1/orders", "POST", params_str, response)) {
            Logger::Error("Failed to place order on Upbit");
            return false;
        }
        
        if (response.isMember("uuid")) {
            Logger::Info("Order placed successfully. Order ID: " + response["uuid"].asString());
            return true;
        }
        
        return false;
        
    } catch (const std::exception& e) {
        Logger::Error("Exception in PlaceOrder: " + std::string(e.what()));
        return false;
    }
}

bool UpbitExchange::CancelOrder(const std::string& order_id) {
    if (!IsConnected()) {
        Logger::Error("Not connected to Upbit exchange");
        return false;
    }
    
    try {
        std::string params = "uuid=" + order_id;
        
        Json::Value response;
        if (!MakeAuthenticatedRequest("/v1/order", "DELETE", params, response)) {
            Logger::Error("Failed to cancel order on Upbit");
            return false;
        }
        
        if (response.isMember("uuid")) {
            Logger::Info("Order cancelled successfully. Order ID: " + order_id);
            return true;
        }
        
        return false;
        
    } catch (const std::exception& e) {
        Logger::Error("Exception in CancelOrder: " + std::string(e.what()));
        return false;
    }
}

bool UpbitExchange::GetOrderStatus(const std::string& order_id, OrderStatus& status) {
    if (!IsConnected()) {
        Logger::Error("Not connected to Upbit exchange");
        return false;
    }
    
    try {
        std::string params = "uuid=" + order_id;
        
        Json::Value response;
        if (!MakeAuthenticatedRequest("/v1/order", "GET", params, response)) {
            Logger::Error("Failed to get order status from Upbit");
            return false;
        }
        
        status = ParseOrderStatus(response);
        return true;
        
    } catch (const std::exception& e) {
        Logger::Error("Exception in GetOrderStatus: " + std::string(e.what()));
        return false;
    }
}

std::vector<OrderStatus> UpbitExchange::GetOpenOrders() {
    std::vector<OrderStatus> orders;
    
    if (!IsConnected()) {
        Logger::Error("Not connected to Upbit exchange");
        return orders;
    }
    
    try {
        std::string params = "state=wait";
        
        Json::Value response;
        if (!MakeAuthenticatedRequest("/v1/orders", "GET", params, response)) {
            Logger::Error("Failed to get open orders from Upbit");
            return orders;
        }
        
        if (response.isArray()) {
            for (const auto& order_data : response) {
                orders.push_back(ParseOrderStatus(order_data));
            }
        }
        
        return orders;
        
    } catch (const std::exception& e) {
        Logger::Error("Exception in GetOpenOrders: " + std::string(e.what()));
        return orders;
    }
}

std::vector<Trade> UpbitExchange::GetTradeHistory(const std::string& symbol, int limit) {
    std::vector<Trade> trades;
    
    if (!IsConnected()) {
        Logger::Error("Not connected to Upbit exchange");
        return trades;
    }
    
    try {
        std::string upbit_symbol = MapSymbol(symbol);
        if (upbit_symbol.empty()) {
            Logger::Error("Invalid symbol: " + symbol);
            return trades;
        }
        
        std::string params = "market=" + upbit_symbol;
        if (limit > 0) {
            params += "&limit=" + std::to_string(std::min(limit, 500));
        }
        
        Json::Value response;
        if (!MakeRequest("/v1/trades/ticks", "GET", params, response)) {
            Logger::Error("Failed to get trade history from Upbit");
            return trades;
        }
        
        if (response.isArray()) {
            for (const auto& trade_data : response) {
                trades.push_back(ParseTrade(trade_data));
            }
        }
        
        return trades;
        
    } catch (const std::exception& e) {
        Logger::Error("Exception in GetTradeHistory: " + std::string(e.what()));
        return trades;
    }
}

AccountInfo UpbitExchange::GetAccountInfo() {
    AccountInfo account_info;
    
    if (!IsConnected()) {
        Logger::Error("Not connected to Upbit exchange");
        return account_info;
    }
    
    try {
        Json::Value response;
        if (!MakeAuthenticatedRequest("/v1/accounts", "GET", "", response)) {
            Logger::Error("Failed to get account info from Upbit");
            return account_info;
        }
        
        account_info.exchange = "upbit";
        account_info.trading_enabled = true;
        account_info.withdrawal_enabled = true;
        
        if (response.isArray()) {
            for (const auto& balance_data : response) {
                if (balance_data.isMember("currency") && balance_data.isMember("balance")) {
                    Balance balance;
                    balance.asset = balance_data["currency"].asString();
                    balance.free = std::stod(balance_data["balance"].asString());
                    balance.locked = balance_data.isMember("locked") ? 
                                   std::stod(balance_data["locked"].asString()) : 0.0;
                    balance.total = balance.free + balance.locked;
                    
                    account_info.balances.push_back(balance);
                }
            }
        }
        
        return account_info;
        
    } catch (const std::exception& e) {
        Logger::Error("Exception in GetAccountInfo: " + std::string(e.what()));
        return account_info;
    }
}

MarketData UpbitExchange::GetMarketData(const std::string& symbol) {
    MarketData market_data;
    
    if (!IsConnected()) {
        Logger::Error("Not connected to Upbit exchange");
        return market_data;
    }
    
    try {
        std::string upbit_symbol = MapSymbol(symbol);
        if (upbit_symbol.empty()) {
            Logger::Error("Invalid symbol: " + symbol);
            return market_data;
        }
        
        std::string params = "markets=" + upbit_symbol;
        
        Json::Value response;
        if (!MakeRequest("/v1/ticker", "GET", params, response)) {
            Logger::Error("Failed to get market data from Upbit");
            return market_data;
        }
        
        if (response.isArray() && !response.empty()) {
            market_data = ParseMarketData(response[0]);
        }
        
        return market_data;
        
    } catch (const std::exception& e) {
        Logger::Error("Exception in GetMarketData: " + std::string(e.what()));
        return market_data;
    }
}

OrderBook UpbitExchange::GetOrderBook(const std::string& symbol, int depth) {
    OrderBook orderbook;
    
    if (!IsConnected()) {
        Logger::Error("Not connected to Upbit exchange");
        return orderbook;
    }
    
    try {
        std::string upbit_symbol = MapSymbol(symbol);
        if (upbit_symbol.empty()) {
            Logger::Error("Invalid symbol: " + symbol);
            return orderbook;
        }
        
        std::string params = "markets=" + upbit_symbol;
        
        Json::Value response;
        if (!MakeRequest("/v1/orderbook", "GET", params, response)) {
            Logger::Error("Failed to get orderbook from Upbit");
            return orderbook;
        }
        
        if (response.isArray() && !response.empty()) {
            orderbook = ParseOrderBook(response[0]);
        }
        
        return orderbook;
        
    } catch (const std::exception& e) {
        Logger::Error("Exception in GetOrderBook: " + std::string(e.what()));
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
        Json::Value response;
        if (!MakeRequest("/v1/market/all", "GET", "", response)) {
            Logger::Error("Failed to get markets from Upbit");
            return markets;
        }
        
        if (response.isArray()) {
            for (const auto& market : response) {
                if (market.isMember("market")) {
                    markets.push_back(market["market"].asString());
                }
            }
        }
        
        return markets;
        
    } catch (const std::exception& e) {
        Logger::Error("Exception in GetMarkets: " + std::string(e.what()));
        return markets;
    }
}

bool UpbitExchange::GetCandles(const std::string& symbol, const std::string& interval, 
                              int count, std::vector<Candle>& candles) {
    try {
        std::string upbit_symbol = MapSymbol(symbol);
        if (upbit_symbol.empty()) {
            Logger::Error("Invalid symbol: " + symbol);
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
            Logger::Error("Unsupported interval: " + interval);
            return false;
        }
        
        std::string params = "market=" + upbit_symbol;
        if (count > 0) {
            params += "&count=" + std::to_string(std::min(count, 200));
        }
        
        Json::Value response;
        if (!MakeRequest(endpoint, "GET", params, response)) {
            Logger::Error("Failed to get candles from Upbit");
            return false;
        }
        
        if (response.isArray()) {
            for (const auto& candle_data : response) {
                candles.push_back(ParseCandle(candle_data));
            }
        }
        
        return true;
        
    } catch (const std::exception& e) {
        Logger::Error("Exception in GetCandles: " + std::string(e.what()));
        return false;
    }
}

bool UpbitExchange::GetTicker(const std::string& symbol, Ticker& ticker) {
    try {
        std::string upbit_symbol = MapSymbol(symbol);
        if (upbit_symbol.empty()) {
            Logger::Error("Invalid symbol: " + symbol);
            return false;
        }
        
        std::string params = "markets=" + upbit_symbol;
        
        Json::Value response;
        if (!MakeRequest("/v1/ticker", "GET", params, response)) {
            Logger::Error("Failed to get ticker from Upbit");
            return false;
        }
        
        if (response.isArray() && !response.empty()) {
            ticker = ParseTicker(response[0]);
            return true;
        }
        
        return false;
        
    } catch (const std::exception& e) {
        Logger::Error("Exception in GetTicker: " + std::string(e.what()));
        return false;
    }
}

std::vector<Ticker> UpbitExchange::GetAllTickers() {
    std::vector<Ticker> tickers;
    
    try {
        Json::Value response;
        if (!MakeRequest("/v1/ticker", "GET", "markets=KRW-BTC,KRW-ETH,KRW-ADA", response)) {
            Logger::Error("Failed to get all tickers from Upbit");
            return tickers;
        }
        
        if (response.isArray()) {
            for (const auto& ticker_data : response) {
                tickers.push_back(ParseTicker(ticker_data));
            }
        }
        
        return tickers;
        
    } catch (const std::exception& e) {
        Logger::Error("Exception in GetAllTickers: " + std::string(e.what()));
        return tickers;
    }
}

// Parsing methods
OrderStatus UpbitExchange::ParseOrderStatus(const Json::Value& order_data) {
    OrderStatus status;
    
    try {
        status.order_id = order_data.get("uuid", "").asString();
        status.symbol = UnmapSymbol(order_data.get("market", "").asString());
        status.side = ParseOrderSide(order_data.get("side", "").asString());
        status.type = ParseOrderType(order_data.get("ord_type", "").asString());
        status.quantity = std::stod(order_data.get("volume", "0").asString());
        status.price = std::stod(order_data.get("price", "0").asString());
        status.filled_quantity = std::stod(order_data.get("executed_volume", "0").asString());
        status.remaining_quantity = status.quantity - status.filled_quantity;
        
        std::string state = order_data.get("state", "").asString();
        if (state == "wait") {
            status.status = "NEW";
        } else if (state == "done") {
            status.status = "FILLED";
        } else if (state == "cancel") {
            status.status = "CANCELED";
        } else {
            status.status = state;
        }
        
        status.timestamp = std::chrono::duration_cast<std::chrono::milliseconds>(
            std::chrono::system_clock::now().time_since_epoch()).count();
            
    } catch (const std::exception& e) {
        Logger::Error("Exception in ParseOrderStatus: " + std::string(e.what()));
    }
    
    return status;
}

Trade UpbitExchange::ParseTrade(const Json::Value& trade_data) {
    Trade trade;
    
    try {
        trade.symbol = UnmapSymbol(trade_data.get("market", "").asString());
        trade.price = std::stod(trade_data.get("trade_price", "0").asString());
        trade.quantity = std::stod(trade_data.get("trade_volume", "0").asString());
        trade.timestamp = trade_data.get("timestamp", 0).asUInt64();
        trade.is_buyer_maker = trade_data.get("ask_bid", "").asString() == "ASK";
        trade.trade_id = std::to_string(trade_data.get("sequential_id", 0).asUInt64());
        
    } catch (const std::exception& e) {
        Logger::Error("Exception in ParseTrade: " + std::string(e.what()));
    }
    
    return trade;
}

MarketData UpbitExchange::ParseMarketData(const Json::Value& ticker_data) {
    MarketData market_data;
    
    try {
        market_data.symbol = UnmapSymbol(ticker_data.get("market", "").asString());
        market_data.last_price = std::stod(ticker_data.get("trade_price", "0").asString());
        market_data.bid_price = 0.0; // Not available in ticker
        market_data.ask_price = 0.0; // Not available in ticker
        market_data.volume_24h = std::stod(ticker_data.get("acc_trade_volume_24h", "0").asString());
        market_data.high_24h = std::stod(ticker_data.get("high_price", "0").asString());
        market_data.low_24h = std::stod(ticker_data.get("low_price", "0").asString());
        market_data.open_24h = std::stod(ticker_data.get("opening_price", "0").asString());
        market_data.price_change_24h = std::stod(ticker_data.get("signed_change_price", "0").asString());
        market_data.price_change_percent_24h = std::stod(ticker_data.get("signed_change_rate", "0").asString()) * 100.0;
        market_data.timestamp = ticker_data.get("timestamp", 0).asUInt64();
        
    } catch (const std::exception& e) {
        Logger::Error("Exception in ParseMarketData: " + std::string(e.what()));
    }
    
    return market_data;
}

OrderBook UpbitExchange::ParseOrderBook(const Json::Value& orderbook_data) {
    OrderBook orderbook;
    
    try {
        orderbook.symbol = UnmapSymbol(orderbook_data.get("market", "").asString());
        orderbook.timestamp = std::chrono::duration_cast<std::chrono::milliseconds>(
            std::chrono::system_clock::now().time_since_epoch()).count();
        
        if (orderbook_data.isMember("orderbook_units") && orderbook_data["orderbook_units"].isArray()) {
            for (const auto& unit : orderbook_data["orderbook_units"]) {
                // Bids
                if (unit.isMember("bid_price") && unit.isMember("bid_size")) {
                    OrderBookEntry bid;
                    bid.price = std::stod(unit["bid_price"].asString());
                    bid.quantity = std::stod(unit["bid_size"].asString());
                    orderbook.bids.push_back(bid);
                }
                
                // Asks
                if (unit.isMember("ask_price") && unit.isMember("ask_size")) {
                    OrderBookEntry ask;
                    ask.price = std::stod(unit["ask_price"].asString());
                    ask.quantity = std::stod(unit["ask_size"].asString());
                    orderbook.asks.push_back(ask);
                }
            }
        }
        
    } catch (const std::exception& e) {
        Logger::Error("Exception in ParseOrderBook: " + std::string(e.what()));
    }
    
    return orderbook;
}

Candle UpbitExchange::ParseCandle(const Json::Value& candle_data) {
    Candle candle;
    
    try {
        candle.market = candle_data.get("market", "").asString();
        candle.candle_date_time_utc = candle_data.get("candle_date_time_utc", "").asString();
        candle.candle_date_time_kst = candle_data.get("candle_date_time_kst", "").asString();
        candle.opening_price = std::stod(candle_data.get("opening_price", "0").asString());
        candle.high_price = std::stod(candle_data.get("high_price", "0").asString());
        candle.low_price = std::stod(candle_data.get("low_price", "0").asString());
        candle.trade_price = std::stod(candle_data.get("trade_price", "0").asString());
        candle.timestamp = candle_data.get("timestamp", 0).asUInt64();
        candle.candle_acc_trade_price = std::stod(candle_data.get("candle_acc_trade_price", "0").asString());
        candle.candle_acc_trade_volume = std::stod(candle_data.get("candle_acc_trade_volume", "0").asString());
        candle.unit = candle_data.get("unit", 1).asInt();
        
    } catch (const std::exception& e) {
        Logger::Error("Exception in ParseCandle: " + std::string(e.what()));
    }
    
    return candle;
}

Ticker UpbitExchange::ParseTicker(const Json::Value& ticker_data) {
    Ticker ticker;
    
    try {
        ticker.market = ticker_data.get("market", "").asString();
        ticker.trade_date = ticker_data.get("trade_date", "").asString();
        ticker.trade_time = ticker_data.get("trade_time", "").asString();
        ticker.trade_date_kst = ticker_data.get("trade_date_kst", "").asString();
        ticker.trade_time_kst = ticker_data.get("trade_time_kst", "").asString();
        ticker.trade_timestamp = ticker_data.get("trade_timestamp", 0).asUInt64();
        ticker.opening_price = std::stod(ticker_data.get("opening_price", "0").asString());
        ticker.high_price = std::stod(ticker_data.get("high_price", "0").asString());
        ticker.low_price = std::stod(ticker_data.get("low_price", "0").asString());
        ticker.trade_price = std::stod(ticker_data.get("trade_price", "0").asString());
        ticker.prev_closing_price = std::stod(ticker_data.get("prev_closing_price", "0").asString());
        ticker.change = ticker_data.get("change", "").asString();
        ticker.change_price = std::stod(ticker_data.get("change_price", "0").asString());
        ticker.change_rate = std::stod(ticker_data.get("change_rate", "0").asString());
        ticker.signed_change_price = std::stod(ticker_data.get("signed_change_price", "0").asString());
        ticker.signed_change_rate = std::stod(ticker_data.get("signed_change_rate", "0").asString());
        ticker.trade_volume = std::stod(ticker_data.get("trade_volume", "0").asString());
        ticker.acc_trade_price = std::stod(ticker_data.get("acc_trade_price", "0").asString());
        ticker.acc_trade_price_24h = std::stod(ticker_data.get("acc_trade_price_24h", "0").asString());
        ticker.acc_trade_volume = std::stod(ticker_data.get("acc_trade_volume", "0").asString());
        ticker.acc_trade_volume_24h = std::stod(ticker_data.get("acc_trade_volume_24h", "0").asString());
        ticker.highest_52_week_price = std::stod(ticker_data.get("highest_52_week_price", "0").asString());
        ticker.highest_52_week_date = ticker_data.get("highest_52_week_date", "").asString();
        ticker.lowest_52_week_price = std::stod(ticker_data.get("lowest_52_week_price", "0").asString());
        ticker.lowest_52_week_date = ticker_data.get("lowest_52_week_date", "").asString();
        ticker.timestamp = ticker_data.get("timestamp", 0).asUInt64();
        
    } catch (const std::exception& e) {
        Logger::Error("Exception in ParseTicker: " + std::string(e.what()));
    }
    
    return ticker;
}

// WebSocket handlers
void UpbitExchange::OnWebSocketMessage(const std::string& message) {
    try {
        JsonParser parser;
        Json::Value json_data;
        if (!parser.Parse(message, json_data)) {
            Logger::Error("Failed to parse WebSocket message");
            return;
        }
        
        // Process different message types
        if (json_data.isMember("type")) {
            std::string type = json_data["type"].asString();
            
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
        Logger::Error("Exception in OnWebSocketMessage: " + std::string(e.what()));
    }
}

void UpbitExchange::OnWebSocketError(const std::string& error) {
    Logger::Error("Upbit WebSocket error: " + error);
}

void UpbitExchange::OnWebSocketClose() {
    Logger::Info("Upbit WebSocket connection closed");
}

// Helper methods
std::string UpbitExchange::GetServerTime() {
    try {
        Json::Value response;
        if (MakeRequest("/v1/market/all", "GET", "", response)) {
            auto now = std::chrono::system_clock::now();
            auto timestamp = std::chrono::duration_cast<std::chrono::milliseconds>(
                now.time_since_epoch()).count();
            return std::to_string(timestamp);
        }
    } catch (const std::exception& e) {
        Logger::Error("Exception in GetServerTime: " + std::string(e.what()));
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

} // namespace ats 