#pragma once

#include <unordered_map>
#include <string>
#include <mutex>
#include <vector>
#include <memory>
#include "types.hpp"
#include "../utils/config_manager.hpp"
#include "../exchange/exchange_interface.hpp"

namespace ats {

class PortfolioManager {
public:
    PortfolioManager(ConfigManager* config_manager);

    bool Initialize();
    bool IsHealthy() const;
    void AddExchange(std::shared_ptr<ExchangeInterface> exchange);

    void update_position(const std::string& symbol, double quantity, double price, OrderSide side);
    void update_balance(const std::string& asset, double amount);
    double get_balance(const std::string& asset) const;

private:
    ConfigManager* config_manager_;
    mutable std::mutex mutex_;
    std::unordered_map<std::string, double> balances_;
    std::vector<std::shared_ptr<ExchangeInterface>> exchanges_;
};

}
 