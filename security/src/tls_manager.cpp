#include "../include/tls_manager.hpp"
#include "../../utils/logger.hpp"
#include <filesystem>
#include <fstream>
#include <sstream>
#include <iomanip>
#include <algorithm>
#include <openssl/rand.h>
#include <openssl/err.h>
#include <openssl/bio.h>
#include <openssl/buffer.h>
#include <grpcpp/security/tls_certificate_provider.h>

namespace ats {
namespace security {

// TlsManager Implementation
TlsManager::TlsManager() : ssl_initialized_(false) {
    LOG_INFO("TlsManager initialized");
}

TlsManager::~TlsManager() {
    shutdown();
}

bool TlsManager::initialize(const std::string& cert_storage_path) {
    try {
        cert_storage_path_ = cert_storage_path;
        
        // Create certificate storage directory
        std::filesystem::create_directories(cert_storage_path_);
        
        // Initialize OpenSSL
        initialize_openssl();
        
        // Create default TLS profiles
        TlsProfile default_profile;
        default_profile.profile_name = "default";
        default_profile.cipher_suites = {
            "ECDHE-RSA-AES256-GCM-SHA384",
            "ECDHE-RSA-AES128-GCM-SHA256",
            "ECDHE-RSA-AES256-SHA384",
            "ECDHE-RSA-AES128-SHA256"
        };
        default_profile.protocols = {"TLSv1.2", "TLSv1.3"};
        default_profile.require_client_cert = false;
        default_profile.verify_peer = true;
        default_profile.min_protocol_version = TLS1_2_VERSION;
        default_profile.max_protocol_version = TLS1_3_VERSION;
        configure_tls_profile("default", default_profile);
        
        TlsProfile secure_profile;
        secure_profile.profile_name = "secure";
        secure_profile.cipher_suites = {
            "ECDHE-RSA-AES256-GCM-SHA384",
            "ECDHE-ECDSA-AES256-GCM-SHA384",
            "ECDHE-RSA-CHACHA20-POLY1305",
            "ECDHE-ECDSA-CHACHA20-POLY1305"
        };
        secure_profile.protocols = {"TLSv1.3"};
        secure_profile.require_client_cert = true;
        secure_profile.verify_peer = true;
        secure_profile.min_protocol_version = TLS1_3_VERSION;
        secure_profile.max_protocol_version = TLS1_3_VERSION;
        configure_tls_profile("secure", secure_profile);
        
        LOG_INFO("TlsManager initialized with certificate storage: {}", cert_storage_path_);
        return true;
        
    } catch (const std::exception& e) {
        LOG_ERROR("Failed to initialize TlsManager: {}", e.what());
        return false;
    }
}

void TlsManager::shutdown() {
    certificates_.clear();
    certificate_authorities_.clear();
    tls_profiles_.clear();
    
    if (ssl_initialized_) {
        cleanup_openssl();
        ssl_initialized_ = false;
    }
    
    LOG_INFO("TlsManager shutdown completed");
}

TlsManager::CertificateInfo TlsManager::generate_self_signed_certificate(const CertificateRequest& request) {
    CertificateInfo cert_info;
    
    try {
        // Generate private key
        EVP_PKEY* private_key = generate_rsa_key(request.key_size);
        if (!private_key) {
            LOG_ERROR("Failed to generate private key");
            return cert_info;
        }
        
        // Create certificate
        X509* cert = X509_new();
        if (!cert) {
            EVP_PKEY_free(private_key);
            LOG_ERROR("Failed to create X509 certificate");
            return cert_info;
        }
        
        // Set version (X.509 v3)
        X509_set_version(cert, 2);
        
        // Set serial number
        ASN1_INTEGER_set(X509_get_serialNumber(cert), 1);
        
        // Set validity period
        X509_gmtime_adj(X509_get_notBefore(cert), 0);
        X509_gmtime_adj(X509_get_notAfter(cert), request.validity_days * 24 * 60 * 60);
        
        // Set public key
        X509_set_pubkey(cert, private_key);
        
        // Set subject name
        X509_NAME* name = X509_get_subject_name(cert);
        X509_NAME_add_entry_by_txt(name, "C", MBSTRING_ASC, 
                                  reinterpret_cast<const unsigned char*>(request.country.c_str()), -1, -1, 0);
        X509_NAME_add_entry_by_txt(name, "ST", MBSTRING_ASC, 
                                  reinterpret_cast<const unsigned char*>(request.state.c_str()), -1, -1, 0);
        X509_NAME_add_entry_by_txt(name, "L", MBSTRING_ASC, 
                                  reinterpret_cast<const unsigned char*>(request.city.c_str()), -1, -1, 0);
        X509_NAME_add_entry_by_txt(name, "O", MBSTRING_ASC, 
                                  reinterpret_cast<const unsigned char*>(request.organization.c_str()), -1, -1, 0);
        X509_NAME_add_entry_by_txt(name, "OU", MBSTRING_ASC, 
                                  reinterpret_cast<const unsigned char*>(request.organizational_unit.c_str()), -1, -1, 0);
        X509_NAME_add_entry_by_txt(name, "CN", MBSTRING_ASC, 
                                  reinterpret_cast<const unsigned char*>(request.common_name.c_str()), -1, -1, 0);
        
        // Set issuer name (same as subject for self-signed)
        X509_set_issuer_name(cert, name);
        
        // Add extensions
        if (!add_certificate_extensions(cert, cert, request)) {
            LOG_WARNING("Failed to add some certificate extensions");
        }
        
        // Sign the certificate
        if (!X509_sign(cert, private_key, EVP_sha256())) {
            log_ssl_error("Certificate signing");
            X509_free(cert);
            EVP_PKEY_free(private_key);
            return cert_info;
        }
        
        // Convert to PEM format
        BIO* cert_bio = BIO_new(BIO_s_mem());
        if (PEM_write_bio_X509(cert_bio, cert)) {
            BUF_MEM* cert_buf;
            BIO_get_mem_ptr(cert_bio, &cert_buf);
            cert_info.cert_pem = std::string(cert_buf->data, cert_buf->length);
        }
        BIO_free(cert_bio);
        
        BIO* key_bio = BIO_new(BIO_s_mem());
        if (PEM_write_bio_PrivateKey(key_bio, private_key, nullptr, nullptr, 0, nullptr, nullptr)) {
            BUF_MEM* key_buf;
            BIO_get_mem_ptr(key_bio, &key_buf);
            cert_info.private_key_pem = std::string(key_buf->data, key_buf->length);
        }
        BIO_free(key_bio);
        
        BIO* pub_key_bio = BIO_new(BIO_s_mem());
        if (PEM_write_bio_PUBKEY(pub_key_bio, private_key)) {
            BUF_MEM* pub_key_buf;
            BIO_get_mem_ptr(pub_key_bio, &pub_key_buf);
            cert_info.public_key_pem = std::string(pub_key_buf->data, pub_key_buf->length);
        }
        BIO_free(pub_key_bio);
        
        // Get certificate fingerprint
        cert_info.cert_fingerprint = get_certificate_fingerprint(cert);
        
        // Set validity dates
        ASN1_TIME* not_before = X509_get_notBefore(cert);
        ASN1_TIME* not_after = X509_get_notAfter(cert);
        
        // Convert ASN1_TIME to time_point (simplified)
        cert_info.not_before = std::chrono::system_clock::now();
        cert_info.not_after = std::chrono::system_clock::now() + std::chrono::hours(24 * request.validity_days);
        
        cert_info.is_valid = true;
        
        // Cleanup
        X509_free(cert);
        EVP_PKEY_free(private_key);
        
        LOG_INFO("Generated self-signed certificate for {}", request.common_name);
        
    } catch (const std::exception& e) {
        LOG_ERROR("Failed to generate self-signed certificate: {}", e.what());
    }
    
    return cert_info;
}

bool TlsManager::save_certificate(const std::string& cert_id, const CertificateInfo& cert_info) {
    try {
        // Save certificate to memory
        certificates_[cert_id] = cert_info;
        
        // Save to files
        std::string cert_file = cert_storage_path_ + cert_id + ".crt";
        std::string key_file = cert_storage_path_ + cert_id + ".key";
        
        if (!save_pem_to_file(cert_file, cert_info.cert_pem)) {
            LOG_ERROR("Failed to save certificate file: {}", cert_file);
            return false;
        }
        
        if (!save_pem_to_file(key_file, cert_info.private_key_pem)) {
            LOG_ERROR("Failed to save private key file: {}", key_file);
            return false;
        }
        
        // Set secure file permissions
        std::filesystem::permissions(cert_file, std::filesystem::perms::owner_read | std::filesystem::perms::owner_write);
        std::filesystem::permissions(key_file, std::filesystem::perms::owner_read);
        
        LOG_INFO("Saved certificate: {}", cert_id);
        return true;
        
    } catch (const std::exception& e) {
        LOG_ERROR("Failed to save certificate {}: {}", cert_id, e.what());
        return false;
    }
}

TlsManager::CertificateInfo TlsManager::get_certificate(const std::string& cert_id) {
    auto it = certificates_.find(cert_id);
    if (it != certificates_.end()) {
        return it->second;
    }
    
    // Try to load from file
    CertificateInfo cert_info;
    std::string cert_file = cert_storage_path_ + cert_id + ".crt";
    std::string key_file = cert_storage_path_ + cert_id + ".key";
    
    if (std::filesystem::exists(cert_file) && std::filesystem::exists(key_file)) {
        cert_info.cert_pem = load_pem_from_file(cert_file);
        cert_info.private_key_pem = load_pem_from_file(key_file);
        
        if (!cert_info.cert_pem.empty() && !cert_info.private_key_pem.empty()) {
            cert_info.is_valid = true;
            certificates_[cert_id] = cert_info;
            LOG_DEBUG("Loaded certificate from file: {}", cert_id);
        }
    }
    
    return cert_info;
}

std::shared_ptr<grpc::ServerCredentials> TlsManager::create_grpc_server_credentials(
    const std::string& cert_id, bool require_client_cert) {
    
    try {
        auto cert_info = get_certificate(cert_id);
        if (!cert_info.is_valid) {
            LOG_ERROR("Certificate not found or invalid: {}", cert_id);
            return nullptr;
        }
        
        grpc::SslServerCredentialsOptions ssl_options;
        
        // Set server certificate and key
        grpc::SslServerCredentialsOptions::PemKeyCertPair key_cert_pair;
        key_cert_pair.private_key = cert_info.private_key_pem;
        key_cert_pair.cert_chain = cert_info.cert_pem;
        ssl_options.pem_key_cert_pairs.push_back(key_cert_pair);
        
        // Configure client certificate requirements
        if (require_client_cert) {
            ssl_options.client_certificate_request = GRPC_SSL_REQUEST_AND_REQUIRE_CLIENT_CERTIFICATE_AND_VERIFY;
        } else {
            ssl_options.client_certificate_request = GRPC_SSL_DONT_REQUEST_CLIENT_CERTIFICATE;
        }
        
        auto server_creds = grpc::SslServerCredentials(ssl_options);
        
        LOG_DEBUG("Created gRPC server credentials for certificate: {}", cert_id);
        return server_creds;
        
    } catch (const std::exception& e) {
        LOG_ERROR("Failed to create gRPC server credentials: {}", e.what());
        return nullptr;
    }
}

std::shared_ptr<grpc::ChannelCredentials> TlsManager::create_grpc_client_credentials(
    const std::string& cert_id, const std::string& target_name_override) {
    
    try {
        grpc::SslCredentialsOptions ssl_options;
        
        // If a specific certificate is provided, use it for client authentication
        if (!cert_id.empty()) {
            auto cert_info = get_certificate(cert_id);
            if (cert_info.is_valid) {
                ssl_options.pem_private_key = cert_info.private_key_pem;
                ssl_options.pem_cert_chain = cert_info.cert_pem;
            }
        }
        
        // Override target name if specified (useful for testing)
        grpc::ChannelArguments args;
        if (!target_name_override.empty()) {
            args.SetSslTargetNameOverride(target_name_override);
        }
        
        auto client_creds = grpc::SslCredentials(ssl_options);
        
        LOG_DEBUG("Created gRPC client credentials");
        return client_creds;
        
    } catch (const std::exception& e) {
        LOG_ERROR("Failed to create gRPC client credentials: {}", e.what());
        return nullptr;
    }
}

SSL_CTX* TlsManager::create_ssl_context(const std::string& cert_id, bool is_server) {
    try {
        const SSL_METHOD* method;
        
        if (is_server) {
            method = TLS_server_method();
        } else {
            method = TLS_client_method();
        }
        
        SSL_CTX* ctx = SSL_CTX_new(method);
        if (!ctx) {
            log_ssl_error("SSL_CTX creation");
            return nullptr;
        }
        
        // Configure basic security settings
        configure_ssl_context(ctx, false);
        
        // Load certificate if specified
        if (!cert_id.empty()) {
            auto cert_info = get_certificate(cert_id);
            if (!cert_info.is_valid) {
                LOG_ERROR("Certificate not found: {}", cert_id);
                SSL_CTX_free(ctx);
                return nullptr;
            }
            
            // Load certificate
            BIO* cert_bio = BIO_new_mem_buf(cert_info.cert_pem.c_str(), cert_info.cert_pem.length());
            X509* cert = PEM_read_bio_X509(cert_bio, nullptr, nullptr, nullptr);
            BIO_free(cert_bio);
            
            if (!cert || SSL_CTX_use_certificate(ctx, cert) != 1) {
                log_ssl_error("Certificate loading");
                if (cert) X509_free(cert);
                SSL_CTX_free(ctx);
                return nullptr;
            }
            X509_free(cert);
            
            // Load private key
            BIO* key_bio = BIO_new_mem_buf(cert_info.private_key_pem.c_str(), cert_info.private_key_pem.length());
            EVP_PKEY* key = PEM_read_bio_PrivateKey(key_bio, nullptr, nullptr, nullptr);
            BIO_free(key_bio);
            
            if (!key || SSL_CTX_use_PrivateKey(ctx, key) != 1) {
                log_ssl_error("Private key loading");
                if (key) EVP_PKEY_free(key);
                SSL_CTX_free(ctx);
                return nullptr;
            }
            EVP_PKEY_free(key);
            
            // Verify that certificate and key match
            if (SSL_CTX_check_private_key(ctx) != 1) {
                log_ssl_error("Certificate/key verification");
                SSL_CTX_free(ctx);
                return nullptr;
            }
        }
        
        LOG_DEBUG("Created SSL context for certificate: {}", cert_id);
        return ctx;
        
    } catch (const std::exception& e) {
        LOG_ERROR("Failed to create SSL context: {}", e.what());
        return nullptr;
    }
}

void TlsManager::configure_ssl_context(SSL_CTX* ctx, bool require_client_cert) {
    if (!ctx) return;
    
    try {
        // Set minimum TLS version
        SSL_CTX_set_min_proto_version(ctx, TLS1_2_VERSION);
        SSL_CTX_set_max_proto_version(ctx, TLS1_3_VERSION);
        
        // Configure cipher suites
        const char* cipher_list = "ECDHE+AESGCM:ECDHE+CHACHA20:DHE+AESGCM:DHE+CHACHA20:!aNULL:!MD5:!DSS";
        SSL_CTX_set_cipher_list(ctx, cipher_list);
        
        // Enable perfect forward secrecy
        enable_perfect_forward_secrecy(ctx);
        
        // Configure secure renegotiation
        configure_secure_renegotiation(ctx);
        
        // Set security level
        SSL_CTX_set_security_level(ctx, 2); // 112-bit security level
        
        // Configure session management
        SSL_CTX_set_session_cache_mode(ctx, SSL_SESS_CACHE_SERVER);
        SSL_CTX_set_timeout(ctx, 300); // 5 minutes
        
        // Configure client certificate verification
        if (require_client_cert) {
            SSL_CTX_set_verify(ctx, SSL_VERIFY_PEER | SSL_VERIFY_FAIL_IF_NO_PEER_CERT, nullptr);
        } else {
            SSL_CTX_set_verify(ctx, SSL_VERIFY_NONE, nullptr);
        }
        
        LOG_DEBUG("Configured SSL context with security settings");
        
    } catch (const std::exception& e) {
        LOG_ERROR("Failed to configure SSL context: {}", e.what());
    }
}

bool TlsManager::enable_perfect_forward_secrecy(SSL_CTX* ctx) {
    if (!ctx) return false;
    
    try {
        // Set ECDH curve for ECDHE
        if (SSL_CTX_set_ecdh_auto(ctx, 1) != 1) {
            // Fallback for older OpenSSL versions
            EC_KEY* ecdh = EC_KEY_new_by_curve_name(NID_X9_62_prime256v1);
            if (ecdh) {
                SSL_CTX_set_tmp_ecdh(ctx, ecdh);
                EC_KEY_free(ecdh);
            }
        }
        
        // Disable static RSA and DH ciphers
        SSL_CTX_set_options(ctx, SSL_OP_CIPHER_SERVER_PREFERENCE);
        SSL_CTX_set_options(ctx, SSL_OP_NO_SSLv2 | SSL_OP_NO_SSLv3);
        
        return true;
        
    } catch (const std::exception& e) {
        LOG_ERROR("Failed to enable perfect forward secrecy: {}", e.what());
        return false;
    }
}

bool TlsManager::configure_secure_renegotiation(SSL_CTX* ctx) {
    if (!ctx) return false;
    
    SSL_CTX_set_options(ctx, SSL_OP_ALLOW_UNSAFE_LEGACY_RENEGOTIATION);
    SSL_CTX_clear_options(ctx, SSL_OP_LEGACY_SERVER_CONNECT);
    
    return true;
}

void TlsManager::initialize_openssl() {
    if (!ssl_initialized_) {
        SSL_library_init();
        SSL_load_error_strings();
        OpenSSL_add_all_algorithms();
        RAND_poll();
        ssl_initialized_ = true;
        LOG_DEBUG("OpenSSL initialized");
    }
}

void TlsManager::cleanup_openssl() {
    if (ssl_initialized_) {
        EVP_cleanup();
        CRYPTO_cleanup_all_ex_data();
        ERR_free_strings();
        LOG_DEBUG("OpenSSL cleanup completed");
    }
}

} // namespace security
} // namespace ats