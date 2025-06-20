#pragma once

#include <string>
#include <unordered_map>
#include <vector>
#include <variant>
#include <stdexcept>

namespace ats {

using JsonValue = std::variant<std::nullptr_t, bool, int, double, std::string, 
                              std::vector<JsonValue>, std::unordered_map<std::string, JsonValue>>;

class JsonParseError : public std::runtime_error {
public:
    explicit JsonParseError(const std::string& message) : std::runtime_error(message) {}
};

class JsonParser {
private:
    std::string json_text_;
    size_t pos_;
    
public:
    explicit JsonParser(const std::string& json);
    
    JsonValue Parse();
    
    // Static convenience methods
    static JsonValue ParseString(const std::string& json);
    static std::string Stringify(const JsonValue& value, bool pretty = false, int indent = 0);
    
private:
    char Current();
    char Peek();
    void Advance();
    void SkipWhitespace();
    bool IsAtEnd();
    void Expect(char c);
    
    JsonValue ParseValue();
    JsonValue ParseObject();
    JsonValue ParseArray();
    JsonValue ParseString();
    JsonValue ParseNumber();
    JsonValue ParseLiteral();
    
    std::string ParseStringValue();
    std::string UnescapeString(const std::string& str);
    
    void ThrowError(const std::string& message);
};

// Helper functions for accessing JSON values
namespace json {
    bool IsNull(const JsonValue& value);
    bool IsBool(const JsonValue& value);
    bool IsInt(const JsonValue& value);
    bool IsDouble(const JsonValue& value);
    bool IsString(const JsonValue& value);
    bool IsArray(const JsonValue& value);
    bool IsObject(const JsonValue& value);
    
    bool AsBool(const JsonValue& value, bool defaultValue = false);
    int AsInt(const JsonValue& value, int defaultValue = 0);
    double AsDouble(const JsonValue& value, double defaultValue = 0.0);
    std::string AsString(const JsonValue& value, const std::string& defaultValue = "");
    std::vector<JsonValue> AsArray(const JsonValue& value);
    std::unordered_map<std::string, JsonValue> AsObject(const JsonValue& value);
    
    // Path access (e.g., "exchange.binance.api_key")
    JsonValue GetPath(const JsonValue& root, const std::string& path);
    bool HasPath(const JsonValue& root, const std::string& path);
}

} // namespace ats 