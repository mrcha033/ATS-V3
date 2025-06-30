#include "exchange_factory.hpp"
#include "binance_exchange.hpp"
#include "upbit_exchange.hpp"

namespace ats {

std::vector<std::shared_ptr<ExchangeInterface>> ExchangeFactory::create_exchanges(
    const std::map<std::string, ExchangeConfig>& configs, AppState* app_state) {
    std::vector<std::shared_ptr<ExchangeInterface>> exchanges;
    for (const auto& pair : configs) {
        const ExchangeConfig& config = pair.second;
        if (!config.enabled) {
            continue;
        }
        if (config.name == "binance") {
            exchanges.push_back(std::make_shared<BinanceExchange>(config, app_state));
        } else if (config.name == "upbit") {
            exchanges.push_back(std::make_shared<UpbitExchange>(config, app_state));
        }
    }
    return exchanges;
}

}