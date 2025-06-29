
#include <gtest/gtest.h>
#include "core/portfolio_manager.hpp"
#include "utils/config_manager.hpp"

class MockConfigManager : public ats::ConfigManager {
public:
    MockConfigManager() {
        // Set up mock configuration data
    }
};

TEST(PortfolioManagerTest, InitialState) {
    MockConfigManager config_manager;
    ats::PortfolioManager pm(&config_manager);
    ASSERT_EQ(pm.get_balance("USDT"), 0.0);
}
