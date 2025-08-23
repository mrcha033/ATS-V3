#pragma once

#include "utils/influxdb_client.hpp"
#include "exchange/exchange_notification_system.hpp"
#include "push_notification_service.hpp"
#include "email_notification_service.hpp"
#include <string>
#include <vector>
#include <memory>
#include <chrono>
#include <atomic>
#include <mutex>
#include <queue>

namespace ats {
namespace notification {

struct NotificationMetrics {
    std::string measurement_name;
    std::string notification_id;
    std::string user_id;
    std::string channel_type; // "push", "email", "slack", etc.
    exchange::NotificationLevel level;
    std::string category;
    std::string exchange_id;
    
    // Timing metrics
    std::chrono::system_clock::time_point created_at;
    std::chrono::system_clock::time_point sent_at;
    std::chrono::system_clock::time_point delivered_at;
    
    // Delivery metrics
    bool delivered = false;
    bool acknowledged = false;
    int retry_count = 0;
    std::chrono::milliseconds delivery_time{0};
    
    // Content metrics
    size_t title_length = 0;
    size_t message_length = 0;
    
    // Error information
    std::string error_code;
    std::string error_message;
    
    // Device/recipient information
    std::string device_id;
    std::string recipient_email;
    
    // Additional metadata
    std::unordered_map<std::string, std::string> custom_tags;
    std::unordered_map<std::string, double> custom_fields;
};

struct NotificationAggregateMetrics {
    std::string measurement_name = "notification_aggregates";
    std::chrono::system_clock::time_point timestamp;
    
    // Counts by level
    uint64_t info_notifications = 0;
    uint64_t warning_notifications = 0;
    uint64_t error_notifications = 0;
    uint64_t critical_notifications = 0;
    
    // Counts by channel
    uint64_t push_notifications = 0;
    uint64_t email_notifications = 0;
    uint64_t slack_notifications = 0;
    uint64_t webhook_notifications = 0;
    
    // Delivery metrics
    uint64_t total_sent = 0;
    uint64_t total_delivered = 0;
    uint64_t total_failed = 0;
    uint64_t total_retries = 0;
    
    // Performance metrics
    double avg_delivery_time_ms = 0.0;
    double max_delivery_time_ms = 0.0;
    double min_delivery_time_ms = 0.0;
    
    // User metrics
    uint64_t active_users = 0;
    uint64_t total_devices = 0;
    uint64_t total_email_recipients = 0;
};

class NotificationInfluxDBStorage {
public:
    explicit NotificationInfluxDBStorage(const std::string& influxdb_url = "http://localhost:8086",
                                       const std::string& database = "ats_notifications");
    ~NotificationInfluxDBStorage();
    
    bool initialize();
    void shutdown();
    
    // Individual notification storage
    bool store_notification_event(const NotificationMetrics& metrics);
    bool store_push_notification(const NotificationHistory& history, const std::string& user_id);
    bool store_email_notification(const EmailDeliveryHistory& history);
    bool store_exchange_notification(const exchange::NotificationMessage& message,
                                   const std::string& channel_type,
                                   bool delivered = false,
                                   const std::string& user_id = "");
    
    // Batch storage
    bool store_notification_batch(const std::vector<NotificationMetrics>& metrics_batch);
    void enable_batch_mode(bool enabled, size_t batch_size = 100, 
                          std::chrono::seconds flush_interval = std::chrono::seconds(30));
    void flush_pending_metrics();
    
    // Aggregate metrics storage
    bool store_aggregate_metrics(const NotificationAggregateMetrics& aggregates);
    void calculate_and_store_hourly_aggregates();
    void calculate_and_store_daily_aggregates();
    
    // Query methods
    std::vector<NotificationMetrics> query_notifications(
        const std::chrono::system_clock::time_point& start_time,
        const std::chrono::system_clock::time_point& end_time,
        const std::string& user_id = "",
        const std::string& channel_type = "",
        exchange::NotificationLevel min_level = exchange::NotificationLevel::INFO) const;
    
    NotificationAggregateMetrics query_aggregate_metrics(
        const std::chrono::system_clock::time_point& start_time,
        const std::chrono::system_clock::time_point& end_time) const;
    
    // Dashboard data queries
    std::vector<std::pair<std::chrono::system_clock::time_point, uint64_t>> 
        get_notification_counts_over_time(
            const std::chrono::system_clock::time_point& start_time,
            const std::chrono::system_clock::time_point& end_time,
            const std::string& interval = "1h") const;
    
    std::unordered_map<std::string, uint64_t> get_notification_counts_by_channel(
        const std::chrono::system_clock::time_point& start_time,
        const std::chrono::system_clock::time_point& end_time) const;
    
    std::unordered_map<std::string, double> get_delivery_performance_metrics(
        const std::chrono::system_clock::time_point& start_time,
        const std::chrono::system_clock::time_point& end_time) const;
    
    std::vector<std::pair<std::string, uint64_t>> get_top_notification_sources(
        const std::chrono::system_clock::time_point& start_time,
        const std::chrono::system_clock::time_point& end_time,
        size_t limit = 10) const;
    
    // Health and maintenance
    bool test_connection() const;
    void cleanup_old_data(std::chrono::hours retention_period = std::chrono::hours(24 * 30)); // 30 days default
    size_t get_stored_notification_count() const;
    
    // Integration helpers
    void integrate_with_push_service(std::shared_ptr<PushNotificationService> push_service);
    void integrate_with_email_service(std::shared_ptr<EmailNotificationService> email_service);
    void integrate_with_exchange_notification_system(
        std::shared_ptr<exchange::ExchangeNotificationSystem> exchange_system);
    
    // Statistics
    struct StorageStats {
        std::atomic<uint64_t> metrics_stored{0};
        std::atomic<uint64_t> batches_stored{0};
        std::atomic<uint64_t> storage_errors{0};
        std::atomic<uint64_t> query_count{0};
        std::atomic<uint64_t> pending_metrics{0};
        
        std::chrono::system_clock::time_point last_storage_time;
        std::chrono::system_clock::time_point last_cleanup_time;
    };
    
    StorageStats get_storage_stats() const;
    void reset_storage_stats();
    
private:
    std::shared_ptr<utils::InfluxDBClient> influxdb_client_;
    std::string influxdb_url_;
    std::string database_name_;
    
    // Batch processing
    bool batch_mode_enabled_ = false;
    size_t batch_size_ = 100;
    std::chrono::seconds flush_interval_{30};
    std::queue<NotificationMetrics> pending_metrics_;
    mutable std::mutex pending_metrics_mutex_;
    
    // Background processing
    std::atomic<bool> running_{false};
    std::unique_ptr<std::thread> batch_processor_thread_;
    std::unique_ptr<std::thread> aggregator_thread_;
    
    StorageStats stats_;
    
    // Helper methods
    utils::InfluxDBPoint create_notification_point(const NotificationMetrics& metrics) const;
    utils::InfluxDBPoint create_aggregate_point(const NotificationAggregateMetrics& aggregates) const;
    
    NotificationMetrics parse_notification_point(const std::unordered_map<std::string, std::string>& point_data) const;
    
    std::string format_influx_timestamp(const std::chrono::system_clock::time_point& timestamp) const;
    std::chrono::system_clock::time_point parse_influx_timestamp(const std::string& timestamp_str) const;
    
    // Background processing loops
    void batch_processor_loop();
    void aggregator_loop();
    
    // Query helpers
    std::string build_notification_query(
        const std::chrono::system_clock::time_point& start_time,
        const std::chrono::system_clock::time_point& end_time,
        const std::string& additional_conditions = "") const;
    
    std::string level_to_string(exchange::NotificationLevel level) const;
    exchange::NotificationLevel string_to_level(const std::string& level_str) const;
    
    // Database setup
    bool create_database_schema();
    bool create_retention_policies();
};

// Utility functions for notification metrics
namespace metrics_utils {

NotificationMetrics create_metrics_from_push_notification(
    const NotificationHistory& history,
    const std::string& user_id);

NotificationMetrics create_metrics_from_email_notification(
    const EmailDeliveryHistory& history);

NotificationMetrics create_metrics_from_exchange_notification(
    const exchange::NotificationMessage& message,
    const std::string& channel_type,
    bool delivered = false,
    const std::string& user_id = "");

NotificationAggregateMetrics calculate_aggregate_metrics(
    const std::vector<NotificationMetrics>& individual_metrics);

// Time range helpers
std::pair<std::chrono::system_clock::time_point, std::chrono::system_clock::time_point>
    get_last_hour_range();

std::pair<std::chrono::system_clock::time_point, std::chrono::system_clock::time_point>
    get_last_day_range();

std::pair<std::chrono::system_clock::time_point, std::chrono::system_clock::time_point>
    get_last_week_range();

} // namespace metrics_utils

} // namespace notification
} // namespace ats