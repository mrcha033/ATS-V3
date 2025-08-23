#include "exchange/resilient_exchange_adapter.hpp"
#include "utils/logger.hpp"
#include <algorithm>
#include <random>
#include <thread>

namespace ats {
namespace exchange {

template<typename ExchangeInterface>
ResilientExchangeAdapter<ExchangeInterface>::ResilientExchangeAdapter(
    std::unique_ptr<FailoverManager<ExchangeInterface>> failover_manager,
    const CircuitBreakerConfig& circuit_config)
    : failover_manager_(std::move(failover_manager))
    , circuit_config_(circuit_config) {
    
    last_failure_time_.store(std::chrono::system_clock::time_point{});
    circuit_opened_time_.store(std::chrono::system_clock::time_point{});
    
    // Set up failover manager callbacks
    failover_manager_->set_failover_callback([this](const std::string& from, const std::string& to, FailoverReason reason) {
        utils::Logger::warn("Exchange failover: {} -> {} (reason: {})", from, to, static_cast<int>(reason));
        
        if (failover_callback_) {
            std::runtime_error error("Failover triggered: " + std::to_string(static_cast<int>(reason)));
            failover_callback_(from, to, "failover", error);
        }
    });
    
    utils::Logger::info("ResilientExchangeAdapter created with circuit breaker config: "
                 "failure_threshold={}, timeout={}s", 
                 circuit_config_.failure_threshold, 
                 circuit_config_.timeout.count());
}

template<typename ExchangeInterface>
ResilientExchangeAdapter<ExchangeInterface>::~ResilientExchangeAdapter() {
    stop();
}

template<typename ExchangeInterface>
void ResilientExchangeAdapter<ExchangeInterface>::register_exchange(
    const std::string& exchange_id,
    std::shared_ptr<ExchangeInterface> exchange,
    int priority) {
    
    failover_manager_->register_exchange(exchange_id, exchange, priority);
    utils::Logger::info("Exchange {} registered in resilient adapter", exchange_id);
}

template<typename ExchangeInterface>
void ResilientExchangeAdapter<ExchangeInterface>::start() {
    failover_manager_->start_health_monitoring();
    utils::Logger::info("ResilientExchangeAdapter started");
}

template<typename ExchangeInterface>
void ResilientExchangeAdapter<ExchangeInterface>::stop() {
    failover_manager_->stop_health_monitoring();
    utils::Logger::info("ResilientExchangeAdapter stopped");
}

template<typename ExchangeInterface>
template<typename ReturnType, typename... Args>
ReturnType ResilientExchangeAdapter<ExchangeInterface>::execute_with_failover(
    const std::string& operation_name,
    std::function<ReturnType(std::shared_ptr<ExchangeInterface>)> operation,
    ReturnType default_return) {
    
    auto start_time = std::chrono::high_resolution_clock::now();
    stats_.total_calls.fetch_add(1);
    
    if (!can_execute()) {
        stats_.circuit_open_calls.fetch_add(1);
        return handle_circuit_open(operation_name, default_return);
    }
    
    auto available_exchanges = failover_manager_->get_available_exchanges();
    if (available_exchanges.empty()) {
        utils::Logger::error("No available exchanges for operation: {}", operation_name);
        record_failure();
        return default_return;
    }
    
    std::exception last_exception("No exchanges available");
    
    for (auto exchange : available_exchanges) {
        try {
            utils::Logger::debug("Executing operation '{}' on exchange", operation_name);
            
            ReturnType result = operation(exchange);
            
            auto end_time = std::chrono::high_resolution_clock::now();
            auto latency = std::chrono::duration_cast<std::chrono::milliseconds>(end_time - start_time);
            
            stats_.total_latency.fetch_add(latency);
            record_success();
            
            return result;
            
        } catch (const std::exception& e) {
            last_exception = e;
            utils::Logger::warn("Operation '{}' failed on exchange: {}", operation_name, e.what());
            
            std::string exchange_id;
            try {
                exchange_id = exchange->get_exchange_id();
            } catch (...) {
                exchange_id = "unknown";
            }
            
            if (!exchange_id.empty()) {
                failover_manager_->trigger_failover(exchange_id, FailoverReason::API_ERROR);
                
                if (failover_callback_) {
                    failover_callback_(exchange_id, "next_available", operation_name, e);
                }
            }
            
            // Don't break immediately, try next exchange
            continue;
        }
    }
    
    // All exchanges failed
    utils::Logger::error("Operation '{}' failed on all available exchanges. Last error: {}", 
                  operation_name, last_exception.what());
    
    record_failure();
    return default_return;
}

template<typename ExchangeInterface>
template<typename ReturnType, typename... Args>
ReturnType ResilientExchangeAdapter<ExchangeInterface>::execute_with_retry(
    const std::string& operation_name,
    std::function<ReturnType(std::shared_ptr<ExchangeInterface>)> operation,
    int max_retries,
    std::chrono::milliseconds retry_delay,
    ReturnType default_return) {
    
    for (int attempt = 0; attempt <= max_retries; ++attempt) {
        try {
            return execute_with_failover(operation_name, operation, default_return);
        } catch (const std::exception& e) {
            if (attempt == max_retries) {
                utils::Logger::error("Operation '{}' failed after {} retries: {}", 
                             operation_name, max_retries, e.what());
                throw;
            }
            
            utils::Logger::warn("Operation '{}' failed on attempt {}/{}, retrying in {}ms: {}", 
                           operation_name, attempt + 1, max_retries + 1, 
                           retry_delay.count(), e.what());
            
            std::this_thread::sleep_for(retry_delay);
        }
    }
    
    return default_return;
}

template<typename ExchangeInterface>
bool ResilientExchangeAdapter<ExchangeInterface>::is_healthy() const {
    if (!can_execute()) {
        return false;
    }
    
    auto primary = failover_manager_->get_primary_exchange();
    return primary != nullptr;
}

template<typename ExchangeInterface>
std::string ResilientExchangeAdapter<ExchangeInterface>::get_current_primary_exchange() const {
    return failover_manager_->get_current_primary_exchange();
}

template<typename ExchangeInterface>
std::vector<std::string> ResilientExchangeAdapter<ExchangeInterface>::get_available_exchanges() const {
    auto exchanges = failover_manager_->get_available_exchanges();
    std::vector<std::string> exchange_ids;
    
    for (auto exchange : exchanges) {
        try {
            exchange_ids.push_back(exchange->get_exchange_id());
        } catch (...) {
            exchange_ids.push_back("unknown");
        }
    }
    
    return exchange_ids;
}

template<typename ExchangeInterface>
ExchangeHealth ResilientExchangeAdapter<ExchangeInterface>::get_exchange_health(
    const std::string& exchange_id) const {
    return failover_manager_->get_exchange_health(exchange_id);
}

template<typename ExchangeInterface>
std::unordered_map<std::string, ExchangeHealth> 
ResilientExchangeAdapter<ExchangeInterface>::get_all_health() const {
    return failover_manager_->get_all_exchange_health();
}

template<typename ExchangeInterface>
CircuitState ResilientExchangeAdapter<ExchangeInterface>::get_circuit_state() const {
    return circuit_state_.load();
}

template<typename ExchangeInterface>
void ResilientExchangeAdapter<ExchangeInterface>::reset_circuit_breaker() {
    CircuitState old_state = circuit_state_.load();
    circuit_state_.store(CircuitState::CLOSED);
    consecutive_failures_.store(0);
    half_open_successes_.store(0);
    half_open_requests_.store(0);
    
    utils::Logger::info("Circuit breaker manually reset");
    notify_circuit_state_change(old_state, CircuitState::CLOSED);
}

template<typename ExchangeInterface>
void ResilientExchangeAdapter<ExchangeInterface>::manually_open_circuit() {
    CircuitState old_state = circuit_state_.load();
    circuit_state_.store(CircuitState::OPEN);
    circuit_opened_time_.store(std::chrono::system_clock::now());
    
    utils::Logger::warn("Circuit breaker manually opened");
    notify_circuit_state_change(old_state, CircuitState::OPEN);
}

template<typename ExchangeInterface>
typename ResilientExchangeAdapter<ExchangeInterface>::OperationStats 
ResilientExchangeAdapter<ExchangeInterface>::get_operation_stats() const {
    return stats_;
}

template<typename ExchangeInterface>
void ResilientExchangeAdapter<ExchangeInterface>::reset_stats() {
    stats_.total_calls.store(0);
    stats_.successful_calls.store(0);
    stats_.failed_calls.store(0);
    stats_.circuit_open_calls.store(0);
    stats_.total_latency.store(std::chrono::milliseconds(0));
    
    utils::Logger::info("Operation statistics reset");
}

template<typename ExchangeInterface>
void ResilientExchangeAdapter<ExchangeInterface>::set_failover_callback(FailoverCallback callback) {
    failover_callback_ = std::move(callback);
}

template<typename ExchangeInterface>
void ResilientExchangeAdapter<ExchangeInterface>::set_circuit_breaker_callback(CircuitBreakerCallback callback) {
    circuit_callback_ = std::move(callback);
}

template<typename ExchangeInterface>
bool ResilientExchangeAdapter<ExchangeInterface>::can_execute() const {
    CircuitState current_state = circuit_state_.load();
    
    switch (current_state) {
        case CircuitState::CLOSED:
            return true;
            
        case CircuitState::OPEN: {
            auto now = std::chrono::system_clock::now();
            auto time_since_opened = now - circuit_opened_time_.load();
            
            if (time_since_opened >= circuit_config_.timeout) {
                circuit_state_.store(CircuitState::HALF_OPEN);
                half_open_requests_.store(0);
                half_open_successes_.store(0);
                utils::Logger::info("Circuit breaker transitioning from OPEN to HALF_OPEN");
                notify_circuit_state_change(CircuitState::OPEN, CircuitState::HALF_OPEN);
                return true;
            }
            return false;
        }
        
        case CircuitState::HALF_OPEN:
            return true;
            
        default:
            return false;
    }
}

template<typename ExchangeInterface>
void ResilientExchangeAdapter<ExchangeInterface>::record_success() {
    stats_.successful_calls.fetch_add(1);
    
    CircuitState current_state = circuit_state_.load();
    
    if (current_state == CircuitState::HALF_OPEN) {
        int successes = half_open_successes_.fetch_add(1) + 1;
        int requests = half_open_requests_.fetch_add(1) + 1;
        
        if (requests >= circuit_config_.min_requests_for_success_rate) {
            double success_rate = static_cast<double>(successes) / requests;
            
            if (success_rate >= circuit_config_.success_threshold) {
                circuit_state_.store(CircuitState::CLOSED);
                consecutive_failures_.store(0);
                utils::Logger::info("Circuit breaker closed after successful recovery");
                notify_circuit_state_change(CircuitState::HALF_OPEN, CircuitState::CLOSED);
            }
        }
    } else if (current_state == CircuitState::CLOSED) {
        consecutive_failures_.store(0);
    }
}

template<typename ExchangeInterface>
void ResilientExchangeAdapter<ExchangeInterface>::record_failure() {
    stats_.failed_calls.fetch_add(1);
    last_failure_time_.store(std::chrono::system_clock::now());
    
    CircuitState current_state = circuit_state_.load();
    
    if (current_state == CircuitState::HALF_OPEN) {
        circuit_state_.store(CircuitState::OPEN);
        circuit_opened_time_.store(std::chrono::system_clock::now());
        utils::Logger::warn("Circuit breaker opened from HALF_OPEN due to failure");
        notify_circuit_state_change(CircuitState::HALF_OPEN, CircuitState::OPEN);
        
    } else if (current_state == CircuitState::CLOSED) {
        int failures = consecutive_failures_.fetch_add(1) + 1;
        
        if (failures >= circuit_config_.failure_threshold) {
            circuit_state_.store(CircuitState::OPEN);
            circuit_opened_time_.store(std::chrono::system_clock::now());
            utils::Logger::warn("Circuit breaker opened due to {} consecutive failures", failures);
            notify_circuit_state_change(CircuitState::CLOSED, CircuitState::OPEN);
        }
    }
    
    update_circuit_state();
}

template<typename ExchangeInterface>
void ResilientExchangeAdapter<ExchangeInterface>::update_circuit_state() {
    // This method can be extended to implement more sophisticated circuit breaker logic
    // such as adaptive thresholds, sliding window failure rate, etc.
}

template<typename ExchangeInterface>
template<typename ReturnType>
ReturnType ResilientExchangeAdapter<ExchangeInterface>::handle_circuit_open(
    const std::string& operation_name,
    ReturnType default_return) {
    
    utils::Logger::debug("Operation '{}' blocked by open circuit breaker", operation_name);
    return default_return;
}

template<typename ExchangeInterface>
void ResilientExchangeAdapter<ExchangeInterface>::notify_circuit_state_change(
    CircuitState old_state, 
    CircuitState new_state) {
    
    if (circuit_callback_) {
        try {
            circuit_callback_(old_state, new_state);
        } catch (const std::exception& e) {
            utils::Logger::error("Error in circuit breaker callback: {}", e.what());
        }
    }
}

template<typename ExchangeInterface>
void ResilientExchangeAdapter<ExchangeInterface>::notify_failover(
    const std::string& from_exchange,
    const std::string& to_exchange,
    const std::string& operation,
    const std::exception& error) {
    
    if (failover_callback_) {
        try {
            failover_callback_(from_exchange, to_exchange, operation, error);
        } catch (const std::exception& e) {
            utils::Logger::error("Error in failover callback: {}", e.what());
        }
    }
}

// ExchangeAdapterBuilder implementation

template<typename ExchangeInterface>
ExchangeAdapterBuilder<ExchangeInterface>::ExchangeAdapterBuilder() {
    // Set default configurations
    failover_config_.health_check_interval = std::chrono::seconds(30);
    failover_config_.connection_timeout = std::chrono::seconds(10);
    failover_config_.max_acceptable_latency = std::chrono::milliseconds(500);
    failover_config_.max_consecutive_failures = 3;
    failover_config_.max_error_rate = 0.1;
    failover_config_.auto_failback_enabled = true;
    failover_config_.failback_cooldown = std::chrono::minutes(5);
}

template<typename ExchangeInterface>
ExchangeAdapterBuilder<ExchangeInterface>& 
ExchangeAdapterBuilder<ExchangeInterface>::with_failover_config(const FailoverConfig& config) {
    failover_config_ = config;
    return *this;
}

template<typename ExchangeInterface>
ExchangeAdapterBuilder<ExchangeInterface>& 
ExchangeAdapterBuilder<ExchangeInterface>::with_circuit_breaker_config(const CircuitBreakerConfig& config) {
    circuit_config_ = config;
    return *this;
}

template<typename ExchangeInterface>
ExchangeAdapterBuilder<ExchangeInterface>& 
ExchangeAdapterBuilder<ExchangeInterface>::add_exchange(
    const std::string& exchange_id,
    std::shared_ptr<ExchangeInterface> exchange,
    int priority) {
    
    exchanges_.push_back({exchange_id, exchange, priority});
    failover_config_.exchange_priority_order.push_back(exchange_id);
    return *this;
}

template<typename ExchangeInterface>
ExchangeAdapterBuilder<ExchangeInterface>& 
ExchangeAdapterBuilder<ExchangeInterface>::with_failover_callback(
    typename ResilientExchangeAdapter<ExchangeInterface>::FailoverCallback callback) {
    failover_callback_ = std::move(callback);
    return *this;
}

template<typename ExchangeInterface>
ExchangeAdapterBuilder<ExchangeInterface>& 
ExchangeAdapterBuilder<ExchangeInterface>::with_circuit_breaker_callback(
    typename ResilientExchangeAdapter<ExchangeInterface>::CircuitBreakerCallback callback) {
    circuit_callback_ = std::move(callback);
    return *this;
}

template<typename ExchangeInterface>
std::unique_ptr<ResilientExchangeAdapter<ExchangeInterface>> 
ExchangeAdapterBuilder<ExchangeInterface>::build() {
    
    auto failover_manager = std::make_unique<FailoverManager<ExchangeInterface>>(failover_config_);
    
    auto adapter = std::make_unique<ResilientExchangeAdapter<ExchangeInterface>>(
        std::move(failover_manager), circuit_config_);
    
    for (const auto& exchange_info : exchanges_) {
        adapter->register_exchange(exchange_info.id, exchange_info.exchange, exchange_info.priority);
    }
    
    if (failover_callback_) {
        adapter->set_failover_callback(failover_callback_);
    }
    
    if (circuit_callback_) {
        adapter->set_circuit_breaker_callback(circuit_callback_);
    }
    
    return adapter;
}

} // namespace exchange
} // namespace ats