#include "config_manager.hpp"
#include <fstream>
#include <nlohmann/json.hpp>
#include "logger.hpp"

namespace ats {

bool ConfigManager::load(const std::string& file_path) {
    std::ifstream file(file_path);
    if (!file.is_open()) {
        Logger::error("Failed to open config file: " + file_path);
        return false;
    }
    try {
        file >> config_data_;

        if (config_data_.contains("app")) {
            config_data_["app"].get_to(app_config_);
        }
        if (config_data_.contains("exchanges")) {
            for (auto& [name, config] : config_data_["exchanges"].items()) {
                ExchangeConfig exchange_cfg = config.get<ExchangeConfig>();
                exchange_cfg.name = name; // Manually set the name
                exchange_configs_[name] = exchange_cfg;
            }
        }
        if (config_data_.contains("trading")) {
            config_data_["trading"].get_to(trading_config_);
        }
        if (config_data_.contains("arbitrage")) {
            config_data_["arbitrage"].get_to(arbitrage_config_);
        }
        if (config_data_.contains("risk_management")) {
            config_data_["risk_management"].get_to(risk_management_config_);
        }
        if (config_data_.contains("monitoring")) {
            config_data_["monitoring"].get_to(monitoring_config_);
        }
        if (config_data_.contains("alerts")) {
            config_data_["alerts"].get_to(alerts_config_);
        }
        if (config_data_.contains("database")) {
            config_data_["database"].get_to(database_config_);
        }
        if (config_data_.contains("logging")) {
            config_data_["logging"].get_to(logging_config_);
        }

    } catch (const nlohmann::json::exception& e) {
        Logger::error("Error parsing config file: " + std::string(e.what()));
        return false;
    }
    return true;
}

AppConfig& ConfigManager::get_app_config() {
    return app_config_;
}

std::map<std::string, ExchangeConfig>& ConfigManager::get_exchange_configs() {
    return exchange_configs_;
}

TradingConfig& ConfigManager::get_trading_config() {
    return trading_config_;
}

ArbitrageConfig& ConfigManager::get_arbitrage_config() {
    return arbitrage_config_;
}

RiskManagementConfig& ConfigManager::get_risk_management_config() {
    return risk_management_config_;
}

MonitoringConfig& ConfigManager::get_monitoring_config() {
    return monitoring_config_;
}

AlertsConfig& ConfigManager::get_alerts_config() {
    return alerts_config_;
}

DatabaseConfig& ConfigManager::get_database_config() {
    return database_config_;
}

LoggingConfig& ConfigManager::get_logging_config() {
    return logging_config_;
}

} 
