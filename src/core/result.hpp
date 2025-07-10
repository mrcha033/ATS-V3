#pragma once

#include <optional>
#include <string>
#include <variant>

namespace ats {

template<typename T>
class Result {
private:
    std::variant<T, std::string> value_;

public:
    explicit Result(T value) : value_(std::move(value)) {}
    explicit Result(std::string error) : value_(std::move(error)) {}

    static Result<T> success(T value) {
        return Result<T>(std::move(value));
    }

    static Result<T> error(std::string error) {
        return Result<T>(std::move(error));
    }

    bool is_success() const {
        return std::holds_alternative<T>(value_);
    }

    bool is_error() const {
        return std::holds_alternative<std::string>(value_);
    }

    const T& value() const {
        return std::get<T>(value_);
    }

    T& value() {
        return std::get<T>(value_);
    }

    const std::string& error() const {
        return std::get<std::string>(value_);
    }

    T value_or(T default_value) const {
        if (is_success()) {
            return value();
        }
        return default_value;
    }

    template<typename F>
    auto map(F&& f) const -> Result<decltype(f(value()))> {
        if (is_success()) {
            return Result<decltype(f(value()))>::success(f(value()));
        }
        return Result<decltype(f(value()))>::error(error());
    }

    template<typename F>
    auto and_then(F&& f) const -> decltype(f(value())) {
        if (is_success()) {
            return f(value());
        }
        return decltype(f(value()))::error(error());
    }
};

} // namespace ats