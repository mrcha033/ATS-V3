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

const AppConfig& ConfigManager::get_app_config() const {
    return app_config_;
}

const std::map<std::string, ExchangeConfig>& ConfigManager::get_exchange_configs() const {
    return exchange_configs_;
}

const TradingConfig& ConfigManager::get_trading_config() const {
    return trading_config_;
}

const ArbitrageConfig& ConfigManager::get_arbitrage_config() const {
    return arbitrage_config_;
}

const RiskManagementConfig& ConfigManager::get_risk_management_config() const {
    return risk_management_config_;
}

const MonitoringConfig& ConfigManager::get_monitoring_config() const {
    return monitoring_config_;
}

const AlertsConfig& ConfigManager::get_alerts_config() const {
    return alerts_config_;
}

const DatabaseConfig& ConfigManager::get_database_config() const {
    return database_config_;
}

const LoggingConfig& ConfigManager::get_logging_config() const {
    return logging_config_;
}

} 
