#include "config_validator.hpp"
#include <filesystem>
#include <regex>
#include <algorithm>
#include <numeric>
#include <limits>

namespace ats {

ConfigValidator::ValidationErrors ConfigValidator::errors_;

ConfigValidator::ValidationResult ConfigValidator::validate_config(const nlohmann::json& config) {
    clear_errors();
    
    // Validate required top-level sections
    if (!config.contains("app")) {
        add_error("app", "Missing required app configuration section");
        return Result<bool>::error("Missing app configuration");
    }
    
    if (!config.contains("exchanges")) {
        add_error("exchanges", "Missing required exchanges configuration section");
        return Result<bool>::error("Missing exchanges configuration");
    }
    
    if (!config.contains("trading")) {
        add_error("trading", "Missing required trading configuration section");
        return Result<bool>::error("Missing trading configuration");
    }
    
    // Validate each section
    auto app_result = validate_app_config(config["app"]);
    if (app_result.is_error()) {
        return app_result;
    }
    
    if (config.contains("exchanges")) {
        for (const auto& [exchange_name, exchange_config] : config["exchanges"].items()) {
            auto exchange_result = validate_exchange_config(exchange_config);
            if (exchange_result.is_error()) {
                return exchange_result;
            }
        }
    }
    
    if (config.contains("trading")) {
        auto trading_result = validate_trading_config(config["trading"]);
        if (trading_result.is_error()) {
            return trading_result;
        }
    }
    
    if (config.contains("arbitrage")) {
        auto arbitrage_result = validate_arbitrage_config(config["arbitrage"]);
        if (arbitrage_result.is_error()) {
            return arbitrage_result;
        }
    }
    
    if (config.contains("risk_management")) {
        auto risk_result = validate_risk_config(config["risk_management"]);
        if (risk_result.is_error()) {
            return risk_result;
        }
    }
    
    if (config.contains("monitoring")) {
        auto monitoring_result = validate_monitoring_config(config["monitoring"]);
        if (monitoring_result.is_error()) {
            return monitoring_result;
        }
    }
    
    if (config.contains("database")) {
        auto database_result = validate_database_config(config["database"]);
        if (database_result.is_error()) {
            return database_result;
        }
    }
    
    if (config.contains("logging")) {
        auto logging_result = validate_logging_config(config["logging"]);
        if (logging_result.is_error()) {
            return logging_result;
        }
    }
    
    if (config.contains("alerts")) {
        auto alerts_result = validate_alerts_config(config["alerts"]);
        if (alerts_result.is_error()) {
            return alerts_result;
        }
    }
    
    return Result<bool>::success(true);
}

ConfigValidator::ValidationResult ConfigValidator::validate_app_config(const nlohmann::json& app_config) {
    validate_required_field(app_config, "name");
    validate_string_field(app_config, "name", 1, 100);
    
    validate_required_field(app_config, "version");
    validate_string_field(app_config, "version", 1, 20);
    
    validate_boolean_field(app_config, "debug");
    
    if (app_config.contains("log_level")) {
        validate_enum_field(app_config, "log_level", {"DEBUG", "INFO", "WARNING", "ERROR", "CRITICAL"});
    }
    
    return errors_.empty() ? Result<bool>::success(true) : 
                           Result<bool>::error("App configuration validation failed");
}

ConfigValidator::ValidationResult ConfigValidator::validate_exchange_config(const nlohmann::json& exchange_config) {
    validate_required_field(exchange_config, "name");
    validate_string_field(exchange_config, "name", 1, 50);
    
    validate_required_field(exchange_config, "enabled");
    validate_boolean_field(exchange_config, "enabled");
    
    validate_required_field(exchange_config, "base_url");
    validate_url_field(exchange_config, "base_url");
    
    if (exchange_config.contains("ws_url")) {
        validate_url_field(exchange_config, "ws_url");
    }
    
    validate_positive_field(exchange_config, "rate_limit_per_second");
    validate_percentage_field(exchange_config, "maker_fee");
    validate_percentage_field(exchange_config, "taker_fee");
    
    if (exchange_config.contains("testnet")) {
        validate_boolean_field(exchange_config, "testnet");
    }
    
    return errors_.empty() ? Result<bool>::success(true) : 
                           Result<bool>::error("Exchange configuration validation failed");
}

ConfigValidator::ValidationResult ConfigValidator::validate_trading_config(const nlohmann::json& trading_config) {
    validate_required_field(trading_config, "pairs");
    validate_array_field(trading_config, "pairs", 1, 100);
    
    if (trading_config.contains("pairs") && trading_config["pairs"].is_array()) {
        for (const auto& pair : trading_config["pairs"]) {
            if (pair.is_string()) {
                if (!validate_trading_pair(pair.get<std::string>())) {
                    add_error("pairs", "Invalid trading pair format", pair.get<std::string>());
                }
            }
        }
    }
    
    validate_required_field(trading_config, "base_currency");
    validate_string_field(trading_config, "base_currency", 1, 10);
    
    return errors_.empty() ? Result<bool>::success(true) : 
                           Result<bool>::error("Trading configuration validation failed");
}

ConfigValidator::ValidationResult ConfigValidator::validate_arbitrage_config(const nlohmann::json& arbitrage_config) {
    validate_positive_field(arbitrage_config, "min_profit_threshold");
    validate_positive_field(arbitrage_config, "max_position_size");
    validate_percentage_field(arbitrage_config, "max_risk_per_trade");
    validate_positive_field(arbitrage_config, "min_volume_usd");
    validate_positive_field(arbitrage_config, "execution_timeout_ms");
    validate_positive_field(arbitrage_config, "price_update_interval_ms");
    validate_positive_field(arbitrage_config, "opportunity_check_interval_ms");
    
    return errors_.empty() ? Result<bool>::success(true) : 
                           Result<bool>::error("Arbitrage configuration validation failed");
}

ConfigValidator::ValidationResult ConfigValidator::validate_risk_config(const nlohmann::json& risk_config) {
    validate_positive_field(risk_config, "max_daily_loss");
    validate_positive_field(risk_config, "max_open_positions");
    validate_percentage_field(risk_config, "position_size_percent");
    validate_percentage_field(risk_config, "stop_loss_percent");
    validate_percentage_field(risk_config, "max_slippage_percent");
    
    return errors_.empty() ? Result<bool>::success(true) : 
                           Result<bool>::error("Risk management configuration validation failed");
}

ConfigValidator::ValidationResult ConfigValidator::validate_monitoring_config(const nlohmann::json& monitoring_config) {
    validate_positive_field(monitoring_config, "system_check_interval_sec");
    validate_positive_field(monitoring_config, "performance_log_interval_sec");
    validate_boolean_field(monitoring_config, "alert_on_high_cpu");
    validate_boolean_field(monitoring_config, "alert_on_high_memory");
    validate_boolean_field(monitoring_config, "alert_on_network_issues");
    validate_percentage_field(monitoring_config, "cpu_threshold_percent");
    validate_percentage_field(monitoring_config, "memory_threshold_percent");
    validate_positive_field(monitoring_config, "temperature_threshold_celsius");
    
    return errors_.empty() ? Result<bool>::success(true) : 
                           Result<bool>::error("Monitoring configuration validation failed");
}

ConfigValidator::ValidationResult ConfigValidator::validate_database_config(const nlohmann::json& database_config) {
    validate_required_field(database_config, "type");
    validate_enum_field(database_config, "type", {"sqlite", "postgresql", "mysql"});
    
    validate_required_field(database_config, "path");
    validate_string_field(database_config, "path", 1, 500);
    
    validate_positive_field(database_config, "backup_interval_hours");
    validate_positive_field(database_config, "max_backup_files");
    
    return errors_.empty() ? Result<bool>::success(true) : 
                           Result<bool>::error("Database configuration validation failed");
}

ConfigValidator::ValidationResult ConfigValidator::validate_logging_config(const nlohmann::json& logging_config) {
    validate_required_field(logging_config, "file_path");
    validate_string_field(logging_config, "file_path", 1, 500);
    
    validate_positive_field(logging_config, "max_file_size_mb");
    validate_positive_field(logging_config, "max_backup_files");
    validate_boolean_field(logging_config, "console_output");
    validate_boolean_field(logging_config, "file_output");
    
    return errors_.empty() ? Result<bool>::success(true) : 
                           Result<bool>::error("Logging configuration validation failed");
}

ConfigValidator::ValidationResult ConfigValidator::validate_alerts_config(const nlohmann::json& alerts_config) {
    validate_boolean_field(alerts_config, "enabled");
    
    if (alerts_config.contains("telegram")) {
        const auto& telegram = alerts_config["telegram"];
        validate_boolean_field(telegram, "enabled");
        if (telegram.contains("bot_token")) {
            validate_string_field(telegram, "bot_token", 10, 200);
        }
        if (telegram.contains("chat_id")) {
            validate_string_field(telegram, "chat_id", 1, 50);
        }
    }
    
    if (alerts_config.contains("discord")) {
        const auto& discord = alerts_config["discord"];
        validate_boolean_field(discord, "enabled");
        if (discord.contains("webhook_url")) {
            validate_url_field(discord, "webhook_url");
        }
    }
    
    return errors_.empty() ? Result<bool>::success(true) : 
                           Result<bool>::error("Alerts configuration validation failed");
}

bool ConfigValidator::validate_required_field(const nlohmann::json& config, const std::string& field) {
    if (!config.contains(field)) {
        add_error(field, "Required field is missing");
        return false;
    }
    return true;
}

bool ConfigValidator::validate_string_field(const nlohmann::json& config, const std::string& field, 
                                           size_t min_length, size_t max_length) {
    if (!config.contains(field)) return true;
    
    if (!config[field].is_string()) {
        add_error(field, "Field must be a string", config[field].dump());
        return false;
    }
    
    std::string value = config[field].get<std::string>();
    if (value.length() < min_length) {
        add_error(field, "String too short (min: " + std::to_string(min_length) + ")", value);
        return false;
    }
    
    if (value.length() > max_length) {
        add_error(field, "String too long (max: " + std::to_string(max_length) + ")", value);
        return false;
    }
    
    return true;
}

bool ConfigValidator::validate_numeric_field(const nlohmann::json& config, const std::string& field,
                                            double min_value, double max_value) {
    if (!config.contains(field)) return true;
    
    if (!config[field].is_number()) {
        add_error(field, "Field must be a number", config[field].dump());
        return false;
    }
    
    double value = config[field].get<double>();
    if (value < min_value) {
        add_error(field, "Value too small (min: " + std::to_string(min_value) + ")", 
                 std::to_string(value));
        return false;
    }
    
    if (value > max_value) {
        add_error(field, "Value too large (max: " + std::to_string(max_value) + ")", 
                 std::to_string(value));
        return false;
    }
    
    return true;
}

bool ConfigValidator::validate_boolean_field(const nlohmann::json& config, const std::string& field) {
    if (!config.contains(field)) return true;
    
    if (!config[field].is_boolean()) {
        add_error(field, "Field must be a boolean", config[field].dump());
        return false;
    }
    
    return true;
}

bool ConfigValidator::validate_array_field(const nlohmann::json& config, const std::string& field,
                                          size_t min_size, size_t max_size) {
    if (!config.contains(field)) return true;
    
    if (!config[field].is_array()) {
        add_error(field, "Field must be an array", config[field].dump());
        return false;
    }
    
    size_t size = config[field].size();
    if (size < min_size) {
        add_error(field, "Array too small (min: " + std::to_string(min_size) + ")", 
                 std::to_string(size));
        return false;
    }
    
    if (size > max_size) {
        add_error(field, "Array too large (max: " + std::to_string(max_size) + ")", 
                 std::to_string(size));
        return false;
    }
    
    return true;
}

bool ConfigValidator::validate_url_field(const nlohmann::json& config, const std::string& field) {
    if (!config.contains(field)) return true;
    
    if (!config[field].is_string()) {
        add_error(field, "URL must be a string", config[field].dump());
        return false;
    }
    
    std::string url = config[field].get<std::string>();
    std::regex url_pattern(R"(^https?://[^\s/$.?#].[^\s]*$)");
    
    if (!std::regex_match(url, url_pattern)) {
        add_error(field, "Invalid URL format", url);
        return false;
    }
    
    return true;
}

bool ConfigValidator::validate_enum_field(const nlohmann::json& config, const std::string& field,
                                         const std::vector<std::string>& valid_values) {
    if (!config.contains(field)) return true;
    
    if (!config[field].is_string()) {
        add_error(field, "Field must be a string", config[field].dump());
        return false;
    }
    
    std::string value = config[field].get<std::string>();
    if (std::find(valid_values.begin(), valid_values.end(), value) == valid_values.end()) {
        add_error(field, "Invalid value. Must be one of: " + 
                 std::accumulate(valid_values.begin(), valid_values.end(), std::string(),
                                [](const std::string& a, const std::string& b) {
                                    return a.empty() ? b : a + ", " + b;
                                }), value);
        return false;
    }
    
    return true;
}

bool ConfigValidator::validate_percentage_field(const nlohmann::json& config, const std::string& field) {
    return validate_numeric_field(config, field, 0.0, 1.0);
}

bool ConfigValidator::validate_positive_field(const nlohmann::json& config, const std::string& field) {
    return validate_numeric_field(config, field, 0.0, std::numeric_limits<double>::infinity());
}

bool ConfigValidator::validate_trading_pair(const std::string& pair) {
    std::regex pair_pattern(R"(^[A-Z]{2,10}/[A-Z]{2,10}$)");
    return std::regex_match(pair, pair_pattern);
}

bool ConfigValidator::validate_file_path(const std::string& path, bool must_exist) {
    if (must_exist && !std::filesystem::exists(path)) {
        return false;
    }
    
    // Check if parent directory exists or can be created
    std::filesystem::path file_path(path);
    if (file_path.has_parent_path()) {
        std::filesystem::path parent = file_path.parent_path();
        if (!std::filesystem::exists(parent)) {
            std::error_code ec;
            std::filesystem::create_directories(parent, ec);
            if (ec) {
                return false;
            }
        }
    }
    
    return true;
}

void ConfigValidator::add_error(const std::string& field, const std::string& message, 
                               const std::string& value) {
    errors_.push_back({field, message, value});
}

} // namespace ats