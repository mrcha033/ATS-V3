#pragma once

#include <string>
#include <vector>
#include <chrono>
#include <unordered_map>
#include <functional>

namespace ats {

enum class HealthStatus {
    HEALTHY,
    WARNING,
    CRITICAL,
    UNKNOWN
};

struct HealthCheckResult {
    std::string component;
    HealthStatus status;
    std::string message;
    std::chrono::steady_clock::time_point timestamp;
    
    HealthCheckResult(const std::string& comp, HealthStatus stat, const std::string& msg)
        : component(comp), status(stat), message(msg), timestamp(std::chrono::steady_clock::now()) {}
};

// Health check function type
using HealthCheckFunction = std::function<bool()>;

// Component health information
struct ComponentHealth {
    std::string name;
    bool is_healthy;
    std::string error_message;
    std::chrono::system_clock::time_point last_check;
};

// Overall health status
struct HealthSummary {
    bool overall_healthy;
    std::chrono::system_clock::time_point timestamp;
    std::vector<ComponentHealth> component_statuses;
};

class HealthCheck {
private:
    std::vector<HealthCheckResult> check_results_;
    std::chrono::seconds check_interval_;
    std::unordered_map<std::string, HealthCheckFunction> health_checks_;
    
public:
    HealthCheck();
    ~HealthCheck();
    
    // Lifecycle
    bool Initialize();
    void Start();
    void Stop();
    
    // Component registration
    void RegisterComponent(const std::string& name, HealthCheckFunction check_func);
    void UnregisterComponent(const std::string& name);
    
    // Health checking
    HealthSummary CheckHealth() const;
    HealthSummary CheckComponent(const std::string& name) const;
    bool IsSystemHealthy() const;
    
    // Main health check function
    bool CheckSystem();
    
    // Individual health checks
    HealthCheckResult CheckNetworkConnectivity();
    HealthCheckResult CheckExchangeConnections();
    HealthCheckResult CheckSystemResources();
    HealthCheckResult CheckDiskSpace();
    HealthCheckResult CheckMemoryUsage();
    HealthCheckResult CheckCpuTemperature();
    HealthCheckResult CheckDatabaseConnection();
    HealthCheckResult CheckLogFileAccess();
    
    // Results management
    std::vector<HealthCheckResult> GetAllResults() const { return check_results_; }
    std::vector<HealthCheckResult> GetCriticalResults() const;
    std::vector<HealthCheckResult> GetWarningResults() const;
    
    // Overall health status
    HealthSummary GetOverallStatus() const;
    std::string GetHealthSummary() const;
    void LogHealthStatus() const;
    
    // Configuration
    void SetCheckInterval(std::chrono::seconds interval) { check_interval_ = interval; }
    
private:
    void AddResult(const HealthCheckResult& result);
    void CleanupOldResults();
    std::string StatusToString(HealthStatus status) const;
    
    // Helper functions for specific checks
    bool PingHost(const std::string& host, int timeout_ms = 5000);
    bool CheckFileWritable(const std::string& filepath);
    double GetAvailableDiskSpaceGB(const std::string& path);
    double GetMemoryUsagePercent();
    double GetCpuTemperatureC();
};

} // namespace ats 