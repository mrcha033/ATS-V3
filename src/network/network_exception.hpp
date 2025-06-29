#pragma once

#include <stdexcept>
#include <string>

namespace ats {

class NetworkException : public std::runtime_error {
public:
    explicit NetworkException(const std::string& message)
        : std::runtime_error(message) {}
};

class TimeoutException : public NetworkException {
public:
    explicit TimeoutException(const std::string& message)
        : NetworkException(message) {}
};

class ConnectionException : public NetworkException {
public:
    explicit ConnectionException(const std::string& message)
        : NetworkException(message) {}
};

} // namespace ats
