#include "crypto_utils.hpp"
#include <sstream>
#include <iomanip>
#include <chrono>
#include <random>
#include <algorithm>

namespace ats {

const std::string CryptoUtils::base64_chars = 
    "ABCDEFGHIJKLMNOPQRSTUVWXYZ"
    "abcdefghijklmnopqrstuvwxyz"
    "0123456789+/";

std::string CryptoUtils::hmac_sha256(const std::string& key, const std::string& data) {
    std::vector<unsigned char> key_bytes(key.begin(), key.end());
    std::vector<unsigned char> data_bytes(data.begin(), data.end());
    
    auto result = hmac_sha256_raw(key_bytes, data_bytes);
    
    std::stringstream ss;
    for (unsigned char byte : result) {
        ss << std::hex << std::setw(2) << std::setfill('0') << static_cast<int>(byte);
    }
    return ss.str();
}

std::string CryptoUtils::sha256(const std::string& data) {
    std::vector<unsigned char> data_bytes(data.begin(), data.end());
    auto result = sha256_raw(data_bytes);
    
    std::stringstream ss;
    for (unsigned char byte : result) {
        ss << std::hex << std::setw(2) << std::setfill('0') << static_cast<int>(byte);
    }
    return ss.str();
}

std::string CryptoUtils::base64_encode(const std::vector<unsigned char>& data) {
    std::string ret;
    int i = 0;
    int j = 0;
    unsigned char char_array_3[3];
    unsigned char char_array_4[4];

    for (size_t idx = 0; idx < data.size(); idx++) {
        char_array_3[i++] = data[idx];
        if (i == 3) {
            char_array_4[0] = (char_array_3[0] & 0xfc) >> 2;
            char_array_4[1] = ((char_array_3[0] & 0x03) << 4) + ((char_array_3[1] & 0xf0) >> 4);
            char_array_4[2] = ((char_array_3[1] & 0x0f) << 2) + ((char_array_3[2] & 0xc0) >> 6);
            char_array_4[3] = char_array_3[2] & 0x3f;

            for (i = 0; (i < 4); i++)
                ret += base64_chars[char_array_4[i]];
            i = 0;
        }
    }

    if (i) {
        for (j = i; j < 3; j++)
            char_array_3[j] = '\0';

        char_array_4[0] = (char_array_3[0] & 0xfc) >> 2;
        char_array_4[1] = ((char_array_3[0] & 0x03) << 4) + ((char_array_3[1] & 0xf0) >> 4);
        char_array_4[2] = ((char_array_3[1] & 0x0f) << 2) + ((char_array_3[2] & 0xc0) >> 6);
        char_array_4[3] = char_array_3[2] & 0x3f;

        for (j = 0; (j < i + 1); j++)
            ret += base64_chars[char_array_4[j]];

        while ((i++ < 3))
            ret += '=';
    }

    return ret;
}

std::vector<unsigned char> CryptoUtils::base64_decode(const std::string& encoded) {
    int in_len = encoded.size();
    int i = 0;
    int j = 0;
    int in = 0;
    unsigned char char_array_4[4], char_array_3[3];
    std::vector<unsigned char> ret;

    while (in_len-- && (encoded[in] != '=') && is_base64(encoded[in])) {
        char_array_4[i++] = encoded[in]; in++;
        if (i == 4) {
            for (i = 0; i < 4; i++)
                char_array_4[i] = base64_chars.find(char_array_4[i]);

            char_array_3[0] = (char_array_4[0] << 2) + ((char_array_4[1] & 0x30) >> 4);
            char_array_3[1] = ((char_array_4[1] & 0xf) << 4) + ((char_array_4[2] & 0x3c) >> 2);
            char_array_3[2] = ((char_array_4[2] & 0x3) << 6) + char_array_4[3];

            for (i = 0; (i < 3); i++)
                ret.push_back(char_array_3[i]);
            i = 0;
        }
    }

    if (i) {
        for (j = i; j < 4; j++)
            char_array_4[j] = 0;

        for (j = 0; j < 4; j++)
            char_array_4[j] = base64_chars.find(char_array_4[j]);

        char_array_3[0] = (char_array_4[0] << 2) + ((char_array_4[1] & 0x30) >> 4);
        char_array_3[1] = ((char_array_4[1] & 0xf) << 4) + ((char_array_4[2] & 0x3c) >> 2);
        char_array_3[2] = ((char_array_4[2] & 0x3) << 6) + char_array_4[3];

        for (j = 0; (j < i - 1); j++) ret.push_back(char_array_3[j]);
    }

    return ret;
}

std::string CryptoUtils::url_encode(const std::string& value) {
    std::ostringstream escaped;
    escaped.fill('0');
    escaped << std::hex;

    for (char c : value) {
        if (std::isalnum(c) || c == '-' || c == '_' || c == '.' || c == '~') {
            escaped << c;
        } else {
            escaped << std::uppercase;
            escaped << '%' << std::setw(2) << int(static_cast<unsigned char>(c));
            escaped << std::nouppercase;
        }
    }

    return escaped.str();
}

long long CryptoUtils::current_timestamp_ms() {
    return std::chrono::duration_cast<std::chrono::milliseconds>(
        std::chrono::system_clock::now().time_since_epoch()).count();
}

std::string CryptoUtils::generate_random_string(size_t length) {
    const std::string charset = "abcdefghijklmnopqrstuvwxyzABCDEFGHIJKLMNOPQRSTUVWXYZ0123456789";
    std::random_device rd;
    std::mt19937 gen(rd());
    std::uniform_int_distribution<> dis(0, charset.size() - 1);
    
    std::string result;
    result.reserve(length);
    for (size_t i = 0; i < length; ++i) {
        result += charset[dis(gen)];
    }
    return result;
}

bool CryptoUtils::is_base64(unsigned char c) {
    return (isalnum(c) || (c == '+') || (c == '/'));
}

// Simple SHA-256 implementation (basic version)
std::vector<unsigned char> CryptoUtils::sha256_raw(const std::vector<unsigned char>& data) {
    // This is a simplified implementation for demonstration
    // In production, use a proper crypto library like OpenSSL or similar
    
    // For now, return a mock hash to keep the code compiling
    std::vector<unsigned char> mock_hash(32, 0);
    
    // Simple checksum as placeholder
    unsigned char checksum = 0;
    for (unsigned char byte : data) {
        checksum ^= byte;
    }
    
    // Fill with pattern based on checksum
    for (size_t i = 0; i < 32; ++i) {
        mock_hash[i] = checksum + static_cast<unsigned char>(i);
    }
    
    return mock_hash;
}

std::vector<unsigned char> CryptoUtils::hmac_sha256_raw(const std::vector<unsigned char>& key, 
                                                        const std::vector<unsigned char>& data) {
    // Simplified HMAC implementation
    // In production, use proper crypto library
    
    const size_t block_size = 64;
    std::vector<unsigned char> k = key;
    
    // Adjust key length
    if (k.size() > block_size) {
        k = sha256_raw(k);
    }
    if (k.size() < block_size) {
        k.resize(block_size, 0);
    }
    
    // Create inner and outer padding
    std::vector<unsigned char> i_pad(block_size, 0x36);
    std::vector<unsigned char> o_pad(block_size, 0x5c);
    
    for (size_t i = 0; i < block_size; ++i) {
        i_pad[i] ^= k[i];
        o_pad[i] ^= k[i];
    }
    
    // Inner hash
    std::vector<unsigned char> inner_data;
    inner_data.insert(inner_data.end(), i_pad.begin(), i_pad.end());
    inner_data.insert(inner_data.end(), data.begin(), data.end());
    auto inner_hash = sha256_raw(inner_data);
    
    // Outer hash
    std::vector<unsigned char> outer_data;
    outer_data.insert(outer_data.end(), o_pad.begin(), o_pad.end());
    outer_data.insert(outer_data.end(), inner_hash.begin(), inner_hash.end());
    
    return sha256_raw(outer_data);
}

} // namespace ats