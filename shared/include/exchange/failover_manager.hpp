#pragma once

#include <memory>
#include <vector>
#include <unordered_map>
#include <string>
#include <atomic>
#include <mutex>
#include <shared_mutex>
#include <chrono>
#include <functional>
#include <thread>
#include "types/common_types.hpp"

namespace ats {
namespace exchange {

enum class HealthStatus {
    HEALTHY,
    DEGRADED,
    UNHEALTHY,
    UNKNOWN
};

enum class FailoverReason {
    CONNECTION_TIMEOUT,
    API_ERROR,
    RATE_LIMIT_EXCEEDED,
    MANUAL_TRIGGER,
    HEALTH_CHECK_FAILED,
    HIGH_LATENCY
};

struct ExchangeHealth {
    HealthStatus status = HealthStatus::UNKNOWN;
    std::chrono::milliseconds latency{0};
    double error_rate = 0.0;
    std::chrono::system_clock::time_point last_check;
    std::chrono::system_clock::time_point last_success;
    int consecutive_failures = 0;
    std::string last_error_message;
    
    bool is_available() const {
        return status == HealthStatus::HEALTHY || status == HealthStatus::DEGRADED;
    }
};

struct FailoverConfig {
    std::chrono::milliseconds health_check_interval{std::chrono::seconds(30)};
    std::chrono::milliseconds connection_timeout{std::chrono::seconds(10)};
    std::chrono::milliseconds max_acceptable_latency{std::chrono::milliseconds(500)};
    int max_consecutive_failures = 3;
    double max_error_rate = 0.1;
    bool auto_failback_enabled = true;
    std::chrono::minutes failback_cooldown{5};
    std::vector<std::string> exchange_priority_order;
};

using FailoverCallback = std::function<void(const std::string& from_exchange, 
                                          const std::string& to_exchange,
                                          FailoverReason reason)>;

using HealthCallback = std::function<void(const std::string& exchange,
                                        const ExchangeHealth& health)>;

template<typename ExchangeInterface>
class FailoverManager {
public:
    explicit FailoverManager(const FailoverConfig& config);
    ~FailoverManager();

    void register_exchange(const std::string& exchange_id, 
                          std::shared_ptr<ExchangeInterface> exchange,
                          int priority = 0);
    
    void unregister_exchange(const std::string& exchange_id);
    
    std::shared_ptr<ExchangeInterface> get_primary_exchange();
    std::vector<std::shared_ptr<ExchangeInterface>> get_available_exchanges();
    
    void set_failover_callback(FailoverCallback callback);
    void set_health_callback(HealthCallback callback);
    
    void start_health_monitoring();
    void stop_health_monitoring();
    
    void trigger_failover(const std::string& exchange_id, FailoverReason reason);
    void manual_failover(const std::string& to_exchange_id);
    
    ExchangeHealth get_exchange_health(const std::string& exchange_id) const;
    std::unordered_map<std::string, ExchangeHealth> get_all_exchange_health() const;
    
    void update_exchange_health(const std::string& exchange_id, 
                              const ExchangeHealth& health);
    
    bool is_monitoring_active() const;
    std::string get_current_primary_exchange() const;
    
private:
    struct ExchangeEntry {
        std::shared_ptr<ExchangeInterface> exchange;
        ExchangeHealth health;
        int priority;
        std::chrono::system_clock::time_point last_failover;
        bool is_primary = false;
    };
    
    FailoverConfig config_;
    std::unordered_map<std::string, ExchangeEntry> exchanges_;
    std::string current_primary_;
    
    mutable std::shared_mutex exchanges_mutex_;
    std::atomic<bool> monitoring_active_{false};
    std::unique_ptr<std::thread> health_monitor_thread_;
    
    FailoverCallback failover_callback_;
    HealthCallback health_callback_;
    
    void health_monitor_loop();
    void check_exchange_health(const std::string& exchange_id, ExchangeEntry& entry);
    void evaluate_failover_conditions();
    void perform_failover(const std::string& from_exchange, 
                         const std::string& to_exchange,
                         FailoverReason reason);
    
    std::string find_best_available_exchange() const;
    bool should_failover(const ExchangeEntry& entry) const;
    bool can_failback_to(const std::string& exchange_id) const;
    
    void notify_failover(const std::string& from_exchange,
                        const std::string& to_exchange,
                        FailoverReason reason);
    
    void notify_health_change(const std::string& exchange_id,
                             const ExchangeHealth& health);
};

class ExchangeHealthChecker {
public:
    virtual ~ExchangeHealthChecker() = default;
    virtual ExchangeHealth check_health(const std::string& exchange_id) = 0;
};

template<typename ExchangeInterface>
class DefaultHealthChecker : public ExchangeHealthChecker {
public:
    explicit DefaultHealthChecker(std::shared_ptr<ExchangeInterface> exchange);
    ExchangeHealth check_health(const std::string& exchange_id) override;

private:
    std::shared_ptr<ExchangeInterface> exchange_;
    std::chrono::system_clock::time_point measure_latency();
    bool test_connection();
    bool test_api_call();
};

} // namespace exchange
} // namespace ats