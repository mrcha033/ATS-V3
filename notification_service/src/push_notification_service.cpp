#include "push_notification_service.hpp"
#include "utils/logger.hpp"
#ifdef HAS_NLOHMANN_JSON
#include <nlohmann/json.hpp>
#endif
#ifdef HAS_CURL
#include <curl/curl.h>
#endif
#include <random>
#include <sstream>
#include <iomanip>
#include <algorithm>

namespace ats {
namespace notification {

// PushNotificationMessage implementation
std::string PushNotificationMessage::to_fcm_json(const std::string& fcm_token) const {
#ifdef HAS_NLOHMANN_JSON
    nlohmann::json fcm_message;
    
    fcm_message["to"] = fcm_token;
    fcm_message["priority"] = priority;
    fcm_message["content_available"] = content_available;
    fcm_message["time_to_live"] = time_to_live.count();
    
    nlohmann::json notification;
    notification["title"] = title;
    notification["body"] = body;
    
    if (!icon.empty()) {
        notification["icon"] = icon;
    }
    
    if (!click_action.empty()) {
        notification["click_action"] = click_action;
    }
    
    fcm_message["notification"] = notification;
    
    if (!data.empty()) {
        fcm_message["data"] = data;
    }
    
    return fcm_message.dump();
#else
    // Simple JSON construction without nlohmann
    std::stringstream json;
    json << "{"
         << "\"to\":\"" << fcm_token << "\","
         << "\"priority\":\"" << priority << "\","
         << "\"content_available\":" << (content_available ? "true" : "false") << ","
         << "\"time_to_live\":" << time_to_live.count() << ","
         << "\"notification\":{"
         << "\"title\":\"" << title << "\","
         << "\"body\":\"" << body << "\"";
    
    if (!icon.empty()) {
        json << ",\"icon\":\"" << icon << "\"";
    }
    
    if (!click_action.empty()) {
        json << ",\"click_action\":\"" << click_action << "\"";
    }
    
    json << "}";
    
    if (!data.empty()) {
        json << ",\"data\":{";
        bool first = true;
        for (const auto& pair : data) {
            if (!first) json << ",";
            json << "\"" << pair.first << "\":\"" << pair.second << "\"";
            first = false;
        }
        json << "}";
    }
    
    json << "}";
    return json.str();
#endif
}

// PushNotificationService implementation
PushNotificationService::PushNotificationService(const PushNotificationConfig& config)
    : config_(config) {
    
#ifdef HAS_CURL
    // Initialize curl globally
    curl_global_init(CURL_GLOBAL_DEFAULT);
#endif
    
    // Create InfluxDB client for notification history storage
    influxdb_client_ = utils::create_influxdb_client();
}

PushNotificationService::~PushNotificationService() {
    shutdown();
#ifdef HAS_CURL
    curl_global_cleanup();
#endif
}

bool PushNotificationService::initialize() {
    if (initialized_.exchange(true)) {
        Logger::warning("Push notification service already initialized");
        return true;
    }
    
    if (config_.firebase_server_key.empty()) {
        Logger::error("Firebase server key not configured");
        return false;
    }
    
    // Initialize InfluxDB connection for notification history
    if (influxdb_client_) {
        try {
            if (!influxdb_client_->connect("http://localhost:8086", "ats_notifications")) {
                Logger::warning("Could not connect to InfluxDB for notification history");
            } else {
                // Create database if it doesn't exist
                influxdb_client_->create_database("ats_notifications");
                Logger::info("Connected to InfluxDB for notification history storage");
            }
        } catch (const std::exception& e) {
            Logger::warning("InfluxDB connection failed: {}", e.what());
        }
    }
    
    Logger::info("Push notification service initialized successfully");
    return true;
}

void PushNotificationService::shutdown() {
    if (!initialized_.exchange(false)) {
        return;
    }
    
    if (influxdb_client_) {
        influxdb_client_->disconnect();
    }
    
    Logger::info("Push notification service shut down");
}

bool PushNotificationService::register_device(const DeviceRegistration& registration) {
    std::unique_lock<std::shared_mutex> lock(devices_mutex_);
    
    // Check if device already exists
    auto it = std::find_if(registered_devices_.begin(), registered_devices_.end(),
        [&registration](const DeviceRegistration& device) {
            return device.device_id == registration.device_id;
        });
    
    if (it != registered_devices_.end()) {
        // Update existing device
        it->fcm_token = registration.fcm_token;
        it->is_active = registration.is_active;
        it->registered_at = std::chrono::system_clock::now();
        Logger::info("Updated device registration: {}", registration.device_id);
    } else {
        // Add new device
        registered_devices_.push_back(registration);
        stats_.active_devices.fetch_add(1);
        Logger::info("Registered new device: {} for user: {}", 
                    registration.device_id, registration.user_id);
    }
    
    return true;
}

bool PushNotificationService::unregister_device(const std::string& device_id) {
    std::unique_lock<std::shared_mutex> lock(devices_mutex_);
    
    auto it = std::find_if(registered_devices_.begin(), registered_devices_.end(),
        [&device_id](const DeviceRegistration& device) {
            return device.device_id == device_id;
        });
    
    if (it != registered_devices_.end()) {
        registered_devices_.erase(it);
        stats_.active_devices.fetch_sub(1);
        Logger::info("Unregistered device: {}", device_id);
        return true;
    }
    
    Logger::warning("Device not found for unregistration: {}", device_id);
    return false;
}

bool PushNotificationService::update_device_token(const std::string& device_id, 
                                                 const std::string& new_token) {
    std::unique_lock<std::shared_mutex> lock(devices_mutex_);
    
    auto it = std::find_if(registered_devices_.begin(), registered_devices_.end(),
        [&device_id](const DeviceRegistration& device) {
            return device.device_id == device_id;
        });
    
    if (it != registered_devices_.end()) {
        it->fcm_token = new_token;
        it->registered_at = std::chrono::system_clock::now();
        Logger::info("Updated FCM token for device: {}", device_id);
        return true;
    }
    
    Logger::warning("Device not found for token update: {}", device_id);
    return false;
}

std::vector<DeviceRegistration> PushNotificationService::get_user_devices(
    const std::string& user_id) const {
    
    std::shared_lock<std::shared_mutex> lock(devices_mutex_);
    
    std::vector<DeviceRegistration> user_devices;
    for (const auto& device : registered_devices_) {
        if (device.user_id == user_id && device.is_active) {
            user_devices.push_back(device);
        }
    }
    
    return user_devices;
}

bool PushNotificationService::send_push_notification(
    const std::string& user_id,
    const PushNotificationMessage& message,
    exchange::NotificationLevel level) {
    
    if (!initialized_.load()) {
        Logger::error("Push notification service not initialized");
        return false;
    }
    
    auto devices = get_user_devices(user_id);
    if (devices.empty()) {
        Logger::warning("No active devices found for user: {}", user_id);
        return false;
    }
    
    bool any_success = false;
    for (const auto& device : devices) {
        if (send_push_notification_to_device(device.device_id, message, level)) {
            any_success = true;
        }
    }
    
    return any_success;
}

bool PushNotificationService::send_push_notification_to_device(
    const std::string& device_id,
    const PushNotificationMessage& message,
    exchange::NotificationLevel level) {
    
    if (!initialized_.load()) {
        Logger::error("Push notification service not initialized");
        return false;
    }
    
    std::shared_lock<std::shared_mutex> lock(devices_mutex_);
    
    // Find device
    auto it = std::find_if(registered_devices_.begin(), registered_devices_.end(),
        [&device_id](const DeviceRegistration& device) {
            return device.device_id == device_id && device.is_active;
        });
    
    if (it == registered_devices_.end()) {
        Logger::warning("Device not found or inactive: {}", device_id);
        return false;
    }
    
    const auto& device = *it;
    lock.unlock();
    
    // Create notification history entry
    NotificationHistory history;
    history.notification_id = generate_notification_id();
    history.user_id = device.user_id;
    history.device_id = device_id;
    history.level = level;
    history.title = message.title;
    history.message = message.body;
    history.channel_type = "push";
    
    // Send FCM request with retry logic
    bool success = false;
    std::string response_body;
    int response_code;
    
    for (int attempt = 0; attempt < config_.retry_attempts && !success; ++attempt) {
        if (attempt > 0) {
            std::this_thread::sleep_for(config_.retry_delay);
            stats_.total_retries.fetch_add(1);
            Logger::debug("Retrying push notification (attempt {})", attempt + 1);
        }
        
        if (send_fcm_request(device.fcm_token, message, response_body, response_code)) {
            success = true;
            history.delivered = true;
            history.delivered_at = std::chrono::system_clock::now();
            stats_.total_delivered.fetch_add(1);
        }
    }
    
    if (!success) {
        history.error_message = "Failed after " + std::to_string(config_.retry_attempts) + " attempts";
        stats_.total_failed.fetch_add(1);
        Logger::error("Failed to send push notification to device: {}", device_id);
    }
    
    handle_fcm_response(response_body, response_code, device_id, history);
    
    stats_.total_sent.fetch_add(1);
    store_notification_history(history);
    
    return success;
}

bool PushNotificationService::send_broadcast_notification(
    const PushNotificationMessage& message,
    exchange::NotificationLevel level) {
    
    std::shared_lock<std::shared_mutex> lock(devices_mutex_);
    auto devices = registered_devices_;
    lock.unlock();
    
    bool any_success = false;
    for (const auto& device : devices) {
        if (device.is_active) {
            if (send_push_notification_to_device(device.device_id, message, level)) {
                any_success = true;
            }
        }
    }
    
    Logger::info("Broadcast notification sent to {} devices", devices.size());
    return any_success;
}

exchange::NotificationHandler PushNotificationService::create_push_notification_handler() {
    return [this](const exchange::NotificationMessage& msg) {
        PushNotificationMessage push_msg;
        push_msg.title = msg.title;
        push_msg.body = msg.message;
        push_msg.data["exchange_id"] = msg.exchange_id;
        push_msg.data["notification_id"] = msg.id;
        push_msg.data["timestamp"] = std::to_string(
            std::chrono::duration_cast<std::chrono::milliseconds>(
                msg.timestamp.time_since_epoch()).count());
        
        // Set icon based on notification level
        switch (msg.level) {
            case exchange::NotificationLevel::INFO:
                push_msg.icon = "ic_info";
                push_msg.priority = "normal";
                break;
            case exchange::NotificationLevel::WARNING:
                push_msg.icon = "ic_warning";
                push_msg.priority = "high";
                break;
            case exchange::NotificationLevel::ERROR:
            case exchange::NotificationLevel::CRITICAL:
                push_msg.icon = "ic_error";
                push_msg.priority = "high";
                break;
        }
        
        // Broadcast to all active devices
        send_broadcast_notification(push_msg, msg.level);
    };
}

PushNotificationService::PushNotificationStats PushNotificationService::get_stats() const {
    return stats_;
}

void PushNotificationService::reset_stats() {
    stats_.total_sent.store(0);
    stats_.total_delivered.store(0);
    stats_.total_failed.store(0);
    stats_.total_retries.store(0);
    
    std::shared_lock<std::shared_mutex> lock(devices_mutex_);
    stats_.active_devices.store(
        std::count_if(registered_devices_.begin(), registered_devices_.end(),
                     [](const DeviceRegistration& device) { return device.is_active; }));
    
    Logger::info("Reset push notification statistics");
}

std::vector<NotificationHistory> PushNotificationService::get_notification_history(
    const std::string& user_id,
    std::chrono::hours lookback) const {
    
    std::shared_lock<std::shared_mutex> lock(history_mutex_);
    
    auto cutoff_time = std::chrono::system_clock::now() - lookback;
    std::vector<NotificationHistory> filtered_history;
    
    for (const auto& history : notification_history_) {
        if (history.user_id == user_id && history.sent_at >= cutoff_time) {
            filtered_history.push_back(history);
        }
    }
    
    std::sort(filtered_history.begin(), filtered_history.end(),
        [](const NotificationHistory& a, const NotificationHistory& b) {
            return a.sent_at > b.sent_at;
        });
    
    return filtered_history;
}

void PushNotificationService::clear_old_history(std::chrono::hours max_age) {
    std::unique_lock<std::shared_mutex> lock(history_mutex_);
    
    auto cutoff_time = std::chrono::system_clock::now() - max_age;
    size_t initial_size = notification_history_.size();
    
    notification_history_.erase(
        std::remove_if(notification_history_.begin(), notification_history_.end(),
            [cutoff_time](const NotificationHistory& history) {
                return history.sent_at < cutoff_time;
            }),
        notification_history_.end()
    );
    
    size_t removed_count = initial_size - notification_history_.size();
    if (removed_count > 0) {
        Logger::info("Cleared {} old notification history entries", removed_count);
    }
}

bool PushNotificationService::send_fcm_request(
    const std::string& fcm_token,
    const PushNotificationMessage& message,
    std::string& response_body,
    int& response_code) {
    
#ifdef HAS_CURL
    CURL* curl = curl_easy_init();
    if (!curl) {
        Logger::error("Failed to initialize CURL for FCM request");
        return false;
    }
    
    // Prepare JSON payload
    std::string json_payload = message.to_fcm_json(fcm_token);
    
    // Set HTTP headers
    struct curl_slist* headers = nullptr;
    std::string auth_header = "Authorization: key=" + config_.firebase_server_key;
    headers = curl_slist_append(headers, "Content-Type: application/json");
    headers = curl_slist_append(headers, auth_header.c_str());
    
    // Configure CURL
    curl_easy_setopt(curl, CURLOPT_URL, "https://fcm.googleapis.com/fcm/send");
    curl_easy_setopt(curl, CURLOPT_POSTFIELDS, json_payload.c_str());
    curl_easy_setopt(curl, CURLOPT_HTTPHEADER, headers);
    curl_easy_setopt(curl, CURLOPT_WRITEFUNCTION, write_callback);
    curl_easy_setopt(curl, CURLOPT_WRITEDATA, &response_body);
    curl_easy_setopt(curl, CURLOPT_TIMEOUT, 30L);
    
    // Perform request
    CURLcode res = curl_easy_perform(curl);
    curl_easy_getinfo(curl, CURLINFO_RESPONSE_CODE, &response_code);
    
    // Cleanup
    curl_slist_free_all(headers);
    curl_easy_cleanup(curl);
    
    if (res != CURLE_OK) {
        Logger::error("CURL request failed: {}", curl_easy_strerror(res));
        return false;
    }
    
    return response_code == 200;
#else
    // CURL not available - simulate success for testing
    response_body = "{\"success\":1}";
    response_code = 200;
    Logger::info("FCM request simulated (CURL not available): {}", message.title);
    return true;
#endif
}

void PushNotificationService::handle_fcm_response(
    const std::string& response_body,
    int response_code,
    const std::string& device_id,
    NotificationHistory& history) {
    
    if (response_code == 200) {
#ifdef HAS_NLOHMANN_JSON
        try {
            nlohmann::json response = nlohmann::json::parse(response_body);
            
            if (response.contains("failure") && response["failure"].get<int>() > 0) {
                if (response.contains("results") && response["results"].is_array() && 
                    !response["results"].empty()) {
                    
                    auto result = response["results"][0];
                    if (result.contains("error")) {
                        std::string error = result["error"];
                        history.error_message = "FCM Error: " + error;
                        history.delivered = false;
                        
                        if (error == "NotRegistered" || error == "InvalidRegistration") {
                            Logger::warning("Device token invalid, marking for removal: {}", device_id);
                            // Could trigger device removal here
                        }
                    }
                }
            }
        } catch (const std::exception& e) {
            Logger::warning("Failed to parse FCM response: {}", e.what());
        }
#else
        // Simple response parsing without nlohmann/json
        if (response_body.find("\"failure\":") != std::string::npos && 
            response_body.find("\"failure\":0") == std::string::npos) {
            history.error_message = "FCM Error detected in response";
            history.delivered = false;
        }
#endif
    } else {
        history.error_message = "HTTP " + std::to_string(response_code) + ": " + response_body;
        history.delivered = false;
    }
}

void PushNotificationService::store_notification_history(const NotificationHistory& history) {
    // Store in memory
    {
        std::unique_lock<std::shared_mutex> lock(history_mutex_);
        notification_history_.push_back(history);
    }
    
    // Store in InfluxDB asynchronously
    store_notification_in_influxdb(history);
}

void PushNotificationService::store_notification_in_influxdb(const NotificationHistory& history) {
    if (!influxdb_client_ || !influxdb_client_->is_connected()) {
        return;
    }
    
    try {
        utils::InfluxDBPoint point("notification_history");
        
        // Tags
        point.tags["user_id"] = history.user_id;
        point.tags["device_id"] = history.device_id;
        point.tags["channel_type"] = history.channel_type;
        point.tags["level"] = std::to_string(static_cast<int>(history.level));
        
        // Fields
        point.fields["delivered"] = history.delivered ? 1.0 : 0.0;
        point.fields["title_length"] = static_cast<double>(history.title.length());
        point.fields["message_length"] = static_cast<double>(history.message.length());
        
        if (history.delivered) {
            auto delivery_time = std::chrono::duration_cast<std::chrono::milliseconds>(
                history.delivered_at - history.sent_at).count();
            point.fields["delivery_time_ms"] = static_cast<double>(delivery_time);
        }
        
        point.timestamp = history.sent_at;
        
        influxdb_client_->write_point(point);
        
    } catch (const std::exception& e) {
        Logger::warning("Failed to store notification history in InfluxDB: {}", e.what());
    }
}

std::string PushNotificationService::generate_notification_id() const {
    static std::random_device rd;
    static std::mt19937 gen(rd());
    static std::uniform_int_distribution<> dis(0, 15);
    
    std::stringstream ss;
    ss << "push_" << std::hex;
    for (int i = 0; i < 16; ++i) {
        ss << dis(gen);
    }
    
    return ss.str();
}

size_t PushNotificationService::write_callback(void* contents, size_t size, size_t nmemb, std::string* userp) {
    size_t total_size = size * nmemb;
    userp->append(static_cast<char*>(contents), total_size);
    return total_size;
}

// Risk notification helpers implementation
namespace risk_notifications {

PushNotificationMessage create_risk_limit_exceeded_notification(
    const std::string& symbol,
    double current_exposure,
    double limit) {
    
    PushNotificationMessage msg;
    msg.title = "Risk Limit Exceeded";
    msg.body = "Symbol " + symbol + " exposure $" + 
               std::to_string(static_cast<int>(current_exposure)) + 
               " exceeds limit $" + std::to_string(static_cast<int>(limit));
    msg.icon = "ic_warning";
    msg.priority = "high";
    msg.data["type"] = "risk_limit";
    msg.data["symbol"] = symbol;
    msg.data["current_exposure"] = std::to_string(current_exposure);
    msg.data["limit"] = std::to_string(limit);
    
    return msg;
}

PushNotificationMessage create_trade_failure_notification(
    const std::string& symbol,
    const std::string& exchange,
    const std::string& error_reason) {
    
    PushNotificationMessage msg;
    msg.title = "Trade Failed";
    msg.body = "Failed to execute " + symbol + " trade on " + exchange + ": " + error_reason;
    msg.icon = "ic_error";
    msg.priority = "high";
    msg.data["type"] = "trade_failure";
    msg.data["symbol"] = symbol;
    msg.data["exchange"] = exchange;
    msg.data["error"] = error_reason;
    
    return msg;
}

PushNotificationMessage create_price_alert_notification(
    const std::string& symbol,
    double current_price,
    double alert_price,
    const std::string& condition) {
    
    PushNotificationMessage msg;
    msg.title = "Price Alert";
    msg.body = symbol + " is now $" + std::to_string(current_price) + 
               " (" + condition + " $" + std::to_string(alert_price) + ")";
    msg.icon = "ic_info";
    msg.priority = "normal";
    msg.data["type"] = "price_alert";
    msg.data["symbol"] = symbol;
    msg.data["current_price"] = std::to_string(current_price);
    msg.data["alert_price"] = std::to_string(alert_price);
    msg.data["condition"] = condition;
    
    return msg;
}

PushNotificationMessage create_system_health_notification(
    const std::string& component,
    const std::string& status,
    const std::string& details) {
    
    PushNotificationMessage msg;
    msg.title = "System Health Alert";
    msg.body = component + " status: " + status + ". " + details;
    msg.icon = status == "healthy" ? "ic_info" : "ic_warning";
    msg.priority = status == "healthy" ? "normal" : "high";
    msg.data["type"] = "system_health";
    msg.data["component"] = component;
    msg.data["status"] = status;
    msg.data["details"] = details;
    
    return msg;
}

} // namespace risk_notifications

} // namespace notification
} // namespace ats