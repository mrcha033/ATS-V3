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

enum class EmailPriority {
    LOW,
    NORMAL,
    HIGH,
    URGENT
};

enum class EmailFormat {
    PLAIN_TEXT,
    HTML,
    MULTIPART
};

struct EmailConfig {
    std::string smtp_server;
    int smtp_port = 587;
    std::string username;
    std::string password;
    std::string from_email;
    std::string from_name;
    bool use_tls = true;
    bool use_ssl = false;
    int connection_timeout = 30;
    int retry_attempts = 3;
    std::chrono::seconds retry_delay{5};
};

struct EmailRecipient {
    std::string email;
    std::string name;
    bool enabled = true;
    std::vector<exchange::NotificationLevel> subscribed_levels;
    std::unordered_map<std::string, bool> category_preferences; // "risk", "trade", "system"
    
    EmailRecipient(const std::string& email_addr) : email(email_addr) {
        // Default subscription to all levels
        subscribed_levels = {
            exchange::NotificationLevel::INFO,
            exchange::NotificationLevel::WARNING,
            exchange::NotificationLevel::ERROR,
            exchange::NotificationLevel::CRITICAL
        };
        
        // Default category preferences
        category_preferences["risk"] = true;
        category_preferences["trade"] = true;
        category_preferences["system"] = true;
    }
};

struct EmailTemplate {
    std::string template_id;
    std::string subject_template;
    std::string body_template_html;
    std::string body_template_text;
    EmailFormat format = EmailFormat::MULTIPART;
    std::vector<std::string> required_variables;
    
    std::string render_subject(const std::unordered_map<std::string, std::string>& variables) const;
    std::string render_body_html(const std::unordered_map<std::string, std::string>& variables) const;
    std::string render_body_text(const std::unordered_map<std::string, std::string>& variables) const;
};

struct EmailMessage {
    std::string to_email;
    std::string to_name;
    std::string subject;
    std::string body_html;
    std::string body_text;
    EmailFormat format = EmailFormat::MULTIPART;
    EmailPriority priority = EmailPriority::NORMAL;
    std::vector<std::string> attachments;
    std::unordered_map<std::string, std::string> headers;
    
    EmailMessage() = default;
    EmailMessage(const std::string& to, const std::string& subj, const std::string& body)
        : to_email(to), subject(subj), body_text(body), format(EmailFormat::PLAIN_TEXT) {}
};

struct EmailDeliveryHistory {
    std::string email_id;
    std::string recipient_email;
    std::string subject;
    exchange::NotificationLevel level;
    std::string category;
    bool delivered = false;
    std::chrono::system_clock::time_point sent_at;
    std::chrono::system_clock::time_point delivered_at;
    std::string smtp_response;
    std::string error_message;
    int retry_count = 0;
    
    EmailDeliveryHistory() : sent_at(std::chrono::system_clock::now()) {}
};

class EmailNotificationService {
public:
    explicit EmailNotificationService(const EmailConfig& config);
    ~EmailNotificationService();
    
    bool initialize();
    void shutdown();
    
    // Recipient management
    bool add_recipient(const EmailRecipient& recipient);
    bool remove_recipient(const std::string& email);
    bool update_recipient_preferences(const std::string& email, 
                                    const std::vector<exchange::NotificationLevel>& levels,
                                    const std::unordered_map<std::string, bool>& categories);
    std::vector<EmailRecipient> get_recipients() const;
    EmailRecipient* get_recipient(const std::string& email);
    
    // Template management
    bool add_email_template(const EmailTemplate& template_obj);
    bool remove_email_template(const std::string& template_id);
    EmailTemplate* get_email_template(const std::string& template_id);
    std::vector<EmailTemplate> get_all_templates() const;
    
    // Email sending
    bool send_email(const EmailMessage& message);
    bool send_templated_email(const std::string& template_id,
                             const std::string& recipient_email,
                             const std::unordered_map<std::string, std::string>& variables,
                             exchange::NotificationLevel level = exchange::NotificationLevel::INFO,
                             const std::string& category = "general");
    
    bool send_notification_email(const exchange::NotificationMessage& notification_msg,
                               const std::string& category = "general");
    
    // Bulk operations
    bool send_broadcast_email(const EmailMessage& message,
                            exchange::NotificationLevel level = exchange::NotificationLevel::INFO,
                            const std::string& category = "general");
    
    bool send_templated_broadcast(const std::string& template_id,
                                const std::unordered_map<std::string, std::string>& variables,
                                exchange::NotificationLevel level = exchange::NotificationLevel::INFO,
                                const std::string& category = "general");
    
    // Integration with ExchangeNotificationSystem
    exchange::NotificationHandler create_email_notification_handler(const std::string& category = "general");
    
    // Statistics and monitoring
    struct EmailNotificationStats {
        std::atomic<uint64_t> total_sent{0};
        std::atomic<uint64_t> total_delivered{0};
        std::atomic<uint64_t> total_failed{0};
        std::atomic<uint64_t> total_retries{0};
        std::atomic<uint64_t> active_recipients{0};
        
        std::unordered_map<std::string, uint64_t> category_stats;
        std::unordered_map<EmailPriority, uint64_t> priority_stats;
    };
    
    EmailNotificationStats get_stats() const;
    void reset_stats();
    
    // History management
    std::vector<EmailDeliveryHistory> get_delivery_history(
        const std::string& recipient_email = "",
        std::chrono::hours lookback = std::chrono::hours(24)) const;
    
    void clear_old_history(std::chrono::hours max_age = std::chrono::hours(168)); // 7 days
    
    // Health check
    bool test_smtp_connection();
    
private:
    EmailConfig config_;
    std::vector<EmailRecipient> recipients_;
    std::vector<EmailTemplate> email_templates_;
    std::vector<EmailDeliveryHistory> delivery_history_;
    std::shared_ptr<utils::InfluxDBClient> influxdb_client_;
    
    mutable std::shared_mutex recipients_mutex_;
    mutable std::shared_mutex templates_mutex_;
    mutable std::shared_mutex history_mutex_;
    
    EmailNotificationStats stats_;
    std::atomic<bool> initialized_{false};
    
    // SMTP operations
    bool send_smtp_email(const EmailMessage& message, EmailDeliveryHistory& history);
    std::string format_email_message(const EmailMessage& message) const;
    std::string encode_base64(const std::string& input) const;
    
    void store_delivery_history(const EmailDeliveryHistory& history);
    void store_email_in_influxdb(const EmailDeliveryHistory& history);
    
    std::string generate_email_id() const;
    std::string get_current_timestamp() const;
    
    // Template rendering helpers
    std::string replace_template_variables(const std::string& template_str,
                                         const std::unordered_map<std::string, std::string>& variables) const;
    
    // Recipient filtering
    std::vector<EmailRecipient> filter_recipients_for_notification(
        exchange::NotificationLevel level,
        const std::string& category) const;
    
    // CURL helper for SMTP
    static size_t read_callback(void* ptr, size_t size, size_t nmemb, void* userp);
    static size_t write_callback(void* contents, size_t size, size_t nmemb, std::string* userp);
    
    // Default templates setup
    void setup_default_templates();
};

// Pre-defined email templates for common notifications
namespace email_templates {

EmailTemplate create_risk_alert_template();
EmailTemplate create_trade_failure_template();
EmailTemplate create_system_health_template();
EmailTemplate create_daily_summary_template();
EmailTemplate create_performance_report_template();

} // namespace email_templates

// Email formatting helpers
namespace email_helpers {

std::string format_currency(double amount, const std::string& currency = "USD");
std::string format_percentage(double percentage);
std::string format_timestamp(const std::chrono::system_clock::time_point& timestamp);
std::string create_html_table(const std::vector<std::vector<std::string>>& data,
                             const std::vector<std::string>& headers = {});

} // namespace email_helpers

} // namespace notification
} // namespace ats