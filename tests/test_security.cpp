#include <gtest/gtest.h>
#include <gmock/gmock.h>
#include "../security/include/crypto_manager.hpp"
#include "../security/include/auth_manager.hpp"
#include "../security/include/tls_manager.hpp"
#include "../security/include/totp_manager.hpp"
#include "../security/include/rbac_manager.hpp"
#include <filesystem>
#include <thread>
#include <chrono>

using namespace ats::security;

class SecurityTestFixture : public ::testing::Test {
protected:
    void SetUp() override {
        test_dir_ = "./test_security_data/";
        std::filesystem::create_directories(test_dir_);
        
        crypto_manager_ = std::make_shared<CryptoManager>();
        ASSERT_TRUE(crypto_manager_->initialize(test_dir_ + "keys/"));
        
        auth_manager_ = std::make_shared<AuthManager>();
        ASSERT_TRUE(auth_manager_->initialize(crypto_manager_));
        
        tls_manager_ = std::make_shared<TlsManager>();
        ASSERT_TRUE(tls_manager_->initialize(test_dir_ + "certs/"));
        
        totp_manager_ = std::make_shared<TotpManager>();
        ASSERT_TRUE(totp_manager_->initialize(crypto_manager_));
        
        rbac_manager_ = std::make_shared<RbacManager>();
        ASSERT_TRUE(rbac_manager_->initialize(crypto_manager_));
    }
    
    void TearDown() override {
        crypto_manager_->shutdown();
        tls_manager_->shutdown();
        
        // Clean up test directory
        std::filesystem::remove_all(test_dir_);
    }
    
    std::string test_dir_;
    std::shared_ptr<CryptoManager> crypto_manager_;
    std::shared_ptr<AuthManager> auth_manager_;
    std::shared_ptr<TlsManager> tls_manager_;
    std::shared_ptr<TotpManager> totp_manager_;
    std::shared_ptr<RbacManager> rbac_manager_;
};

// CryptoManager Tests
class CryptoManagerTest : public SecurityTestFixture {};

TEST_F(CryptoManagerTest, AES256EncryptionDecryption) {
    std::string plaintext = "This is a secret message for testing AES-256-GCM encryption!";
    std::string key_id = "test_key";
    
    // Encrypt the plaintext
    auto encrypted = crypto_manager_->encrypt_aes256_gcm(plaintext, key_id);
    ASSERT_TRUE(encrypted.success);
    ASSERT_FALSE(encrypted.encrypted_data.empty());
    ASSERT_EQ(encrypted.iv.size(), 12); // GCM IV size
    ASSERT_EQ(encrypted.tag.size(), 16); // GCM tag size
    
    // Decrypt the ciphertext
    std::string decrypted = crypto_manager_->decrypt_aes256_gcm(
        encrypted.encrypted_data, encrypted.iv, encrypted.tag, key_id);
    
    ASSERT_EQ(plaintext, decrypted);
}

TEST_F(CryptoManagerTest, HMACGeneration) {
    std::string data = "test data for HMAC";
    std::string key = "secret_key_123";
    
    // Generate HMAC-SHA256
    std::string hmac256 = crypto_manager_->generate_hmac_sha256(data, key);
    ASSERT_FALSE(hmac256.empty());
    ASSERT_TRUE(crypto_manager_->verify_hmac_sha256(data, key, hmac256));
    
    // Generate HMAC-SHA512
    std::string hmac512 = crypto_manager_->generate_hmac_sha512(data, key);
    ASSERT_FALSE(hmac512.empty());
    ASSERT_TRUE(crypto_manager_->verify_hmac_sha512(data, key, hmac512));
    
    // Verify wrong HMAC fails
    ASSERT_FALSE(crypto_manager_->verify_hmac_sha256(data, "wrong_key", hmac256));
}

TEST_F(CryptoManagerTest, APIKeyStorage) {
    std::string exchange = "binance";
    std::string api_key = "test_api_key_123";
    std::string secret_key = "test_secret_key_456";
    std::string passphrase = "test_passphrase";
    
    // Store encrypted API credentials
    ASSERT_TRUE(crypto_manager_->store_encrypted_api_key(exchange, api_key, secret_key, passphrase));
    
    // Retrieve and verify credentials
    auto credentials = crypto_manager_->retrieve_api_credentials(exchange);
    ASSERT_TRUE(credentials.valid);
    ASSERT_EQ(credentials.api_key, api_key);
    ASSERT_EQ(credentials.secret_key, secret_key);
    ASSERT_EQ(credentials.passphrase, passphrase);
    
    // List stored exchanges
    auto exchanges = crypto_manager_->list_stored_exchanges();
    ASSERT_EQ(exchanges.size(), 1);
    ASSERT_EQ(exchanges[0], exchange);
    
    // Delete credentials
    ASSERT_TRUE(crypto_manager_->delete_api_credentials(exchange));
    auto deleted_creds = crypto_manager_->retrieve_api_credentials(exchange);
    ASSERT_FALSE(deleted_creds.valid);
}

TEST_F(CryptoManagerTest, RandomGeneration) {
    // Test random key generation
    auto key1 = crypto_manager_->generate_random_key(32);
    auto key2 = crypto_manager_->generate_random_key(32);
    ASSERT_EQ(key1.size(), 32);
    ASSERT_EQ(key2.size(), 32);
    ASSERT_NE(key1, key2); // Should be different
    
    // Test random string generation
    std::string str1 = crypto_manager_->generate_random_string(16);
    std::string str2 = crypto_manager_->generate_random_string(16);
    ASSERT_EQ(str1.size(), 16);
    ASSERT_EQ(str2.size(), 16);
    ASSERT_NE(str1, str2); // Should be different
}

// TotpManager Tests
class TotpManagerTest : public SecurityTestFixture {};

TEST_F(TotpManagerTest, SecretGeneration) {
    std::string user_id = "test_user";
    std::string issuer = "ATS Test";
    
    auto secret = totp_manager_->generate_totp_secret(user_id, issuer);
    ASSERT_FALSE(secret.secret_key.empty());
    ASSERT_FALSE(secret.qr_code_url.empty());
    ASSERT_FALSE(secret.is_active);
    
    // Check backup codes
    for (int i = 0; i < 10; i++) {
        ASSERT_FALSE(secret.backup_codes[i].empty());
    }
    
    // Store the secret
    ASSERT_TRUE(totp_manager_->store_totp_secret(user_id, secret));
    
    // Retrieve the secret
    auto retrieved = totp_manager_->get_totp_secret(user_id);
    ASSERT_EQ(retrieved.secret_key, secret.secret_key);
}

TEST_F(TotpManagerTest, TOTPCodeGeneration) {
    std::string user_id = "test_user";
    auto secret = totp_manager_->generate_totp_secret(user_id);
    ASSERT_TRUE(totp_manager_->store_totp_secret(user_id, secret));
    ASSERT_TRUE(totp_manager_->enable_2fa_for_user(user_id));
    
    // Generate TOTP code
    auto now = std::chrono::system_clock::now();
    std::string code = totp_manager_->generate_totp_code(secret.secret_key, now);
    ASSERT_EQ(code.length(), 6);
    ASSERT_TRUE(std::all_of(code.begin(), code.end(), ::isdigit));
    
    // Verify the code
    ASSERT_TRUE(totp_manager_->verify_totp_code(user_id, code));
    
    // Test with wrong code
    ASSERT_FALSE(totp_manager_->verify_totp_code(user_id, "000000"));
}

TEST_F(TotpManagerTest, TOTPTimeDrift) {
    std::string user_id = "test_user";
    auto secret = totp_manager_->generate_totp_secret(user_id);
    ASSERT_TRUE(totp_manager_->store_totp_secret(user_id, secret));
    ASSERT_TRUE(totp_manager_->enable_2fa_for_user(user_id));
    
    auto now = std::chrono::system_clock::now();
    
    // Generate code for 30 seconds ago (should still work with tolerance)
    auto past_time = now - std::chrono::seconds(30);
    std::string past_code = totp_manager_->generate_totp_code(secret.secret_key, past_time);
    ASSERT_TRUE(totp_manager_->verify_totp_code(user_id, past_code, 30, 1));
    
    // Generate code for 30 seconds in the future (should still work with tolerance)
    auto future_time = now + std::chrono::seconds(30);
    std::string future_code = totp_manager_->generate_totp_code(secret.secret_key, future_time);
    ASSERT_TRUE(totp_manager_->verify_totp_code(user_id, future_code, 30, 1));
}

TEST_F(TotpManagerTest, FailedAttemptLocking) {
    std::string user_id = "test_user";
    auto secret = totp_manager_->generate_totp_secret(user_id);
    ASSERT_TRUE(totp_manager_->store_totp_secret(user_id, secret));
    ASSERT_TRUE(totp_manager_->enable_2fa_for_user(user_id));
    
    // Make multiple failed attempts
    for (int i = 0; i < 5; i++) {
        ASSERT_FALSE(totp_manager_->verify_totp_code(user_id, "000000"));
    }
    
    // User should be locked now
    ASSERT_TRUE(totp_manager_->is_user_2fa_locked(user_id));
    
    // Even correct code should fail when locked
    std::string correct_code = totp_manager_->generate_totp_code(secret.secret_key);
    ASSERT_FALSE(totp_manager_->verify_totp_code(user_id, correct_code));
    
    // Unlock user
    totp_manager_->unlock_user_2fa(user_id);
    ASSERT_FALSE(totp_manager_->is_user_2fa_locked(user_id));
    
    // Now correct code should work
    ASSERT_TRUE(totp_manager_->verify_totp_code(user_id, correct_code));
}

// RbacManager Tests
class RbacManagerTest : public SecurityTestFixture {};

TEST_F(RbacManagerTest, PermissionManagement) {
    Permission perm;
    perm.permission_id = "test_perm";
    perm.name = "Test Permission";
    perm.description = "A test permission";
    perm.resource_type = "test";
    perm.action = "read";
    perm.scope = "global";
    perm.created_at = std::chrono::system_clock::now();
    perm.created_by = "test";
    
    // Create permission
    ASSERT_TRUE(rbac_manager_->create_permission(perm));
    
    // Retrieve permission
    auto retrieved = rbac_manager_->get_permission(perm.permission_id);
    ASSERT_EQ(retrieved.permission_id, perm.permission_id);
    ASSERT_EQ(retrieved.name, perm.name);
    
    // List permissions
    auto permissions = rbac_manager_->list_permissions();
    ASSERT_GT(permissions.size(), 0); // Should have at least our test permission + defaults
}

TEST_F(RbacManagerTest, RoleManagement) {
    // First create a permission
    Permission perm;
    perm.permission_id = "test_perm";
    perm.name = "Test Permission";
    perm.resource_type = "test";
    perm.action = "read";
    perm.scope = "global";
    perm.created_at = std::chrono::system_clock::now();
    perm.created_by = "test";
    ASSERT_TRUE(rbac_manager_->create_permission(perm));
    
    // Create role
    Role role;
    role.role_id = "test_role";
    role.name = "Test Role";
    role.description = "A test role";
    role.permission_ids = {perm.permission_id};
    role.created_at = std::chrono::system_clock::now();
    role.created_by = "test";
    
    ASSERT_TRUE(rbac_manager_->create_role(role));
    
    // Retrieve role
    auto retrieved = rbac_manager_->get_role(role.role_id);
    ASSERT_EQ(retrieved.role_id, role.role_id);
    ASSERT_EQ(retrieved.name, role.name);
    ASSERT_EQ(retrieved.permission_ids.size(), 1);
    
    // Get role permissions
    auto role_perms = rbac_manager_->get_role_permissions(role.role_id);
    ASSERT_EQ(role_perms.size(), 1);
    ASSERT_EQ(role_perms[0].permission_id, perm.permission_id);
}

TEST_F(RbacManagerTest, UserManagement) {
    // Create user
    User user;
    user.user_id = "test_user";
    user.username = "testuser";
    user.email = "test@example.com";
    user.full_name = "Test User";
    user.created_at = std::chrono::system_clock::now();
    user.created_by = "test";
    
    ASSERT_TRUE(rbac_manager_->create_user(user));
    
    // Retrieve user
    auto retrieved = rbac_manager_->get_user(user.user_id);
    ASSERT_EQ(retrieved.user_id, user.user_id);
    ASSERT_EQ(retrieved.username, user.username);
    
    // Get user by username
    auto by_username = rbac_manager_->get_user_by_username(user.username);
    ASSERT_EQ(by_username.user_id, user.user_id);
    
    // Deactivate user
    ASSERT_TRUE(rbac_manager_->deactivate_user(user.user_id));
    auto deactivated = rbac_manager_->get_user(user.user_id);
    ASSERT_FALSE(deactivated.is_active);
    
    // Reactivate user
    ASSERT_TRUE(rbac_manager_->activate_user(user.user_id));
    auto reactivated = rbac_manager_->get_user(user.user_id);
    ASSERT_TRUE(reactivated.is_active);
}

TEST_F(RbacManagerTest, AccessControl) {
    // Create permission, role, and user
    Permission perm;
    perm.permission_id = "test_access_perm";
    perm.name = "Test Access Permission";
    perm.resource_type = "test";
    perm.action = "read";
    perm.scope = "global";
    perm.created_at = std::chrono::system_clock::now();
    perm.created_by = "test";
    ASSERT_TRUE(rbac_manager_->create_permission(perm));
    
    Role role;
    role.role_id = "test_access_role";
    role.name = "Test Access Role";
    role.permission_ids = {perm.permission_id};
    role.created_at = std::chrono::system_clock::now();
    role.created_by = "test";
    ASSERT_TRUE(rbac_manager_->create_role(role));
    
    User user;
    user.user_id = "test_access_user";
    user.username = "testaccessuser";
    user.role_ids = {role.role_id};
    user.created_at = std::chrono::system_clock::now();
    user.created_by = "test";
    ASSERT_TRUE(rbac_manager_->create_user(user));
    
    // Test access control
    ASSERT_TRUE(rbac_manager_->user_has_permission(user.user_id, perm.permission_id));
    ASSERT_TRUE(rbac_manager_->user_has_role(user.user_id, role.role_id));
    
    // Test with wrong permission
    ASSERT_FALSE(rbac_manager_->user_has_permission(user.user_id, "nonexistent_perm"));
    
    // Test access context
    RbacManager::AccessContext context;
    context.user_id = user.user_id;
    context.resource_type = "test";
    context.action = "read";
    context.timestamp = std::chrono::system_clock::now();
    
    ASSERT_TRUE(rbac_manager_->check_access(context));
    
    // Test with wrong action
    context.action = "write";
    ASSERT_FALSE(rbac_manager_->check_access(context));
}

TEST_F(RbacManagerTest, SessionManagement) {
    // Create user first
    User user;
    user.user_id = "session_test_user";
    user.username = "sessionuser";
    user.created_at = std::chrono::system_clock::now();
    user.created_by = "test";
    ASSERT_TRUE(rbac_manager_->create_user(user));
    
    // Create session
    std::string session_id = rbac_manager_->create_user_session(
        user.user_id, "127.0.0.1", "Test-Agent", std::chrono::seconds(3600));
    
    ASSERT_FALSE(session_id.empty());
    
    // Validate session
    ASSERT_TRUE(rbac_manager_->validate_user_session(session_id));
    
    // Get session info
    auto session = rbac_manager_->get_user_session(session_id);
    ASSERT_EQ(session.user_id, user.user_id);
    ASSERT_EQ(session.client_ip, "127.0.0.1");
    ASSERT_TRUE(session.is_active);
    
    // Update session activity
    ASSERT_TRUE(rbac_manager_->update_session_activity(session_id));
    
    // Terminate session
    ASSERT_TRUE(rbac_manager_->terminate_user_session(session_id));
    ASSERT_FALSE(rbac_manager_->validate_user_session(session_id));
}

// TLS Manager Tests
class TlsManagerTest : public SecurityTestFixture {};

TEST_F(TlsManagerTest, SelfSignedCertificateGeneration) {
    TlsManager::CertificateRequest request;
    request.common_name = "test.ats.local";
    request.organization = "ATS Test";
    request.country = "US";
    request.validity_days = 365;
    request.key_size = 2048;
    
    auto cert_info = tls_manager_->generate_self_signed_certificate(request);
    ASSERT_TRUE(cert_info.is_valid);
    ASSERT_FALSE(cert_info.cert_pem.empty());
    ASSERT_FALSE(cert_info.private_key_pem.empty());
    ASSERT_FALSE(cert_info.cert_fingerprint.empty());
}

TEST_F(TlsManagerTest, CertificateStorage) {
    // Generate certificate
    TlsManager::CertificateRequest request;
    request.common_name = "storage.test.local";
    request.organization = "ATS Test";
    request.country = "US";
    
    auto cert_info = tls_manager_->generate_self_signed_certificate(request);
    ASSERT_TRUE(cert_info.is_valid);
    
    // Save certificate
    std::string cert_id = "test_cert";
    ASSERT_TRUE(tls_manager_->save_certificate(cert_id, cert_info));
    
    // Retrieve certificate
    auto retrieved = tls_manager_->get_certificate(cert_id);
    ASSERT_TRUE(retrieved.is_valid);
    ASSERT_EQ(retrieved.cert_pem, cert_info.cert_pem);
    
    // List certificates
    auto certs = tls_manager_->list_certificates();
    ASSERT_GT(certs.size(), 0);
    
    // Delete certificate
    ASSERT_TRUE(tls_manager_->delete_certificate(cert_id));
    auto deleted = tls_manager_->get_certificate(cert_id);
    ASSERT_FALSE(deleted.is_valid);
}

// Integration Tests
class SecurityIntegrationTest : public SecurityTestFixture {};

TEST_F(SecurityIntegrationTest, Complete2FAWorkflow) {
    // 1. Create user in RBAC
    User user;
    user.user_id = "integration_user";
    user.username = "integrationuser";
    user.email = "integration@test.com";
    user.role_ids = {"role_trader"}; // Use default trader role
    user.created_at = std::chrono::system_clock::now();
    user.created_by = "test";
    ASSERT_TRUE(rbac_manager_->create_user(user));
    
    // 2. Set up 2FA for user
    auto secret = totp_manager_->generate_totp_secret(user.user_id);
    ASSERT_TRUE(totp_manager_->store_totp_secret(user.user_id, secret));
    ASSERT_TRUE(totp_manager_->enable_2fa_for_user(user.user_id));
    
    // 3. Create session (simulating login with 2FA)
    std::string session_id = rbac_manager_->create_user_session(
        user.user_id, "127.0.0.1", "Integration-Test");
    ASSERT_FALSE(session_id.empty());
    
    // 4. Test trading permissions through RBAC middleware
    RbacMiddleware middleware(rbac_manager_);
    
    // Should have view permissions (from trader role)
    ASSERT_TRUE(middleware.check_user_permission(session_id, "perm_trading_view"));
    
    // Should have place order permissions (from trader role)
    ASSERT_TRUE(middleware.check_user_permission(session_id, "perm_trading_place_order"));
    
    // Should NOT have admin permissions
    ASSERT_FALSE(middleware.check_user_permission(session_id, "perm_admin_user_management"));
    
    // 5. Test trading action authorization
    ASSERT_TRUE(middleware.authorize_trading_action(session_id, "place_order", "binance", "BTCUSDT"));
    ASSERT_TRUE(middleware.authorize_trading_action(session_id, "cancel_order", "binance", "BTCUSDT"));
    ASSERT_FALSE(middleware.authorize_admin_action(session_id, "create_user"));
}

TEST_F(SecurityIntegrationTest, EncryptedAPIKeyWithTradingAuth) {
    // 1. Store encrypted API key
    std::string exchange = "binance";
    std::string api_key = "test_binance_key";
    std::string secret_key = "test_binance_secret";
    ASSERT_TRUE(crypto_manager_->store_encrypted_api_key(exchange, api_key, secret_key));
    
    // 2. Create user with trading permissions
    User user;
    user.user_id = "trading_user";
    user.username = "tradinguser";
    user.role_ids = {"role_trader"};
    user.created_at = std::chrono::system_clock::now();
    user.created_by = "test";
    ASSERT_TRUE(rbac_manager_->create_user(user));
    
    // 3. Create session
    std::string session_id = rbac_manager_->create_user_session(user.user_id, "127.0.0.1", "Test");
    
    // 4. Verify user can access API keys (simulated through permissions)
    RbacMiddleware middleware(rbac_manager_);
    ASSERT_TRUE(middleware.check_user_permission(session_id, "perm_trading_place_order"));
    
    // 5. Retrieve and verify encrypted API key
    auto credentials = crypto_manager_->retrieve_api_credentials(exchange);
    ASSERT_TRUE(credentials.valid);
    ASSERT_EQ(credentials.api_key, api_key);
    ASSERT_EQ(credentials.secret_key, secret_key);
    
    // 6. Generate HMAC signature (simulating exchange request signing)
    std::string test_data = "timestamp=1234567890&symbol=BTCUSDT&side=BUY";
    std::string signature = crypto_manager_->generate_hmac_sha256(test_data, credentials.secret_key);
    ASSERT_FALSE(signature.empty());
    ASSERT_TRUE(crypto_manager_->verify_hmac_sha256(test_data, credentials.secret_key, signature));
}

// Performance Tests
class SecurityPerformanceTest : public SecurityTestFixture {};

TEST_F(SecurityPerformanceTest, CryptoOperationsPerformance) {
    const int iterations = 1000;
    std::string plaintext = "Performance test data for encryption/decryption benchmarking";
    
    auto start = std::chrono::high_resolution_clock::now();
    
    for (int i = 0; i < iterations; i++) {
        auto encrypted = crypto_manager_->encrypt_aes256_gcm(plaintext);
        ASSERT_TRUE(encrypted.success);
        
        std::string decrypted = crypto_manager_->decrypt_aes256_gcm(
            encrypted.encrypted_data, encrypted.iv, encrypted.tag);
        ASSERT_EQ(decrypted, plaintext);
    }
    
    auto end = std::chrono::high_resolution_clock::now();
    auto duration = std::chrono::duration_cast<std::chrono::milliseconds>(end - start);
    
    // Performance assertion: Should complete 1000 operations in less than 5 seconds
    ASSERT_LT(duration.count(), 5000);
    
    std::cout << "Crypto operations performance: " << iterations 
              << " encrypt/decrypt cycles in " << duration.count() << "ms" << std::endl;
}

TEST_F(SecurityPerformanceTest, TOTPVerificationPerformance) {
    const int iterations = 100;
    
    // Set up user with 2FA
    std::string user_id = "perf_user";
    auto secret = totp_manager_->generate_totp_secret(user_id);
    ASSERT_TRUE(totp_manager_->store_totp_secret(user_id, secret));
    ASSERT_TRUE(totp_manager_->enable_2fa_for_user(user_id));
    
    auto start = std::chrono::high_resolution_clock::now();
    
    for (int i = 0; i < iterations; i++) {
        std::string code = totp_manager_->generate_totp_code(secret.secret_key);
        ASSERT_TRUE(totp_manager_->verify_totp_code(user_id, code));
    }
    
    auto end = std::chrono::high_resolution_clock::now();
    auto duration = std::chrono::duration_cast<std::chrono::milliseconds>(end - start);
    
    // Performance assertion: Should complete 100 TOTP verifications in less than 1 second
    ASSERT_LT(duration.count(), 1000);
    
    std::cout << "TOTP verification performance: " << iterations 
              << " verifications in " << duration.count() << "ms" << std::endl;
}

// Main test runner
int main(int argc, char** argv) {
    ::testing::InitGoogleTest(&argc, argv);
    return RUN_ALL_TESTS();
}