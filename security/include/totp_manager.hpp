#pragma once

#include "crypto_manager.hpp"
#include <string>
#include <vector>
#include <memory>
#include <chrono>
#include <cstdint>

namespace ats {
namespace security {

// Time-based One-Time Password (TOTP) Manager for Google Authenticator
class TotpManager {
public:
    TotpManager();
    ~TotpManager();
    
    bool initialize(std::shared_ptr<CryptoManager> crypto_manager);
    
    // Secret key generation and management
    struct TotpSecret {
        std::string secret_key;     // Base32 encoded secret
        std::string backup_codes[10]; // Recovery codes
        std::string qr_code_url;    // URL for QR code generation
        bool is_active = false;
    };
    
    // Generate new TOTP secret for user
    TotpSecret generate_totp_secret(const std::string& user_id, 
                                   const std::string& issuer = "ATS Trading System",
                                   const std::string& account_name = "");
    
    // Store encrypted TOTP secret
    bool store_totp_secret(const std::string& user_id, const TotpSecret& secret);
    
    // Retrieve TOTP secret for user
    TotpSecret get_totp_secret(const std::string& user_id);
    
    // Remove TOTP secret for user
    bool remove_totp_secret(const std::string& user_id);
    
    // TOTP Code generation and verification
    std::string generate_totp_code(const std::string& secret_key, 
                                  std::chrono::system_clock::time_point time_point = std::chrono::system_clock::now());
    
    // Verify TOTP code with time window tolerance
    bool verify_totp_code(const std::string& user_id, 
                         const std::string& code,
                         int time_window_seconds = 30,
                         int tolerance_periods = 1);
    
    // Backup code management
    std::vector<std::string> generate_backup_codes(int count = 10);
    bool verify_backup_code(const std::string& user_id, const std::string& backup_code);
    bool use_backup_code(const std::string& user_id, const std::string& backup_code);
    std::vector<std::string> get_remaining_backup_codes(const std::string& user_id);
    
    // QR Code generation support
    std::string generate_qr_code_url(const std::string& secret_key,
                                    const std::string& user_account,
                                    const std::string& issuer);
    
    // TOTP Configuration
    struct TotpConfig {
        int time_step_seconds = 30;     // Standard TOTP time window
        int code_digits = 6;            // Standard 6-digit codes
        std::string hash_algorithm = "SHA1"; // SHA1, SHA256, SHA512
        int tolerance_periods = 1;       // Allow ±1 time period
    };
    
    bool configure_totp(const TotpConfig& config);
    TotpConfig get_totp_config() const;
    
    // User 2FA status management
    struct UserTotpStatus {
        std::string user_id;
        bool is_enabled = false;
        bool is_verified = false;
        std::chrono::system_clock::time_point created_at;
        std::chrono::system_clock::time_point last_used;
        int backup_codes_remaining = 0;
        int failed_attempts = 0;
        bool is_locked = false;
        std::chrono::system_clock::time_point locked_until;
    };
    
    bool enable_2fa_for_user(const std::string& user_id);
    bool disable_2fa_for_user(const std::string& user_id);
    bool verify_2fa_setup(const std::string& user_id, const std::string& verification_code);
    UserTotpStatus get_user_2fa_status(const std::string& user_id);
    
    // Security features
    bool is_user_2fa_locked(const std::string& user_id);
    void lock_user_2fa(const std::string& user_id, std::chrono::minutes duration = std::chrono::minutes(15));
    void unlock_user_2fa(const std::string& user_id);
    void record_failed_attempt(const std::string& user_id);
    void reset_failed_attempts(const std::string& user_id);
    
    // Batch operations
    std::vector<std::string> list_users_with_2fa();
    bool backup_all_2fa_data(const std::string& backup_file_path);
    bool restore_2fa_data(const std::string& backup_file_path);
    
    // Administrative functions
    void cleanup_expired_secrets();
    void generate_security_report();
    
private:
    std::shared_ptr<CryptoManager> crypto_manager_;
    TotpConfig config_;
    std::string storage_path_;
    
    // TOTP algorithm implementation
    uint32_t hotp(const std::vector<uint8_t>& key, uint64_t counter, int digits);
    uint32_t totp(const std::vector<uint8_t>& key, 
                  std::chrono::system_clock::time_point time_point, 
                  int time_step_seconds, 
                  int digits);
    
    // Base32 encoding/decoding for secret keys
    std::string base32_encode(const std::vector<uint8_t>& data);
    std::vector<uint8_t> base32_decode(const std::string& encoded);
    
    // HMAC implementation for TOTP
    std::vector<uint8_t> hmac_sha1(const std::vector<uint8_t>& key, const std::vector<uint8_t>& data);
    
    // File operations for persistent storage
    bool save_user_totp_data(const std::string& user_id, const UserTotpStatus& status, const TotpSecret& secret);
    bool load_user_totp_data(const std::string& user_id, UserTotpStatus& status, TotpSecret& secret);
    bool delete_user_totp_data(const std::string& user_id);
    
    // URL encoding for QR codes
    std::string url_encode(const std::string& input);
    
    // Security utilities
    std::string generate_secure_random_string(int length);
    uint64_t get_time_counter(std::chrono::system_clock::time_point time_point, int time_step_seconds);
    
    // Validation helpers
    bool is_valid_totp_code(const std::string& code);
    bool is_valid_backup_code(const std::string& code);
    
    // Logging and monitoring
    void log_2fa_event(const std::string& user_id, const std::string& event, const std::string& details);
};

// 2FA Integration Manager for login workflows
class TwoFactorAuth {
public:
    TwoFactorAuth(std::shared_ptr<TotpManager> totp_manager);
    ~TwoFactorAuth();
    
    // Login workflow integration
    enum class AuthResult {
        SUCCESS,
        INVALID_CODE,
        USER_NOT_FOUND,
        USER_LOCKED,
        SETUP_REQUIRED,
        BACKUP_CODE_USED,
        ERROR
    };
    
    // Primary authentication with 2FA
    AuthResult authenticate_user(const std::string& user_id, 
                               const std::string& totp_code);
    
    // Backup code authentication
    AuthResult authenticate_with_backup_code(const std::string& user_id, 
                                            const std::string& backup_code);
    
    // 2FA setup workflow
    struct SetupSession {
        std::string session_id;
        std::string user_id;
        TotpManager::TotpSecret temp_secret;
        std::chrono::system_clock::time_point expires_at;
        bool is_verified = false;
    };
    
    std::string start_2fa_setup(const std::string& user_id);
    SetupSession get_setup_session(const std::string& session_id);
    AuthResult verify_setup_code(const std::string& session_id, const std::string& verification_code);
    bool complete_2fa_setup(const std::string& session_id);
    bool cancel_2fa_setup(const std::string& session_id);
    
    // Emergency access
    bool generate_emergency_bypass_code(const std::string& user_id, 
                                       std::chrono::minutes validity = std::chrono::minutes(60));
    bool use_emergency_bypass_code(const std::string& user_id, const std::string& bypass_code);
    
    // Administrative functions
    bool force_disable_2fa(const std::string& user_id, const std::string& admin_reason);
    bool reset_user_2fa(const std::string& user_id);
    
private:
    std::shared_ptr<TotpManager> totp_manager_;
    std::unordered_map<std::string, SetupSession> setup_sessions_;
    std::unordered_map<std::string, std::string> emergency_codes_;
    
    // Session management
    void cleanup_expired_setup_sessions();
    void cleanup_expired_emergency_codes();
    
    // Utilities
    std::string generate_session_id();
    bool is_setup_session_valid(const SetupSession& session);
};

// Exception classes for 2FA operations
class TotpException : public SecurityException {
public:
    explicit TotpException(const std::string& message) 
        : SecurityException("TOTP Error: " + message) {}
};

class TwoFactorAuthException : public SecurityException {
public:
    explicit TwoFactorAuthException(const std::string& message) 
        : SecurityException("2FA Error: " + message) {}
};

} // namespace security
} // namespace ats