#pragma once

#include "failover_manager.hpp"
#include "types/common_types.hpp"
#include <memory>
#include <functional>
#include <atomic>
#include <chrono>

namespace ats {
namespace exchange {

struct CircuitBreakerConfig {
    int failure_threshold = 5;
    std::chrono::seconds timeout{30};
    std::chrono::seconds recovery_timeout{60};
    double success_threshold = 0.5;
    int min_requests_for_success_rate = 10;
};

enum class CircuitState {
    CLOSED,    // Normal operation
    OPEN,      // Failing, not allowing calls
    HALF_OPEN  // Testing if service has recovered
};

template<typename ExchangeInterface>
class ResilientExchangeAdapter {
public:
    ResilientExchangeAdapter(
        std::unique_ptr<FailoverManager<ExchangeInterface>> failover_manager,
        const CircuitBreakerConfig& circuit_config = CircuitBreakerConfig{}
    );
    
    ~ResilientExchangeAdapter();
    
    void register_exchange(const std::string& exchange_id,
                          std::shared_ptr<ExchangeInterface> exchange,
                          int priority = 0);
    
    void start();
    void stop();
    
    template<typename ReturnType, typename... Args>
    ReturnType execute_with_failover(
        const std::string& operation_name,
        std::function<ReturnType(std::shared_ptr<ExchangeInterface>)> operation,
        ReturnType default_return = ReturnType{}
    );
    
    template<typename ReturnType, typename... Args>
    ReturnType execute_with_retry(
        const std::string& operation_name,
        std::function<ReturnType(std::shared_ptr<ExchangeInterface>)> operation,
        int max_retries = 3,
        std::chrono::milliseconds retry_delay = std::chrono::milliseconds(1000),
        ReturnType default_return = ReturnType{}
    );
    
    bool is_healthy() const;
    std::string get_current_primary_exchange() const;
    std::vector<std::string> get_available_exchanges() const;
    
    ExchangeHealth get_exchange_health(const std::string& exchange_id) const;
    std::unordered_map<std::string, ExchangeHealth> get_all_health() const;
    
    CircuitState get_circuit_state() const;
    void reset_circuit_breaker();
    void manually_open_circuit();
    
    struct OperationStats {
        std::atomic<uint64_t> total_calls{0};
        std::atomic<uint64_t> successful_calls{0};
        std::atomic<uint64_t> failed_calls{0};
        std::atomic<uint64_t> circuit_open_calls{0};
        std::atomic<std::chrono::milliseconds> total_latency{std::chrono::milliseconds(0)};
        
        double success_rate() const {
            uint64_t total = total_calls.load();
            if (total == 0) return 1.0;
            return static_cast<double>(successful_calls.load()) / total;
        }
        
        std::chrono::milliseconds average_latency() const {
            uint64_t total = total_calls.load();
            if (total == 0) return std::chrono::milliseconds(0);
            return std::chrono::milliseconds(total_latency.load().count() / total);
        }
    };
    
    OperationStats get_operation_stats() const;
    void reset_stats();
    
    using FailoverCallback = std::function<void(const std::string& from_exchange,
                                              const std::string& to_exchange,
                                              const std::string& operation,
                                              const std::exception& error)>;
    
    using CircuitBreakerCallback = std::function<void(CircuitState old_state,
                                                    CircuitState new_state)>;
    
    void set_failover_callback(FailoverCallback callback);
    void set_circuit_breaker_callback(CircuitBreakerCallback callback);
    
private:
    std::unique_ptr<FailoverManager<ExchangeInterface>> failover_manager_;
    CircuitBreakerConfig circuit_config_;
    
    mutable std::atomic<CircuitState> circuit_state_{CircuitState::CLOSED};
    mutable std::atomic<std::chrono::system_clock::time_point> last_failure_time_;
    mutable std::atomic<std::chrono::system_clock::time_point> circuit_opened_time_;
    mutable std::atomic<int> consecutive_failures_{0};
    mutable std::atomic<int> half_open_successes_{0};
    mutable std::atomic<int> half_open_requests_{0};
    
    mutable OperationStats stats_;
    
    FailoverCallback failover_callback_;
    CircuitBreakerCallback circuit_callback_;
    
    bool can_execute() const;
    void record_success();
    void record_failure();
    void update_circuit_state();
    
    template<typename ReturnType>
    ReturnType handle_circuit_open(const std::string& operation_name, 
                                  ReturnType default_return);
    
    void notify_circuit_state_change(CircuitState old_state, CircuitState new_state);
    void notify_failover(const std::string& from_exchange,
                        const std::string& to_exchange,
                        const std::string& operation,
                        const std::exception& error);
};

template<typename ExchangeInterface>
class ExchangeAdapterBuilder {
public:
    ExchangeAdapterBuilder();
    
    ExchangeAdapterBuilder& with_failover_config(const FailoverConfig& config);
    ExchangeAdapterBuilder& with_circuit_breaker_config(const CircuitBreakerConfig& config);
    
    ExchangeAdapterBuilder& add_exchange(const std::string& exchange_id,
                                        std::shared_ptr<ExchangeInterface> exchange,
                                        int priority = 0);
    
    ExchangeAdapterBuilder& with_failover_callback(
        typename ResilientExchangeAdapter<ExchangeInterface>::FailoverCallback callback);
    
    ExchangeAdapterBuilder& with_circuit_breaker_callback(
        typename ResilientExchangeAdapter<ExchangeInterface>::CircuitBreakerCallback callback);
    
    std::unique_ptr<ResilientExchangeAdapter<ExchangeInterface>> build();
    
private:
    FailoverConfig failover_config_;
    CircuitBreakerConfig circuit_config_;
    
    struct ExchangeInfo {
        std::string id;
        std::shared_ptr<ExchangeInterface> exchange;
        int priority;
    };
    
    std::vector<ExchangeInfo> exchanges_;
    typename ResilientExchangeAdapter<ExchangeInterface>::FailoverCallback failover_callback_;
    typename ResilientExchangeAdapter<ExchangeInterface>::CircuitBreakerCallback circuit_callback_;
};

template<typename ExchangeInterface>
std::unique_ptr<ResilientExchangeAdapter<ExchangeInterface>> 
create_resilient_adapter(const FailoverConfig& failover_config = FailoverConfig{},
                        const CircuitBreakerConfig& circuit_config = CircuitBreakerConfig{}) {
    return ExchangeAdapterBuilder<ExchangeInterface>()
        .with_failover_config(failover_config)
        .with_circuit_breaker_config(circuit_config)
        .build();
}

} // namespace exchange
} // namespace ats