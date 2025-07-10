#pragma once

#include <string>
#include <map>
#include <memory>
#include <optional>

namespace ats {

class SecureConfig {
public:
    SecureConfig() = default;
    ~SecureConfig() = default;

    // Prevent copying to avoid sensitive data leaks
    SecureConfig(const SecureConfig&) = delete;
    SecureConfig& operator=(const SecureConfig&) = delete;

    // Allow moving
    SecureConfig(SecureConfig&&) = default;
    SecureConfig& operator=(SecureConfig&&) = default;

    // Load configuration from environment variables with fallback to JSON
    bool load_secure_config(const std::string& json_file_path);

    // Get configuration values
    std::optional<std::string> get_exchange_api_key(const std::string& exchange_name) const;
    std::optional<std::string> get_exchange_secret(const std::string& exchange_name) const;
    std::optional<std::string> get_telegram_token() const;
    std::optional<std::string> get_discord_webhook() const;

    // Validate configuration
    bool validate_exchange_credentials(const std::string& exchange_name) const;
    bool validate_notification_config() const;

private:
    // Environment variable names
    std::string get_env_var_name(const std::string& exchange, const std::string& key_type) const;
    std::optional<std::string> get_env_var(const std::string& var_name) const;

    // Secure storage for sensitive data
    std::map<std::string, std::string> secure_data_;
    
    // Load from JSON as fallback
    bool load_from_json(const std::string& json_file_path);
    
    // Clear sensitive data
    void clear_sensitive_data();
};

} // namespace ats