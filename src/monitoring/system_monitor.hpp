#pragma once

#include <string>
#include <vector>
#include <chrono>
#include <thread>
#include <atomic>
#include <mutex>
#include <memory>

namespace ats {

struct SystemMetrics {
    double cpu_usage_percent;
    double memory_usage_percent;
    double disk_usage_percent;
    double cpu_temperature_celsius;
    long system_uptime_seconds;
    std::chrono::system_clock::time_point timestamp;
    
    SystemMetrics() 
        : cpu_usage_percent(0.0)
        , memory_usage_percent(0.0)
        , disk_usage_percent(0.0)
        , cpu_temperature_celsius(0.0)
        , system_uptime_seconds(0)
        , timestamp(std::chrono::system_clock::now()) {}
};

class SystemMonitor {
public:
    explicit SystemMonitor(int update_interval_ms = 5000);
    ~SystemMonitor();
    
    // Control methods
    void Start();
    void Stop();
    bool IsRunning() const;
    
    // Metrics access
    SystemMetrics GetCurrentMetrics() const;
    std::vector<SystemMetrics> GetMetricsHistory(size_t max_entries = 100) const;
    
    // Individual metric getters
    double GetCpuUsage() const;
    double GetMemoryUsage() const;
    double GetDiskUsage() const;
    double GetCpuTemperature() const;
    long GetSystemUptime() const;
    
    // Threshold management
    void SetCpuThreshold(double threshold) { cpu_threshold_ = threshold; }
    void SetMemoryThreshold(double threshold) { memory_threshold_ = threshold; }
    void SetDiskThreshold(double threshold) { disk_threshold_ = threshold; }
    void SetTemperatureThreshold(double threshold) { temperature_threshold_ = threshold; }
    
    // Alert checking
    std::vector<std::string> CheckThresholds() const;
    bool HasCriticalAlert() const;

private:
    // Configuration
    int update_interval_;
    
    // Threading
    std::atomic<bool> running_;
    std::thread monitoring_thread_;
    
    // Metrics storage
    mutable std::mutex history_mutex_;
    std::vector<SystemMetrics> metrics_history_;
    
    // Thresholds
    double cpu_threshold_;
    double memory_threshold_;
    double disk_threshold_;
    double temperature_threshold_;
    
    // Private methods
    void MonitoringLoop();
    SystemMetrics CollectMetrics() const;
    void UpdateMetricsHistory(const SystemMetrics& metrics);
    
    // Platform-specific methods
    double CalculateCpuUsage() const;
    double CalculateMemoryUsage() const;
    double CalculateDiskUsage() const;
    double ReadCpuTemperature() const;
    long ReadSystemUptime() const;
};

} // namespace ats 