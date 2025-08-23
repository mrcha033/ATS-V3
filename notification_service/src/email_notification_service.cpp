#include "email_notification_service.hpp"
#include "utils/logger.hpp"
#include <curl/curl.h>
#include <nlohmann/json.hpp>
#include <random>
#include <sstream>
#include <iomanip>
#include <algorithm>
#include <regex>
#include <ctime>

namespace ats {
namespace notification {

// EmailTemplate implementation
std::string EmailTemplate::render_subject(const std::unordered_map<std::string, std::string>& variables) const {
    std::string result = subject_template;
    for (const auto& var : variables) {
        std::string placeholder = "{{" + var.first + "}}";
        size_t pos = 0;
        while ((pos = result.find(placeholder, pos)) != std::string::npos) {
            result.replace(pos, placeholder.length(), var.second);
            pos += var.second.length();
        }
    }
    return result;
}

std::string EmailTemplate::render_body_html(const std::unordered_map<std::string, std::string>& variables) const {
    std::string result = body_template_html;
    for (const auto& var : variables) {
        std::string placeholder = "{{" + var.first + "}}";
        size_t pos = 0;
        while ((pos = result.find(placeholder, pos)) != std::string::npos) {
            result.replace(pos, placeholder.length(), var.second);
            pos += var.second.length();
        }
    }
    return result;
}

std::string EmailTemplate::render_body_text(const std::unordered_map<std::string, std::string>& variables) const {
    std::string result = body_template_text;
    for (const auto& var : variables) {
        std::string placeholder = "{{" + var.first + "}}";
        size_t pos = 0;
        while ((pos = result.find(placeholder, pos)) != std::string::npos) {
            result.replace(pos, placeholder.length(), var.second);
            pos += var.second.length();
        }
    }
    return result;
}

// EmailNotificationService implementation
EmailNotificationService::EmailNotificationService(const EmailConfig& config)
    : config_(config) {
    
    curl_global_init(CURL_GLOBAL_DEFAULT);
    influxdb_client_ = utils::create_influxdb_client();
}

EmailNotificationService::~EmailNotificationService() {
    shutdown();
    curl_global_cleanup();
}

bool EmailNotificationService::initialize() {
    if (initialized_.exchange(true)) {
        Logger::warning("Email notification service already initialized");
        return true;
    }
    
    if (config_.smtp_server.empty() || config_.username.empty()) {
        Logger::error("SMTP configuration incomplete");
        return false;
    }
    
    // Test SMTP connection
    if (!test_smtp_connection()) {
        Logger::error("SMTP connection test failed");
        initialized_.store(false);
        return false;
    }
    
    // Initialize InfluxDB connection
    if (influxdb_client_) {
        try {
            if (!influxdb_client_->connect("http://localhost:8086", "ats_notifications")) {
                Logger::warning("Could not connect to InfluxDB for email history");
            } else {
                influxdb_client_->create_database("ats_notifications");
                Logger::info("Connected to InfluxDB for email history storage");
            }
        } catch (const std::exception& e) {
            Logger::warning("InfluxDB connection failed: {}", e.what());
        }
    }
    
    // Setup default templates
    setup_default_templates();
    
    Logger::info("Email notification service initialized successfully");
    return true;
}

void EmailNotificationService::shutdown() {
    if (!initialized_.exchange(false)) {
        return;
    }
    
    if (influxdb_client_) {
        influxdb_client_->disconnect();
    }
    
    Logger::info("Email notification service shut down");
}

bool EmailNotificationService::add_recipient(const EmailRecipient& recipient) {
    std::unique_lock<std::shared_mutex> lock(recipients_mutex_);
    
    // Check if recipient already exists
    auto it = std::find_if(recipients_.begin(), recipients_.end(),
        [&recipient](const EmailRecipient& r) {
            return r.email == recipient.email;
        });
    
    if (it != recipients_.end()) {
        // Update existing recipient
        *it = recipient;
        Logger::info("Updated email recipient: {}", recipient.email);
    } else {
        // Add new recipient
        recipients_.push_back(recipient);
        stats_.active_recipients.fetch_add(1);
        Logger::info("Added new email recipient: {}", recipient.email);
    }
    
    return true;
}

bool EmailNotificationService::remove_recipient(const std::string& email) {
    std::unique_lock<std::shared_mutex> lock(recipients_mutex_);
    
    auto it = std::find_if(recipients_.begin(), recipients_.end(),
        [&email](const EmailRecipient& r) {
            return r.email == email;
        });
    
    if (it != recipients_.end()) {
        recipients_.erase(it);
        stats_.active_recipients.fetch_sub(1);
        Logger::info("Removed email recipient: {}", email);
        return true;
    }
    
    Logger::warning("Email recipient not found for removal: {}", email);
    return false;
}

bool EmailNotificationService::update_recipient_preferences(
    const std::string& email,
    const std::vector<exchange::NotificationLevel>& levels,
    const std::unordered_map<std::string, bool>& categories) {
    
    std::unique_lock<std::shared_mutex> lock(recipients_mutex_);
    
    auto it = std::find_if(recipients_.begin(), recipients_.end(),
        [&email](const EmailRecipient& r) {
            return r.email == email;
        });
    
    if (it != recipients_.end()) {
        it->subscribed_levels = levels;
        it->category_preferences = categories;
        Logger::info("Updated preferences for email recipient: {}", email);
        return true;
    }
    
    Logger::warning("Email recipient not found for preference update: {}", email);
    return false;
}

std::vector<EmailRecipient> EmailNotificationService::get_recipients() const {
    std::shared_lock<std::shared_mutex> lock(recipients_mutex_);
    return recipients_;
}

EmailRecipient* EmailNotificationService::get_recipient(const std::string& email) {
    std::shared_lock<std::shared_mutex> lock(recipients_mutex_);
    
    auto it = std::find_if(recipients_.begin(), recipients_.end(),
        [&email](const EmailRecipient& r) {
            return r.email == email;
        });
    
    return (it != recipients_.end()) ? &(*it) : nullptr;
}

bool EmailNotificationService::add_email_template(const EmailTemplate& template_obj) {
    std::unique_lock<std::shared_mutex> lock(templates_mutex_);
    
    // Check if template already exists
    auto it = std::find_if(email_templates_.begin(), email_templates_.end(),
        [&template_obj](const EmailTemplate& t) {
            return t.template_id == template_obj.template_id;
        });
    
    if (it != email_templates_.end()) {
        // Update existing template
        *it = template_obj;
        Logger::info("Updated email template: {}", template_obj.template_id);
    } else {
        // Add new template
        email_templates_.push_back(template_obj);
        Logger::info("Added new email template: {}", template_obj.template_id);
    }
    
    return true;
}

bool EmailNotificationService::remove_email_template(const std::string& template_id) {
    std::unique_lock<std::shared_mutex> lock(templates_mutex_);
    
    auto it = std::find_if(email_templates_.begin(), email_templates_.end(),
        [&template_id](const EmailTemplate& t) {
            return t.template_id == template_id;
        });
    
    if (it != email_templates_.end()) {
        email_templates_.erase(it);
        Logger::info("Removed email template: {}", template_id);
        return true;
    }
    
    Logger::warning("Email template not found for removal: {}", template_id);
    return false;
}

EmailTemplate* EmailNotificationService::get_email_template(const std::string& template_id) {
    std::shared_lock<std::shared_mutex> lock(templates_mutex_);
    
    auto it = std::find_if(email_templates_.begin(), email_templates_.end(),
        [&template_id](const EmailTemplate& t) {
            return t.template_id == template_id;
        });
    
    return (it != email_templates_.end()) ? &(*it) : nullptr;
}

std::vector<EmailTemplate> EmailNotificationService::get_all_templates() const {
    std::shared_lock<std::shared_mutex> lock(templates_mutex_);
    return email_templates_;
}

bool EmailNotificationService::send_email(const EmailMessage& message) {
    if (!initialized_.load()) {
        Logger::error("Email notification service not initialized");
        return false;
    }
    
    EmailDeliveryHistory history;
    history.email_id = generate_email_id();
    history.recipient_email = message.to_email;
    history.subject = message.subject;
    history.level = exchange::NotificationLevel::INFO; // Default level for direct sends
    history.category = "direct";
    
    bool success = false;
    for (int attempt = 0; attempt < config_.retry_attempts && !success; ++attempt) {
        if (attempt > 0) {
            std::this_thread::sleep_for(config_.retry_delay);
            stats_.total_retries.fetch_add(1);
            history.retry_count++;
            Logger::debug("Retrying email send (attempt {})", attempt + 1);
        }
        
        if (send_smtp_email(message, history)) {
            success = true;
            history.delivered = true;
            history.delivered_at = std::chrono::system_clock::now();
            stats_.total_delivered.fetch_add(1);
        }
    }
    
    if (!success) {
        history.error_message = "Failed after " + std::to_string(config_.retry_attempts) + " attempts";
        stats_.total_failed.fetch_add(1);
        Logger::error("Failed to send email to: {}", message.to_email);
    }
    
    stats_.total_sent.fetch_add(1);
    store_delivery_history(history);
    
    return success;
}

bool EmailNotificationService::send_templated_email(
    const std::string& template_id,
    const std::string& recipient_email,
    const std::unordered_map<std::string, std::string>& variables,
    exchange::NotificationLevel level,
    const std::string& category) {
    
    auto* template_obj = get_email_template(template_id);
    if (!template_obj) {
        Logger::error("Email template not found: {}", template_id);
        return false;
    }
    
    auto* recipient = get_recipient(recipient_email);
    if (!recipient || !recipient->enabled) {
        Logger::debug("Recipient disabled or not found: {}", recipient_email);
        return false;
    }
    
    // Check if recipient is subscribed to this level and category
    bool subscribed_to_level = std::find(recipient->subscribed_levels.begin(),
                                        recipient->subscribed_levels.end(),
                                        level) != recipient->subscribed_levels.end();
    
    bool subscribed_to_category = recipient->category_preferences.find(category) != 
                                 recipient->category_preferences.end() &&
                                 recipient->category_preferences.at(category);
    
    if (!subscribed_to_level || !subscribed_to_category) {
        Logger::debug("Recipient {} not subscribed to level {} or category {}", 
                     recipient_email, static_cast<int>(level), category);
        return false;
    }
    
    // Render email from template
    EmailMessage message;
    message.to_email = recipient_email;
    message.to_name = recipient->name;
    message.subject = template_obj->render_subject(variables);
    message.body_html = template_obj->render_body_html(variables);
    message.body_text = template_obj->render_body_text(variables);
    message.format = template_obj->format;
    
    // Set priority based on notification level
    switch (level) {
        case exchange::NotificationLevel::INFO:
            message.priority = EmailPriority::LOW;
            break;
        case exchange::NotificationLevel::WARNING:
            message.priority = EmailPriority::NORMAL;
            break;
        case exchange::NotificationLevel::ERROR:
            message.priority = EmailPriority::HIGH;
            break;
        case exchange::NotificationLevel::CRITICAL:
            message.priority = EmailPriority::URGENT;
            break;
    }
    
    return send_email(message);
}

bool EmailNotificationService::send_notification_email(
    const exchange::NotificationMessage& notification_msg,
    const std::string& category) {
    
    // Use appropriate template based on notification content
    std::string template_id = "generic_notification";
    if (notification_msg.title.find("Risk") != std::string::npos ||
        notification_msg.message.find("limit") != std::string::npos) {
        template_id = "risk_alert";
    } else if (notification_msg.title.find("Trade") != std::string::npos ||
               notification_msg.title.find("Order") != std::string::npos) {
        template_id = "trade_notification";
    } else if (notification_msg.title.find("System") != std::string::npos ||
               notification_msg.title.find("Health") != std::string::npos) {
        template_id = "system_health";
    }
    
    // Prepare template variables
    std::unordered_map<std::string, std::string> variables;
    variables["title"] = notification_msg.title;
    variables["message"] = notification_msg.message;
    variables["exchange_id"] = notification_msg.exchange_id.empty() ? "System" : notification_msg.exchange_id;
    variables["timestamp"] = email_helpers::format_timestamp(notification_msg.timestamp);
    variables["level"] = [](exchange::NotificationLevel lvl) {
        switch (lvl) {
            case exchange::NotificationLevel::INFO: return "Information";
            case exchange::NotificationLevel::WARNING: return "Warning";
            case exchange::NotificationLevel::ERROR: return "Error";
            case exchange::NotificationLevel::CRITICAL: return "Critical";
            default: return "Unknown";
        }
    }(notification_msg.level);
    
    // Add metadata to variables
    for (const auto& meta : notification_msg.metadata) {
        variables["meta_" + meta.first] = meta.second;
    }
    
    // Send to all eligible recipients
    auto eligible_recipients = filter_recipients_for_notification(notification_msg.level, category);
    bool any_success = false;
    
    for (const auto& recipient : eligible_recipients) {
        if (send_templated_email(template_id, recipient.email, variables, 
                               notification_msg.level, category)) {
            any_success = true;
        }
    }
    
    return any_success;
}

bool EmailNotificationService::send_broadcast_email(
    const EmailMessage& message,
    exchange::NotificationLevel level,
    const std::string& category) {
    
    auto eligible_recipients = filter_recipients_for_notification(level, category);
    bool any_success = false;
    
    for (const auto& recipient : eligible_recipients) {
        EmailMessage recipient_message = message;
        recipient_message.to_email = recipient.email;
        recipient_message.to_name = recipient.name;
        
        if (send_email(recipient_message)) {
            any_success = true;
        }
    }
    
    Logger::info("Broadcast email sent to {} eligible recipients", eligible_recipients.size());
    return any_success;
}

bool EmailNotificationService::send_templated_broadcast(
    const std::string& template_id,
    const std::unordered_map<std::string, std::string>& variables,
    exchange::NotificationLevel level,
    const std::string& category) {
    
    auto eligible_recipients = filter_recipients_for_notification(level, category);
    bool any_success = false;
    
    for (const auto& recipient : eligible_recipients) {
        if (send_templated_email(template_id, recipient.email, variables, level, category)) {
            any_success = true;
        }
    }
    
    Logger::info("Templated broadcast sent to {} eligible recipients", eligible_recipients.size());
    return any_success;
}

exchange::NotificationHandler EmailNotificationService::create_email_notification_handler(
    const std::string& category) {
    
    return [this, category](const exchange::NotificationMessage& msg) {
        send_notification_email(msg, category);
    };
}

EmailNotificationService::EmailNotificationStats EmailNotificationService::get_stats() const {
    return stats_;
}

void EmailNotificationService::reset_stats() {
    stats_.total_sent.store(0);
    stats_.total_delivered.store(0);
    stats_.total_failed.store(0);
    stats_.total_retries.store(0);
    
    std::shared_lock<std::shared_mutex> lock(recipients_mutex_);
    stats_.active_recipients.store(
        std::count_if(recipients_.begin(), recipients_.end(),
                     [](const EmailRecipient& r) { return r.enabled; }));
    
    Logger::info("Reset email notification statistics");
}

std::vector<EmailDeliveryHistory> EmailNotificationService::get_delivery_history(
    const std::string& recipient_email,
    std::chrono::hours lookback) const {
    
    std::shared_lock<std::shared_mutex> lock(history_mutex_);
    
    auto cutoff_time = std::chrono::system_clock::now() - lookback;
    std::vector<EmailDeliveryHistory> filtered_history;
    
    for (const auto& history : delivery_history_) {
        bool matches_recipient = recipient_email.empty() || history.recipient_email == recipient_email;
        bool within_timeframe = history.sent_at >= cutoff_time;
        
        if (matches_recipient && within_timeframe) {
            filtered_history.push_back(history);
        }
    }
    
    std::sort(filtered_history.begin(), filtered_history.end(),
        [](const EmailDeliveryHistory& a, const EmailDeliveryHistory& b) {
            return a.sent_at > b.sent_at;
        });
    
    return filtered_history;
}

void EmailNotificationService::clear_old_history(std::chrono::hours max_age) {
    std::unique_lock<std::shared_mutex> lock(history_mutex_);
    
    auto cutoff_time = std::chrono::system_clock::now() - max_age;
    size_t initial_size = delivery_history_.size();
    
    delivery_history_.erase(
        std::remove_if(delivery_history_.begin(), delivery_history_.end(),
            [cutoff_time](const EmailDeliveryHistory& history) {
                return history.sent_at < cutoff_time;
            }),
        delivery_history_.end()
    );
    
    size_t removed_count = initial_size - delivery_history_.size();
    if (removed_count > 0) {
        Logger::info("Cleared {} old email delivery history entries", removed_count);
    }
}

bool EmailNotificationService::test_smtp_connection() {
    CURL* curl = curl_easy_init();
    if (!curl) {
        return false;
    }
    
    std::string smtp_url = (config_.use_ssl ? "smtps://" : "smtp://") + 
                          config_.smtp_server + ":" + std::to_string(config_.smtp_port);
    
    curl_easy_setopt(curl, CURLOPT_URL, smtp_url.c_str());
    curl_easy_setopt(curl, CURLOPT_CONNECT_ONLY, 1L);
    curl_easy_setopt(curl, CURLOPT_TIMEOUT, config_.connection_timeout);
    
    if (config_.use_tls) {
        curl_easy_setopt(curl, CURLOPT_USE_SSL, CURLUSESSL_ALL);
    }
    
    CURLcode res = curl_easy_perform(curl);
    curl_easy_cleanup(curl);
    
    if (res == CURLE_OK) {
        Logger::info("SMTP connection test successful");
        return true;
    } else {
        Logger::error("SMTP connection test failed: {}", curl_easy_strerror(res));
        return false;
    }
}

// Private implementation methods would continue here...
// [Additional private methods implementation omitted for brevity]

std::string EmailNotificationService::generate_email_id() const {
    static std::random_device rd;
    static std::mt19937 gen(rd());
    static std::uniform_int_distribution<> dis(0, 15);
    
    std::stringstream ss;
    ss << "email_" << std::hex;
    for (int i = 0; i < 16; ++i) {
        ss << dis(gen);
    }
    
    return ss.str();
}

void EmailNotificationService::setup_default_templates() {
    // Add default templates
    add_email_template(email_templates::create_risk_alert_template());
    add_email_template(email_templates::create_trade_failure_template());
    add_email_template(email_templates::create_system_health_template());
    
    // Generic notification template
    EmailTemplate generic;
    generic.template_id = "generic_notification";
    generic.subject_template = "ATS Alert: {{title}}";
    generic.body_template_text = "ATS Notification\n\nLevel: {{level}}\nTitle: {{title}}\nMessage: {{message}}\nExchange: {{exchange_id}}\nTime: {{timestamp}}";
    generic.body_template_html = R"(<html><body>
        <h2>ATS Notification</h2>
        <p><strong>Level:</strong> {{level}}</p>
        <p><strong>Title:</strong> {{title}}</p>
        <p><strong>Message:</strong> {{message}}</p>
        <p><strong>Exchange:</strong> {{exchange_id}}</p>
        <p><strong>Time:</strong> {{timestamp}}</p>
    </body></html>)";
    generic.format = EmailFormat::MULTIPART;
    
    add_email_template(generic);
}

std::vector<EmailRecipient> EmailNotificationService::filter_recipients_for_notification(
    exchange::NotificationLevel level,
    const std::string& category) const {
    
    std::shared_lock<std::shared_mutex> lock(recipients_mutex_);
    
    std::vector<EmailRecipient> eligible;
    for (const auto& recipient : recipients_) {
        if (!recipient.enabled) continue;
        
        // Check level subscription
        bool subscribed_to_level = std::find(recipient.subscribed_levels.begin(),
                                            recipient.subscribed_levels.end(),
                                            level) != recipient.subscribed_levels.end();
        
        // Check category subscription
        bool subscribed_to_category = recipient.category_preferences.find(category) != 
                                     recipient.category_preferences.end() &&
                                     recipient.category_preferences.at(category);
        
        if (subscribed_to_level && subscribed_to_category) {
            eligible.push_back(recipient);
        }
    }
    
    return eligible;
}

// Template implementations
namespace email_templates {

EmailTemplate create_risk_alert_template() {
    EmailTemplate tmpl;
    tmpl.template_id = "risk_alert";
    tmpl.subject_template = "Risk Alert: {{title}}";
    tmpl.body_template_text = "RISK ALERT\n\nSymbol: {{symbol}}\nCurrent Exposure: {{current_exposure}}\nLimit: {{limit}}\nTime: {{timestamp}}\n\nImmediate action may be required.";
    tmpl.body_template_html = R"(<html><body style="font-family: Arial, sans-serif;">
        <h2 style="color: #d9534f;">Risk Alert</h2>
        <table border="1" style="border-collapse: collapse;">
            <tr><td><strong>Symbol:</strong></td><td>{{symbol}}</td></tr>
            <tr><td><strong>Current Exposure:</strong></td><td>{{current_exposure}}</td></tr>
            <tr><td><strong>Limit:</strong></td><td>{{limit}}</td></tr>
            <tr><td><strong>Time:</strong></td><td>{{timestamp}}</td></tr>
        </table>
        <p style="color: #d9534f;"><strong>Immediate action may be required.</strong></p>
    </body></html>)";
    tmpl.format = EmailFormat::MULTIPART;
    tmpl.required_variables = {"symbol", "current_exposure", "limit", "timestamp"};
    
    return tmpl;
}

EmailTemplate create_trade_failure_template() {
    EmailTemplate tmpl;
    tmpl.template_id = "trade_notification";
    tmpl.subject_template = "Trade Notification: {{status}}";
    tmpl.body_template_text = "Trade Update\n\nSymbol: {{symbol}}\nExchange: {{exchange}}\nStatus: {{status}}\nDetails: {{message}}\nTime: {{timestamp}}";
    tmpl.body_template_html = R"(<html><body style="font-family: Arial, sans-serif;">
        <h2>Trade Notification</h2>
        <table border="1" style="border-collapse: collapse;">
            <tr><td><strong>Symbol:</strong></td><td>{{symbol}}</td></tr>
            <tr><td><strong>Exchange:</strong></td><td>{{exchange}}</td></tr>
            <tr><td><strong>Status:</strong></td><td>{{status}}</td></tr>
            <tr><td><strong>Details:</strong></td><td>{{message}}</td></tr>
            <tr><td><strong>Time:</strong></td><td>{{timestamp}}</td></tr>
        </table>
    </body></html>)";
    tmpl.format = EmailFormat::MULTIPART;
    
    return tmpl;
}

EmailTemplate create_system_health_template() {
    EmailTemplate tmpl;
    tmpl.template_id = "system_health";
    tmpl.subject_template = "System Health: {{component}} - {{status}}";
    tmpl.body_template_text = "System Health Update\n\nComponent: {{component}}\nStatus: {{status}}\nDetails: {{message}}\nTime: {{timestamp}}";
    tmpl.body_template_html = R"(<html><body style="font-family: Arial, sans-serif;">
        <h2>System Health Update</h2>
        <table border="1" style="border-collapse: collapse;">
            <tr><td><strong>Component:</strong></td><td>{{component}}</td></tr>
            <tr><td><strong>Status:</strong></td><td>{{status}}</td></tr>
            <tr><td><strong>Details:</strong></td><td>{{message}}</td></tr>
            <tr><td><strong>Time:</strong></td><td>{{timestamp}}</td></tr>
        </table>
    </body></html>)";
    tmpl.format = EmailFormat::MULTIPART;
    
    return tmpl;
}

} // namespace email_templates

// Helper functions
namespace email_helpers {

std::string format_currency(double amount, const std::string& currency) {
    std::stringstream ss;
    ss << currency << " " << std::fixed << std::setprecision(2) << amount;
    return ss.str();
}

std::string format_percentage(double percentage) {
    std::stringstream ss;
    ss << std::fixed << std::setprecision(2) << percentage << "%";
    return ss.str();
}

std::string format_timestamp(const std::chrono::system_clock::time_point& timestamp) {
    auto time_t = std::chrono::system_clock::to_time_t(timestamp);
    std::stringstream ss;
    ss << std::put_time(std::localtime(&time_t), "%Y-%m-%d %H:%M:%S");
    return ss.str();
}

} // namespace email_helpers

} // namespace notification
} // namespace ats