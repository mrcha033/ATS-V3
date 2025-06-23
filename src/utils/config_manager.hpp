#pragma once

#include <string>
#include <unordered_map>
#include <vector>
#include <mutex>
#include "../core/types.hpp"

// Compatibility layer for older compilers that don't have std::shared_mutex
#if __cplusplus >= 201703L && defined(__has_include)
    #if __has_include(<shared_mutex>)
        #include <shared_mutex>
        #define HAS_SHARED_MUTEX 1
    #else
        #define HAS_SHARED_MUTEX 0
    #endif
#else
    #define HAS_SHARED_MUTEX 0
#endif

namespace ats {

// Configuration value type - simplified for compatibility
struct ConfigValue {
    enum Type { String, Int, Double, Bool, StringArray } type;
    union {
        std::string* string_val;
        int int_val;
        double double_val;
        bool bool_val;
        std::vector<std::string>* array_val;
    };
    
    // Default constructor
    ConfigValue() : type(Int), int_val(0) {}
    
    ConfigValue(const std::string& val) : type(String), string_val(new std::string(val)) {}
    ConfigValue(int val) : type(Int), int_val(val) {}
    ConfigValue(double val) : type(Double), double_val(val) {}
    ConfigValue(bool val) : type(Bool), bool_val(val) {}
    ConfigValue(const std::vector<std::string>& val) : type(StringArray), array_val(new std::vector<std::string>(val)) {}
    
    ~ConfigValue() {
        if (type == String) delete string_val;
        else if (type == StringArray) delete array_val;
    }
    
    // Copy constructor
    ConfigValue(const ConfigValue& other) : type(other.type) {
        switch (type) {
            case String: string_val = new std::string(*other.string_val); break;
            case Int: int_val = other.int_val; break;
            case Double: double_val = other.double_val; break;
            case Bool: bool_val = other.bool_val; break;
            case StringArray: array_val = new std::vector<std::string>(*other.array_val); break;
        }
    }
    
    // Assignment operator
    ConfigValue& operator=(const ConfigValue& other) {
        if (this != &other) {
            if (type == String) delete string_val;
            else if (type == StringArray) delete array_val;
            
            type = other.type;
            switch (type) {
                case String: string_val = new std::string(*other.string_val); break;
                case Int: int_val = other.int_val; break;
                case Double: double_val = other.double_val; break;
                case Bool: bool_val = other.bool_val; break;
                case StringArray: array_val = new std::vector<std::string>(*other.array_val); break;
            }
        }
        return *this;
    }
};

class ConfigManager {
private:
    std::unordered_map<std::string, ConfigValue> config_data_;
    std::string config_file_path_;
    
#if HAS_SHARED_MUTEX
    mutable std::shared_mutex config_mutex_; // Thread-safe access
#else
    mutable std::mutex config_mutex_; // Fallback for older compilers
#endif
    
public:
    ConfigManager() = default;
    ~ConfigManager() = default;
    
    bool LoadConfig(const std::string& file_path);
    bool ReloadConfig();
    bool SaveConfig(const std::string& file_path = "");
    
    // Getters with default values (thread-safe)
    std::string GetString(const std::string& key, const std::string& default_value = "") const;
    int GetInt(const std::string& key, int default_value = 0) const;
    double GetDouble(const std::string& key, double default_value = 0.0) const;
    bool GetBool(const std::string& key, bool default_value = false) const;
    std::vector<std::string> GetStringArray(const std::string& key, const std::vector<std::string>& default_value = {}) const;
    
    // Setters (thread-safe)
    void SetString(const std::string& key, const std::string& value);
    void SetInt(const std::string& key, int value);
    void SetDouble(const std::string& key, double value);
    void SetBool(const std::string& key, bool value);
    void SetStringArray(const std::string& key, const std::vector<std::string>& value);
    
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
    
    // JSON parsing helpers - implementation details hidden
    void ParseJsonFromString(const std::string& json_content);
    void ParseJsonRecursive(const void* json_value, const std::string& prefix);
    
    template<typename T>
    T GetValue(const std::string& key, const T& default_value) const;
};

} // namespace ats 