#include "auth_manager.hpp"
#include "utils/logger.hpp"
#include <sstream>
#include <iomanip>
#include <algorithm>
#include <regex>
#include <cstring>
#include <openssl/sha.h>
#include <openssl/bio.h>
#include <openssl/buffer.h>

namespace ats {
namespace security {

// AuthManager Implementation
AuthManager::AuthManager() {
    utils::Logger::info("AuthManager initialized");
}

AuthManager::~AuthManager() {
    // Clear sensitive data
    for (auto& token : api_tokens_) {
        // Simple secure clearing - zero out the string
        std::fill(token.second.secret.begin(), token.second.secret.end(), '\0');
    }
    api_tokens_.clear();
    
    sessions_.clear();
    utils::Logger::info("AuthManager destroyed");
}

bool AuthManager::initialize(std::shared_ptr<CryptoManager> crypto_manager) {
    crypto_manager_ = crypto_manager;
    
    // Configure default exchange authentication methods
    ExchangeAuthConfig binance_config;
    binance_config.exchange_name = "binance";
    binance_config.signature_method = "HMAC-SHA256";
    binance_config.timestamp_format = "unix";
    binance_config.required_headers = {"X-MBX-APIKEY"};
    binance_config.signature_header_name = "signature";
    binance_config.include_body_in_signature = false;
    binance_config.timestamp_tolerance_seconds = 5000; // 5 seconds
    configure_exchange_auth(binance_config);
    
    ExchangeAuthConfig upbit_config;
    upbit_config.exchange_name = "upbit";
    upbit_config.signature_method = "HMAC-SHA512";
    upbit_config.timestamp_format = "unix";
    upbit_config.required_headers = {"Authorization"};
    upbit_config.signature_header_name = "Authorization";
    upbit_config.include_body_in_signature = true;
    upbit_config.timestamp_tolerance_seconds = 30;
    configure_exchange_auth(upbit_config);
    
    ExchangeAuthConfig coinbase_config;
    coinbase_config.exchange_name = "coinbase";
    coinbase_config.signature_method = "HMAC-SHA256";
    coinbase_config.timestamp_format = "unix";
    coinbase_config.required_headers = {"CB-ACCESS-KEY", "CB-ACCESS-SIGN", "CB-ACCESS-TIMESTAMP"};
    coinbase_config.signature_header_name = "CB-ACCESS-SIGN";
    coinbase_config.include_body_in_signature = true;
    coinbase_config.timestamp_tolerance_seconds = 30;
    configure_exchange_auth(coinbase_config);
    
    utils::Logger::info("AuthManager initialized with {} exchange configurations", exchange_configs_.size());
    return true;
}

bool AuthManager::configure_exchange_auth(const ExchangeAuthConfig& config) {
    exchange_configs_[config.exchange_name] = config;
    utils::Logger::debug("Configured authentication for exchange: {}", config.exchange_name);
    return true;
}

AuthManager::SignedRequest AuthManager::sign_exchange_request(
    const std::string& exchange,
    const std::string& method,
    const std::string& endpoint,
    const std::string& query_params,
    const std::string& body) {
    
    SignedRequest request;
    request.method = method;
    request.url = endpoint;
    request.query_string = query_params;
    request.body = body;
    
    try {
        auto config_it = exchange_configs_.find(exchange);
        if (config_it == exchange_configs_.end()) {
            utils::Logger::error("No authentication configuration found for exchange: {}", exchange);
            return request;
        }
        
        const auto& config = config_it->second;
        
        // Get API credentials
        auto credentials = crypto_manager_->retrieve_api_credentials(exchange);
        if (!credentials.valid) {
            utils::Logger::error("Failed to retrieve API credentials for exchange: {}", exchange);
            return request;
        }
        
        // Generate timestamp and nonce
        std::string timestamp = get_current_timestamp(config.timestamp_format);
        std::string nonce = generate_nonce();
        
        // Build signature base string
        std::string signature_data = build_signature_base_string(
            method, endpoint, query_params, body, timestamp, nonce);
        
        // Generate signature based on exchange method
        if (exchange == "binance") {
            request.signature = ExchangeApiSigner::sign_binance_request(
                query_params + "&timestamp=" + timestamp, credentials.secret_key);
        } else if (exchange == "upbit") {
            request.signature = ExchangeApiSigner::sign_upbit_request(
                method, endpoint, query_params, credentials.secret_key);
        } else if (exchange == "coinbase") {
            request.signature = ExchangeApiSigner::sign_coinbase_request(
                timestamp, method, endpoint, body, credentials.secret_key);
        } else {
            // Generic HMAC signing
            if (config.signature_method == "HMAC-SHA256") {
                request.signature = crypto_manager_->generate_hmac_sha256(signature_data, credentials.secret_key);
            } else if (config.signature_method == "HMAC-SHA512") {
                request.signature = crypto_manager_->generate_hmac_sha512(signature_data, credentials.secret_key);
            }
        }
        
        // Set required headers
        request.headers["X-API-KEY"] = credentials.api_key;
        request.headers["X-TIMESTAMP"] = timestamp;
        request.headers["X-NONCE"] = nonce;
        request.headers[config.signature_header_name] = request.signature;
        
        // Exchange-specific headers
        if (exchange == "binance") {
            request.headers["X-MBX-APIKEY"] = credentials.api_key;
        } else if (exchange == "upbit") {
            request.headers["Authorization"] = "Bearer " + request.signature;
        } else if (exchange == "coinbase") {
            request.headers["CB-ACCESS-KEY"] = credentials.api_key;
            request.headers["CB-ACCESS-SIGN"] = request.signature;
            request.headers["CB-ACCESS-TIMESTAMP"] = timestamp;
            if (!credentials.passphrase.empty()) {
                request.headers["CB-ACCESS-PASSPHRASE"] = credentials.passphrase;
            }
        }
        
        request.success = true;
        utils::Logger::debug("Successfully signed request for exchange: {}", exchange);
        
    } catch (const std::exception& e) {
        utils::Logger::error("Failed to sign request for {}: {}", exchange, e.what());
    }
    
    return request;
}

bool AuthManager::verify_exchange_signature(
    const std::string& exchange,
    const std::string& method,
    const std::string& endpoint,
    const std::string& query_params,
    const std::string& body,
    const std::unordered_map<std::string, std::string>& headers) {
    
    try {
        auto config_it = exchange_configs_.find(exchange);
        if (config_it == exchange_configs_.end()) {
            utils::Logger::error("No authentication configuration found for exchange: {}", exchange);
            return false;
        }
        
        const auto& config = config_it->second;
        
        // Extract signature from headers
        auto sig_it = headers.find(config.signature_header_name);
        if (sig_it == headers.end()) {
            utils::Logger::error("Signature header not found: {}", config.signature_header_name);
            return false;
        }
        
        std::string provided_signature = sig_it->second;
        
        // Extract timestamp
        auto timestamp_it = headers.find("X-TIMESTAMP");
        if (timestamp_it == headers.end()) {
            utils::Logger::error("Timestamp header not found");
            return false;
        }
        
        std::string timestamp = timestamp_it->second;
        
        // Validate timestamp
        if (!is_timestamp_valid(timestamp, config.timestamp_format, config.timestamp_tolerance_seconds)) {
            utils::Logger::error("Invalid or expired timestamp: {}", timestamp);
            return false;
        }
        
        // Validate nonce (if present)
        auto nonce_it = headers.find("X-NONCE");
        if (nonce_it != headers.end()) {
            if (!validate_nonce(nonce_it->second, std::chrono::seconds(config.timestamp_tolerance_seconds))) {
                utils::Logger::error("Invalid or reused nonce: {}", nonce_it->second);
                return false;
            }
        }
        
        // Get API credentials
        auto credentials = crypto_manager_->retrieve_api_credentials(exchange);
        if (!credentials.valid) {
            utils::Logger::error("Failed to retrieve API credentials for exchange: {}", exchange);
            return false;
        }
        
        // Build signature base string
        std::string signature_data = build_signature_base_string(
            method, endpoint, query_params, body, timestamp, nonce_it->second);
        
        // Verify signature
        bool signature_valid = false;
        if (config.signature_method == "HMAC-SHA256") {
            signature_valid = crypto_manager_->verify_hmac_sha256(
                signature_data, credentials.secret_key, provided_signature);
        } else if (config.signature_method == "HMAC-SHA512") {
            signature_valid = crypto_manager_->verify_hmac_sha512(
                signature_data, credentials.secret_key, provided_signature);
        }
        
        if (signature_valid) {
            utils::Logger::debug("Signature verification successful for exchange: {}", exchange);
        } else {
            utils::Logger::warn("Signature verification failed for exchange: {}", exchange);
        }
        
        return signature_valid;
        
    } catch (const std::exception& e) {
        utils::Logger::error("Error verifying signature for {}: {}", exchange, e.what());
        return false;
    }
}

std::string AuthManager::generate_api_token(const std::vector<std::string>& permissions,
                                          std::chrono::seconds ttl) {
    try {
        std::string token_id = crypto_manager_->generate_random_string(32);
        std::string secret = crypto_manager_->generate_random_string(64);
        
        ApiToken token;
        token.token_id = token_id;
        token.secret = secret;
        token.permissions = permissions;
        token.expires_at = std::chrono::system_clock::now() + ttl;
        token.is_active = true;
        
        api_tokens_[token_id] = token;
        
        // Create JWT-like token structure
        std::ostringstream token_stream;
        token_stream << token_id << "." << secret;
        
        utils::Logger::debug("Generated API token with {} permissions", permissions.size());
        return token_stream.str();
        
    } catch (const std::exception& e) {
        utils::Logger::error("Failed to generate API token: {}", e.what());
        return "";
    }
}

bool AuthManager::verify_api_token(const std::string& token, const std::string& required_permission) {
    try {
        // Parse token
        size_t dot_pos = token.find('.');
        if (dot_pos == std::string::npos) {
            utils::Logger::debug("Invalid token format");
            return false;
        }
        
        std::string token_id = token.substr(0, dot_pos);
        std::string secret = token.substr(dot_pos + 1);
        
        // Find token
        auto token_it = api_tokens_.find(token_id);
        if (token_it == api_tokens_.end()) {
            utils::Logger::debug("Token not found: {}", token_id);
            return false;
        }
        
        const auto& api_token = token_it->second;
        
        // Check if token is active
        if (!api_token.is_active) {
            utils::Logger::debug("Token is deactivated: {}", token_id);
            return false;
        }
        
        // Check expiration
        if (std::chrono::system_clock::now() > api_token.expires_at) {
            utils::Logger::debug("Token expired: {}", token_id);
            return false;
        }
        
        // Verify secret (constant-time comparison to prevent timing attacks)
        if (secret.length() != api_token.secret.length() || 
            !std::equal(secret.begin(), secret.end(), api_token.secret.begin())) {
            utils::Logger::warn("Invalid token secret for: {}", token_id);
            return false;
        }
        
        // Check permissions
        if (!required_permission.empty()) {
            auto perm_it = std::find(api_token.permissions.begin(), 
                                   api_token.permissions.end(), 
                                   required_permission);
            if (perm_it == api_token.permissions.end()) {
                utils::Logger::debug("Token lacks required permission '{}': {}", required_permission, token_id);
                return false;
            }
        }
        
        utils::Logger::debug("Token verification successful: {}", token_id);
        return true;
        
    } catch (const std::exception& e) {
        utils::Logger::error("Error verifying API token: {}", e.what());
        return false;
    }
}

std::string AuthManager::create_session(const std::string& user_id, std::chrono::seconds ttl) {
    try {
        std::string session_id = crypto_manager_->generate_random_string(48);
        
        Session session;
        session.session_id = session_id;
        session.user_id = user_id;
        session.created_at = std::chrono::system_clock::now();
        session.last_activity = session.created_at;
        session.expires_at = session.created_at + ttl;
        session.is_valid = true;
        
        sessions_[session_id] = session;
        
        utils::Logger::debug("Created session for user: {}", user_id);
        return session_id;
        
    } catch (const std::exception& e) {
        utils::Logger::error("Failed to create session for user {}: {}", user_id, e.what());
        return "";
    }
}

bool AuthManager::validate_session(const std::string& session_id) {
    try {
        auto session_it = sessions_.find(session_id);
        if (session_it == sessions_.end()) {
            utils::Logger::debug("Session not found: {}", session_id);
            return false;
        }
        
        auto& session = session_it->second;
        
        // Check if session is valid
        if (!session.is_valid) {
            utils::Logger::debug("Session is invalid: {}", session_id);
            return false;
        }
        
        // Check expiration
        if (std::chrono::system_clock::now() > session.expires_at) {
            utils::Logger::debug("Session expired: {}", session_id);
            session.is_valid = false;
            return false;
        }
        
        return true;
        
    } catch (const std::exception& e) {
        utils::Logger::error("Error validating session: {}", e.what());
        return false;
    }
}

bool AuthManager::check_rate_limit(const std::string& identifier,
                                 const std::string& action,
                                 int max_requests_per_window,
                                 std::chrono::seconds window_size) {
    try {
        std::string key = identifier + ":" + action;
        auto now = std::chrono::system_clock::now();
        
        auto rate_it = rate_limits_.find(key);
        if (rate_it == rate_limits_.end()) {
            // First request
            rate_limits_[key] = {now, 1};
            return true;
        }
        
        auto& entry = rate_it->second;
        
        // Check if we're in a new window
        if (now - entry.window_start >= window_size) {
            // Reset the window
            entry.window_start = now;
            entry.request_count = 1;
            return true;
        }
        
        // Check if we're under the limit
        if (entry.request_count < max_requests_per_window) {
            entry.request_count++;
            return true;
        }
        
        utils::Logger::debug("Rate limit exceeded for {}: {} requests in window", key, entry.request_count);
        return false;
        
    } catch (const std::exception& e) {
        utils::Logger::error("Error checking rate limit: {}", e.what());
        return false;
    }
}

std::string AuthManager::generate_nonce() {
    return crypto_manager_->generate_random_string(16) + 
           std::to_string(std::chrono::duration_cast<std::chrono::microseconds>(
               std::chrono::system_clock::now().time_since_epoch()).count());
}

bool AuthManager::validate_nonce(const std::string& nonce, std::chrono::seconds validity_window) {
    auto now = std::chrono::system_clock::now();
    
    // Check if nonce was already used
    auto nonce_it = used_nonces_.find(nonce);
    if (nonce_it != used_nonces_.end()) {
        utils::Logger::debug("Nonce already used: {}", nonce);
        return false;
    }
    
    // Store the nonce
    used_nonces_[nonce] = now;
    
    // Cleanup old nonces
    cleanup_old_nonces();
    
    return true;
}

std::string AuthManager::build_signature_base_string(
    const std::string& method,
    const std::string& url,
    const std::string& query_params,
    const std::string& body,
    const std::string& timestamp,
    const std::string& nonce) {
    
    std::ostringstream oss;
    oss << method << "\n"
        << url << "\n"
        << query_params << "\n"
        << timestamp << "\n"
        << nonce;
    
    if (!body.empty()) {
        oss << "\n" << body;
    }
    
    return oss.str();
}

std::string AuthManager::get_current_timestamp(const std::string& format) {
    auto now = std::chrono::system_clock::now();
    
    if (format == "unix") {
        auto timestamp = std::chrono::duration_cast<std::chrono::milliseconds>(now.time_since_epoch()).count();
        return std::to_string(timestamp);
    } else if (format == "iso8601") {
        auto time_t = std::chrono::system_clock::to_time_t(now);
        std::ostringstream oss;
        oss << std::put_time(std::gmtime(&time_t), "%Y-%m-%dT%H:%M:%SZ");
        return oss.str();
    }
    
    return std::to_string(std::chrono::duration_cast<std::chrono::seconds>(now.time_since_epoch()).count());
}

void AuthManager::cleanup_old_nonces() {
    auto now = std::chrono::system_clock::now();
    auto cutoff = now - std::chrono::minutes(5); // Keep nonces for 5 minutes
    
    auto it = used_nonces_.begin();
    while (it != used_nonces_.end()) {
        if (it->second < cutoff) {
            it = used_nonces_.erase(it);
        } else {
            ++it;
        }
    }
}

} // namespace security
} // namespace ats
// ExchangeApiSigner Implementation
ExchangeApiSigner::ExchangeApiSigner() {
}

std::string ExchangeApiSigner::sign_binance_request(const std::string& query_string, const std::string& secret_key) {
    return sign_hmac_sha256(query_string, secret_key);
}

std::string ExchangeApiSigner::sign_upbit_request(const std::string& method,
                                                const std::string& url,
                                                const std::string& query_params,
                                                const std::string& secret_key) {
    std::ostringstream payload;
    payload << method << " " << url;
    if (\!query_params.empty()) {
        payload << "?" << query_params;
    }
    
    return sign_hmac_sha512(payload.str(), secret_key);
}

std::string ExchangeApiSigner::sign_coinbase_request(const std::string& timestamp,
                                                   const std::string& method,
                                                   const std::string& request_path,
                                                   const std::string& body,
                                                   const std::string& secret_key) {
    std::ostringstream message;
    message << timestamp << method << request_path << body;
    
    return sign_hmac_sha256(message.str(), secret_key);
}

std::string ExchangeApiSigner::sign_hmac_sha256(const std::string& data, const std::string& key) {
    unsigned char hash[SHA256_DIGEST_LENGTH];
    unsigned int hash_len;
    
    HMAC(EVP_sha256(),
         key.c_str(), static_cast<int>(key.length()),
         reinterpret_cast<const unsigned char*>(data.c_str()), data.length(),
         hash, &hash_len);
    
    CryptoManager crypto;
    return crypto.bytes_to_hex(std::vector<uint8_t>(hash, hash + hash_len));
}

std::string ExchangeApiSigner::sign_hmac_sha512(const std::string& data, const std::string& key) {
    unsigned char hash[SHA512_DIGEST_LENGTH];
    unsigned int hash_len;
    
    HMAC(EVP_sha512(),
         key.c_str(), static_cast<int>(key.length()),
         reinterpret_cast<const unsigned char*>(data.c_str()), data.length(),
         hash, &hash_len);
    
    CryptoManager crypto;
    return crypto.bytes_to_hex(std::vector<uint8_t>(hash, hash + hash_len));
}

std::string ExchangeApiSigner::sha256_hash(const std::string& input) {
    unsigned char hash[SHA256_DIGEST_LENGTH];
    SHA256(reinterpret_cast<const unsigned char*>(input.c_str()), input.length(), hash);
    
    CryptoManager crypto;
    return crypto.bytes_to_hex(std::vector<uint8_t>(hash, hash + SHA256_DIGEST_LENGTH));
}

// AuthMiddleware Implementation  
AuthMiddleware::AuthMiddleware(std::shared_ptr<AuthManager> auth_manager)
    : auth_manager_(auth_manager) {
}

bool AuthMiddleware::authenticate_request(const std::unordered_map<std::string, std::string>& headers,
                                        const std::string& required_permission) {
    try {
        // Try Bearer token authentication
        auto auth_it = headers.find("Authorization");
        if (auth_it \!= headers.end()) {
            std::string bearer_token = extract_bearer_token(auth_it->second);
            if (\!bearer_token.empty()) {
                return auth_manager_->verify_api_token(bearer_token, required_permission);
            }
        }
        
        // Try API key authentication
        std::string api_key = extract_api_key(headers);
        if (\!api_key.empty()) {
            return auth_manager_->verify_api_token(api_key, required_permission);
        }
        
        utils::Logger::debug("No valid authentication found in request");
        return false;
        
    } catch (const std::exception& e) {
        utils::Logger::error("Error authenticating request: {}", e.what());
        return false;
    }
}

std::string AuthMiddleware::extract_bearer_token(const std::string& auth_header) {
    const std::string bearer_prefix = "Bearer ";
    if (auth_header.length() > bearer_prefix.length() &&
        auth_header.substr(0, bearer_prefix.length()) == bearer_prefix) {
        return auth_header.substr(bearer_prefix.length());
    }
    return "";
}

std::string AuthMiddleware::extract_api_key(const std::unordered_map<std::string, std::string>& headers) {
    auto key_it = headers.find("X-API-KEY");
    if (key_it \!= headers.end()) {
        return key_it->second;
    }
    
    key_it = headers.find("API-KEY");
    if (key_it \!= headers.end()) {
        return key_it->second;
    }
    
    return "";
}
