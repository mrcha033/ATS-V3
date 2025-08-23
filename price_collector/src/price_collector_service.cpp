#include "price_collector_service.hpp"
#include <iostream>

namespace ats {
namespace price_collector {

PriceCollectorService::PriceCollectorService() {}

PriceCollectorService::~PriceCollectorService() {}

bool PriceCollectorService::initialize(const config::ConfigManager& config) {
    return true;
}

bool PriceCollectorService::start() {
    running_ = true;
    return true;
}

void PriceCollectorService::stop() {
    running_ = false;
}

bool PriceCollectorService::is_running() const {
    return running_;
}

} // namespace price_collector
} // namespace ats
