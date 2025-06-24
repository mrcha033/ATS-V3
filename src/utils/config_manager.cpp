#include "config_manager.hpp"
#include "logger.hpp"
#include <fstream>
#include <sstream>
#include <algorithm>
#include <cctype>
#include <nlohmann/json.hpp>

// Type alias for the forward-declared nlohmann_json
using nlohmann_json = nlohmann::json;

// Include shared_mutex only if available
#ifdef HAS_SHARED_MUTEX
    #if HAS_SHARED_MUTEX
        #include <shared_mutex>
    #endif
#endif

namespace ats {

// Type aliases for mutex locks based on availability
#if HAS_SHARED_MUTEX
    using shared_lock_type = std::shared_lock<std::shared_mutex>;
    using unique_lock_type = std::unique_lock<std::shared_mutex>;
#else
    using shared_lock_type = std::unique_lock<std::mutex>;  // Fallback to exclusive lock
    using unique_lock_type = std::unique_lock<std::mutex>;
#endif

bool ConfigManager::LoadConfig(const std::string& file_path) {
    unique_lock_type lock(config_mutex_);
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
    unique_lock_type lock(config_mutex_);
    if (config_file_path_.empty()) {
        LOG_ERROR("No config file path set for reload");
        return false;
    }
    
    config_data_.clear();
    lock.unlock(); // Release lock before calling LoadConfig
    return LoadConfig(config_file_path_);
}

bool ConfigManager::SaveConfig(const std::string& file_path) {
    shared_lock_type lock(config_mutex_);
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
    unique_lock_type lock(config_mutex_);
    config_data_[key] = value;
}

void ConfigManager::SetInt(const std::string& key, int value) {
    unique_lock_type lock(config_mutex_);
    config_data_[key] = value;
}

void ConfigManager::SetDouble(const std::string& key, double value) {
    unique_lock_type lock(config_mutex_);
    config_data_[key] = value;
}

void ConfigManager::SetBool(const std::string& key, bool value) {
    unique_lock_type lock(config_mutex_);
    config_data_[key] = value;
}

void ConfigManager::SetStringArray(const std::string& key, const std::vector<std::string>& value) {
    unique_lock_type lock(config_mutex_);
    config_data_[key] = value;
}

bool ConfigManager::HasKey(const std::string& key) const {
    shared_lock_type lock(config_mutex_);
    return config_data_.find(key) != config_data_.end();
}

bool ConfigManager::ValidateRequiredKeys(const std::vector<std::string>& required_keys) const {
    shared_lock_type lock(config_mutex_);
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
    binance.name = GetString("exchanges.binance.name", "binance");
    binance.api_key = GetString("exchanges.binance.api_key", "");
    binance.secret_key = GetString("exchanges.binance.secret_key", "");
    binance.base_url = GetString("exchanges.binance.base_url", "https://api.binance.com");
    binance.ws_url = GetString("exchanges.binance.ws_url", "wss://stream.binance.com:9443/ws");
    binance.enabled = GetBool("exchanges.binance.enabled", true);
    binance.rate_limit_per_second = GetInt("exchanges.binance.rate_limit_per_second", 10);
    binance.maker_fee = GetDouble("exchanges.binance.maker_fee", 0.001);
    binance.taker_fee = GetDouble("exchanges.binance.taker_fee", 0.001);
    configs.push_back(binance);
    
    // Upbit configuration
    ExchangeConfig upbit;
    upbit.name = GetString("exchanges.upbit.name", "upbit");
    upbit.api_key = GetString("exchanges.upbit.api_key", "");
    upbit.secret_key = GetString("exchanges.upbit.secret_key", "");
    upbit.base_url = GetString("exchanges.upbit.base_url", "https://api.upbit.com");
    upbit.ws_url = GetString("exchanges.upbit.ws_url", "wss://api.upbit.com/websocket/v1");
    upbit.enabled = GetBool("exchanges.upbit.enabled", true);
    upbit.rate_limit_per_second = GetInt("exchanges.upbit.rate_limit_per_second", 10);
    upbit.maker_fee = GetDouble("exchanges.upbit.maker_fee", 0.0025);
    upbit.taker_fee = GetDouble("exchanges.upbit.taker_fee", 0.0025);
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
        
        // Parse JSON content using our simple parser
        ParseJsonFromString(json_content);
        
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
        
    } catch (const std::exception& e) {
        LOG_ERROR("Configuration parse error: {}", e.what());
        LOG_WARNING("Using default configuration values");
        return false;
    }
}

std::string ConfigManager::ToJson() const {
    shared_lock_type lock(config_mutex_);
    
    // Simple JSON output - basic implementation
    std::ostringstream json;
    json << "{\n";
    
    bool first = true;
    for (const auto& pair : config_data_) {
        if (!first) json << ",\n";
        first = false;
        
        json << "  \"" << pair.first << "\": ";
        
        // Handle different config value types
        switch (pair.second.type) {
            case ConfigValue::String:
                json << "\"" << *pair.second.string_val << "\"";
                break;
            case ConfigValue::Int:
                json << pair.second.int_val;
                break;
            case ConfigValue::Double:
                json << pair.second.double_val;
                break;
            case ConfigValue::Bool:
                json << (pair.second.bool_val ? "true" : "false");
                break;
            case ConfigValue::StringArray:
                json << "[";
                for (size_t i = 0; i < pair.second.array_val->size(); ++i) {
                    if (i > 0) json << ", ";
                    json << "\"" << (*pair.second.array_val)[i] << "\"";
                }
                json << "]";
                break;
        }
    }
    
    json << "\n}";
    return json.str();
}

void ConfigManager::ParseJsonFromString(const std::string& json_content) {
    try {
        auto root = nlohmann::json::parse(json_content);
        
        if (!root.is_object()) {
            LOG_ERROR("JSON root is not an object");
            return;
        }
        
        // Simple recursive parsing
        ParseJsonRecursive(&root, "");
        
    } catch (const std::exception& e) {
        LOG_ERROR("JSON parsing failed: {}", e.what());
        throw;
    }
}

void ConfigManager::ParseJsonRecursive(const void* json_value_ptr, const std::string& prefix) {
    const auto& value = *static_cast<const nlohmann::json*>(json_value_ptr);
    
    if (value.is_object()) {
        for (auto it = value.begin(); it != value.end(); ++it) {
            std::string key = prefix.empty() ? it.key() : prefix + "." + it.key();
            ParseJsonRecursive(&(it.value()), key);
        }
    } else if (value.is_string()) {
        SetString(prefix, value.get<std::string>());
    } else if (value.is_number_integer()) {
        SetInt(prefix, static_cast<int>(value.get<int>()));
    } else if (value.is_number_float()) {
        SetDouble(prefix, value.get<double>());
    } else if (value.is_boolean()) {
        SetBool(prefix, value.get<bool>());
    } else if (value.is_array()) {
        std::vector<std::string> string_array;
        for (const auto& item : value) {
            if (item.is_string()) {
                string_array.push_back(item.get<std::string>());
            }
        }
        SetStringArray(prefix, string_array);
    }
}

template<typename T>
T ConfigManager::GetValue(const std::string& key, const T& default_value) const {
    shared_lock_type lock(config_mutex_);
    
    auto it = config_data_.find(key);
    if (it == config_data_.end()) {
        return default_value;
    }
    
    // Type-safe access to ConfigValue
    const ConfigValue& config_val = it->second;
    
    if constexpr (std::is_same_v<T, std::string>) {
        if (config_val.type == ConfigValue::String) {
            return *config_val.string_val;
        }
    } else if constexpr (std::is_same_v<T, int>) {
        if (config_val.type == ConfigValue::Int) {
            return config_val.int_val;
        }
    } else if constexpr (std::is_same_v<T, double>) {
        if (config_val.type == ConfigValue::Double) {
            return config_val.double_val;
        }
    } else if constexpr (std::is_same_v<T, bool>) {
        if (config_val.type == ConfigValue::Bool) {
            return config_val.bool_val;
        }
    } else if constexpr (std::is_same_v<T, std::vector<std::string>>) {
        if (config_val.type == ConfigValue::StringArray) {
            return *config_val.array_val;
        }
    }
    
    LOG_WARNING("Type mismatch for config key: {}, using default", key);
    return default_value;
}

// Explicit template instantiations
template std::string ConfigManager::GetValue<std::string>(const std::string&, const std::string&) const;
template int ConfigManager::GetValue<int>(const std::string&, const int&) const;
template double ConfigManager::GetValue<double>(const std::string&, const double&) const;
template bool ConfigManager::GetValue<bool>(const std::string&, const bool&) const;
template std::vector<std::string> ConfigManager::GetValue<std::vector<std::string>>(const std::string&, const std::vector<std::string>&) const;

} // namespace ats 
