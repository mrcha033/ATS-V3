#pragma once

#include "exchange_plugin_interface.hpp"
#include <string>
#include <memory>
#include <vector>
#include <unordered_map>
#include <mutex>
#include <shared_mutex>
#include <thread>
#include <atomic>
#include <functional>

#ifdef _WIN32
    #define WIN32_LEAN_AND_MEAN
    #include <windows.h>
    #undef ERROR  // Undefine Windows ERROR macro to avoid conflicts
    using LibraryHandle = HMODULE;
#else
    #include <dlfcn.h>
    using LibraryHandle = void*;
#endif

namespace ats {
namespace exchange {

// Plugin status enumeration
enum class PluginStatus {
    UNLOADED,
    LOADED,
    INITIALIZED,
    RUNNING,
    STOPPED,
    ERROR
};

// Plugin event types
enum class PluginEvent {
    PLUGIN_LOADED,
    PLUGIN_UNLOADED,
    PLUGIN_STARTED,
    PLUGIN_STOPPED,
    PLUGIN_ERROR
};

// Plugin event callback
using PluginEventCallback = std::function<void(const std::string& plugin_id, PluginEvent event, const std::string& message)>;

// Plugin instance wrapper with status tracking
struct PluginInstance {
    std::unique_ptr<IExchangePlugin> plugin;
    PluginDescriptor descriptor;
    PluginStatus status;
    std::string last_error;
    std::chrono::system_clock::time_point status_changed_at;
    
    PluginInstance() : status(PluginStatus::UNLOADED) {}
};

class ExchangePluginManager {
public:
    static ExchangePluginManager& instance();
    
    ~ExchangePluginManager();
    
    // Plugin discovery and loading
    bool scan_plugin_directory(const std::string& directory_path);
    bool load_plugin(const std::string& plugin_path);
    bool load_plugin_from_memory(const std::string& plugin_id, CreatePluginFunction create_func, const ExchangePluginMetadata& metadata);
    bool unload_plugin(const std::string& plugin_id);
    void unload_all_plugins();
    
    // Plugin lifecycle management
    bool initialize_plugin(const std::string& plugin_id, const types::ExchangeConfig& config);
    bool start_plugin(const std::string& plugin_id);
    bool stop_plugin(const std::string& plugin_id);
    void stop_all_plugins();
    
    // Plugin access
    std::shared_ptr<IExchangePlugin> get_plugin(const std::string& plugin_id);
    std::vector<std::string> get_loaded_plugins() const;
    std::vector<std::string> get_running_plugins() const;
    std::vector<ExchangePluginMetadata> get_available_plugins() const;
    
    // Plugin information
    bool is_plugin_loaded(const std::string& plugin_id) const;
    bool is_plugin_running(const std::string& plugin_id) const;
    PluginStatus get_plugin_status(const std::string& plugin_id) const;
    ExchangePluginMetadata get_plugin_metadata(const std::string& plugin_id) const;
    std::string get_plugin_error(const std::string& plugin_id) const;
    
    // Plugin validation
    bool validate_plugin(const std::string& plugin_path);
    bool is_plugin_compatible(const std::string& plugin_path);
    std::string get_plugin_api_version(const std::string& plugin_path);
    
    // Event handling
    void set_event_callback(PluginEventCallback callback);
    void clear_event_callback();
    
    // Configuration
    void set_plugin_directory(const std::string& directory);
    std::string get_plugin_directory() const;
    void set_auto_scan_enabled(bool enabled);
    bool is_auto_scan_enabled() const;
    void set_scan_interval(std::chrono::seconds interval);
    
    // Hot reload support
    void enable_hot_reload();
    void disable_hot_reload();
    bool is_hot_reload_enabled() const;
    
    // Statistics and monitoring
    size_t get_total_plugins() const;
    size_t get_loaded_plugins_count() const;
    size_t get_running_plugins_count() const;
    std::chrono::system_clock::time_point get_last_scan_time() const;
    
    // Error handling
    std::vector<std::string> get_loading_errors() const;
    void clear_loading_errors();
    
private:
    ExchangePluginManager();
    ExchangePluginManager(const ExchangePluginManager&) = delete;
    ExchangePluginManager& operator=(const ExchangePluginManager&) = delete;
    
    // Internal plugin management
    bool load_plugin_library(const std::string& plugin_path, PluginDescriptor& descriptor);
    void unload_plugin_library(PluginDescriptor& descriptor);
    bool validate_plugin_functions(const PluginDescriptor& descriptor);
    void update_plugin_status(const std::string& plugin_id, PluginStatus status, const std::string& error = "");
    void notify_event(const std::string& plugin_id, PluginEvent event, const std::string& message = "");
    
    // File system monitoring
    void start_file_watcher();
    void stop_file_watcher();
    void scan_for_changes();
    void handle_file_change(const std::string& file_path);
    
    // Utility functions
    std::string get_library_extension() const;
    bool is_library_file(const std::string& filename) const;
    std::string normalize_plugin_id(const std::string& plugin_id) const;
    
    // Thread-safe access helpers
    std::shared_lock<std::shared_mutex> get_read_lock() const;
    std::unique_lock<std::shared_mutex> get_write_lock() const;
    
    // Member variables
    mutable std::shared_mutex plugins_mutex_;
    std::unordered_map<std::string, std::unique_ptr<PluginInstance>> plugins_;
    
    std::string plugin_directory_;
    std::atomic<bool> auto_scan_enabled_;
    std::atomic<bool> hot_reload_enabled_;
    std::chrono::seconds scan_interval_;
    std::chrono::system_clock::time_point last_scan_time_;
    
    PluginEventCallback event_callback_;
    std::mutex event_callback_mutex_;
    
    std::vector<std::string> loading_errors_;
    std::mutex errors_mutex_;
    
    // File watcher thread
    std::unique_ptr<std::thread> file_watcher_thread_;
    std::atomic<bool> file_watcher_running_;
    std::unordered_map<std::string, std::filesystem::file_time_type> file_timestamps_;
    
    // Constants
    static constexpr const char* PLUGIN_API_VERSION = "1.0.0";
    static constexpr const char* CREATE_FUNCTION_NAME = "create_plugin_instance";
    static constexpr const char* METADATA_FUNCTION_NAME = "get_plugin_metadata";
    static constexpr const char* API_VERSION_FUNCTION_NAME = "get_plugin_api_version";
};

// Plugin registry for built-in plugins (compiled into the main application)
class BuiltinPluginRegistry {
public:
    static BuiltinPluginRegistry& instance();
    
    // Register built-in plugin
    void register_plugin(const std::string& plugin_id, CreatePluginFunction create_func, const ExchangePluginMetadata& metadata);
    void unregister_plugin(const std::string& plugin_id);
    
    // Get registered plugins
    std::vector<std::string> get_registered_plugins() const;
    CreatePluginFunction get_create_function(const std::string& plugin_id) const;
    ExchangePluginMetadata get_metadata(const std::string& plugin_id) const;
    bool is_registered(const std::string& plugin_id) const;
    
    // Load all built-in plugins into the plugin manager
    void load_all_builtin_plugins();
    
private:
    BuiltinPluginRegistry() = default;
    
    struct BuiltinPlugin {
        CreatePluginFunction create_func;
        ExchangePluginMetadata metadata;
    };
    
    mutable std::shared_mutex registry_mutex_;
    std::unordered_map<std::string, BuiltinPlugin> builtin_plugins_;
};

// Convenience macro for registering built-in plugins
#define REGISTER_BUILTIN_EXCHANGE_PLUGIN(plugin_id, plugin_class) \
    namespace { \
        struct plugin_class##_Registrar { \
            plugin_class##_Registrar() { \
                static plugin_class temp_instance; \
                ats::exchange::BuiltinPluginRegistry::instance().register_plugin( \
                    plugin_id, \
                    []() { return std::make_unique<plugin_class>(); }, \
                    temp_instance.get_metadata() \
                ); \
            } \
        }; \
        static plugin_class##_Registrar plugin_class##_registrar; \
    }

} // namespace exchange
} // namespace ats