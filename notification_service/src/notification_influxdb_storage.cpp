#include "notification_influxdb_storage.hpp"
#include "utils/logger.hpp"
#include <nlohmann/json.hpp>
#include <sstream>
#include <iomanip>
#include <algorithm>
#include <thread>

namespace ats {
namespace notification {

NotificationInfluxDBStorage::NotificationInfluxDBStorage(const std::string& influxdb_url,
                                                       const std::string& database)
    : influxdb_url_(influxdb_url), database_name_(database) {
    
    influxdb_client_ = utils::create_influxdb_client();
}

NotificationInfluxDBStorage::~NotificationInfluxDBStorage() {
    shutdown();
}

bool NotificationInfluxDBStorage::initialize() {
    if (running_.exchange(true)) {
        Logger::warning("Notification InfluxDB storage already initialized");
        return true;
    }
    
    if (!influxdb_client_) {
        Logger::error("Failed to create InfluxDB client");
        return false;
    }
    
    // Connect to InfluxDB
    if (!influxdb_client_->connect(influxdb_url_, database_name_)) {
        Logger::error("Failed to connect to InfluxDB at {}", influxdb_url_);
        running_.store(false);
        return false;
    }
    
    // Create database if it doesn't exist
    if (!influxdb_client_->create_database(database_name_)) {
        Logger::warning("Could not create InfluxDB database: {}", database_name_);
    }
    
    // Setup database schema and retention policies
    if (!create_database_schema()) {
        Logger::warning("Failed to create database schema");
    }
    
    if (!create_retention_policies()) {
        Logger::warning("Failed to create retention policies");
    }
    
    // Start background threads if batch mode is enabled
    if (batch_mode_enabled_) {
        batch_processor_thread_ = std::make_unique<std::thread>([this] { batch_processor_loop(); });
    }
    
    // Start aggregator thread
    aggregator_thread_ = std::make_unique<std::thread>([this] { aggregator_loop(); });
    
    Logger::info("Notification InfluxDB storage initialized successfully");
    return true;
}

void NotificationInfluxDBStorage::shutdown() {
    if (!running_.exchange(false)) {
        return;
    }
    
    // Flush any pending metrics
    flush_pending_metrics();
    
    // Stop background threads
    if (batch_processor_thread_ && batch_processor_thread_->joinable()) {
        batch_processor_thread_->join();
    }
    
    if (aggregator_thread_ && aggregator_thread_->joinable()) {
        aggregator_thread_->join();
    }
    
    // Disconnect from InfluxDB
    if (influxdb_client_) {
        influxdb_client_->disconnect();
    }
    
    Logger::info("Notification InfluxDB storage shut down");
}

bool NotificationInfluxDBStorage::store_notification_event(const NotificationMetrics& metrics) {
    if (!influxdb_client_ || !influxdb_client_->is_connected()) {
        Logger::error("InfluxDB client not connected");
        return false;
    }
    
    if (batch_mode_enabled_) {
        // Add to batch queue
        std::lock_guard<std::mutex> lock(pending_metrics_mutex_);
        pending_metrics_.push(metrics);
        stats_.pending_metrics.fetch_add(1);
        return true;
    } else {
        // Store immediately
        try {
            utils::InfluxDBPoint point = create_notification_point(metrics);
            bool success = influxdb_client_->write_point(point);
            
            if (success) {
                stats_.metrics_stored.fetch_add(1);
                stats_.last_storage_time = std::chrono::system_clock::now();
            } else {
                stats_.storage_errors.fetch_add(1);
                Logger::error("Failed to store notification metrics");
            }
            
            return success;
        } catch (const std::exception& e) {
            stats_.storage_errors.fetch_add(1);
            Logger::error("Exception storing notification metrics: {}", e.what());
            return false;
        }
    }
}

bool NotificationInfluxDBStorage::store_push_notification(const NotificationHistory& history, 
                                                        const std::string& user_id) {
    auto metrics = metrics_utils::create_metrics_from_push_notification(history, user_id);
    return store_notification_event(metrics);
}

bool NotificationInfluxDBStorage::store_email_notification(const EmailDeliveryHistory& history) {
    auto metrics = metrics_utils::create_metrics_from_email_notification(history);
    return store_notification_event(metrics);
}

bool NotificationInfluxDBStorage::store_exchange_notification(
    const exchange::NotificationMessage& message,
    const std::string& channel_type,
    bool delivered,
    const std::string& user_id) {
    
    auto metrics = metrics_utils::create_metrics_from_exchange_notification(
        message, channel_type, delivered, user_id);
    return store_notification_event(metrics);
}

bool NotificationInfluxDBStorage::store_notification_batch(
    const std::vector<NotificationMetrics>& metrics_batch) {
    
    if (!influxdb_client_ || !influxdb_client_->is_connected()) {
        Logger::error("InfluxDB client not connected");
        return false;
    }
    
    try {
        std::vector<utils::InfluxDBPoint> points;
        points.reserve(metrics_batch.size());
        
        for (const auto& metrics : metrics_batch) {
            points.push_back(create_notification_point(metrics));
        }
        
        bool success = influxdb_client_->write_points(points);
        
        if (success) {
            stats_.metrics_stored.fetch_add(metrics_batch.size());
            stats_.batches_stored.fetch_add(1);
            stats_.last_storage_time = std::chrono::system_clock::now();
        } else {
            stats_.storage_errors.fetch_add(1);
            Logger::error("Failed to store notification metrics batch");
        }
        
        return success;
    } catch (const std::exception& e) {
        stats_.storage_errors.fetch_add(1);
        Logger::error("Exception storing notification metrics batch: {}", e.what());
        return false;
    }
}

void NotificationInfluxDBStorage::enable_batch_mode(bool enabled, size_t batch_size, 
                                                  std::chrono::seconds flush_interval) {
    batch_mode_enabled_ = enabled;
    batch_size_ = batch_size;
    flush_interval_ = flush_interval;
    
    if (enabled && running_.load() && !batch_processor_thread_) {
        batch_processor_thread_ = std::make_unique<std::thread>([this] { batch_processor_loop(); });
    }
    
    Logger::info("Batch mode {}, batch_size: {}, flush_interval: {}s", 
                enabled ? "enabled" : "disabled", batch_size, flush_interval.count());
}

void NotificationInfluxDBStorage::flush_pending_metrics() {
    if (!batch_mode_enabled_) {
        return;
    }
    
    std::lock_guard<std::mutex> lock(pending_metrics_mutex_);
    
    if (pending_metrics_.empty()) {
        return;
    }
    
    std::vector<NotificationMetrics> batch;
    while (!pending_metrics_.empty() && batch.size() < batch_size_) {
        batch.push_back(pending_metrics_.front());
        pending_metrics_.pop();
        stats_.pending_metrics.fetch_sub(1);
    }
    
    if (!batch.empty()) {
        store_notification_batch(batch);
        Logger::debug("Flushed {} pending notification metrics", batch.size());
    }
}

bool NotificationInfluxDBStorage::store_aggregate_metrics(const NotificationAggregateMetrics& aggregates) {
    if (!influxdb_client_ || !influxdb_client_->is_connected()) {
        Logger::error("InfluxDB client not connected");
        return false;
    }
    
    try {
        utils::InfluxDBPoint point = create_aggregate_point(aggregates);
        bool success = influxdb_client_->write_point(point);
        
        if (success) {
            stats_.metrics_stored.fetch_add(1);
        } else {
            stats_.storage_errors.fetch_add(1);
            Logger::error("Failed to store aggregate metrics");
        }
        
        return success;
    } catch (const std::exception& e) {
        stats_.storage_errors.fetch_add(1);
        Logger::error("Exception storing aggregate metrics: {}", e.what());
        return false;
    }
}

void NotificationInfluxDBStorage::calculate_and_store_hourly_aggregates() {
    auto [start_time, end_time] = metrics_utils::get_last_hour_range();
    
    try {
        // Query individual metrics for the hour
        auto individual_metrics = query_notifications(start_time, end_time);
        
        if (!individual_metrics.empty()) {
            // Calculate aggregates
            auto aggregates = metrics_utils::calculate_aggregate_metrics(individual_metrics);
            aggregates.timestamp = end_time;
            
            // Store hourly aggregates
            store_aggregate_metrics(aggregates);
            
            Logger::debug("Stored hourly aggregates for {} notifications", individual_metrics.size());
        }
    } catch (const std::exception& e) {
        Logger::error("Failed to calculate hourly aggregates: {}", e.what());
    }
}

void NotificationInfluxDBStorage::calculate_and_store_daily_aggregates() {
    auto [start_time, end_time] = metrics_utils::get_last_day_range();
    
    try {
        // Query individual metrics for the day
        auto individual_metrics = query_notifications(start_time, end_time);
        
        if (!individual_metrics.empty()) {
            // Calculate aggregates
            auto aggregates = metrics_utils::calculate_aggregate_metrics(individual_metrics);
            aggregates.measurement_name = "notification_daily_aggregates";
            aggregates.timestamp = end_time;
            
            // Store daily aggregates
            store_aggregate_metrics(aggregates);
            
            Logger::debug("Stored daily aggregates for {} notifications", individual_metrics.size());
        }
    } catch (const std::exception& e) {
        Logger::error("Failed to calculate daily aggregates: {}", e.what());
    }
}

std::vector<NotificationMetrics> NotificationInfluxDBStorage::query_notifications(
    const std::chrono::system_clock::time_point& start_time,
    const std::chrono::system_clock::time_point& end_time,
    const std::string& user_id,
    const std::string& channel_type,
    exchange::NotificationLevel min_level) const {
    
    if (!influxdb_client_ || !influxdb_client_->is_connected()) {
        Logger::error("InfluxDB client not connected");
        return {};
    }
    
    try {
        std::string additional_conditions;
        
        if (!user_id.empty()) {
            additional_conditions += " AND user_id = '" + user_id + "'";
        }
        
        if (!channel_type.empty()) {
            additional_conditions += " AND channel_type = '" + channel_type + "'";
        }
        
        if (min_level != exchange::NotificationLevel::INFO) {
            additional_conditions += " AND level >= " + std::to_string(static_cast<int>(min_level));
        }
        
        std::string query = build_notification_query(start_time, end_time, additional_conditions);
        
        auto query_result = influxdb_client_->query_table(query);
        stats_.query_count.fetch_add(1);
        
        std::vector<NotificationMetrics> results;
        for (const auto& row : query_result) {
            results.push_back(parse_notification_point(row));
        }
        
        return results;
    } catch (const std::exception& e) {
        Logger::error("Exception querying notifications: {}", e.what());
        return {};
    }
}

bool NotificationInfluxDBStorage::test_connection() const {
    if (!influxdb_client_) {
        return false;
    }
    
    try {
        return influxdb_client_->is_connected();
    } catch (const std::exception& e) {
        Logger::error("Exception testing InfluxDB connection: {}", e.what());
        return false;
    }
}

void NotificationInfluxDBStorage::cleanup_old_data(std::chrono::hours retention_period) {
    if (!influxdb_client_ || !influxdb_client_->is_connected()) {
        Logger::error("InfluxDB client not connected for cleanup");
        return;
    }
    
    try {
        auto cutoff_time = std::chrono::system_clock::now() - retention_period;
        std::string cutoff_timestamp = format_influx_timestamp(cutoff_time);
        
        std::string delete_query = "DELETE FROM notification_events WHERE time < '" + cutoff_timestamp + "'";
        influxdb_client_->query(delete_query);
        
        delete_query = "DELETE FROM notification_aggregates WHERE time < '" + cutoff_timestamp + "'";
        influxdb_client_->query(delete_query);
        
        stats_.last_cleanup_time = std::chrono::system_clock::now();
        
        Logger::info("Cleaned up notification data older than {} hours", retention_period.count());
    } catch (const std::exception& e) {
        Logger::error("Exception during data cleanup: {}", e.what());
    }
}

NotificationInfluxDBStorage::StorageStats NotificationInfluxDBStorage::get_storage_stats() const {
    return stats_;
}

void NotificationInfluxDBStorage::reset_storage_stats() {
    stats_.metrics_stored.store(0);
    stats_.batches_stored.store(0);
    stats_.storage_errors.store(0);
    stats_.query_count.store(0);
    stats_.pending_metrics.store(0);
    
    Logger::info("Reset notification InfluxDB storage statistics");
}

// Private helper methods
utils::InfluxDBPoint NotificationInfluxDBStorage::create_notification_point(
    const NotificationMetrics& metrics) const {
    
    utils::InfluxDBPoint point(metrics.measurement_name.empty() ? "notification_events" : metrics.measurement_name);
    
    // Tags
    point.tags["notification_id"] = metrics.notification_id;
    if (!metrics.user_id.empty()) point.tags["user_id"] = metrics.user_id;
    if (!metrics.channel_type.empty()) point.tags["channel_type"] = metrics.channel_type;
    point.tags["level"] = level_to_string(metrics.level);
    if (!metrics.category.empty()) point.tags["category"] = metrics.category;
    if (!metrics.exchange_id.empty()) point.tags["exchange_id"] = metrics.exchange_id;
    if (!metrics.device_id.empty()) point.tags["device_id"] = metrics.device_id;
    
    // Add custom tags
    for (const auto& tag : metrics.custom_tags) {
        point.tags[tag.first] = tag.second;
    }
    
    // Fields
    point.fields["delivered"] = metrics.delivered ? 1.0 : 0.0;
    point.fields["acknowledged"] = metrics.acknowledged ? 1.0 : 0.0;
    point.fields["retry_count"] = static_cast<double>(metrics.retry_count);
    point.fields["delivery_time_ms"] = static_cast<double>(metrics.delivery_time.count());
    point.fields["title_length"] = static_cast<double>(metrics.title_length);
    point.fields["message_length"] = static_cast<double>(metrics.message_length);
    
    if (!metrics.error_code.empty()) {
        point.tags["error_code"] = metrics.error_code;
    }
    
    // Add custom fields
    for (const auto& field : metrics.custom_fields) {
        point.fields[field.first] = field.second;
    }
    
    point.timestamp = metrics.created_at;
    
    return point;
}

utils::InfluxDBPoint NotificationInfluxDBStorage::create_aggregate_point(
    const NotificationAggregateMetrics& aggregates) const {
    
    utils::InfluxDBPoint point(aggregates.measurement_name);
    
    // Fields for counts by level
    point.fields["info_notifications"] = static_cast<double>(aggregates.info_notifications);
    point.fields["warning_notifications"] = static_cast<double>(aggregates.warning_notifications);
    point.fields["error_notifications"] = static_cast<double>(aggregates.error_notifications);
    point.fields["critical_notifications"] = static_cast<double>(aggregates.critical_notifications);
    
    // Fields for counts by channel
    point.fields["push_notifications"] = static_cast<double>(aggregates.push_notifications);
    point.fields["email_notifications"] = static_cast<double>(aggregates.email_notifications);
    point.fields["slack_notifications"] = static_cast<double>(aggregates.slack_notifications);
    point.fields["webhook_notifications"] = static_cast<double>(aggregates.webhook_notifications);
    
    // Delivery metrics
    point.fields["total_sent"] = static_cast<double>(aggregates.total_sent);
    point.fields["total_delivered"] = static_cast<double>(aggregates.total_delivered);
    point.fields["total_failed"] = static_cast<double>(aggregates.total_failed);
    point.fields["total_retries"] = static_cast<double>(aggregates.total_retries);
    
    // Performance metrics
    point.fields["avg_delivery_time_ms"] = aggregates.avg_delivery_time_ms;
    point.fields["max_delivery_time_ms"] = aggregates.max_delivery_time_ms;
    point.fields["min_delivery_time_ms"] = aggregates.min_delivery_time_ms;
    
    // User metrics
    point.fields["active_users"] = static_cast<double>(aggregates.active_users);
    point.fields["total_devices"] = static_cast<double>(aggregates.total_devices);
    point.fields["total_email_recipients"] = static_cast<double>(aggregates.total_email_recipients);
    
    point.timestamp = aggregates.timestamp;
    
    return point;
}

void NotificationInfluxDBStorage::batch_processor_loop() {
    Logger::info("Notification InfluxDB batch processor started");
    
    while (running_.load()) {
        try {
            flush_pending_metrics();
        } catch (const std::exception& e) {
            Logger::error("Error in batch processor: {}", e.what());
        }
        
        std::this_thread::sleep_for(flush_interval_);
    }
    
    Logger::info("Notification InfluxDB batch processor stopped");
}

void NotificationInfluxDBStorage::aggregator_loop() {
    Logger::info("Notification InfluxDB aggregator started");
    
    auto last_hourly_aggregate = std::chrono::system_clock::now();
    auto last_daily_aggregate = std::chrono::system_clock::now();
    
    while (running_.load()) {
        try {
            auto now = std::chrono::system_clock::now();
            
            // Run hourly aggregates every hour
            if (now - last_hourly_aggregate >= std::chrono::hours(1)) {
                calculate_and_store_hourly_aggregates();
                last_hourly_aggregate = now;
            }
            
            // Run daily aggregates every day
            if (now - last_daily_aggregate >= std::chrono::hours(24)) {
                calculate_and_store_daily_aggregates();
                last_daily_aggregate = now;
            }
            
        } catch (const std::exception& e) {
            Logger::error("Error in aggregator: {}", e.what());
        }
        
        // Sleep for 10 minutes before next check
        std::this_thread::sleep_for(std::chrono::minutes(10));
    }
    
    Logger::info("Notification InfluxDB aggregator stopped");
}

std::string NotificationInfluxDBStorage::build_notification_query(
    const std::chrono::system_clock::time_point& start_time,
    const std::chrono::system_clock::time_point& end_time,
    const std::string& additional_conditions) const {
    
    std::string start_timestamp = format_influx_timestamp(start_time);
    std::string end_timestamp = format_influx_timestamp(end_time);
    
    std::stringstream query;
    query << "SELECT * FROM notification_events WHERE time >= '" << start_timestamp 
          << "' AND time <= '" << end_timestamp << "'";
    
    if (!additional_conditions.empty()) {
        query << additional_conditions;
    }
    
    query << " ORDER BY time DESC";
    
    return query.str();
}

std::string NotificationInfluxDBStorage::level_to_string(exchange::NotificationLevel level) const {
    switch (level) {
        case exchange::NotificationLevel::INFO: return "INFO";
        case exchange::NotificationLevel::WARNING: return "WARNING";
        case exchange::NotificationLevel::ERROR: return "ERROR";
        case exchange::NotificationLevel::CRITICAL: return "CRITICAL";
        default: return "UNKNOWN";
    }
}

exchange::NotificationLevel NotificationInfluxDBStorage::string_to_level(const std::string& level_str) const {
    if (level_str == "INFO") return exchange::NotificationLevel::INFO;
    if (level_str == "WARNING") return exchange::NotificationLevel::WARNING;
    if (level_str == "ERROR") return exchange::NotificationLevel::ERROR;
    if (level_str == "CRITICAL") return exchange::NotificationLevel::CRITICAL;
    return exchange::NotificationLevel::INFO;
}

std::string NotificationInfluxDBStorage::format_influx_timestamp(
    const std::chrono::system_clock::time_point& timestamp) const {
    
    auto time_t = std::chrono::system_clock::to_time_t(timestamp);
    std::stringstream ss;
    ss << std::put_time(std::gmtime(&time_t), "%Y-%m-%dT%H:%M:%SZ");
    return ss.str();
}

bool NotificationInfluxDBStorage::create_database_schema() {
    // InfluxDB is schemaless, but we can create retention policies and continuous queries
    return true;
}

bool NotificationInfluxDBStorage::create_retention_policies() {
    try {
        // Create retention policy for raw notification events (30 days)
        std::string rp_query = "CREATE RETENTION POLICY \"30_days\" ON \"" + database_name_ + 
                              "\" DURATION 30d REPLICATION 1 DEFAULT";
        influxdb_client_->query(rp_query);
        
        // Create retention policy for aggregated data (1 year)
        rp_query = "CREATE RETENTION POLICY \"1_year\" ON \"" + database_name_ + 
                   "\" DURATION 365d REPLICATION 1";
        influxdb_client_->query(rp_query);
        
        return true;
    } catch (const std::exception& e) {
        Logger::warning("Failed to create retention policies: {}", e.what());
        return false;
    }
}

// Utility functions implementation
namespace metrics_utils {

NotificationMetrics create_metrics_from_push_notification(
    const NotificationHistory& history,
    const std::string& user_id) {
    
    NotificationMetrics metrics;
    metrics.measurement_name = "notification_events";
    metrics.notification_id = history.notification_id;
    metrics.user_id = user_id;
    metrics.channel_type = "push";
    metrics.level = history.level;
    metrics.category = "push";
    metrics.device_id = history.device_id;
    
    metrics.created_at = history.sent_at;
    metrics.sent_at = history.sent_at;
    if (history.delivered) {
        metrics.delivered_at = history.delivered_at;
        metrics.delivery_time = std::chrono::duration_cast<std::chrono::milliseconds>(
            history.delivered_at - history.sent_at);
    }
    
    metrics.delivered = history.delivered;
    metrics.title_length = history.title.length();
    metrics.message_length = history.message.length();
    
    if (!history.error_message.empty()) {
        metrics.error_message = history.error_message;
    }
    
    return metrics;
}

NotificationMetrics create_metrics_from_email_notification(
    const EmailDeliveryHistory& history) {
    
    NotificationMetrics metrics;
    metrics.measurement_name = "notification_events";
    metrics.notification_id = history.email_id;
    metrics.user_id = ""; // Email doesn't have user_id directly
    metrics.channel_type = "email";
    metrics.level = history.level;
    metrics.category = history.category;
    metrics.recipient_email = history.recipient_email;
    
    metrics.created_at = history.sent_at;
    metrics.sent_at = history.sent_at;
    if (history.delivered) {
        metrics.delivered_at = history.delivered_at;
        metrics.delivery_time = std::chrono::duration_cast<std::chrono::milliseconds>(
            history.delivered_at - history.sent_at);
    }
    
    metrics.delivered = history.delivered;
    metrics.retry_count = history.retry_count;
    metrics.title_length = history.subject.length();
    
    if (!history.error_message.empty()) {
        metrics.error_message = history.error_message;
    }
    
    return metrics;
}

NotificationMetrics create_metrics_from_exchange_notification(
    const exchange::NotificationMessage& message,
    const std::string& channel_type,
    bool delivered,
    const std::string& user_id) {
    
    NotificationMetrics metrics;
    metrics.measurement_name = "notification_events";
    metrics.notification_id = message.id;
    metrics.user_id = user_id;
    metrics.channel_type = channel_type;
    metrics.level = message.level;
    metrics.category = "exchange";
    metrics.exchange_id = message.exchange_id;
    
    metrics.created_at = message.timestamp;
    metrics.sent_at = message.timestamp;
    if (delivered) {
        metrics.delivered_at = std::chrono::system_clock::now();
        metrics.delivery_time = std::chrono::duration_cast<std::chrono::milliseconds>(
            metrics.delivered_at - metrics.sent_at);
    }
    
    metrics.delivered = delivered;
    metrics.acknowledged = message.acknowledged;
    metrics.title_length = message.title.length();
    metrics.message_length = message.message.length();
    
    return metrics;
}

NotificationAggregateMetrics calculate_aggregate_metrics(
    const std::vector<NotificationMetrics>& individual_metrics) {
    
    NotificationAggregateMetrics aggregates;
    aggregates.timestamp = std::chrono::system_clock::now();
    
    std::vector<double> delivery_times;
    std::unordered_set<std::string> unique_users;
    std::unordered_set<std::string> unique_devices;
    std::unordered_set<std::string> unique_emails;
    
    for (const auto& metrics : individual_metrics) {
        // Count by level
        switch (metrics.level) {
            case exchange::NotificationLevel::INFO:
                aggregates.info_notifications++;
                break;
            case exchange::NotificationLevel::WARNING:
                aggregates.warning_notifications++;
                break;
            case exchange::NotificationLevel::ERROR:
                aggregates.error_notifications++;
                break;
            case exchange::NotificationLevel::CRITICAL:
                aggregates.critical_notifications++;
                break;
        }
        
        // Count by channel
        if (metrics.channel_type == "push") {
            aggregates.push_notifications++;
        } else if (metrics.channel_type == "email") {
            aggregates.email_notifications++;
        } else if (metrics.channel_type == "slack") {
            aggregates.slack_notifications++;
        } else if (metrics.channel_type == "webhook") {
            aggregates.webhook_notifications++;
        }
        
        // Delivery metrics
        aggregates.total_sent++;
        if (metrics.delivered) {
            aggregates.total_delivered++;
            if (metrics.delivery_time.count() > 0) {
                delivery_times.push_back(static_cast<double>(metrics.delivery_time.count()));
            }
        } else {
            aggregates.total_failed++;
        }
        
        aggregates.total_retries += metrics.retry_count;
        
        // Unique users, devices, emails
        if (!metrics.user_id.empty()) {
            unique_users.insert(metrics.user_id);
        }
        if (!metrics.device_id.empty()) {
            unique_devices.insert(metrics.device_id);
        }
        if (!metrics.recipient_email.empty()) {
            unique_emails.insert(metrics.recipient_email);
        }
    }
    
    // Calculate delivery time statistics
    if (!delivery_times.empty()) {
        aggregates.avg_delivery_time_ms = std::accumulate(delivery_times.begin(), delivery_times.end(), 0.0) / delivery_times.size();
        aggregates.max_delivery_time_ms = *std::max_element(delivery_times.begin(), delivery_times.end());
        aggregates.min_delivery_time_ms = *std::min_element(delivery_times.begin(), delivery_times.end());
    }
    
    // Set unique counts
    aggregates.active_users = unique_users.size();
    aggregates.total_devices = unique_devices.size();
    aggregates.total_email_recipients = unique_emails.size();
    
    return aggregates;
}

std::pair<std::chrono::system_clock::time_point, std::chrono::system_clock::time_point>
    get_last_hour_range() {
    
    auto now = std::chrono::system_clock::now();
    auto start = now - std::chrono::hours(1);
    return {start, now};
}

std::pair<std::chrono::system_clock::time_point, std::chrono::system_clock::time_point>
    get_last_day_range() {
    
    auto now = std::chrono::system_clock::now();
    auto start = now - std::chrono::hours(24);
    return {start, now};
}

std::pair<std::chrono::system_clock::time_point, std::chrono::system_clock::time_point>
    get_last_week_range() {
    
    auto now = std::chrono::system_clock::now();
    auto start = now - std::chrono::hours(24 * 7);
    return {start, now};
}

} // namespace metrics_utils

} // namespace notification
} // namespace ats