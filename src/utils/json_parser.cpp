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
    std::string indent_str = pretty ? std::string(indent, ' ') : "";
    std::string newline = pretty ? "\n" : "";
    
    if (std::holds_alternative<std::nullptr_t>(value)) {
        return "null";
    } else if (std::holds_alternative<bool>(value)) {
        return std::get<bool>(value) ? "true" : "false";
    } else if (std::holds_alternative<int>(value)) {
        return std::to_string(std::get<int>(value));
    } else if (std::holds_alternative<double>(value)) {
        std::ostringstream oss;
        oss << std::get<double>(value);
        return oss.str();
    } else if (std::holds_alternative<std::string>(value)) {
        std::string str = std::get<std::string>(value);
        // Escape special characters
        std::string escaped = "\"";
        for (char c : str) {
            switch (c) {
                case '"': escaped += "\\\""; break;
                case '\\': escaped += "\\\\"; break;
                case '\b': escaped += "\\b"; break;
                case '\f': escaped += "\\f"; break;
                case '\n': escaped += "\\n"; break;
                case '\r': escaped += "\\r"; break;
                case '\t': escaped += "\\t"; break;
                default: escaped += c; break;
            }
        }
        escaped += "\"";
        return escaped;
    } else if (std::holds_alternative<std::vector<JsonValue>>(value)) {
        auto arr = std::get<std::vector<JsonValue>>(value);
        std::string result = "[";
        for (size_t i = 0; i < arr.size(); ++i) {
            if (i > 0) result += ",";
            if (pretty) result += newline + std::string(indent + 2, ' ');
            result += Stringify(arr[i], pretty, indent + 2);
        }
        if (pretty && !arr.empty()) result += newline + indent_str;
        result += "]";
        return result;
    } else if (std::holds_alternative<std::unordered_map<std::string, JsonValue>>(value)) {
        auto obj = std::get<std::unordered_map<std::string, JsonValue>>(value);
        std::string result = "{";
        bool first = true;
        for (const auto& pair : obj) {
            if (!first) result += ",";
            first = false;
            if (pretty) result += newline + std::string(indent + 2, ' ');
            result += "\"" + pair.first + "\":" + (pretty ? " " : "");
            result += Stringify(pair.second, pretty, indent + 2);
        }
        if (pretty && !obj.empty()) result += newline + indent_str;
        result += "}";
        return result;
    }
    return "null";
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
                case 'u': {
                    // Unicode escape sequence \uXXXX
                    Advance();
                    std::string hex;
                    for (int i = 0; i < 4; ++i) {
                        if (IsAtEnd() || !std::isxdigit(Current())) {
                            ThrowError("Invalid unicode escape sequence");
                        }
                        hex += Current();
                        Advance();
                    }
                    // For simplicity, just add the unicode sequence as-is
                    result += "\\u" + hex;
                    continue; // Skip the Advance() at the end
                }
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
    size_t start = pos_;
    bool is_negative = false;
    bool has_decimal = false;
    bool has_exponent = false;
    
    // Handle negative sign
    if (Current() == '-') {
        is_negative = true;
        Advance();
    }
    
    // Parse integer part
    if (!std::isdigit(Current())) {
        ThrowError("Invalid number format");
    }
    
    if (Current() == '0') {
        Advance();
    } else {
        while (!IsAtEnd() && std::isdigit(Current())) {
            Advance();
        }
    }
    
    // Parse decimal part
    if (!IsAtEnd() && Current() == '.') {
        has_decimal = true;
        Advance();
        if (!std::isdigit(Current())) {
            ThrowError("Invalid number format: expected digit after decimal point");
        }
        while (!IsAtEnd() && std::isdigit(Current())) {
            Advance();
        }
    }
    
    // Parse exponent part
    if (!IsAtEnd() && (Current() == 'e' || Current() == 'E')) {
        has_exponent = true;
        Advance();
        if (!IsAtEnd() && (Current() == '+' || Current() == '-')) {
            Advance();
        }
        if (!std::isdigit(Current())) {
            ThrowError("Invalid number format: expected digit in exponent");
        }
        while (!IsAtEnd() && std::isdigit(Current())) {
            Advance();
        }
    }
    
    std::string number_str = json_text_.substr(start, pos_ - start);
    
    try {
        if (has_decimal || has_exponent) {
            double value = std::stod(number_str);
            return JsonValue(value);
        } else {
            int value = std::stoi(number_str);
            return JsonValue(value);
        }
    } catch (const std::exception&) {
        ThrowError("Invalid number format: " + number_str);
    }
    
    return JsonValue(nullptr);
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
    } else {
        ThrowError("Invalid literal");
    }
    
    return JsonValue(nullptr);
}

void JsonParser::ThrowError(const std::string& message) {
    std::ostringstream oss;
    oss << "JSON Parse Error at position " << pos_ << ": " << message;
    throw JsonParseError(oss.str());
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
    } else if (IsDouble(value)) {
        return static_cast<int>(std::get<double>(value));
    }
    return defaultValue;
}

double AsDouble(const JsonValue& value, double defaultValue) {
    if (IsDouble(value)) {
        return std::get<double>(value);
    } else if (IsInt(value)) {
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
    return {};
}

std::unordered_map<std::string, JsonValue> AsObject(const JsonValue& value) {
    if (IsObject(value)) {
        return std::get<std::unordered_map<std::string, JsonValue>>(value);
    }
    return {};
}

// Path access functions
JsonValue GetPath(const JsonValue& root, const std::string& path) {
    if (path.empty()) {
        return root;
    }
    
    std::istringstream iss(path);
    std::string segment;
    JsonValue current = root;
    
    while (std::getline(iss, segment, '.')) {
        if (IsObject(current)) {
            auto obj = AsObject(current);
            auto it = obj.find(segment);
            if (it != obj.end()) {
                current = it->second;
            } else {
                return JsonValue(nullptr);
            }
        } else {
            return JsonValue(nullptr);
        }
    }
    
    return current;
}

bool HasPath(const JsonValue& root, const std::string& path) {
    JsonValue result = GetPath(root, path);
    return !IsNull(result);
}

} // namespace json
} // namespace ats 