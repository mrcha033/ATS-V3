#pragma once

#include "exchange_interface.hpp"
#include "binance_api.hpp"
#include "../core/app_state.hpp"
#include "../utils/structured_logger.hpp"
#include "../monitoring/performance_monitor.hpp"
#include <memory>
#include <atomic>
#include <chrono>

namespace ats {

class BinanceExchange : public ExchangeInterface {
public:
    BinanceExchange(const ExchangeConfig& config, AppState* app_state);
    ~BinanceExchange() override;

    std::string get_name() const override;
    void connect() override;
    void disconnect() override;
    Price get_price(const std::string& symbol) override;
    OrderResult place_order(const Order& order) override;
    
    // Additional methods for Binance-specific functionality
    std::vector<Balance> get_balances();
    bool test_connectivity();
    ExchangeStatus get_status() const { return status_; }

private:
    ExchangeConfig config_;
    AppState* app_state_;
    std::unique_ptr<BinanceAPI> api_;
    std::atomic<ExchangeStatus> status_;
    std::chrono::steady_clock::time_point last_heartbeat_;
    
    // Helper methods
    bool initialize_api();
    bool validate_symbol(const std::string& symbol);
    std::string convert_order_side(OrderSide side);
    std::string convert_order_type(OrderType type);
    OrderStatus convert_binance_status(const std::string& status);
    Price convert_ticker_to_price(const BinanceTickerPrice& ticker);
    OrderResult convert_binance_order(const BinanceOrderResponse& response);
    
    // Error handling
    void handle_connection_error(const std::string& error);
    void update_heartbeat();
    bool is_connected() const;
};

} // namespace ats 