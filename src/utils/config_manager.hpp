#pragma once

#include <string>
#include <unordered_map>
#include <variant>
#include <vector>

namespace ats {

using ConfigValue = std::variant<std::string, int, double, bool>;

class ConfigManager {
private:
    std::unordered_map<std::string, ConfigValue> config_data_;
    std::string config_file_path_;
    
public:
    ConfigManager() = default;
    ~ConfigManager() = default;
    
    bool LoadConfig(const std::string& file_path);
    bool ReloadConfig();
    bool SaveConfig(const std::string& file_path = "");
    
    // Getters with default values
    std::string GetString(const std::string& key, const std::string& default_value = "") const;
    int GetInt(const std::string& key, int default_value = 0) const;
    double GetDouble(const std::string& key, double default_value = 0.0) const;
    bool GetBool(const std::string& key, bool default_value = false) const;
    
    // Setters
    void SetString(const std::string& key, const std::string& value);
    void SetInt(const std::string& key, int value);
    void SetDouble(const std::string& key, double value);
    void SetBool(const std::string& key, bool value);
    
    // Validation
    bool HasKey(const std::string& key) const;
    bool ValidateRequiredKeys(const std::vector<std::string>& required_keys) const;
    
    // Exchange specific configurations
    struct ExchangeConfig {
        std::string name;
        std::string api_key;
        std::string secret_key;
        std::string base_url;
        std::string ws_url;
        bool enabled;
        int rate_limit_per_second;
        double maker_fee;
        double taker_fee;
    };
    
    std::vector<ExchangeConfig> GetExchangeConfigs() const;
    
    // Trading pairs configuration
    std::vector<std::string> GetTradingPairs() const;
    
    // Arbitrage thresholds
    double GetMinProfitThreshold() const;
    double GetMaxPositionSize() const;
    double GetMaxRiskPerTrade() const;
    
private:
    bool ParseJson(const std::string& json_content);
    std::string ToJson() const;
    
    template<typename T>
    T GetValue(const std::string& key, const T& default_value) const;
};

} // namespace ats 