#include "exchange/exchange_notification_system.hpp"
#include "utils/logger.hpp"
#ifdef HAS_NLOHMANN_JSON
#include <nlohmann/json.hpp>
#endif
#ifdef HAS_CURL
#include <curl/curl.h>
#endif
#include <thread>
#include <random>
#include <sstream>
#include <iomanip>

namespace ats {
namespace exchange {

// Import Logger from utils namespace
using ats::utils::Logger;

// NotificationMessage implementation
std::string NotificationMessage::to_json() const {
#ifdef HAS_NLOHMANN_JSON
    nlohmann::json j;
    j["id"] = id;
    j["level"] = static_cast<int>(level);
    j["title"] = title;
    j["message"] = message;
    j["exchange_id"] = exchange_id;
    j["timestamp"] = std::chrono::duration_cast<std::chrono::milliseconds>(
        timestamp.time_since_epoch()).count();
    j["acknowledged"] = acknowledged;
    j["metadata"] = metadata;
    
    return j.dump();
#else
    // Fallback JSON implementation
    std::stringstream ss;
    ss << "{\"id\":\"" << id << "\",";
    ss << "\"level\":" << static_cast<int>(level) << ",";
    ss << "\"title\":\"" << title << "\",";
    ss << "\"message\":\"" << message << "\",";
    ss << "\"exchange_id\":\"" << exchange_id << "\",";
    ss << "\"timestamp\":" << std::chrono::duration_cast<std::chrono::milliseconds>(
        timestamp.time_since_epoch()).count() << ",";
    ss << "\"acknowledged\":" << (acknowledged ? "true" : "false") << "}";
    return ss.str();
#endif
}

NotificationMessage NotificationMessage::from_json(const std::string& json_str) {
#ifdef HAS_NLOHMANN_JSON
    nlohmann::json j = nlohmann::json::parse(json_str);
    
    NotificationMessage msg;
    msg.id = j["id"];
    msg.level = static_cast<NotificationLevel>(j["level"]);
    msg.title = j["title"];
    msg.message = j["message"];
    msg.exchange_id = j["exchange_id"];
    msg.acknowledged = j["acknowledged"];
    msg.metadata = j["metadata"];
    
    auto timestamp_ms = j["timestamp"].get<int64_t>();
    msg.timestamp = std::chrono::system_clock::time_point(
        std::chrono::milliseconds(timestamp_ms));
    
    return msg;
#else
    // Fallback JSON parsing - simplified implementation
    NotificationMessage msg;
    utils::Logger::warn("JSON parsing disabled - returning empty notification message");
    return msg;
#endif
}

// ExchangeNotificationSystem implementation
ExchangeNotificationSystem::ExchangeNotificationSystem() {
    // Add default console handler
    add_notification_handler(NotificationChannel::LOG, 
        [this](const NotificationMessage& msg) { log_handler(msg); });
}

ExchangeNotificationSystem::~ExchangeNotificationSystem() {
    stop();
}

void ExchangeNotificationSystem::start() {
    if (running_.exchange(true)) {
        Logger::warn("Notification system already running");
        return;
    }
    
    cleanup_thread_ = std::make_unique<std::thread>([this] { cleanup_loop(); });
    Logger::info("Exchange notification system started");
}

void ExchangeNotificationSystem::stop() {
    if (!running_.exchange(false)) {
        return;
    }
    
    if (cleanup_thread_ && cleanup_thread_->joinable()) {
        cleanup_thread_->join();
    }
    
    Logger::info("Exchange notification system stopped");
}

void ExchangeNotificationSystem::add_notification_handler(
    NotificationChannel channel, 
    NotificationHandler handler) {
    
    std::unique_lock<std::shared_mutex> lock(handlers_mutex_);
    handlers_[channel] = std::move(handler);
    Logger::info("Added notification handler for channel {}", static_cast<int>(channel));
}

void ExchangeNotificationSystem::remove_notification_handler(NotificationChannel channel) {
    std::unique_lock<std::shared_mutex> lock(handlers_mutex_);
    handlers_.erase(channel);
    Logger::info("Removed notification handler for channel {}", static_cast<int>(channel));
}

void ExchangeNotificationSystem::add_notification_rule(const NotificationRule& rule) {
    std::unique_lock<std::shared_mutex> lock(rules_mutex_);
    rules_.push_back(rule);
    Logger::info("Added notification rule: {}", rule.rule_id);
}

void ExchangeNotificationSystem::remove_notification_rule(const std::string& rule_id) {
    std::unique_lock<std::shared_mutex> lock(rules_mutex_);
    rules_.erase(
        std::remove_if(rules_.begin(), rules_.end(),
            [&rule_id](const NotificationRule& rule) { return rule.rule_id == rule_id; }),
        rules_.end()
    );
    Logger::info("Removed notification rule: {}", rule_id);
}

void ExchangeNotificationSystem::enable_rule(const std::string& rule_id) {
    std::unique_lock<std::shared_mutex> lock(rules_mutex_);
    for (auto& rule : rules_) {
        if (rule.rule_id == rule_id) {
            rule.enabled = true;
            Logger::info("Enabled notification rule: {}", rule_id);
            return;
        }
    }
}

void ExchangeNotificationSystem::disable_rule(const std::string& rule_id) {
    std::unique_lock<std::shared_mutex> lock(rules_mutex_);
    for (auto& rule : rules_) {
        if (rule.rule_id == rule_id) {
            rule.enabled = false;
            Logger::info("Disabled notification rule: {}", rule_id);
            return;
        }
    }
}

void ExchangeNotificationSystem::send_notification(const NotificationMessage& message) {
    NotificationMessage msg_copy = message;
    if (msg_copy.id.empty()) {
        msg_copy.id = generate_notification_id();
    }
    
    // Update statistics
    stats_.total_notifications.fetch_add(1);
    switch (msg_copy.level) {
        case NotificationLevel::INFO:
            stats_.info_notifications.fetch_add(1);
            break;
        case NotificationLevel::WARNING:
            stats_.warning_notifications.fetch_add(1);
            break;
        case NotificationLevel::ERROR:
            stats_.error_notifications.fetch_add(1);
            break;
        case NotificationLevel::CRITICAL:
            stats_.critical_notifications.fetch_add(1);
            break;
    }
    
    // Store in history
    {
        std::unique_lock<std::shared_mutex> lock(history_mutex_);
        notification_history_.push_back(msg_copy);
    }
    
    // Process notification rules
    process_notification_rules(msg_copy);
    
    Logger::debug("Sent notification: {} - {}", msg_copy.title, msg_copy.message);
}

void ExchangeNotificationSystem::send_notification(
    NotificationLevel level,
    const std::string& title,
    const std::string& message,
    const std::string& exchange_id) {
    
    NotificationMessage msg;
    msg.id = generate_notification_id();
    msg.level = level;
    msg.title = title;
    msg.message = message;
    msg.exchange_id = exchange_id;
    msg.timestamp = std::chrono::system_clock::now();
    
    send_notification(msg);
}

std::vector<NotificationMessage> ExchangeNotificationSystem::get_recent_notifications(
    std::chrono::minutes lookback) const {
    
    std::shared_lock<std::shared_mutex> lock(history_mutex_);
    
    auto cutoff_time = std::chrono::system_clock::now() - lookback;
    std::vector<NotificationMessage> recent;
    
    for (const auto& notification : notification_history_) {
        if (notification.timestamp >= cutoff_time) {
            recent.push_back(notification);
        }
    }
    
    std::sort(recent.begin(), recent.end(),
        [](const NotificationMessage& a, const NotificationMessage& b) {
            return a.timestamp > b.timestamp;
        });
    
    return recent;
}

std::vector<NotificationMessage> ExchangeNotificationSystem::get_unacknowledged_notifications() const {
    std::shared_lock<std::shared_mutex> lock(history_mutex_);
    
    std::vector<NotificationMessage> unacknowledged;
    for (const auto& notification : notification_history_) {
        if (!notification.acknowledged) {
            unacknowledged.push_back(notification);
        }
    }
    
    return unacknowledged;
}

void ExchangeNotificationSystem::acknowledge_notification(const std::string& notification_id) {
    std::unique_lock<std::shared_mutex> lock(history_mutex_);
    
    for (auto& notification : notification_history_) {
        if (notification.id == notification_id) {
            notification.acknowledged = true;
            stats_.acknowledged_notifications.fetch_add(1);
            Logger::debug("Acknowledged notification: {}", notification_id);
            return;
        }
    }
}

void ExchangeNotificationSystem::acknowledge_all_notifications() {
    std::unique_lock<std::shared_mutex> lock(history_mutex_);
    
    for (auto& notification : notification_history_) {
        if (!notification.acknowledged) {
            notification.acknowledged = true;
            stats_.acknowledged_notifications.fetch_add(1);
        }
    }
    
    Logger::info("Acknowledged all notifications");
}

void ExchangeNotificationSystem::clear_old_notifications(std::chrono::hours max_age) {
    std::unique_lock<std::shared_mutex> lock(history_mutex_);
    
    auto cutoff_time = std::chrono::system_clock::now() - max_age;
    size_t initial_size = notification_history_.size();
    
    notification_history_.erase(
        std::remove_if(notification_history_.begin(), notification_history_.end(),
            [cutoff_time](const NotificationMessage& notification) {
                return notification.timestamp < cutoff_time;
            }),
        notification_history_.end()
    );
    
    size_t removed_count = initial_size - notification_history_.size();
    if (removed_count > 0) {
        Logger::info("Cleared {} old notifications", removed_count);
    }
}

ExchangeNotificationSystem::NotificationStats ExchangeNotificationSystem::get_stats() const {
    return stats_;
}

void ExchangeNotificationSystem::reset_stats() {
    stats_.total_notifications.store(0);
    stats_.info_notifications.store(0);
    stats_.warning_notifications.store(0);
    stats_.error_notifications.store(0);
    stats_.critical_notifications.store(0);
    stats_.acknowledged_notifications.store(0);
    stats_.channel_stats.clear();
    
    Logger::info("Reset notification statistics");
}

template<typename ExchangeInterface>
void ExchangeNotificationSystem::integrate_with_failover_manager(
    FailoverManager<ExchangeInterface>* manager) {
    
    manager->set_failover_callback([this](const std::string& from_exchange,
                                         const std::string& to_exchange,
                                         FailoverReason reason) {
        std::string reason_str;
        switch (reason) {
            case FailoverReason::CONNECTION_TIMEOUT:
                reason_str = "Connection Timeout";
                break;
            case FailoverReason::API_ERROR:
                reason_str = "API Error";
                break;
            case FailoverReason::RATE_LIMIT_EXCEEDED:
                reason_str = "Rate Limit Exceeded";
                break;
            case FailoverReason::MANUAL_TRIGGER:
                reason_str = "Manual Trigger";
                break;
            case FailoverReason::HEALTH_CHECK_FAILED:
                reason_str = "Health Check Failed";
                break;
            case FailoverReason::HIGH_LATENCY:
                reason_str = "High Latency";
                break;
        }
        
        send_notification(
            NotificationLevel::WARNING,
            "Exchange Failover",
            "Failover from " + from_exchange + " to " + to_exchange + ". Reason: " + reason_str,
            from_exchange
        );
    });
    
    manager->set_health_callback([this](const std::string& exchange,
                                       const ExchangeHealth& health) {
        if (health.status == HealthStatus::UNHEALTHY) {
            send_notification(
                NotificationLevel::ERROR,
                "Exchange Health Alert",
                "Exchange " + exchange + " is unhealthy. Consecutive failures: " + 
                std::to_string(health.consecutive_failures),
                exchange
            );
        } else if (health.status == HealthStatus::DEGRADED) {
            send_notification(
                NotificationLevel::WARNING,
                "Exchange Performance Warning",
                "Exchange " + exchange + " performance degraded. Latency: " + 
                std::to_string(health.latency.count()) + "ms",
                exchange
            );
        }
    });
}

template<typename ExchangeInterface>
void ExchangeNotificationSystem::integrate_with_resilient_adapter(
    ResilientExchangeAdapter<ExchangeInterface>* adapter) {
    
    adapter->set_failover_callback([this](const std::string& from_exchange,
                                         const std::string& to_exchange,
                                         const std::string& operation,
                                         const std::exception& error) {
        send_notification(
            NotificationLevel::WARNING,
            "Operation Failover",
            "Operation '" + operation + "' failed on " + from_exchange + 
            " and switched to " + to_exchange + ". Error: " + error.what(),
            from_exchange
        );
    });
    
    adapter->set_circuit_breaker_callback([this](CircuitState old_state, CircuitState new_state) {
        std::string old_state_str, new_state_str;
        
        switch (old_state) {
            case CircuitState::CLOSED: old_state_str = "CLOSED"; break;
            case CircuitState::OPEN: old_state_str = "OPEN"; break;
            case CircuitState::HALF_OPEN: old_state_str = "HALF_OPEN"; break;
        }
        
        switch (new_state) {
            case CircuitState::CLOSED: new_state_str = "CLOSED"; break;
            case CircuitState::OPEN: new_state_str = "OPEN"; break;
            case CircuitState::HALF_OPEN: new_state_str = "HALF_OPEN"; break;
        }
        
        NotificationLevel level = (new_state == CircuitState::OPEN) ? 
                                 NotificationLevel::ERROR : NotificationLevel::INFO;
        
        send_notification(
            level,
            "Circuit Breaker State Change",
            "Circuit breaker changed from " + old_state_str + " to " + new_state_str,
            ""
        );
    });
}

void ExchangeNotificationSystem::cleanup_loop() {
    Logger::info("Notification cleanup thread started");
    
    while (running_.load()) {
        try {
            // Clear old notifications every hour
            clear_old_notifications(std::chrono::hours(24));
            
            // Reset hourly counters for throttling
            {
                std::unique_lock<std::shared_mutex> lock(rules_mutex_);
                auto now = std::chrono::system_clock::now();
                
                for (auto& rule : rules_) {
                    auto time_since_hour_start = now - rule.hour_start;
                    if (time_since_hour_start >= std::chrono::hours(1)) {
                        rule.notifications_sent_this_hour = 0;
                        rule.hour_start = now;
                    }
                }
            }
            
        } catch (const std::exception& e) {
            Logger::error("Error in notification cleanup: {}", e.what());
        }
        
        std::this_thread::sleep_for(std::chrono::minutes(10));
    }
    
    Logger::info("Notification cleanup thread stopped");
}

bool ExchangeNotificationSystem::should_send_notification(
    const NotificationRule& rule, 
    const NotificationMessage& message) {
    
    if (!rule.enabled) {
        return false;
    }
    
    if (!rule.condition(message)) {
        return false;
    }
    
    auto now = std::chrono::system_clock::now();
    
    // Check throttling
    if (rule.last_sent != std::chrono::system_clock::time_point{}) {
        auto time_since_last = now - rule.last_sent;
        if (time_since_last < rule.throttle_interval) {
            return false;
        }
    }
    
    // Check hourly limit
    if (rule.notifications_sent_this_hour >= rule.max_notifications_per_hour) {
        return false;
    }
    
    return true;
}

void ExchangeNotificationSystem::update_rule_throttle(NotificationRule& rule) {
    auto now = std::chrono::system_clock::now();
    rule.last_sent = now;
    
    // Initialize hour tracking if needed
    if (rule.hour_start == std::chrono::system_clock::time_point{}) {
        rule.hour_start = now;
    }
    
    auto time_since_hour_start = now - rule.hour_start;
    if (time_since_hour_start >= std::chrono::hours(1)) {
        rule.notifications_sent_this_hour = 1;
        rule.hour_start = now;
    } else {
        rule.notifications_sent_this_hour++;
    }
}

void ExchangeNotificationSystem::process_notification_rules(const NotificationMessage& message) {
    std::shared_lock<std::shared_mutex> rules_lock(rules_mutex_);
    std::shared_lock<std::shared_mutex> handlers_lock(handlers_mutex_);
    
    for (auto& rule : rules_) {
        if (should_send_notification(rule, message)) {
            for (NotificationChannel channel : rule.channels) {
                auto handler_it = handlers_.find(channel);
                if (handler_it != handlers_.end()) {
                    try {
                        handler_it->second(message);
                        stats_.channel_stats[channel]++;
                    } catch (const std::exception& e) {
                        Logger::error("Error in notification handler for channel {}: {}", 
                                     static_cast<int>(channel), e.what());
                    }
                }
            }
            
            // Update throttle after successful send
            const_cast<NotificationRule&>(rule).last_sent = std::chrono::system_clock::now();
            const_cast<NotificationRule&>(rule).notifications_sent_this_hour++;
        }
    }
}

std::string ExchangeNotificationSystem::generate_notification_id() const {
    static std::random_device rd;
    static std::mt19937 gen(rd());
    static std::uniform_int_distribution<> dis(0, 15);
    
    std::stringstream ss;
    ss << std::hex;
    for (int i = 0; i < 32; ++i) {
        ss << dis(gen);
    }
    
    return ss.str();
}

void ExchangeNotificationSystem::log_handler(const NotificationMessage& message) {
    std::string level_str;
    switch (message.level) {
        case NotificationLevel::INFO: level_str = "INFO"; break;
        case NotificationLevel::WARNING: level_str = "WARNING"; break;
        case NotificationLevel::ERROR: level_str = "ERROR"; break;
        case NotificationLevel::CRITICAL: level_str = "CRITICAL"; break;
    }
    
    Logger::info("[NOTIFICATION] [{}] {}: {} (Exchange: {})", 
                 level_str, message.title, message.message, 
                 message.exchange_id.empty() ? "ALL" : message.exchange_id);
}

void ExchangeNotificationSystem::console_handler(const NotificationMessage& message) {
    auto time_t = std::chrono::system_clock::to_time_t(message.timestamp);
    std::stringstream ss;
    ss << std::put_time(std::localtime(&time_t), "%Y-%m-%d %H:%M:%S");
    
    std::cout << "[" << ss.str() << "] " << message.title << ": " << message.message << std::endl;
}

// Predefined notification rules
namespace notification_rules {

NotificationRule exchange_failover_rule() {
    NotificationRule rule;
    rule.rule_id = "exchange_failover";
    rule.condition = [](const NotificationMessage& msg) {
        return msg.title.find("Failover") != std::string::npos ||
               msg.title.find("failover") != std::string::npos;
    };
    rule.channels = {NotificationChannel::LOG, NotificationChannel::SLACK};
    rule.throttle_interval = std::chrono::minutes(5);
    rule.max_notifications_per_hour = 6;
    
    return rule;
}

NotificationRule exchange_health_degraded_rule() {
    NotificationRule rule;
    rule.rule_id = "exchange_health_degraded";
    rule.condition = [](const NotificationMessage& msg) {
        return msg.level >= NotificationLevel::WARNING &&
               (msg.title.find("Health") != std::string::npos ||
                msg.title.find("Performance") != std::string::npos);
    };
    rule.channels = {NotificationChannel::LOG};
    rule.throttle_interval = std::chrono::minutes(10);
    
    return rule;
}

NotificationRule exchange_disconnected_rule() {
    NotificationRule rule;
    rule.rule_id = "exchange_disconnected";
    rule.condition = [](const NotificationMessage& msg) {
        return msg.level >= NotificationLevel::ERROR &&
               (msg.message.find("disconnect") != std::string::npos ||
                msg.message.find("connection") != std::string::npos);
    };
    rule.channels = {NotificationChannel::LOG, NotificationChannel::EMAIL};
    rule.throttle_interval = std::chrono::minutes(2);
    
    return rule;
}

NotificationRule high_error_rate_rule() {
    NotificationRule rule;
    rule.rule_id = "high_error_rate";
    rule.condition = [](const NotificationMessage& msg) {
        return msg.level >= NotificationLevel::WARNING &&
               msg.message.find("error rate") != std::string::npos;
    };
    rule.channels = {NotificationChannel::LOG, NotificationChannel::WEBHOOK};
    rule.throttle_interval = std::chrono::minutes(15);
    
    return rule;
}

NotificationRule circuit_breaker_opened_rule() {
    NotificationRule rule;
    rule.rule_id = "circuit_breaker_opened";
    rule.condition = [](const NotificationMessage& msg) {
        return msg.message.find("Circuit breaker") != std::string::npos &&
               msg.message.find("OPEN") != std::string::npos;
    };
    rule.channels = {NotificationChannel::LOG, NotificationChannel::SLACK, NotificationChannel::EMAIL};
    rule.throttle_interval = std::chrono::minutes(1);
    rule.max_notifications_per_hour = 10;
    
    return rule;
}

NotificationRule api_rate_limit_rule() {
    NotificationRule rule;
    rule.rule_id = "api_rate_limit";
    rule.condition = [](const NotificationMessage& msg) {
        return msg.message.find("rate limit") != std::string::npos ||
               msg.message.find("Rate Limit") != std::string::npos;
    };
    rule.channels = {NotificationChannel::LOG};
    rule.throttle_interval = std::chrono::minutes(5);
    
    return rule;
}

} // namespace notification_rules

} // namespace exchange
} // namespace ats