#pragma once

// Prevent Windows macro pollution
#if defined(_WIN32) && !defined(WIN32_LEAN_AND_MEAN)
    #define WIN32_LEAN_AND_MEAN
#endif
#if defined(_WIN32) && !defined(NOMINMAX)
    #define NOMINMAX
#endif

#include <string>
#include <memory>
#include <sstream>
#include <type_traits>
#include <vector>
#include <mutex>
#include <fstream>
#include <chrono>
#include <iostream>
#include <iomanip>

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

namespace ats {

enum class LogLevel : int {
    TRACE = 0,
    DBG = 1,      // Renamed from DEBUG to avoid macro conflicts
    INFO = 2,
    WARNING = 3,
    ERR = 4,      // Renamed from ERROR to avoid macro conflicts
    CRITICAL = 5
};

class Logger {
private:
    static std::unique_ptr<Logger> instance_;
    static std::mutex instance_mutex_;
    LogLevel min_level_ = LogLevel::INFO;
    bool console_output_ = true;
    bool file_output_ = true;
    std::string log_file_path_ = "logs/ats_v3.log";
    
    // Persistent file stream for better performance
    mutable std::ofstream log_file_stream_;
    mutable std::mutex log_mutex_;
    
    Logger() = default;
    
public:
    static void Initialize();
    static Logger& Instance();
    
    ~Logger();
    
    void SetLevel(LogLevel level) noexcept { min_level_ = level; }
    void SetConsoleOutput(bool enabled) noexcept { console_output_ = enabled; }
    void SetFileOutput(bool enabled) noexcept { file_output_ = enabled; }
    void SetLogFile(const std::string& path);
    
    void Log(LogLevel level, const std::string& message);
    
    template<typename... Args>
    void Log(LogLevel level, const std::string& format, Args&&... args) {
        if (level < min_level_) return;
        
        std::string formatted = FormatMessage(format, std::forward<Args>(args)...);
        Log(level, formatted);
    }
    
private:
    // Optimized formatting using ostringstream with pre-allocation
    template<typename... Args>
    std::string FormatMessage(const std::string& format, Args&&... args) {
        std::ostringstream oss;
        oss.str().reserve(format.length() + 256); // Pre-allocate for efficiency
        
        // Convert all arguments to string vector first
        std::vector<std::string> arg_strings = {ToString(std::forward<Args>(args))...};
        
        size_t arg_index = 0;
        size_t pos = 0;
        
        while (pos < format.length()) {
            size_t placeholder_pos = format.find("{}", pos);
            if (placeholder_pos == std::string::npos) {
                // No more placeholders, append rest of format string
                oss << format.substr(pos);
                break;
            }
            
            // Append text before placeholder
            oss << format.substr(pos, placeholder_pos - pos);
            
            // Append argument if available
            if (arg_index < arg_strings.size()) {
                oss << arg_strings[arg_index++];
            } else {
                oss << "{}"; // Keep placeholder if no argument available
            }
            
            pos = placeholder_pos + 2; // Skip "{}"
        }
        
        return oss.str();
    }
    
    // Efficient type-to-string conversion - simplified for older compilers
    std::string ToString(const std::string& value) { return value; }
    std::string ToString(const char* value) { return std::string(value); }
    std::string ToString(char* value) { return std::string(value); }
    std::string ToString(bool value) { return value ? "true" : "false"; }
    std::string ToString(int value) { return std::to_string(value); }
    std::string ToString(long value) { return std::to_string(value); }
    std::string ToString(long long value) { return std::to_string(value); }
    std::string ToString(unsigned value) { return std::to_string(value); }
    std::string ToString(unsigned long value) { return std::to_string(value); }
    std::string ToString(unsigned long long value) { return std::to_string(value); }
    std::string ToString(float value) { 
        std::ostringstream oss;
        oss.precision(6);
        oss << value;
        return oss.str();
    }
    std::string ToString(double value) { 
        std::ostringstream oss;
        oss.precision(6);
        oss << value;
        return oss.str();
    }
    
    // Generic fallback for other types
    template<typename T>
    std::string ToString(const T& value) {
        std::ostringstream oss;
        oss << value;
        return oss.str();
    }
    
    std::string FormatTimestamp();
    std::string LevelToString(LogLevel level);
    
    // File stream management helpers
    void OpenLogFile();
    void CloseLogFile();
};

// Convenience macros
#define LOG_TRACE(...) ats::Logger::Instance().Log(ats::LogLevel::TRACE, __VA_ARGS__)
#define LOG_DEBUG(...) ats::Logger::Instance().Log(ats::LogLevel::DBG, __VA_ARGS__)
#define LOG_INFO(...) ats::Logger::Instance().Log(ats::LogLevel::INFO, __VA_ARGS__)
#define LOG_WARNING(...) ats::Logger::Instance().Log(ats::LogLevel::WARNING, __VA_ARGS__)
#define LOG_ERROR(...) ats::Logger::Instance().Log(ats::LogLevel::ERR, __VA_ARGS__)
#define LOG_CRITICAL(...) ats::Logger::Instance().Log(ats::LogLevel::CRITICAL, __VA_ARGS__)

} // namespace ats 