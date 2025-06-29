#pragma once

#include <string>
#include <vector>
#include <memory>
#include "../core/types.hpp"

namespace ats {

class ExchangeInterface {
public:
    virtual ~ExchangeInterface() = default;

    virtual std::string get_name() const = 0;
    virtual void connect() = 0;
    virtual void disconnect() = 0;
    virtual Price get_price(const std::string& symbol) = 0;
};

}
 