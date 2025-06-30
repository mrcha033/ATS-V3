#pragma once

#include "../utils/thread_safe_queue.hpp"
#include "event.hpp"
#include "event_pusher.hpp"
#include "opportunity_detector.hpp"
#include "arbitrage_engine.hpp"

namespace ats {

class EventLoop : public EventPusher {
public:
    EventLoop(OpportunityDetector* opportunity_detector, ArbitrageEngine* arbitrage_engine);

    void run();
    void stop();

    void push_event(Event event) override;

private:
    void process_event(const Event& event);

    ThreadSafeQueue<Event> event_queue_;
    OpportunityDetector* opportunity_detector_;
    ArbitrageEngine* arbitrage_engine_;
    std::atomic<bool> running_;
};

}