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

    const AppConfig& get_app_config() const;
    const std::map<std::string, ExchangeConfig>& get_exchange_configs() const;
    const TradingConfig& get_trading_config() const;
    const ArbitrageConfig& get_arbitrage_config() const;
    const RiskManagementConfig& get_risk_management_config() const;
    const MonitoringConfig& get_monitoring_config() const;
    const AlertsConfig& get_alerts_config() const;
    const DatabaseConfig& get_database_config() const;
    const LoggingConfig& get_logging_config() const;

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