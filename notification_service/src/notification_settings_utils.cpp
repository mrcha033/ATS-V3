#include "notification_settings_service.hpp"
#include <sstream>
#include <iomanip>
#include <chrono>
#include <ctime>

namespace ats {
namespace notification {
namespace settings_utils {

std::string notification_level_to_string(exchange::NotificationLevel level) {
    switch (level) {
        case exchange::NotificationLevel::INFO: return "INFO";
        case exchange::NotificationLevel::WARNING: return "WARNING";
        case exchange::NotificationLevel::ERROR: return "ERROR";
        case exchange::NotificationLevel::CRITICAL: return "CRITICAL";
        default: return "UNKNOWN";
    }
}

exchange::NotificationLevel string_to_notification_level(const std::string& level_str) {
    if (level_str == "INFO") return exchange::NotificationLevel::INFO;
    if (level_str == "WARNING") return exchange::NotificationLevel::WARNING;
    if (level_str == "ERROR") return exchange::NotificationLevel::ERROR;
    if (level_str == "CRITICAL") return exchange::NotificationLevel::CRITICAL;
    return exchange::NotificationLevel::INFO; // Default
}

std::string notification_channel_to_string(NotificationChannel channel) {
    switch (channel) {
        case NotificationChannel::PUSH: return "PUSH";
        case NotificationChannel::EMAIL: return "EMAIL";
        case NotificationChannel::SMS: return "SMS";
        case NotificationChannel::SLACK: return "SLACK";
        case NotificationChannel::WEBHOOK: return "WEBHOOK";
        default: return "UNKNOWN";
    }
}

NotificationChannel string_to_notification_channel(const std::string& channel_str) {
    if (channel_str == "PUSH") return NotificationChannel::PUSH;
    if (channel_str == "EMAIL") return NotificationChannel::EMAIL;
    if (channel_str == "SMS") return NotificationChannel::SMS;
    if (channel_str == "SLACK") return NotificationChannel::SLACK;
    if (channel_str == "WEBHOOK") return NotificationChannel::WEBHOOK;
    return NotificationChannel::PUSH; // Default
}

std::string notification_frequency_to_string(NotificationFrequency frequency) {
    switch (frequency) {
        case NotificationFrequency::IMMEDIATE: return "IMMEDIATE";
        case NotificationFrequency::BATCHED_5MIN: return "BATCHED_5MIN";
        case NotificationFrequency::BATCHED_15MIN: return "BATCHED_15MIN";
        case NotificationFrequency::BATCHED_HOURLY: return "BATCHED_HOURLY";
        case NotificationFrequency::DAILY_DIGEST: return "DAILY_DIGEST";
        case NotificationFrequency::DISABLED: return "DISABLED";
        default: return "UNKNOWN";
    }
}

NotificationFrequency string_to_notification_frequency(const std::string& frequency_str) {
    if (frequency_str == "IMMEDIATE") return NotificationFrequency::IMMEDIATE;
    if (frequency_str == "BATCHED_5MIN") return NotificationFrequency::BATCHED_5MIN;
    if (frequency_str == "BATCHED_15MIN") return NotificationFrequency::BATCHED_15MIN;
    if (frequency_str == "BATCHED_HOURLY") return NotificationFrequency::BATCHED_HOURLY;
    if (frequency_str == "DAILY_DIGEST") return NotificationFrequency::DAILY_DIGEST;
    if (frequency_str == "DISABLED") return NotificationFrequency::DISABLED;
    return NotificationFrequency::IMMEDIATE; // Default
}

std::chrono::minutes parse_time_string(const std::string& time_str) {
    // Parse time in HH:MM format
    std::istringstream iss(time_str);
    std::string hour_str, minute_str;
    
    if (std::getline(iss, hour_str, ':') && std::getline(iss, minute_str)) {
        try {
            int hours = std::stoi(hour_str);
            int minutes = std::stoi(minute_str);
            return std::chrono::minutes(hours * 60 + minutes);
        } catch (const std::exception&) {
            // Return 0 minutes on parse error
            return std::chrono::minutes(0);
        }
    }
    
    return std::chrono::minutes(0);
}

std::string current_time_string() {
    auto now = std::chrono::system_clock::now();
    auto time_t = std::chrono::system_clock::to_time_t(now);
    
    std::stringstream ss;
    ss << std::put_time(std::localtime(&time_t), "%H:%M");
    return ss.str();
}

bool is_weekend(const std::chrono::system_clock::time_point& time_point) {
    auto time_t = std::chrono::system_clock::to_time_t(time_point);
    auto tm_struct = *std::localtime(&time_t);
    
    // tm_wday: 0=Sunday, 6=Saturday
    return (tm_struct.tm_wday == 0 || tm_struct.tm_wday == 6);
}

UserNotificationProfile create_default_user_profile(const std::string& user_id, 
                                                   const std::string& email) {
    UserNotificationProfile profile(user_id);
    profile.email = email;
    profile.preferred_timezone = "UTC";
    profile.global_enabled = true;
    profile.quiet_mode_enabled = false;
    profile.quiet_hours_start = "22:00";
    profile.quiet_hours_end = "08:00";
    
    // Default channel settings
    profile.channel_enabled[NotificationChannel::PUSH] = true;
    profile.channel_enabled[NotificationChannel::EMAIL] = true;
    profile.channel_enabled[NotificationChannel::SMS] = false;
    profile.channel_enabled[NotificationChannel::SLACK] = false;
    profile.channel_enabled[NotificationChannel::WEBHOOK] = false;
    
    // Default frequency settings
    profile.channel_frequency[NotificationChannel::PUSH] = NotificationFrequency::IMMEDIATE;
    profile.channel_frequency[NotificationChannel::EMAIL] = NotificationFrequency::BATCHED_15MIN;
    profile.channel_frequency[NotificationChannel::SMS] = NotificationFrequency::IMMEDIATE;
    profile.channel_frequency[NotificationChannel::SLACK] = NotificationFrequency::BATCHED_5MIN;
    profile.channel_frequency[NotificationChannel::WEBHOOK] = NotificationFrequency::IMMEDIATE;
    
    return profile;
}

std::vector<NotificationRule> create_default_notification_rules(const std::string& user_id) {
    std::vector<NotificationRule> rules;
    
    // Risk notification rule
    NotificationRule risk_rule;
    risk_rule.rule_id = "default_risk";
    risk_rule.user_id = user_id;
    risk_rule.category = "risk";
    risk_rule.min_level = exchange::NotificationLevel::WARNING;
    risk_rule.enabled_channels = {NotificationChannel::PUSH, NotificationChannel::EMAIL};
    risk_rule.frequency = NotificationFrequency::IMMEDIATE;
    risk_rule.enabled = true;
    risk_rule.max_notifications_per_hour = 10;
    risk_rule.cooldown_period = std::chrono::minutes(5);
    
    rules.push_back(risk_rule);
    
    // Trade notification rule
    NotificationRule trade_rule;
    trade_rule.rule_id = "default_trade";
    trade_rule.user_id = user_id;
    trade_rule.category = "trade";
    trade_rule.min_level = exchange::NotificationLevel::INFO;
    trade_rule.enabled_channels = {NotificationChannel::PUSH};
    trade_rule.frequency = NotificationFrequency::BATCHED_5MIN;
    trade_rule.enabled = true;
    trade_rule.max_notifications_per_hour = 30;
    trade_rule.cooldown_period = std::chrono::minutes(1);
    
    rules.push_back(trade_rule);
    
    // System notification rule
    NotificationRule system_rule;
    system_rule.rule_id = "default_system";
    system_rule.user_id = user_id;
    system_rule.category = "system";
    system_rule.min_level = exchange::NotificationLevel::ERROR;
    system_rule.enabled_channels = {NotificationChannel::EMAIL};
    system_rule.frequency = NotificationFrequency::IMMEDIATE;
    system_rule.enabled = true;
    system_rule.max_notifications_per_hour = 5;
    system_rule.cooldown_period = std::chrono::minutes(10);
    
    rules.push_back(system_rule);
    
    // Market notification rule
    NotificationRule market_rule;
    market_rule.rule_id = "default_market";
    market_rule.user_id = user_id;
    market_rule.category = "market";
    market_rule.min_level = exchange::NotificationLevel::INFO;
    market_rule.enabled_channels = {NotificationChannel::PUSH};
    market_rule.frequency = NotificationFrequency::BATCHED_HOURLY;
    market_rule.enabled = false; // Disabled by default
    market_rule.max_notifications_per_hour = 60;
    market_rule.cooldown_period = std::chrono::minutes(1);
    
    rules.push_back(market_rule);
    
    return rules;
}

} // namespace settings_utils
} // namespace notification
} // namespace ats