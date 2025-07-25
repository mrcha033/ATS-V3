#pragma once

#include "crypto_manager.hpp"
#include <string>
#include <vector>
#include <unordered_map>
#include <unordered_set>
#include <memory>
#include <chrono>
#include <functional>

namespace ats {
namespace security {

// Role-Based Access Control (RBAC) Manager
class RbacManager {
public:
    RbacManager();
    ~RbacManager();
    
    bool initialize(std::shared_ptr<CryptoManager> crypto_manager);
    
    // Permission System
    struct Permission {
        std::string permission_id;
        std::string name;
        std::string description;
        std::string resource_type;    // "trading", "admin", "data", "api"
        std::string action;          // "read", "write", "execute", "delete"
        std::string scope;           // "global", "exchange", "strategy", "account"
        bool is_system_permission = false;
        std::chrono::system_clock::time_point created_at;
        std::string created_by;
    };
    
    bool create_permission(const Permission& permission);
    bool update_permission(const std::string& permission_id, const Permission& permission);
    bool delete_permission(const std::string& permission_id);
    Permission get_permission(const std::string& permission_id);
    std::vector<Permission> list_permissions(const std::string& resource_type = "");
    
    // Role System
    struct Role {
        std::string role_id;
        std::string name;
        std::string description;
        std::vector<std::string> permission_ids;
        std::unordered_map<std::string, std::string> attributes; // Custom role attributes
        bool is_system_role = false;
        bool is_active = true;
        std::chrono::system_clock::time_point created_at;
        std::string created_by;
    };
    
    bool create_role(const Role& role);
    bool update_role(const std::string& role_id, const Role& role);
    bool delete_role(const std::string& role_id);
    Role get_role(const std::string& role_id);
    std::vector<Role> list_roles();
    
    // Role-Permission Management
    bool assign_permission_to_role(const std::string& role_id, const std::string& permission_id);
    bool revoke_permission_from_role(const std::string& role_id, const std::string& permission_id);
    std::vector<Permission> get_role_permissions(const std::string& role_id);
    std::vector<Role> get_permission_roles(const std::string& permission_id);
    
    // User System
    struct User {
        std::string user_id;
        std::string username;
        std::string email;
        std::string full_name;
        std::vector<std::string> role_ids;
        std::vector<std::string> direct_permission_ids; // Direct permissions (not through roles)
        std::unordered_map<std::string, std::string> attributes;
        bool is_active = true;
        bool is_system_user = false;
        std::chrono::system_clock::time_point created_at;
        std::chrono::system_clock::time_point last_login;
        std::string created_by;
        std::string password_hash; // For local authentication
    };
    
    bool create_user(const User& user);
    bool update_user(const std::string& user_id, const User& user);
    bool delete_user(const std::string& user_id);
    bool activate_user(const std::string& user_id);
    bool deactivate_user(const std::string& user_id);
    User get_user(const std::string& user_id);
    User get_user_by_username(const std::string& username);
    std::vector<User> list_users(bool include_inactive = false);
    
    // User-Role Management
    bool assign_role_to_user(const std::string& user_id, const std::string& role_id);
    bool revoke_role_from_user(const std::string& user_id, const std::string& role_id);
    bool assign_direct_permission_to_user(const std::string& user_id, const std::string& permission_id);
    bool revoke_direct_permission_from_user(const std::string& user_id, const std::string& permission_id);
    
    // Access Control Queries
    bool user_has_permission(const std::string& user_id, const std::string& permission_id);
    bool user_has_role(const std::string& user_id, const std::string& role_id);
    std::vector<Permission> get_user_permissions(const std::string& user_id); // All permissions (role + direct)
    std::vector<Role> get_user_roles(const std::string& user_id);
    
    // Context-based Access Control
    struct AccessContext {
        std::string user_id;
        std::string resource_type;
        std::string resource_id;
        std::string action;
        std::string client_ip;
        std::unordered_map<std::string, std::string> attributes;
        std::chrono::system_clock::time_point timestamp;
    };
    
    bool check_access(const AccessContext& context);
    bool check_permission_with_context(const std::string& user_id, 
                                      const std::string& permission_id,
                                      const AccessContext& context);
    
    // Session Management
    struct UserSession {
        std::string session_id;
        std::string user_id;
        std::vector<std::string> active_roles;
        std::unordered_map<std::string, std::string> session_attributes;
        std::chrono::system_clock::time_point created_at;
        std::chrono::system_clock::time_point last_activity;
        std::chrono::system_clock::time_point expires_at;
        std::string client_ip;
        std::string user_agent;
        bool is_active = true;
    };
    
    std::string create_user_session(const std::string& user_id, 
                                   const std::string& client_ip,
                                   const std::string& user_agent,
                                   std::chrono::seconds ttl = std::chrono::seconds(28800));
    bool validate_user_session(const std::string& session_id);
    bool update_session_activity(const std::string& session_id);
    bool terminate_user_session(const std::string& session_id);
    UserSession get_user_session(const std::string& session_id);
    std::vector<UserSession> get_user_sessions(const std::string& user_id);
    
    // Audit and Logging
    struct AccessLog {
        std::string log_id;
        std::string user_id;
        std::string action;
        std::string resource_type;
        std::string resource_id;
        std::string permission_id;
        bool access_granted;
        std::string reason;
        std::string client_ip;
        std::chrono::system_clock::time_point timestamp;
        std::unordered_map<std::string, std::string> metadata;
    };
    
    void log_access_attempt(const AccessLog& log);
    std::vector<AccessLog> get_access_logs(const std::string& user_id = "",
                                          std::chrono::system_clock::time_point from = {},
                                          std::chrono::system_clock::time_point to = {},
                                          int limit = 100);
    
    // Security Policies
    struct SecurityPolicy {
        std::string policy_id;
        std::string name;
        std::string type; // "password", "session", "access", "rate_limit"
        std::unordered_map<std::string, std::string> rules;
        bool is_active = true;
        std::chrono::system_clock::time_point created_at;
    };
    
    bool create_security_policy(const SecurityPolicy& policy);
    bool update_security_policy(const std::string& policy_id, const SecurityPolicy& policy);
    bool delete_security_policy(const std::string& policy_id);
    SecurityPolicy get_security_policy(const std::string& policy_id);
    std::vector<SecurityPolicy> list_security_policies(const std::string& type = "");
    
    // Administration Functions
    bool initialize_default_roles_and_permissions();
    void cleanup_expired_sessions();
    void cleanup_old_access_logs(std::chrono::hours retention_period = std::chrono::hours(24 * 30));
    bool backup_rbac_data(const std::string& backup_file_path);
    bool restore_rbac_data(const std::string& backup_file_path);
    
    // Validation and Security
    bool validate_permission_hierarchy(const std::string& role_id);
    std::vector<std::string> detect_privilege_escalation_attempts();
    bool is_user_locked_out(const std::string& user_id);
    void lock_user_account(const std::string& user_id, std::chrono::minutes duration);
    
private:
    std::shared_ptr<CryptoManager> crypto_manager_;
    std::string storage_path_;
    
    // In-memory caches for performance
    std::unordered_map<std::string, Permission> permissions_cache_;
    std::unordered_map<std::string, Role> roles_cache_;
    std::unordered_map<std::string, User> users_cache_;
    std::unordered_map<std::string, UserSession> sessions_cache_;
    std::unordered_map<std::string, SecurityPolicy> policies_cache_;
    
    // Cache management
    void refresh_permissions_cache();
    void refresh_roles_cache();
    void refresh_users_cache();
    void clear_all_caches();
    
    // File operations
    bool save_permissions_to_file();
    bool load_permissions_from_file();
    bool save_roles_to_file();
    bool load_roles_from_file();
    bool save_users_to_file();
    bool load_users_from_file();
    bool save_sessions_to_file();
    bool load_sessions_from_file();
    
    // Utility functions
    std::string generate_unique_id(const std::string& prefix);
    std::string hash_password(const std::string& password, const std::string& salt);
    std::string generate_salt();
    bool verify_password(const std::string& password, const std::string& hash, const std::string& salt);
    
    // Access control helpers
    bool has_permission_recursive(const std::string& user_id, const std::string& permission_id);
    std::vector<std::string> get_effective_roles(const std::string& user_id);
    bool evaluate_access_policy(const AccessContext& context, const SecurityPolicy& policy);
    
    // Security validation
    bool is_system_permission(const std::string& permission_id);
    bool is_system_role(const std::string& role_id);
    bool can_user_modify_role(const std::string& user_id, const std::string& role_id);
    bool can_user_modify_user(const std::string& admin_user_id, const std::string& target_user_id);
    
    // Logging helpers
    void log_rbac_event(const std::string& event_type, const std::string& user_id, 
                       const std::string& details, const std::string& client_ip = "");
};

// RBAC Middleware for request processing
class RbacMiddleware {
public:
    using AccessCheckCallback = std::function<bool(const std::string& user_id, 
                                                  const std::string& permission,
                                                  const std::unordered_map<std::string, std::string>& context)>;
    
    RbacMiddleware(std::shared_ptr<RbacManager> rbac_manager);
    
    // Middleware functions
    bool check_user_permission(const std::string& session_id,
                              const std::string& required_permission,
                              const std::string& resource_type = "",
                              const std::string& resource_id = "");
    
    bool check_user_role(const std::string& session_id,
                        const std::string& required_role);
    
    bool authorize_request(const std::string& session_id,
                          const std::string& endpoint,
                          const std::string& method,
                          const std::unordered_map<std::string, std::string>& headers);
    
    // Trading-specific authorization
    bool authorize_trading_action(const std::string& session_id,
                                 const std::string& action, // "place_order", "cancel_order", etc.
                                 const std::string& exchange,
                                 const std::string& symbol = "");
    
    bool authorize_admin_action(const std::string& session_id,
                               const std::string& admin_action);
    
    // Data access authorization
    bool authorize_data_access(const std::string& session_id,
                              const std::string& data_type, // "price_data", "user_data", etc.
                              const std::string& access_level); // "read", "write"
    
private:
    std::shared_ptr<RbacManager> rbac_manager_;
    
    std::string extract_user_id_from_session(const std::string& session_id);
    RbacManager::AccessContext create_access_context(const std::string& user_id,
                                                     const std::string& resource_type,
                                                     const std::string& action,
                                                     const std::unordered_map<std::string, std::string>& extra_attributes = {});
};

// Exception classes for RBAC operations
class RbacException : public SecurityException {
public:
    explicit RbacException(const std::string& message) 
        : SecurityException("RBAC Error: " + message) {}
};

class AccessDeniedException : public RbacException {
public:
    explicit AccessDeniedException(const std::string& message) 
        : RbacException("Access Denied: " + message) {}
};

class UserNotFoundException : public RbacException {
public:
    explicit UserNotFoundException(const std::string& user_id) 
        : RbacException("User not found: " + user_id) {}
};

class RoleNotFoundException : public RbacException {
public:
    explicit RoleNotFoundException(const std::string& role_id) 
        : RbacException("Role not found: " + role_id) {}
};

} // namespace security
} // namespace ats