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
        if (level >= LogLevel::ERROR) {
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
    
    std::ostringstream oss;
    oss << std::put_time(std::localtime(&time_t), "%Y-%m-%d %H:%M:%S");
    oss << '.' << std::setfill('0') << std::setw(3) << ms.count();
    
    return oss.str();
}

std::string Logger::LevelToString(LogLevel level) {
    switch (level) {
        case LogLevel::TRACE:    return "TRACE";
        case LogLevel::DEBUG:    return "DEBUG";
        case LogLevel::INFO:     return "INFO";
        case LogLevel::WARNING:  return "WARN";
        case LogLevel::ERROR:    return "ERROR";
        case LogLevel::CRITICAL: return "CRIT";
        default:                 return "UNKNOWN";
    }
}

} // namespace ats 
