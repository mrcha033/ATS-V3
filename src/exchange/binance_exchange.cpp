#include "binance_exchange.hpp"
#include "../utils/json_parser.hpp"
#include <chrono>
#include <sstream>
#include <iomanip>
#include <algorithm>
#include <cmath>

namespace ats {

BinanceExchange::BinanceExchange(const std::string& api_key, const std::string& secret_key)
    : api_key_(api_key), secret_key_(secret_key), 
      base_url_("https://api.binance.com"), ws_url_("wss://stream.binance.com:9443/ws/"),
      maker_fee_(0.001), taker_fee_(0.001), rate_limit_(1200) {
    
    rest_client_ = std::make_unique<RestClient>();
    ws_client_ = std::make_unique<WebSocketClient>();
    
    // Initialize symbol mappings
    InitializeSymbolMappings();
    
    SetStatus(ExchangeStatus::DISCONNECTED);
}

BinanceExchange::~BinanceExchange() {
    Disconnect();
}

bool BinanceExchange::Connect() {
    try {
        SetStatus(ExchangeStatus::CONNECTING);
        
        // Test REST API connection
        auto response = MakePublicRequest("/api/v3/ping");
        if (response.empty()) {
            SetError("Failed to connect to Binance REST API");
            SetStatus(ExchangeStatus::ERROR);
            return false;
        }
        
        // Connect WebSocket
        ws_client_->SetMessageCallback([this](const std::string& msg) {
            OnWebSocketMessage(msg);
        });
        ws_client_->SetStateCallback([this](WebSocketState state) {
            OnWebSocketStateChange(state);
        });
        ws_client_->SetErrorCallback([this](const std::string& error) {
            OnWebSocketError(error);
        });
        
        if (!ws_client_->Connect(ws_url_)) {
            SetError("Failed to connect to Binance WebSocket");
            SetStatus(ExchangeStatus::ERROR);
            return false;
        }
        
        SetStatus(ExchangeStatus::CONNECTED);
        LOG_INFO("Connected to Binance exchange");
        return true;
        
    } catch (const std::exception& e) {
        SetError("Connection error: " + std::string(e.what()));
        SetStatus(ExchangeStatus::ERROR);
        return false;
    }
}

void BinanceExchange::Disconnect() {
    if (ws_client_) {
        ws_client_->Disconnect();
    }
    
    price_callbacks_.clear();
    orderbook_callbacks_.clear();
    
    SetStatus(ExchangeStatus::DISCONNECTED);
    LOG_INFO("Disconnected from Binance exchange");
}

ExchangeStatus BinanceExchange::GetStatus() const {
    return status_;
}

bool BinanceExchange::GetPrice(const std::string& symbol, Price& price) {
    try {
        std::string binance_symbol = ConvertSymbol(symbol);
        std::unordered_map<std::string, std::string> params = {{"symbol", binance_symbol}};
        
        auto response = MakePublicRequest("/api/v3/ticker/bookTicker", params);
        if (response.empty()) {
            SetError("Failed to get price for " + symbol);
            return false;
        }
        
        price = ParsePrice(response, symbol);
        return true;
        
    } catch (const std::exception& e) {
        SetError("Error getting price: " + std::string(e.what()));
        return false;
    }
}

bool BinanceExchange::GetOrderBook(const std::string& symbol, OrderBook& orderbook) {
    try {
        std::string binance_symbol = ConvertSymbol(symbol);
        std::unordered_map<std::string, std::string> params = {
            {"symbol", binance_symbol},
            {"limit", "100"}
        };
        
        auto response = MakePublicRequest("/api/v3/depth", params);
        if (response.empty()) {
            SetError("Failed to get order book for " + symbol);
            return false;
        }
        
        orderbook = ParseOrderBook(response, symbol);
        return true;
        
    } catch (const std::exception& e) {
        SetError("Error getting order book: " + std::string(e.what()));
        return false;
    }
}

std::vector<std::string> BinanceExchange::GetSupportedSymbols() {
    try {
        auto response = MakePublicRequest("/api/v3/exchangeInfo");
        if (response.empty()) {
            SetError("Failed to get exchange info");
            return {};
        }
        
        // Parse exchange info and extract symbols
        auto json = ats::json::ParseJson(response);
        auto symbols_array = ats::json::GetValue(json, "symbols");
        
        std::vector<std::string> supported_symbols;
        if (ats::json::IsArray(symbols_array)) {
            for (const auto& symbol_info : symbols_array) {
                if (ats::json::IsObject(symbol_info)) {
                    auto symbol_name = ats::json::GetString(ats::json::GetValue(symbol_info, "symbol"));
                    supported_symbols.push_back(ConvertSymbolBack(symbol_name));
                }
            }
        }
        
        return supported_symbols;
        
    } catch (const std::exception& e) {
        SetError("Error getting supported symbols: " + std::string(e.what()));
        return {};
    }
}

std::vector<Balance> BinanceExchange::GetBalances() {
    try {
        auto response = MakeAuthenticatedRequest("/api/v3/account");
        if (response.empty()) {
            SetError("Failed to get account balances");
            return {};
        }
        
        // Parse balances from response
        auto json = ats::json::ParseJson(response);
        auto balances_array = ats::json::GetValue(json, "balances");
        
        std::vector<Balance> balances;
        if (ats::json::IsArray(balances_array)) {
            for (const auto& balance_info : balances_array) {
                if (ats::json::IsObject(balance_info)) {
                    Balance balance;
                    balance.asset = ats::json::GetString(ats::json::GetValue(balance_info, "asset"));
                    balance.free = ats::json::GetNumber(ats::json::GetValue(balance_info, "free"));
                    balance.locked = ats::json::GetNumber(ats::json::GetValue(balance_info, "locked"));
                    
                    if (balance.total() > 0) {
                        balances.push_back(balance);
                    }
                }
            }
        }
        
        return balances;
        
    } catch (const std::exception& e) {
        SetError("Error getting balances: " + std::string(e.what()));
        return {};
    }
}

double BinanceExchange::GetBalance(const std::string& asset) {
    auto balances = GetBalances();
    for (const auto& balance : balances) {
        if (balance.asset == asset) {
            return balance.free;
        }
    }
    return 0.0;
}

std::string BinanceExchange::PlaceOrder(const std::string& symbol, const std::string& side, 
                                       const std::string& type, double quantity, double price) {
    try {
        std::string binance_symbol = ConvertSymbol(symbol);
        
        std::unordered_map<std::string, std::string> params = {
            {"symbol", binance_symbol},
            {"side", side},
            {"type", type},
            {"quantity", std::to_string(quantity)},
            {"timestamp", GetTimestamp()}
        };
        
        if (type == "LIMIT") {
            params["price"] = std::to_string(price);
            params["timeInForce"] = "GTC";
        }
        
        auto response = MakeAuthenticatedRequest("/api/v3/order", "POST", params);
        if (response.empty()) {
            SetError("Failed to place order");
            return "";
        }
        
        // Parse order ID from response
        auto json = ats::json::ParseJson(response);
        auto order_id = ats::json::GetString(ats::json::GetValue(json, "orderId"));
        
        LOG_INFO("Order placed successfully: {}", order_id);
        return order_id;
        
    } catch (const std::exception& e) {
        SetError("Error placing order: " + std::string(e.what()));
        return "";
    }
}

bool BinanceExchange::CancelOrder(const std::string& order_id) {
    try {
        // Note: This is simplified - real implementation would need symbol info
        SetError("Cancel order not fully implemented - need symbol information");
        return false;
        
    } catch (const std::exception& e) {
        SetError("Error cancelling order: " + std::string(e.what()));
        return false;
    }
}

Order BinanceExchange::GetOrder(const std::string& order_id) {
    Order order;
    try {
        // Note: This is simplified - real implementation would need symbol info
        SetError("Get order not fully implemented - need symbol information");
        return order;
        
    } catch (const std::exception& e) {
        SetError("Error getting order: " + std::string(e.what()));
        return order;
    }
}

std::vector<Order> BinanceExchange::GetOpenOrders(const std::string& symbol) {
    try {
        std::unordered_map<std::string, std::string> params = {{"timestamp", GetTimestamp()}};
        
        if (!symbol.empty()) {
            params["symbol"] = ConvertSymbol(symbol);
        }
        
        auto response = MakeAuthenticatedRequest("/api/v3/openOrders", "GET", params);
        if (response.empty()) {
            SetError("Failed to get open orders");
            return {};
        }
        
        // Parse orders from response
        std::vector<Order> orders;
        auto json = ats::json::ParseJson(response);
        
        if (ats::json::IsArray(json)) {
            for (const auto& order_info : json) {
                if (ats::json::IsObject(order_info)) {
                    orders.push_back(ParseOrder(ats::json::JsonToString(order_info)));
                }
            }
        }
        
        return orders;
        
    } catch (const std::exception& e) {
        SetError("Error getting open orders: " + std::string(e.what()));
        return {};
    }
}

bool BinanceExchange::SubscribeToPrice(const std::string& symbol, 
                                      std::function<void(const Price&)> callback) {
    price_callbacks_[symbol] = callback;
    
    // Send subscription message to WebSocket
    std::string binance_symbol = ConvertSymbol(symbol);
    std::transform(binance_symbol.begin(), binance_symbol.end(), binance_symbol.begin(), ::tolower);
    
    std::string subscribe_msg = R"({"method":"SUBSCRIBE","params":[")" + binance_symbol + R"(@ticker"],"id":1})";
    return ws_client_->SendMessage(subscribe_msg);
}

bool BinanceExchange::SubscribeToOrderBook(const std::string& symbol,
                                          std::function<void(const OrderBook&)> callback) {
    orderbook_callbacks_[symbol] = callback;
    
    // Send subscription message to WebSocket
    std::string binance_symbol = ConvertSymbol(symbol);
    std::transform(binance_symbol.begin(), binance_symbol.end(), binance_symbol.begin(), ::tolower);
    
    std::string subscribe_msg = R"({"method":"SUBSCRIBE","params":[")" + binance_symbol + R"(@depth"],"id":2})";
    return ws_client_->SendMessage(subscribe_msg);
}

bool BinanceExchange::UnsubscribeFromPrice(const std::string& symbol) {
    price_callbacks_.erase(symbol);
    
    // Send unsubscribe message to WebSocket
    std::string binance_symbol = ConvertSymbol(symbol);
    std::transform(binance_symbol.begin(), binance_symbol.end(), binance_symbol.begin(), ::tolower);
    
    std::string unsubscribe_msg = R"({"method":"UNSUBSCRIBE","params":[")" + binance_symbol + R"(@ticker"],"id":3})";
    return ws_client_->SendMessage(unsubscribe_msg);
}

bool BinanceExchange::UnsubscribeFromOrderBook(const std::string& symbol) {
    orderbook_callbacks_.erase(symbol);
    
    // Send unsubscribe message to WebSocket
    std::string binance_symbol = ConvertSymbol(symbol);
    std::transform(binance_symbol.begin(), binance_symbol.end(), binance_symbol.begin(), ::tolower);
    
    std::string unsubscribe_msg = R"({"method":"UNSUBSCRIBE","params":[")" + binance_symbol + R"(@depth"],"id":4})";
    return ws_client_->SendMessage(unsubscribe_msg);
}

double BinanceExchange::GetMinOrderSize(const std::string& symbol) const {
    // This would normally be fetched from exchange info
    return 0.001; // Default minimum
}

double BinanceExchange::GetMaxOrderSize(const std::string& symbol) const {
    // This would normally be fetched from exchange info
    return 1000000.0; // Default maximum
}

bool BinanceExchange::IsHealthy() const {
    return status_ == ExchangeStatus::CONNECTED && 
           ws_client_ && ws_client_->IsConnected();
}

// Private helper methods
std::string BinanceExchange::CreateSignature(const std::string& params) const {
    // Simplified signature creation - real implementation would use HMAC-SHA256
    // This is a placeholder implementation
    return "dummy_signature";
}

std::string BinanceExchange::GetTimestamp() const {
    auto now = std::chrono::system_clock::now();
    auto timestamp = std::chrono::duration_cast<std::chrono::milliseconds>(now.time_since_epoch());
    return std::to_string(timestamp.count());
}

std::string BinanceExchange::ConvertSymbol(const std::string& standard_symbol) const {
    auto it = symbol_map_.find(standard_symbol);
    if (it != symbol_map_.end()) {
        return it->second;
    }
    
    // Default conversion: remove '/' and make uppercase
    std::string result = standard_symbol;
    result.erase(std::remove(result.begin(), result.end(), '/'), result.end());
    std::transform(result.begin(), result.end(), result.begin(), ::toupper);
    return result;
}

std::string BinanceExchange::ConvertSymbolBack(const std::string& binance_symbol) const {
    // Convert from Binance format back to standard format
    // This is simplified - real implementation would use exchange info
    if (binance_symbol.length() >= 6) {
        std::string base = binance_symbol.substr(0, 3);
        std::string quote = binance_symbol.substr(3);
        return base + "/" + quote;
    }
    return binance_symbol;
}

std::string BinanceExchange::MakeAuthenticatedRequest(const std::string& endpoint, 
                                                     const std::string& method,
                                                     const std::unordered_map<std::string, std::string>& params) {
    // Build query string
    std::string query_string;
    for (const auto& param : params) {
        if (!query_string.empty()) query_string += "&";
        query_string += param.first + "=" + param.second;
    }
    
    // Create signature
    std::string signature = CreateSignature(query_string);
    query_string += "&signature=" + signature;
    
    // Make request with authentication headers
    std::unordered_map<std::string, std::string> headers = {
        {"X-MBX-APIKEY", api_key_}
    };
    
    std::string url = base_url_ + endpoint + "?" + query_string;
    
    if (method == "GET") {
        auto response = rest_client_->Get(url, headers);
        return response.body;
    } else if (method == "POST") {
        auto response = rest_client_->Post(url, "", headers);
        return response.body;
    }
    
    return "";
}

std::string BinanceExchange::MakePublicRequest(const std::string& endpoint,
                                              const std::unordered_map<std::string, std::string>& params) {
    std::string url = base_url_ + endpoint;
    
    if (!params.empty()) {
        url += "?";
        bool first = true;
        for (const auto& param : params) {
            if (!first) url += "&";
            url += param.first + "=" + param.second;
            first = false;
        }
    }
    
    auto response = rest_client_->Get(url);
    return response.body;
}

void BinanceExchange::OnWebSocketMessage(const std::string& message) {
    try {
        auto json = ats::json::ParseJson(message);
        
        // Handle different message types based on stream name
        if (ats::json::HasKey(json, "stream")) {
            std::string stream = ats::json::GetString(ats::json::GetValue(json, "stream"));
            
            if (stream.find("@ticker") != std::string::npos) {
                // Price update
                auto data = ats::json::GetValue(json, "data");
                std::string symbol = ats::json::GetString(ats::json::GetValue(data, "s"));
                std::string standard_symbol = ConvertSymbolBack(symbol);
                
                auto it = price_callbacks_.find(standard_symbol);
                if (it != price_callbacks_.end()) {
                    Price price = ParsePrice(ats::json::JsonToString(data), standard_symbol);
                    it->second(price);
                }
            } else if (stream.find("@depth") != std::string::npos) {
                // Order book update
                auto data = ats::json::GetValue(json, "data");
                std::string symbol = ats::json::GetString(ats::json::GetValue(data, "s"));
                std::string standard_symbol = ConvertSymbolBack(symbol);
                
                auto it = orderbook_callbacks_.find(standard_symbol);
                if (it != orderbook_callbacks_.end()) {
                    OrderBook orderbook = ParseOrderBook(ats::json::JsonToString(data), standard_symbol);
                    it->second(orderbook);
                }
            }
        }
        
    } catch (const std::exception& e) {
        LOG_ERROR("Error processing WebSocket message: {}", e.what());
    }
}

void BinanceExchange::OnWebSocketStateChange(WebSocketState state) {
    LOG_INFO("Binance WebSocket state changed to: {}", static_cast<int>(state));
}

void BinanceExchange::OnWebSocketError(const std::string& error) {
    LOG_ERROR("Binance WebSocket error: {}", error);
    SetError("WebSocket error: " + error);
}

Price BinanceExchange::ParsePrice(const std::string& json_data, const std::string& symbol) {
    Price price;
    price.symbol = symbol;
    
    try {
        auto json = ats::json::ParseJson(json_data);
        
        if (ats::json::HasKey(json, "bidPrice")) {
            price.bid = ats::json::GetNumber(ats::json::GetValue(json, "bidPrice"));
        }
        if (ats::json::HasKey(json, "askPrice")) {
            price.ask = ats::json::GetNumber(ats::json::GetValue(json, "askPrice"));
        }
        if (ats::json::HasKey(json, "price")) {
            price.last = ats::json::GetNumber(ats::json::GetValue(json, "price"));
        }
        if (ats::json::HasKey(json, "volume")) {
            price.volume = ats::json::GetNumber(ats::json::GetValue(json, "volume"));
        }
        
        // Set timestamp
        auto now = std::chrono::system_clock::now();
        price.timestamp = std::chrono::duration_cast<std::chrono::milliseconds>(now.time_since_epoch()).count();
        
    } catch (const std::exception& e) {
        LOG_ERROR("Error parsing price data: {}", e.what());
    }
    
    return price;
}

OrderBook BinanceExchange::ParseOrderBook(const std::string& json_data, const std::string& symbol) {
    OrderBook orderbook;
    orderbook.symbol = symbol;
    
    try {
        auto json = ats::json::ParseJson(json_data);
        
        // Parse bids
        if (ats::json::HasKey(json, "bids")) {
            auto bids_array = ats::json::GetValue(json, "bids");
            if (ats::json::IsArray(bids_array)) {
                for (const auto& bid_data : bids_array) {
                    if (ats::json::IsArray(bid_data)) {
                        if (ats::json::GetSize(bid_data) >= 2) {
                            double price = ats::json::GetNumber(bid_data[0]);
                            double volume = ats::json::GetNumber(bid_data[1]);
                            orderbook.bids.emplace_back(price, volume);
                        }
                    }
                }
            }
        }
        
        // Parse asks
        if (ats::json::HasKey(json, "asks")) {
            auto asks_array = ats::json::GetValue(json, "asks");
            if (ats::json::IsArray(asks_array)) {
                for (const auto& ask_data : asks_array) {
                    if (ats::json::IsArray(ask_data)) {
                        if (ats::json::GetSize(ask_data) >= 2) {
                            double price = ats::json::GetNumber(ask_data[0]);
                            double volume = ats::json::GetNumber(ask_data[1]);
                            orderbook.asks.emplace_back(price, volume);
                        }
                    }
                }
            }
        }
        
        // Set timestamp
        auto now = std::chrono::system_clock::now();
        orderbook.timestamp = std::chrono::duration_cast<std::chrono::milliseconds>(now.time_since_epoch()).count();
        
    } catch (const std::exception& e) {
        LOG_ERROR("Error parsing order book data: {}", e.what());
    }
    
    return orderbook;
}

Order BinanceExchange::ParseOrder(const std::string& json_data) {
    Order order;
    
    try {
        auto json = ats::json::ParseJson(json_data);
        
        order.order_id = ats::json::GetString(ats::json::GetValue(json, "orderId"));
        order.exchange = "binance";
        order.symbol = ConvertSymbolBack(ats::json::GetString(ats::json::GetValue(json, "symbol")));
        
        std::string side = ats::json::GetString(ats::json::GetValue(json, "side"));
        order.side = (side == "BUY") ? OrderSide::BUY : OrderSide::SELL;
        
        std::string type = ats::json::GetString(ats::json::GetValue(json, "type"));
        order.type = (type == "MARKET") ? OrderType::MARKET : OrderType::LIMIT;
        
        order.quantity = ats::json::GetNumber(ats::json::GetValue(json, "origQty"));
        order.price = ats::json::GetNumber(ats::json::GetValue(json, "price"));
        order.filled_quantity = ats::json::GetNumber(ats::json::GetValue(json, "executedQty"));
        
        std::string status = ats::json::GetString(ats::json::GetValue(json, "status"));
        if (status == "NEW") order.status = OrderStatus::NEW;
        else if (status == "PARTIALLY_FILLED") order.status = OrderStatus::PARTIAL;
        else if (status == "FILLED") order.status = OrderStatus::FILLED;
        else if (status == "CANCELED") order.status = OrderStatus::CANCELLED;
        else if (status == "REJECTED") order.status = OrderStatus::REJECTED;
        else order.status = OrderStatus::PENDING;
        
        order.timestamp = static_cast<long long>(ats::json::GetNumber(ats::json::GetValue(json, "time")));
        
    } catch (const std::exception& e) {
        LOG_ERROR("Error parsing order data: {}", e.what());
    }
    
    return order;
}

Balance BinanceExchange::ParseBalance(const std::string& json_data) {
    Balance balance;
    
    try {
        auto json = ats::json::ParseJson(json_data);
        
        balance.asset = ats::json::GetString(ats::json::GetValue(json, "asset"));
        balance.free = ats::json::GetNumber(ats::json::GetValue(json, "free"));
        balance.locked = ats::json::GetNumber(ats::json::GetValue(json, "locked"));
        
    } catch (const std::exception& e) {
        LOG_ERROR("Error parsing balance data: {}", e.what());
    }
    
    return balance;
}

void BinanceExchange::InitializeSymbolMappings() {
    // Standard symbol -> Binance symbol mapping
    symbol_map_["BTC/USDT"] = "BTCUSDT";
    symbol_map_["ETH/USDT"] = "ETHUSDT";
    symbol_map_["BNB/USDT"] = "BNBUSDT";
    symbol_map_["ADA/USDT"] = "ADAUSDT";
    symbol_map_["SOL/USDT"] = "SOLUSDT";
    symbol_map_["DOT/USDT"] = "DOTUSDT";
    symbol_map_["LINK/USDT"] = "LINKUSDT";
    symbol_map_["UNI/USDT"] = "UNIUSDT";
    symbol_map_["LTC/USDT"] = "LTCUSDT";
    symbol_map_["BCH/USDT"] = "BCHUSDT";
}

} // namespace ats 