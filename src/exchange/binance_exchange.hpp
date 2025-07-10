#pragma once

#include "exchange_interface.hpp"
#include "core/app_state.hpp"
#include <memory>

namespace ats {

class BinanceExchange : public ExchangeInterface {
public:
    BinanceExchange(const ExchangeConfig& config, ats::AppState* app_state);

    std::string get_name() const override;
    void connect() override;
    void disconnect() override;
    Price get_price(const std::string& symbol) override;
    OrderResult place_order(const Order& order) override;

private:
    ExchangeConfig config_;
    ats::AppState* app_state_;
};

} 