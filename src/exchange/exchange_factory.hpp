#pragma once

#include <memory>
#include <vector>
#include <string>
#include <nlohmann/json.hpp>
#include "exchange_interface.hpp"
#include "../core/app_state.hpp"

namespace ats {

class ExchangeFactory {
public:
    static std::vector<std::shared_ptr<ExchangeInterface>> create_exchanges(
        const nlohmann::json& configs, AppState* app_state);
};

}
