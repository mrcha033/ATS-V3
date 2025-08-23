#include "crypto_manager.hpp"
#include "utils/logger.hpp"
#include <fstream>
#include <iomanip>
#include <sstream>
#include <algorithm>
#include <random>
#include <filesystem>
#include <openssl/kdf.h>
#include <openssl/evp.h>
#include <openssl/bio.h>
#include <openssl/buffer.h>

namespace ats {
namespace security {

// CryptoManager Implementation
CryptoManager::CryptoManager() {
    // Initialize OpenSSL
    SSL_library_init();
    SSL_load_error_strings();
    OpenSSL_add_all_algorithms();
    
    utils::Logger::info("CryptoManager initialized");
}

CryptoManager::~CryptoManager() {
    shutdown();
}

bool CryptoManager::initialize(const std::string& master_key_path) {
    try {
        // Set key storage path
        if (master_key_path.empty()) {
            key_storage_path_ = "./security/keys/";
        } else {
            key_storage_path_ = master_key_path;
        }
        
        // Create directory if it doesn't exist
        std::filesystem::create_directories(key_storage_path_);
        
        // Try to load existing master key or generate new one
        std::string master_key_file = key_storage_path_ + "master.key";
        if (std::filesystem::exists(master_key_file)) {
            if (!load_master_key_from_file(master_key_file)) {
                utils::Logger::error("Failed to load existing master key");
                return false;
            }
            utils::Logger::info("Loaded existing master key");
        } else {
            if (!generate_new_master_key()) {
                utils::Logger::error("Failed to generate new master key");
                return false;
            }
            if (!save_master_key_to_file(master_key_file)) {
                utils::Logger::error("Failed to save new master key");
                return false;
            }
            utils::Logger::info("Generated and saved new master key");
        }
        
        utils::Logger::info("CryptoManager initialized successfully");
        return true;
        
    } catch (const std::exception& e) {
        utils::Logger::error("Failed to initialize CryptoManager: {}", e.what());
        return false;
    }
}

void CryptoManager::shutdown() {
    // Clear sensitive data
    SecurityUtils::secure_zero_memory(master_key_.data(), master_key_.size());
    master_key_.clear();
    
    for (auto& pair : encryption_keys_) {
        SecurityUtils::secure_zero_memory(pair.second.data(), pair.second.size());
    }
    encryption_keys_.clear();
    
    cleanup_openssl();
    utils::Logger::info("CryptoManager shutdown completed");
}

CryptoManager::EncryptionResult CryptoManager::encrypt_aes256_gcm(const std::string& plaintext, const std::string& key_id) {
    EncryptionResult result;
    
    try {
        // Derive encryption key
        std::vector<uint8_t> key(32); // 256 bits
        if (!derive_key_from_master(key_id, key)) {
            throw EncryptionException("Failed to derive encryption key");
        }
        
        // Generate random IV (12 bytes for GCM)
        result.iv = generate_random_key(12);
        
        // Set up encryption context
        EVP_CIPHER_CTX* ctx = EVP_CIPHER_CTX_new();
        if (!ctx) {
            throw EncryptionException("Failed to create cipher context");
        }
        
        // Initialize encryption
        if (EVP_EncryptInit_ex(ctx, EVP_aes_256_gcm(), nullptr, nullptr, nullptr) != 1) {
            EVP_CIPHER_CTX_free(ctx);
            throw EncryptionException("Failed to initialize AES-256-GCM encryption");
        }
        
        // Set IV length
        if (EVP_CIPHER_CTX_ctrl(ctx, EVP_CTRL_GCM_SET_IVLEN, 12, nullptr) != 1) {
            EVP_CIPHER_CTX_free(ctx);
            throw EncryptionException("Failed to set IV length");
        }
        
        // Initialize key and IV
        if (EVP_EncryptInit_ex(ctx, nullptr, nullptr, key.data(), result.iv.data()) != 1) {
            EVP_CIPHER_CTX_free(ctx);
            throw EncryptionException("Failed to set key and IV");
        }
        
        // Encrypt the plaintext
        result.encrypted_data.resize(plaintext.length() + 16); // Extra space for potential padding
        int len;
        int ciphertext_len;
        
        if (EVP_EncryptUpdate(ctx, result.encrypted_data.data(), &len,
                             reinterpret_cast<const unsigned char*>(plaintext.c_str()),
                             static_cast<int>(plaintext.length())) != 1) {
            EVP_CIPHER_CTX_free(ctx);
            throw EncryptionException("Failed to encrypt data");
        }
        ciphertext_len = len;
        
        // Finalize encryption
        if (EVP_EncryptFinal_ex(ctx, result.encrypted_data.data() + len, &len) != 1) {
            EVP_CIPHER_CTX_free(ctx);
            throw EncryptionException("Failed to finalize encryption");
        }
        ciphertext_len += len;
        
        // Get the authentication tag
        result.tag.resize(16); // 128-bit tag
        if (EVP_CIPHER_CTX_ctrl(ctx, EVP_CTRL_GCM_GET_TAG, 16, result.tag.data()) != 1) {
            EVP_CIPHER_CTX_free(ctx);
            throw EncryptionException("Failed to get authentication tag");
        }
        
        // Resize to actual encrypted data length
        result.encrypted_data.resize(ciphertext_len);
        
        EVP_CIPHER_CTX_free(ctx);
        
        // Clear the derived key
        SecurityUtils::secure_zero_memory(key.data(), key.size());
        
        result.success = true;
        utils::Logger::debug("Successfully encrypted {} bytes", plaintext.length());
        
    } catch (const std::exception& e) {
        utils::Logger::error("Encryption failed: {}", e.what());
        result.success = false;
    }
    
    return result;
}

std::string CryptoManager::decrypt_aes256_gcm(const std::vector<uint8_t>& encrypted_data,
                                             const std::vector<uint8_t>& iv,
                                             const std::vector<uint8_t>& tag,
                                             const std::string& key_id) {
    try {
        // Derive decryption key
        std::vector<uint8_t> key(32); // 256 bits
        if (!derive_key_from_master(key_id, key)) {
            throw DecryptionException("Failed to derive decryption key");
        }
        
        // Set up decryption context
        EVP_CIPHER_CTX* ctx = EVP_CIPHER_CTX_new();
        if (!ctx) {
            throw DecryptionException("Failed to create cipher context");
        }
        
        // Initialize decryption
        if (EVP_DecryptInit_ex(ctx, EVP_aes_256_gcm(), nullptr, nullptr, nullptr) != 1) {
            EVP_CIPHER_CTX_free(ctx);
            throw DecryptionException("Failed to initialize AES-256-GCM decryption");
        }
        
        // Set IV length
        if (EVP_CIPHER_CTX_ctrl(ctx, EVP_CTRL_GCM_SET_IVLEN, iv.size(), nullptr) != 1) {
            EVP_CIPHER_CTX_free(ctx);
            throw DecryptionException("Failed to set IV length");
        }
        
        // Initialize key and IV
        if (EVP_DecryptInit_ex(ctx, nullptr, nullptr, key.data(), iv.data()) != 1) {
            EVP_CIPHER_CTX_free(ctx);
            throw DecryptionException("Failed to set key and IV");
        }
        
        // Decrypt the ciphertext
        std::vector<uint8_t> plaintext_buffer(encrypted_data.size() + 16);
        int len;
        int plaintext_len;
        
        if (EVP_DecryptUpdate(ctx, plaintext_buffer.data(), &len,
                             encrypted_data.data(),
                             static_cast<int>(encrypted_data.size())) != 1) {
            EVP_CIPHER_CTX_free(ctx);
            throw DecryptionException("Failed to decrypt data");
        }
        plaintext_len = len;
        
        // Set the authentication tag
        if (EVP_CIPHER_CTX_ctrl(ctx, EVP_CTRL_GCM_SET_TAG, tag.size(),
                               const_cast<unsigned char*>(tag.data())) != 1) {
            EVP_CIPHER_CTX_free(ctx);
            throw DecryptionException("Failed to set authentication tag");
        }
        
        // Finalize decryption and verify tag
        if (EVP_DecryptFinal_ex(ctx, plaintext_buffer.data() + len, &len) != 1) {
            EVP_CIPHER_CTX_free(ctx);
            throw DecryptionException("Authentication verification failed");
        }
        plaintext_len += len;
        
        EVP_CIPHER_CTX_free(ctx);
        
        // Clear the derived key
        SecurityUtils::secure_zero_memory(key.data(), key.size());
        
        // Convert to string
        std::string result(reinterpret_cast<char*>(plaintext_buffer.data()), plaintext_len);
        
        // Clear the plaintext buffer
        SecurityUtils::secure_zero_memory(plaintext_buffer.data(), plaintext_buffer.size());
        
        utils::Logger::debug("Successfully decrypted {} bytes", plaintext_len);
        return result;
        
    } catch (const std::exception& e) {
        utils::Logger::error("Decryption failed: {}", e.what());
        return "";
    }
}

bool CryptoManager::store_encrypted_api_key(const std::string& exchange,
                                          const std::string& api_key,
                                          const std::string& secret_key,
                                          const std::string& passphrase) {
    try {
        // Create JSON-like structure for credentials
        std::ostringstream credentials_stream;
        credentials_stream << "{"
                          << "\"api_key\":\"" << api_key << "\","
                          << "\"secret_key\":\"" << secret_key << "\","
                          << "\"passphrase\":\"" << passphrase << "\""
                          << "}";
        
        std::string credentials_json = credentials_stream.str();
        
        // Encrypt the credentials
        auto encryption_result = encrypt_aes256_gcm(credentials_json, "api_credentials_" + exchange);
        if (!encryption_result.success) {
            utils::Logger::error("Failed to encrypt API credentials for {}", exchange);
            return false;
        }
        
        // Save to file
        if (!save_encrypted_credentials_to_file(exchange, encryption_result)) {
            utils::Logger::error("Failed to save encrypted credentials for {}", exchange);
            return false;
        }
        
        // Clear sensitive data
        SecurityUtils::secure_zero_string(credentials_json);
        
        utils::Logger::info("Successfully stored encrypted API credentials for {}", exchange);
        return true;
        
    } catch (const std::exception& e) {
        utils::Logger::error("Failed to store API credentials for {}: {}", exchange, e.what());
        return false;
    }
}

CryptoManager::ApiCredentials CryptoManager::retrieve_api_credentials(const std::string& exchange) {
    ApiCredentials credentials;
    
    try {
        // Load encrypted credentials from file
        EncryptionResult encrypted_data;
        if (!load_encrypted_credentials_from_file(exchange, encrypted_data)) {
            utils::Logger::error("Failed to load encrypted credentials for {}", exchange);
            return credentials;
        }
        
        // Decrypt the credentials
        std::string credentials_json = decrypt_aes256_gcm(
            encrypted_data.encrypted_data,
            encrypted_data.iv,
            encrypted_data.tag,
            "api_credentials_" + exchange
        );
        
        if (credentials_json.empty()) {
            utils::Logger::error("Failed to decrypt credentials for {}", exchange);
            return credentials;
        }
        
        // Parse JSON (simple parsing for this structure)
        // In production, use a proper JSON library
        size_t api_key_start = credentials_json.find("\"api_key\":\"") + 12;
        size_t api_key_end = credentials_json.find("\"", api_key_start);
        credentials.api_key = credentials_json.substr(api_key_start, api_key_end - api_key_start);
        
        size_t secret_key_start = credentials_json.find("\"secret_key\":\"") + 14;
        size_t secret_key_end = credentials_json.find("\"", secret_key_start);
        credentials.secret_key = credentials_json.substr(secret_key_start, secret_key_end - secret_key_start);
        
        size_t passphrase_start = credentials_json.find("\"passphrase\":\"") + 13;
        size_t passphrase_end = credentials_json.find("\"", passphrase_start);
        credentials.passphrase = credentials_json.substr(passphrase_start, passphrase_end - passphrase_start);
        
        credentials.valid = true;
        
        // Clear sensitive data
        SecurityUtils::secure_zero_string(credentials_json);
        
        utils::Logger::debug("Successfully retrieved API credentials for {}", exchange);
        
    } catch (const std::exception& e) {
        utils::Logger::error("Failed to retrieve API credentials for {}: {}", exchange, e.what());
    }
    
    return credentials;
}

std::string CryptoManager::generate_hmac_sha256(const std::string& data, const std::string& key) {
    unsigned char hash[SHA256_DIGEST_LENGTH];
    unsigned int hash_len;
    
    HMAC(EVP_sha256(),
         key.c_str(), static_cast<int>(key.length()),
         reinterpret_cast<const unsigned char*>(data.c_str()), data.length(),
         hash, &hash_len);
    
    return bytes_to_hex(std::vector<uint8_t>(hash, hash + hash_len));
}

std::string CryptoManager::generate_hmac_sha512(const std::string& data, const std::string& key) {
    unsigned char hash[SHA512_DIGEST_LENGTH];
    unsigned int hash_len;
    
    HMAC(EVP_sha512(),
         key.c_str(), static_cast<int>(key.length()),
         reinterpret_cast<const unsigned char*>(data.c_str()), data.length(),
         hash, &hash_len);
    
    return bytes_to_hex(std::vector<uint8_t>(hash, hash + hash_len));
}

bool CryptoManager::verify_hmac_sha256(const std::string& data, const std::string& key, const std::string& signature) {
    std::string computed_signature = generate_hmac_sha256(data, key);
    return SecurityUtils::secure_compare(computed_signature, signature);
}

bool CryptoManager::verify_hmac_sha512(const std::string& data, const std::string& key, const std::string& signature) {
    std::string computed_signature = generate_hmac_sha512(data, key);
    return SecurityUtils::secure_compare(computed_signature, signature);
}

std::vector<uint8_t> CryptoManager::generate_random_key(size_t key_size) {
    std::vector<uint8_t> key(key_size);
    
    if (RAND_bytes(key.data(), static_cast<int>(key_size)) != 1) {
        throw SecurityException("Failed to generate random key");
    }
    
    return key;
}

std::string CryptoManager::generate_random_string(size_t length) {
    const char charset[] = "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789";
    std::string result;
    result.reserve(length);
    
    std::random_device rd;
    std::mt19937 gen(rd());
    std::uniform_int_distribution<> dis(0, sizeof(charset) - 2);
    
    for (size_t i = 0; i < length; ++i) {
        result += charset[dis(gen)];
    }
    
    return result;
}

bool CryptoManager::generate_new_master_key() {
    try {
        master_key_ = generate_random_key(32); // 256-bit master key
        utils::Logger::info("Generated new master key");
        return true;
    } catch (const std::exception& e) {
        utils::Logger::error("Failed to generate master key: {}", e.what());
        return false;
    }
}

std::string CryptoManager::bytes_to_hex(const std::vector<uint8_t>& bytes) {
    std::ostringstream oss;
    oss << std::hex << std::setfill('0');
    for (uint8_t byte : bytes) {
        oss << std::setw(2) << static_cast<unsigned>(byte);
    }
    return oss.str();
}

std::vector<uint8_t> CryptoManager::hex_to_bytes(const std::string& hex) {
    std::vector<uint8_t> bytes;
    for (size_t i = 0; i < hex.length(); i += 2) {
        std::string byte_string = hex.substr(i, 2);
        uint8_t byte = static_cast<uint8_t>(std::stoul(byte_string, nullptr, 16));
        bytes.push_back(byte);
    }
    return bytes;
}

std::string CryptoManager::bytes_to_base64(const std::vector<uint8_t>& bytes) {
    BIO* bio = BIO_new(BIO_s_mem());
    BIO* b64 = BIO_new(BIO_f_base64());
    BIO_set_flags(b64, BIO_FLAGS_BASE64_NO_NL);
    bio = BIO_push(b64, bio);
    
    BIO_write(bio, bytes.data(), static_cast<int>(bytes.size()));
    BIO_flush(bio);
    
    BUF_MEM* buffer_ptr;
    BIO_get_mem_ptr(bio, &buffer_ptr);
    
    std::string result(buffer_ptr->data, buffer_ptr->length);
    BIO_free_all(bio);
    
    return result;
}

std::vector<uint8_t> CryptoManager::base64_to_bytes(const std::string& base64) {
    BIO* bio = BIO_new_mem_buf(base64.c_str(), static_cast<int>(base64.length()));
    BIO* b64 = BIO_new(BIO_f_base64());
    BIO_set_flags(b64, BIO_FLAGS_BASE64_NO_NL);
    bio = BIO_push(b64, bio);
    
    std::vector<uint8_t> result(base64.length());
    int decoded_size = BIO_read(bio, result.data(), static_cast<int>(base64.length()));
    BIO_free_all(bio);
    
    result.resize(decoded_size);
    return result;
}

bool CryptoManager::derive_key_from_master(const std::string& key_id, std::vector<uint8_t>& derived_key) {
    if (master_key_.empty()) {
        utils::Logger::error("Master key not available for key derivation");
        return false;
    }
    
    try {
        EVP_KDF* kdf = EVP_KDF_fetch(nullptr, "HKDF", nullptr);
        if (!kdf) {
            utils::Logger::error("Failed to fetch HKDF");
            return false;
        }
        
        EVP_KDF_CTX* ctx = EVP_KDF_CTX_new(kdf);
        EVP_KDF_free(kdf);
        
        if (!ctx) {
            utils::Logger::error("Failed to create KDF context");
            return false;
        }
        
        // Set up HKDF parameters
        OSSL_PARAM params[] = {
            OSSL_PARAM_construct_utf8_string("digest", const_cast<char*>("SHA256"), 0),
            OSSL_PARAM_construct_octet_string("key", const_cast<uint8_t*>(master_key_.data()), master_key_.size()),
            OSSL_PARAM_construct_octet_string("info", const_cast<char*>(key_id.c_str()), key_id.length()),
            OSSL_PARAM_construct_end()
        };
        
        if (EVP_KDF_derive(ctx, derived_key.data(), derived_key.size(), params) != 1) {
            EVP_KDF_CTX_free(ctx);
            utils::Logger::error("Key derivation failed");
            return false;
        }
        
        EVP_KDF_CTX_free(ctx);
        return true;
        
    } catch (const std::exception& e) {
        utils::Logger::error("Key derivation error: {}", e.what());
        return false;
    }
}

void CryptoManager::cleanup_openssl() {
    EVP_cleanup();
    CRYPTO_cleanup_all_ex_data();
    ERR_free_strings();
}

std::string CryptoManager::get_openssl_error() {
    BIO* bio = BIO_new(BIO_s_mem());
    ERR_print_errors(bio);
    
    BUF_MEM* buffer_ptr;
    BIO_get_mem_ptr(bio, &buffer_ptr);
    
    std::string error_string(buffer_ptr->data, buffer_ptr->length);
    BIO_free(bio);
    
    return error_string;
}

void CryptoManager::log_security_event(const std::string& event, const std::string& details) {
    utils::Logger::info("Security Event [{}]: {}", event, details);
}

bool CryptoManager::save_encrypted_credentials_to_file(const std::string& exchange, const EncryptionResult& credentials) {
    try {
        std::string filename = key_storage_path_ + exchange + ".cred";
        std::ofstream file(filename, std::ios::binary);
        
        if (!file.is_open()) {
            utils::Logger::error("Failed to open credentials file for writing: {}", filename);
            return false;
        }
        
        // Write IV size and IV
        uint32_t iv_size = static_cast<uint32_t>(credentials.iv.size());
        file.write(reinterpret_cast<const char*>(&iv_size), sizeof(iv_size));
        file.write(reinterpret_cast<const char*>(credentials.iv.data()), credentials.iv.size());
        
        // Write tag size and tag
        uint32_t tag_size = static_cast<uint32_t>(credentials.tag.size());
        file.write(reinterpret_cast<const char*>(&tag_size), sizeof(tag_size));
        file.write(reinterpret_cast<const char*>(credentials.tag.data()), credentials.tag.size());
        
        // Write encrypted data size and data
        uint32_t data_size = static_cast<uint32_t>(credentials.encrypted_data.size());
        file.write(reinterpret_cast<const char*>(&data_size), sizeof(data_size));
        file.write(reinterpret_cast<const char*>(credentials.encrypted_data.data()), credentials.encrypted_data.size());
        
        file.close();
        
        // Set secure file permissions (read-only for owner)
        std::filesystem::permissions(filename, std::filesystem::perms::owner_read);
        
        return true;
        
    } catch (const std::exception& e) {
        utils::Logger::error("Failed to save encrypted credentials: {}", e.what());
        return false;
    }
}

bool CryptoManager::load_encrypted_credentials_from_file(const std::string& exchange, EncryptionResult& credentials) {
    try {
        std::string filename = key_storage_path_ + exchange + ".cred";
        std::ifstream file(filename, std::ios::binary);
        
        if (!file.is_open()) {
            utils::Logger::error("Failed to open credentials file for reading: {}", filename);
            return false;
        }
        
        // Read IV
        uint32_t iv_size;
        file.read(reinterpret_cast<char*>(&iv_size), sizeof(iv_size));
        credentials.iv.resize(iv_size);
        file.read(reinterpret_cast<char*>(credentials.iv.data()), iv_size);
        
        // Read tag
        uint32_t tag_size;
        file.read(reinterpret_cast<char*>(&tag_size), sizeof(tag_size));
        credentials.tag.resize(tag_size);
        file.read(reinterpret_cast<char*>(credentials.tag.data()), tag_size);
        
        // Read encrypted data
        uint32_t data_size;
        file.read(reinterpret_cast<char*>(&data_size), sizeof(data_size));
        credentials.encrypted_data.resize(data_size);
        file.read(reinterpret_cast<char*>(credentials.encrypted_data.data()), data_size);
        
        file.close();
        
        credentials.success = true;
        return true;
        
    } catch (const std::exception& e) {
        utils::Logger::error("Failed to load encrypted credentials: {}", e.what());
        return false;
    }
}

bool CryptoManager::load_master_key_from_file(const std::string& file_path) {
    try {
        std::ifstream file(file_path, std::ios::binary);
        if (!file.is_open()) {
            utils::Logger::error("Failed to open master key file: {}", file_path);
            return false;
        }
        
        // Read key size
        uint32_t key_size;
        file.read(reinterpret_cast<char*>(&key_size), sizeof(key_size));
        
        if (key_size != 32) {
            utils::Logger::error("Invalid master key size: {}", key_size);
            return false;
        }
        
        // Read master key
        master_key_.resize(key_size);
        file.read(reinterpret_cast<char*>(master_key_.data()), key_size);
        file.close();
        
        return true;
        
    } catch (const std::exception& e) {
        utils::Logger::error("Failed to load master key: {}", e.what());
        return false;
    }
}

bool CryptoManager::save_master_key_to_file(const std::string& file_path) {
    try {
        std::ofstream file(file_path, std::ios::binary);
        if (!file.is_open()) {
            utils::Logger::error("Failed to create master key file: {}", file_path);
            return false;
        }
        
        // Write key size
        uint32_t key_size = static_cast<uint32_t>(master_key_.size());
        file.write(reinterpret_cast<const char*>(&key_size), sizeof(key_size));
        
        // Write master key
        file.write(reinterpret_cast<const char*>(master_key_.data()), master_key_.size());
        file.close();
        
        // Set secure file permissions
        std::filesystem::permissions(file_path, std::filesystem::perms::owner_read);
        
        return true;
        
    } catch (const std::exception& e) {
        utils::Logger::error("Failed to save master key: {}", e.what());
        return false;
    }
}

bool CryptoManager::delete_api_credentials(const std::string& exchange) {
    try {
        std::string filename = key_storage_path_ + exchange + ".cred";
        if (std::filesystem::exists(filename)) {
            std::filesystem::remove(filename);
            utils::Logger::info("Deleted API credentials for {}", exchange);
        }
        return true;
    } catch (const std::exception& e) {
        utils::Logger::error("Failed to delete API credentials for {}: {}", exchange, e.what());
        return false;
    }
}

std::vector<std::string> CryptoManager::list_stored_exchanges() {
    std::vector<std::string> exchanges;
    
    try {
        for (const auto& entry : std::filesystem::directory_iterator(key_storage_path_)) {
            if (entry.is_regular_file() && entry.path().extension() == ".cred") {
                std::string exchange = entry.path().stem().string();
                exchanges.push_back(exchange);
            }
        }
    } catch (const std::exception& e) {
        utils::Logger::error("Failed to list stored exchanges: {}", e.what());
    }
    
    return exchanges;
}

// SecurityUtils Implementation
bool SecurityUtils::secure_compare(const std::string& a, const std::string& b) {
    if (a.length() != b.length()) {
        return false;
    }
    
    volatile int result = 0;
    for (size_t i = 0; i < a.length(); ++i) {
        result |= a[i] ^ b[i];
    }
    
    return result == 0;
}

bool SecurityUtils::secure_compare(const std::vector<uint8_t>& a, const std::vector<uint8_t>& b) {
    if (a.size() != b.size()) {
        return false;
    }
    
    volatile int result = 0;
    for (size_t i = 0; i < a.size(); ++i) {
        result |= a[i] ^ b[i];
    }
    
    return result == 0;
}

void SecurityUtils::secure_zero_memory(void* ptr, size_t size) {
    if (ptr && size > 0) {
        volatile char* volatile_ptr = static_cast<volatile char*>(ptr);
        for (size_t i = 0; i < size; ++i) {
            volatile_ptr[i] = 0;
        }
    }
}

void SecurityUtils::secure_zero_string(std::string& str) {
    secure_zero_memory(&str[0], str.size());
    str.clear();
}

bool SecurityUtils::is_valid_base64(const std::string& input) {
    const std::string valid_chars = "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/=";
    
    for (char c : input) {
        if (valid_chars.find(c) == std::string::npos) {
            return false;
        }
    }
    
    // Check padding
    size_t padding_count = 0;
    for (auto it = input.rbegin(); it != input.rend() && *it == '='; ++it) {
        padding_count++;
    }
    
    return padding_count <= 2;
}

bool SecurityUtils::is_valid_hex(const std::string& input) {
    for (char c : input) {
        if (!std::isxdigit(c)) {
            return false;
        }
    }
    return true;
}

bool SecurityUtils::is_printable_ascii(const std::string& input) {
    for (char c : input) {
        if (c < 32 || c > 126) {
            return false;
        }
    }
    return true;
}

std::string SecurityUtils::generate_csrf_token() {
    std::vector<uint8_t> random_bytes(32);
    if (RAND_bytes(random_bytes.data(), 32) != 1) {
        throw SecurityException("Failed to generate CSRF token");
    }
    
    CryptoManager crypto;
    return crypto.bytes_to_base64(random_bytes);
}

// SecureRandom Implementation
bool SecureRandom::initialized_ = false;

bool SecureRandom::initialize() {
    if (!initialized_) {
        if (RAND_poll() == 1) {
            initialized_ = true;
            return true;
        }
        return false;
    }
    return true;
}

std::vector<uint8_t> SecureRandom::generate_bytes(size_t count) {
    if (!initialize()) {
        throw SecurityException("Failed to initialize secure random");
    }
    
    std::vector<uint8_t> bytes(count);
    if (RAND_bytes(bytes.data(), static_cast<int>(count)) != 1) {
        throw SecurityException("Failed to generate secure random bytes");
    }
    
    return bytes;
}

uint32_t SecureRandom::generate_uint32() {
    auto bytes = generate_bytes(4);
    return (static_cast<uint32_t>(bytes[0]) << 24) |
           (static_cast<uint32_t>(bytes[1]) << 16) |
           (static_cast<uint32_t>(bytes[2]) << 8) |
           static_cast<uint32_t>(bytes[3]);
}

uint64_t SecureRandom::generate_uint64() {
    auto bytes = generate_bytes(8);
    uint64_t result = 0;
    for (int i = 0; i < 8; ++i) {
        result = (result << 8) | bytes[i];
    }
    return result;
}

std::string SecureRandom::generate_hex_string(size_t length) {
    auto bytes = generate_bytes((length + 1) / 2);
    CryptoManager crypto;
    std::string hex = crypto.bytes_to_hex(bytes);
    return hex.substr(0, length);
}

std::string SecureRandom::generate_alphanumeric_string(size_t length) {
    const char charset[] = "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789";
    std::string result;
    result.reserve(length);
    
    for (size_t i = 0; i < length; ++i) {
        uint32_t random_index = generate_uint32() % (sizeof(charset) - 1);
        result += charset[random_index];
    }
    
    return result;
}

} // namespace security
} // namespace ats