#pragma once

#include <string>
#include <vector>
#include <functional>
#include "types.hpp"
#include "portfolio_manager.hpp"
#include "risk_manager.hpp"
#include "../exchange/exchange_interface.hpp"
#include "../utils/config_manager.hpp"

namespace ats {

class TradeExecutor {
public:
    using ExecutionCallback = std::function<void(const Trade&)>;

    TradeExecutor(ConfigManager* config_manager, PortfolioManager* portfolio_manager, RiskManager* risk_manager);

    bool Initialize();
    void Start();
    void Stop();
    bool IsHealthy() const;

    void AddExchange(std::shared_ptr<ExchangeInterface> exchange);
    void SetExecutionCallback(ExecutionCallback callback);

    virtual void execute_trade(const ArbitrageOpportunity& opportunity);

private:
    ConfigManager* config_manager_;
    PortfolioManager* portfolio_manager_;
    RiskManager* risk_manager_;
    std::vector<std::shared_ptr<ExchangeInterface>> exchanges_;
    ExecutionCallback execution_callback_;
};

}
 