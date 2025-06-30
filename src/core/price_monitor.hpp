#pragma once

#include <vector>
#include <memory>
#include <thread>
#include <atomic>
#include "types.hpp"
#include "event_pusher.hpp"
#include "../exchange/exchange_interface.hpp"
#include "../utils/config_manager.hpp"

namespace ats {

class PriceMonitor {
public:
    PriceMonitor(ConfigManager* config_manager, const std::vector<std::shared_ptr<ExchangeInterface>>& exchanges);
    ~PriceMonitor();

    void start();
    void stop();

    void set_event_pusher(EventPusher* event_pusher);

private:
    void run();
    void check_prices();

    ConfigManager* config_manager_;
    std::vector<std::shared_ptr<ExchangeInterface>> exchanges_;
    std::vector<std::string> symbols_;
    EventPusher* event_pusher_;
    std::thread thread_;
    std::atomic<bool> running_;
};

} 