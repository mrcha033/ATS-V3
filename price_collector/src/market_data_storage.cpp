#include "market_data_storage.hpp"
#include <iostream>

namespace ats {
namespace price_collector {

// Minimal stub implementation to satisfy linker
class MarketDataStorageImpl : public MarketDataStorage {
public:
    bool initialize(const std::string& connection_string) override { return true; }
    bool store_ticker(const types::Ticker& ticker) override { return true; }
    bool store_tickers(const std::vector<types::Ticker>& tickers) override { return true; }
    
    types::Ticker get_latest_ticker(const std::string& exchange, const std::string& symbol) const override {
        return types::Ticker{};
    }
    
    std::vector<types::Ticker> get_ticker_history(const std::string& exchange, 
                                                 const std::string& symbol,
                                                 std::chrono::system_clock::time_point from,
                                                 std::chrono::system_clock::time_point to) const override {
        return {};
    }
    
    std::vector<types::Ticker> get_all_latest_tickers() const override {
        return {};
    }
};

} // namespace price_collector
} // namespace ats