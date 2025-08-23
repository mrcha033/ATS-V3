#include "exchange/failover_manager.hpp"
#include "utils/logger.hpp"
#include <algorithm>
#include <thread>
#include <future>

namespace ats {
namespace exchange {

template<typename ExchangeInterface>
FailoverManager<ExchangeInterface>::FailoverManager(const FailoverConfig& config)
    : config_(config) {
    utils::Logger::info("FailoverManager created with {} exchanges in priority order", 
                 config_.exchange_priority_order.size());
}

template<typename ExchangeInterface>
FailoverManager<ExchangeInterface>::~FailoverManager() {
    stop_health_monitoring();
}

template<typename ExchangeInterface>
void FailoverManager<ExchangeInterface>::register_exchange(
    const std::string& exchange_id,
    std::shared_ptr<ExchangeInterface> exchange,
    int priority) {
    
    std::unique_lock<std::shared_mutex> lock(exchanges_mutex_);
    
    ExchangeEntry entry;
    entry.exchange = exchange;
    entry.priority = priority;
    entry.health.status = HealthStatus::UNKNOWN;
    entry.health.last_check = std::chrono::system_clock::now();
    
    exchanges_[exchange_id] = std::move(entry);
    
    if (current_primary_.empty() || priority > exchanges_[current_primary_].priority) {
        if (!current_primary_.empty()) {
            exchanges_[current_primary_].is_primary = false;
        }
        current_primary_ = exchange_id;
        exchanges_[exchange_id].is_primary = true;
    }
    
    utils::Logger::info("Exchange {} registered with priority {}", exchange_id, priority);
}

template<typename ExchangeInterface>
void FailoverManager<ExchangeInterface>::unregister_exchange(const std::string& exchange_id) {
    std::unique_lock<std::shared_mutex> lock(exchanges_mutex_);
    
    auto it = exchanges_.find(exchange_id);
    if (it == exchanges_.end()) {
        return;
    }
    
    bool was_primary = it->second.is_primary;
    exchanges_.erase(it);
    
    if (was_primary) {
        current_primary_.clear();
        std::string new_primary = find_best_available_exchange();
        if (!new_primary.empty()) {
            current_primary_ = new_primary;
            exchanges_[new_primary].is_primary = true;
        }
    }
    
    utils::Logger::info("Exchange {} unregistered", exchange_id);
}

template<typename ExchangeInterface>
std::shared_ptr<ExchangeInterface> FailoverManager<ExchangeInterface>::get_primary_exchange() {
    std::shared_lock<std::shared_mutex> lock(exchanges_mutex_);
    
    if (current_primary_.empty()) {
        return nullptr;
    }
    
    auto it = exchanges_.find(current_primary_);
    if (it == exchanges_.end() || !it->second.health.is_available()) {
        return nullptr;
    }
    
    return it->second.exchange;
}

template<typename ExchangeInterface>
std::vector<std::shared_ptr<ExchangeInterface>> 
FailoverManager<ExchangeInterface>::get_available_exchanges() {
    std::shared_lock<std::shared_mutex> lock(exchanges_mutex_);
    
    std::vector<std::shared_ptr<ExchangeInterface>> available;
    
    for (const auto& [exchange_id, entry] : exchanges_) {
        if (entry.health.is_available()) {
            available.push_back(entry.exchange);
        }
    }
    
    std::sort(available.begin(), available.end(), 
        [this](const auto& a, const auto& b) {
            auto it_a = std::find_if(exchanges_.begin(), exchanges_.end(),
                [&a](const auto& pair) { return pair.second.exchange == a; });
            auto it_b = std::find_if(exchanges_.begin(), exchanges_.end(),
                [&b](const auto& pair) { return pair.second.exchange == b; });
            
            return it_a->second.priority > it_b->second.priority;
        });
    
    return available;
}

template<typename ExchangeInterface>
void FailoverManager<ExchangeInterface>::set_failover_callback(FailoverCallback callback) {
    failover_callback_ = std::move(callback);
}

template<typename ExchangeInterface>
void FailoverManager<ExchangeInterface>::set_health_callback(HealthCallback callback) {
    health_callback_ = std::move(callback);
}

template<typename ExchangeInterface>
void FailoverManager<ExchangeInterface>::start_health_monitoring() {
    if (monitoring_active_.exchange(true)) {
        utils::Logger::warn("Health monitoring already active");
        return;
    }
    
    health_monitor_thread_ = std::make_unique<std::thread>([this] {
        health_monitor_loop();
    });
    
    utils::Logger::info("Health monitoring started with interval {}ms", 
                 config_.health_check_interval.count());
}

template<typename ExchangeInterface>
void FailoverManager<ExchangeInterface>::stop_health_monitoring() {
    if (!monitoring_active_.exchange(false)) {
        return;
    }
    
    if (health_monitor_thread_ && health_monitor_thread_->joinable()) {
        health_monitor_thread_->join();
    }
    
    utils::Logger::info("Health monitoring stopped");
}

template<typename ExchangeInterface>
void FailoverManager<ExchangeInterface>::trigger_failover(
    const std::string& exchange_id, 
    FailoverReason reason) {
    
    std::unique_lock<std::shared_mutex> lock(exchanges_mutex_);
    
    auto it = exchanges_.find(exchange_id);
    if (it == exchanges_.end()) {
        utils::Logger::warn("Cannot trigger failover for unknown exchange: {}", exchange_id);
        return;
    }
    
    it->second.health.status = HealthStatus::UNHEALTHY;
    it->second.health.consecutive_failures++;
    
    if (it->second.is_primary) {
        std::string new_primary = find_best_available_exchange();
        if (!new_primary.empty() && new_primary != exchange_id) {
            perform_failover(exchange_id, new_primary, reason);
        }
    }
}

template<typename ExchangeInterface>
void FailoverManager<ExchangeInterface>::manual_failover(const std::string& to_exchange_id) {
    std::unique_lock<std::shared_mutex> lock(exchanges_mutex_);
    
    auto it = exchanges_.find(to_exchange_id);
    if (it == exchanges_.end()) {
        utils::Logger::error("Cannot failover to unknown exchange: {}", to_exchange_id);
        return;
    }
    
    if (!it->second.health.is_available()) {
        utils::Logger::error("Cannot failover to unhealthy exchange: {}", to_exchange_id);
        return;
    }
    
    if (it->second.is_primary) {
        utils::Logger::info("Exchange {} is already primary", to_exchange_id);
        return;
    }
    
    perform_failover(current_primary_, to_exchange_id, FailoverReason::MANUAL_TRIGGER);
}

template<typename ExchangeInterface>
ExchangeHealth FailoverManager<ExchangeInterface>::get_exchange_health(
    const std::string& exchange_id) const {
    
    std::shared_lock<std::shared_mutex> lock(exchanges_mutex_);
    
    auto it = exchanges_.find(exchange_id);
    if (it == exchanges_.end()) {
        return ExchangeHealth{};
    }
    
    return it->second.health;
}

template<typename ExchangeInterface>
std::unordered_map<std::string, ExchangeHealth> 
FailoverManager<ExchangeInterface>::get_all_exchange_health() const {
    
    std::shared_lock<std::shared_mutex> lock(exchanges_mutex_);
    
    std::unordered_map<std::string, ExchangeHealth> health_map;
    for (const auto& [exchange_id, entry] : exchanges_) {
        health_map[exchange_id] = entry.health;
    }
    
    return health_map;
}

template<typename ExchangeInterface>
void FailoverManager<ExchangeInterface>::update_exchange_health(
    const std::string& exchange_id,
    const ExchangeHealth& health) {
    
    std::unique_lock<std::shared_mutex> lock(exchanges_mutex_);
    
    auto it = exchanges_.find(exchange_id);
    if (it == exchanges_.end()) {
        return;
    }
    
    ExchangeHealth old_health = it->second.health;
    it->second.health = health;
    it->second.health.last_check = std::chrono::system_clock::now();
    
    if (old_health.status != health.status) {
        notify_health_change(exchange_id, health);
    }
    
    if (should_failover(it->second)) {
        trigger_failover(exchange_id, FailoverReason::HEALTH_CHECK_FAILED);
    }
}

template<typename ExchangeInterface>
bool FailoverManager<ExchangeInterface>::is_monitoring_active() const {
    return monitoring_active_.load();
}

template<typename ExchangeInterface>
std::string FailoverManager<ExchangeInterface>::get_current_primary_exchange() const {
    std::shared_lock<std::shared_mutex> lock(exchanges_mutex_);
    return current_primary_;
}

template<typename ExchangeInterface>
void FailoverManager<ExchangeInterface>::health_monitor_loop() {
    utils::Logger::info("Health monitoring loop started");
    
    while (monitoring_active_.load()) {
        {
            std::shared_lock<std::shared_mutex> lock(exchanges_mutex_);
            for (auto& [exchange_id, entry] : exchanges_) {
                check_exchange_health(exchange_id, entry);
            }
        }
        
        evaluate_failover_conditions();
        
        std::this_thread::sleep_for(config_.health_check_interval);
    }
    
    utils::Logger::info("Health monitoring loop stopped");
}

template<typename ExchangeInterface>
void FailoverManager<ExchangeInterface>::check_exchange_health(
    const std::string& exchange_id,
    ExchangeEntry& entry) {
    
    try {
        auto start_time = std::chrono::high_resolution_clock::now();
        
        bool is_connected = false;
        if (entry.exchange) {
            // Assume exchanges have is_connected method
            try {
                is_connected = entry.exchange->is_connected();
            } catch (...) {
                // Fallback: assume not connected if method fails
                is_connected = false;
            }
        }
        
        auto end_time = std::chrono::high_resolution_clock::now();
        auto latency = std::chrono::duration_cast<std::chrono::milliseconds>(
            end_time - start_time);
        
        entry.health.latency = latency;
        entry.health.last_check = std::chrono::system_clock::now();
        
        if (is_connected && latency <= config_.max_acceptable_latency) {
            entry.health.status = HealthStatus::HEALTHY;
            entry.health.last_success = std::chrono::system_clock::now();
            entry.health.consecutive_failures = 0;
        } else if (is_connected) {
            entry.health.status = HealthStatus::DEGRADED;
            entry.health.last_success = std::chrono::system_clock::now();
        } else {
            entry.health.status = HealthStatus::UNHEALTHY;
            entry.health.consecutive_failures++;
        }
        
    } catch (const std::exception& e) {
        entry.health.status = HealthStatus::UNHEALTHY;
        entry.health.consecutive_failures++;
        entry.health.last_error_message = e.what();
        
        utils::Logger::warn("Health check failed for exchange {}: {}", exchange_id, e.what());
    }
}

template<typename ExchangeInterface>
void FailoverManager<ExchangeInterface>::evaluate_failover_conditions() {
    std::shared_lock<std::shared_mutex> lock(exchanges_mutex_);
    
    if (current_primary_.empty()) {
        return;
    }
    
    auto it = exchanges_.find(current_primary_);
    if (it == exchanges_.end()) {
        return;
    }
    
    const auto& primary_entry = it->second;
    
    if (should_failover(primary_entry)) {
        std::string new_primary = find_best_available_exchange();
        if (!new_primary.empty() && new_primary != current_primary_) {
            FailoverReason reason = FailoverReason::HEALTH_CHECK_FAILED;
            if (primary_entry.health.latency > config_.max_acceptable_latency) {
                reason = FailoverReason::HIGH_LATENCY;
            }
            
            lock.unlock();
            trigger_failover(current_primary_, reason);
        }
    }
    
    if (config_.auto_failback_enabled) {
        for (const auto& preferred_id : config_.exchange_priority_order) {
            if (preferred_id == current_primary_) {
                break;
            }
            
            if (can_failback_to(preferred_id)) {
                lock.unlock();
                perform_failover(current_primary_, preferred_id, 
                               FailoverReason::MANUAL_TRIGGER);
                break;
            }
        }
    }
}

template<typename ExchangeInterface>
void FailoverManager<ExchangeInterface>::perform_failover(
    const std::string& from_exchange,
    const std::string& to_exchange,
    FailoverReason reason) {
    
    std::unique_lock<std::shared_mutex> lock(exchanges_mutex_);
    
    if (from_exchange != current_primary_) {
        return;
    }
    
    auto from_it = exchanges_.find(from_exchange);
    auto to_it = exchanges_.find(to_exchange);
    
    if (from_it == exchanges_.end() || to_it == exchanges_.end()) {
        utils::Logger::error("Cannot perform failover: exchange not found");
        return;
    }
    
    if (!to_it->second.health.is_available()) {
        utils::Logger::error("Cannot failover to unhealthy exchange: {}", to_exchange);
        return;
    }
    
    from_it->second.is_primary = false;
    from_it->second.last_failover = std::chrono::system_clock::now();
    
    to_it->second.is_primary = true;
    current_primary_ = to_exchange;
    
    utils::Logger::info("Failover completed: {} -> {} (reason: {})", 
                 from_exchange, to_exchange, static_cast<int>(reason));
    
    notify_failover(from_exchange, to_exchange, reason);
}

template<typename ExchangeInterface>
std::string FailoverManager<ExchangeInterface>::find_best_available_exchange() const {
    std::string best_exchange;
    int highest_priority = -1;
    
    for (const auto& [exchange_id, entry] : exchanges_) {
        if (entry.health.is_available() && entry.priority > highest_priority) {
            best_exchange = exchange_id;
            highest_priority = entry.priority;
        }
    }
    
    return best_exchange;
}

template<typename ExchangeInterface>
bool FailoverManager<ExchangeInterface>::should_failover(const ExchangeEntry& entry) const {
    if (entry.health.status == HealthStatus::UNHEALTHY) {
        return entry.health.consecutive_failures >= config_.max_consecutive_failures;
    }
    
    if (entry.health.latency > config_.max_acceptable_latency) {
        return true;
    }
    
    if (entry.health.error_rate > config_.max_error_rate) {
        return true;
    }
    
    return false;
}

template<typename ExchangeInterface>
bool FailoverManager<ExchangeInterface>::can_failback_to(const std::string& exchange_id) const {
    auto it = exchanges_.find(exchange_id);
    if (it == exchanges_.end()) {
        return false;
    }
    
    const auto& entry = it->second;
    
    if (!entry.health.is_available()) {
        return false;
    }
    
    auto time_since_failover = std::chrono::system_clock::now() - entry.last_failover;
    if (time_since_failover < config_.failback_cooldown) {
        return false;
    }
    
    return entry.priority > exchanges_.at(current_primary_).priority;
}

template<typename ExchangeInterface>
void FailoverManager<ExchangeInterface>::notify_failover(
    const std::string& from_exchange,
    const std::string& to_exchange,
    FailoverReason reason) {
    
    if (failover_callback_) {
        try {
            failover_callback_(from_exchange, to_exchange, reason);
        } catch (const std::exception& e) {
            utils::Logger::error("Error in failover callback: {}", e.what());
        }
    }
}

template<typename ExchangeInterface>
void FailoverManager<ExchangeInterface>::notify_health_change(
    const std::string& exchange_id,
    const ExchangeHealth& health) {
    
    if (health_callback_) {
        try {
            health_callback_(exchange_id, health);
        } catch (const std::exception& e) {
            utils::Logger::error("Error in health callback: {}", e.what());
        }
    }
}

template<typename ExchangeInterface>
DefaultHealthChecker<ExchangeInterface>::DefaultHealthChecker(
    std::shared_ptr<ExchangeInterface> exchange)
    : exchange_(exchange) {
}

template<typename ExchangeInterface>
ExchangeHealth DefaultHealthChecker<ExchangeInterface>::check_health(
    const std::string& exchange_id) {
    
    ExchangeHealth health;
    health.last_check = std::chrono::system_clock::now();
    
    try {
        auto start_time = std::chrono::high_resolution_clock::now();
        
        bool connection_ok = test_connection();
        bool api_ok = test_api_call();
        
        auto end_time = std::chrono::high_resolution_clock::now();
        health.latency = std::chrono::duration_cast<std::chrono::milliseconds>(
            end_time - start_time);
        
        if (connection_ok && api_ok) {
            health.status = HealthStatus::HEALTHY;
            health.last_success = health.last_check;
            health.consecutive_failures = 0;
        } else if (connection_ok) {
            health.status = HealthStatus::DEGRADED;
        } else {
            health.status = HealthStatus::UNHEALTHY;
            health.consecutive_failures++;
        }
        
    } catch (const std::exception& e) {
        health.status = HealthStatus::UNHEALTHY;
        health.last_error_message = e.what();
        health.consecutive_failures++;
    }
    
    return health;
}

template<typename ExchangeInterface>
bool DefaultHealthChecker<ExchangeInterface>::test_connection() {
    if (!exchange_) {
        return false;
    }
    
    try {
        return exchange_->is_connected();
    } catch (...) {
        // Fallback: assume not connected if method fails
        return false;
    }
    
    return true;
}

template<typename ExchangeInterface>
bool DefaultHealthChecker<ExchangeInterface>::test_api_call() {
    if (!exchange_) {
        return false;
    }
    
    try {
        auto symbols = exchange_->get_supported_symbols();
        return !symbols.empty();
    } catch (...) {
        // Fallback: assume API is working if connection test passed
        return true;
    }
}

} // namespace exchange
} // namespace ats