#pragma once

#include <string>
#include <vector>
#include <optional>
#include <functional>
#include <nlohmann/json.hpp>
#include "config_types.hpp"
#include "../core/result.hpp"

namespace ats {

struct ValidationError {
    std::string field;
    std::string message;
    std::string value;
};

class ConfigValidator {
public:
    using ValidationResult = Result<bool>;
    using ValidationErrors = std::vector<ValidationError>;

    // Validate complete configuration
    static ValidationResult validate_config(const nlohmann::json& config);
    
    // Validate specific sections
    static ValidationResult validate_app_config(const nlohmann::json& app_config);
    static ValidationResult validate_exchange_config(const nlohmann::json& exchange_config);
    static ValidationResult validate_trading_config(const nlohmann::json& trading_config);
    static ValidationResult validate_arbitrage_config(const nlohmann::json& arbitrage_config);
    static ValidationResult validate_risk_config(const nlohmann::json& risk_config);
    static ValidationResult validate_monitoring_config(const nlohmann::json& monitoring_config);
    static ValidationResult validate_database_config(const nlohmann::json& database_config);
    static ValidationResult validate_logging_config(const nlohmann::json& logging_config);
    static ValidationResult validate_alerts_config(const nlohmann::json& alerts_config);

    // Get all validation errors
    static const ValidationErrors& get_errors() { return errors_; }
    
    // Clear errors
    static void clear_errors() { errors_.clear(); }

private:
    static ValidationErrors errors_;
    
    // Validation helper functions
    static bool validate_required_field(const nlohmann::json& config, const std::string& field);
    static bool validate_string_field(const nlohmann::json& config, const std::string& field, 
                                     size_t min_length = 0, size_t max_length = SIZE_MAX);
    static bool validate_numeric_field(const nlohmann::json& config, const std::string& field,
                                      double min_value = -std::numeric_limits<double>::infinity(),
                                      double max_value = std::numeric_limits<double>::infinity());
    static bool validate_boolean_field(const nlohmann::json& config, const std::string& field);
    static bool validate_array_field(const nlohmann::json& config, const std::string& field,
                                    size_t min_size = 0, size_t max_size = SIZE_MAX);
    static bool validate_url_field(const nlohmann::json& config, const std::string& field);
    static bool validate_enum_field(const nlohmann::json& config, const std::string& field,
                                   const std::vector<std::string>& valid_values);
    static bool validate_percentage_field(const nlohmann::json& config, const std::string& field);
    static bool validate_positive_field(const nlohmann::json& config, const std::string& field);
    static bool validate_trading_pair(const std::string& pair);
    static bool validate_file_path(const std::string& path, bool must_exist = false);
    
    // Add error helper
    static void add_error(const std::string& field, const std::string& message, 
                         const std::string& value = "");
};

} // namespace ats