#pragma once

#include "crypto_manager.hpp"
#include <string>
#include <vector>
#include <memory>
#include <unordered_map>
#include <openssl/ssl.h>
#include <openssl/x509.h>
#include <openssl/x509v3.h>
#include <openssl/pem.h>
#include <openssl/rsa.h>
#include <openssl/evp.h>
#include <grpcpp/security/server_credentials.h>
#include <grpcpp/security/tls_credentials_options.h>

namespace ats {
namespace security {

// TLS Certificate Management
class TlsManager {
public:
    TlsManager();
    ~TlsManager();
    
    bool initialize(const std::string& cert_storage_path = "./security/certs/");
    void shutdown();
    
    // Certificate Generation
    struct CertificateRequest {
        std::string common_name;
        std::string organization;
        std::string organizational_unit;
        std::string country;
        std::string state;
        std::string city;
        std::vector<std::string> san_dns_names;  // Subject Alternative Names
        std::vector<std::string> san_ip_addresses;
        int validity_days = 365;
        int key_size = 2048;
    };
    
    struct CertificateInfo {
        std::string cert_pem;
        std::string private_key_pem;
        std::string public_key_pem;
        std::string cert_fingerprint;
        std::chrono::system_clock::time_point not_before;
        std::chrono::system_clock::time_point not_after;
        bool is_valid = false;
    };
    
    // Self-signed certificate generation
    CertificateInfo generate_self_signed_certificate(const CertificateRequest& request);
    
    // Certificate Signing Request (CSR) generation
    std::string generate_csr(const CertificateRequest& request, std::string& private_key_pem);
    
    // Certificate loading and validation
    bool load_certificate(const std::string& cert_id, 
                         const std::string& cert_file_path, 
                         const std::string& key_file_path,
                         const std::string& ca_file_path = "");
    
    bool validate_certificate(const std::string& cert_id);
    bool verify_certificate_chain(const std::string& cert_id);
    bool check_certificate_expiration(const std::string& cert_id, int warning_days = 30);
    
    // Certificate storage and retrieval
    bool save_certificate(const std::string& cert_id, const CertificateInfo& cert_info);
    CertificateInfo get_certificate(const std::string& cert_id);
    std::vector<std::string> list_certificates();
    bool delete_certificate(const std::string& cert_id);
    
    // gRPC TLS Configuration
    std::shared_ptr<grpc::ServerCredentials> create_grpc_server_credentials(
        const std::string& cert_id,
        bool require_client_cert = false);
    
    std::shared_ptr<grpc::ChannelCredentials> create_grpc_client_credentials(
        const std::string& cert_id = "",
        const std::string& target_name_override = "");
    
    // SSL Context Creation (for REST and WebSocket)
    SSL_CTX* create_ssl_context(const std::string& cert_id, bool is_server = true);
    void configure_ssl_context(SSL_CTX* ctx, bool require_client_cert = false);
    
    // Certificate Renewal
    bool is_certificate_near_expiry(const std::string& cert_id, int warning_days = 30);
    bool renew_certificate(const std::string& cert_id, const CertificateRequest& request);
    
    // OCSP (Online Certificate Status Protocol) Support
    enum class CertificateStatus {
        VALID,
        REVOKED,
        UNKNOWN,
        ERROR
    };
    
    CertificateStatus check_ocsp_status(const std::string& cert_id);
    
    // Certificate Authority (CA) Management
    struct CaInfo {
        std::string ca_cert_pem;
        std::string ca_key_pem;
        std::string ca_name;
        bool is_root_ca = false;
    };
    
    bool create_certificate_authority(const std::string& ca_id, const CertificateRequest& request);
    bool sign_certificate_with_ca(const std::string& ca_id, 
                                 const std::string& csr_pem, 
                                 CertificateInfo& signed_cert);
    
    // TLS Configuration Profiles
    struct TlsProfile {
        std::string profile_name;
        std::vector<std::string> cipher_suites;
        std::vector<std::string> protocols;  // TLS1.2, TLS1.3
        bool require_client_cert = false;
        bool verify_peer = true;
        int min_protocol_version = TLS1_2_VERSION;
        int max_protocol_version = TLS1_3_VERSION;
    };
    
    bool configure_tls_profile(const std::string& profile_name, const TlsProfile& profile);
    TlsProfile get_tls_profile(const std::string& profile_name);
    
    // Security Policies
    bool enable_perfect_forward_secrecy(SSL_CTX* ctx);
    bool configure_secure_renegotiation(SSL_CTX* ctx);
    bool set_minimum_tls_version(SSL_CTX* ctx, int min_version);
    
private:
    std::string cert_storage_path_;
    std::unordered_map<std::string, CertificateInfo> certificates_;
    std::unordered_map<std::string, CaInfo> certificate_authorities_;
    std::unordered_map<std::string, TlsProfile> tls_profiles_;
    
    // OpenSSL management
    bool ssl_initialized_;
    void initialize_openssl();
    void cleanup_openssl();
    
    // Certificate file operations
    bool save_pem_to_file(const std::string& file_path, const std::string& pem_data);
    std::string load_pem_from_file(const std::string& file_path);
    
    // Certificate validation helpers
    bool verify_certificate_signature(X509* cert, X509* ca_cert);
    bool check_certificate_purpose(X509* cert, int purpose);
    std::string get_certificate_fingerprint(X509* cert);
    
    // Key generation helpers
    EVP_PKEY* generate_rsa_key(int key_size);
    EVP_PKEY* generate_ec_key(int curve_nid = NID_X9_62_prime256v1);
    
    // X.509 extension helpers
    bool add_certificate_extensions(X509* cert, X509* ca_cert, const CertificateRequest& request);
    bool add_san_extension(X509* cert, const std::vector<std::string>& dns_names, 
                          const std::vector<std::string>& ip_addresses);
    
    // Error handling
    std::string get_ssl_error_string();
    void log_ssl_error(const std::string& operation);
};

// TLS Connection Manager for different protocols
class TlsConnectionManager {
public:
    TlsConnectionManager(std::shared_ptr<TlsManager> tls_manager);
    ~TlsConnectionManager();
    
    // gRPC TLS Configuration
    struct GrpcTlsConfig {
        std::string server_cert_id;
        std::string client_cert_id;
        bool mutual_tls = false;
        std::string tls_profile = "default";
        std::vector<std::string> allowed_client_certs;
    };
    
    bool configure_grpc_server_tls(const std::string& server_address, 
                                  const GrpcTlsConfig& config);
    bool configure_grpc_client_tls(const std::string& target_address, 
                                  const GrpcTlsConfig& config);
    
    // REST API TLS Configuration
    struct RestTlsConfig {
        std::string cert_id;
        std::string tls_profile = "default";
        bool require_client_cert = false;
        int port = 8443;
    };
    
    SSL_CTX* get_rest_ssl_context(const RestTlsConfig& config);
    
    // WebSocket TLS Configuration
    struct WebSocketTlsConfig {
        std::string cert_id;
        std::string tls_profile = "secure";
        bool compression_enabled = false;
        std::vector<std::string> supported_protocols;
    };
    
    SSL_CTX* get_websocket_ssl_context(const WebSocketTlsConfig& config);
    
    // Connection validation
    bool validate_tls_connection(SSL* ssl);
    std::string get_peer_certificate_info(SSL* ssl);
    bool verify_peer_certificate(SSL* ssl, const std::string& expected_hostname = "");
    
    // Connection monitoring
    struct TlsConnectionStats {
        std::string protocol_version;
        std::string cipher_suite;
        std::string peer_certificate_subject;
        bool perfect_forward_secrecy;
        int key_size;
        std::chrono::system_clock::time_point connection_time;
    };
    
    TlsConnectionStats get_connection_stats(SSL* ssl);
    
private:
    std::shared_ptr<TlsManager> tls_manager_;
    std::unordered_map<std::string, SSL_CTX*> ssl_contexts_;
    
    // Callback functions
    static int verify_callback(int preverify_ok, X509_STORE_CTX* ctx);
    static int alpn_select_callback(SSL* ssl, const unsigned char** out, 
                                   unsigned char* outlen, const unsigned char* in, 
                                   unsigned int inlen, void* arg);
    
    void cleanup_ssl_contexts();
};

// TLS Security Auditor
class TlsSecurityAuditor {
public:
    TlsSecurityAuditor(std::shared_ptr<TlsManager> tls_manager);
    
    // Security assessment
    struct SecurityAssessment {
        std::string cert_id;
        bool is_secure = true;
        std::vector<std::string> warnings;
        std::vector<std::string> vulnerabilities;
        std::vector<std::string> recommendations;
        int security_score = 0;  // 0-100
    };
    
    SecurityAssessment audit_certificate(const std::string& cert_id);
    SecurityAssessment audit_tls_configuration(const std::string& profile_name);
    
    // Compliance checks
    bool check_pci_dss_compliance(const std::string& cert_id);
    bool check_fips_compliance(const std::string& cert_id);
    bool check_common_criteria_compliance(const std::string& cert_id);
    
    // Vulnerability scanning
    bool scan_for_weak_ciphers(SSL_CTX* ctx);
    bool check_certificate_chain_validation(const std::string& cert_id);
    bool verify_perfect_forward_secrecy(SSL_CTX* ctx);
    
    // Monitoring and alerting
    void schedule_certificate_expiry_monitoring();
    void check_all_certificates_expiry(int warning_days = 30);
    
private:
    std::shared_ptr<TlsManager> tls_manager_;
    
    // Security evaluation helpers
    int evaluate_key_strength(EVP_PKEY* key);
    bool is_cipher_suite_secure(const std::string& cipher);
    bool is_protocol_version_secure(int version);
};

} // namespace security
} // namespace ats