#pragma once

#include <string>
#include <vector>
#include <memory>
#include <stdexcept>
#include "config_types.hpp" // Include for LoggingConfig

namespace ats {

// Helper to format strings
template<typename ... Args>
std::string format_string( const std::string& format, Args ... args )
{
    int size_s = std::snprintf( nullptr, 0, format.c_str(), args ... ) + 1; // Extra space for '\0'
    if( size_s <= 0 ){ throw std::runtime_error( "Error during formatting." ); }
    auto size = static_cast<size_t>( size_s );
    std::unique_ptr<char[]> buf( new char[ size ] );
    std::snprintf( buf.get(), size, format.c_str(), args ... );
    return std::string( buf.get(), buf.get() + size - 1 ); // We don't want the '\0' inside
}

enum class LogLevel {
    DEBUG,
    INFO,
    WARNING,
    ERROR,
    CRITICAL
};

class Logger {
public:
    static void init(const LoggingConfig& config, LogLevel app_log_level);
    static void set_log_level(LogLevel level);
    static void log(LogLevel level, const std::string& message);
    static void info(const std::string& message);
    static void error(const std::string& message);
};


#define LOG_INFO(...) ats::Logger::log(ats::LogLevel::INFO, ats::format_string(__VA_ARGS__))
#define LOG_ERROR(...) ats::Logger::log(ats::LogLevel::ERROR, ats::format_string(__VA_ARGS__))
#define LOG_WARNING(...) ats::Logger::log(ats::LogLevel::WARNING, ats::format_string(__VA_ARGS__))
#define LOG_DEBUG(...) ats::Logger::log(ats::LogLevel::DEBUG, ats::format_string(__VA_ARGS__))
#define LOG_CRITICAL(...) ats::Logger::log(ats::LogLevel::CRITICAL, ats::format_string(__VA_ARGS__))

} 