#pragma once

#include <vector>
#include <string>
#include <memory>
#include "types.hpp"
#include "event_pusher.hpp"
#include "../utils/config_manager.hpp"

namespace ats {

class OpportunityDetector {
public:
    OpportunityDetector(ConfigManager* config_manager, const std::vector<std::string>& symbols);

    void start();
    void stop();

    void set_event_pusher(EventPusher* event_pusher);
    void update_prices(const PriceComparison& comparison);

private:
    ConfigManager* config_manager_;
    std::vector<std::string> symbols_;
    EventPusher* event_pusher_;
};

} 