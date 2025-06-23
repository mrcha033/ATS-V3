#include "config_manager.hpp"
#include "json_parser.hpp"
#include "logger.hpp"
#include <fstream>
#include <sstream>
#include <algorithm>
#include <cctype>
#include <shared_mutex>

namespace ats {

bool ConfigManager::LoadConfig(const std::string& file_path) {
    std::unique_lock<std::shared_mutex> lock(config_mutex_);
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
    std::unique_lock<std::shared_mutex> lock(config_mutex_);
    if (config_file_path_.empty()) {
        LOG_ERROR("No config file path set for reload");
        return false;
    }
    
    config_data_.clear();
    lock.unlock(); // Release lock before calling LoadConfig
    return LoadConfig(config_file_path_);
}

bool ConfigManager::SaveConfig(const std::string& file_path) {
    std::shared_lock<std::shared_mutex> lock(config_mutex_);
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

std::vector<std::string> ConfigManager::GetStringArray(const std::string& key, const std::vector<std::string>& default_value) const {
    return GetValue<std::vector<std::string>>(key, default_value);
}

void ConfigManager::SetString(const std::string& key, const std::string& value) {
    std::unique_lock<std::shared_mutex> lock(config_mutex_);
    config_data_[key] = value;
}

void ConfigManager::SetInt(const std::string& key, int value) {
    std::unique_lock<std::shared_mutex> lock(config_mutex_);
    config_data_[key] = value;
}

void ConfigManager::SetDouble(const std::string& key, double value) {
    std::unique_lock<std::shared_mutex> lock(config_mutex_);
    config_data_[key] = value;
}

void ConfigManager::SetBool(const std::string& key, bool value) {
    std::unique_lock<std::shared_mutex> lock(config_mutex_);
    config_data_[key] = value;
}

void ConfigManager::SetStringArray(const std::string& key, const std::vector<std::string>& value) {
    std::unique_lock<std::shared_mutex> lock(config_mutex_);
    config_data_[key] = value;
}

bool ConfigManager::HasKey(const std::string& key) const {
    std::shared_lock<std::shared_mutex> lock(config_mutex_);
    return config_data_.find(key) != config_data_.end();
}

bool ConfigManager::ValidateRequiredKeys(const std::vector<std::string>& required_keys) const {
    std::shared_lock<std::shared_mutex> lock(config_mutex_);
    for (const auto& key : required_keys) {
        if (config_data_.find(key) == config_data_.end()) {
            LOG_ERROR("Required configuration key missing: {}", key);
            return false;
        }
    }
    return true;
}

std::vector<ConfigManager::ExchangeConfig> ConfigManager::GetExchangeConfigs() const {
    std::vector<ExchangeConfig> configs;
    
    // Binance configuration
    ExchangeConfig binance;
    binance.name = GetString("binance.name", "binance");
    binance.api_key = GetString("binance.api_key", "");
    binance.secret_key = GetString("binance.secret_key", "");
    binance.base_url = GetString("binance.base_url", "https://api.binance.com");
    binance.ws_url = GetString("binance.ws_url", "wss://stream.binance.com:9443/ws");
    binance.enabled = GetBool("binance.enabled", true);
    binance.rate_limit_per_second = GetInt("binance.rate_limit_per_second", 10);
    binance.maker_fee = GetDouble("binance.maker_fee", 0.001);
    binance.taker_fee = GetDouble("binance.taker_fee", 0.001);
    configs.push_back(binance);
    
    // Upbit configuration
    ExchangeConfig upbit;
    upbit.name = GetString("upbit.name", "upbit");
    upbit.api_key = GetString("upbit.api_key", "");
    upbit.secret_key = GetString("upbit.secret_key", "");
    upbit.base_url = GetString("upbit.base_url", "https://api.upbit.com");
    upbit.ws_url = GetString("upbit.ws_url", "wss://api.upbit.com/websocket/v1");
    upbit.enabled = GetBool("upbit.enabled", true);
    upbit.rate_limit_per_second = GetInt("upbit.rate_limit_per_second", 10);
    upbit.maker_fee = GetDouble("upbit.maker_fee", 0.0025);
    upbit.taker_fee = GetDouble("upbit.taker_fee", 0.0025);
    configs.push_back(upbit);
    
    return configs;
}

std::vector<std::string> ConfigManager::GetTradingPairs() const {
    return GetStringArray("trading.pairs", {
        "BTC/USDT",
        "ETH/USDT", 
        "BNB/USDT",
        "ADA/USDT",
        "SOL/USDT"
    });
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
            LOG_ERROR("Empty JSON content - configuration file is required");
            return false;
        }
        
        // Parse JSON content
        JsonValue root = JsonParser::ParseString(json_content);
        
        if (!ats::json::IsObject(root)) {
            LOG_ERROR("JSON root is not an object");
            return false;
        }
        
        auto rootObj = ats::json::AsObject(root);
        ParseJsonObject(rootObj, "");
        
        // Validate required configuration keys
        std::vector<std::string> required_keys = {
            "app.name",
            "app.version"
        };
        
        if (!ValidateRequiredKeys(required_keys)) {
            LOG_ERROR("Required configuration keys are missing");
            return false;
        }
        
        LOG_INFO("Configuration parsed and validated successfully from JSON");
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
    std::shared_lock<std::shared_mutex> lock(config_mutex_);
    
    // Simple JSON output - basic implementation
    std::ostringstream json;
    json << "{\n";
    
    bool first = true;
    for (const auto& pair : config_data_) {
        if (!first) json << ",\n";
        first = false;
        
        json << "  \"" << pair.first << "\": ";
        
        std::visit([&json](const auto& value) {
            using T = std::decay_t<decltype(value)>;
            if constexpr (std::is_same_v<T, std::string>) {
                json << "\"" << value << "\"";
            } else if constexpr (std::is_same_v<T, bool>) {
                json << (value ? "true" : "false");
            } else if constexpr (std::is_arithmetic_v<T>) {
                json << value;
            } else if constexpr (std::is_same_v<T, std::vector<std::string>>) {
                json << "[";
                for (size_t i = 0; i < value.size(); ++i) {
                    if (i > 0) json << ", ";
                    json << "\"" << value[i] << "\"";
                }
                json << "]";
            }
        }, pair.second);
    }
    
    json << "\n}";
    return json.str();
}

void ConfigManager::ParseJsonObject(const std::unordered_map<std::string, JsonValue>& obj, const std::string& prefix) {
    for (const auto& pair : obj) {
        std::string key = prefix.empty() ? pair.first : prefix + "." + pair.first;
        ParseJsonValue(key, pair.second);
    }
}

void ConfigManager::ParseJsonValue(const std::string& key, const JsonValue& value) {
    if (ats::json::IsString(value)) {
        config_data_[key] = ats::json::AsString(value);
    } else if (ats::json::IsInt(value)) {
        config_data_[key] = ats::json::AsInt(value);
    } else if (ats::json::IsDouble(value)) {
        config_data_[key] = ats::json::AsDouble(value);
    } else if (ats::json::IsBool(value)) {
        config_data_[key] = ats::json::AsBool(value);
    } else if (ats::json::IsArray(value)) {
        auto arr = ats::json::AsArray(value);
        std::vector<std::string> string_array;
        for (const auto& item : arr) {
            if (ats::json::IsString(item)) {
                string_array.push_back(ats::json::AsString(item));
            }
        }
        config_data_[key] = string_array;
    } else if (ats::json::IsObject(value)) {
        auto obj = ats::json::AsObject(value);
        ParseJsonObject(obj, key);
    }
}

template<typename T>
T ConfigManager::GetValue(const std::string& key, const T& default_value) const {
    std::shared_lock<std::shared_mutex> lock(config_mutex_);
    
    auto it = config_data_.find(key);
    if (it == config_data_.end()) {
        return default_value;
    }
    
    try {
        return std::get<T>(it->second);
    } catch (const std::bad_variant_access&) {
        LOG_WARNING("Type mismatch for config key: {}, using default", key);
        return default_value;
    }
}

// Explicit template instantiations
template std::string ConfigManager::GetValue<std::string>(const std::string&, const std::string&) const;
template int ConfigManager::GetValue<int>(const std::string&, const int&) const;
template double ConfigManager::GetValue<double>(const std::string&, const double&) const;
template bool ConfigManager::GetValue<bool>(const std::string&, const bool&) const;
template std::vector<std::string> ConfigManager::GetValue<std::vector<std::string>>(const std::string&, const std::vector<std::string>&) const;

} // namespace ats 