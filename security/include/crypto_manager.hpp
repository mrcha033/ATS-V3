#pragma once

#include <string>
#include <vector>
#include <memory>
#include <unordered_map>
#include <openssl/evp.h>
#include <openssl/aes.h>
#include <openssl/rand.h>
#include <openssl/hmac.h>
#include <openssl/sha.h>
#include <openssl/rsa.h>
#include <openssl/pem.h>
#include <openssl/x509.h>
#include <openssl/ssl.h>
#include <openssl/err.h>

namespace ats {
namespace security {

// Secure key storage and encryption
class CryptoManager {
public:
    CryptoManager();
    ~CryptoManager();
    
    // Lifecycle
    bool initialize(const std::string& master_key_path = "");
    void shutdown();
    
    // AES-256 Encryption/Decryption
    struct EncryptionResult {
        std::vector<uint8_t> encrypted_data;
        std::vector<uint8_t> iv;
        std::vector<uint8_t> tag;
        bool success = false;
    };
    
    EncryptionResult encrypt_aes256_gcm(const std::string& plaintext, const std::string& key_id = "default");
    std::string decrypt_aes256_gcm(const std::vector<uint8_t>& encrypted_data,
                                  const std::vector<uint8_t>& iv,
                                  const std::vector<uint8_t>& tag,
                                  const std::string& key_id = "default");
    
    // API Key Management
    bool store_encrypted_api_key(const std::string& exchange,
                                const std::string& api_key,
                                const std::string& secret_key,
                                const std::string& passphrase = "");
    
    struct ApiCredentials {
        std::string api_key;
        std::string secret_key;
        std::string passphrase;
        bool valid = false;
    };
    
    ApiCredentials retrieve_api_credentials(const std::string& exchange);
    bool delete_api_credentials(const std::string& exchange);
    std::vector<std::string> list_stored_exchanges();
    
    // HMAC Operations
    std::string generate_hmac_sha256(const std::string& data, const std::string& key);
    std::string generate_hmac_sha512(const std::string& data, const std::string& key);
    bool verify_hmac_sha256(const std::string& data, const std::string& key, const std::string& signature);
    bool verify_hmac_sha512(const std::string& data, const std::string& key, const std::string& signature);
    
    // Key Generation and Management
    std::vector<uint8_t> generate_random_key(size_t key_size = 32); // 32 bytes = 256 bits
    std::string generate_random_string(size_t length = 16);
    bool create_key_pair(const std::string& key_id);
    
    // Master Key Management
    bool set_master_key(const std::vector<uint8_t>& key);
    bool load_master_key_from_file(const std::string& file_path);
    bool save_master_key_to_file(const std::string& file_path);
    bool generate_new_master_key();
    
    // Utility functions
    std::string bytes_to_hex(const std::vector<uint8_t>& bytes);
    std::vector<uint8_t> hex_to_bytes(const std::string& hex);
    std::string bytes_to_base64(const std::vector<uint8_t>& bytes);
    std::vector<uint8_t> base64_to_bytes(const std::string& base64);
    
    // Security validation
    bool validate_key_strength(const std::string& key);
    int calculate_entropy(const std::string& data);
    
private:
    std::vector<uint8_t> master_key_;
    std::unordered_map<std::string, std::vector<uint8_t>> encryption_keys_;
    std::string key_storage_path_;
    
    // Internal encryption helpers
    bool derive_key_from_master(const std::string& key_id, std::vector<uint8_t>& derived_key);
    bool save_encrypted_credentials_to_file(const std::string& exchange, const EncryptionResult& credentials);
    bool load_encrypted_credentials_from_file(const std::string& exchange, EncryptionResult& credentials);
    
    // OpenSSL cleanup
    void cleanup_openssl();
    
    // Error handling
    std::string get_openssl_error();
    void log_security_event(const std::string& event, const std::string& details);
};

// Digital Signature Manager
class SignatureManager {
public:
    SignatureManager();
    ~SignatureManager();
    
    bool initialize();
    
    // RSA Key Management
    bool generate_rsa_keypair(const std::string& key_id, int key_size = 2048);
    bool load_rsa_keypair(const std::string& key_id, const std::string& private_key_path, const std::string& public_key_path);
    bool save_rsa_keypair(const std::string& key_id, const std::string& private_key_path, const std::string& public_key_path);
    
    // Digital Signatures
    std::string sign_data(const std::string& data, const std::string& key_id);
    bool verify_signature(const std::string& data, const std::string& signature, const std::string& key_id);
    
    // Certificate Management
    bool load_certificate(const std::string& cert_id, const std::string& cert_path);
    bool verify_certificate_chain(const std::string& cert_id);
    
private:
    std::unordered_map<std::string, EVP_PKEY*> private_keys_;
    std::unordered_map<std::string, EVP_PKEY*> public_keys_;
    std::unordered_map<std::string, X509*> certificates_;
    
    void cleanup_keys();
};

// Secure Random Number Generator
class SecureRandom {
public:
    static bool initialize();
    static std::vector<uint8_t> generate_bytes(size_t count);
    static uint32_t generate_uint32();
    static uint64_t generate_uint64();
    static std::string generate_hex_string(size_t length);
    static std::string generate_alphanumeric_string(size_t length);
    
private:
    static bool initialized_;
};

// Security Utilities
class SecurityUtils {
public:
    // Constant-time comparison to prevent timing attacks
    static bool secure_compare(const std::string& a, const std::string& b);
    static bool secure_compare(const std::vector<uint8_t>& a, const std::vector<uint8_t>& b);
    
    // Secure memory operations
    static void secure_zero_memory(void* ptr, size_t size);
    static void secure_zero_string(std::string& str);
    
    // Input validation
    static bool is_valid_base64(const std::string& input);
    static bool is_valid_hex(const std::string& input);
    static bool is_printable_ascii(const std::string& input);
    
    // Security headers and tokens
    static std::string generate_csrf_token();
    static bool validate_csrf_token(const std::string& token, const std::string& session_id);
    
    // Rate limiting helpers
    static std::string generate_rate_limit_key(const std::string& identifier, const std::string& action);
};

// Exception classes for security operations
class SecurityException : public std::exception {
public:
    explicit SecurityException(const std::string& message) : message_(message) {}
    const char* what() const noexcept override { return message_.c_str(); }
    
private:
    std::string message_;
};

class EncryptionException : public SecurityException {
public:
    explicit EncryptionException(const std::string& message) 
        : SecurityException("Encryption Error: " + message) {}
};

class DecryptionException : public SecurityException {
public:
    explicit DecryptionException(const std::string& message) 
        : SecurityException("Decryption Error: " + message) {}
};

class KeyManagementException : public SecurityException {
public:
    explicit KeyManagementException(const std::string& message) 
        : SecurityException("Key Management Error: " + message) {}
};

} // namespace security
} // namespace ats