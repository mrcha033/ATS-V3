#include "secure_config.hpp"
#include "json_parser.hpp"
#include "logger.hpp"
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <algorithm>
#include <cctype>

namespace ats {

bool SecureConfig::load_secure_config(const std::string& json_file_path) {
    Logger::info("Loading secure configuration...");
    
    // First try to load from environment variables
    bool env_loaded = false;
    const std::vector<std::string> exchanges = {"BINANCE", "UPBIT"};
    
    for (const auto& exchange : exchanges) {
        auto api_key = get_env_var(get_env_var_name(exchange, "API_KEY"));
        auto secret = get_env_var(get_env_var_name(exchange, "SECRET_KEY"));
        
        if (api_key && secret) {
            secure_data_[exchange + "_API_KEY"] = *api_key;
            secure_data_[exchange + "_SECRET_KEY"] = *secret;
            env_loaded = true;
            Logger::info("Loaded " + exchange + " credentials from environment variables");
        }
    }
    
    // Try to load notification tokens
    auto telegram_token = get_env_var("TELEGRAM_BOT_TOKEN");
    auto discord_webhook = get_env_var("DISCORD_WEBHOOK_URL");
    
    if (telegram_token) {
        secure_data_["TELEGRAM_BOT_TOKEN"] = *telegram_token;
    }
    if (discord_webhook) {
        secure_data_["DISCORD_WEBHOOK_URL"] = *discord_webhook;
    }
    
    // If no environment variables found, try JSON file
    if (!env_loaded) {
        Logger::warning("No environment variables found, falling back to JSON configuration");
        return load_from_json(json_file_path);
    }
    
    return true;
}

std::optional<std::string> SecureConfig::get_exchange_api_key(const std::string& exchange_name) const {
    std::string upper_exchange = exchange_name;
    std::transform(upper_exchange.begin(), upper_exchange.end(), upper_exchange.begin(), ::toupper);
    
    auto it = secure_data_.find(upper_exchange + "_API_KEY");
    if (it != secure_data_.end()) {
        return it->second;
    }
    return std::nullopt;
}

std::optional<std::string> SecureConfig::get_exchange_secret(const std::string& exchange_name) const {
    std::string upper_exchange = exchange_name;
    std::transform(upper_exchange.begin(), upper_exchange.end(), upper_exchange.begin(), ::toupper);
    
    auto it = secure_data_.find(upper_exchange + "_SECRET_KEY");
    if (it != secure_data_.end()) {
        return it->second;
    }
    return std::nullopt;
}

std::optional<std::string> SecureConfig::get_telegram_token() const {
    auto it = secure_data_.find("TELEGRAM_BOT_TOKEN");
    if (it != secure_data_.end()) {
        return it->second;
    }
    return std::nullopt;
}

std::optional<std::string> SecureConfig::get_discord_webhook() const {
    auto it = secure_data_.find("DISCORD_WEBHOOK_URL");
    if (it != secure_data_.end()) {
        return it->second;
    }
    return std::nullopt;
}

bool SecureConfig::validate_exchange_credentials(const std::string& exchange_name) const {
    auto api_key = get_exchange_api_key(exchange_name);
    auto secret = get_exchange_secret(exchange_name);
    
    if (!api_key || !secret) {
        Logger::error("Missing credentials for exchange: " + exchange_name);
        return false;
    }
    
    // Basic validation - check for obvious placeholders
    if (api_key->find("YOUR_") != std::string::npos || 
        secret->find("YOUR_") != std::string::npos) {
        Logger::error("Placeholder credentials detected for exchange: " + exchange_name);
        return false;
    }
    
    // Check minimum length requirements
    if (api_key->length() < 10 || secret->length() < 10) {
        Logger::error("Credentials too short for exchange: " + exchange_name);
        return false;
    }
    
    return true;
}

bool SecureConfig::validate_notification_config() const {
    // Check if at least one notification method is configured
    bool has_telegram = get_telegram_token().has_value();
    bool has_discord = get_discord_webhook().has_value();
    
    if (!has_telegram && !has_discord) {
        Logger::warning("No notification methods configured");
        return false;
    }
    
    return true;
}

std::string SecureConfig::get_env_var_name(const std::string& exchange, const std::string& key_type) const {
    std::string upper_exchange = exchange;
    std::transform(upper_exchange.begin(), upper_exchange.end(), upper_exchange.begin(), ::toupper);
    return upper_exchange + "_" + key_type;
}

std::optional<std::string> SecureConfig::get_env_var(const std::string& var_name) const {
    const char* value = std::getenv(var_name.c_str());
    if (value && strlen(value) > 0) {
        return std::string(value);
    }
    return std::nullopt;
}

bool SecureConfig::load_from_json(const std::string& json_file_path) {
    if (!std::filesystem::exists(json_file_path)) {
        Logger::error("Configuration file not found: " + json_file_path);
        return false;
    }
    
    try {
        std::ifstream file(json_file_path);
        if (!file.is_open()) {
            Logger::error("Cannot open configuration file: " + json_file_path);
            return false;
        }
        
        nlohmann::json config;
        file >> config;
        
        // Load exchange credentials
        if (config.contains("exchanges")) {
            for (const auto& [exchange_name, exchange_config] : config["exchanges"].items()) {
                std::string upper_exchange = exchange_name;
                std::transform(upper_exchange.begin(), upper_exchange.end(), upper_exchange.begin(), ::toupper);
                
                if (exchange_config.contains("api_key")) {
                    secure_data_[upper_exchange + "_API_KEY"] = exchange_config["api_key"];
                }
                if (exchange_config.contains("secret_key")) {
                    secure_data_[upper_exchange + "_SECRET_KEY"] = exchange_config["secret_key"];
                }
                if (exchange_config.contains("api_secret")) {
                    secure_data_[upper_exchange + "_SECRET_KEY"] = exchange_config["api_secret"];
                }
            }
        }
        
        // Load notification credentials
        if (config.contains("alerts")) {
            const auto& alerts = config["alerts"];
            if (alerts.contains("telegram") && alerts["telegram"].contains("bot_token")) {
                secure_data_["TELEGRAM_BOT_TOKEN"] = alerts["telegram"]["bot_token"];
            }
            if (alerts.contains("discord") && alerts["discord"].contains("webhook_url")) {
                secure_data_["DISCORD_WEBHOOK_URL"] = alerts["discord"]["webhook_url"];
            }
        }
        
        Logger::warning("Loaded credentials from JSON file - consider using environment variables for better security");
        return true;
        
    } catch (const std::exception& e) {
        Logger::error("Error loading configuration from JSON: " + std::string(e.what()));
        return false;
    }
}

void SecureConfig::clear_sensitive_data() {
    // Overwrite sensitive data with zeros before clearing
    for (auto& [key, value] : secure_data_) {
        std::fill(value.begin(), value.end(), '\0');
    }
    secure_data_.clear();
}

} // namespace ats