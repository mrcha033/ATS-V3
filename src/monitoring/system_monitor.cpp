#include "system_monitor.hpp"
#include "../utils/logger.hpp"

namespace ats {

SystemMonitor::SystemMonitor() = default;
SystemMonitor::~SystemMonitor() = default;

bool SystemMonitor::Initialize() {
    LOG_INFO("System Monitor initialized");
    return true;
}

void SystemMonitor::Start() {
    LOG_INFO("System Monitor started");
}

void SystemMonitor::Stop() {
    LOG_INFO("System Monitor stopped");
}

SystemStats SystemMonitor::GetCurrentMetrics() const {
    SystemStats stats;
    stats.cpu_percent = 25.0;    // Placeholder values
    stats.memory_percent = 45.0;
    stats.disk_usage_percent = 60.0;
    stats.temperature_celsius = 55.0;
    stats.uptime_seconds = 86400;
    stats.timestamp = std::chrono::duration_cast<std::chrono::milliseconds>(
        std::chrono::system_clock::now().time_since_epoch()).count();
    return stats;
}

SystemStats SystemMonitor::GetCurrentStats() const {
    return GetCurrentMetrics();
}

bool SystemMonitor::IsSystemHealthy() const {
    return IsHealthy();
}

bool SystemMonitor::IsHealthy() const {
    auto stats = GetCurrentMetrics();
    return stats.cpu_percent < 90.0 && 
           stats.memory_percent < 90.0 &&
           stats.temperature_celsius < 80.0;
}

std::string SystemMonitor::GetStatus() const {
    return IsHealthy() ? "HEALTHY" : "UNHEALTHY";
}

void SystemMonitor::LogSystemInfo() const {
    auto stats = GetCurrentMetrics();
    LOG_INFO("=== System Metrics ===");
    LOG_INFO("CPU Usage: {:.1f}%", stats.cpu_percent);
    LOG_INFO("Memory Usage: {:.1f}%", stats.memory_percent);
    LOG_INFO("Disk Usage: {:.1f}%", stats.disk_usage_percent);
    LOG_INFO("Temperature: {:.1f}°C", stats.temperature_celsius);
    LOG_INFO("Uptime: {} seconds", stats.uptime_seconds);
}

void SystemMonitor::LogSystemStats() const {
    LogSystemInfo();
}

} // namespace ats 