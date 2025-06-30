#include "event_loop.hpp"

namespace ats {

EventLoop::EventLoop(OpportunityDetector* opportunity_detector, ArbitrageEngine* arbitrage_engine)
    : opportunity_detector_(opportunity_detector),
      arbitrage_engine_(arbitrage_engine),
      running_(false) {}

void EventLoop::run() {
    running_ = true;
    while (running_) {
        Event event;
        event_queue_.wait_and_pop(event);
        process_event(event);
    }
}

void EventLoop::stop() {
    running_ = false;
}

void EventLoop::push_event(Event event) {
    event_queue_.push(std::move(event));
}

void EventLoop::process_event(const Event& event) {
    std::visit([this](auto&& arg) {
        using T = std::decay_t<decltype(arg)>;
        if constexpr (std::is_same_v<T, PriceUpdateEvent>) {
            opportunity_detector_->update_prices(arg.comparison);
        } else if constexpr (std::is_same_v<T, ArbitrageOpportunityEvent>) {
            arbitrage_engine_->evaluate_opportunity(arg.opportunity);
        }
    }, event);
}

}