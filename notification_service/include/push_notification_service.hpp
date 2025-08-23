#pragma once

#include "exchange/exchange_notification_system.hpp"
#include "utils/influxdb_client.hpp"
#include <string>
#include <vector>
#include <memory>
#include <unordered_map>
#include <chrono>
#include <atomic>
#include <mutex>

namespace ats {
namespace notification {

enum class PushNotificationChannel {
    FCM_ANDROID,
    FCM_IOS,
    FCM_WEB
};

struct PushNotificationConfig {
    std::string firebase_server_key;
    std::string firebase_sender_id;
    std::string firebase_project_id;
    bool enabled = true;
    int retry_attempts = 3;
    std::chrono::seconds retry_delay{5};
};

struct DeviceRegistration {
    std::string device_id;
    std::string fcm_token;
    PushNotificationChannel channel;
    std::string user_id;
    std::chrono::system_clock::time_point registered_at;
    bool is_active = true;
    
    DeviceRegistration() : registered_at(std::chrono::system_clock::now()) {}
};

struct PushNotificationMessage {
    std::string title;
    std::string body;
    std::string icon;
    std::string click_action;
    std::unordered_map<std::string, std::string> data;
    std::string priority = "high";
    bool content_available = true;
    std::chrono::seconds time_to_live{86400}; // 24 hours
    
    std::string to_fcm_json(const std::string& fcm_token) const;
};

struct NotificationHistory {
    std::string notification_id;
    std::string user_id;
    std::string device_id;
    exchange::NotificationLevel level;
    std::string title;
    std::string message;
    std::string channel_type; // "push", "email"
    bool delivered = false;
    std::chrono::system_clock::time_point sent_at;
    std::chrono::system_clock::time_point delivered_at;
    std::string error_message;
    
    NotificationHistory() : sent_at(std::chrono::system_clock::now()) {}
};

class PushNotificationService {
public:
    explicit PushNotificationService(const PushNotificationConfig& config);
    ~PushNotificationService();
    
    bool initialize();
    void shutdown();
    
    // Device management
    bool register_device(const DeviceRegistration& registration);
    bool unregister_device(const std::string& device_id);
    bool update_device_token(const std::string& device_id, const std::string& new_token);
    std::vector<DeviceRegistration> get_user_devices(const std::string& user_id) const;
    
    // Push notification sending
    bool send_push_notification(const std::string& user_id, 
                               const PushNotificationMessage& message,
                               exchange::NotificationLevel level = exchange::NotificationLevel::INFO);
    
    bool send_push_notification_to_device(const std::string& device_id,
                                         const PushNotificationMessage& message,
                                         exchange::NotificationLevel level = exchange::NotificationLevel::INFO);
    
    bool send_broadcast_notification(const PushNotificationMessage& message,
                                   exchange::NotificationLevel level = exchange::NotificationLevel::INFO);
    
    // Integration with ExchangeNotificationSystem
    exchange::NotificationHandler create_push_notification_handler();
    
    // Statistics and monitoring
    struct PushNotificationStats {
        std::atomic<uint64_t> total_sent{0};
        std::atomic<uint64_t> total_delivered{0};
        std::atomic<uint64_t> total_failed{0};
        std::atomic<uint64_t> total_retries{0};
        std::atomic<uint64_t> active_devices{0};
        
        std::unordered_map<PushNotificationChannel, uint64_t> channel_stats;
    };
    
    PushNotificationStats get_stats() const;
    void reset_stats();
    
    // History management
    std::vector<NotificationHistory> get_notification_history(
        const std::string& user_id,
        std::chrono::hours lookback = std::chrono::hours(24)) const;
    
    void clear_old_history(std::chrono::hours max_age = std::chrono::hours(168)); // 7 days
    
private:
    PushNotificationConfig config_;
    std::vector<DeviceRegistration> registered_devices_;
    std::vector<NotificationHistory> notification_history_;
    std::shared_ptr<utils::InfluxDBClient> influxdb_client_;
    
    mutable std::shared_mutex devices_mutex_;
    mutable std::shared_mutex history_mutex_;
    
    PushNotificationStats stats_;
    std::atomic<bool> initialized_{false};
    
    // FCM HTTP client
    bool send_fcm_request(const std::string& fcm_token, 
                         const PushNotificationMessage& message,
                         std::string& response_body,
                         int& response_code);
    
    void handle_fcm_response(const std::string& response_body, 
                           int response_code,
                           const std::string& device_id,
                           NotificationHistory& history);
    
    void store_notification_history(const NotificationHistory& history);
    void store_notification_in_influxdb(const NotificationHistory& history);
    
    std::string generate_notification_id() const;
    
    // CURL helper for HTTP requests
    static size_t write_callback(void* contents, size_t size, size_t nmemb, std::string* userp);
};

// Risk-related notification helpers
namespace risk_notifications {

PushNotificationMessage create_risk_limit_exceeded_notification(
    const std::string& symbol,
    double current_exposure,
    double limit);

PushNotificationMessage create_trade_failure_notification(
    const std::string& symbol,
    const std::string& exchange,
    const std::string& error_reason);

PushNotificationMessage create_price_alert_notification(
    const std::string& symbol,
    double current_price,
    double alert_price,
    const std::string& condition);

PushNotificationMessage create_system_health_notification(
    const std::string& component,
    const std::string& status,
    const std::string& details);

} // namespace risk_notifications

} // namespace notification
} // namespace ats