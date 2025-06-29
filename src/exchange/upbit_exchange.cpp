#include "upbit_exchange.hpp"
#include "../core/app_state.hpp"

namespace ats {

UpbitExchange::UpbitExchange(const nlohmann::json& config, AppState* app_state)
    : config_(config), app_state_(app_state) {}

std::string UpbitExchange::get_name() const {
    return "upbit";
}

void UpbitExchange::connect() {
    // Connect to Upbit
}

void UpbitExchange::disconnect() {
    // Disconnect from Upbit
}

Price UpbitExchange::get_price(const std::string& symbol) {
    // Get price from Upbit
    return Price();
}

}