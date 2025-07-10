#include "gtest/gtest.h"
#include "core/risk_manager.hpp"
#include "utils/config_manager.hpp"
#include "data/database_manager.hpp"
#include "utils/logger.hpp"

class RiskManagerTest : public ::testing::Test {
protected:
    void SetUp() override {
        ats::LoggingConfig logging_config;
        logging_config.console_output = true;
        ats::Logger::init(logging_config, ats::LogLevel::DEBUG);
        config_manager = std::make_unique<ats::ConfigManager>();
        config_manager->load("config/settings.json");
        db_manager = std::make_unique<ats::DatabaseManager>(":memory:");
        risk_manager = std::make_unique<ats::RiskManager>(config_manager.get(), db_manager.get());
        risk_manager->Initialize();
    }

    std::unique_ptr<ats::ConfigManager> config_manager;
    std::unique_ptr<ats::DatabaseManager> db_manager;
    std::unique_ptr<ats::RiskManager> risk_manager;
};

TEST_F(RiskManagerTest, AssessOpportunity_Approved) {
    ats::ArbitrageOpportunity opportunity;
    opportunity.is_executable = true;
    opportunity.net_profit_percent = 1.0;
    opportunity.max_volume = 1000;
    opportunity.buy_price = 100;

    ats::RiskAssessment assessment = risk_manager->AssessOpportunity(opportunity);
    ASSERT_TRUE(assessment.is_approved);
}

TEST_F(RiskManagerTest, AssessOpportunity_ExceedsMaxPositionSize) {
    ats::RiskLimits limits;
    limits.max_position_size_usd = 500;
    risk_manager->SetLimits(limits);

    ats::ArbitrageOpportunity opportunity;
    opportunity.is_executable = true;
    opportunity.net_profit_percent = 1.0;
    opportunity.max_volume = 1000;
    opportunity.buy_price = 100;

    ats::RiskAssessment assessment = risk_manager->AssessOpportunity(opportunity);
    ASSERT_FALSE(assessment.is_approved);
}

TEST_F(RiskManagerTest, AssessOpportunity_DailyLossLimitExceeded) {
    risk_manager->UpdatePnL(-1001);
    ats::RiskLimits limits;
    limits.max_daily_loss_usd = 1000;
    risk_manager->SetLimits(limits);

    ats::ArbitrageOpportunity opportunity;
    opportunity.is_executable = true;
    opportunity.net_profit_percent = 1.0;
    opportunity.max_volume = 1000;
    opportunity.buy_price = 100;

    ats::RiskAssessment assessment = risk_manager->AssessOpportunity(opportunity);
    ASSERT_FALSE(assessment.is_approved);
}

TEST_F(RiskManagerTest, AssessOpportunity_InsufficientLiquidity) {
    ats::RiskLimits limits;
    limits.min_liquidity_threshold = 10000;
    risk_manager->SetLimits(limits);

    ats::ArbitrageOpportunity opportunity;
    opportunity.is_executable = true;
    opportunity.net_profit_percent = 1.0;
    opportunity.max_volume = 1000;
    opportunity.buy_price = 100;
    opportunity.buy_liquidity = 5000;
    opportunity.sell_liquidity = 5000;

    ats::RiskAssessment assessment = risk_manager->AssessOpportunity(opportunity);
    ASSERT_FALSE(assessment.is_approved);
}

TEST_F(RiskManagerTest, AssessOpportunity_HighSpread) {
    ats::RiskLimits limits;
    limits.max_spread_threshold = 0.5;
    risk_manager->SetLimits(limits);

    ats::ArbitrageOpportunity opportunity;
    opportunity.is_executable = true;
    opportunity.net_profit_percent = 1.0;
    opportunity.max_volume = 1000;
    opportunity.buy_price = 100;
    opportunity.sell_price = 101;

    ats::RiskAssessment assessment = risk_manager->AssessOpportunity(opportunity);
    ASSERT_FALSE(assessment.is_approved);
}

TEST_F(RiskManagerTest, AssessOpportunity_KillSwitchActive) {
    risk_manager->ActivateKillSwitch("Test");

    ats::ArbitrageOpportunity opportunity;
    opportunity.is_executable = true;
    opportunity.net_profit_percent = 1.0;
    opportunity.max_volume = 1000;
    opportunity.buy_price = 100;

    ats::RiskAssessment assessment = risk_manager->AssessOpportunity(opportunity);
    ASSERT_FALSE(assessment.is_approved);
}

TEST_F(RiskManagerTest, AssessOpportunity_TradingHalted) {
    risk_manager->HaltTrading("Test");

    ats::ArbitrageOpportunity opportunity;
    opportunity.is_executable = true;
    opportunity.net_profit_percent = 1.0;
    opportunity.max_volume = 1000;
    opportunity.buy_price = 100;

    ats::RiskAssessment assessment = risk_manager->AssessOpportunity(opportunity);
    ASSERT_FALSE(assessment.is_approved);
}

