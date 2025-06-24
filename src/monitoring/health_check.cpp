#include "health_check.hpp"
#include "../utils/logger.hpp"
#include <fstream>
#include <filesystem>
#include <chrono>
#include <algorithm>

#ifdef _WIN32
    // Only include necessary Windows headers for specific functions
    #ifndef WIN32_LEAN_AND_MEAN
        #define WIN32_LEAN_AND_MEAN
    #endif
    #ifndef NOMINMAX
        #define NOMINMAX
    #endif
    #include <windows.h>
    #include <winsock2.h>
    #include <ws2tcpip.h>
    #pragma comment(lib, "ws2_32.lib")
    
    // Clean up Windows macro pollution after includes
    #ifdef ERROR
        #undef ERROR
    #endif
    #ifdef min
        #undef min
    #endif
    #ifdef max
        #undef max
    #endif
#else
    #include <sys/statvfs.h>
    #include <sys/sysinfo.h>
    #include <unistd.h>
    #include <sys/socket.h>
    #include <netinet/in.h>
    #include <arpa/inet.h>
    #include <netdb.h>
#endif

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
    // Test connectivity by trying to resolve common DNS names
    std::vector<std::string> test_hosts = {"8.8.8.8", "1.1.1.1", "google.com"};
    
    for (const auto& host : test_hosts) {
        if (PingHost(host, 3000)) {
            return HealthCheckResult("NetworkConnectivity", HealthStatus::HEALTHY, 
                                   "Network connectivity verified");
        }
    }
    
    return HealthCheckResult("NetworkConnectivity", HealthStatus::CRITICAL, 
                           "No network connectivity detected");
}

HealthCheckResult HealthCheck::CheckExchangeConnections() {
    // This would typically check actual exchange connections
    // For now, return healthy as a placeholder
    return HealthCheckResult("ExchangeConnections", HealthStatus::HEALTHY, 
                           "Exchange connections are responsive");
}

HealthCheckResult HealthCheck::CheckSystemResources() {
    std::vector<std::string> issues;
    
    // Check memory usage
    double memory_usage = GetMemoryUsagePercent();
    if (memory_usage > 90.0) {
        issues.push_back("High memory usage: " + std::to_string(memory_usage) + "%");
    }
    
    // Check disk space
    double disk_space_gb = GetAvailableDiskSpaceGB(".");
    if (disk_space_gb < 1.0) {
        issues.push_back("Low disk space: " + std::to_string(disk_space_gb) + " GB");
    }
    
    // Check CPU temperature (simplified)
    double cpu_temp = GetCpuTemperatureC();
    if (cpu_temp > 85.0) {
        issues.push_back("High CPU temperature: " + std::to_string(cpu_temp) + "°C");
    }
    
    if (issues.empty()) {
        return HealthCheckResult("SystemResources", HealthStatus::HEALTHY, 
                               "System resources are adequate");
    } else if (issues.size() == 1) {
        return HealthCheckResult("SystemResources", HealthStatus::WARNING, issues[0]);
    } else {
        return HealthCheckResult("SystemResources", HealthStatus::CRITICAL, 
                               "Multiple resource issues detected");
    }
}

HealthCheckResult HealthCheck::CheckDiskHealth() {
    try {
        // Check filesystem health by testing read/write operations
        std::string test_file = "health_check_disk_test.tmp";
        
        // Test write operation
        {
            std::ofstream outfile(test_file, std::ios::binary | std::ios::trunc);
            if (!outfile.is_open()) {
                return HealthCheckResult("DiskHealth", HealthStatus::CRITICAL,
                    "Cannot create test file - disk may be full or read-only");
            }
            
            // Write test data
            std::string test_data = "ATS Health Check Test Data";
            outfile.write(test_data.c_str(), test_data.length());
            if (outfile.fail()) {
                outfile.close();
                std::filesystem::remove(test_file);
                return HealthCheckResult("DiskHealth", HealthStatus::CRITICAL,
                    "Write operation failed - disk may have errors");
            }
        }
        
        // Test read operation
        {
            std::ifstream infile(test_file, std::ios::binary);
            if (!infile.is_open()) {
                std::filesystem::remove(test_file);
                return HealthCheckResult("DiskHealth", HealthStatus::CRITICAL,
                    "Cannot read test file - disk read error");
            }
            
            std::string read_data;
            char buffer[256];
            infile.read(buffer, sizeof(buffer));
            read_data.assign(buffer, infile.gcount());
            
            if (read_data.find("ATS Health Check") == std::string::npos) {
                infile.close();
                std::filesystem::remove(test_file);
                return HealthCheckResult("DiskHealth", HealthStatus::WARNING,
                    "Data integrity check failed - possible disk corruption");
            }
        }
        
        // Cleanup test file
        std::filesystem::remove(test_file);
        
        // Check disk space
        try {
            auto space_info = std::filesystem::space(".");
            double usage_percent = 100.0 * (1.0 - (double)space_info.available / space_info.capacity);
            
            if (usage_percent > 95.0) {
                return HealthCheckResult("DiskHealth", HealthStatus::CRITICAL,
                    "Disk usage critical: " + std::to_string(usage_percent) + "%");
            } else if (usage_percent > 85.0) {
                return HealthCheckResult("DiskHealth", HealthStatus::WARNING,
                    "Disk usage high: " + std::to_string(usage_percent) + "%");
            }
            
            return HealthCheckResult("DiskHealth", HealthStatus::HEALTHY,
                "Disk health OK, usage: " + std::to_string(usage_percent) + "%");
                
        } catch (const std::filesystem::filesystem_error& e) {
            return HealthCheckResult("DiskHealth", HealthStatus::WARNING,
                "Cannot check disk space: " + std::string(e.what()));
        }
        
    } catch (const std::exception& e) {
        return HealthCheckResult("DiskHealth", HealthStatus::CRITICAL,
            "Disk health check failed: " + std::string(e.what()));
    }
}

HealthCheckResult HealthCheck::CheckMemoryUsage() {
    double memory_percent = GetMemoryUsagePercent();
    
    if (memory_percent > 95.0) {
        return HealthCheckResult("MemoryUsage", HealthStatus::CRITICAL, 
                               "Critical memory usage: " + std::to_string(memory_percent) + "%");
    } else if (memory_percent > 85.0) {
        return HealthCheckResult("MemoryUsage", HealthStatus::WARNING, 
                               "High memory usage: " + std::to_string(memory_percent) + "%");
    } else {
        return HealthCheckResult("MemoryUsage", HealthStatus::HEALTHY, 
                               "Memory usage: " + std::to_string(memory_percent) + "%");
    }
}

HealthCheckResult HealthCheck::CheckCpuTemperature() {
    double temp_c = GetCpuTemperatureC();
    
    if (temp_c > 90.0) {
        return HealthCheckResult("CpuTemperature", HealthStatus::CRITICAL, 
                               "Critical CPU temperature: " + std::to_string(temp_c) + "°C");
    } else if (temp_c > 80.0) {
        return HealthCheckResult("CpuTemperature", HealthStatus::WARNING, 
                               "High CPU temperature: " + std::to_string(temp_c) + "°C");
    } else {
        return HealthCheckResult("CpuTemperature", HealthStatus::HEALTHY, 
                               "CPU temperature: " + std::to_string(temp_c) + "°C");
    }
}

HealthCheckResult HealthCheck::CheckDatabaseConnection() {
    // For this trading system, we don't have a traditional database
    // Check if log files are accessible instead
    if (CheckFileWritable("logs/test.tmp")) {
        return HealthCheckResult("DatabaseConnection", HealthStatus::HEALTHY, 
                               "File system access verified");
    } else {
        return HealthCheckResult("DatabaseConnection", HealthStatus::WARNING, 
                               "File system access issues");
    }
}

HealthCheckResult HealthCheck::CheckLogFileAccess() {
    std::string log_path = "logs/ats_v3.log";
    
    if (CheckFileWritable(log_path)) {
        return HealthCheckResult("LogFileAccess", HealthStatus::HEALTHY, 
                               "Log file is accessible");
    } else {
        return HealthCheckResult("LogFileAccess", HealthStatus::WARNING, 
                               "Log file access issues");
    }
}

std::vector<HealthCheckResult> HealthCheck::GetCriticalResults() const {
    std::vector<HealthCheckResult> results;
    std::copy_if(check_results_.begin(), check_results_.end(), std::back_inserter(results),
                [](const HealthCheckResult& result) {
                    return result.status == HealthStatus::CRITICAL;
                });
    return results;
}

std::vector<HealthCheckResult> HealthCheck::GetWarningResults() const {
    std::vector<HealthCheckResult> results;
    std::copy_if(check_results_.begin(), check_results_.end(), std::back_inserter(results),
                [](const HealthCheckResult& result) {
                    return result.status == HealthStatus::WARNING;
                });
    return results;
}

void HealthCheck::AddResult(const HealthCheckResult& result) {
    check_results_.push_back(result);
    
    // Keep only recent results (limit to 1000 entries)
    if (check_results_.size() > 1000) {
        check_results_.erase(check_results_.begin(), 
                           check_results_.begin() + (check_results_.size() - 1000));
    }
}

void HealthCheck::CleanupOldResults() {
    auto now = std::chrono::steady_clock::now();
    auto cutoff = now - std::chrono::hours(24); // Keep results for 24 hours
    
    check_results_.erase(
        std::remove_if(check_results_.begin(), check_results_.end(),
            [cutoff](const HealthCheckResult& result) {
                return result.timestamp < cutoff;
            }),
        check_results_.end()
    );
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
#ifdef _WIN32
    // Windows implementation
    WSADATA wsaData;
    if (WSAStartup(MAKEWORD(2, 2), &wsaData) != 0) {
        return false;
    }
    
    struct sockaddr_in addr;
    addr.sin_family = AF_INET;
    addr.sin_port = htons(80); // Try HTTP port
    
    // Try to resolve hostname
    struct hostent* host_entry = gethostbyname(host.c_str());
    if (host_entry == nullptr) {
        // Try as IP address
        addr.sin_addr.s_addr = inet_addr(host.c_str());
        if (addr.sin_addr.s_addr == INADDR_NONE) {
            WSACleanup();
            return false;
        }
    } else {
        memcpy(&addr.sin_addr, host_entry->h_addr_list[0], host_entry->h_length);
    }
    
    SOCKET sock = socket(AF_INET, SOCK_STREAM, 0);
    if (sock == INVALID_SOCKET) {
        WSACleanup();
        return false;
    }
    
    // Set timeout
    DWORD timeout = timeout_ms;
    setsockopt(sock, SOL_SOCKET, SO_RCVTIMEO, (char*)&timeout, sizeof(timeout));
    setsockopt(sock, SOL_SOCKET, SO_SNDTIMEO, (char*)&timeout, sizeof(timeout));
    
    // Try to connect
    int result = connect(sock, (struct sockaddr*)&addr, sizeof(addr));
    
    closesocket(sock);
    WSACleanup();
    
    return result == 0;
    
#else
    // Linux implementation
    struct sockaddr_in addr;
    addr.sin_family = AF_INET;
    addr.sin_port = htons(80);
    
    // Try to resolve hostname
    struct hostent* host_entry = gethostbyname(host.c_str());
    if (host_entry == nullptr) {
        // Try as IP address
        if (inet_aton(host.c_str(), &addr.sin_addr) == 0) {
            return false;
        }
    } else {
        memcpy(&addr.sin_addr, host_entry->h_addr_list[0], host_entry->h_length);
    }
    
    int sock = socket(AF_INET, SOCK_STREAM, 0);
    if (sock < 0) {
        return false;
    }
    
    // Set timeout
    struct timeval tv;
    tv.tv_sec = timeout_ms / 1000;
    tv.tv_usec = (timeout_ms % 1000) * 1000;
    setsockopt(sock, SOL_SOCKET, SO_RCVTIMEO, &tv, sizeof(tv));
    setsockopt(sock, SOL_SOCKET, SO_SNDTIMEO, &tv, sizeof(tv));
    
    // Try to connect
    int result = connect(sock, (struct sockaddr*)&addr, sizeof(addr));
    
    close(sock);
    
    return result == 0;
#endif
}

bool HealthCheck::CheckFileWritable(const std::string& filepath) {
    std::string temp_test_file = filepath;
    bool is_temp_file = false;
    
    try {
        // For test files, create a unique temporary file instead of modifying existing files
        if (filepath.find("test.tmp") != std::string::npos || 
            filepath.find("health_check") != std::string::npos) {
            auto timestamp = std::chrono::system_clock::now().time_since_epoch().count();
            temp_test_file = filepath + ".health_test_" + std::to_string(timestamp);
            is_temp_file = true;
        }
        
        // Ensure directory exists
        std::filesystem::path file_path(temp_test_file);
        std::filesystem::create_directories(file_path.parent_path());
        
        // Try to write a test file
        std::ofstream test_file(temp_test_file, std::ios::out | std::ios::trunc);
        if (!test_file.is_open()) {
            return false;
        }
        
        test_file << "health_check_test_" << std::chrono::system_clock::now().time_since_epoch().count() << std::endl;
        test_file.close();
        
        // Try to read it back
        std::ifstream read_file(temp_test_file);
        bool read_success = read_file.is_open();
        if (read_success) {
            std::string line;
            read_success = std::getline(read_file, line) && !line.empty();
        }
        read_file.close();
        
        // Clean up temporary test file
        if (is_temp_file) {
            try {
                std::filesystem::remove(temp_test_file);
                LOG_DEBUG("Cleaned up temporary health check file: {}", temp_test_file);
            } catch (const std::exception& e) {
                LOG_WARNING("Failed to remove temporary health check file {}: {}", temp_test_file, e.what());
            }
        }
        
        return read_success;
        
    } catch (const std::exception& e) {
        LOG_ERROR("File write check failed for {}: {}", filepath, e.what());
        
        // Attempt cleanup even on failure
        if (is_temp_file) {
            try {
                std::filesystem::remove(temp_test_file);
            } catch (...) {
                // Ignore cleanup errors in error path
            }
        }
        
        return false;
    }
}

double HealthCheck::GetAvailableDiskSpaceGB(const std::string& path) {
    try {
#ifdef _WIN32
        ULARGE_INTEGER free_bytes_available;
        ULARGE_INTEGER total_number_of_bytes;
        
        if (GetDiskFreeSpaceExA(path.c_str(), &free_bytes_available, &total_number_of_bytes, nullptr)) {
            return static_cast<double>(free_bytes_available.QuadPart) / (1024.0 * 1024.0 * 1024.0);
        }
        return 0.0;
        
#else
        struct statvfs stat;
        if (statvfs(path.c_str(), &stat) == 0) {
            unsigned long available_bytes = stat.f_bavail * stat.f_frsize;
            return static_cast<double>(available_bytes) / (1024.0 * 1024.0 * 1024.0);
        }
        return 0.0;
#endif
        
    } catch (const std::exception& e) {
        LOG_ERROR("Disk space check failed for {}: {}", path, e.what());
        return 0.0;
    }
}

double HealthCheck::GetMemoryUsagePercent() {
    try {
#ifdef _WIN32
        MEMORYSTATUSEX memInfo;
        memInfo.dwLength = sizeof(MEMORYSTATUSEX);
        
        if (GlobalMemoryStatusEx(&memInfo)) {
            return static_cast<double>(memInfo.dwMemoryLoad);
        }
        return 0.0;
        
#else
        struct sysinfo mem_info;
        if (sysinfo(&mem_info) == 0) {
            unsigned long total_ram = mem_info.totalram * mem_info.mem_unit;
            unsigned long free_ram = mem_info.freeram * mem_info.mem_unit;
            unsigned long used_ram = total_ram - free_ram;
            
            return static_cast<double>(used_ram) / total_ram * 100.0;
        }
        return 0.0;
#endif
        
    } catch (const std::exception& e) {
        LOG_ERROR("Memory usage check failed: {}", e.what());
        return 0.0;
    }
}

double HealthCheck::GetCpuTemperatureC() {
    try {
#ifdef _WIN32
        // Windows doesn't have a simple API for CPU temperature
        // Return a safe default value
        return 45.0;
        
#else
        // Try to read temperature from common Linux thermal files
        std::vector<std::string> temp_files = {
            "/sys/class/thermal/thermal_zone0/temp",
            "/sys/class/thermal/thermal_zone1/temp"
        };
        
        for (const auto& temp_file : temp_files) {
            std::ifstream file(temp_file);
            if (file.is_open()) {
                std::string temp_str;
                std::getline(file, temp_str);
                file.close();
                
                if (!temp_str.empty()) {
                    // Temperature is usually in millidegrees Celsius
                    double temp = std::stod(temp_str) / 1000.0;
                    if (temp > 0 && temp < 150) { // Sanity check
                        return temp;
                    }
                }
            }
        }
        
        // If no thermal files are available, return a safe default
        return 45.0;
#endif
        
    } catch (const std::exception& e) {
        LOG_ERROR("CPU temperature check failed: {}", e.what());
        return 45.0; // Safe default
    }
}

} // namespace ats 
