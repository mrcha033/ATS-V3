#include "config_types.hpp"
#include <nlohmann/json.hpp>
#include <cstdlib>
#include <algorithm>

namespace ats {

std::string get_env_var(const std::string& key) {
    char* val = getenv(key.c_str());
    return val == NULL ? std::string("") : std::string(val);
}

// No longer defining from_json functions here, as NLOHMANN_DEFINE_TYPE_NON_INTRUSIVE handles it.

} // namespace ats
