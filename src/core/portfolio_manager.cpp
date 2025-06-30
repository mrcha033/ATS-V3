#include "portfolio_manager.hpp"

namespace ats {

PortfolioManager::PortfolioManager(ConfigManager* config_manager)
    : config_manager_(config_manager) {}

bool PortfolioManager::Initialize() {
    return true;
}

bool PortfolioManager::IsHealthy() const {
    return true;
}

void PortfolioManager::AddExchange(std::shared_ptr<ExchangeInterface> exchange) {
    exchanges_.push_back(exchange);
}

void PortfolioManager::update_position(const OrderResult& order_result) {
    std::lock_guard<std::mutex> lock(mutex_);
    if (order_result.side == OrderSide::BUY) {
        balances_[order_result.symbol] += order_result.executed_quantity;
        // Assuming quote currency is USDT for simplicity, adjust as needed
        balances_["USDT"] -= order_result.cummulative_quote_quantity;
    } else {
        balances_[order_result.symbol] -= order_result.executed_quantity;
        balances_["USDT"] += order_result.cummulative_quote_quantity;
    }
    // Deduct commission
    if (!order_result.commission_asset.empty() && order_result.commission > 0) {
        balances_[order_result.commission_asset] -= order_result.commission;
    }
}

void PortfolioManager::update_balance(const std::string& asset, double amount) {
    std::lock_guard<std::mutex> lock(mutex_);
    balances_[asset] += amount;
}

double PortfolioManager::get_balance(const std::string& asset) const {
    std::lock_guard<std::mutex> lock(mutex_);
    auto it = balances_.find(asset);
    if (it != balances_.end()) {
        return it->second;
    }
    return 0.0;
}

} 
