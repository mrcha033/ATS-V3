#pragma once

#include <string>
#include <vector>
#include <memory>
#include <stdexcept>

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

class Logger {
public:
    static void init(const std::string& file_path);
    static void info(const std::string& message);
    static void error(const std::string& message);
};

#define LOG_INFO(...) ats::Logger::info(ats::format_string(__VA_ARGS__))
#define LOG_ERROR(...) ats::Logger::error(ats::format_string(__VA_ARGS__))
#define LOG_WARNING(...) ats::Logger::info("WARNING: " + ats::format_string(__VA_ARGS__))
#define LOG_DEBUG(...) ats::Logger::info("DEBUG: " + ats::format_string(__VA_ARGS__))
#define LOG_CRITICAL(...) ats::Logger::error("CRITICAL: " + ats::format_string(__VA_ARGS__))

} 