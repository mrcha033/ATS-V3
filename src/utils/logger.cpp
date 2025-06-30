#include "logger.hpp"
#include <iostream>
#include <fstream>
#include <chrono>
#include <iomanip>
#include <mutex>
#include <sstream>

namespace ats {

static std::ofstream log_file;
static std::mutex log_mutex;

static LogLevel current_log_level = LogLevel::INFO;
static bool console_output_enabled = true; // Default to true

void Logger::init(const LoggingConfig& config, LogLevel app_log_level) {
    if (config.file_output) {
        log_file.open(config.file_path, std::ios::app);
    }
    current_log_level = app_log_level;
    console_output_enabled = config.console_output;
}

void Logger::set_log_level(LogLevel level) {
    current_log_level = level;
}

void Logger::log(LogLevel level, const std::string& message) {
    if (level < current_log_level) {
        return;
    }

    std::lock_guard<std::mutex> lock(log_mutex);
    auto now = std::chrono::system_clock::now();
    auto time_t = std::chrono::system_clock::to_time_t(now);

    std::string level_str;
    switch (level) {
        case LogLevel::DEBUG: level_str = "DEBUG"; break;
        case LogLevel::INFO: level_str = "INFO"; break;
        case LogLevel::WARNING: level_str = "WARNING"; break;
        case LogLevel::ERROR: level_str = "ERROR"; break;
        case LogLevel::CRITICAL: level_str = "CRITICAL"; break;
    }

    std::stringstream log_stream;
    log_stream << std::put_time(std::localtime(&time_t), "%F %T") << " [" << level_str << "] " << message << std::endl;

    if (log_file.is_open()) {
        log_file << log_stream.str();
    }
    if (console_output_enabled) {
        std::cout << log_stream.str();
    }
}

void Logger::info(const std::string& message) {
    log(LogLevel::INFO, message);
}

void Logger::error(const std::string& message) {
    log(LogLevel::ERROR, message);
}

}