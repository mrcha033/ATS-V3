#pragma once

#include <nlohmann/json.hpp>
#include <string>
#include <vector>
#include <map>

namespace ats {
namespace json {

// Simple alias to nlohmann::json for compatibility
using JsonValue = nlohmann::json;

// Parse JSON from string
JsonValue ParseJson(const std::string& jsonStr);

// Convert JSON to string
std::string JsonToString(const JsonValue& json);

// Helper functions for type checking and value extraction
bool IsObject(const JsonValue& value);
bool IsArray(const JsonValue& value);
bool IsString(const JsonValue& value);
bool IsNumber(const JsonValue& value);
bool IsBool(const JsonValue& value);
bool IsNull(const JsonValue& value);

// Value extraction (with default values)
std::string GetString(const JsonValue& value, const std::string& defaultVal = "");
double GetNumber(const JsonValue& value, double defaultVal = 0.0);
bool GetBool(const JsonValue& value, bool defaultVal = false);

// Object/Array access
JsonValue GetValue(const JsonValue& obj, const std::string& key);
bool HasKey(const JsonValue& obj, const std::string& key);
size_t GetSize(const JsonValue& value);

} // namespace json
} // namespace ats 