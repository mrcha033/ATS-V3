#pragma once

#include <string>
#include <vector>
#include <nlohmann/json.hpp>

namespace ats {

class ConfigManager {
public:
    bool load(const std::string& file_path);

    std::string get_db_path() const;
    std::vector<std::string> get_symbols() const;
    nlohmann::json get_exchanges_config() const;

private:
    nlohmann::json config_data_;
};

} 