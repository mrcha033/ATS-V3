#pragma once

#include <string>
#include <vector>
#include <memory>
#include "../core/types.hpp"
#include "../utils/config_types.hpp"

namespace ats {

class AppState;

class ExchangeInterface {
public:
    ExchangeInterface(const ExchangeConfig& config, AppState* app_state);
    virtual ~ExchangeInterface() = default;

    virtual std::string get_name() const = 0;
    virtual void connect() = 0;
    virtual void disconnect() = 0;
    virtual Price get_price(const std::string& symbol) = 0;
    virtual OrderResult place_order(const Order& order) = 0;
};

}
 