#pragma once

#include <string>
#include <vector>
#include <memory>
#include <openssl/evp.h>
#include <openssl/aes.h>
#include <openssl/rand.h>
#include <openssl/hmac.h>
#include <openssl/sha.h>

namespace ats {
namespace utils {

class CryptoUtils {
public:
    // AES-256-GCM encryption/decryption
    struct EncryptionResult {
        std::vector<uint8_t> encrypted_data;
        std::vector<uint8_t> iv;
        std::vector<uint8_t> tag;
        bool success;
        
        EncryptionResult() : success(false) {}
    };
    
    struct DecryptionResult {
        std::vector<uint8_t> decrypted_data;
        bool success;
        
        DecryptionResult() : success(false) {}
    };
    
    // Generate random bytes
    static std::vector<uint8_t> generate_random_bytes(size_t length);
    
    // Generate random key for AES-256
    static std::vector<uint8_t> generate_aes_key();
    
    // Generate random IV
    static std::vector<uint8_t> generate_iv(size_t length = 12);
    
    // AES-256-GCM encryption
    static EncryptionResult encrypt_aes_gcm(const std::vector<uint8_t>& plaintext,
                                           const std::vector<uint8_t>& key,
                                           const std::vector<uint8_t>& iv = {},
                                           const std::vector<uint8_t>& aad = {});
    
    // AES-256-GCM decryption
    static DecryptionResult decrypt_aes_gcm(const std::vector<uint8_t>& ciphertext,
                                           const std::vector<uint8_t>& key,
                                           const std::vector<uint8_t>& iv,
                                           const std::vector<uint8_t>& tag,
                                           const std::vector<uint8_t>& aad = {});
    
    // String versions of encryption/decryption
    static EncryptionResult encrypt_string(const std::string& plaintext,
                                          const std::string& key,
                                          const std::string& iv = "");
    
    static std::string decrypt_string(const EncryptionResult& encrypted,
                                     const std::string& key);
    
    // HMAC operations
    static std::vector<uint8_t> hmac_sha256(const std::vector<uint8_t>& data,
                                           const std::vector<uint8_t>& key);
    
    static std::string hmac_sha256_hex(const std::string& data,
                                      const std::string& key);
    
    static std::string hmac_sha256_base64(const std::string& data,
                                         const std::string& key);
    
    // Hash functions
    static std::vector<uint8_t> sha256(const std::vector<uint8_t>& data);
    static std::string sha256_hex(const std::string& data);
    static std::string sha256_base64(const std::string& data);
    
    // Encoding/Decoding utilities
    static std::string base64_encode(const std::vector<uint8_t>& data);
    static std::vector<uint8_t> base64_decode(const std::string& encoded);
    
    static std::string hex_encode(const std::vector<uint8_t>& data);
    static std::vector<uint8_t> hex_decode(const std::string& hex);
    
    // Password-based key derivation (PBKDF2)
    static std::vector<uint8_t> pbkdf2_sha256(const std::string& password,
                                             const std::vector<uint8_t>& salt,
                                             int iterations = 100000,
                                             size_t key_length = 32);
    
    // Secure string comparison (timing-safe)
    static bool secure_compare(const std::string& a, const std::string& b);
    
    // Generate secure random string
    static std::string generate_random_string(size_t length,
                                             const std::string& charset = "0123456789ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz");
    
    // API signature generation for exchanges
    static std::string generate_binance_signature(const std::string& query_string,
                                                 const std::string& secret_key);
    
    static std::string generate_upbit_signature(const std::string& access_key,
                                               const std::string& secret_key,
                                               const std::string& query_string = "");
    
    // Utility functions for secure memory handling
    static void secure_zero_memory(void* ptr, size_t size);
    
private:
    // OpenSSL initialization
    static void ensure_openssl_initialized();
    
    // Error handling
    static std::string get_openssl_error();
    
    // Internal helper functions
    static std::vector<uint8_t> string_to_bytes(const std::string& str);
    static std::string bytes_to_string(const std::vector<uint8_t>& bytes);
};

// RAII wrapper for secure string storage
class SecureString {
public:
    explicit SecureString(const std::string& str);
    explicit SecureString(size_t size);
    ~SecureString();
    
    SecureString(const SecureString&) = delete;
    SecureString& operator=(const SecureString&) = delete;
    
    SecureString(SecureString&& other) noexcept;
    SecureString& operator=(SecureString&& other) noexcept;
    
    const char* c_str() const;
    size_t size() const;
    bool empty() const;
    
    void clear();
    
private:
    char* data_;
    size_t size_;
    size_t capacity_;
    
    void secure_allocate(size_t size);
    void secure_deallocate();
};

// Configuration encryption helper
class ConfigEncryption {
public:
    static bool encrypt_config_file(const std::string& input_file,
                                   const std::string& output_file,
                                   const std::string& password);
    
    static bool decrypt_config_file(const std::string& input_file,
                                   const std::string& output_file,
                                   const std::string& password);
    
    static std::string encrypt_config_value(const std::string& value,
                                           const std::string& master_key);
    
    static std::string decrypt_config_value(const std::string& encrypted_value,
                                           const std::string& master_key);
    
private:
    static const std::string ENCRYPTION_PREFIX;
    static const size_t SALT_SIZE = 16;
    static const size_t IV_SIZE = 12;
    static const size_t TAG_SIZE = 16;
};

} // namespace utils
} // namespace ats