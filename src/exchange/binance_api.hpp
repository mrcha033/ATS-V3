#pragma once

#include <string>
#include <map>
#include <vector>
#include "../core/types.hpp"
#include "../network/rest_client.hpp"
#include "../utils/crypto_utils.hpp"

namespace ats {

struct BinanceTickerPrice {
    std::string symbol;
    double price;
    long long timestamp;
};

struct BinanceOrderBook {
    std::string symbol;
    std::vector<std::pair<double, double>> bids;
    std::vector<std::pair<double, double>> asks;
    long long last_update_id;
};

struct BinanceAccount {
    double maker_commission;
    double taker_commission;
    double buyer_commission;
    double seller_commission;
    bool can_trade;
    bool can_withdraw;
    bool can_deposit;
    std::vector<Balance> balances;
};

struct BinanceOrderResponse {
    std::string symbol;
    std::string order_id;
    std::string client_order_id;
    std::string transact_time;
    double price;
    double orig_qty;
    double executed_qty;
    double cummulative_quote_qty;
    std::string status;
    std::string time_in_force;
    std::string type;
    std::string side;
};

class BinanceAPI {
public:
    BinanceAPI(const std::string& api_key, const std::string& secret_key, 
               const std::string& base_url = "https://api.binance.com");
    
    // Market Data Endpoints
    BinanceTickerPrice get_ticker_price(const std::string& symbol);
    std::vector<BinanceTickerPrice> get_all_ticker_prices();
    BinanceOrderBook get_order_book(const std::string& symbol, int limit = 100);
    
    // Account Endpoints
    BinanceAccount get_account_info();
    std::vector<Balance> get_account_balances();
    
    // Trading Endpoints
    BinanceOrderResponse place_market_order(const std::string& symbol, const std::string& side, double quantity);
    BinanceOrderResponse place_limit_order(const std::string& symbol, const std::string& side, 
                                          double quantity, double price);
    BinanceOrderResponse cancel_order(const std::string& symbol, const std::string& order_id);
    BinanceOrderResponse get_order_status(const std::string& symbol, const std::string& order_id);
    
    // Utility Methods
    bool test_connectivity();
    long long get_server_time();
    
    // Error Handling
    struct APIError {
        int code;
        std::string message;
        bool is_error() const { return code != 0; }
    };
    
    APIError get_last_error() const { return last_error_; }

private:
    std::string api_key_;
    std::string secret_key_;
    std::string base_url_;
    RestClient rest_client_;
    APIError last_error_;
    
    // Helper methods
    std::string create_signature(const std::string& query_string);
    std::string build_query_string(const std::map<std::string, std::string>& params);
    HttpResponse make_signed_request(const std::string& endpoint, const std::string& method,
                                   const std::map<std::string, std::string>& params = {});
    HttpResponse make_public_request(const std::string& endpoint, 
                                   const std::map<std::string, std::string>& params = {});
    
    // Response parsing
    BinanceTickerPrice parse_ticker_price(const nlohmann::json& json);
    BinanceOrderBook parse_order_book(const nlohmann::json& json);
    BinanceAccount parse_account(const nlohmann::json& json);
    BinanceOrderResponse parse_order_response(const nlohmann::json& json);
    
    // Error handling
    void handle_api_error(const HttpResponse& response);
    bool is_rate_limited(const HttpResponse& response);
    void wait_for_rate_limit();
};

} // namespace ats