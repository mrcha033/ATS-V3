#pragma once

#include <vector>
#include <string>
#include <functional>
#include <memory>
#include "types.hpp"
#include "price_monitor.hpp"

namespace ats {

class OpportunityDetector {
public:
    using OpportunityCallback = std::function<void(const ArbitrageOpportunity&)>;

    OpportunityDetector(const std::vector<std::string>& symbols);

    void start();
    void stop();

    void set_opportunity_callback(OpportunityCallback callback);
    void update_prices(const PriceComparison& comparison);

private:
    std::vector<std::string> symbols_;
    OpportunityCallback opportunity_callback_;
    std::unique_ptr<PriceMonitor> price_monitor_;
};

} 