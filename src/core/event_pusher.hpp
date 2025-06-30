#pragma once

#include "event.hpp"

namespace ats {

class EventPusher {
public:
    virtual ~EventPusher() = default;
    virtual void push_event(Event event) = 0;
};

}