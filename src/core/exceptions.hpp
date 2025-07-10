#pragma once

#include <stdexcept>
#include <string>

namespace ats {

class ATSException : public std::runtime_error {
public:
    explicit ATSException(const std::string& message) : std::runtime_error(message) {}
    explicit ATSException(const char* message) : std::runtime_error(message) {}
};

class ConfigurationError : public ATSException {
public:
    explicit ConfigurationError(const std::string& message) 
        : ATSException("Configuration Error: " + message) {}
};

class DatabaseError : public ATSException {
public:
    explicit DatabaseError(const std::string& message) 
        : ATSException("Database Error: " + message) {}
};

class ExchangeError : public ATSException {
public:
    explicit ExchangeError(const std::string& message) 
        : ATSException("Exchange Error: " + message) {}
};

class NetworkError : public ATSException {
public:
    explicit NetworkError(const std::string& message) 
        : ATSException("Network Error: " + message) {}
};

class RiskManagementError : public ATSException {
public:
    explicit RiskManagementError(const std::string& message) 
        : ATSException("Risk Management Error: " + message) {}
};

class TradingError : public ATSException {
public:
    explicit TradingError(const std::string& message) 
        : ATSException("Trading Error: " + message) {}
};

class ValidationError : public ATSException {
public:
    explicit ValidationError(const std::string& message) 
        : ATSException("Validation Error: " + message) {}
};

} // namespace ats