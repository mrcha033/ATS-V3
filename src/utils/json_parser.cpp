#include "json_parser.hpp"
#include <cctype>
#include <sstream>
#include <stdexcept>

namespace ats {

// JsonParser class implementation
JsonParser::JsonParser(const std::string& json) : json_text_(json), pos_(0) {}

JsonValue JsonParser::Parse() {
    SkipWhitespace();
    if (IsAtEnd()) {
        throw JsonParseError("Empty JSON string");
    }
    return ParseValue();
}

JsonValue JsonParser::ParseString(const std::string& json) {
    JsonParser parser(json);
    return parser.Parse();
}

std::string JsonParser::Stringify(const JsonValue& value, bool pretty, int indent) {
    // Simple stringify implementation
    if (std::holds_alternative<std::nullptr_t>(value)) {
        return "null";
    } else if (std::holds_alternative<bool>(value)) {
        return std::get<bool>(value) ? "true" : "false";
    } else if (std::holds_alternative<int>(value)) {
        return std::to_string(std::get<int>(value));
    } else if (std::holds_alternative<double>(value)) {
        return std::to_string(std::get<double>(value));
    } else if (std::holds_alternative<std::string>(value)) {
        return "\"" + std::get<std::string>(value) + "\"";
    }
    return "{}"; // Simplified for arrays/objects
}

char JsonParser::Current() {
    if (IsAtEnd()) return '\0';
    return json_text_[pos_];
}

char JsonParser::Peek() {
    if (pos_ + 1 >= json_text_.length()) return '\0';
    return json_text_[pos_ + 1];
}

void JsonParser::Advance() {
    if (!IsAtEnd()) pos_++;
}

void JsonParser::SkipWhitespace() {
    while (!IsAtEnd() && std::isspace(Current())) {
        Advance();
    }
}

bool JsonParser::IsAtEnd() {
    return pos_ >= json_text_.length();
}

void JsonParser::Expect(char c) {
    if (Current() != c) {
        ThrowError("Expected '" + std::string(1, c) + "'");
    }
    Advance();
}

JsonValue JsonParser::ParseValue() {
    SkipWhitespace();
    
    char c = Current();
    switch (c) {
        case '"': return ParseString();
        case '{': return ParseObject();
        case '[': return ParseArray();
        case 't': case 'f': case 'n': return ParseLiteral();
        default:
            if (std::isdigit(c) || c == '-') {
                return ParseNumber();
            }
            ThrowError("Unexpected character");
    }
    return JsonValue(nullptr);
}

JsonValue JsonParser::ParseObject() {
    std::unordered_map<std::string, JsonValue> obj;
    Expect('{');
    SkipWhitespace();
    
    if (Current() == '}') {
        Advance();
        return JsonValue(obj);
    }
    
    while (!IsAtEnd()) {
        SkipWhitespace();
        if (Current() != '"') {
            ThrowError("Expected string key");
        }
        
        std::string key = ParseStringValue();
        
        SkipWhitespace();
        Expect(':');
        SkipWhitespace();
        
        JsonValue value = ParseValue();
        obj[key] = value;
        
        SkipWhitespace();
        if (Current() == '}') {
            Advance();
            break;
        } else if (Current() == ',') {
            Advance();
        } else {
            ThrowError("Expected ',' or '}'");
        }
    }
    
    return JsonValue(obj);
}

JsonValue JsonParser::ParseArray() {
    std::vector<JsonValue> arr;
    Expect('[');
    SkipWhitespace();
    
    if (Current() == ']') {
        Advance();
        return JsonValue(arr);
    }
    
    while (!IsAtEnd()) {
        SkipWhitespace();
        JsonValue value = ParseValue();
        arr.push_back(value);
        
        SkipWhitespace();
        if (Current() == ']') {
            Advance();
            break;
        } else if (Current() == ',') {
            Advance();
        } else {
            ThrowError("Expected ',' or ']'");
        }
    }
    
    return JsonValue(arr);
}

JsonValue JsonParser::ParseString() {
    return JsonValue(ParseStringValue());
}

std::string JsonParser::ParseStringValue() {
    Expect('"');
    std::string result;
    
    while (!IsAtEnd() && Current() != '"') {
        if (Current() == '\\') {
            Advance();
            if (IsAtEnd()) {
                ThrowError("Unterminated string escape");
            }
            
            char escaped = Current();
            switch (escaped) {
                case '"': result += '"'; break;
                case '\\': result += '\\'; break;
                case '/': result += '/'; break;
                case 'b': result += '\b'; break;
                case 'f': result += '\f'; break;
                case 'n': result += '\n'; break;
                case 'r': result += '\r'; break;
                case 't': result += '\t'; break;
                default:
                    result += escaped;
                    break;
            }
        } else {
            result += Current();
        }
        Advance();
    }
    
    if (IsAtEnd()) {
        ThrowError("Unterminated string");
    }
    
    Expect('"');
    return result;
}

JsonValue JsonParser::ParseNumber() {
    std::string number;
    
    if (Current() == '-') {
        number += Current();
        Advance();
    }
    
    while (!IsAtEnd() && (std::isdigit(Current()) || Current() == '.')) {
        number += Current();
        Advance();
    }
    
    if (number.find('.') != std::string::npos) {
        return JsonValue(std::stod(number));
    } else {
        return JsonValue(std::stoi(number));
    }
}

JsonValue JsonParser::ParseLiteral() {
    if (json_text_.substr(pos_, 4) == "true") {
        pos_ += 4;
        return JsonValue(true);
    } else if (json_text_.substr(pos_, 5) == "false") {
        pos_ += 5;
        return JsonValue(false);
    } else if (json_text_.substr(pos_, 4) == "null") {
        pos_ += 4;
        return JsonValue(nullptr);
    }
    
    ThrowError("Invalid literal");
    return JsonValue(nullptr);
}

void JsonParser::ThrowError(const std::string& message) {
    throw JsonParseError("JSON Parse Error at position " + std::to_string(pos_) + ": " + message);
}

namespace json {

// Helper function to check JSON value types
bool IsNull(const JsonValue& value) {
    return std::holds_alternative<std::nullptr_t>(value);
}

bool IsBool(const JsonValue& value) {
    return std::holds_alternative<bool>(value);
}

bool IsInt(const JsonValue& value) {
    return std::holds_alternative<int>(value);
}

bool IsDouble(const JsonValue& value) {
    return std::holds_alternative<double>(value);
}

bool IsString(const JsonValue& value) {
    return std::holds_alternative<std::string>(value);
}

bool IsArray(const JsonValue& value) {
    return std::holds_alternative<std::vector<JsonValue>>(value);
}

bool IsObject(const JsonValue& value) {
    return std::holds_alternative<std::unordered_map<std::string, JsonValue>>(value);
}

// Type conversion functions with default values
bool AsBool(const JsonValue& value, bool defaultValue) {
    if (IsBool(value)) {
        return std::get<bool>(value);
    }
    return defaultValue;
}

int AsInt(const JsonValue& value, int defaultValue) {
    if (IsInt(value)) {
        return std::get<int>(value);
    }
    if (IsDouble(value)) {
        return static_cast<int>(std::get<double>(value));
    }
    return defaultValue;
}

double AsDouble(const JsonValue& value, double defaultValue) {
    if (IsDouble(value)) {
        return std::get<double>(value);
    }
    if (IsInt(value)) {
        return static_cast<double>(std::get<int>(value));
    }
    return defaultValue;
}

std::string AsString(const JsonValue& value, const std::string& defaultValue) {
    if (IsString(value)) {
        return std::get<std::string>(value);
    }
    return defaultValue;
}

std::vector<JsonValue> AsArray(const JsonValue& value) {
    if (IsArray(value)) {
        return std::get<std::vector<JsonValue>>(value);
    }
    return std::vector<JsonValue>();
}

std::unordered_map<std::string, JsonValue> AsObject(const JsonValue& value) {
    if (IsObject(value)) {
        return std::get<std::unordered_map<std::string, JsonValue>>(value);
    }
    return std::unordered_map<std::string, JsonValue>();
}

// Path access functions
JsonValue GetPath(const JsonValue& root, const std::string& path) {
    if (path.empty()) {
        return root;
    }
    
    std::vector<std::string> parts;
    std::istringstream iss(path);
    std::string part;
    
    while (std::getline(iss, part, '.')) {
        if (!part.empty()) {
            parts.push_back(part);
        }
    }
    
    JsonValue current = root;
    
    for (const auto& key : parts) {
        if (!IsObject(current)) {
            return JsonValue(nullptr);
        }
        
        auto obj = AsObject(current);
        auto it = obj.find(key);
        if (it == obj.end()) {
            return JsonValue(nullptr);
        }
        
        current = it->second;
    }
    
    return current;
}

bool HasPath(const JsonValue& root, const std::string& path) {
    JsonValue result = GetPath(root, path);
    return !IsNull(result);
}

} // namespace json
} // namespace ats 