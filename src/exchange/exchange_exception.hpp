#pragma once

#include <stdexcept>
#include <string>

namespace ats {

class ExchangeException : public std::runtime_error {
public:
    explicit ExchangeException(const std::string& message)
        : std::runtime_error(message) {}
};

} // namespace ats
