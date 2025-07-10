#include "utils/crypto_utils.hpp"
#include "utils/logger.hpp"
#include <openssl/evp.h>
#include <openssl/rand.h>
#include <openssl/hmac.h>
#include <openssl/sha.h>
#include <openssl/bio.h>
#include <openssl/buffer.h>
#include <openssl/err.h>
#include <algorithm>
#include <sstream>
#include <iomanip>
#include <fstream>
#include <cstring>

namespace ats {
namespace utils {

// Static initialization
void CryptoUtils::ensure_openssl_initialized() {
    static bool initialized = false;
    if (!initialized) {
        OpenSSL_add_all_algorithms();
        ERR_load_crypto_strings();
        initialized = true;
    }
}

std::string CryptoUtils::get_openssl_error() {
    BIO* bio = BIO_new(BIO_s_mem());
    ERR_print_errors(bio);
    char* buf;
    size_t len = BIO_get_mem_data(bio, &buf);
    std::string error(buf, len);
    BIO_free(bio);
    return error;
}

std::vector<uint8_t> CryptoUtils::generate_random_bytes(size_t length) {
    ensure_openssl_initialized();
    
    std::vector<uint8_t> bytes(length);
    if (RAND_bytes(bytes.data(), static_cast<int>(length)) != 1) {
        Logger::error("Failed to generate random bytes: {}", get_openssl_error());
        return {};
    }
    return bytes;
}

std::vector<uint8_t> CryptoUtils::generate_aes_key() {
    return generate_random_bytes(32); // 256 bits
}

std::vector<uint8_t> CryptoUtils::generate_iv(size_t length) {
    return generate_random_bytes(length);
}

CryptoUtils::EncryptionResult CryptoUtils::encrypt_aes_gcm(
    const std::vector<uint8_t>& plaintext,
    const std::vector<uint8_t>& key,
    const std::vector<uint8_t>& iv,
    const std::vector<uint8_t>& aad) {
    
    ensure_openssl_initialized();
    
    EncryptionResult result;
    
    if (key.size() != 32) {
        Logger::error("AES-256 key must be 32 bytes");
        return result;
    }
    
    // Generate IV if not provided
    std::vector<uint8_t> actual_iv = iv.empty() ? generate_iv(12) : iv;
    if (actual_iv.size() != 12) {
        Logger::error("GCM IV must be 12 bytes");
        return result;
    }
    
    EVP_CIPHER_CTX* ctx = EVP_CIPHER_CTX_new();
    if (!ctx) {
        Logger::error("Failed to create cipher context");
        return result;
    }
    
    do {
        // Initialize encryption
        if (EVP_EncryptInit_ex(ctx, EVP_aes_256_gcm(), nullptr, nullptr, nullptr) != 1) {
            Logger::error("Failed to initialize AES-256-GCM encryption");
            break;
        }
        
        // Set IV length
        if (EVP_CIPHER_CTX_ctrl(ctx, EVP_CTRL_GCM_SET_IVLEN, actual_iv.size(), nullptr) != 1) {
            Logger::error("Failed to set IV length");
            break;
        }
        
        // Initialize key and IV
        if (EVP_EncryptInit_ex(ctx, nullptr, nullptr, key.data(), actual_iv.data()) != 1) {
            Logger::error("Failed to set key and IV");
            break;
        }
        
        // Provide AAD if present
        int len;
        if (!aad.empty()) {
            if (EVP_EncryptUpdate(ctx, nullptr, &len, aad.data(), aad.size()) != 1) {
                Logger::error("Failed to set AAD");
                break;
            }
        }
        
        // Encrypt plaintext
        result.encrypted_data.resize(plaintext.size());
        if (EVP_EncryptUpdate(ctx, result.encrypted_data.data(), &len, 
                             plaintext.data(), plaintext.size()) != 1) {
            Logger::error("Failed to encrypt data");
            break;
        }
        result.encrypted_data.resize(len);
        
        // Finalize encryption
        std::vector<uint8_t> final_block(16);
        if (EVP_EncryptFinal_ex(ctx, final_block.data(), &len) != 1) {
            Logger::error("Failed to finalize encryption");
            break;
        }
        result.encrypted_data.insert(result.encrypted_data.end(), 
                                   final_block.begin(), final_block.begin() + len);
        
        // Get authentication tag
        result.tag.resize(16);
        if (EVP_CIPHER_CTX_ctrl(ctx, EVP_CTRL_GCM_GET_TAG, 16, result.tag.data()) != 1) {
            Logger::error("Failed to get authentication tag");
            break;
        }
        
        result.iv = actual_iv;
        result.success = true;
        
    } while (false);
    
    EVP_CIPHER_CTX_free(ctx);
    return result;
}

CryptoUtils::DecryptionResult CryptoUtils::decrypt_aes_gcm(
    const std::vector<uint8_t>& ciphertext,
    const std::vector<uint8_t>& key,
    const std::vector<uint8_t>& iv,
    const std::vector<uint8_t>& tag,
    const std::vector<uint8_t>& aad) {
    
    ensure_openssl_initialized();
    
    DecryptionResult result;
    
    if (key.size() != 32 || iv.size() != 12 || tag.size() != 16) {
        Logger::error("Invalid key, IV, or tag size for AES-256-GCM");
        return result;
    }
    
    EVP_CIPHER_CTX* ctx = EVP_CIPHER_CTX_new();
    if (!ctx) {
        Logger::error("Failed to create cipher context");
        return result;
    }
    
    do {
        // Initialize decryption
        if (EVP_DecryptInit_ex(ctx, EVP_aes_256_gcm(), nullptr, nullptr, nullptr) != 1) {
            Logger::error("Failed to initialize AES-256-GCM decryption");
            break;
        }
        
        // Set IV length
        if (EVP_CIPHER_CTX_ctrl(ctx, EVP_CTRL_GCM_SET_IVLEN, iv.size(), nullptr) != 1) {
            Logger::error("Failed to set IV length");
            break;
        }
        
        // Initialize key and IV
        if (EVP_DecryptInit_ex(ctx, nullptr, nullptr, key.data(), iv.data()) != 1) {
            Logger::error("Failed to set key and IV");
            break;
        }
        
        // Provide AAD if present
        int len;
        if (!aad.empty()) {
            if (EVP_DecryptUpdate(ctx, nullptr, &len, aad.data(), aad.size()) != 1) {
                Logger::error("Failed to set AAD");
                break;
            }
        }
        
        // Decrypt ciphertext
        result.decrypted_data.resize(ciphertext.size());
        if (EVP_DecryptUpdate(ctx, result.decrypted_data.data(), &len,
                             ciphertext.data(), ciphertext.size()) != 1) {
            Logger::error("Failed to decrypt data");
            break;
        }
        result.decrypted_data.resize(len);
        
        // Set authentication tag
        if (EVP_CIPHER_CTX_ctrl(ctx, EVP_CTRL_GCM_SET_TAG, tag.size(), 
                               const_cast<uint8_t*>(tag.data())) != 1) {
            Logger::error("Failed to set authentication tag");
            break;
        }
        
        // Finalize decryption (this verifies the tag)
        std::vector<uint8_t> final_block(16);
        if (EVP_DecryptFinal_ex(ctx, final_block.data(), &len) != 1) {
            Logger::error("Authentication failed or decryption error");
            break;
        }
        result.decrypted_data.insert(result.decrypted_data.end(),
                                   final_block.begin(), final_block.begin() + len);
        
        result.success = true;
        
    } while (false);
    
    EVP_CIPHER_CTX_free(ctx);
    return result;
}

std::vector<uint8_t> CryptoUtils::hmac_sha256(const std::vector<uint8_t>& data,
                                             const std::vector<uint8_t>& key) {
    ensure_openssl_initialized();
    
    std::vector<uint8_t> result(SHA256_DIGEST_LENGTH);
    unsigned int len;
    
    if (!HMAC(EVP_sha256(), key.data(), key.size(), data.data(), data.size(),
              result.data(), &len)) {
        Logger::error("HMAC-SHA256 failed: {}", get_openssl_error());
        return {};
    }
    
    result.resize(len);
    return result;
}

std::string CryptoUtils::hmac_sha256_hex(const std::string& data, const std::string& key) {
    auto data_bytes = string_to_bytes(data);
    auto key_bytes = string_to_bytes(key);
    auto hmac_bytes = hmac_sha256(data_bytes, key_bytes);
    return hex_encode(hmac_bytes);
}

std::string CryptoUtils::hmac_sha256_base64(const std::string& data, const std::string& key) {
    auto data_bytes = string_to_bytes(data);
    auto key_bytes = string_to_bytes(key);
    auto hmac_bytes = hmac_sha256(data_bytes, key_bytes);
    return base64_encode(hmac_bytes);
}

std::string CryptoUtils::base64_encode(const std::vector<uint8_t>& data) {
    BIO* bio = BIO_new(BIO_s_mem());
    BIO* b64 = BIO_new(BIO_f_base64());
    BIO_set_flags(b64, BIO_FLAGS_BASE64_NO_NL);
    bio = BIO_push(b64, bio);
    
    BIO_write(bio, data.data(), data.size());
    BIO_flush(bio);
    
    BUF_MEM* buffer_ptr;
    BIO_get_mem_ptr(bio, &buffer_ptr);
    
    std::string result(buffer_ptr->data, buffer_ptr->length);
    BIO_free_all(bio);
    
    return result;
}

std::vector<uint8_t> CryptoUtils::base64_decode(const std::string& encoded) {
    BIO* bio = BIO_new_mem_buf(encoded.data(), encoded.size());
    BIO* b64 = BIO_new(BIO_f_base64());
    BIO_set_flags(b64, BIO_FLAGS_BASE64_NO_NL);
    bio = BIO_push(b64, bio);
    
    std::vector<uint8_t> result(encoded.size());
    int len = BIO_read(bio, result.data(), result.size());
    BIO_free_all(bio);
    
    if (len < 0) {
        Logger::error("Base64 decode failed");
        return {};
    }
    
    result.resize(len);
    return result;
}

std::string CryptoUtils::hex_encode(const std::vector<uint8_t>& data) {
    std::ostringstream oss;
    oss << std::hex << std::setfill('0');
    for (uint8_t byte : data) {
        oss << std::setw(2) << static_cast<int>(byte);
    }
    return oss.str();
}

std::vector<uint8_t> CryptoUtils::hex_decode(const std::string& hex) {
    std::vector<uint8_t> result;
    if (hex.length() % 2 != 0) {
        Logger::error("Invalid hex string length");
        return result;
    }
    
    result.reserve(hex.length() / 2);
    for (size_t i = 0; i < hex.length(); i += 2) {
        std::string byte_str = hex.substr(i, 2);
        uint8_t byte = static_cast<uint8_t>(std::stoi(byte_str, nullptr, 16));
        result.push_back(byte);
    }
    
    return result;
}

std::string CryptoUtils::generate_binance_signature(const std::string& query_string,
                                                   const std::string& secret_key) {
    return hmac_sha256_hex(query_string, secret_key);
}

std::string CryptoUtils::generate_upbit_signature(const std::string& access_key,
                                                 const std::string& secret_key,
                                                 const std::string& query_string) {
    std::string payload = access_key + query_string;
    return hmac_sha256_hex(payload, secret_key);
}

std::vector<uint8_t> CryptoUtils::string_to_bytes(const std::string& str) {
    return std::vector<uint8_t>(str.begin(), str.end());
}

std::string CryptoUtils::bytes_to_string(const std::vector<uint8_t>& bytes) {
    return std::string(bytes.begin(), bytes.end());
}

bool CryptoUtils::secure_compare(const std::string& a, const std::string& b) {
    if (a.length() != b.length()) {
        return false;
    }
    
    volatile int result = 0;
    for (size_t i = 0; i < a.length(); ++i) {
        result |= a[i] ^ b[i];
    }
    
    return result == 0;
}

void CryptoUtils::secure_zero_memory(void* ptr, size_t size) {
    volatile char* p = static_cast<volatile char*>(ptr);
    for (size_t i = 0; i < size; ++i) {
        p[i] = 0;
    }
}

// SecureString implementation
SecureString::SecureString(const std::string& str) : data_(nullptr), size_(0), capacity_(0) {
    secure_allocate(str.size());
    if (data_) {
        std::memcpy(data_, str.c_str(), str.size());
        size_ = str.size();
    }
}

SecureString::SecureString(size_t size) : data_(nullptr), size_(0), capacity_(0) {
    secure_allocate(size);
    if (data_) {
        size_ = size;
        std::memset(data_, 0, size);
    }
}

SecureString::~SecureString() {
    secure_deallocate();
}

SecureString::SecureString(SecureString&& other) noexcept
    : data_(other.data_), size_(other.size_), capacity_(other.capacity_) {
    other.data_ = nullptr;
    other.size_ = 0;
    other.capacity_ = 0;
}

SecureString& SecureString::operator=(SecureString&& other) noexcept {
    if (this != &other) {
        secure_deallocate();
        data_ = other.data_;
        size_ = other.size_;
        capacity_ = other.capacity_;
        other.data_ = nullptr;
        other.size_ = 0;
        other.capacity_ = 0;
    }
    return *this;
}

void SecureString::secure_allocate(size_t size) {
    capacity_ = size + 1; // +1 for null terminator
    data_ = static_cast<char*>(std::malloc(capacity_));
    if (data_) {
        data_[size] = '\0';
    }
}

void SecureString::secure_deallocate() {
    if (data_) {
        CryptoUtils::secure_zero_memory(data_, capacity_);
        std::free(data_);
        data_ = nullptr;
    }
    size_ = 0;
    capacity_ = 0;
}

const char* SecureString::c_str() const {
    return data_ ? data_ : "";
}

size_t SecureString::size() const {
    return size_;
}

bool SecureString::empty() const {
    return size_ == 0;
}

void SecureString::clear() {
    if (data_) {
        CryptoUtils::secure_zero_memory(data_, capacity_);
        size_ = 0;
    }
}

} // namespace utils
} // namespace ats