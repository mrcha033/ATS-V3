#pragma once

#include <string>
#include "types.hpp"

namespace ats {

class PriceProvider {
public:
    virtual ~PriceProvider() = default;
    virtual bool GetLatestPrice(const std::string& exchange, const std::string& symbol, Price& price) = 0;
};

} // namespace ats
