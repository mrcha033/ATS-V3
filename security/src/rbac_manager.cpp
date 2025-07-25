#include "../include/rbac_manager.hpp"
#include "../../utils/logger.hpp"
#include <fstream>
#include <sstream>
#include <algorithm>
#include <filesystem>
#include <openssl/sha.h>
#include <random>

namespace ats {
namespace security {

RbacManager::RbacManager() {
    storage_path_ = "./security/rbac/";
}

RbacManager::~RbacManager() = default;

bool RbacManager::initialize(std::shared_ptr<CryptoManager> crypto_manager) {
    if (!crypto_manager) {
        LOG_ERROR("CryptoManager is null");
        return false;
    }
    
    crypto_manager_ = crypto_manager;
    
    try {
        // Create storage directory
        std::filesystem::create_directories(storage_path_);
        
        // Load existing data
        load_permissions_from_file();
        load_roles_from_file();
        load_users_from_file();
        load_sessions_from_file();
        
        // Initialize default system roles and permissions if empty
        if (permissions_cache_.empty() || roles_cache_.empty()) {
            initialize_default_roles_and_permissions();
        }
        
        LOG_INFO("RbacManager initialized successfully");
        return true;
        
    } catch (const std::exception& e) {
        LOG_ERROR("Failed to initialize RbacManager: {}", e.what());
        return false;
    }
}

bool RbacManager::initialize_default_roles_and_permissions() {
    try {
        // Create default permissions
        std::vector<Permission> default_permissions = {
            // Trading permissions
            {"perm_trading_view", "View Trading", "View trading data and positions", "trading", "read", "global", true, std::chrono::system_clock::now(), "system"},
            {"perm_trading_place_order", "Place Orders", "Place trading orders", "trading", "execute", "exchange", true, std::chrono::system_clock::now(), "system"},
            {"perm_trading_cancel_order", "Cancel Orders", "Cancel existing orders", "trading", "execute", "exchange", true, std::chrono::system_clock::now(), "system"},
            {"perm_trading_modify_order", "Modify Orders", "Modify existing orders", "trading", "write", "exchange", true, std::chrono::system_clock::now(), "system"},
            
            // Data permissions
            {"perm_data_read_prices", "Read Price Data", "Access price and market data", "data", "read", "global", true, std::chrono::system_clock::now(), "system"},
            {"perm_data_read_accounts", "Read Account Data", "Access account information", "data", "read", "account", true, std::chrono::system_clock::now(), "system"},
            {"perm_data_write_config", "Write Configuration", "Modify system configuration", "data", "write", "global", true, std::chrono::system_clock::now(), "system"},
            
            // Admin permissions
            {"perm_admin_user_management", "User Management", "Create and manage users", "admin", "write", "global", true, std::chrono::system_clock::now(), "system"},
            {"perm_admin_role_management", "Role Management", "Create and manage roles", "admin", "write", "global", true, std::chrono::system_clock::now(), "system"},
            {"perm_admin_system_config", "System Configuration", "Modify system settings", "admin", "write", "global", true, std::chrono::system_clock::now(), "system"},
            {"perm_admin_security_audit", "Security Audit", "View security logs and audit information", "admin", "read", "global", true, std::chrono::system_clock::now(), "system"},
            
            // API permissions
            {"perm_api_read", "API Read Access", "Read access to API endpoints", "api", "read", "global", true, std::chrono::system_clock::now(), "system"},
            {"perm_api_write", "API Write Access", "Write access to API endpoints", "api", "write", "global", true, std::chrono::system_clock::now(), "system"},
            {"perm_api_admin", "API Admin Access", "Administrative access to API", "api", "execute", "global", true, std::chrono::system_clock::now(), "system"}
        };
        
        for (const auto& perm : default_permissions) {
            create_permission(perm);
        }
        
        // Create default roles
        std::vector<Role> default_roles = {
            {
                "role_trader", "Trader", "Basic trading user with limited permissions",
                {"perm_trading_view", "perm_trading_place_order", "perm_trading_cancel_order", 
                 "perm_data_read_prices", "perm_data_read_accounts", "perm_api_read"},
                {}, true, true, std::chrono::system_clock::now(), "system"
            },
            {
                "role_senior_trader", "Senior Trader", "Advanced trading user with order modification rights",
                {"perm_trading_view", "perm_trading_place_order", "perm_trading_cancel_order", "perm_trading_modify_order",
                 "perm_data_read_prices", "perm_data_read_accounts", "perm_api_read", "perm_api_write"},
                {}, true, true, std::chrono::system_clock::now(), "system"
            },
            {
                "role_risk_manager", "Risk Manager", "Risk management and monitoring role",
                {"perm_trading_view", "perm_trading_cancel_order", "perm_data_read_prices", 
                 "perm_data_read_accounts", "perm_data_write_config", "perm_api_read"},
                {}, true, true, std::chrono::system_clock::now(), "system"
            },
            {
                "role_admin", "Administrator", "Full administrative access",
                {"perm_trading_view", "perm_trading_place_order", "perm_trading_cancel_order", "perm_trading_modify_order",
                 "perm_data_read_prices", "perm_data_read_accounts", "perm_data_write_config",
                 "perm_admin_user_management", "perm_admin_role_management", "perm_admin_system_config", "perm_admin_security_audit",
                 "perm_api_read", "perm_api_write", "perm_api_admin"},
                {}, true, true, std::chrono::system_clock::now(), "system"
            },
            {
                "role_viewer", "Viewer", "Read-only access to system data",
                {"perm_trading_view", "perm_data_read_prices", "perm_api_read"},
                {}, true, true, std::chrono::system_clock::now(), "system"
            }
        };
        
        for (const auto& role : default_roles) {
            create_role(role);
        }
        
        // Create default admin user if no users exist
        if (users_cache_.empty()) {
            User admin_user;
            admin_user.user_id = "user_admin";
            admin_user.username = "admin";
            admin_user.email = "admin@ats.local";
            admin_user.full_name = "System Administrator";
            admin_user.role_ids = {"role_admin"};
            admin_user.is_active = true;
            admin_user.is_system_user = true;
            admin_user.created_at = std::chrono::system_clock::now();
            admin_user.created_by = "system";
            admin_user.password_hash = hash_password("admin123", generate_salt());
            
            create_user(admin_user);
            LOG_INFO("Created default admin user (username: admin, password: admin123)");
        }
        
        LOG_INFO("Initialized default RBAC roles and permissions");
        return true;
        
    } catch (const std::exception& e) {
        LOG_ERROR("Failed to initialize default RBAC data: {}", e.what());
        return false;
    }
}

bool RbacManager::create_permission(const Permission& permission) {
    try {
        if (permissions_cache_.find(permission.permission_id) != permissions_cache_.end()) {
            LOG_WARNING("Permission already exists: {}", permission.permission_id);
            return false;
        }
        
        permissions_cache_[permission.permission_id] = permission;
        save_permissions_to_file();
        
        LOG_INFO("Created permission: {} ({})", permission.permission_id, permission.name);
        return true;
        
    } catch (const std::exception& e) {
        LOG_ERROR("Failed to create permission {}: {}", permission.permission_id, e.what());
        return false;
    }
}

bool RbacManager::create_role(const Role& role) {
    try {
        if (roles_cache_.find(role.role_id) != roles_cache_.end()) {
            LOG_WARNING("Role already exists: {}", role.role_id);
            return false;
        }
        
        // Validate that all permissions exist
        for (const auto& perm_id : role.permission_ids) {
            if (permissions_cache_.find(perm_id) == permissions_cache_.end()) {
                LOG_ERROR("Permission {} does not exist for role {}", perm_id, role.role_id);
                return false;
            }
        }
        
        roles_cache_[role.role_id] = role;
        save_roles_to_file();
        
        LOG_INFO("Created role: {} ({}) with {} permissions", 
                role.role_id, role.name, role.permission_ids.size());
        return true;
        
    } catch (const std::exception& e) {
        LOG_ERROR("Failed to create role {}: {}", role.role_id, e.what());
        return false;
    }
}

bool RbacManager::create_user(const User& user) {
    try {
        if (users_cache_.find(user.user_id) != users_cache_.end()) {
            LOG_WARNING("User already exists: {}", user.user_id);
            return false;
        }
        
        // Check for duplicate username
        for (const auto& [id, existing_user] : users_cache_) {
            if (existing_user.username == user.username) {
                LOG_ERROR("Username already exists: {}", user.username);
                return false;
            }
        }
        
        // Validate that all roles exist
        for (const auto& role_id : user.role_ids) {
            if (roles_cache_.find(role_id) == roles_cache_.end()) {
                LOG_ERROR("Role {} does not exist for user {}", role_id, user.user_id);
                return false;
            }
        }
        
        users_cache_[user.user_id] = user;
        save_users_to_file();
        
        LOG_INFO("Created user: {} ({}) with {} roles", 
                user.user_id, user.username, user.role_ids.size());
        return true;
        
    } catch (const std::exception& e) {
        LOG_ERROR("Failed to create user {}: {}", user.user_id, e.what());
        return false;
    }
}

bool RbacManager::user_has_permission(const std::string& user_id, const std::string& permission_id) {
    try {
        auto user_it = users_cache_.find(user_id);
        if (user_it == users_cache_.end() || !user_it->second.is_active) {
            return false;
        }
        
        const auto& user = user_it->second;
        
        // Check direct permissions
        if (std::find(user.direct_permission_ids.begin(), user.direct_permission_ids.end(), permission_id) 
            != user.direct_permission_ids.end()) {
            return true;
        }
        
        // Check role-based permissions
        for (const auto& role_id : user.role_ids) {
            auto role_it = roles_cache_.find(role_id);
            if (role_it != roles_cache_.end() && role_it->second.is_active) {
                const auto& role_perms = role_it->second.permission_ids;
                if (std::find(role_perms.begin(), role_perms.end(), permission_id) != role_perms.end()) {
                    return true;
                }
            }
        }
        
        return false;
        
    } catch (const std::exception& e) {
        LOG_ERROR("Error checking user permission for {} / {}: {}", user_id, permission_id, e.what());
        return false;
    }
}

bool RbacManager::check_access(const AccessContext& context) {
    try {
        // Basic user validation
        auto user_it = users_cache_.find(context.user_id);
        if (user_it == users_cache_.end() || !user_it->second.is_active) {
            log_access_attempt({
                generate_unique_id("log"), context.user_id, context.action,
                context.resource_type, context.resource_id, "",
                false, "User not found or inactive", context.client_ip, context.timestamp, {}
            });
            return false;
        }
        
        // Find appropriate permission based on resource type and action
        std::string required_permission;
        for (const auto& [perm_id, perm] : permissions_cache_) {
            if (perm.resource_type == context.resource_type && perm.action == context.action) {
                required_permission = perm_id;
                break;
            }
        }
        
        if (required_permission.empty()) {
            LOG_WARNING("No permission found for resource_type: {} action: {}", 
                       context.resource_type, context.action);
            return false;
        }
        
        bool access_granted = user_has_permission(context.user_id, required_permission);
        
        // Log access attempt
        log_access_attempt({
            generate_unique_id("log"), context.user_id, context.action,
            context.resource_type, context.resource_id, required_permission,
            access_granted, access_granted ? "Access granted" : "Access denied",
            context.client_ip, context.timestamp, context.attributes
        });
        
        return access_granted;
        
    } catch (const std::exception& e) {
        LOG_ERROR("Error checking access for user {}: {}", context.user_id, e.what());
        return false;
    }
}

std::string RbacManager::create_user_session(const std::string& user_id, 
                                            const std::string& client_ip,
                                            const std::string& user_agent,
                                            std::chrono::seconds ttl) {
    try {
        auto user_it = users_cache_.find(user_id);
        if (user_it == users_cache_.end() || !user_it->second.is_active) {
            LOG_ERROR("Cannot create session for invalid user: {}", user_id);
            return "";
        }
        
        UserSession session;
        session.session_id = generate_unique_id("sess");
        session.user_id = user_id;
        session.active_roles = user_it->second.role_ids;
        session.created_at = std::chrono::system_clock::now();
        session.last_activity = session.created_at;
        session.expires_at = session.created_at + ttl;
        session.client_ip = client_ip;
        session.user_agent = user_agent;
        session.is_active = true;
        
        sessions_cache_[session.session_id] = session;
        save_sessions_to_file();
        
        // Update user's last login time
        users_cache_[user_id].last_login = std::chrono::system_clock::now();
        save_users_to_file();
        
        LOG_INFO("Created session {} for user {}", session.session_id, user_id);
        return session.session_id;
        
    } catch (const std::exception& e) {
        LOG_ERROR("Failed to create session for user {}: {}", user_id, e.what());
        return "";
    }
}

bool RbacManager::validate_user_session(const std::string& session_id) {
    try {
        auto session_it = sessions_cache_.find(session_id);
        if (session_it == sessions_cache_.end()) {
            return false;
        }
        
        auto& session = session_it->second;
        auto now = std::chrono::system_clock::now();
        
        // Check if session is active and not expired
        if (!session.is_active || now >= session.expires_at) {
            if (session.is_active) {
                session.is_active = false;
                save_sessions_to_file();
                LOG_INFO("Session {} expired", session_id);
            }
            return false;
        }
        
        // Check if user is still active
        auto user_it = users_cache_.find(session.user_id);
        if (user_it == users_cache_.end() || !user_it->second.is_active) {
            session.is_active = false;
            save_sessions_to_file();
            LOG_INFO("Session {} invalidated - user inactive", session_id);
            return false;
        }
        
        return true;
        
    } catch (const std::exception& e) {
        LOG_ERROR("Error validating session {}: {}", session_id, e.what());
        return false;
    }
}

void RbacManager::log_access_attempt(const AccessLog& log) {
    try {
        // For now, just log to system logger
        // In production, this would go to a dedicated audit database
        LOG_INFO("ACCESS_LOG: user={} action={} resource={}:{} granted={} reason={} ip={}", 
                log.user_id, log.action, log.resource_type, log.resource_id, 
                log.access_granted, log.reason, log.client_ip);
                
    } catch (const std::exception& e) {
        LOG_ERROR("Failed to log access attempt: {}", e.what());
    }
}

// Private helper methods

std::string RbacManager::generate_unique_id(const std::string& prefix) {
    static std::random_device rd;
    static std::mt19937 gen(rd());
    static std::uniform_int_distribution<> dis(100000, 999999);
    
    auto now = std::chrono::system_clock::now();
    auto timestamp = std::chrono::duration_cast<std::chrono::seconds>(now.time_since_epoch()).count();
    
    return prefix + "_" + std::to_string(timestamp) + "_" + std::to_string(dis(gen));
}

std::string RbacManager::hash_password(const std::string& password, const std::string& salt) {
    std::string salted_password = password + salt;
    
    unsigned char hash[SHA256_DIGEST_LENGTH];
    SHA256(reinterpret_cast<const unsigned char*>(salted_password.c_str()), 
           salted_password.length(), hash);
    
    std::stringstream ss;
    for (int i = 0; i < SHA256_DIGEST_LENGTH; i++) {
        ss << std::hex << std::setw(2) << std::setfill('0') << static_cast<int>(hash[i]);
    }
    
    return salt + "$" + ss.str();
}

std::string RbacManager::generate_salt() {
    return crypto_manager_->generate_random_string(16);
}

bool RbacManager::save_permissions_to_file() {
    try {
        std::string file_path = storage_path_ + "permissions.dat";
        
        std::ostringstream data_stream;
        data_stream << permissions_cache_.size() << "\n";
        
        for (const auto& [id, perm] : permissions_cache_) {
            data_stream << perm.permission_id << "|"
                       << perm.name << "|"
                       << perm.description << "|"
                       << perm.resource_type << "|"
                       << perm.action << "|"
                       << perm.scope << "|"
                       << (perm.is_system_permission ? "1" : "0") << "|"
                       << std::chrono::duration_cast<std::chrono::seconds>(perm.created_at.time_since_epoch()).count() << "|"
                       << perm.created_by << "\n";
        }
        
        auto encrypted = crypto_manager_->encrypt_aes256_gcm(data_stream.str(), "rbac_permissions");
        if (!encrypted.success) {
            LOG_ERROR("Failed to encrypt permissions data");
            return false;
        }
        
        std::ofstream file(file_path, std::ios::binary);
        if (!file) {
            LOG_ERROR("Failed to open permissions file for writing: {}", file_path);
            return false;
        }
        
        file.write(reinterpret_cast<const char*>(encrypted.iv.data()), encrypted.iv.size());
        file.write(reinterpret_cast<const char*>(encrypted.tag.data()), encrypted.tag.size());
        file.write(reinterpret_cast<const char*>(encrypted.encrypted_data.data()), encrypted.encrypted_data.size());
        
        return true;
        
    } catch (const std::exception& e) {
        LOG_ERROR("Failed to save permissions to file: {}", e.what());
        return false;
    }
}

bool RbacManager::load_permissions_from_file() {
    try {
        std::string file_path = storage_path_ + "permissions.dat";
        
        if (!std::filesystem::exists(file_path)) {
            LOG_INFO("Permissions file does not exist, starting with empty permissions");
            return true;
        }
        
        std::ifstream file(file_path, std::ios::binary);
        if (!file) {
            LOG_ERROR("Failed to open permissions file for reading: {}", file_path);
            return false;
        }
        
        // Read encrypted data
        std::vector<uint8_t> iv(12);
        std::vector<uint8_t> tag(16);
        file.read(reinterpret_cast<char*>(iv.data()), iv.size());
        file.read(reinterpret_cast<char*>(tag.data()), tag.size());
        
        std::vector<uint8_t> encrypted_data;
        file.seekg(0, std::ios::end);
        size_t file_size = file.tellg();
        file.seekg(28, std::ios::beg);
        
        size_t data_size = file_size - 28;
        encrypted_data.resize(data_size);
        file.read(reinterpret_cast<char*>(encrypted_data.data()), data_size);
        
        // Decrypt data
        std::string decrypted = crypto_manager_->decrypt_aes256_gcm(encrypted_data, iv, tag, "rbac_permissions");
        if (decrypted.empty()) {
            LOG_ERROR("Failed to decrypt permissions data");
            return false;
        }
        
        // Parse permissions
        std::istringstream data_stream(decrypted);
        std::string line;
        std::getline(data_stream, line);
        size_t count = std::stoul(line);
        
        permissions_cache_.clear();
        
        for (size_t i = 0; i < count; i++) {
            std::getline(data_stream, line);
            std::istringstream line_stream(line);
            std::string token;
            
            Permission perm;
            std::getline(line_stream, perm.permission_id, '|');
            std::getline(line_stream, perm.name, '|');
            std::getline(line_stream, perm.description, '|');
            std::getline(line_stream, perm.resource_type, '|');
            std::getline(line_stream, perm.action, '|');
            std::getline(line_stream, perm.scope, '|');
            std::getline(line_stream, token, '|');
            perm.is_system_permission = (token == "1");
            std::getline(line_stream, token, '|');
            perm.created_at = std::chrono::system_clock::from_time_t(std::stoll(token));
            std::getline(line_stream, perm.created_by, '|');
            
            permissions_cache_[perm.permission_id] = perm;
        }
        
        LOG_INFO("Loaded {} permissions from file", permissions_cache_.size());
        return true;
        
    } catch (const std::exception& e) {
        LOG_ERROR("Failed to load permissions from file: {}", e.what());
        return false;
    }
}

bool RbacManager::save_roles_to_file() {
    // Similar implementation to save_permissions_to_file but for roles
    // Simplified for brevity - would include full role serialization
    return true;
}

bool RbacManager::load_roles_from_file() {
    // Similar implementation to load_permissions_from_file but for roles
    // Simplified for brevity - would include full role deserialization
    return true;
}

bool RbacManager::save_users_to_file() {
    // Similar implementation to save_permissions_to_file but for users
    // Simplified for brevity - would include full user serialization
    return true;
}

bool RbacManager::load_users_from_file() {
    // Similar implementation to load_permissions_from_file but for users
    // Simplified for brevity - would include full user deserialization
    return true;
}

bool RbacManager::save_sessions_to_file() {
    // Similar implementation to save_permissions_to_file but for sessions
    // Simplified for brevity - would include full session serialization
    return true;
}

bool RbacManager::load_sessions_from_file() {
    // Similar implementation to load_permissions_from_file but for sessions
    // Simplified for brevity - would include full session deserialization
    return true;
}

// RbacMiddleware Implementation

RbacMiddleware::RbacMiddleware(std::shared_ptr<RbacManager> rbac_manager) 
    : rbac_manager_(rbac_manager) {
    if (!rbac_manager_) {
        throw RbacException("RbacManager is null");
    }
}

bool RbacMiddleware::check_user_permission(const std::string& session_id,
                                          const std::string& required_permission,
                                          const std::string& resource_type,
                                          const std::string& resource_id) {
    try {
        if (!rbac_manager_->validate_user_session(session_id)) {
            LOG_WARNING("Invalid session for permission check: {}", session_id);
            return false;
        }
        
        std::string user_id = extract_user_id_from_session(session_id);
        if (user_id.empty()) {
            return false;
        }
        
        return rbac_manager_->user_has_permission(user_id, required_permission);
        
    } catch (const std::exception& e) {
        LOG_ERROR("Error checking user permission: {}", e.what());
        return false;
    }
}

bool RbacMiddleware::authorize_trading_action(const std::string& session_id,
                                             const std::string& action,
                                             const std::string& exchange,
                                             const std::string& symbol) {
    std::string required_permission;
    
    if (action == "place_order") {
        required_permission = "perm_trading_place_order";
    } else if (action == "cancel_order") {
        required_permission = "perm_trading_cancel_order";
    } else if (action == "modify_order") {
        required_permission = "perm_trading_modify_order";
    } else if (action == "view_positions") {
        required_permission = "perm_trading_view";
    } else {
        LOG_ERROR("Unknown trading action: {}", action);
        return false;
    }
    
    return check_user_permission(session_id, required_permission, "trading", exchange + ":" + symbol);
}

std::string RbacMiddleware::extract_user_id_from_session(const std::string& session_id) {
    auto session = rbac_manager_->get_user_session(session_id);
    return session.user_id;
}

RbacManager::AccessContext RbacMiddleware::create_access_context(const std::string& user_id,
                                                                const std::string& resource_type,
                                                                const std::string& action,
                                                                const std::unordered_map<std::string, std::string>& extra_attributes) {
    RbacManager::AccessContext context;
    context.user_id = user_id;
    context.resource_type = resource_type;
    context.action = action;
    context.timestamp = std::chrono::system_clock::now();
    context.attributes = extra_attributes;
    
    return context;
}

} // namespace security
} // namespace ats