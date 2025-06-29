#pragma once

#include "exchange_interface.hpp"
#include "core/app_state.hpp"
#include <memory>

namespace ats {

class UpbitExchange : public ExchangeInterface {
public:
    UpbitExchange(const nlohmann::json& config, ats::AppState* app_state);

    std::string get_name() const override;
    void connect() override;
    void disconnect() override;
    Price get_price(const std::string& symbol) override;

private:
    nlohmann::json config_;
    ats::AppState* app_state_;
};

} 