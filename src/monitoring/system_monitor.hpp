#pragma once

#include <thread>
#include <atomic>
#include <chrono>
#include <mutex>

namespace ats {

struct SystemStats {
    double cpu_percent;
    double memory_percent;
    double memory_used_mb;
    double memory_total_mb;
    double disk_usage_percent;
    double temperature_celsius;
    double load_average_1min;
    long long uptime_seconds;
    long long timestamp;
    
    SystemStats() : cpu_percent(0.0), memory_percent(0.0), memory_used_mb(0.0), 
                   memory_total_mb(0.0), disk_usage_percent(0.0), temperature_celsius(0.0),
                   load_average_1min(0.0), uptime_seconds(0), timestamp(0) {}
};

class SystemMonitor {
private:
    std::thread monitor_thread_;
    std::atomic<bool> running_;
    std::chrono::seconds check_interval_;
    
    SystemStats current_stats_;
    mutable std::mutex stats_mutex_;
    
    // Alert thresholds
    double cpu_threshold_;
    double memory_threshold_;
    double temperature_threshold_;
    
public:
    SystemMonitor();
    ~SystemMonitor();
    
    // Lifecycle
    bool Initialize();
    void Start();
    void Stop();
    bool IsRunning() const { return running_.load(); }
    
    // Configuration
    void SetCheckInterval(std::chrono::seconds interval) { check_interval_ = interval; }
    void SetCpuThreshold(double threshold) { cpu_threshold_ = threshold; }
    void SetMemoryThreshold(double threshold) { memory_threshold_ = threshold; }
    void SetTemperatureThreshold(double threshold) { temperature_threshold_ = threshold; }
    
    // Statistics
    SystemStats GetCurrentMetrics() const;
    SystemStats GetCurrentStats() const;
    bool IsSystemHealthy() const;
    bool IsHealthy() const;
    std::string GetStatus() const;
    
    // Logging
    void LogSystemStats() const;
    void LogSystemInfo() const;
    
private:
    void MonitorLoop();
    void UpdateStats();
    
    // Platform-specific implementations
    double GetCpuUsage();
    double GetMemoryUsage();
    double GetMemoryUsedMB();
    double GetMemoryTotalMB();
    double GetDiskUsage();
    double GetTemperature();  // Raspberry Pi specific
    double GetLoadAverage();
    long long GetUptime();
    
    void CheckAlerts(const SystemStats& stats);
};

} // namespace ats 