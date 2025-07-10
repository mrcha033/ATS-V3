#pragma once

#include <string>
#include <vector>
#include <map>
#include <nlohmann/json.hpp>
#include "config_types.hpp"

namespace ats {

class ConfigManager {
public:
    bool load(const std::string& file_path);

    AppConfig& get_app_config();
    std::map<std::string, ExchangeConfig>& get_exchange_configs();
    TradingConfig& get_trading_config();
    ArbitrageConfig& get_arbitrage_config();
    RiskManagementConfig& get_risk_management_config();
    MonitoringConfig& get_monitoring_config();
    AlertsConfig& get_alerts_config();
    DatabaseConfig& get_database_config();
    LoggingConfig& get_logging_config();

private:
    nlohmann::json config_data_; // Keep for initial parsing
    AppConfig app_config_;
    std::map<std::string, ExchangeConfig> exchange_configs_;
    TradingConfig trading_config_;
    ArbitrageConfig arbitrage_config_;
    RiskManagementConfig risk_management_config_;
    MonitoringConfig monitoring_config_;
    AlertsConfig alerts_config_;
    DatabaseConfig database_config_;
    LoggingConfig logging_config_;
};

} 