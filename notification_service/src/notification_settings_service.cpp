#include "notification_settings_service.hpp"
#include "utils/logger.hpp"
#include <nlohmann/json.hpp>
#include <algorithm>
#include <random>
#include <sstream>
#include <iomanip>
#include <thread>
#include <chrono>

namespace ats {
namespace notification {

NotificationSettingsService::NotificationSettingsService(
    std::shared_ptr<PushNotificationService> push_service,
    std::shared_ptr<EmailNotificationService> email_service)
    : push_service_(push_service), email_service_(email_service) {
}

NotificationSettingsService::~NotificationSettingsService() {
    shutdown();
}

bool NotificationSettingsService::initialize() {
    if (initialized_.exchange(true)) {
        Logger::warning("Notification settings service already initialized");
        return true;
    }
    
    // Load existing user profiles (from database in production)
    load_user_profiles();
    
    // Start batch processor
    start_batch_processor();
    
    Logger::info("Notification settings service initialized successfully");
    return true;
}

void NotificationSettingsService::shutdown() {
    if (!initialized_.exchange(false)) {
        return;
    }
    
    stop_batch_processor();
    
    Logger::info("Notification settings service shut down");
}

bool NotificationSettingsService::create_user_profile(const UserNotificationProfile& profile) {
    std::unique_lock<std::shared_mutex> lock(profiles_mutex_);
    
    // Check if user already exists
    auto it = std::find_if(user_profiles_.begin(), user_profiles_.end(),
        [&profile](const UserNotificationProfile& p) {
            return p.user_id == profile.user_id;
        });
    
    if (it != user_profiles_.end()) {
        Logger::warning("User profile already exists: {}", profile.user_id);
        return false;
    }
    
    // Create profile with default rules
    UserNotificationProfile new_profile = profile;
    create_default_rules_for_user(new_profile);
    
    user_profiles_.push_back(new_profile);
    stats_.total_users.fetch_add(1);
    if (new_profile.global_enabled) {
        stats_.active_users.fetch_add(1);
    }
    
    save_user_profile(new_profile);
    
    Logger::info("Created user notification profile: {}", profile.user_id);
    return true;
}

bool NotificationSettingsService::update_user_profile(const UserNotificationProfile& profile) {
    std::unique_lock<std::shared_mutex> lock(profiles_mutex_);
    
    auto it = std::find_if(user_profiles_.begin(), user_profiles_.end(),
        [&profile](const UserNotificationProfile& p) {
            return p.user_id == profile.user_id;
        });
    
    if (it == user_profiles_.end()) {
        Logger::warning("User profile not found for update: {}", profile.user_id);
        return false;
    }
    
    bool was_active = it->global_enabled;
    *it = profile;
    it->last_updated = std::chrono::system_clock::now();
    
    // Update active users count
    if (was_active != profile.global_enabled) {
        if (profile.global_enabled) {
            stats_.active_users.fetch_add(1);
        } else {
            stats_.active_users.fetch_sub(1);
        }
    }
    
    save_user_profile(*it);
    
    Logger::info("Updated user notification profile: {}", profile.user_id);
    return true;
}

bool NotificationSettingsService::delete_user_profile(const std::string& user_id) {
    std::unique_lock<std::shared_mutex> lock(profiles_mutex_);
    
    auto it = std::find_if(user_profiles_.begin(), user_profiles_.end(),
        [&user_id](const UserNotificationProfile& p) {
            return p.user_id == user_id;
        });
    
    if (it == user_profiles_.end()) {
        Logger::warning("User profile not found for deletion: {}", user_id);
        return false;
    }
    
    bool was_active = it->global_enabled;
    user_profiles_.erase(it);
    
    stats_.total_users.fetch_sub(1);
    if (was_active) {
        stats_.active_users.fetch_sub(1);
    }
    
    Logger::info("Deleted user notification profile: {}", user_id);
    return true;
}

UserNotificationProfile* NotificationSettingsService::get_user_profile(const std::string& user_id) {
    std::shared_lock<std::shared_mutex> lock(profiles_mutex_);
    
    auto it = std::find_if(user_profiles_.begin(), user_profiles_.end(),
        [&user_id](const UserNotificationProfile& p) {
            return p.user_id == user_id;
        });
    
    return (it != user_profiles_.end()) ? &(*it) : nullptr;
}

std::vector<UserNotificationProfile> NotificationSettingsService::get_all_user_profiles() const {
    std::shared_lock<std::shared_mutex> lock(profiles_mutex_);
    return user_profiles_;
}

bool NotificationSettingsService::add_notification_rule(const std::string& user_id, 
                                                       const NotificationRule& rule) {
    std::unique_lock<std::shared_mutex> lock(profiles_mutex_);
    
    auto it = std::find_if(user_profiles_.begin(), user_profiles_.end(),
        [&user_id](const UserNotificationProfile& p) {
            return p.user_id == user_id;
        });
    
    if (it == user_profiles_.end()) {
        Logger::warning("User profile not found for adding rule: {}", user_id);
        return false;
    }
    
    NotificationRule new_rule = rule;
    new_rule.user_id = user_id;
    new_rule.updated_at = std::chrono::system_clock::now();
    
    it->custom_rules.push_back(new_rule);
    it->last_updated = std::chrono::system_clock::now();
    stats_.total_rules.fetch_add(1);
    
    save_user_profile(*it);
    
    Logger::info("Added notification rule {} for user {}", rule.rule_id, user_id);
    return true;
}

bool NotificationSettingsService::update_notification_rule(const std::string& user_id, 
                                                          const NotificationRule& rule) {
    std::unique_lock<std::shared_mutex> lock(profiles_mutex_);
    
    auto profile_it = std::find_if(user_profiles_.begin(), user_profiles_.end(),
        [&user_id](const UserNotificationProfile& p) {
            return p.user_id == user_id;
        });
    
    if (profile_it == user_profiles_.end()) {
        Logger::warning("User profile not found for updating rule: {}", user_id);
        return false;
    }
    
    auto rule_it = std::find_if(profile_it->custom_rules.begin(), profile_it->custom_rules.end(),
        [&rule](const NotificationRule& r) {
            return r.rule_id == rule.rule_id;
        });
    
    if (rule_it == profile_it->custom_rules.end()) {
        Logger::warning("Notification rule not found for update: {}", rule.rule_id);
        return false;
    }
    
    *rule_it = rule;
    rule_it->user_id = user_id;
    rule_it->updated_at = std::chrono::system_clock::now();
    profile_it->last_updated = std::chrono::system_clock::now();
    
    save_user_profile(*profile_it);
    
    Logger::info("Updated notification rule {} for user {}", rule.rule_id, user_id);
    return true;
}

bool NotificationSettingsService::delete_notification_rule(const std::string& user_id, 
                                                          const std::string& rule_id) {
    std::unique_lock<std::shared_mutex> lock(profiles_mutex_);
    
    auto profile_it = std::find_if(user_profiles_.begin(), user_profiles_.end(),
        [&user_id](const UserNotificationProfile& p) {
            return p.user_id == user_id;
        });
    
    if (profile_it == user_profiles_.end()) {
        Logger::warning("User profile not found for deleting rule: {}", user_id);
        return false;
    }
    
    auto initial_size = profile_it->custom_rules.size();
    profile_it->custom_rules.erase(
        std::remove_if(profile_it->custom_rules.begin(), profile_it->custom_rules.end(),
            [&rule_id](const NotificationRule& r) {
                return r.rule_id == rule_id;
            }),
        profile_it->custom_rules.end()
    );
    
    if (profile_it->custom_rules.size() < initial_size) {
        profile_it->last_updated = std::chrono::system_clock::now();
        stats_.total_rules.fetch_sub(1);
        save_user_profile(*profile_it);
        
        Logger::info("Deleted notification rule {} for user {}", rule_id, user_id);
        return true;
    }
    
    Logger::warning("Notification rule not found for deletion: {}", rule_id);
    return false;
}

std::vector<NotificationRule> NotificationSettingsService::get_user_rules(const std::string& user_id) const {
    std::shared_lock<std::shared_mutex> lock(profiles_mutex_);
    
    auto it = std::find_if(user_profiles_.begin(), user_profiles_.end(),
        [&user_id](const UserNotificationProfile& p) {
            return p.user_id == user_id;
        });
    
    if (it != user_profiles_.end()) {
        return it->custom_rules;
    }
    
    return {};
}

bool NotificationSettingsService::register_user_device(const std::string& user_id, 
                                                      const DeviceRegistration& device) {
    std::unique_lock<std::shared_mutex> lock(profiles_mutex_);
    
    auto it = std::find_if(user_profiles_.begin(), user_profiles_.end(),
        [&user_id](const UserNotificationProfile& p) {
            return p.user_id == user_id;
        });
    
    if (it == user_profiles_.end()) {
        Logger::warning("User profile not found for device registration: {}", user_id);
        return false;
    }
    
    // Check if device already exists
    auto device_it = std::find_if(it->registered_devices.begin(), it->registered_devices.end(),
        [&device](const DeviceRegistration& d) {
            return d.device_id == device.device_id;
        });
    
    if (device_it != it->registered_devices.end()) {
        // Update existing device
        *device_it = device;
        device_it->user_id = user_id;
        Logger::info("Updated device {} for user {}", device.device_id, user_id);
    } else {
        // Add new device
        DeviceRegistration new_device = device;
        new_device.user_id = user_id;
        it->registered_devices.push_back(new_device);
        Logger::info("Registered new device {} for user {}", device.device_id, user_id);
    }
    
    it->last_updated = std::chrono::system_clock::now();
    save_user_profile(*it);
    
    // Also register with push notification service
    if (push_service_) {
        DeviceRegistration push_device = device;
        push_device.user_id = user_id;
        push_service_->register_device(push_device);
    }
    
    return true;
}

bool NotificationSettingsService::unregister_user_device(const std::string& user_id, 
                                                        const std::string& device_id) {
    std::unique_lock<std::shared_mutex> lock(profiles_mutex_);
    
    auto profile_it = std::find_if(user_profiles_.begin(), user_profiles_.end(),
        [&user_id](const UserNotificationProfile& p) {
            return p.user_id == user_id;
        });
    
    if (profile_it == user_profiles_.end()) {
        Logger::warning("User profile not found for device unregistration: {}", user_id);
        return false;
    }
    
    auto initial_size = profile_it->registered_devices.size();
    profile_it->registered_devices.erase(
        std::remove_if(profile_it->registered_devices.begin(), profile_it->registered_devices.end(),
            [&device_id](const DeviceRegistration& d) {
                return d.device_id == device_id;
            }),
        profile_it->registered_devices.end()
    );
    
    if (profile_it->registered_devices.size() < initial_size) {
        profile_it->last_updated = std::chrono::system_clock::now();
        save_user_profile(*profile_it);
        
        // Also unregister from push notification service
        if (push_service_) {
            push_service_->unregister_device(device_id);
        }
        
        Logger::info("Unregistered device {} for user {}", device_id, user_id);
        return true;
    }
    
    Logger::warning("Device not found for unregistration: {}", device_id);
    return false;
}

bool NotificationSettingsService::should_send_notification(
    const std::string& user_id,
    const exchange::NotificationMessage& message,
    const std::string& category,
    NotificationChannel channel) const {
    
    std::shared_lock<std::shared_mutex> lock(profiles_mutex_);
    
    auto it = std::find_if(user_profiles_.begin(), user_profiles_.end(),
        [&user_id](const UserNotificationProfile& p) {
            return p.user_id == user_id;
        });
    
    if (it == user_profiles_.end()) {
        return false; // No profile, don't send
    }
    
    const auto& profile = *it;
    
    // Check if notifications are globally enabled
    if (!profile.global_enabled) {
        return false;
    }
    
    // Check if channel is enabled
    auto channel_it = profile.channel_enabled.find(channel);
    if (channel_it == profile.channel_enabled.end() || !channel_it->second) {
        return false;
    }
    
    // Check quiet hours
    if (profile.quiet_mode_enabled && is_in_quiet_hours(profile)) {
        // Only allow critical notifications during quiet hours
        if (message.level != exchange::NotificationLevel::CRITICAL) {
            return false;
        }
    }
    
    // Check custom rules
    for (const auto& rule : profile.custom_rules) {
        if (rule.enabled && matches_rule_criteria(rule, message, category)) {
            // Check if channel is enabled for this rule
            auto rule_channel_it = std::find(rule.enabled_channels.begin(), 
                                           rule.enabled_channels.end(), channel);
            return (rule_channel_it != rule.enabled_channels.end());
        }
    }
    
    // Default behavior: send for WARNING and above
    return message.level >= exchange::NotificationLevel::WARNING;
}

bool NotificationSettingsService::process_notification(
    const exchange::NotificationMessage& message,
    const std::string& category) {
    
    std::shared_lock<std::shared_mutex> lock(profiles_mutex_);
    auto profiles = user_profiles_;
    lock.unlock();
    
    bool any_sent = false;
    
    for (const auto& profile : profiles) {
        if (!profile.global_enabled) {
            continue;
        }
        
        // Process each enabled channel
        for (const auto& channel_pair : profile.channel_enabled) {
            NotificationChannel channel = channel_pair.first;
            bool channel_enabled = channel_pair.second;
            
            if (!channel_enabled) {
                continue;
            }
            
            if (!should_send_notification(profile.user_id, message, category, channel)) {
                stats_.notifications_filtered.fetch_add(1);
                continue;
            }
            
            // Check frequency setting
            auto freq_it = profile.channel_frequency.find(channel);
            NotificationFrequency frequency = (freq_it != profile.channel_frequency.end()) ?
                freq_it->second : NotificationFrequency::IMMEDIATE;
            
            if (frequency == NotificationFrequency::IMMEDIATE) {
                // Send immediately
                switch (channel) {
                    case NotificationChannel::PUSH:
                        if (push_service_) {
                            push_service_->send_push_notification(
                                profile.user_id,
                                notification::risk_notifications::create_system_health_notification(
                                    message.exchange_id.empty() ? "System" : message.exchange_id,
                                    message.title,
                                    message.message
                                ),
                                message.level
                            );
                            any_sent = true;
                        }
                        break;
                        
                    case NotificationChannel::EMAIL:
                        if (email_service_) {
                            email_service_->send_notification_email(message, category);
                            any_sent = true;
                        }
                        break;
                        
                    default:
                        // Other channels not implemented yet
                        break;
                }
                
                stats_.notifications_sent_immediate.fetch_add(1);
                stats_.channel_usage[channel]++;
                
            } else {
                // Add to batch for later processing
                add_to_batch(profile.user_id, channel, message);
                stats_.notifications_batched.fetch_add(1);
                any_sent = true;
            }
            
            stats_.frequency_usage[frequency]++;
        }
    }
    
    return any_sent;
}

exchange::NotificationHandler NotificationSettingsService::create_settings_aware_handler(
    const std::string& category) {
    
    return [this, category](const exchange::NotificationMessage& msg) {
        process_notification(msg, category);
    };
}

void NotificationSettingsService::start_batch_processor() {
    if (batch_processor_running_.exchange(true)) {
        Logger::warning("Batch processor already running");
        return;
    }
    
    batch_processor_thread_ = std::make_unique<std::thread>([this] { batch_processor_loop(); });
    Logger::info("Started notification batch processor");
}

void NotificationSettingsService::stop_batch_processor() {
    if (!batch_processor_running_.exchange(false)) {
        return;
    }
    
    if (batch_processor_thread_ && batch_processor_thread_->joinable()) {
        batch_processor_thread_->join();
    }
    
    Logger::info("Stopped notification batch processor");
}

void NotificationSettingsService::batch_processor_loop() {
    Logger::info("Batch processor thread started");
    
    while (batch_processor_running_.load()) {
        try {
            process_pending_batches();
        } catch (const std::exception& e) {
            Logger::error("Error in batch processor: {}", e.what());
        }
        
        // Sleep for 1 minute before next batch check
        std::this_thread::sleep_for(std::chrono::minutes(1));
    }
    
    Logger::info("Batch processor thread stopped");
}

void NotificationSettingsService::process_pending_batches() {
    std::unique_lock<std::shared_mutex> lock(batches_mutex_);
    
    auto now = std::chrono::system_clock::now();
    std::vector<NotificationBatch> ready_batches;
    
    // Find batches ready to send
    for (auto& batch : pending_batches_) {
        if (!batch.sent && batch.scheduled_send_time <= now) {
            ready_batches.push_back(batch);
            batch.sent = true; // Mark as processed
        }
    }
    
    lock.unlock();
    
    // Send ready batches
    for (const auto& batch : ready_batches) {
        send_batched_notifications(batch);
    }
    
    // Clean up sent batches
    if (!ready_batches.empty()) {
        lock.lock();
        pending_batches_.erase(
            std::remove_if(pending_batches_.begin(), pending_batches_.end(),
                [](const NotificationBatch& batch) { return batch.sent; }),
            pending_batches_.end()
        );
        lock.unlock();
        
        Logger::debug("Processed {} notification batches", ready_batches.size());
    }
}

NotificationSettingsService::NotificationSettingsStats NotificationSettingsService::get_stats() const {
    return stats_;
}

void NotificationSettingsService::reset_stats() {
    stats_.notifications_filtered.store(0);
    stats_.notifications_batched.store(0);
    stats_.notifications_sent_immediate.store(0);
    stats_.channel_usage.clear();
    stats_.frequency_usage.clear();
    
    std::shared_lock<std::shared_mutex> lock(profiles_mutex_);
    stats_.total_users.store(user_profiles_.size());
    stats_.active_users.store(
        std::count_if(user_profiles_.begin(), user_profiles_.end(),
                     [](const UserNotificationProfile& p) { return p.global_enabled; }));
    
    size_t total_rules = 0;
    for (const auto& profile : user_profiles_) {
        total_rules += profile.custom_rules.size();
    }
    stats_.total_rules.store(total_rules);
    
    Logger::info("Reset notification settings statistics");
}

// Private helper methods
bool NotificationSettingsService::is_in_quiet_hours(const UserNotificationProfile& profile) const {
    // Simple implementation - in production would consider timezone
    std::string current_time = get_current_time_string();
    return is_time_in_range(current_time, profile.quiet_hours_start, profile.quiet_hours_end);
}

bool NotificationSettingsService::matches_rule_criteria(
    const NotificationRule& rule,
    const exchange::NotificationMessage& message,
    const std::string& category) const {
    
    // Check category
    if (rule.category != "all" && rule.category != category) {
        return false;
    }
    
    // Check minimum level
    if (message.level < rule.min_level) {
        return false;
    }
    
    // Check exchange filters
    if (!rule.exchange_filters.empty()) {
        bool found = std::find(rule.exchange_filters.begin(), rule.exchange_filters.end(),
                              message.exchange_id) != rule.exchange_filters.end();
        if (!found) {
            return false;
        }
    }
    
    // Check keyword filters
    if (!rule.keyword_filters.empty()) {
        bool found = false;
        for (const auto& keyword : rule.keyword_filters) {
            if (message.message.find(keyword) != std::string::npos ||
                message.title.find(keyword) != std::string::npos) {
                found = true;
                break;
            }
        }
        if (!found) {
            return false;
        }
    }
    
    // Check exclude keywords
    for (const auto& exclude_keyword : rule.exclude_keywords) {
        if (message.message.find(exclude_keyword) != std::string::npos ||
            message.title.find(exclude_keyword) != std::string::npos) {
            return false;
        }
    }
    
    return true;
}

void NotificationSettingsService::add_to_batch(
    const std::string& user_id,
    NotificationChannel channel,
    const exchange::NotificationMessage& message) {
    
    std::unique_lock<std::shared_mutex> lock(batches_mutex_);
    
    // Find existing batch for this user and channel
    auto it = std::find_if(pending_batches_.begin(), pending_batches_.end(),
        [&user_id, channel](const NotificationBatch& batch) {
            return batch.user_id == user_id && batch.channel == channel && !batch.sent;
        });
    
    if (it != pending_batches_.end()) {
        // Add to existing batch
        it->messages.push_back(message);
    } else {
        // Create new batch
        NotificationBatch new_batch;
        new_batch.batch_id = generate_batch_id();
        new_batch.user_id = user_id;
        new_batch.channel = channel;
        new_batch.messages.push_back(message);
        
        // Calculate send time based on frequency
        auto* profile = get_user_profile(user_id);
        if (profile) {
            auto freq_it = profile->channel_frequency.find(channel);
            NotificationFrequency frequency = (freq_it != profile->channel_frequency.end()) ?
                freq_it->second : NotificationFrequency::BATCHED_15MIN;
            
            switch (frequency) {
                case NotificationFrequency::BATCHED_5MIN:
                    new_batch.scheduled_send_time = new_batch.created_at + std::chrono::minutes(5);
                    break;
                case NotificationFrequency::BATCHED_15MIN:
                    new_batch.scheduled_send_time = new_batch.created_at + std::chrono::minutes(15);
                    break;
                case NotificationFrequency::BATCHED_HOURLY:
                    new_batch.scheduled_send_time = new_batch.created_at + std::chrono::hours(1);
                    break;
                case NotificationFrequency::DAILY_DIGEST:
                    new_batch.scheduled_send_time = new_batch.created_at + std::chrono::hours(24);
                    break;
                default:
                    new_batch.scheduled_send_time = new_batch.created_at + std::chrono::minutes(15);
                    break;
            }
        }
        
        pending_batches_.push_back(new_batch);
    }
}

void NotificationSettingsService::send_batched_notifications(const NotificationBatch& batch) {
    auto* profile = get_user_profile(batch.user_id);
    if (!profile) {
        Logger::warning("User profile not found for batch sending: {}", batch.user_id);
        return;
    }
    
    switch (batch.channel) {
        case NotificationChannel::EMAIL:
            if (email_service_ && !batch.messages.empty()) {
                // Create digest email
                std::stringstream subject;
                subject << "ATS Digest - " << batch.messages.size() << " notifications";
                
                std::stringstream body;
                body << "ATS Notification Digest\n\n";
                for (const auto& msg : batch.messages) {
                    body << "• " << msg.title << ": " << msg.message << "\n";
                }
                
                EmailMessage digest_email(profile->email, subject.str(), body.str());
                email_service_->send_email(digest_email);
                
                Logger::debug("Sent email digest with {} notifications to {}", 
                             batch.messages.size(), batch.user_id);
            }
            break;
            
        case NotificationChannel::PUSH:
            if (push_service_) {
                // Send individual push notifications for batch
                for (const auto& msg : batch.messages) {
                    auto push_msg = notification::risk_notifications::create_system_health_notification(
                        msg.exchange_id.empty() ? "System" : msg.exchange_id,
                        msg.title,
                        msg.message
                    );
                    push_service_->send_push_notification(batch.user_id, push_msg, msg.level);
                }
                
                Logger::debug("Sent {} push notifications to {}", 
                             batch.messages.size(), batch.user_id);
            }
            break;
            
        default:
            Logger::warning("Batch sending not implemented for channel: {}", 
                           static_cast<int>(batch.channel));
            break;
    }
}

std::string NotificationSettingsService::generate_batch_id() const {
    static std::random_device rd;
    static std::mt19937 gen(rd());
    static std::uniform_int_distribution<> dis(0, 15);
    
    std::stringstream ss;
    ss << "batch_" << std::hex;
    for (int i = 0; i < 16; ++i) {
        ss << dis(gen);
    }
    
    return ss.str();
}

std::string NotificationSettingsService::get_current_time_string() const {
    auto now = std::chrono::system_clock::now();
    auto time_t = std::chrono::system_clock::to_time_t(now);
    std::stringstream ss;
    ss << std::put_time(std::localtime(&time_t), "%H:%M");
    return ss.str();
}

bool NotificationSettingsService::is_time_in_range(
    const std::string& time_str,
    const std::string& start,
    const std::string& end) const {
    
    // Simple time comparison - in production would handle timezone properly
    return time_str >= start && time_str <= end;
}

void NotificationSettingsService::create_default_rules_for_user(UserNotificationProfile& profile) {
    profile.custom_rules.clear();
    profile.custom_rules.push_back(create_risk_rule(profile.user_id));
    profile.custom_rules.push_back(create_trade_rule(profile.user_id));
    profile.custom_rules.push_back(create_system_rule(profile.user_id));
}

NotificationRule NotificationSettingsService::create_risk_rule(const std::string& user_id) {
    NotificationRule rule;
    rule.rule_id = "default_risk";
    rule.user_id = user_id;
    rule.category = "risk";
    rule.min_level = exchange::NotificationLevel::WARNING;
    rule.enabled_channels = {NotificationChannel::PUSH, NotificationChannel::EMAIL};
    rule.frequency = NotificationFrequency::IMMEDIATE;
    rule.max_notifications_per_hour = 5;
    
    return rule;
}

NotificationRule NotificationSettingsService::create_trade_rule(const std::string& user_id) {
    NotificationRule rule;
    rule.rule_id = "default_trade";
    rule.user_id = user_id;
    rule.category = "trade";
    rule.min_level = exchange::NotificationLevel::INFO;
    rule.enabled_channels = {NotificationChannel::PUSH};
    rule.frequency = NotificationFrequency::BATCHED_5MIN;
    rule.max_notifications_per_hour = 20;
    
    return rule;
}

NotificationRule NotificationSettingsService::create_system_rule(const std::string& user_id) {
    NotificationRule rule;
    rule.rule_id = "default_system";
    rule.user_id = user_id;
    rule.category = "system";
    rule.min_level = exchange::NotificationLevel::ERROR;
    rule.enabled_channels = {NotificationChannel::EMAIL};
    rule.frequency = NotificationFrequency::IMMEDIATE;
    rule.max_notifications_per_hour = 3;
    
    return rule;
}

void NotificationSettingsService::save_user_profile(const UserNotificationProfile& profile) {
    // In production, this would save to database
    Logger::debug("Saved user profile: {}", profile.user_id);
}

void NotificationSettingsService::load_user_profiles() {
    // In production, this would load from database
    Logger::debug("Loaded user profiles from storage");
}

} // namespace notification
} // namespace ats