#pragma once

#include "failover_manager.hpp"
#include "resilient_exchange_adapter.hpp"
#include "types/common_types.hpp"
#include <memory>
#include <string>
#include <functional>
#include <vector>
#include <atomic>
#include <chrono>
#include <shared_mutex>
#include <unordered_map>
#include <thread>

namespace ats {
namespace exchange {

enum class NotificationLevel {
    INFO,
    WARNING,
    ERROR,
    CRITICAL
};

enum class NotificationChannel {
    LOG,
    EMAIL,
    SLACK,
    WEBHOOK,
    DATABASE,
    REDIS_PUBSUB
};

struct NotificationMessage {
    std::string id;
    NotificationLevel level;
    std::string title;
    std::string message;
    std::string exchange_id;
    std::chrono::system_clock::time_point timestamp;
    std::unordered_map<std::string, std::string> metadata;
    bool acknowledged = false;
    
    NotificationMessage() : timestamp(std::chrono::system_clock::now()) {}
    
    std::string to_json() const;
    static NotificationMessage from_json(const std::string& json_str);
};

struct NotificationRule {
    std::string rule_id;
    std::function<bool(const NotificationMessage&)> condition;
    std::vector<NotificationChannel> channels;
    std::chrono::milliseconds throttle_interval{std::chrono::minutes(5)};
    int max_notifications_per_hour = 10;
    bool enabled = true;
    
    std::chrono::system_clock::time_point last_sent;
    int notifications_sent_this_hour = 0;
    std::chrono::system_clock::time_point hour_start;
};

using NotificationHandler = std::function<void(const NotificationMessage&)>;

class ExchangeNotificationSystem {
public:
    ExchangeNotificationSystem();
    ~ExchangeNotificationSystem();
    
    void start();
    void stop();
    
    void add_notification_handler(NotificationChannel channel, NotificationHandler handler);
    void remove_notification_handler(NotificationChannel channel);
    
    void add_notification_rule(const NotificationRule& rule);
    void remove_notification_rule(const std::string& rule_id);
    void enable_rule(const std::string& rule_id);
    void disable_rule(const std::string& rule_id);
    
    void send_notification(const NotificationMessage& message);
    void send_notification(NotificationLevel level,
                          const std::string& title,
                          const std::string& message,
                          const std::string& exchange_id = "");
    
    std::vector<NotificationMessage> get_recent_notifications(
        std::chrono::minutes lookback = std::chrono::minutes(60)) const;
    
    std::vector<NotificationMessage> get_unacknowledged_notifications() const;
    
    void acknowledge_notification(const std::string& notification_id);
    void acknowledge_all_notifications();
    
    void clear_old_notifications(std::chrono::hours max_age = std::chrono::hours(24));
    
    struct NotificationStats {
        std::atomic<uint64_t> total_notifications{0};
        std::atomic<uint64_t> info_notifications{0};
        std::atomic<uint64_t> warning_notifications{0};
        std::atomic<uint64_t> error_notifications{0};
        std::atomic<uint64_t> critical_notifications{0};
        std::atomic<uint64_t> acknowledged_notifications{0};
        
        std::unordered_map<NotificationChannel, uint64_t> channel_stats;
        
        // Custom copy constructor to handle atomics
        NotificationStats() = default;
        NotificationStats(const NotificationStats& other) 
            : total_notifications(other.total_notifications.load())
            , info_notifications(other.info_notifications.load())
            , warning_notifications(other.warning_notifications.load())
            , error_notifications(other.error_notifications.load())
            , critical_notifications(other.critical_notifications.load())
            , acknowledged_notifications(other.acknowledged_notifications.load())
            , channel_stats(other.channel_stats) {}
            
        NotificationStats& operator=(const NotificationStats& other) {
            if (this != &other) {
                total_notifications.store(other.total_notifications.load());
                info_notifications.store(other.info_notifications.load());
                warning_notifications.store(other.warning_notifications.load());
                error_notifications.store(other.error_notifications.load());
                critical_notifications.store(other.critical_notifications.load());
                acknowledged_notifications.store(other.acknowledged_notifications.load());
                channel_stats = other.channel_stats;
            }
            return *this;
        }
    };
    
    NotificationStats get_stats() const;
    void reset_stats();
    
    template<typename ExchangeInterface>
    void integrate_with_failover_manager(FailoverManager<ExchangeInterface>* manager);
    
    template<typename ExchangeInterface>
    void integrate_with_resilient_adapter(ResilientExchangeAdapter<ExchangeInterface>* adapter);
    
private:
    std::unordered_map<NotificationChannel, NotificationHandler> handlers_;
    std::vector<NotificationRule> rules_;
    std::vector<NotificationMessage> notification_history_;
    
    mutable std::shared_mutex handlers_mutex_;
    mutable std::shared_mutex rules_mutex_;
    mutable std::shared_mutex history_mutex_;
    
    std::atomic<bool> running_{false};
    std::unique_ptr<std::thread> cleanup_thread_;
    
    NotificationStats stats_;
    
    void cleanup_loop();
    bool should_send_notification(const NotificationRule& rule, const NotificationMessage& message);
    void update_rule_throttle(NotificationRule& rule);
    void process_notification_rules(const NotificationMessage& message);
    std::string generate_notification_id() const;
    
    // Built-in notification handlers
    void log_handler(const NotificationMessage& message);
    void console_handler(const NotificationMessage& message);
};

class SlackNotificationHandler {
public:
    SlackNotificationHandler(const std::string& webhook_url);
    void operator()(const NotificationMessage& message);
    
private:
    std::string webhook_url_;
    void send_to_slack(const NotificationMessage& message);
};

class EmailNotificationHandler {
public:
    EmailNotificationHandler(const std::string& smtp_server, 
                           const std::string& username,
                           const std::string& password,
                           const std::vector<std::string>& recipients);
    void operator()(const NotificationMessage& message);
    
private:
    std::string smtp_server_;
    std::string username_;
    std::string password_;
    std::vector<std::string> recipients_;
    
    void send_email(const NotificationMessage& message);
};

class WebhookNotificationHandler {
public:
    WebhookNotificationHandler(const std::string& webhook_url,
                             const std::unordered_map<std::string, std::string>& headers = {});
    void operator()(const NotificationMessage& message);
    
private:
    std::string webhook_url_;
    std::unordered_map<std::string, std::string> headers_;
    
    void send_webhook(const NotificationMessage& message);
};

class DatabaseNotificationHandler {
public:
    DatabaseNotificationHandler(const std::string& connection_string);
    void operator()(const NotificationMessage& message);
    
private:
    std::string connection_string_;
    void save_to_database(const NotificationMessage& message);
};

// Predefined notification rules
namespace notification_rules {

NotificationRule exchange_failover_rule();
NotificationRule exchange_health_degraded_rule();
NotificationRule exchange_disconnected_rule();
NotificationRule high_error_rate_rule();
NotificationRule circuit_breaker_opened_rule();
NotificationRule api_rate_limit_rule();

} // namespace notification_rules

// Helper functions for creating common notification scenarios
namespace notification_helpers {

void setup_basic_notifications(ExchangeNotificationSystem& system,
                              const std::string& log_file_path = "");

void setup_slack_notifications(ExchangeNotificationSystem& system,
                              const std::string& webhook_url);

void setup_email_notifications(ExchangeNotificationSystem& system,
                              const std::string& smtp_server,
                              const std::string& username,
                              const std::string& password,
                              const std::vector<std::string>& recipients);

template<typename ExchangeInterface>
void setup_comprehensive_monitoring(ExchangeNotificationSystem& system,
                                  FailoverManager<ExchangeInterface>* failover_manager,
                                  ResilientExchangeAdapter<ExchangeInterface>* resilient_adapter);

} // namespace notification_helpers

} // namespace exchange
} // namespace ats