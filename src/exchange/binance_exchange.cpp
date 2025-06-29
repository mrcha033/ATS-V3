#include "binance_exchange.hpp"
#include "../core/app_state.hpp"

namespace ats {

BinanceExchange::BinanceExchange(const nlohmann::json& config, AppState* app_state)
    : config_(config), app_state_(app_state) {}

std::string BinanceExchange::get_name() const {
    return "binance";
}

void BinanceExchange::connect() {
    // Connect to Binance
}

void BinanceExchange::disconnect() {
    // Disconnect from Binance
}

Price BinanceExchange::get_price(const std::string& symbol) {
    // Get price from Binance
    return Price();
}

} 