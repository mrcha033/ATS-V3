#pragma once

#include "crypto_manager.hpp"
#include <string>
#include <unordered_map>
#include <chrono>
#include <memory>
#include <functional>

namespace ats {
namespace security {

// Authentication and signature verification
class AuthManager {
public:
    AuthManager();
    ~AuthManager();
    
    bool initialize(std::shared_ptr<CryptoManager> crypto_manager);
    
    // Exchange API Authentication
    struct ExchangeAuthConfig {
        std::string exchange_name;
        std::string signature_method; // "HMAC-SHA256", "HMAC-SHA512", "RSA-SHA256"
        std::string timestamp_format; // "unix", "iso8601"
        std::vector<std::string> required_headers;
        std::string signature_header_name;
        bool include_body_in_signature = true;
        int timestamp_tolerance_seconds = 30;
    };
    
    bool configure_exchange_auth(const ExchangeAuthConfig& config);
    
    // Request signing for exchange APIs
    struct SignedRequest {
        std::string method;
        std::string url;
        std::string query_string;
        std::string body;
        std::unordered_map<std::string, std::string> headers;
        std::string signature;
        bool success = false;
    };
    
    SignedRequest sign_exchange_request(const std::string& exchange,
                                      const std::string& method,
                                      const std::string& endpoint,
                                      const std::string& query_params = "",
                                      const std::string& body = "");
    
    // Signature verification for incoming requests
    bool verify_exchange_signature(const std::string& exchange,
                                 const std::string& method,
                                 const std::string& endpoint,
                                 const std::string& query_params,
                                 const std::string& body,
                                 const std::unordered_map<std::string, std::string>& headers);
    
    // Internal API authentication (for our own services)
    struct ApiToken {
        std::string token_id;
        std::string secret;
        std::vector<std::string> permissions;
        std::chrono::system_clock::time_point expires_at;
        bool is_active = true;
    };
    
    std::string generate_api_token(const std::vector<std::string>& permissions,
                                 std::chrono::seconds ttl = std::chrono::seconds(3600));
    bool verify_api_token(const std::string& token, const std::string& required_permission = "");
    bool revoke_api_token(const std::string& token_id);
    
    // JWT Token management
    struct JwtClaims {
        std::string subject;
        std::string issuer;
        std::string audience;
        std::chrono::system_clock::time_point issued_at;
        std::chrono::system_clock::time_point expires_at;
        std::unordered_map<std::string, std::string> custom_claims;
    };
    
    std::string generate_jwt_token(const JwtClaims& claims);
    bool verify_jwt_token(const std::string& token, JwtClaims& claims);
    
    // Session management
    struct Session {
        std::string session_id;
        std::string user_id;
        std::chrono::system_clock::time_point created_at;
        std::chrono::system_clock::time_point last_activity;
        std::chrono::system_clock::time_point expires_at;
        std::unordered_map<std::string, std::string> attributes;
        bool is_valid = true;
    };
    
    std::string create_session(const std::string& user_id,
                             std::chrono::seconds ttl = std::chrono::seconds(86400));
    bool validate_session(const std::string& session_id);
    bool update_session_activity(const std::string& session_id);
    bool terminate_session(const std::string& session_id);
    Session get_session(const std::string& session_id);
    
    // Rate limiting support
    bool check_rate_limit(const std::string& identifier,
                         const std::string& action,
                         int max_requests_per_window,
                         std::chrono::seconds window_size);
    
    // Nonce management (prevent replay attacks)
    bool validate_nonce(const std::string& nonce, std::chrono::seconds validity_window);
    std::string generate_nonce();
    
private:
    std::shared_ptr<CryptoManager> crypto_manager_;
    
    // Exchange configurations
    std::unordered_map<std::string, ExchangeAuthConfig> exchange_configs_;
    
    // Token storage
    std::unordered_map<std::string, ApiToken> api_tokens_;
    
    // Session storage
    std::unordered_map<std::string, Session> sessions_;
    
    // Rate limiting storage
    struct RateLimitEntry {
        std::chrono::system_clock::time_point window_start;
        int request_count;
    };
    std::unordered_map<std::string, RateLimitEntry> rate_limits_;
    
    // Nonce storage for replay protection
    std::unordered_map<std::string, std::chrono::system_clock::time_point> used_nonces_;
    
    // Helper methods
    std::string build_signature_base_string(const std::string& method,
                                           const std::string& url,
                                           const std::string& query_params,
                                           const std::string& body,
                                           const std::string& timestamp,
                                           const std::string& nonce = "");
    
    std::string get_current_timestamp(const std::string& format);
    bool is_timestamp_valid(const std::string& timestamp,
                          const std::string& format,
                          int tolerance_seconds);
    
    void cleanup_expired_tokens();
    void cleanup_expired_sessions();
    void cleanup_old_nonces();
    void cleanup_rate_limits();
    
    // JWT helpers
    std::string base64_url_encode(const std::string& input);
    std::string base64_url_decode(const std::string& input);
    std::string create_jwt_header();
    std::string create_jwt_payload(const JwtClaims& claims);
};

// HMAC Request Signer for various exchange APIs
class ExchangeApiSigner {
public:
    ExchangeApiSigner();
    
    // Binance signature
    static std::string sign_binance_request(const std::string& query_string,
                                           const std::string& secret_key);
    
    // Upbit signature
    static std::string sign_upbit_request(const std::string& method,
                                         const std::string& url,
                                         const std::string& query_params,
                                         const std::string& secret_key);
    
    // Coinbase Pro signature
    static std::string sign_coinbase_request(const std::string& timestamp,
                                            const std::string& method,
                                            const std::string& request_path,
                                            const std::string& body,
                                            const std::string& secret_key);
    
    // Kraken signature
    static std::string sign_kraken_request(const std::string& url_path,
                                          const std::string& nonce,
                                          const std::string& post_data,
                                          const std::string& secret_key);
    
    // Generic HMAC signing
    static std::string sign_hmac_sha256(const std::string& data, const std::string& key);
    static std::string sign_hmac_sha512(const std::string& data, const std::string& key);
    
private:
    static std::string url_encode(const std::string& input);
    static std::string sha256_hash(const std::string& input);
};

// Request authentication middleware
class AuthMiddleware {
public:
    using AuthCallback = std::function<bool(const std::string& token, const std::string& permission)>;
    using RateLimitCallback = std::function<bool(const std::string& identifier, const std::string& action)>;
    
    AuthMiddleware(std::shared_ptr<AuthManager> auth_manager);
    
    // Middleware functions
    bool authenticate_request(const std::unordered_map<std::string, std::string>& headers,
                            const std::string& required_permission = "");
    
    bool check_request_rate_limit(const std::string& client_ip,
                                const std::string& endpoint,
                                int max_requests = 100,
                                std::chrono::seconds window = std::chrono::seconds(60));
    
    bool validate_request_signature(const std::string& method,
                                  const std::string& url,
                                  const std::string& body,
                                  const std::unordered_map<std::string, std::string>& headers);
    
    // CSRF protection
    bool validate_csrf_token(const std::string& token,
                            const std::string& session_id);
    
private:
    std::shared_ptr<AuthManager> auth_manager_;
    
    std::string extract_bearer_token(const std::string& auth_header);
    std::string extract_api_key(const std::unordered_map<std::string, std::string>& headers);
};

} // namespace security
} // namespace ats