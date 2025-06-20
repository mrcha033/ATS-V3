#include "json_parser.hpp"
#include "logger.hpp"
#include <cctype>
#include <sstream>
#include <iomanip>

namespace ats {

JsonParser::JsonParser(const std::string& json) : json_text_(json), pos_(0) {}

JsonValue JsonParser::Parse() {
    SkipWhitespace();
    auto result = ParseValue();
    SkipWhitespace();
    if (!IsAtEnd()) {
        ThrowError("Unexpected characters after JSON value");
    }
    return result;
}

JsonValue JsonParser::ParseString(const std::string& json) {
    try {
        JsonParser parser(json);
        return parser.Parse();
    } catch (const JsonParseError& e) {
        LOG_ERROR("JSON parse error: {}", e.what());
        throw;
    }
}

std::string JsonParser::Stringify(const JsonValue& value, bool pretty, int indent) {
    std::ostringstream oss;
    
    auto write_indent = [&]() {
        if (pretty) {
            for (int i = 0; i < indent; ++i) {
                oss << "  ";
            }
        }
    };
    
    auto write_newline = [&]() {
        if (pretty) {
            oss << "\n";
        }
    };
    
    std::visit([&](auto&& val) {
        using T = std::decay_t<decltype(val)>;
        
        if constexpr (std::is_same_v<T, std::nullptr_t>) {
            oss << "null";
        }
        else if constexpr (std::is_same_v<T, bool>) {
            oss << (val ? "true" : "false");
        }
        else if constexpr (std::is_same_v<T, int>) {
            oss << val;
        }
        else if constexpr (std::is_same_v<T, double>) {
            oss << std::fixed << std::setprecision(6) << val;
        }
        else if constexpr (std::is_same_v<T, std::string>) {
            oss << "\"";
            for (char c : val) {
                switch (c) {
                    case '"': oss << "\\\""; break;
                    case '\\': oss << "\\\\"; break;
                    case '\b': oss << "\\b"; break;
                    case '\f': oss << "\\f"; break;
                    case '\n': oss << "\\n"; break;
                    case '\r': oss << "\\r"; break;
                    case '\t': oss << "\\t"; break;
                    default:
                        if (c < 32) {
                            oss << "\\u" << std::hex << std::setw(4) << std::setfill('0') << (int)c;
                        } else {
                            oss << c;
                        }
                        break;
                }
            }
            oss << "\"";
        }
        else if constexpr (std::is_same_v<T, std::vector<JsonValue>>) {
            oss << "[";
            write_newline();
            for (size_t i = 0; i < val.size(); ++i) {
                write_indent();
                oss << "  ";
                oss << Stringify(val[i], pretty, indent + 1);
                if (i < val.size() - 1) {
                    oss << ",";
                }
                write_newline();
            }
            write_indent();
            oss << "]";
        }
        else if constexpr (std::is_same_v<T, std::unordered_map<std::string, JsonValue>>) {
            oss << "{";
            write_newline();
            
            size_t count = 0;
            for (const auto& [key, value] : val) {
                write_indent();
                oss << "  \"" << key << "\": ";
                oss << Stringify(value, pretty, indent + 1);
                if (count < val.size() - 1) {
                    oss << ",";
                }
                write_newline();
                ++count;
            }
            write_indent();
            oss << "}";
        }
    }, value);
    
    return oss.str();
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
        ThrowError("Expected '" + std::string(1, c) + "' but found '" + std::string(1, Current()) + "'");
    }
    Advance();
}

JsonValue JsonParser::ParseValue() {
    SkipWhitespace();
    
    char c = Current();
    switch (c) {
        case '{':
            return ParseObject();
        case '[':
            return ParseArray();
        case '"':
            return ParseString();
        case 't':
        case 'f':
        case 'n':
            return ParseLiteral();
        default:
            if (c == '-' || std::isdigit(c)) {
                return ParseNumber();
            }
            ThrowError("Unexpected character: " + std::string(1, c));
    }
}

JsonValue JsonParser::ParseObject() {
    std::unordered_map<std::string, JsonValue> obj;
    
    Expect('{');
    SkipWhitespace();
    
    if (Current() == '}') {
        Advance();
        return obj;
    }
    
    while (true) {
        SkipWhitespace();
        
        // Parse key
        if (Current() != '"') {
            ThrowError("Expected string key in object");
        }
        std::string key = ParseStringValue();
        
        SkipWhitespace();
        Expect(':');
        SkipWhitespace();
        
        // Parse value
        JsonValue value = ParseValue();
        obj[key] = value;
        
        SkipWhitespace();
        if (Current() == '}') {
            Advance();
            break;
        } else if (Current() == ',') {
            Advance();
            continue;
        } else {
            ThrowError("Expected ',' or '}' in object");
        }
    }
    
    return obj;
}

JsonValue JsonParser::ParseArray() {
    std::vector<JsonValue> arr;
    
    Expect('[');
    SkipWhitespace();
    
    if (Current() == ']') {
        Advance();
        return arr;
    }
    
    while (true) {
        SkipWhitespace();
        JsonValue value = ParseValue();
        arr.push_back(value);
        
        SkipWhitespace();
        if (Current() == ']') {
            Advance();
            break;
        } else if (Current() == ',') {
            Advance();
            continue;
        } else {
            ThrowError("Expected ',' or ']' in array");
        }
    }
    
    return arr;
}

JsonValue JsonParser::ParseString() {
    return ParseStringValue();
}

std::string JsonParser::ParseStringValue() {
    Expect('"');
    
    std::string result;
    while (Current() != '"' && !IsAtEnd()) {
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
                    // Unicode escape \uXXXX
                    std::string hex;
                    for (int i = 0; i < 4; i++) {
                        Advance();
                        if (IsAtEnd() || !std::isxdigit(Current())) {
                            ThrowError("Invalid unicode escape");
                        }
                        hex += Current();
                    }
                    // For simplicity, just add the hex as is
                    result += "\\u" + hex;
                    break;
                }
                default:
                    ThrowError("Invalid escape character: \\" + std::string(1, escaped));
            }
        } else {
            result += Current();
        }
        Advance();
    }
    
    if (Current() != '"') {
        ThrowError("Unterminated string");
    }
    Advance();
    
    return result;
}

JsonValue JsonParser::ParseNumber() {
    std::string numStr;
    bool isFloat = false;
    
    if (Current() == '-') {
        numStr += Current();
        Advance();
    }
    
    if (!std::isdigit(Current())) {
        ThrowError("Invalid number format");
    }
    
    while (std::isdigit(Current())) {
        numStr += Current();
        Advance();
    }
    
    if (Current() == '.') {
        isFloat = true;
        numStr += Current();
        Advance();
        
        if (!std::isdigit(Current())) {
            ThrowError("Invalid number format after decimal point");
        }
        
        while (std::isdigit(Current())) {
            numStr += Current();
            Advance();
        }
    }
    
    if (Current() == 'e' || Current() == 'E') {
        isFloat = true;
        numStr += Current();
        Advance();
        
        if (Current() == '+' || Current() == '-') {
            numStr += Current();
            Advance();
        }
        
        if (!std::isdigit(Current())) {
            ThrowError("Invalid number format in exponent");
        }
        
        while (std::isdigit(Current())) {
            numStr += Current();
            Advance();
        }
    }
    
    try {
        if (isFloat) {
            return std::stod(numStr);
        } else {
            return std::stoi(numStr);
        }
    } catch (const std::exception& e) {
        ThrowError("Invalid number: " + numStr);
    }
}

JsonValue JsonParser::ParseLiteral() {
    if (json_text_.substr(pos_, 4) == "true") {
        pos_ += 4;
        return true;
    } else if (json_text_.substr(pos_, 5) == "false") {
        pos_ += 5;
        return false;
    } else if (json_text_.substr(pos_, 4) == "null") {
        pos_ += 4;
        return nullptr;
    } else {
        ThrowError("Invalid literal");
    }
}

void JsonParser::ThrowError(const std::string& message) {
    throw JsonParseError("JSON Parse Error at position " + std::to_string(pos_) + ": " + message);
}

// Helper functions implementation
namespace json {

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

bool AsBool(const JsonValue& value, bool defaultValue) {
    if (auto ptr = std::get_if<bool>(&value)) {
        return *ptr;
    }
    return defaultValue;
}

int AsInt(const JsonValue& value, int defaultValue) {
    if (auto ptr = std::get_if<int>(&value)) {
        return *ptr;
    }
    if (auto ptr = std::get_if<double>(&value)) {
        return static_cast<int>(*ptr);
    }
    return defaultValue;
}

double AsDouble(const JsonValue& value, double defaultValue) {
    if (auto ptr = std::get_if<double>(&value)) {
        return *ptr;
    }
    if (auto ptr = std::get_if<int>(&value)) {
        return static_cast<double>(*ptr);
    }
    return defaultValue;
}

std::string AsString(const JsonValue& value, const std::string& defaultValue) {
    if (auto ptr = std::get_if<std::string>(&value)) {
        return *ptr;
    }
    return defaultValue;
}

std::vector<JsonValue> AsArray(const JsonValue& value) {
    if (auto ptr = std::get_if<std::vector<JsonValue>>(&value)) {
        return *ptr;
    }
    return {};
}

std::unordered_map<std::string, JsonValue> AsObject(const JsonValue& value) {
    if (auto ptr = std::get_if<std::unordered_map<std::string, JsonValue>>(&value)) {
        return *ptr;
    }
    return {};
}

JsonValue GetPath(const JsonValue& root, const std::string& path) {
    if (path.empty()) {
        return root;
    }
    
    size_t dotPos = path.find('.');
    std::string key = (dotPos == std::string::npos) ? path : path.substr(0, dotPos);
    std::string remainingPath = (dotPos == std::string::npos) ? "" : path.substr(dotPos + 1);
    
    if (auto obj = std::get_if<std::unordered_map<std::string, JsonValue>>(&root)) {
        auto it = obj->find(key);
        if (it != obj->end()) {
            return GetPath(it->second, remainingPath);
        }
    }
    
    return nullptr; // Path not found
}

bool HasPath(const JsonValue& root, const std::string& path) {
    JsonValue result = GetPath(root, path);
    return !IsNull(result);
}

} // namespace json

} // namespace ats 