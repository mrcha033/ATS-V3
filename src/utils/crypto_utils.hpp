#pragma once

#include <string>
#include <vector>

namespace ats {

class CryptoUtils {
public:
    // HMAC-SHA256 for API signature
    static std::string hmac_sha256(const std::string& key, const std::string& data);
    
    // SHA256 hash
    static std::string sha256(const std::string& data);
    
    // Base64 encoding/decoding
    static std::string base64_encode(const std::vector<unsigned char>& data);
    static std::vector<unsigned char> base64_decode(const std::string& encoded);
    
    // URL encoding
    static std::string url_encode(const std::string& value);
    
    // Generate timestamp
    static long long current_timestamp_ms();
    
    // Generate random string (for client order IDs)
    static std::string generate_random_string(size_t length);

private:
    static const std::string base64_chars;
    static bool is_base64(unsigned char c);
    
    // Simple HMAC-SHA256 implementation without external dependencies
    static std::vector<unsigned char> sha256_raw(const std::vector<unsigned char>& data);
    static std::vector<unsigned char> hmac_sha256_raw(const std::vector<unsigned char>& key, 
                                                      const std::vector<unsigned char>& data);
};

} // namespace ats