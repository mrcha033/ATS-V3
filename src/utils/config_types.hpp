#pragma once

#include <string>
#include <vector>
#include <map>
#include <nlohmann/json.hpp>

namespace ats {

std::string get_env_var(const std::string& key);

struct AppConfig {
    std::string name;
    std::string version;
    bool debug;
    std::string log_level;

    
};

struct ExchangeConfig {
    std::string name;
    bool enabled;
    std::string api_key;
    std::string secret_key;
    std::string base_url;
    std::string ws_url;
    int rate_limit_per_second;
    double maker_fee;
    double taker_fee;
    bool testnet;
};

struct TradingConfig {
    std::vector<std::string> pairs;
    std::string base_currency;

    
};

struct ArbitrageConfig {
    double min_profit_threshold;
    double max_position_size;
    double max_risk_per_trade;
    double min_volume_usd;
    int execution_timeout_ms;
    int price_update_interval_ms;
    int opportunity_check_interval_ms;

    
};

struct RiskManagementConfig {
    double max_daily_loss;
    int max_open_positions;
    double position_size_percent;
    double stop_loss_percent;
    double max_slippage_percent;
};
NLOHMANN_DEFINE_TYPE_NON_INTRUSIVE(RiskManagementConfig, max_daily_loss, max_open_positions, position_size_percent, stop_loss_percent, max_slippage_percent)

struct MonitoringConfig {
    int system_check_interval_sec;
    int performance_log_interval_sec;
    bool alert_on_high_cpu;
    bool alert_on_high_memory;
    bool alert_on_network_issues;
    double cpu_threshold_percent;
    double memory_threshold_percent;
    double temperature_threshold_celsius;

    
};

    
};

struct TelegramAlertConfig {
    bool enabled;
    std::string bot_token;
    std::string chat_id;

    
};

struct DiscordAlertConfig {
    bool enabled;
    std::string webhook_url;

    
};

struct AlertsConfig {
    bool enabled;
    TelegramAlertConfig telegram;
    DiscordAlertConfig discord;

    
};

struct DatabaseConfig {
    std::string type;
    std::string path;
    int backup_interval_hours;
    int max_backup_files;
};
NLOHMANN_DEFINE_TYPE_NON_INTRUSIVE(DatabaseConfig, type, path, backup_interval_hours, max_backup_files)

struct LoggingConfig {
    std::string file_path;
    int max_file_size_mb;
    int max_backup_files;
    bool console_output;
    bool file_output;
};

} // namespace ats
