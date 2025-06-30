#pragma once

#include <string>
#include <vector>
#include <nlohmann/json.hpp>

namespace ats {

struct ExchangeFees {
    double maker_fee;
    double taker_fee;
};

class ConfigManager {
public:
    bool load(const std::string& file_path);

    std::string get_db_path() const;
    std::vector<std::string> get_symbols() const;
    nlohmann::json get_exchanges_config() const;
    ExchangeFees get_exchange_fees(const std::string& exchange_name) const;
    std::vector<exchange_config> get_exchanges() const;
    std::string get_log_level() const;
    std::vector<std::string> get_trading_pairs() const;

private:
    nlohmann::json config_data_;
};

} 