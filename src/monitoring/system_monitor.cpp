#include "system_monitor.hpp"
#include "../utils/logger.hpp"
#include <fstream>
#include <sstream>
#include <filesystem>

#ifdef _WIN32
#include <windows.h>
#include <pdh.h>
#include <psapi.h>
#elif defined(__linux__)
#include <sys/sysinfo.h>
#include <sys/statvfs.h>
#include <unistd.h>
#endif

namespace ats {

SystemMonitor::SystemMonitor() 
    : running_(false), update_interval_(std::chrono::seconds(5)) {
}

SystemMonitor::~SystemMonitor() {
    Stop();
}

bool SystemMonitor::Initialize() {
    LOG_INFO("System Monitor initialized");
    return true;
}

void SystemMonitor::Start() {
    if (running_.load()) {
        LOG_WARNING("System Monitor is already running");
        return;
    }
    
    running_ = true;
    monitor_thread_ = std::thread(&SystemMonitor::MonitoringLoop, this);
    LOG_INFO("System Monitor started");
}

void SystemMonitor::Stop() {
    if (!running_.load()) {
        return;
    }
    
    running_ = false;
    
    if (monitor_thread_.joinable()) {
        monitor_thread_.join();
    }
    
    LOG_INFO("System Monitor stopped");
}

SystemStats SystemMonitor::GetCurrentMetrics() const {
    SystemStats stats;
    
    // Get current timestamp
    stats.timestamp = std::chrono::duration_cast<std::chrono::milliseconds>(
        std::chrono::system_clock::now().time_since_epoch()).count();
    
    // Get CPU usage
    stats.cpu_percent = GetCpuUsage();
    
    // Get memory usage
    stats.memory_percent = GetMemoryUsage();
    
    // Get disk usage
    stats.disk_usage_percent = GetDiskUsage();
    
    // Get temperature (if available)
    stats.temperature_celsius = GetCpuTemperature();
    
    // Get uptime
    stats.uptime_seconds = GetSystemUptime();
    
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

void SystemMonitor::MonitoringLoop() {
    while (running_.load()) {
        try {
            // Collect current metrics
            SystemStats stats = GetCurrentMetrics();
            
            // Store in history (keep last 1000 readings)
            {
                std::lock_guard<std::mutex> lock(history_mutex_);
                metrics_history_.push_back(stats);
                if (metrics_history_.size() > 1000) {
                    metrics_history_.erase(metrics_history_.begin());
                }
            }
            
            // Check for critical conditions
            if (stats.cpu_percent > 95.0) {
                LOG_WARNING("Critical CPU usage: {:.1f}%", stats.cpu_percent);
            }
            
            if (stats.memory_percent > 95.0) {
                LOG_WARNING("Critical memory usage: {:.1f}%", stats.memory_percent);
            }
            
            if (stats.temperature_celsius > 85.0) {
                LOG_WARNING("Critical CPU temperature: {:.1f}°C", stats.temperature_celsius);
            }
            
            // Sleep until next update
            std::this_thread::sleep_for(update_interval_);
            
        } catch (const std::exception& e) {
            LOG_ERROR("Error in monitoring loop: {}", e.what());
            std::this_thread::sleep_for(std::chrono::seconds(1));
        }
    }
}

double SystemMonitor::GetCpuUsage() const {
#ifdef _WIN32
    // Windows implementation
    static FILETIME lastIdleTime, lastKernelTime, lastUserTime;
    static bool first_call = true;
    
    FILETIME idleTime, kernelTime, userTime;
    if (!GetSystemTimes(&idleTime, &kernelTime, &userTime)) {
        return 0.0;
    }
    
    if (first_call) {
        lastIdleTime = idleTime;
        lastKernelTime = kernelTime;
        lastUserTime = userTime;
        first_call = false;
        return 0.0;
    }
    
    auto FileTimeToInt64 = [](const FILETIME& ft) {
        return ((uint64_t)ft.dwHighDateTime << 32) | ft.dwLowDateTime;
    };
    
    uint64_t idle = FileTimeToInt64(idleTime) - FileTimeToInt64(lastIdleTime);
    uint64_t kernel = FileTimeToInt64(kernelTime) - FileTimeToInt64(lastKernelTime);
    uint64_t user = FileTimeToInt64(userTime) - FileTimeToInt64(lastUserTime);
    
    uint64_t total = kernel + user;
    
    lastIdleTime = idleTime;
    lastKernelTime = kernelTime;
    lastUserTime = userTime;
    
    if (total == 0) return 0.0;
    return (double)(total - idle) * 100.0 / total;
    
#elif defined(__linux__)
    // Linux implementation
    std::ifstream file("/proc/stat");
    if (!file.is_open()) return 0.0;
    
    std::string line;
    std::getline(file, line);
    
    std::istringstream iss(line);
    std::string cpu;
    long user, nice, system, idle, iowait, irq, softirq, steal;
    
    iss >> cpu >> user >> nice >> system >> idle >> iowait >> irq >> softirq >> steal;
    
    static long prev_idle = 0, prev_total = 0;
    static bool first_call = true;
    
    long total = user + nice + system + idle + iowait + irq + softirq + steal;
    
    if (first_call) {
        prev_idle = idle;
        prev_total = total;
        first_call = false;
        return 0.0;
    }
    
    long diff_idle = idle - prev_idle;
    long diff_total = total - prev_total;
    
    prev_idle = idle;
    prev_total = total;
    
    if (diff_total == 0) return 0.0;
    return (double)(diff_total - diff_idle) * 100.0 / diff_total;
    
#else
    // Fallback for other systems
    return 25.0; // Placeholder value
#endif
}

double SystemMonitor::GetMemoryUsage() const {
#ifdef _WIN32
    MEMORYSTATUSEX memInfo;
    memInfo.dwLength = sizeof(MEMORYSTATUSEX);
    GlobalMemoryStatusEx(&memInfo);
    return (double)memInfo.dwMemoryLoad;
    
#elif defined(__linux__)
    struct sysinfo si;
    if (sysinfo(&si) != 0) return 0.0;
    
    long total_ram = si.totalram * si.mem_unit;
    long free_ram = si.freeram * si.mem_unit;
    long used_ram = total_ram - free_ram;
    
    return (double)used_ram * 100.0 / total_ram;
    
#else
    return 45.0; // Placeholder value
#endif
}

double SystemMonitor::GetDiskUsage() const {
#ifdef _WIN32
    ULARGE_INTEGER freeBytesAvailable, totalNumberOfBytes;
    if (GetDiskFreeSpaceExA("C:\\", &freeBytesAvailable, &totalNumberOfBytes, NULL)) {
        uint64_t used = totalNumberOfBytes.QuadPart - freeBytesAvailable.QuadPart;
        return (double)used * 100.0 / totalNumberOfBytes.QuadPart;
    }
    return 0.0;
    
#elif defined(__linux__)
    struct statvfs stat;
    if (statvfs("/", &stat) != 0) return 0.0;
    
    uint64_t total = stat.f_blocks * stat.f_frsize;
    uint64_t free = stat.f_bavail * stat.f_frsize;
    uint64_t used = total - free;
    
    return (double)used * 100.0 / total;
    
#else
    return 60.0; // Placeholder value
#endif
}

double SystemMonitor::GetCpuTemperature() const {
#ifdef __linux__
    // Try to read from thermal zone
    std::ifstream temp_file("/sys/class/thermal/thermal_zone0/temp");
    if (temp_file.is_open()) {
        int temp_millidegrees;
        temp_file >> temp_millidegrees;
        return temp_millidegrees / 1000.0;
    }
#endif
    
    // Fallback - estimate based on CPU usage
    double cpu_usage = GetCpuUsage();
    return 40.0 + (cpu_usage * 0.4); // Base temp + usage-based increase
}

long SystemMonitor::GetSystemUptime() const {
#ifdef _WIN32
    return GetTickCount64() / 1000;
    
#elif defined(__linux__)
    struct sysinfo si;
    if (sysinfo(&si) == 0) {
        return si.uptime;
    }
    return 0;
    
#else
    return 86400; // Placeholder: 1 day
#endif
}

} // namespace ats 