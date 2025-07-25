#include "../include/totp_manager.hpp"
#include "../../utils/logger.hpp"
#include <openssl/hmac.h>
#include <openssl/sha.h>
#include <fstream>
#include <sstream>
#include <iomanip>
#include <algorithm>
#include <random>
#include <filesystem>
#include <ctime>

namespace ats {
namespace security {

// Base32 alphabet for secret key encoding
static const char BASE32_ALPHABET[] = "ABCDEFGHIJKLMNOPQRSTUVWXYZ234567";

TotpManager::TotpManager() {
    config_.time_step_seconds = 30;
    config_.code_digits = 6;
    config_.hash_algorithm = "SHA1";
    config_.tolerance_periods = 1;
    storage_path_ = "./security/2fa/";
}

TotpManager::~TotpManager() = default;

bool TotpManager::initialize(std::shared_ptr<CryptoManager> crypto_manager) {
    if (!crypto_manager) {
        LOG_ERROR("CryptoManager is null");
        return false;
    }
    
    crypto_manager_ = crypto_manager;
    
    // Create storage directory if it doesn't exist
    try {
        std::filesystem::create_directories(storage_path_);
        LOG_INFO("TotpManager initialized successfully");
        return true;
    } catch (const std::exception& e) {
        LOG_ERROR("Failed to create TOTP storage directory: {}", e.what());
        return false;
    }
}

TotpManager::TotpSecret TotpManager::generate_totp_secret(const std::string& user_id, 
                                                         const std::string& issuer,
                                                         const std::string& account_name) {
    TotpSecret secret;
    
    try {
        // Generate 160-bit (20-byte) secret key
        auto key_bytes = crypto_manager_->generate_random_key(20);
        secret.secret_key = base32_encode(key_bytes);
        
        // Generate backup codes
        auto backup_codes = generate_backup_codes(10);
        for (int i = 0; i < 10 && i < backup_codes.size(); i++) {
            secret.backup_codes[i] = backup_codes[i];
        }
        
        // Generate QR code URL
        std::string account = account_name.empty() ? user_id : account_name;
        secret.qr_code_url = generate_qr_code_url(secret.secret_key, account, issuer);
        
        secret.is_active = false;
        
        LOG_INFO("Generated TOTP secret for user: {}", user_id);
        return secret;
        
    } catch (const std::exception& e) {
        LOG_ERROR("Failed to generate TOTP secret for user {}: {}", user_id, e.what());
        return secret;
    }
}

bool TotpManager::store_totp_secret(const std::string& user_id, const TotpSecret& secret) {
    try {
        // Create user status
        UserTotpStatus status;
        status.user_id = user_id;
        status.is_enabled = false;
        status.is_verified = false;
        status.created_at = std::chrono::system_clock::now();
        status.backup_codes_remaining = 10;
        
        return save_user_totp_data(user_id, status, secret);
        
    } catch (const std::exception& e) {
        LOG_ERROR("Failed to store TOTP secret for user {}: {}", user_id, e.what());
        return false;
    }
}

TotpManager::TotpSecret TotpManager::get_totp_secret(const std::string& user_id) {
    TotpSecret secret;
    UserTotpStatus status;
    
    if (load_user_totp_data(user_id, status, secret)) {
        return secret;
    }
    
    return secret; // Return empty secret if not found
}

std::string TotpManager::generate_totp_code(const std::string& secret_key, 
                                           std::chrono::system_clock::time_point time_point) {
    try {
        auto key_bytes = base32_decode(secret_key);
        if (key_bytes.empty()) {
            throw TotpException("Invalid secret key format");
        }
        
        uint32_t code = totp(key_bytes, time_point, config_.time_step_seconds, config_.code_digits);
        
        // Format code with leading zeros
        std::ostringstream oss;
        oss << std::setfill('0') << std::setw(config_.code_digits) << code;
        return oss.str();
        
    } catch (const std::exception& e) {
        LOG_ERROR("Failed to generate TOTP code: {}", e.what());
        return "";
    }
}

bool TotpManager::verify_totp_code(const std::string& user_id, 
                                  const std::string& code,
                                  int time_window_seconds,
                                  int tolerance_periods) {
    try {
        // Check if user is locked
        if (is_user_2fa_locked(user_id)) {
            LOG_WARNING("2FA verification attempted for locked user: {}", user_id);
            return false;
        }
        
        // Get user's secret
        auto secret = get_totp_secret(user_id);
        if (secret.secret_key.empty()) {
            LOG_ERROR("No TOTP secret found for user: {}", user_id);
            record_failed_attempt(user_id);
            return false;
        }
        
        auto now = std::chrono::system_clock::now();
        auto key_bytes = base32_decode(secret.secret_key);
        
        // Check current time window and tolerance periods
        for (int i = -tolerance_periods; i <= tolerance_periods; i++) {
            auto test_time = now + std::chrono::seconds(i * time_window_seconds);
            uint32_t expected_code = totp(key_bytes, test_time, time_window_seconds, config_.code_digits);
            
            std::ostringstream oss;
            oss << std::setfill('0') << std::setw(config_.code_digits) << expected_code;
            
            if (oss.str() == code) {
                reset_failed_attempts(user_id);
                
                // Update last used time
                UserTotpStatus status;
                TotpSecret temp_secret;
                if (load_user_totp_data(user_id, status, temp_secret)) {
                    status.last_used = now;
                    save_user_totp_data(user_id, status, temp_secret);
                }
                
                LOG_INFO("TOTP verification successful for user: {}", user_id);
                return true;
            }
        }
        
        record_failed_attempt(user_id);
        LOG_WARNING("TOTP verification failed for user: {}", user_id);
        return false;
        
    } catch (const std::exception& e) {
        LOG_ERROR("Error during TOTP verification for user {}: {}", user_id, e.what());
        record_failed_attempt(user_id);
        return false;
    }
}

std::vector<std::string> TotpManager::generate_backup_codes(int count) {
    std::vector<std::string> codes;
    
    for (int i = 0; i < count; i++) {
        // Generate 8-character alphanumeric backup code
        std::string code = generate_secure_random_string(8);
        codes.push_back(code);
    }
    
    return codes;
}

std::string TotpManager::generate_qr_code_url(const std::string& secret_key,
                                             const std::string& user_account,
                                             const std::string& issuer) {
    std::ostringstream url;
    url << "otpauth://totp/";
    url << url_encode(issuer) << ":" << url_encode(user_account);
    url << "?secret=" << secret_key;
    url << "&issuer=" << url_encode(issuer);
    url << "&digits=" << config_.code_digits;
    url << "&period=" << config_.time_step_seconds;
    url << "&algorithm=" << config_.hash_algorithm;
    
    return url.str();
}

bool TotpManager::enable_2fa_for_user(const std::string& user_id) {
    UserTotpStatus status;
    TotpSecret secret;
    
    if (!load_user_totp_data(user_id, status, secret)) {
        LOG_ERROR("Cannot enable 2FA for user {} - no TOTP data found", user_id);
        return false;
    }
    
    status.is_enabled = true;
    if (save_user_totp_data(user_id, status, secret)) {
        LOG_INFO("2FA enabled for user: {}", user_id);
        return true;
    }
    
    return false;
}

TotpManager::UserTotpStatus TotpManager::get_user_2fa_status(const std::string& user_id) {
    UserTotpStatus status;
    TotpSecret secret;
    
    load_user_totp_data(user_id, status, secret);
    return status;
}

bool TotpManager::is_user_2fa_locked(const std::string& user_id) {
    auto status = get_user_2fa_status(user_id);
    if (!status.is_locked) {
        return false;
    }
    
    // Check if lock has expired
    auto now = std::chrono::system_clock::now();
    if (now >= status.locked_until) {
        unlock_user_2fa(user_id);
        return false;
    }
    
    return true;
}

void TotpManager::record_failed_attempt(const std::string& user_id) {
    UserTotpStatus status;
    TotpSecret secret;
    
    if (load_user_totp_data(user_id, status, secret)) {
        status.failed_attempts++;
        
        // Lock user after 5 failed attempts
        if (status.failed_attempts >= 5) {
            status.is_locked = true;
            status.locked_until = std::chrono::system_clock::now() + std::chrono::minutes(15);
            LOG_WARNING("User {} locked due to too many failed 2FA attempts", user_id);
        }
        
        save_user_totp_data(user_id, status, secret);
    }
}

// Private implementation methods

uint32_t TotpManager::hotp(const std::vector<uint8_t>& key, uint64_t counter, int digits) {
    // Convert counter to big-endian bytes
    std::vector<uint8_t> counter_bytes(8);
    for (int i = 7; i >= 0; --i) {
        counter_bytes[i] = counter & 0xFF;
        counter >>= 8;
    }
    
    // Calculate HMAC-SHA1
    auto hash = hmac_sha1(key, counter_bytes);
    
    // Dynamic truncation
    int offset = hash[hash.size() - 1] & 0x0F;
    uint32_t code = ((hash[offset] & 0x7F) << 24) |
                    ((hash[offset + 1] & 0xFF) << 16) |
                    ((hash[offset + 2] & 0xFF) << 8) |
                    (hash[offset + 3] & 0xFF);
    
    // Apply modulo to get desired number of digits
    uint32_t modulo = 1;
    for (int i = 0; i < digits; i++) {
        modulo *= 10;
    }
    
    return code % modulo;
}

uint32_t TotpManager::totp(const std::vector<uint8_t>& key, 
                          std::chrono::system_clock::time_point time_point, 
                          int time_step_seconds, 
                          int digits) {
    uint64_t counter = get_time_counter(time_point, time_step_seconds);
    return hotp(key, counter, digits);
}

std::string TotpManager::base32_encode(const std::vector<uint8_t>& data) {
    std::string result;
    int buffer = 0;
    int buffer_bits = 0;
    
    for (uint8_t byte : data) {
        buffer = (buffer << 8) | byte;
        buffer_bits += 8;
        
        while (buffer_bits >= 5) {
            int index = (buffer >> (buffer_bits - 5)) & 0x1F;
            result += BASE32_ALPHABET[index];
            buffer_bits -= 5;
        }
    }
    
    if (buffer_bits > 0) {
        int index = (buffer << (5 - buffer_bits)) & 0x1F;
        result += BASE32_ALPHABET[index];
    }
    
    return result;
}

std::vector<uint8_t> TotpManager::base32_decode(const std::string& encoded) {
    std::vector<uint8_t> result;
    int buffer = 0;
    int buffer_bits = 0;
    
    for (char c : encoded) {
        if (c == '=') break; // Padding
        
        // Find character in alphabet
        int value = -1;
        for (int i = 0; i < 32; i++) {
            if (BASE32_ALPHABET[i] == std::toupper(c)) {
                value = i;
                break;
            }
        }
        
        if (value == -1) {
            LOG_ERROR("Invalid Base32 character: {}", c);
            return {};
        }
        
        buffer = (buffer << 5) | value;
        buffer_bits += 5;
        
        if (buffer_bits >= 8) {
            result.push_back((buffer >> (buffer_bits - 8)) & 0xFF);
            buffer_bits -= 8;
        }
    }
    
    return result;
}

std::vector<uint8_t> TotpManager::hmac_sha1(const std::vector<uint8_t>& key, const std::vector<uint8_t>& data) {
    std::vector<uint8_t> result(SHA_DIGEST_LENGTH);
    unsigned int len = 0;
    
    HMAC(EVP_sha1(), key.data(), static_cast<int>(key.size()),
         data.data(), data.size(), result.data(), &len);
    
    result.resize(len);
    return result;
}

uint64_t TotpManager::get_time_counter(std::chrono::system_clock::time_point time_point, int time_step_seconds) {
    auto epoch_time = time_point.time_since_epoch();
    auto seconds = std::chrono::duration_cast<std::chrono::seconds>(epoch_time).count();
    return seconds / time_step_seconds;
}

std::string TotpManager::generate_secure_random_string(int length) {
    const char charset[] = "ABCDEFGHIJKLMNOPQRSTUVWXYZ0123456789";
    const int charset_size = sizeof(charset) - 1;
    
    std::string result;
    result.reserve(length);
    
    for (int i = 0; i < length; i++) {
        uint32_t random_value = SecureRandom::generate_uint32();
        result += charset[random_value % charset_size];
    }
    
    return result;
}

std::string TotpManager::url_encode(const std::string& input) {
    std::ostringstream encoded;
    encoded << std::hex;
    
    for (char c : input) {
        if (std::isalnum(c) || c == '-' || c == '_' || c == '.' || c == '~') {
            encoded << c;
        } else {
            encoded << '%' << std::setw(2) << int(static_cast<unsigned char>(c));
        }
    }
    
    return encoded.str();
}

bool TotpManager::save_user_totp_data(const std::string& user_id, const UserTotpStatus& status, const TotpSecret& secret) {
    try {
        std::string file_path = storage_path_ + user_id + ".2fa";
        
        // Encrypt the secret data
        std::ostringstream data_stream;
        data_stream << secret.secret_key << "\n";
        for (int i = 0; i < 10; i++) {
            data_stream << secret.backup_codes[i] << "\n";
        }
        data_stream << secret.qr_code_url << "\n";
        data_stream << (secret.is_active ? "1" : "0") << "\n";
        data_stream << (status.is_enabled ? "1" : "0") << "\n";
        data_stream << (status.is_verified ? "1" : "0") << "\n";
        data_stream << std::chrono::duration_cast<std::chrono::seconds>(status.created_at.time_since_epoch()).count() << "\n";
        data_stream << std::chrono::duration_cast<std::chrono::seconds>(status.last_used.time_since_epoch()).count() << "\n";
        data_stream << status.backup_codes_remaining << "\n";
        data_stream << status.failed_attempts << "\n";
        data_stream << (status.is_locked ? "1" : "0") << "\n";
        data_stream << std::chrono::duration_cast<std::chrono::seconds>(status.locked_until.time_since_epoch()).count() << "\n";
        
        auto encrypted = crypto_manager_->encrypt_aes256_gcm(data_stream.str(), "totp_" + user_id);
        if (!encrypted.success) {
            LOG_ERROR("Failed to encrypt TOTP data for user: {}", user_id);
            return false;
        }
        
        // Save encrypted data to file
        std::ofstream file(file_path, std::ios::binary);
        if (!file) {
            LOG_ERROR("Failed to open TOTP file for writing: {}", file_path);
            return false;
        }
        
        // Write IV, tag, and encrypted data
        file.write(reinterpret_cast<const char*>(encrypted.iv.data()), encrypted.iv.size());
        file.write(reinterpret_cast<const char*>(encrypted.tag.data()), encrypted.tag.size());
        file.write(reinterpret_cast<const char*>(encrypted.encrypted_data.data()), encrypted.encrypted_data.size());
        
        return true;
        
    } catch (const std::exception& e) {
        LOG_ERROR("Failed to save TOTP data for user {}: {}", user_id, e.what());
        return false;
    }
}

bool TotpManager::load_user_totp_data(const std::string& user_id, UserTotpStatus& status, TotpSecret& secret) {
    try {
        std::string file_path = storage_path_ + user_id + ".2fa";
        
        if (!std::filesystem::exists(file_path)) {
            return false;
        }
        
        std::ifstream file(file_path, std::ios::binary);
        if (!file) {
            LOG_ERROR("Failed to open TOTP file for reading: {}", file_path);
            return false;
        }
        
        // Read IV, tag, and encrypted data
        std::vector<uint8_t> iv(12);
        std::vector<uint8_t> tag(16);
        file.read(reinterpret_cast<char*>(iv.data()), iv.size());
        file.read(reinterpret_cast<char*>(tag.data()), tag.size());
        
        std::vector<uint8_t> encrypted_data;
        file.seekg(0, std::ios::end);
        size_t file_size = file.tellg();
        file.seekg(28, std::ios::beg); // Skip IV and tag
        
        size_t data_size = file_size - 28;
        encrypted_data.resize(data_size);
        file.read(reinterpret_cast<char*>(encrypted_data.data()), data_size);
        
        // Decrypt data
        std::string decrypted = crypto_manager_->decrypt_aes256_gcm(encrypted_data, iv, tag, "totp_" + user_id);
        if (decrypted.empty()) {
            LOG_ERROR("Failed to decrypt TOTP data for user: {}", user_id);
            return false;
        }
        
        // Parse decrypted data
        std::istringstream data_stream(decrypted);
        std::string line;
        
        std::getline(data_stream, secret.secret_key);
        for (int i = 0; i < 10; i++) {
            std::getline(data_stream, secret.backup_codes[i]);
        }
        std::getline(data_stream, secret.qr_code_url);
        
        std::getline(data_stream, line);
        secret.is_active = (line == "1");
        
        std::getline(data_stream, line);
        status.is_enabled = (line == "1");
        
        std::getline(data_stream, line);
        status.is_verified = (line == "1");
        
        std::getline(data_stream, line);
        status.created_at = std::chrono::system_clock::from_time_t(std::stoll(line));
        
        std::getline(data_stream, line);
        status.last_used = std::chrono::system_clock::from_time_t(std::stoll(line));
        
        std::getline(data_stream, line);
        status.backup_codes_remaining = std::stoi(line);
        
        std::getline(data_stream, line);
        status.failed_attempts = std::stoi(line);
        
        std::getline(data_stream, line);
        status.is_locked = (line == "1");
        
        std::getline(data_stream, line);
        status.locked_until = std::chrono::system_clock::from_time_t(std::stoll(line));
        
        status.user_id = user_id;
        
        return true;
        
    } catch (const std::exception& e) {
        LOG_ERROR("Failed to load TOTP data for user {}: {}", user_id, e.what());
        return false;
    }
}

void TotpManager::unlock_user_2fa(const std::string& user_id) {
    UserTotpStatus status;
    TotpSecret secret;
    
    if (load_user_totp_data(user_id, status, secret)) {
        status.is_locked = false;
        status.locked_until = std::chrono::system_clock::time_point{};
        save_user_totp_data(user_id, status, secret);
        LOG_INFO("2FA unlocked for user: {}", user_id);
    }
}

void TotpManager::reset_failed_attempts(const std::string& user_id) {
    UserTotpStatus status;
    TotpSecret secret;
    
    if (load_user_totp_data(user_id, status, secret)) {
        status.failed_attempts = 0;
        save_user_totp_data(user_id, status, secret);
    }
}

// TwoFactorAuth Implementation

TwoFactorAuth::TwoFactorAuth(std::shared_ptr<TotpManager> totp_manager) 
    : totp_manager_(totp_manager) {
    if (!totp_manager_) {
        throw TwoFactorAuthException("TotpManager is null");
    }
}

TwoFactorAuth::~TwoFactorAuth() = default;

TwoFactorAuth::AuthResult TwoFactorAuth::authenticate_user(const std::string& user_id, 
                                                          const std::string& totp_code) {
    try {
        // Check if user exists and has 2FA enabled
        auto status = totp_manager_->get_user_2fa_status(user_id);
        if (status.user_id.empty()) {
            return AuthResult::USER_NOT_FOUND;
        }
        
        if (!status.is_enabled) {
            return AuthResult::SETUP_REQUIRED;
        }
        
        if (status.is_locked) {
            return AuthResult::USER_LOCKED;
        }
        
        // Verify TOTP code
        if (totp_manager_->verify_totp_code(user_id, totp_code)) {
            return AuthResult::SUCCESS;
        } else {
            return AuthResult::INVALID_CODE;
        }
        
    } catch (const std::exception& e) {
        LOG_ERROR("Error during 2FA authentication for user {}: {}", user_id, e.what());
        return AuthResult::ERROR;
    }
}

std::string TwoFactorAuth::start_2fa_setup(const std::string& user_id) {
    try {
        // Generate setup session
        SetupSession session;
        session.session_id = generate_session_id();
        session.user_id = user_id;
        session.expires_at = std::chrono::system_clock::now() + std::chrono::minutes(30);
        session.temp_secret = totp_manager_->generate_totp_secret(user_id);
        
        setup_sessions_[session.session_id] = session;
        
        LOG_INFO("Started 2FA setup for user: {}", user_id);
        return session.session_id;
        
    } catch (const std::exception& e) {
        LOG_ERROR("Failed to start 2FA setup for user {}: {}", user_id, e.what());
        return "";
    }
}

TwoFactorAuth::AuthResult TwoFactorAuth::verify_setup_code(const std::string& session_id, 
                                                          const std::string& verification_code) {
    auto it = setup_sessions_.find(session_id);
    if (it == setup_sessions_.end()) {
        return AuthResult::ERROR;
    }
    
    auto& session = it->second;
    if (!is_setup_session_valid(session)) {
        setup_sessions_.erase(it);
        return AuthResult::ERROR;
    }
    
    // Verify the code using temporary secret
    std::string generated_code = totp_manager_->generate_totp_code(session.temp_secret.secret_key);
    if (generated_code == verification_code) {
        session.is_verified = true;
        return AuthResult::SUCCESS;
    }
    
    return AuthResult::INVALID_CODE;
}

bool TwoFactorAuth::complete_2fa_setup(const std::string& session_id) {
    auto it = setup_sessions_.find(session_id);
    if (it == setup_sessions_.end()) {
        return false;
    }
    
    auto& session = it->second;
    if (!is_setup_session_valid(session) || !session.is_verified) {
        setup_sessions_.erase(it);
        return false;
    }
    
    // Store the secret and enable 2FA for the user
    bool success = totp_manager_->store_totp_secret(session.user_id, session.temp_secret) &&
                   totp_manager_->enable_2fa_for_user(session.user_id);
    
    setup_sessions_.erase(it);
    
    if (success) {
        LOG_INFO("Completed 2FA setup for user: {}", session.user_id);
    }
    
    return success;
}

std::string TwoFactorAuth::generate_session_id() {
    return totp_manager_->generate_secure_random_string(32);
}

bool TwoFactorAuth::is_setup_session_valid(const SetupSession& session) {
    auto now = std::chrono::system_clock::now();
    return now < session.expires_at;
}

} // namespace security
} // namespace ats