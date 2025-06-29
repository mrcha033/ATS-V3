#include "logger.hpp"
#include <iostream>
#include <fstream>
#include <chrono>
#include <iomanip>
#include <mutex>

namespace ats {

static std::ofstream log_file;
static std::mutex log_mutex;

void Logger::init(const std::string& file_path) {
    log_file.open(file_path, std::ios::app);
}

void Logger::info(const std::string& message) {
    std::lock_guard<std::mutex> lock(log_mutex);
    auto now = std::chrono::system_clock::now();
    auto time_t = std::chrono::system_clock::to_time_t(now);
    log_file << std::put_time(std::localtime(&time_t), "%F %T") << " [INFO] " << message << std::endl;
    std::cout << std::put_time(std::localtime(&time_t), "%F %T") << " [INFO] " << message << std::endl;
}

void Logger::error(const std::string& message) {
    std::lock_guard<std::mutex> lock(log_mutex);
    auto now = std::chrono::system_clock::now();
    auto time_t = std::chrono::system_clock::to_time_t(now);
    log_file << std::put_time(std::localtime(&time_t), "%F %T") << " [ERROR] " << message << std::endl;
    std::cerr << std::put_time(std::localtime(&time_t), "%F %T") << " [ERROR] " << message << std::endl;
}

}