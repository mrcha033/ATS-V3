#include "health_check.hpp"
#include "../utils/logger.hpp"

namespace ats {

HealthCheck::HealthCheck() = default;
HealthCheck::~HealthCheck() = default;

bool HealthCheck::Initialize() {
    LOG_INFO("Health Checker initialized");
    return true;
}

void HealthCheck::Start() {
    LOG_INFO("Health Checker started");
}

void HealthCheck::Stop() {
    LOG_INFO("Health Checker stopped");
}

void HealthCheck::RegisterComponent(const std::string& name, HealthCheckFunction check_func) {
    health_checks_[name] = check_func;
    LOG_INFO("Registered health check for component: {}", name);
}

void HealthCheck::UnregisterComponent(const std::string& name) {
    health_checks_.erase(name);
    LOG_INFO("Unregistered health check for component: {}", name);
}

HealthSummary HealthCheck::CheckHealth() const {
    HealthSummary summary;
    summary.overall_healthy = true;
    summary.timestamp = std::chrono::system_clock::now();
    
    for (const auto& check : health_checks_) {
        ComponentHealth component_health;
        component_health.name = check.first;
        component_health.is_healthy = check.second();
        component_health.last_check = summary.timestamp;
        
        if (!component_health.is_healthy) {
            summary.overall_healthy = false;
            component_health.error_message = "Component unhealthy";
        }
        
        summary.component_statuses.push_back(component_health);
    }
    
    return summary;
}

HealthSummary HealthCheck::CheckComponent(const std::string& name) const {
    HealthSummary summary;
    summary.overall_healthy = false;
    summary.timestamp = std::chrono::system_clock::now();
    
    auto it = health_checks_.find(name);
    if (it != health_checks_.end()) {
        ComponentHealth component_health;
        component_health.name = name;
        component_health.is_healthy = it->second();
        component_health.last_check = summary.timestamp;
        
        if (!component_health.is_healthy) {
            component_health.error_message = "Component unhealthy";
        }
        
        summary.component_statuses.push_back(component_health);
        summary.overall_healthy = component_health.is_healthy;
    }
    
    return summary;
}

bool HealthCheck::IsSystemHealthy() const {
    auto summary = CheckHealth();
    return summary.overall_healthy;
}

std::string HealthCheck::GetHealthSummary() const {
    auto health_summary = CheckHealth();
    std::string result = "System Health: " + std::string(health_summary.overall_healthy ? "HEALTHY" : "UNHEALTHY");
    
    for (const auto& component : health_summary.component_statuses) {
        result += "\n  " + component.name + ": " + 
                  (component.is_healthy ? "HEALTHY" : "UNHEALTHY");
        if (!component.is_healthy && !component.error_message.empty()) {
            result += " (" + component.error_message + ")";
        }
    }
    
    return result;
}

void HealthCheck::LogHealthStatus() const {
    auto summary = CheckHealth();
    LOG_INFO("=== Health Status ===");
    LOG_INFO("Overall: {}", summary.overall_healthy ? "HEALTHY" : "UNHEALTHY");
    
    for (const auto& component : summary.component_statuses) {
        if (component.is_healthy) {
            LOG_INFO("{}: HEALTHY", component.name);
        } else {
            LOG_WARNING("{}: UNHEALTHY - {}", component.name, component.error_message);
        }
    }
}

HealthSummary HealthCheck::GetOverallStatus() const {
    return CheckHealth();
}

bool HealthCheck::CheckSystem() {
    return IsSystemHealthy();
}

// Stub implementations for individual health checks
HealthCheckResult HealthCheck::CheckNetworkConnectivity() {
    return HealthCheckResult("NetworkConnectivity", HealthStatus::HEALTHY, "Network is reachable");
}

HealthCheckResult HealthCheck::CheckExchangeConnections() {
    return HealthCheckResult("ExchangeConnections", HealthStatus::HEALTHY, "Exchange connections are healthy");
}

HealthCheckResult HealthCheck::CheckSystemResources() {
    return HealthCheckResult("SystemResources", HealthStatus::HEALTHY, "System resources are adequate");
}

HealthCheckResult HealthCheck::CheckDiskSpace() {
    return HealthCheckResult("DiskSpace", HealthStatus::HEALTHY, "Disk space is sufficient");
}

HealthCheckResult HealthCheck::CheckMemoryUsage() {
    return HealthCheckResult("MemoryUsage", HealthStatus::HEALTHY, "Memory usage is normal");
}

HealthCheckResult HealthCheck::CheckCpuTemperature() {
    return HealthCheckResult("CpuTemperature", HealthStatus::HEALTHY, "CPU temperature is normal");
}

HealthCheckResult HealthCheck::CheckDatabaseConnection() {
    return HealthCheckResult("DatabaseConnection", HealthStatus::HEALTHY, "Database connection is active");
}

HealthCheckResult HealthCheck::CheckLogFileAccess() {
    return HealthCheckResult("LogFileAccess", HealthStatus::HEALTHY, "Log file is accessible");
}

std::vector<HealthCheckResult> HealthCheck::GetCriticalResults() const {
    std::vector<HealthCheckResult> results;
    // TODO: Filter critical results from check_results_
    return results;
}

std::vector<HealthCheckResult> HealthCheck::GetWarningResults() const {
    std::vector<HealthCheckResult> results;
    // TODO: Filter warning results from check_results_
    return results;
}

void HealthCheck::AddResult(const HealthCheckResult& result) {
    check_results_.push_back(result);
}

void HealthCheck::CleanupOldResults() {
    // TODO: Remove old results based on timestamp
}

std::string HealthCheck::StatusToString(HealthStatus status) const {
    switch (status) {
        case HealthStatus::HEALTHY: return "HEALTHY";
        case HealthStatus::WARNING: return "WARNING";
        case HealthStatus::CRITICAL: return "CRITICAL";
        case HealthStatus::UNKNOWN: return "UNKNOWN";
        default: return "UNKNOWN";
    }
}

bool HealthCheck::PingHost(const std::string& host, int timeout_ms) {
    // TODO: Implement ping functionality
    return true;
}

bool HealthCheck::CheckFileWritable(const std::string& filepath) {
    // TODO: Implement file write check
    return true;
}

double HealthCheck::GetAvailableDiskSpaceGB(const std::string& path) {
    // TODO: Implement disk space check
    return 100.0; // Placeholder
}

} // namespace ats 