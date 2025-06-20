#include "config_manager.hpp"
#include "json_parser.hpp"
#include "logger.hpp"
#include <fstream>
#include <sstream>
#include <algorithm>
#include <cctype>

namespace ats {

bool ConfigManager::LoadConfig(const std::string& file_path) {
    config_file_path_ = file_path;
    
    std::ifstream file(file_path);
    if (!file.is_open()) {
        LOG_ERROR("Failed to open config file: {}", file_path);
        return false;
    }
    
    std::stringstream buffer;
    buffer << file.rdbuf();
    std::string json_content = buffer.str();
    file.close();
    
    return ParseJson(json_content);
}

bool ConfigManager::ReloadConfig() {
    if (config_file_path_.empty()) {
        LOG_ERROR("No config file path set for reload");
        return false;
    }
    
    config_data_.clear();
    return LoadConfig(config_file_path_);
}

bool ConfigManager::SaveConfig(const std::string& file_path) {
    std::string target_path = file_path.empty() ? config_file_path_ : file_path;
    
    std::ofstream file(target_path);
    if (!file.is_open()) {
        LOG_ERROR("Failed to open config file for writing: {}", target_path);
        return false;
    }
    
    file << ToJson();
    file.close();
    
    LOG_INFO("Configuration saved to: {}", target_path);
    return true;
}

std::string ConfigManager::GetString(const std::string& key, const std::string& default_value) const {
    return GetValue<std::string>(key, default_value);
}

int ConfigManager::GetInt(const std::string& key, int default_value) const {
    return GetValue<int>(key, default_value);
}

double ConfigManager::GetDouble(const std::string& key, double default_value) const {
    return GetValue<double>(key, default_value);
}

bool ConfigManager::GetBool(const std::string& key, bool default_value) const {
    return GetValue<bool>(key, default_value);
}

void ConfigManager::SetString(const std::string& key, const std::string& value) {
    config_data_[key] = value;
}

void ConfigManager::SetInt(const std::string& key, int value) {
    config_data_[key] = value;
}

void ConfigManager::SetDouble(const std::string& key, double value) {
    config_data_[key] = value;
}

void ConfigManager::SetBool(const std::string& key, bool value) {
    config_data_[key] = value;
}

bool ConfigManager::HasKey(const std::string& key) const {
    return config_data_.find(key) != config_data_.end();
}

bool ConfigManager::ValidateRequiredKeys(const std::vector<std::string>& required_keys) const {
    for (const auto& key : required_keys) {
        if (!HasKey(key)) {
            LOG_ERROR("Required configuration key missing: {}", key);
            return false;
        }
    }
    return true;
}

std::vector<ConfigManager::ExchangeConfig> ConfigManager::GetExchangeConfigs() const {
    std::vector<ExchangeConfig> configs;
    
    // For now, return hardcoded configs - will be improved with proper JSON parsing
    ExchangeConfig binance;
    binance.name = GetString("binance.name", "binance");
    binance.api_key = GetString("binance.api_key", "");
    binance.secret_key = GetString("binance.secret_key", "");
    binance.base_url = GetString("binance.base_url", "https://api.binance.com");
    binance.ws_url = GetString("binance.ws_url", "wss://stream.binance.com:9443/ws");
    binance.enabled = GetBool("binance.enabled", true);
    binance.rate_limit_per_second = GetInt("binance.rate_limit", 10);
    binance.maker_fee = GetDouble("binance.maker_fee", 0.001);
    binance.taker_fee = GetDouble("binance.taker_fee", 0.001);
    configs.push_back(binance);
    
    ExchangeConfig upbit;
    upbit.name = GetString("upbit.name", "upbit");
    upbit.api_key = GetString("upbit.api_key", "");
    upbit.secret_key = GetString("upbit.secret_key", "");
    upbit.base_url = GetString("upbit.base_url", "https://api.upbit.com");
    upbit.ws_url = GetString("upbit.ws_url", "wss://api.upbit.com/websocket/v1");
    upbit.enabled = GetBool("upbit.enabled", true);
    upbit.rate_limit_per_second = GetInt("upbit.rate_limit", 10);
    upbit.maker_fee = GetDouble("upbit.maker_fee", 0.0025);
    upbit.taker_fee = GetDouble("upbit.taker_fee", 0.0025);
    configs.push_back(upbit);
    
    return configs;
}

std::vector<std::string> ConfigManager::GetTradingPairs() const {
    // Default trading pairs
    return {
        "BTC/USDT",
        "ETH/USDT", 
        "BNB/USDT",
        "ADA/USDT",
        "SOL/USDT"
    };
}

double ConfigManager::GetMinProfitThreshold() const {
    return GetDouble("arbitrage.min_profit_threshold", 0.001); // 0.1%
}

double ConfigManager::GetMaxPositionSize() const {
    return GetDouble("arbitrage.max_position_size", 1000.0); // $1000
}

double ConfigManager::GetMaxRiskPerTrade() const {
    return GetDouble("arbitrage.max_risk_per_trade", 0.02); // 2%
}

bool ConfigManager::ParseJson(const std::string& json_content) {
    try {
        // Set defaults first
        SetString("app.name", "ATS V3");
        SetString("app.version", "1.0.0");
        SetBool("app.debug", false);
        
        if (json_content.empty()) {
            LOG_WARNING("Empty JSON content, using default configuration");
            return true;
        }
        
        // Use proper JSON parser
        JsonValue root = JsonParser::ParseString(json_content);
        
        if (!json::IsObject(root)) {
            LOG_ERROR("JSON root is not an object");
            return false;
        }
        
        auto rootObj = json::AsObject(root);
        ParseJsonObject(rootObj, "");
        
        LOG_INFO("Configuration parsed successfully from JSON");
        return true;
        
    } catch (const JsonParseError& e) {
        LOG_ERROR("JSON parse error: {}", e.what());
        LOG_WARNING("Using default configuration values");
        return false;
    } catch (const std::exception& e) {
        LOG_ERROR("Failed to parse JSON config: {}", e.what());
        LOG_WARNING("Using default configuration values");
        return false;
    }
}

std::string ConfigManager::ToJson() const {
    // Simple JSON output - for production, use proper JSON library
    std::ostringstream oss;
    oss << "{\n";
    oss << "  \"app\": {\n";
    oss << "    \"name\": \"" << GetString("app.name", "ATS V3") << "\",\n";
    oss << "    \"version\": \"" << GetString("app.version", "1.0.0") << "\",\n";
    oss << "    \"debug\": " << (GetBool("app.debug", false) ? "true" : "false") << "\n";
    oss << "  },\n";
    oss << "  \"arbitrage\": {\n";
    oss << "    \"min_profit_threshold\": " << GetDouble("arbitrage.min_profit_threshold", 0.001) << ",\n";
    oss << "    \"max_position_size\": " << GetDouble("arbitrage.max_position_size", 1000.0) << ",\n";
    oss << "    \"max_risk_per_trade\": " << GetDouble("arbitrage.max_risk_per_trade", 0.02) << "\n";
    oss << "  }\n";
    oss << "}\n";
    return oss.str();
}

template<typename T>
T ConfigManager::GetValue(const std::string& key, const T& default_value) const {
    auto it = config_data_.find(key);
    if (it != config_data_.end()) {
        try {
            return std::get<T>(it->second);
        } catch (const std::bad_variant_access&) {
            LOG_WARNING("Type mismatch for config key: {}, using default", key);
        }
    }
    return default_value;
}

void ConfigManager::ParseJsonObject(const std::unordered_map<std::string, JsonValue>& obj, const std::string& prefix) {
    for (const auto& [key, value] : obj) {
        std::string full_key = prefix.empty() ? key : prefix + "." + key;
        ParseJsonValue(full_key, value);
    }
}

void ConfigManager::ParseJsonValue(const std::string& key, const JsonValue& value) {
    if (json::IsBool(value)) {
        SetBool(key, json::AsBool(value));
    }
    else if (json::IsInt(value)) {
        SetInt(key, json::AsInt(value));
    }
    else if (json::IsDouble(value)) {
        SetDouble(key, json::AsDouble(value));
    }
    else if (json::IsString(value)) {
        SetString(key, json::AsString(value));
    }
    else if (json::IsObject(value)) {
        auto obj = json::AsObject(value);
        ParseJsonObject(obj, key);
    }
    else if (json::IsArray(value)) {
        // Arrays are more complex - for now, skip them
        // In a production system, we'd need to handle array parsing
        LOG_WARNING("Array values not yet supported for config key: {}", key);
    }
    else if (json::IsNull(value)) {
        // Skip null values
        LOG_DEBUG("Null value for config key: {}", key);
    }
}

} // namespace ats 