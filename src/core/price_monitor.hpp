#pragma once

#include <vector>
#include <memory>
#include <functional>
#include "types.hpp"
#include "../exchange/exchange_interface.hpp"

namespace ats {

class PriceMonitor {
public:
    using UpdateCallback = std::function<void(const PriceComparison&)>;

    PriceMonitor(const std::vector<std::shared_ptr<ExchangeInterface>>& exchanges);

    void start();
    void stop();

    void set_update_callback(UpdateCallback callback);
    void check_prices();

private:
    std::vector<std::shared_ptr<ExchangeInterface>> exchanges_;
    UpdateCallback update_callback_;
};

} 