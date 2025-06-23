#pragma once

#include <string>
#include <memory>
#include <sstream>
#include <type_traits>
#include <vector>
#include <mutex>

namespace ats {

enum class LogLevel {
    TRACE = 0,
    DEBUG = 1,
    INFO = 2,
    WARNING = 3,
    ERROR = 4,
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
    
    Logger() = default;
    
public:
    static void Initialize();
    static Logger& Instance();
    
    void SetLevel(LogLevel level) noexcept { min_level_ = level; }
    void SetConsoleOutput(bool enabled) noexcept { console_output_ = enabled; }
    void SetFileOutput(bool enabled) noexcept { file_output_ = enabled; }
    void SetLogFile(const std::string& path) noexcept { log_file_path_ = path; }
    
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
    
    // Efficient type-to-string conversion
    template<typename T>
    std::string ToString(T&& value) {
        if constexpr (std::is_same_v<std::decay_t<T>, std::string>) {
            return std::forward<T>(value);
        } else if constexpr (std::is_same_v<std::decay_t<T>, const char*>) {
            return std::string(value);
        } else if constexpr (std::is_same_v<std::decay_t<T>, char*>) {
            return std::string(value);
        } else if constexpr (std::is_arithmetic_v<std::decay_t<T>>) {
            if constexpr (std::is_floating_point_v<std::decay_t<T>>) {
                // Use ostringstream for floating point to control precision
                std::ostringstream oss;
                oss.precision(6);
                oss << std::forward<T>(value);
                return oss.str();
            } else {
                return std::to_string(value);
            }
        } else if constexpr (std::is_same_v<std::decay_t<T>, bool>) {
            return value ? "true" : "false";
        } else {
            // For other types, try to convert to string via stream
            std::ostringstream oss;
            oss << std::forward<T>(value);
            return oss.str();
        }
    }
    
    std::string FormatTimestamp();
    std::string LevelToString(LogLevel level);
};

// Convenience macros
#define LOG_TRACE(...) ats::Logger::Instance().Log(ats::LogLevel::TRACE, __VA_ARGS__)
#define LOG_DEBUG(...) ats::Logger::Instance().Log(ats::LogLevel::DEBUG, __VA_ARGS__)
#define LOG_INFO(...) ats::Logger::Instance().Log(ats::LogLevel::INFO, __VA_ARGS__)
#define LOG_WARNING(...) ats::Logger::Instance().Log(ats::LogLevel::WARNING, __VA_ARGS__)
#define LOG_ERROR(...) ats::Logger::Instance().Log(ats::LogLevel::ERROR, __VA_ARGS__)
#define LOG_CRITICAL(...) ats::Logger::Instance().Log(ats::LogLevel::CRITICAL, __VA_ARGS__)

} // namespace ats 