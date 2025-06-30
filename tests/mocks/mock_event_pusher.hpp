#pragma once

#include "core/event_pusher.hpp"
#include <gmock/gmock.h>

namespace ats {
namespace mocks {

class MockEventPusher : public EventPusher {
public:
    MOCK_METHOD(void, push_event, (Event event), (override));
};

}
}