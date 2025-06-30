#pragma once

#include "core/event_loop.hpp"
#include <gmock/gmock.h>

namespace ats {
namespace mocks {

class MockEventLoop : public EventLoop {
public:
    MOCK_METHOD(void, push_event, (Event event), (override));
};

}
}