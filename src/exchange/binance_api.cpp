#include "binance_api.hpp"
#include "../utils/structured_logger.hpp"
#include <nlohmann/json.hpp>
#include <thread>
#include <chrono>

namespace ats {

BinanceAPI::BinanceAPI(const std::string& api_key, const std::string& secret_key, const std::string& base_url)
    : api_key_(api_key), secret_key_(secret_key), base_url_(base_url), last_error_{0, ""} {
    
    rest_client_.SetBaseUrl(base_url);
    rest_client_.SetUserAgent("ATS-V3/1.0");
    rest_client_.AddHeader("Content-Type", "application/json");
    rest_client_.AddHeader("X-MBX-APIKEY", api_key);
}

BinanceTickerPrice BinanceAPI::get_ticker_price(const std::string& symbol) {
    SLOG_DEBUG("Getting ticker price", {{"symbol", symbol}});
    
    std::map<std::string, std::string> params;
    params["symbol"] = symbol;
    
    auto response = make_public_request("/api/v3/ticker/price", params);
    
    if (!response.IsSuccess()) {
        handle_api_error(response);
        return BinanceTickerPrice{symbol, 0.0, 0};
    }
    
    try {
        auto json = nlohmann::json::parse(response.body);
        return parse_ticker_price(json);
    } catch (const std::exception& e) {
        SLOG_ERROR("Failed to parse ticker price response", {
            {"symbol", symbol},
            {"error", e.what()},
            {"response", response.body}
        });
        last_error_ = {-1, "JSON parse error: " + std::string(e.what())};
        return BinanceTickerPrice{symbol, 0.0, 0};
    }
}

std::vector<BinanceTickerPrice> BinanceAPI::get_all_ticker_prices() {
    SLOG_DEBUG("Getting all ticker prices");
    
    auto response = make_public_request("/api/v3/ticker/price");
    
    if (!response.IsSuccess()) {
        handle_api_error(response);
        return {};
    }
    
    try {
        auto json = nlohmann::json::parse(response.body);
        std::vector<BinanceTickerPrice> prices;
        
        for (const auto& item : json) {
            prices.push_back(parse_ticker_price(item));
        }
        
        SLOG_DEBUG("Retrieved ticker prices", {{"count", std::to_string(prices.size())}});
        return prices;
    } catch (const std::exception& e) {
        SLOG_ERROR("Failed to parse ticker prices response", {
            {"error", e.what()},
            {"response", response.body.substr(0, 200) + "..."}
        });
        last_error_ = {-1, "JSON parse error: " + std::string(e.what())};
        return {};
    }
}

BinanceOrderBook BinanceAPI::get_order_book(const std::string& symbol, int limit) {
    SLOG_DEBUG("Getting order book", {{"symbol", symbol}, {"limit", std::to_string(limit)}});
    
    std::map<std::string, std::string> params;
    params["symbol"] = symbol;
    params["limit"] = std::to_string(limit);
    
    auto response = make_public_request("/api/v3/depth", params);
    
    if (!response.IsSuccess()) {
        handle_api_error(response);
        return BinanceOrderBook{symbol, {}, {}, 0};
    }
    
    try {
        auto json = nlohmann::json::parse(response.body);
        return parse_order_book(json);
    } catch (const std::exception& e) {
        SLOG_ERROR("Failed to parse order book response", {
            {"symbol", symbol},
            {"error", e.what()},
            {"response", response.body.substr(0, 200) + "..."}
        });
        last_error_ = {-1, "JSON parse error: " + std::string(e.what())};
        return BinanceOrderBook{symbol, {}, {}, 0};
    }
}

BinanceAccount BinanceAPI::get_account_info() {
    SLOG_DEBUG("Getting account info");
    
    auto response = make_signed_request("/api/v3/account", "GET");
    
    if (!response.IsSuccess()) {
        handle_api_error(response);
        return BinanceAccount{};
    }
    
    try {
        auto json = nlohmann::json::parse(response.body);
        return parse_account(json);
    } catch (const std::exception& e) {
        SLOG_ERROR("Failed to parse account response", {
            {"error", e.what()},
            {"response", response.body.substr(0, 200) + "..."}
        });
        last_error_ = {-1, "JSON parse error: " + std::string(e.what())};
        return BinanceAccount{};
    }
}

BinanceOrderResponse BinanceAPI::place_market_order(const std::string& symbol, const std::string& side, double quantity) {
    SLOG_INFO("Placing market order", {
        {"symbol", symbol},
        {"side", side},
        {"quantity", std::to_string(quantity)}
    });
    
    std::map<std::string, std::string> params;
    params["symbol"] = symbol;
    params["side"] = side;
    params["type"] = "MARKET";
    params["quantity"] = std::to_string(quantity);
    params["newClientOrderId"] = "ATS_" + CryptoUtils::generate_random_string(8);
    
    auto response = make_signed_request("/api/v3/order", "POST", params);
    
    if (!response.IsSuccess()) {
        handle_api_error(response);
        return BinanceOrderResponse{};
    }
    
    try {
        auto json = nlohmann::json::parse(response.body);
        auto order_result = parse_order_response(json);
        
        SLOG_TRADE(symbol, side, 0.0, quantity, order_result.order_id);
        return order_result;
    } catch (const std::exception& e) {
        SLOG_ERROR("Failed to parse order response", {
            {"symbol", symbol},
            {"error", e.what()},
            {"response", response.body}
        });
        last_error_ = {-1, "JSON parse error: " + std::string(e.what())};
        return BinanceOrderResponse{};
    }
}

BinanceOrderResponse BinanceAPI::place_limit_order(const std::string& symbol, const std::string& side, 
                                                  double quantity, double price) {
    SLOG_INFO("Placing limit order", {
        {"symbol", symbol},
        {"side", side},
        {"quantity", std::to_string(quantity)},
        {"price", std::to_string(price)}
    });
    
    std::map<std::string, std::string> params;
    params["symbol"] = symbol;
    params["side"] = side;
    params["type"] = "LIMIT";
    params["timeInForce"] = "GTC";
    params["quantity"] = std::to_string(quantity);
    params["price"] = std::to_string(price);
    params["newClientOrderId"] = "ATS_" + CryptoUtils::generate_random_string(8);
    
    auto response = make_signed_request("/api/v3/order", "POST", params);
    
    if (!response.IsSuccess()) {
        handle_api_error(response);
        return BinanceOrderResponse{};
    }
    
    try {
        auto json = nlohmann::json::parse(response.body);
        auto order_result = parse_order_response(json);
        
        SLOG_TRADE(symbol, side, price, quantity, order_result.order_id);
        return order_result;
    } catch (const std::exception& e) {
        SLOG_ERROR("Failed to parse order response", {
            {"symbol", symbol},
            {"error", e.what()},
            {"response", response.body}
        });
        last_error_ = {-1, "JSON parse error: " + std::string(e.what())};
        return BinanceOrderResponse{};
    }
}

bool BinanceAPI::test_connectivity() {
    SLOG_DEBUG("Testing Binance connectivity");
    
    auto response = make_public_request("/api/v3/ping");
    bool success = response.IsSuccess();
    
    if (success) {
        SLOG_INFO("Binance connectivity test passed");
    } else {
        SLOG_ERROR("Binance connectivity test failed", {
            {"status_code", std::to_string(response.status_code)},
            {"error", response.error_message}
        });
    }
    
    return success;
}

long long BinanceAPI::get_server_time() {
    auto response = make_public_request("/api/v3/time");
    
    if (!response.IsSuccess()) {
        handle_api_error(response);
        return 0;
    }
    
    try {
        auto json = nlohmann::json::parse(response.body);
        return json["serverTime"].get<long long>();
    } catch (const std::exception& e) {
        SLOG_ERROR("Failed to parse server time", {{"error", e.what()}});
        return 0;
    }
}

std::string BinanceAPI::create_signature(const std::string& query_string) {
    return CryptoUtils::hmac_sha256(secret_key_, query_string);
}

std::string BinanceAPI::build_query_string(const std::map<std::string, std::string>& params) {
    std::string query;
    bool first = true;
    
    for (const auto& [key, value] : params) {
        if (!first) {
            query += "&";
        }
        query += key + "=" + CryptoUtils::url_encode(value);
        first = false;
    }
    
    return query;
}

HttpResponse BinanceAPI::make_signed_request(const std::string& endpoint, const std::string& method,
                                           const std::map<std::string, std::string>& params) {
    auto mutable_params = params;
    mutable_params["timestamp"] = std::to_string(CryptoUtils::current_timestamp_ms());
    
    std::string query_string = build_query_string(mutable_params);
    std::string signature = create_signature(query_string);
    query_string += "&signature=" + signature;
    
    std::string url = endpoint + "?" + query_string;
    
    if (method == "GET") {
        return rest_client_.Get(url);
    } else if (method == "POST") {
        return rest_client_.Post(url, "");
    } else if (method == "DELETE") {
        return rest_client_.Delete(url);
    }
    
    return HttpResponse{0, "", {}, 0, "Unsupported method: " + method};
}

HttpResponse BinanceAPI::make_public_request(const std::string& endpoint, 
                                           const std::map<std::string, std::string>& params) {
    std::string url = endpoint;
    
    if (!params.empty()) {
        url += "?" + build_query_string(params);
    }
    
    return rest_client_.Get(url);
}

BinanceTickerPrice BinanceAPI::parse_ticker_price(const nlohmann::json& json) {
    return BinanceTickerPrice{
        json["symbol"].get<std::string>(),
        std::stod(json["price"].get<std::string>()),
        CryptoUtils::current_timestamp_ms()
    };
}

BinanceOrderBook BinanceAPI::parse_order_book(const nlohmann::json& json) {
    BinanceOrderBook book;
    book.symbol = ""; // Symbol not included in response
    book.last_update_id = json["lastUpdateId"].get<long long>();
    
    for (const auto& bid : json["bids"]) {
        double price = std::stod(bid[0].get<std::string>());
        double quantity = std::stod(bid[1].get<std::string>());
        book.bids.emplace_back(price, quantity);
    }
    
    for (const auto& ask : json["asks"]) {
        double price = std::stod(ask[0].get<std::string>());
        double quantity = std::stod(ask[1].get<std::string>());
        book.asks.emplace_back(price, quantity);
    }
    
    return book;
}

BinanceAccount BinanceAPI::parse_account(const nlohmann::json& json) {
    BinanceAccount account;
    account.maker_commission = json["makerCommission"].get<double>();
    account.taker_commission = json["takerCommission"].get<double>();
    account.buyer_commission = json["buyerCommission"].get<double>();
    account.seller_commission = json["sellerCommission"].get<double>();
    account.can_trade = json["canTrade"].get<bool>();
    account.can_withdraw = json["canWithdraw"].get<bool>();
    account.can_deposit = json["canDeposit"].get<bool>();
    
    for (const auto& balance_json : json["balances"]) {
        Balance balance;
        balance.asset = balance_json["asset"].get<std::string>();
        balance.free = std::stod(balance_json["free"].get<std::string>());
        balance.locked = std::stod(balance_json["locked"].get<std::string>());
        account.balances.push_back(balance);
    }
    
    return account;
}

BinanceOrderResponse BinanceAPI::parse_order_response(const nlohmann::json& json) {
    BinanceOrderResponse response;
    response.symbol = json["symbol"].get<std::string>();
    response.order_id = std::to_string(json["orderId"].get<long long>());
    response.client_order_id = json["clientOrderId"].get<std::string>();
    response.transact_time = std::to_string(json["transactTime"].get<long long>());
    
    if (json.contains("price")) {
        response.price = std::stod(json["price"].get<std::string>());
    }
    if (json.contains("origQty")) {
        response.orig_qty = std::stod(json["origQty"].get<std::string>());
    }
    if (json.contains("executedQty")) {
        response.executed_qty = std::stod(json["executedQty"].get<std::string>());
    }
    if (json.contains("cummulativeQuoteQty")) {
        response.cummulative_quote_qty = std::stod(json["cummulativeQuoteQty"].get<std::string>());
    }
    
    response.status = json["status"].get<std::string>();
    response.type = json["type"].get<std::string>();
    response.side = json["side"].get<std::string>();
    
    return response;
}

void BinanceAPI::handle_api_error(const HttpResponse& response) {
    try {
        auto json = nlohmann::json::parse(response.body);
        last_error_.code = json["code"].get<int>();
        last_error_.message = json["msg"].get<std::string>();
        
        SLOG_ERROR("Binance API error", {
            {"code", std::to_string(last_error_.code)},
            {"message", last_error_.message},
            {"status_code", std::to_string(response.status_code)}
        });
        
        if (is_rate_limited(response)) {
            wait_for_rate_limit();
        }
    } catch (const std::exception& e) {
        last_error_.code = response.status_code;
        last_error_.message = "HTTP error: " + response.error_message;
        
        SLOG_ERROR("Binance HTTP error", {
            {"status_code", std::to_string(response.status_code)},
            {"error", response.error_message}
        });
    }
}

bool BinanceAPI::is_rate_limited(const HttpResponse& response) {
    return response.status_code == 429 || last_error_.code == -1003;
}

void BinanceAPI::wait_for_rate_limit() {
    SLOG_WARNING("Rate limited, waiting before retry");
    std::this_thread::sleep_for(std::chrono::seconds(1));
}

} // namespace ats