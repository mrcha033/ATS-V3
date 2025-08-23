#pragma once

#include <string>
#include <vector>
#include <unordered_map>
#include <memory>
#include <mutex>
#include <functional>
#include <thread>
#include <atomic>
#ifdef HAS_NLOHMANN_JSON
#include <nlohmann/json.hpp>
#endif
#include "types/common_types.hpp"
#include "utils/crypto_utils.hpp"
#include "utils/logger.hpp"

namespace ats {
namespace config {

struct DatabaseConfig {
    std::string redis_host;
    int redis_port;
    std::string redis_password;
    std::string influxdb_host;
    int influxdb_port;
    std::string influxdb_username;
    std::string influxdb_password;
    std::string influxdb_database;
    std::string rocksdb_path;
    bool enable_ssl;
    
    DatabaseConfig() : redis_host("localhost"), redis_port(6379),
                      influxdb_host("localhost"), influxdb_port(8086),
                      influxdb_database("ats"), rocksdb_path("./data/rocksdb"),
                      enable_ssl(false) {}
};

struct MonitoringConfig {
    std::string log_level;
    std::string log_file_path;
    size_t log_max_file_size;
    size_t log_max_files;
    bool metrics_enabled;
    int prometheus_port;
    int dashboard_port;
    std::string notification_email;
    std::string notification_webhook;
    bool enable_performance_monitoring;
    
    MonitoringConfig() : log_level("INFO"), log_file_path("logs/ats.log"),
                        log_max_file_size(10 * 1024 * 1024), log_max_files(5),
                        metrics_enabled(true), prometheus_port(9090),
                        dashboard_port(8080), enable_performance_monitoring(true) {}
};

struct SecurityConfig {
    std::string master_key;
    bool encrypt_config;
    bool encrypt_logs;
    int session_timeout_minutes;
    bool enable_2fa;
    std::string jwt_secret;
    int jwt_expiry_hours;
    
    SecurityConfig() : encrypt_config(true), encrypt_logs(false),
                      session_timeout_minutes(60), enable_2fa(false),
                      jwt_expiry_hours(24) {}
};

class ConfigManager {
public:
    ConfigManager();
    ~ConfigManager();
    
    // Configuration loading and saving
    bool load_config(const std::string& config_file_path);
    bool save_config(const std::string& config_file_path) const;
    bool reload_config();
    
    // Hot reload functionality
    void enable_hot_reload(bool enable = true);
    void check_for_config_changes();
    
    // Exchange configurations
    std::vector<types::ExchangeConfig> get_exchange_configs() const;
    types::ExchangeConfig get_exchange_config(const std::string& exchange_id) const;
    void set_exchange_config(const types::ExchangeConfig& config);
    void remove_exchange_config(const std::string& exchange_id);
    
    // Trading configuration
    types::TradingConfig get_trading_config() const;
    void set_trading_config(const types::TradingConfig& config);
    
    // Risk management configuration
    types::RiskConfig get_risk_config() const;
    void set_risk_config(const types::RiskConfig& config);
    
    // Database configuration
    DatabaseConfig get_database_config() const;
    void set_database_config(const DatabaseConfig& config);
    
    // Monitoring configuration
    MonitoringConfig get_monitoring_config() const;
    void set_monitoring_config(const MonitoringConfig& config);
    
    // Security configuration
    SecurityConfig get_security_config() const;
    void set_security_config(const SecurityConfig& config);
    
    // Generic configuration access
    template<typename T>
    T get_value(const std::string& key, const T& default_value = T{}) const;
    
    template<typename T>
    void set_value(const std::string& key, const T& value);
    
    // Environment variable support
    std::string get_env_var(const std::string& var_name, const std::string& default_value = "") const;
    void load_env_overrides();
    
    // Configuration validation
    bool validate_config() const;
    std::vector<std::string> get_validation_errors() const;
    
    // Encrypted configuration support
    bool is_encrypted_config() const;
    bool set_master_password(const std::string& password);
    
    // Configuration file watching
    void start_file_watcher();
    void stop_file_watcher();
    
    // Callback registration for config changes
#ifdef HAS_NLOHMANN_JSON
    using ConfigChangeCallback = std::function<void(const std::string& section, const nlohmann::json& new_value)>;
#else
    using ConfigChangeCallback = std::function<void(const std::string& section, const std::string& new_value)>;
#endif
    void register_change_callback(const std::string& section, ConfigChangeCallback callback);
    void unregister_change_callback(const std::string& section);
    
    // Debug and utility functions
    std::string dump_config() const;
    void print_config_summary() const;
    
private:
    mutable std::mutex config_mutex_;
#ifdef HAS_NLOHMANN_JSON
    nlohmann::json config_json_;
#else
    std::unordered_map<std::string, std::string> config_map_;
#endif
    std::string config_file_path_;
    std::time_t last_modified_time_;
    bool hot_reload_enabled_;
    
    // Encrypted configuration support
    bool is_encrypted_;
    std::unique_ptr<utils::SecureString> master_password_;
    
    // Configuration watchers
    std::thread file_watcher_thread_;
    std::atomic<bool> file_watcher_running_;
    
    // Change callbacks
#ifndef HAS_NLOHMANN_JSON
    // When JSON is not available, callbacks are not supported
#else
    std::unordered_map<std::string, ConfigChangeCallback> change_callbacks_;
#endif
    
    // Private helper methods
    bool load_json_config(const std::string& file_path);
    bool load_encrypted_config(const std::string& file_path);
    bool save_json_config(const std::string& file_path) const;
    bool save_encrypted_config(const std::string& file_path) const;
    
#ifdef HAS_NLOHMANN_JSON
    nlohmann::json decrypt_config_section(const nlohmann::json& encrypted_section) const;
    nlohmann::json encrypt_config_section(const nlohmann::json& plain_section) const;
    
    void notify_config_change(const std::string& section, const nlohmann::json& new_value);
#endif
    
    void apply_env_overrides();
    
    // Configuration validation helpers
    bool validate_exchange_config(const types::ExchangeConfig& config, std::vector<std::string>& errors) const;
    bool validate_trading_config(const types::TradingConfig& config, std::vector<std::string>& errors) const;
    bool validate_risk_config(const types::RiskConfig& config, std::vector<std::string>& errors) const;
    bool validate_database_config(const DatabaseConfig& config, std::vector<std::string>& errors) const;
    
#ifdef HAS_NLOHMANN_JSON
    // JSON conversion helpers
    types::ExchangeConfig json_to_exchange_config(const nlohmann::json& json) const;
    nlohmann::json exchange_config_to_json(const types::ExchangeConfig& config) const;
    
    types::TradingConfig json_to_trading_config(const nlohmann::json& json) const;
    nlohmann::json trading_config_to_json(const types::TradingConfig& config) const;
    
    types::RiskConfig json_to_risk_config(const nlohmann::json& json) const;
    nlohmann::json risk_config_to_json(const types::RiskConfig& config) const;
    
    DatabaseConfig json_to_database_config(const nlohmann::json& json) const;
    nlohmann::json database_config_to_json(const DatabaseConfig& config) const;
#endif
    
#ifdef HAS_NLOHMANN_JSON
    MonitoringConfig json_to_monitoring_config(const nlohmann::json& json) const;
    nlohmann::json monitoring_config_to_json(const MonitoringConfig& config) const;
    
    SecurityConfig json_to_security_config(const nlohmann::json& json) const;
    nlohmann::json security_config_to_json(const SecurityConfig& config) const;
#endif
    
    // File watching implementation
    void file_watcher_loop();
    std::time_t get_file_modified_time(const std::string& file_path) const;
    
    // Environment variable mappings
    static const std::unordered_map<std::string, std::string> ENV_VAR_MAPPINGS;
};

// Template implementation
template<typename T>
T ConfigManager::get_value(const std::string& key, const T& default_value) const {
#ifdef HAS_NLOHMANN_JSON
    std::lock_guard<std::mutex> lock(config_mutex_);
    
    // Split key by dots for nested access
    std::vector<std::string> keys;
    std::stringstream ss(key);
    std::string item;
    while (std::getline(ss, item, '.')) {
        keys.push_back(item);
    }
    
    const nlohmann::json* current = &config_json_;
    for (const auto& k : keys) {
        if (current->contains(k)) {
            current = &(*current)[k];
        } else {
            return default_value;
        }
    }
    
    try {
        return current->get<T>();
    } catch (const std::exception&) {
        return default_value;
    }
#else
    return default_value;
#endif
}

template<typename T>
void ConfigManager::set_value(const std::string& key, const T& value) {
#ifdef HAS_NLOHMANN_JSON
    std::lock_guard<std::mutex> lock(config_mutex_);
    
    // Split key by dots for nested access
    std::vector<std::string> keys;
    std::stringstream ss(key);
    std::string item;
    while (std::getline(ss, item, '.')) {
        keys.push_back(item);
    }
    
    nlohmann::json* current = &config_json_;
    for (size_t i = 0; i < keys.size() - 1; ++i) {
        if (!current->contains(keys[i])) {
            (*current)[keys[i]] = nlohmann::json::object();
        }
        current = &(*current)[keys[i]];
    }
    
    (*current)[keys.back()] = value;
    
    // Notify change
    notify_config_change(keys[0], config_json_[keys[0]]);
#else
    ats::utils::Logger::warn("JSON functionality disabled - config value setting not supported");
#endif
}

} // namespace config
} // namespace ats