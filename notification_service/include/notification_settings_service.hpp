#pragma once

#include "push_notification_service.hpp"
#include "email_notification_service.hpp"
#include "exchange/exchange_notification_system.hpp"
#include <string>
#include <vector>
#include <memory>
#include <unordered_map>
#include <chrono>
#include <atomic>
#include <mutex>

namespace ats {
namespace notification {

enum class NotificationChannel {
    PUSH,
    EMAIL,
    SMS,
    SLACK,
    WEBHOOK
};

enum class NotificationFrequency {
    IMMEDIATE,
    BATCHED_5MIN,
    BATCHED_15MIN,
    BATCHED_HOURLY,
    DAILY_DIGEST,
    DISABLED
};

struct NotificationRule {
    std::string rule_id;
    std::string user_id;
    std::string category; // "risk", "trade", "system", "market"
    exchange::NotificationLevel min_level = exchange::NotificationLevel::INFO;
    std::vector<NotificationChannel> enabled_channels;
    NotificationFrequency frequency = NotificationFrequency::IMMEDIATE;
    bool enabled = true;
    
    // Time-based settings
    std::string quiet_hours_start = "22:00"; // 10 PM
    std::string quiet_hours_end = "08:00";   // 8 AM
    std::vector<int> quiet_days; // 0 = Sunday, 6 = Saturday
    std::string timezone = "UTC";
    
    // Throttling settings
    int max_notifications_per_hour = 10;
    std::chrono::minutes cooldown_period{5};
    
    // Content filtering
    std::vector<std::string> keyword_filters; // Only send if message contains these
    std::vector<std::string> exclude_keywords; // Don't send if message contains these
    std::vector<std::string> exchange_filters; // Only for specific exchanges
    
    std::chrono::system_clock::time_point created_at;
    std::chrono::system_clock::time_point updated_at;
    
    NotificationRule() {
        auto now = std::chrono::system_clock::now();
        created_at = now;
        updated_at = now;
    }
};

struct UserNotificationProfile {
    std::string user_id;
    std::string email;
    std::string phone_number;
    std::string preferred_timezone = "UTC";
    
    // Global settings
    bool global_enabled = true;
    bool quiet_mode_enabled = false;
    std::string quiet_hours_start = "22:00";
    std::string quiet_hours_end = "08:00";
    
    // Channel preferences
    std::unordered_map<NotificationChannel, bool> channel_enabled;
    std::unordered_map<NotificationChannel, NotificationFrequency> channel_frequency;
    
    // Device tokens for push notifications
    std::vector<DeviceRegistration> registered_devices;
    
    // Custom notification rules
    std::vector<NotificationRule> custom_rules;
    
    std::chrono::system_clock::time_point created_at;
    std::chrono::system_clock::time_point last_updated;
    
    UserNotificationProfile(const std::string& uid) : user_id(uid) {
        auto now = std::chrono::system_clock::now();
        created_at = now;
        last_updated = now;
        
        // Default channel settings
        channel_enabled[NotificationChannel::PUSH] = true;
        channel_enabled[NotificationChannel::EMAIL] = true;
        channel_enabled[NotificationChannel::SMS] = false;
        channel_enabled[NotificationChannel::SLACK] = false;
        channel_enabled[NotificationChannel::WEBHOOK] = false;
        
        // Default frequency settings
        channel_frequency[NotificationChannel::PUSH] = NotificationFrequency::IMMEDIATE;
        channel_frequency[NotificationChannel::EMAIL] = NotificationFrequency::BATCHED_15MIN;
        channel_frequency[NotificationChannel::SMS] = NotificationFrequency::IMMEDIATE;
        channel_frequency[NotificationChannel::SLACK] = NotificationFrequency::BATCHED_5MIN;
        channel_frequency[NotificationChannel::WEBHOOK] = NotificationFrequency::IMMEDIATE;
    }
};

struct NotificationBatch {
    std::string batch_id;
    std::string user_id;
    NotificationChannel channel;
    std::vector<exchange::NotificationMessage> messages;
    std::chrono::system_clock::time_point created_at;
    std::chrono::system_clock::time_point scheduled_send_time;
    bool sent = false;
    
    NotificationBatch() : created_at(std::chrono::system_clock::now()) {}
};

class NotificationSettingsService {
public:
    NotificationSettingsService(
        std::shared_ptr<PushNotificationService> push_service,
        std::shared_ptr<EmailNotificationService> email_service);
    
    ~NotificationSettingsService();
    
    bool initialize();
    void shutdown();
    
    // User profile management
    bool create_user_profile(const UserNotificationProfile& profile);
    bool update_user_profile(const UserNotificationProfile& profile);
    bool delete_user_profile(const std::string& user_id);
    UserNotificationProfile* get_user_profile(const std::string& user_id);
    std::vector<UserNotificationProfile> get_all_user_profiles() const;
    
    // Notification rule management
    bool add_notification_rule(const std::string& user_id, const NotificationRule& rule);
    bool update_notification_rule(const std::string& user_id, const NotificationRule& rule);
    bool delete_notification_rule(const std::string& user_id, const std::string& rule_id);
    std::vector<NotificationRule> get_user_rules(const std::string& user_id) const;
    
    // Device management
    bool register_user_device(const std::string& user_id, const DeviceRegistration& device);
    bool unregister_user_device(const std::string& user_id, const std::string& device_id);
    bool update_device_token(const std::string& user_id, const std::string& device_id, 
                           const std::string& new_token);
    std::vector<DeviceRegistration> get_user_devices(const std::string& user_id) const;
    
    // Quick settings methods
    bool enable_notifications(const std::string& user_id, bool enabled = true);
    bool set_quiet_mode(const std::string& user_id, bool enabled, 
                       const std::string& start_time = "", const std::string& end_time = "");
    bool set_channel_enabled(const std::string& user_id, NotificationChannel channel, bool enabled);
    bool set_channel_frequency(const std::string& user_id, NotificationChannel channel, 
                              NotificationFrequency frequency);
    bool set_minimum_level(const std::string& user_id, const std::string& category,
                          exchange::NotificationLevel min_level);
    
    // Notification processing
    bool should_send_notification(const std::string& user_id, 
                                const exchange::NotificationMessage& message,
                                const std::string& category,
                                NotificationChannel channel) const;
    
    bool process_notification(const exchange::NotificationMessage& message,
                            const std::string& category = "general");
    
    // Batch processing
    void start_batch_processor();
    void stop_batch_processor();
    void process_pending_batches();
    
    // Integration helpers
    exchange::NotificationHandler create_settings_aware_handler(const std::string& category = "general");
    
    // Statistics and monitoring
    struct NotificationSettingsStats {
        std::atomic<uint64_t> total_users{0};
        std::atomic<uint64_t> active_users{0};
        std::atomic<uint64_t> total_rules{0};
        std::atomic<uint64_t> notifications_filtered{0};
        std::atomic<uint64_t> notifications_batched{0};
        std::atomic<uint64_t> notifications_sent_immediate{0};
        
        std::unordered_map<NotificationChannel, uint64_t> channel_usage;
        std::unordered_map<NotificationFrequency, uint64_t> frequency_usage;
    };
    
    NotificationSettingsStats get_stats() const;
    void reset_stats();
    
    // Configuration import/export
    std::string export_user_settings(const std::string& user_id) const;
    bool import_user_settings(const std::string& user_id, const std::string& settings_json);
    
    // Bulk operations
    bool bulk_update_user_settings(const std::vector<std::string>& user_ids,
                                 const std::function<void(UserNotificationProfile&)>& updater);
    
private:
    std::shared_ptr<PushNotificationService> push_service_;
    std::shared_ptr<EmailNotificationService> email_service_;
    
    std::vector<UserNotificationProfile> user_profiles_;
    std::vector<NotificationBatch> pending_batches_;
    
    mutable std::shared_mutex profiles_mutex_;
    mutable std::shared_mutex batches_mutex_;
    
    std::atomic<bool> initialized_{false};
    std::atomic<bool> batch_processor_running_{false};
    std::unique_ptr<std::thread> batch_processor_thread_;
    
    NotificationSettingsStats stats_;
    
    // Helper methods
    bool is_in_quiet_hours(const UserNotificationProfile& profile) const;
    bool matches_rule_criteria(const NotificationRule& rule, 
                             const exchange::NotificationMessage& message,
                             const std::string& category) const;
    
    void add_to_batch(const std::string& user_id, NotificationChannel channel,
                     const exchange::NotificationMessage& message);
    
    void send_batched_notifications(const NotificationBatch& batch);
    void batch_processor_loop();
    
    std::string generate_batch_id() const;
    std::string get_current_time_string() const;
    bool is_time_in_range(const std::string& time_str, const std::string& start, const std::string& end) const;
    
    // Default rule creation helpers
    void create_default_rules_for_user(UserNotificationProfile& profile);
    NotificationRule create_risk_rule(const std::string& user_id);
    NotificationRule create_trade_rule(const std::string& user_id);
    NotificationRule create_system_rule(const std::string& user_id);
    
    // Persistence helpers (for future database integration)
    void save_user_profile(const UserNotificationProfile& profile);
    void load_user_profiles();
};

// Utility functions for notification settings
namespace settings_utils {

std::string notification_level_to_string(exchange::NotificationLevel level);
exchange::NotificationLevel string_to_notification_level(const std::string& level_str);

std::string notification_channel_to_string(NotificationChannel channel);
NotificationChannel string_to_notification_channel(const std::string& channel_str);

std::string notification_frequency_to_string(NotificationFrequency frequency);
NotificationFrequency string_to_notification_frequency(const std::string& frequency_str);

// Time parsing helpers
std::chrono::minutes parse_time_string(const std::string& time_str);
std::string current_time_string();
bool is_weekend(const std::chrono::system_clock::time_point& time_point);

// Default configuration generators
UserNotificationProfile create_default_user_profile(const std::string& user_id, 
                                                   const std::string& email = "");

std::vector<NotificationRule> create_default_notification_rules(const std::string& user_id);

} // namespace settings_utils

} // namespace notification
} // namespace ats