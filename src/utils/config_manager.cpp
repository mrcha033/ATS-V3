#include "config_manager.hpp"
#include <fstream>
#include <cstdlib>
#include <algorithm>

namespace ats {

bool ConfigManager::load(const std::string& file_path) {
    std::ifstream file(file_path);
    if (!file.is_open()) {
        return false;
    }
    file >> config_data_;
    return true;
}

std::string ConfigManager::get_db_path() const {
    return config_data_.value("db_path", "trades.db");
}

std::vector<std::string> ConfigManager::get_symbols() const {
    return config_data_.value("symbols", std::vector<std::string>{"BTC/USDT"});
}

std::string get_env_var(const std::string& key) {
    char* val = getenv(key.c_str());
    return val == NULL ? std::string("") : std::string(val);
}

nlohmann::json ConfigManager::get_exchanges_config() const {
    auto exchanges = config_data_.value("exchanges", nlohmann::json::object());
    for (auto& [name, config] : exchanges.items()) {
        std::string upper_name = name;
        std::transform(upper_name.begin(), upper_name.end(), upper_name.begin(), ::toupper);

        std::string api_key_env = get_env_var(upper_name + "_API_KEY");
        if (!api_key_env.empty()) {
            config["api_key"] = api_key_env;
        }

        std::string secret_key_env = get_env_var(upper_name + "_SECRET_KEY");
        if (!secret_key_env.empty()) {
            config["secret_key"] = secret_key_env;
        }
    }
    return exchanges;
}

} 
