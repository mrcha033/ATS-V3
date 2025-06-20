#pragma once

#include <string>
#include <memory>
#include <sstream>
#include <type_traits>

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
        
        std::ostringstream oss;
        FormatHelper(oss, format, std::forward<Args>(args)...);
        Log(level, oss.str());
    }
    
private:
    template<typename T>
    void FormatHelper(std::ostringstream& oss, const std::string& format, T&& value) {
        size_t pos = format.find("{}");
        if (pos != std::string::npos) {
            oss << format.substr(0, pos) << std::forward<T>(value) << format.substr(pos + 2);
        } else {
            oss << format;
        }
    }
    
    template<typename T, typename... Args>
    void FormatHelper(std::ostringstream& oss, const std::string& format, T&& value, Args&&... args) {
        size_t pos = format.find("{}");
        if (pos != std::string::npos) {
            std::string remaining = format.substr(0, pos) + ToString(std::forward<T>(value)) + format.substr(pos + 2);
            FormatHelper(oss, remaining, std::forward<Args>(args)...);
        } else {
            oss << format;
        }
    }
    
    // Helper to convert values to string
    template<typename T>
    std::string ToString(T&& value) {
        if constexpr (std::is_same_v<std::decay_t<T>, std::string>) {
            return std::forward<T>(value);
        } else if constexpr (std::is_same_v<std::decay_t<T>, const char*>) {
            return std::string(value);
        } else if constexpr (std::is_arithmetic_v<std::decay_t<T>>) {
            return std::to_string(value);
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