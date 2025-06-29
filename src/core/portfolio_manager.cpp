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

void PortfolioManager::update_position(const std::string& symbol, double quantity, double price, OrderSide side) {
    std::lock_guard<std::mutex> lock(mutex_);
    if (side == OrderSide::BUY) {
        balances_["USD"] -= quantity * price;
        balances_[symbol] += quantity;
    } else {
        balances_["USD"] += quantity * price;
        balances_[symbol] -= quantity;
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
