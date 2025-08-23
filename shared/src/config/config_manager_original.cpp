#include "config/config_manager.hpp"
#include "utils/logger.hpp"
#include <fstream>
#include <filesystem>
#include <cstdlib>
#include <thread>
#include <chrono>

namespace ats {
namespace config {

// Environment variable mappings
const std::unordered_map<std::string, std::string> ConfigManager::ENV_VAR_MAPPINGS = {
    {"ATS_REDIS_HOST", "database.redis_host"},
    {"ATS_REDIS_PORT", "database.redis_port"},
    {"ATS_REDIS_PASSWORD", "database.redis_password"},
    {"ATS_INFLUXDB_HOST", "database.influxdb_host"},
    {"ATS_INFLUXDB_PORT", "database.influxdb_port"},
    {"ATS_INFLUXDB_USERNAME", "database.influxdb_username"},
    {"ATS_INFLUXDB_PASSWORD", "database.influxdb_password"},
    {"ATS_LOG_LEVEL", "monitoring.log_level"},
    {"ATS_LOG_FILE", "monitoring.log_file_path"},
    {"ATS_PROMETHEUS_PORT", "monitoring.prometheus_port"},
    {"ATS_DASHBOARD_PORT", "monitoring.dashboard_port"},
    {"ATS_MASTER_KEY", "security.master_key"},
    {"ATS_JWT_SECRET", "security.jwt_secret"},
    {"ATS_TRADING_ENABLED", "trading.enabled"},
    {"ATS_MIN_SPREAD", "trading.min_spread_threshold"},
    {"ATS_MAX_POSITION_SIZE", "trading.max_position_size"}
};

ConfigManager::ConfigManager() 
    : last_modified_time_(0)
    , hot_reload_enabled_(false)
    , is_encrypted_(false)
    , file_watcher_running_(false) {
    
    // Initialize with default configuration
    config_json_ = nlohmann::json::object();
    
    // Set default values
    config_json_["database"] = database_config_to_json(DatabaseConfig());
    config_json_["monitoring"] = monitoring_config_to_json(MonitoringConfig());
    config_json_["security"] = security_config_to_json(SecurityConfig());
    config_json_["trading"] = trading_config_to_json(types::TradingConfig());
    config_json_["risk"] = risk_config_to_json(types::RiskConfig());
    config_json_["exchanges"] = nlohmann::json::array();
}

ConfigManager::~ConfigManager() {
    stop_file_watcher();
}

bool ConfigManager::load_config(const std::string& config_file_path) {
    std::lock_guard<std::mutex> lock(config_mutex_);
    
    config_file_path_ = config_file_path;
    
    if (!std::filesystem::exists(config_file_path)) {
        utils::Logger::warn("Config file does not exist: {}", config_file_path);
        return false;
    }
    
    try {
        // Check if config is encrypted
        std::ifstream file(config_file_path);
        std::string first_line;
        std::getline(file, first_line);
        file.close();
        
        if (first_line.find("ATS_ENCRYPTED_CONFIG") != std::string::npos) {
            is_encrypted_ = true;
            if (!load_encrypted_config(config_file_path)) {
                utils::Logger::error("Failed to load encrypted config");
                return false;
            }
        } else {
            is_encrypted_ = false;
            if (!load_json_config(config_file_path)) {
                utils::Logger::error("Failed to load JSON config");
                return false;
            }
        }
        
        // Apply environment variable overrides
        apply_env_overrides();
        
        // Validate configuration
        if (!validate_config()) {
            utils::Logger::error("Configuration validation failed");
            auto errors = get_validation_errors();
            for (const auto& error : errors) {
                utils::Logger::error("Config validation error: {}", error);
            }
            return false;
        }
        
        last_modified_time_ = get_file_modified_time(config_file_path);
        
        utils::Logger::info("Configuration loaded successfully from: {}", config_file_path);
        return true;
        
    } catch (const std::exception& e) {
        utils::Logger::error("Exception loading config: {}", e.what());
        return false;
    }
}

bool ConfigManager::save_config(const std::string& config_file_path) const {
    std::lock_guard<std::mutex> lock(config_mutex_);
    
    try {
        // Create directory if it doesn't exist
        std::filesystem::create_directories(std::filesystem::path(config_file_path).parent_path());
        
        if (is_encrypted_) {
            return save_encrypted_config(config_file_path);
        } else {
            return save_json_config(config_file_path);
        }
        
    } catch (const std::exception& e) {
        utils::Logger::error("Exception saving config: {}", e.what());
        return false;
    }
}

bool ConfigManager::reload_config() {
    if (config_file_path_.empty()) {
        utils::Logger::warn("No config file path set for reload");
        return false;
    }
    
    return load_config(config_file_path_);
}

void ConfigManager::enable_hot_reload(bool enable) {
    hot_reload_enabled_ = enable;
    if (enable && !config_file_path_.empty()) {
        start_file_watcher();
    } else {
        stop_file_watcher();
    }
}

void ConfigManager::check_for_config_changes() {
    if (config_file_path_.empty()) return;
    
    std::time_t current_time = get_file_modified_time(config_file_path_);
    if (current_time > last_modified_time_) {
        utils::Logger::info("Config file changed, reloading...");
        reload_config();
    }
}

std::vector<types::ExchangeConfig> ConfigManager::get_exchange_configs() const {
    std::lock_guard<std::mutex> lock(config_mutex_);
    
    std::vector<types::ExchangeConfig> configs;
    if (config_json_.contains("exchanges") && config_json_["exchanges"].is_array()) {
        for (const auto& exchange_json : config_json_["exchanges"]) {
            configs.push_back(json_to_exchange_config(exchange_json));
        }
    }
    return configs;
}

types::ExchangeConfig ConfigManager::get_exchange_config(const std::string& exchange_id) const {
    std::lock_guard<std::mutex> lock(config_mutex_);
    
    if (config_json_.contains("exchanges") && config_json_["exchanges"].is_array()) {
        for (const auto& exchange_json : config_json_["exchanges"]) {
            if (exchange_json.contains("id") && exchange_json["id"].get<std::string>() == exchange_id) {
                return json_to_exchange_config(exchange_json);
            }
        }
    }
    
    return types::ExchangeConfig(); // Return default if not found
}

void ConfigManager::set_exchange_config(const types::ExchangeConfig& config) {
    std::lock_guard<std::mutex> lock(config_mutex_);
    
    if (!config_json_.contains("exchanges")) {
        config_json_["exchanges"] = nlohmann::json::array();
    }
    
    // Find existing config or add new one
    bool found = false;
    for (auto& exchange_json : config_json_["exchanges"]) {
        if (exchange_json.contains("id") && exchange_json["id"].get<std::string>() == config.id) {
            exchange_json = exchange_config_to_json(config);
            found = true;
            break;
        }
    }
    
    if (!found) {
        config_json_["exchanges"].push_back(exchange_config_to_json(config));
    }
    
    notify_config_change("exchanges", config_json_["exchanges"]);
}

types::TradingConfig ConfigManager::get_trading_config() const {
    std::lock_guard<std::mutex> lock(config_mutex_);
    
    if (config_json_.contains("trading")) {
        return json_to_trading_config(config_json_["trading"]);
    }
    
    return types::TradingConfig();
}

void ConfigManager::set_trading_config(const types::TradingConfig& config) {
    std::lock_guard<std::mutex> lock(config_mutex_);
    
    config_json_["trading"] = trading_config_to_json(config);
    notify_config_change("trading", config_json_["trading"]);
}

types::RiskConfig ConfigManager::get_risk_config() const {
    std::lock_guard<std::mutex> lock(config_mutex_);
    
    if (config_json_.contains("risk")) {
        return json_to_risk_config(config_json_["risk"]);
    }
    
    return types::RiskConfig();
}

void ConfigManager::set_risk_config(const types::RiskConfig& config) {
    std::lock_guard<std::mutex> lock(config_mutex_);
    
    config_json_["risk"] = risk_config_to_json(config);
    notify_config_change("risk", config_json_["risk"]);
}

DatabaseConfig ConfigManager::get_database_config() const {
    std::lock_guard<std::mutex> lock(config_mutex_);
    
    if (config_json_.contains("database")) {
        return json_to_database_config(config_json_["database"]);
    }
    
    return DatabaseConfig();
}

void ConfigManager::set_database_config(const DatabaseConfig& config) {
    std::lock_guard<std::mutex> lock(config_mutex_);
    
    config_json_["database"] = database_config_to_json(config);
    notify_config_change("database", config_json_["database"]);
}

MonitoringConfig ConfigManager::get_monitoring_config() const {
    std::lock_guard<std::mutex> lock(config_mutex_);
    
    if (config_json_.contains("monitoring")) {
        return json_to_monitoring_config(config_json_["monitoring"]);
    }
    
    return MonitoringConfig();
}

void ConfigManager::set_monitoring_config(const MonitoringConfig& config) {
    std::lock_guard<std::mutex> lock(config_mutex_);
    
    config_json_["monitoring"] = monitoring_config_to_json(config);
    notify_config_change("monitoring", config_json_["monitoring"]);
}

SecurityConfig ConfigManager::get_security_config() const {
    std::lock_guard<std::mutex> lock(config_mutex_);
    
    if (config_json_.contains("security")) {
        return json_to_security_config(config_json_["security"]);
    }
    
    return SecurityConfig();
}

void ConfigManager::set_security_config(const SecurityConfig& config) {
    std::lock_guard<std::mutex> lock(config_mutex_);
    
    config_json_["security"] = security_config_to_json(config);
    notify_config_change("security", config_json_["security"]);
}

std::string ConfigManager::get_env_var(const std::string& var_name, const std::string& default_value) const {
    const char* value = std::getenv(var_name.c_str());
    return value ? std::string(value) : default_value;
}

void ConfigManager::apply_env_overrides() {
    for (const auto& [env_var, config_path] : ENV_VAR_MAPPINGS) {
        std::string env_value = get_env_var(env_var);
        if (!env_value.empty()) {
            // Try to parse as different types
            try {
                if (env_value == "true" || env_value == "false") {
                    set_value(config_path, env_value == "true");
                } else {
                    // Try as number first
                    try {
                        if (env_value.find('.') != std::string::npos) {
                            double num_val = std::stod(env_value);
                            set_value(config_path, num_val);
                        } else {
                            int num_val = std::stoi(env_value);
                            set_value(config_path, num_val);
                        }
                    } catch (...) {
                        // If not a number, treat as string
                        set_value(config_path, env_value);
                    }
                }
            } catch (const std::exception& e) {
                utils::Logger::warn("Failed to apply env override {}: {}", env_var, e.what());
            }
        }
    }
}

bool ConfigManager::validate_config() const {
    std::vector<std::string> errors;
    
    // Validate exchange configurations
    auto exchanges = get_exchange_configs();
    for (const auto& exchange : exchanges) {
        validate_exchange_config(exchange, errors);
    }
    
    // Validate trading configuration
    validate_trading_config(get_trading_config(), errors);
    
    // Validate risk configuration
    validate_risk_config(get_risk_config(), errors);
    
    // Validate database configuration
    validate_database_config(get_database_config(), errors);
    
    return errors.empty();
}

std::vector<std::string> ConfigManager::get_validation_errors() const {
    std::vector<std::string> errors;
    
    // Validate exchange configurations
    auto exchanges = get_exchange_configs();
    for (const auto& exchange : exchanges) {
        validate_exchange_config(exchange, errors);
    }
    
    // Validate trading configuration
    validate_trading_config(get_trading_config(), errors);
    
    // Validate risk configuration
    validate_risk_config(get_risk_config(), errors);
    
    // Validate database configuration
    validate_database_config(get_database_config(), errors);
    
    return errors;
}

bool ConfigManager::load_json_config(const std::string& file_path) {
    std::ifstream file(file_path);
    if (!file.is_open()) {
        utils::Logger::error("Cannot open config file: {}", file_path);
        return false;
    }
    
    try {
        file >> config_json_;
        return true;
    } catch (const std::exception& e) {
        utils::Logger::error("JSON parsing error in config file: {}", e.what());
        return false;
    }
}

bool ConfigManager::save_json_config(const std::string& file_path) const {
    std::ofstream file(file_path);
    if (!file.is_open()) {
        utils::Logger::error("Cannot write config file: {}", file_path);
        return false;
    }
    
    file << config_json_.dump(4);
    return true;
}

std::string ConfigManager::dump_config() const {
    std::lock_guard<std::mutex> lock(config_mutex_);
    
    // Create a copy without sensitive data
    nlohmann::json safe_config = config_json_;
    
    // Remove sensitive information
    if (safe_config.contains("exchanges")) {
        for (auto& exchange : safe_config["exchanges"]) {
            if (exchange.contains("api_key")) exchange["api_key"] = "***";
            if (exchange.contains("secret_key")) exchange["secret_key"] = "***";
            if (exchange.contains("passphrase")) exchange["passphrase"] = "***";
        }
    }
    
    if (safe_config.contains("security")) {
        if (safe_config["security"].contains("master_key")) safe_config["security"]["master_key"] = "***";
        if (safe_config["security"].contains("jwt_secret")) safe_config["security"]["jwt_secret"] = "***";
    }
    
    if (safe_config.contains("database")) {
        if (safe_config["database"].contains("redis_password")) safe_config["database"]["redis_password"] = "***";
        if (safe_config["database"].contains("influxdb_password")) safe_config["database"]["influxdb_password"] = "***";
    }
    
    return safe_config.dump(4);
}

// JSON conversion helpers implementation
types::ExchangeConfig ConfigManager::json_to_exchange_config(const nlohmann::json& json) const {
    types::ExchangeConfig config;
    
    if (json.contains("id")) config.id = json["id"];
    if (json.contains("name")) config.name = json["name"];
    if (json.contains("api_key")) config.api_key = json["api_key"];
    if (json.contains("secret_key")) config.secret_key = json["secret_key"];
    if (json.contains("passphrase")) config.passphrase = json["passphrase"];
    if (json.contains("sandbox_mode")) config.sandbox_mode = json["sandbox_mode"];
    if (json.contains("rate_limit")) config.rate_limit = json["rate_limit"];
    if (json.contains("timeout_ms")) config.timeout_ms = json["timeout_ms"];
    if (json.contains("supported_symbols")) {
        config.supported_symbols = json["supported_symbols"];
    }
    
    return config;
}

nlohmann::json ConfigManager::exchange_config_to_json(const types::ExchangeConfig& config) const {
    nlohmann::json json;
    
    json["id"] = config.id;
    json["name"] = config.name;
    json["api_key"] = config.api_key;
    json["secret_key"] = config.secret_key;
    json["passphrase"] = config.passphrase;
    json["sandbox_mode"] = config.sandbox_mode;
    json["rate_limit"] = config.rate_limit;
    json["timeout_ms"] = config.timeout_ms;
    json["supported_symbols"] = config.supported_symbols;
    
    return json;
}

std::time_t ConfigManager::get_file_modified_time(const std::string& file_path) const {
    try {
        auto ftime = std::filesystem::last_write_time(file_path);
        auto sctp = std::chrono::time_point_cast<std::chrono::system_clock::duration>(
            ftime - std::filesystem::file_time_type::clock::now() + std::chrono::system_clock::now());
        return std::chrono::system_clock::to_time_t(sctp);
    } catch (const std::exception&) {
        return 0;
    }
}

void ConfigManager::notify_config_change(const std::string& section, const nlohmann::json& new_value) {
    auto it = change_callbacks_.find(section);
    if (it != change_callbacks_.end()) {
        try {
            it->second(section, new_value);
        } catch (const std::exception& e) {
            utils::Logger::error("Error in config change callback for section {}: {}", section, e.what());
        }
    }
}

// Validation helper implementations
bool ConfigManager::validate_exchange_config(const types::ExchangeConfig& config, std::vector<std::string>& errors) const {
    bool valid = true;
    
    if (config.id.empty()) {
        errors.push_back("Exchange ID cannot be empty");
        valid = false;
    }
    
    if (config.api_key.empty()) {
        errors.push_back("Exchange API key cannot be empty for " + config.id);
        valid = false;
    }
    
    if (config.secret_key.empty()) {
        errors.push_back("Exchange secret key cannot be empty for " + config.id);
        valid = false;
    }
    
    if (config.rate_limit <= 0) {
        errors.push_back("Exchange rate limit must be positive for " + config.id);
        valid = false;
    }
    
    if (config.timeout_ms <= 0) {
        errors.push_back("Exchange timeout must be positive for " + config.id);
        valid = false;
    }
    
    return valid;
}

bool ConfigManager::validate_trading_config(const types::TradingConfig& config, std::vector<std::string>& errors) const {
    bool valid = true;
    
    if (config.min_spread_threshold < 0) {
        errors.push_back("Minimum spread threshold cannot be negative");
        valid = false;
    }
    
    if (config.max_position_size <= 0) {
        errors.push_back("Maximum position size must be positive");
        valid = false;
    }
    
    if (config.max_daily_trades <= 0) {
        errors.push_back("Maximum daily trades must be positive");
        valid = false;
    }
    
    if (config.commission_rate < 0) {
        errors.push_back("Commission rate cannot be negative");
        valid = false;
    }
    
    return valid;
}

bool ConfigManager::validate_risk_config(const types::RiskConfig& config, std::vector<std::string>& errors) const {
    bool valid = true;
    
    if (config.max_portfolio_risk <= 0 || config.max_portfolio_risk > 1) {
        errors.push_back("Maximum portfolio risk must be between 0 and 1");
        valid = false;
    }
    
    if (config.max_single_trade_risk <= 0 || config.max_single_trade_risk > 1) {
        errors.push_back("Maximum single trade risk must be between 0 and 1");
        valid = false;
    }
    
    if (config.max_drawdown <= 0 || config.max_drawdown > 1) {
        errors.push_back("Maximum drawdown must be between 0 and 1");
        valid = false;
    }
    
    return valid;
}

bool ConfigManager::validate_database_config(const DatabaseConfig& config, std::vector<std::string>& errors) const {
    bool valid = true;
    
    if (config.redis_host.empty()) {
        errors.push_back("Redis host cannot be empty");
        valid = false;
    }
    
    if (config.redis_port <= 0 || config.redis_port > 65535) {
        errors.push_back("Redis port must be between 1 and 65535");
        valid = false;
    }
    
    if (config.influxdb_host.empty()) {
        errors.push_back("InfluxDB host cannot be empty");
        valid = false;
    }
    
    if (config.influxdb_port <= 0 || config.influxdb_port > 65535) {
        errors.push_back("InfluxDB port must be between 1 and 65535");
        valid = false;
    }
    
    if (config.rocksdb_path.empty()) {
        errors.push_back("RocksDB path cannot be empty");
        valid = false;
    }
    
    return valid;
}

} // namespace config
} // namespace ats