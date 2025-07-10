#pragma once

#include "order_router.hpp"
#include "exchange/exchange_interface.hpp"
#include "types/common_types.hpp"
#include <memory>
#include <unordered_map>
#include <chrono>

namespace ats {
namespace trading_engine {

// Adapter that bridges existing ExchangeInterface with ExchangeTradingInterface
class ExchangeTradingAdapter : public ExchangeTradingInterface {
public:
    ExchangeTradingAdapter(std::shared_ptr<ats::ExchangeInterface> exchange);
    ~ExchangeTradingAdapter() override;
    
    // Basic trading operations
    std::string place_order(const types::Order& order) override;
    bool cancel_order(const std::string& order_id) override;
    OrderExecutionDetails get_order_status(const std::string& order_id) override;
    std::vector<OrderExecutionDetails> get_active_orders() override;
    
    // Advanced operations
    std::string place_conditional_order(const types::Order& order, 
                                       const std::string& condition) override;
    bool modify_order(const std::string& order_id, double new_price, double new_quantity) override;
    
    // Account information
    std::vector<types::Balance> get_account_balances() override;
    types::Balance get_balance(const types::Currency& currency) override;
    double get_available_balance(const types::Currency& currency) override;
    
    // Trading limits and fees
    double get_minimum_order_size(const std::string& symbol) override;
    double get_maximum_order_size(const std::string& symbol) override;
    double get_trading_fee(const std::string& symbol, bool is_maker = false) override;
    
    // Market data for trading
    types::Ticker get_current_ticker(const std::string& symbol) override;
    std::vector<std::pair<double, double>> get_order_book(const std::string& symbol, int depth = 20) override;
    
    // Exchange specific information
    std::string get_exchange_id() const override;
    bool is_connected() const override;
    std::chrono::milliseconds get_average_latency() const override;
    bool is_market_open() const override;
    
    // Error handling and diagnostics
    std::string get_last_error() const override;
    void clear_error() override;
    bool is_healthy() const override;

private:
    std::shared_ptr<ats::ExchangeInterface> exchange_;
    std::unordered_map<std::string, OrderExecutionDetails> order_tracking_;
    std::string last_error_;
    std::atomic<std::chrono::milliseconds> average_latency_{std::chrono::milliseconds(100)};
    mutable std::shared_mutex mutex_;
    
    // Conversion helpers
    ats::Order convert_to_legacy_order(const types::Order& order);
    types::Ticker convert_from_legacy_price(const ats::Price& price, const std::string& symbol);
    OrderExecutionDetails create_order_details(const std::string& order_id, const types::Order& order);
    
    // Mock implementations for missing functionality
    std::vector<types::Balance> mock_get_balances();
    types::Balance mock_get_balance(const types::Currency& currency);
    std::vector<std::pair<double, double>> mock_get_order_book(const std::string& symbol);
};

// Factory for creating exchange trading adapters
class ExchangeTradingAdapterFactory {
public:
    static std::unique_ptr<ExchangeTradingAdapter> create_binance_adapter(
        const ExchangeConfig& config, AppState* app_state);
    
    static std::unique_ptr<ExchangeTradingAdapter> create_upbit_adapter(
        const ExchangeConfig& config, AppState* app_state);
    
    static std::unique_ptr<ExchangeTradingAdapter> create_adapter(
        const std::string& exchange_name, 
        const ExchangeConfig& config, 
        AppState* app_state);

private:
    static std::shared_ptr<ats::ExchangeInterface> create_exchange_interface(
        const std::string& exchange_name,
        const ExchangeConfig& config,
        AppState* app_state);
};

// REST API client for enhanced exchange communication
class ExchangeRestClient {
public:
    ExchangeRestClient(const std::string& base_url, const std::string& api_key, const std::string& secret);
    ~ExchangeRestClient();
    
    // HTTP methods
    std::string get(const std::string& endpoint, const std::map<std::string, std::string>& params = {});
    std::string post(const std::string& endpoint, const std::string& body, 
                    const std::map<std::string, std::string>& headers = {});
    std::string delete_request(const std::string& endpoint, const std::map<std::string, std::string>& params = {});
    
    // Authentication
    std::map<std::string, std::string> create_signed_headers(const std::string& method, 
                                                           const std::string& endpoint,
                                                           const std::string& body = "");
    
    // Rate limiting
    void set_rate_limit(int requests_per_second);
    bool check_rate_limit();
    
    // Error handling
    bool is_success_status(int status_code);
    std::string parse_error_message(const std::string& response);
    
private:
    struct Implementation;
    std::unique_ptr<Implementation> impl_;
    
    std::string create_signature(const std::string& message);
    std::string create_timestamp();
    void wait_for_rate_limit();
};

// Enhanced Binance trading interface with full REST API support
class BinanceTradingInterface : public ExchangeTradingInterface {
public:
    BinanceTradingInterface(const std::string& api_key, const std::string& secret, bool testnet = false);
    ~BinanceTradingInterface() override;
    
    // Basic trading operations
    std::string place_order(const types::Order& order) override;
    bool cancel_order(const std::string& order_id) override;
    OrderExecutionDetails get_order_status(const std::string& order_id) override;
    std::vector<OrderExecutionDetails> get_active_orders() override;
    
    // Advanced operations
    std::string place_conditional_order(const types::Order& order, 
                                       const std::string& condition) override;
    bool modify_order(const std::string& order_id, double new_price, double new_quantity) override;
    
    // Account information
    std::vector<types::Balance> get_account_balances() override;
    types::Balance get_balance(const types::Currency& currency) override;
    double get_available_balance(const types::Currency& currency) override;
    
    // Trading limits and fees
    double get_minimum_order_size(const std::string& symbol) override;
    double get_maximum_order_size(const std::string& symbol) override;
    double get_trading_fee(const std::string& symbol, bool is_maker = false) override;
    
    // Market data for trading
    types::Ticker get_current_ticker(const std::string& symbol) override;
    std::vector<std::pair<double, double>> get_order_book(const std::string& symbol, int depth = 20) override;
    
    // Exchange specific information
    std::string get_exchange_id() const override;
    bool is_connected() const override;
    std::chrono::milliseconds get_average_latency() const override;
    bool is_market_open() const override;
    
    // Error handling and diagnostics
    std::string get_last_error() const override;
    void clear_error() override;
    bool is_healthy() const override;

private:
    std::unique_ptr<ExchangeRestClient> rest_client_;
    std::string exchange_id_;
    std::string last_error_;
    std::atomic<bool> connected_{false};
    std::atomic<std::chrono::milliseconds> average_latency_{std::chrono::milliseconds(100)};
    mutable std::shared_mutex mutex_;
    
    // Binance specific implementations
    std::string place_binance_order(const types::Order& order);
    OrderExecutionDetails parse_binance_order_response(const std::string& response);
    std::vector<types::Balance> parse_binance_balances(const std::string& response);
    types::Ticker parse_binance_ticker(const std::string& response);
    
    // Helper methods
    std::string order_side_to_string(types::OrderSide side);
    std::string order_type_to_string(types::OrderType type);
    std::string symbol_to_binance_format(const std::string& symbol);
    double round_to_tick_size(double price, const std::string& symbol);
    double round_to_step_size(double quantity, const std::string& symbol);
};

// Enhanced Upbit trading interface
class UpbitTradingInterface : public ExchangeTradingInterface {
public:
    UpbitTradingInterface(const std::string& access_key, const std::string& secret_key);
    ~UpbitTradingInterface() override;
    
    // Basic trading operations
    std::string place_order(const types::Order& order) override;
    bool cancel_order(const std::string& order_id) override;
    OrderExecutionDetails get_order_status(const std::string& order_id) override;
    std::vector<OrderExecutionDetails> get_active_orders() override;
    
    // Advanced operations
    std::string place_conditional_order(const types::Order& order, 
                                       const std::string& condition) override;
    bool modify_order(const std::string& order_id, double new_price, double new_quantity) override;
    
    // Account information
    std::vector<types::Balance> get_account_balances() override;
    types::Balance get_balance(const types::Currency& currency) override;
    double get_available_balance(const types::Currency& currency) override;
    
    // Trading limits and fees
    double get_minimum_order_size(const std::string& symbol) override;
    double get_maximum_order_size(const std::string& symbol) override;
    double get_trading_fee(const std::string& symbol, bool is_maker = false) override;
    
    // Market data for trading
    types::Ticker get_current_ticker(const std::string& symbol) override;
    std::vector<std::pair<double, double>> get_order_book(const std::string& symbol, int depth = 20) override;
    
    // Exchange specific information
    std::string get_exchange_id() const override;
    bool is_connected() const override;
    std::chrono::milliseconds get_average_latency() const override;
    bool is_market_open() const override;
    
    // Error handling and diagnostics
    std::string get_last_error() const override;
    void clear_error() override;
    bool is_healthy() const override;

private:
    std::unique_ptr<ExchangeRestClient> rest_client_;
    std::string exchange_id_;
    std::string last_error_;
    std::atomic<bool> connected_{false};
    std::atomic<std::chrono::milliseconds> average_latency_{std::chrono::milliseconds(150)};
    mutable std::shared_mutex mutex_;
    
    // Upbit specific implementations
    std::string place_upbit_order(const types::Order& order);
    OrderExecutionDetails parse_upbit_order_response(const std::string& response);
    std::vector<types::Balance> parse_upbit_balances(const std::string& response);
    types::Ticker parse_upbit_ticker(const std::string& response);
    
    // Helper methods
    std::string order_side_to_string(types::OrderSide side);
    std::string order_type_to_string(types::OrderType type);
    std::string symbol_to_upbit_format(const std::string& symbol);
    std::string create_jwt_token(const std::string& payload);
};

} // namespace trading_engine
} // namespace ats