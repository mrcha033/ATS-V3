#include "json_parser.hpp"
#include <stdexcept>

namespace ats {
namespace json {

JsonValue ParseJson(const std::string& jsonStr) {
    try {
        return nlohmann::json::parse(jsonStr);
    } catch (const nlohmann::json::parse_error& e) {
        throw std::runtime_error("JSON parse error: " + std::string(e.what()));
    }
}

std::string JsonToString(const JsonValue& json) {
    return json.dump();
}

bool IsObject(const JsonValue& value) {
    return value.is_object();
}

bool IsArray(const JsonValue& value) {
    return value.is_array();
}

bool IsString(const JsonValue& value) {
    return value.is_string();
}

bool IsNumber(const JsonValue& value) {
    return value.is_number();
}

bool IsBool(const JsonValue& value) {
    return value.is_boolean();
}

bool IsNull(const JsonValue& value) {
    return value.is_null();
}

std::string GetString(const JsonValue& value, const std::string& defaultVal) {
    if (value.is_string()) {
        return value.get<std::string>();
    }
    return defaultVal;
}

double GetNumber(const JsonValue& value, double defaultVal) {
    if (value.is_number()) {
        return value.get<double>();
    }
    return defaultVal;
}

bool GetBool(const JsonValue& value, bool defaultVal) {
    if (value.is_boolean()) {
        return value.get<bool>();
    }
    return defaultVal;
}

JsonValue GetValue(const JsonValue& obj, const std::string& key) {
    if (obj.is_object() && obj.contains(key)) {
        return obj[key];
    }
    return JsonValue{}; // null value
}

bool HasKey(const JsonValue& obj, const std::string& key) {
    return obj.is_object() && obj.contains(key);
}

size_t GetSize(const JsonValue& value) {
    if (value.is_array() || value.is_object()) {
        return value.size();
    }
    return 0;
}

} // namespace json
} // namespace ats 
