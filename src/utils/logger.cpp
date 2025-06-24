#include "logger.hpp"
#include <iostream>
#include <fstream>
#include <filesystem>
#include <chrono>
#include <iomanip>
#include <mutex>

namespace ats {

std::unique_ptr<Logger> Logger::instance_ = nullptr;
std::mutex Logger::instance_mutex_;

void Logger::Initialize() {
    std::lock_guard<std::mutex> lock(instance_mutex_);
    if (!instance_) {
        instance_ = std::unique_ptr<Logger>(new Logger());
        
        // Create logs directory if it doesn't exist
        std::filesystem::create_directories("logs");
    }
}

Logger& Logger::Instance() {
    std::lock_guard<std::mutex> lock(instance_mutex_);
    if (!instance_) {
        instance_ = std::unique_ptr<Logger>(new Logger());
        
        // Create logs directory if it doesn't exist
        std::filesystem::create_directories("logs");
    }
    return *instance_;
}

void Logger::Log(LogLevel level, const std::string& message) {
    if (level < min_level_) {
        return;
    }
    
    static std::mutex log_mutex;
    std::lock_guard<std::mutex> lock(log_mutex);
    
    std::string timestamp = FormatTimestamp();
    std::string level_str = LevelToString(level);
    std::string log_line = "[" + timestamp + "] [" + level_str + "] " + message;
    
    // Console output
    if (console_output_) {
        if (level >= LogLevel::ERR) {
            std::cerr << log_line << std::endl;
        } else {
            std::cout << log_line << std::endl;
        }
    }
    
    // File output
    if (file_output_) {
        std::ofstream log_file(log_file_path_, std::ios::app);
        if (log_file.is_open()) {
            log_file << log_line << std::endl;
            log_file.close();
        }
    }
}

std::string Logger::FormatTimestamp() {
    auto now = std::chrono::system_clock::now();
    auto time_t = std::chrono::system_clock::to_time_t(now);
    auto ms = std::chrono::duration_cast<std::chrono::milliseconds>(
        now.time_since_epoch()) % 1000;
    
    // Thread-safe timestamp formatting
    std::tm tm_buf;
    std::tm* tm_ptr = nullptr;
    
#ifdef _WIN32
    // Windows: use localtime_s
    if (localtime_s(&tm_buf, &time_t) == 0) {
        tm_ptr = &tm_buf;
    }
#else
    // POSIX: use localtime_r
    tm_ptr = localtime_r(&time_t, &tm_buf);
#endif
    
    std::ostringstream oss;
    if (tm_ptr) {
        oss << std::put_time(tm_ptr, "%Y-%m-%d %H:%M:%S");
    } else {
        // Fallback: use UTC time if local time conversion fails
#ifdef _WIN32
        // Windows: use gmtime_s for UTC fallback
        if (gmtime_s(&tm_buf, &time_t) == 0) {
            oss << std::put_time(&tm_buf, "%Y-%m-%d %H:%M:%S");
            oss << " UTC";
        } else {
            // Last resort: use raw timestamp
            oss << time_t;
        }
#else
        // POSIX: use gmtime_r for UTC fallback
        tm_ptr = gmtime_r(&time_t, &tm_buf);
        if (tm_ptr) {
            oss << std::put_time(tm_ptr, "%Y-%m-%d %H:%M:%S");
            oss << " UTC";
        } else {
            // Last resort: use raw timestamp
            oss << time_t;
        }
#endif
    }
    
    oss << '.' << std::setfill('0') << std::setw(3) << ms.count();
    
    return oss.str();
}

std::string Logger::LevelToString(LogLevel level) {
    switch (level) {
        case LogLevel::TRACE:    return "TRACE";
        case LogLevel::DBG:      return "DEBUG";
        case LogLevel::INFO:     return "INFO";
        case LogLevel::WARNING:  return "WARN";
        case LogLevel::ERR:      return "ERROR";
        case LogLevel::CRITICAL: return "CRIT";
        default:                 return "UNKNOWN";
    }
}

} // namespace ats 
