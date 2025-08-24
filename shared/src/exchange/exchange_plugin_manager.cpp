#include "exchange/exchange_plugin_manager.hpp"
#include "utils/logger.hpp"
#include <filesystem>
#include <algorithm>
#include <shared_mutex>
#include <mutex>

namespace ats {
namespace exchange {

// Static member initialization
ExchangePluginManager& ExchangePluginManager::instance() {
    static ExchangePluginManager instance;
    return instance;
}

ExchangePluginManager::ExchangePluginManager()
    : auto_scan_enabled_(false)
    , hot_reload_enabled_(false)
    , scan_interval_(std::chrono::seconds(30))
    , last_scan_time_(std::chrono::system_clock::now())
    , file_watcher_running_(false) {
    
    // Set default plugin directory
    plugin_directory_ = (std::filesystem::current_path() / "plugins").string();
}

ExchangePluginManager::~ExchangePluginManager() {
    stop_file_watcher();
    stop_all_plugins();
    unload_all_plugins();
}

bool ExchangePluginManager::scan_plugin_directory(const std::string& directory_path) {
    try {
        if (!std::filesystem::exists(directory_path)) {
            utils::Logger::error("Plugin directory does not exist: " + directory_path);
            return false;
        }
        
        size_t loaded_count = 0;
        for (const auto& entry : std::filesystem::directory_iterator(directory_path)) {
            if (entry.is_regular_file() && is_library_file(entry.path().filename().string())) {
                if (load_plugin(entry.path().string())) {
                    loaded_count++;
                }
            }
        }
        
        last_scan_time_ = std::chrono::system_clock::now();
        utils::Logger::info("Scanned plugin directory: " + directory_path + ", loaded " + std::to_string(loaded_count) + " plugins");
        return true;
        
    } catch (const std::exception& e) {
        utils::Logger::error("Error scanning plugin directory: " + std::string(e.what()));
        return false;
    }
}

bool ExchangePluginManager::load_plugin(const std::string& plugin_path) {
    auto write_lock = get_write_lock();
    
    try {
        // Extract plugin ID from filename
        std::string plugin_id = std::filesystem::path(plugin_path).stem().string();
        plugin_id = normalize_plugin_id(plugin_id);
        
        // Check if already loaded
        if (plugins_.find(plugin_id) != plugins_.end()) {
            utils::Logger::warn("Plugin already loaded: " + plugin_id);
            return true;
        }
        
        // Create plugin instance
        auto plugin_instance = std::make_unique<PluginInstance>();
        
        // Load the library
        if (!load_plugin_library(plugin_path, plugin_instance->descriptor)) {
            std::string error = "Failed to load plugin library: " + plugin_path;
            {
                std::lock_guard<std::mutex> lock(errors_mutex_);
                loading_errors_.push_back(error);
            }
            return false;
        }
        
        // Validate plugin functions
        if (!validate_plugin_functions(plugin_instance->descriptor)) {
            unload_plugin_library(plugin_instance->descriptor);
            std::string error = "Plugin validation failed: " + plugin_id;
            {
                std::lock_guard<std::mutex> lock(errors_mutex_);
                loading_errors_.push_back(error);
            }
            return false;
        }
        
        // Update plugin status
        plugin_instance->descriptor.plugin_path = plugin_path;
        plugin_instance->descriptor.is_loaded = true;
        plugin_instance->descriptor.loaded_at = std::chrono::system_clock::now();
        plugin_instance->status = PluginStatus::LOADED;
        plugin_instance->status_changed_at = std::chrono::system_clock::now();
        
        // Store the plugin
        plugins_[plugin_id] = std::move(plugin_instance);
        
        notify_event(plugin_id, PluginEvent::PLUGIN_LOADED, "Plugin loaded successfully");
        utils::Logger::info("Successfully loaded plugin: " + plugin_id);
        return true;
        
    } catch (const std::exception& e) {
        std::string error = "Exception loading plugin: " + plugin_path + " - " + e.what();
        utils::Logger::error(error);
        {
            std::lock_guard<std::mutex> lock(errors_mutex_);
            loading_errors_.push_back(error);
        }
        return false;
    }
}

bool ExchangePluginManager::load_plugin_from_memory(const std::string& plugin_id, 
                                                   CreatePluginFunction create_func, 
                                                   const ExchangePluginMetadata& metadata) {
    auto write_lock = get_write_lock();
    
    try {
        std::string normalized_id = normalize_plugin_id(plugin_id);
        
        // Check if already loaded
        if (plugins_.find(normalized_id) != plugins_.end()) {
            utils::Logger::warn("Built-in plugin already loaded: " + normalized_id);
            return true;
        }
        
        // Create plugin instance for built-in plugin
        auto plugin_instance = std::make_unique<PluginInstance>();
        plugin_instance->descriptor.metadata = metadata;
        plugin_instance->descriptor.create_function = create_func;
        plugin_instance->descriptor.is_loaded = true;
        plugin_instance->descriptor.loaded_at = std::chrono::system_clock::now();
        plugin_instance->status = PluginStatus::LOADED;
        plugin_instance->status_changed_at = std::chrono::system_clock::now();
        
        // Store the plugin
        plugins_[normalized_id] = std::move(plugin_instance);
        
        notify_event(normalized_id, PluginEvent::PLUGIN_LOADED, "Built-in plugin loaded");
        utils::Logger::info("Successfully loaded built-in plugin: " + normalized_id);
        return true;
        
    } catch (const std::exception& e) {
        std::string error = "Exception loading built-in plugin: " + plugin_id + " - " + e.what();
        utils::Logger::error(error);
        return false;
    }
}

bool ExchangePluginManager::unload_plugin(const std::string& plugin_id) {
    auto write_lock = get_write_lock();
    
    auto it = plugins_.find(plugin_id);
    if (it == plugins_.end()) {
        return false;
    }
    
    // Stop plugin if running
    if (it->second->status == PluginStatus::RUNNING) {
        stop_plugin(plugin_id);
    }
    
    // Cleanup plugin
    if (it->second->plugin) {
        it->second->plugin->cleanup();
        it->second->plugin.reset();
    }
    
    // Unload library if it was loaded from file
    if (it->second->descriptor.library_handle) {
        unload_plugin_library(it->second->descriptor);
    }
    
    notify_event(plugin_id, PluginEvent::PLUGIN_UNLOADED, "Plugin unloaded");
    plugins_.erase(it);
    
    utils::Logger::info("Unloaded plugin: " + plugin_id);
    return true;
}

void ExchangePluginManager::unload_all_plugins() {
    auto write_lock = get_write_lock();
    
    std::vector<std::string> plugin_ids;
    plugin_ids.reserve(plugins_.size());
    
    for (const auto& pair : plugins_) {
        plugin_ids.push_back(pair.first);
    }
    
    write_lock.unlock();
    
    for (const auto& plugin_id : plugin_ids) {
        unload_plugin(plugin_id);
    }
}

bool ExchangePluginManager::initialize_plugin(const std::string& plugin_id, const types::ExchangeConfig& config) {
    auto write_lock = get_write_lock();
    
    auto it = plugins_.find(plugin_id);
    if (it == plugins_.end() || it->second->status != PluginStatus::LOADED) {
        return false;
    }
    
    try {
        // Create plugin instance if not exists
        if (!it->second->plugin) {
            it->second->plugin = it->second->descriptor.create_function();
            if (!it->second->plugin) {
                update_plugin_status(plugin_id, PluginStatus::ERROR, "Failed to create plugin instance");
                return false;
            }
        }
        
        // Initialize plugin
        if (!it->second->plugin->initialize(config)) {
            update_plugin_status(plugin_id, PluginStatus::ERROR, "Plugin initialization failed");
            return false;
        }
        
        update_plugin_status(plugin_id, PluginStatus::INITIALIZED);
        utils::Logger::info("Initialized plugin: " + plugin_id);
        return true;
        
    } catch (const std::exception& e) {
        std::string error = "Exception initializing plugin: " + std::string(e.what());
        update_plugin_status(plugin_id, PluginStatus::ERROR, error);
        utils::Logger::error("Failed to initialize plugin " + plugin_id + ": " + error);
        return false;
    }
}

bool ExchangePluginManager::start_plugin(const std::string& plugin_id) {
    auto write_lock = get_write_lock();
    
    auto it = plugins_.find(plugin_id);
    if (it == plugins_.end() || 
        (it->second->status != PluginStatus::INITIALIZED && it->second->status != PluginStatus::STOPPED)) {
        return false;
    }
    
    try {
        if (!it->second->plugin->start()) {
            update_plugin_status(plugin_id, PluginStatus::ERROR, "Plugin start failed");
            return false;
        }
        
        update_plugin_status(plugin_id, PluginStatus::RUNNING);
        notify_event(plugin_id, PluginEvent::PLUGIN_STARTED, "Plugin started successfully");
        utils::Logger::info("Started plugin: " + plugin_id);
        return true;
        
    } catch (const std::exception& e) {
        std::string error = "Exception starting plugin: " + std::string(e.what());
        update_plugin_status(plugin_id, PluginStatus::ERROR, error);
        utils::Logger::error("Failed to start plugin " + plugin_id + ": " + error);
        return false;
    }
}

bool ExchangePluginManager::stop_plugin(const std::string& plugin_id) {
    auto write_lock = get_write_lock();
    
    auto it = plugins_.find(plugin_id);
    if (it == plugins_.end() || it->second->status != PluginStatus::RUNNING) {
        return false;
    }
    
    try {
        it->second->plugin->stop();
        update_plugin_status(plugin_id, PluginStatus::STOPPED);
        notify_event(plugin_id, PluginEvent::PLUGIN_STOPPED, "Plugin stopped");
        utils::Logger::info("Stopped plugin: " + plugin_id);
        return true;
        
    } catch (const std::exception& e) {
        std::string error = "Exception stopping plugin: " + std::string(e.what());
        update_plugin_status(plugin_id, PluginStatus::ERROR, error);
        utils::Logger::error("Failed to stop plugin " + plugin_id + ": " + error);
        return false;
    }
}

void ExchangePluginManager::stop_all_plugins() {
    auto read_lock = get_read_lock();
    
    std::vector<std::string> running_plugins;
    for (const auto& pair : plugins_) {
        if (pair.second->status == PluginStatus::RUNNING) {
            running_plugins.push_back(pair.first);
        }
    }
    
    read_lock.unlock();
    
    for (const auto& plugin_id : running_plugins) {
        stop_plugin(plugin_id);
    }
}

std::shared_ptr<IExchangePlugin> ExchangePluginManager::get_plugin(const std::string& plugin_id) {
    auto read_lock = get_read_lock();
    
    auto it = plugins_.find(plugin_id);
    if (it == plugins_.end() || !it->second->plugin) {
        return nullptr;
    }
    
    return std::shared_ptr<IExchangePlugin>(it->second->plugin.get(), [](IExchangePlugin*) {});
}

std::vector<std::string> ExchangePluginManager::get_loaded_plugins() const {
    auto read_lock = get_read_lock();
    
    std::vector<std::string> result;
    result.reserve(plugins_.size());
    
    for (const auto& pair : plugins_) {
        result.push_back(pair.first);
    }
    
    return result;
}

std::vector<std::string> ExchangePluginManager::get_running_plugins() const {
    auto read_lock = get_read_lock();
    
    std::vector<std::string> result;
    
    for (const auto& pair : plugins_) {
        if (pair.second->status == PluginStatus::RUNNING) {
            result.push_back(pair.first);
        }
    }
    
    return result;
}

std::vector<ExchangePluginMetadata> ExchangePluginManager::get_available_plugins() const {
    auto read_lock = get_read_lock();
    
    std::vector<ExchangePluginMetadata> result;
    result.reserve(plugins_.size());
    
    for (const auto& pair : plugins_) {
        result.push_back(pair.second->descriptor.metadata);
    }
    
    return result;
}

bool ExchangePluginManager::is_plugin_loaded(const std::string& plugin_id) const {
    auto read_lock = get_read_lock();
    return plugins_.find(plugin_id) != plugins_.end();
}

bool ExchangePluginManager::is_plugin_running(const std::string& plugin_id) const {
    auto read_lock = get_read_lock();
    
    auto it = plugins_.find(plugin_id);
    return it != plugins_.end() && it->second->status == PluginStatus::RUNNING;
}

PluginStatus ExchangePluginManager::get_plugin_status(const std::string& plugin_id) const {
    auto read_lock = get_read_lock();
    
    auto it = plugins_.find(plugin_id);
    return it != plugins_.end() ? it->second->status : PluginStatus::UNLOADED;
}

ExchangePluginMetadata ExchangePluginManager::get_plugin_metadata(const std::string& plugin_id) const {
    auto read_lock = get_read_lock();
    
    auto it = plugins_.find(plugin_id);
    return it != plugins_.end() ? it->second->descriptor.metadata : ExchangePluginMetadata{};
}

std::string ExchangePluginManager::get_plugin_error(const std::string& plugin_id) const {
    auto read_lock = get_read_lock();
    
    auto it = plugins_.find(plugin_id);
    return it != plugins_.end() ? it->second->last_error : "";
}

void ExchangePluginManager::set_event_callback(PluginEventCallback callback) {
    std::lock_guard<std::mutex> lock(event_callback_mutex_);
    event_callback_ = callback;
}

void ExchangePluginManager::clear_event_callback() {
    std::lock_guard<std::mutex> lock(event_callback_mutex_);
    event_callback_ = nullptr;
}

void ExchangePluginManager::set_plugin_directory(const std::string& directory) {
    plugin_directory_ = directory;
}

std::string ExchangePluginManager::get_plugin_directory() const {
    return plugin_directory_;
}

size_t ExchangePluginManager::get_total_plugins() const {
    auto read_lock = get_read_lock();
    return plugins_.size();
}

size_t ExchangePluginManager::get_loaded_plugins_count() const {
    auto read_lock = get_read_lock();
    
    size_t count = 0;
    for (const auto& pair : plugins_) {
        if (pair.second->status >= PluginStatus::LOADED) {
            count++;
        }
    }
    return count;
}

size_t ExchangePluginManager::get_running_plugins_count() const {
    auto read_lock = get_read_lock();
    
    size_t count = 0;
    for (const auto& pair : plugins_) {
        if (pair.second->status == PluginStatus::RUNNING) {
            count++;
        }
    }
    return count;
}

// Private helper methods

bool ExchangePluginManager::load_plugin_library(const std::string& plugin_path, PluginDescriptor& descriptor) {
    // Function pointer declarations for both platforms
    typedef std::unique_ptr<IExchangePlugin>(*CreateFuncPtr)();
    typedef ExchangePluginMetadata(*MetadataFuncPtr)();
    CreateFuncPtr create_func_ptr = nullptr;
    MetadataFuncPtr metadata_func_ptr = nullptr;
    
#ifdef _WIN32
    HMODULE handle = LoadLibraryA(plugin_path.c_str());
    if (!handle) {
        utils::Logger::error("Failed to load plugin library: " + plugin_path + " (Error: " + std::to_string(GetLastError()) + ")");
        return false;
    }
    
    // Get required function pointers
    create_func_ptr = reinterpret_cast<CreateFuncPtr>(GetProcAddress(handle, CREATE_FUNCTION_NAME));
    metadata_func_ptr = reinterpret_cast<MetadataFuncPtr>(GetProcAddress(handle, METADATA_FUNCTION_NAME));
    
    if (!create_func_ptr || !metadata_func_ptr) {
        FreeLibrary(handle);
        utils::Logger::error("Plugin missing required functions: " + plugin_path);
        return false;
    }
    
    descriptor.library_handle = handle;
#else
    void* handle = dlopen(plugin_path.c_str(), RTLD_LAZY);
    if (!handle) {
        utils::Logger::error("Failed to load plugin library: " + plugin_path + " (" + dlerror() + ")");
        return false;
    }
    
    // Get required function pointers
    create_func_ptr = reinterpret_cast<CreateFuncPtr>(dlsym(handle, CREATE_FUNCTION_NAME));
    metadata_func_ptr = reinterpret_cast<MetadataFuncPtr>(dlsym(handle, METADATA_FUNCTION_NAME));
    
    if (!create_func_ptr || !metadata_func_ptr) {
        dlclose(handle);
        utils::Logger::error("Plugin missing required functions: " + plugin_path);
        return false;
    }
    
    descriptor.library_handle = handle;
#endif
    
    // Wrap function pointers in std::function objects
    descriptor.create_function = [create_func_ptr]() { return create_func_ptr(); };
    descriptor.metadata_function = [metadata_func_ptr]() { return metadata_func_ptr(); };
    
    // Get metadata
    try {
        descriptor.metadata = descriptor.metadata_function();
    } catch (const std::exception& e) {
        unload_plugin_library(descriptor);
        utils::Logger::error("Failed to get plugin metadata: " + std::string(e.what()));
        return false;
    }
    
    return true;
}

void ExchangePluginManager::unload_plugin_library(PluginDescriptor& descriptor) {
    if (descriptor.library_handle) {
#ifdef _WIN32
        FreeLibrary(static_cast<HMODULE>(descriptor.library_handle));
#else
        dlclose(descriptor.library_handle);
#endif
        descriptor.library_handle = nullptr;
        descriptor.is_loaded = false;
    }
}

bool ExchangePluginManager::validate_plugin_functions(const PluginDescriptor& descriptor) {
    return descriptor.create_function && descriptor.metadata_function;
}

void ExchangePluginManager::update_plugin_status(const std::string& plugin_id, PluginStatus status, const std::string& error) {
    auto it = plugins_.find(plugin_id);
    if (it != plugins_.end()) {
        it->second->status = status;
        it->second->last_error = error;
        it->second->status_changed_at = std::chrono::system_clock::now();
        
        if (status == PluginStatus::ERROR && !error.empty()) {
            notify_event(plugin_id, PluginEvent::PLUGIN_ERROR, error);
        }
    }
}

void ExchangePluginManager::notify_event(const std::string& plugin_id, PluginEvent event, const std::string& message) {
    std::lock_guard<std::mutex> lock(event_callback_mutex_);
    if (event_callback_) {
        event_callback_(plugin_id, event, message);
    }
}

std::string ExchangePluginManager::get_library_extension() const {
#ifdef _WIN32
    return ".dll";
#elif __APPLE__
    return ".dylib";
#else
    return ".so";
#endif
}

bool ExchangePluginManager::is_library_file(const std::string& filename) const {
    std::string ext = get_library_extension();
    return filename.size() > ext.size() && 
           filename.substr(filename.size() - ext.size()) == ext;
}

std::string ExchangePluginManager::normalize_plugin_id(const std::string& plugin_id) const {
    std::string result = plugin_id;
    std::transform(result.begin(), result.end(), result.begin(), ::tolower);
    return result;
}

std::shared_lock<std::shared_mutex> ExchangePluginManager::get_read_lock() const {
    return std::shared_lock<std::shared_mutex>(plugins_mutex_);
}

std::unique_lock<std::shared_mutex> ExchangePluginManager::get_write_lock() const {
    return std::unique_lock<std::shared_mutex>(plugins_mutex_);
}

// BuiltinPluginRegistry implementation

BuiltinPluginRegistry& BuiltinPluginRegistry::instance() {
    static BuiltinPluginRegistry instance;
    return instance;
}

void BuiltinPluginRegistry::register_plugin(const std::string& plugin_id, 
                                          CreatePluginFunction create_func, 
                                          const ExchangePluginMetadata& metadata) {
    std::unique_lock<std::shared_mutex> lock(registry_mutex_);
    builtin_plugins_[plugin_id] = {create_func, metadata};
}

void BuiltinPluginRegistry::unregister_plugin(const std::string& plugin_id) {
    std::unique_lock<std::shared_mutex> lock(registry_mutex_);
    builtin_plugins_.erase(plugin_id);
}

std::vector<std::string> BuiltinPluginRegistry::get_registered_plugins() const {
    std::shared_lock<std::shared_mutex> lock(registry_mutex_);
    
    std::vector<std::string> result;
    result.reserve(builtin_plugins_.size());
    
    for (const auto& pair : builtin_plugins_) {
        result.push_back(pair.first);
    }
    
    return result;
}

CreatePluginFunction BuiltinPluginRegistry::get_create_function(const std::string& plugin_id) const {
    std::shared_lock<std::shared_mutex> lock(registry_mutex_);
    
    auto it = builtin_plugins_.find(plugin_id);
    return it != builtin_plugins_.end() ? it->second.create_func : nullptr;
}

ExchangePluginMetadata BuiltinPluginRegistry::get_metadata(const std::string& plugin_id) const {
    std::shared_lock<std::shared_mutex> lock(registry_mutex_);
    
    auto it = builtin_plugins_.find(plugin_id);
    return it != builtin_plugins_.end() ? it->second.metadata : ExchangePluginMetadata{};
}

bool BuiltinPluginRegistry::is_registered(const std::string& plugin_id) const {
    std::shared_lock<std::shared_mutex> lock(registry_mutex_);
    return builtin_plugins_.find(plugin_id) != builtin_plugins_.end();
}

void BuiltinPluginRegistry::load_all_builtin_plugins() {
    auto plugins = get_registered_plugins();
    
    for (const auto& plugin_id : plugins) {
        auto create_func = get_create_function(plugin_id);
        auto metadata = get_metadata(plugin_id);
        
        if (create_func) {
            ExchangePluginManager::instance().load_plugin_from_memory(plugin_id, create_func, metadata);
        }
    }
}

// Production-grade file watcher implementations
void ExchangePluginManager::start_file_watcher() {
    if (file_watcher_running_.load()) {
        utils::Logger::warn("File watcher is already running");
        return;
    }

    if (!auto_scan_enabled_.load()) {
        utils::Logger::debug("File watcher not started - auto scan is disabled");
        return;
    }

    try {
        // Initialize file timestamps for existing files
        if (std::filesystem::exists(plugin_directory_)) {
            auto write_lock = get_write_lock();
            file_timestamps_.clear();
            
            for (const auto& entry : std::filesystem::directory_iterator(plugin_directory_)) {
                if (entry.is_regular_file() && is_library_file(entry.path().filename().string())) {
                    try {
                        file_timestamps_[entry.path().string()] = std::filesystem::last_write_time(entry.path());
                    } catch (const std::filesystem::filesystem_error& e) {
                        utils::Logger::error("Failed to get timestamp for file: " + entry.path().string() + " - " + e.what());
                    }
                }
            }
        }

        // Start the watcher thread
        file_watcher_running_.store(true);
        file_watcher_thread_ = std::make_unique<std::thread>([this]() {
            utils::Logger::info("File watcher thread started, monitoring: " + plugin_directory_);
            
            while (file_watcher_running_.load()) {
                try {
                    scan_for_changes();
                    std::this_thread::sleep_for(scan_interval_);
                } catch (const std::exception& e) {
                    utils::Logger::error("File watcher error: " + std::string(e.what()));
                    // Continue running even if there's an error
                    std::this_thread::sleep_for(std::chrono::seconds(5));
                }
            }
            
            utils::Logger::info("File watcher thread stopped");
        });
        
        utils::Logger::info("File watcher started successfully");
    } catch (const std::exception& e) {
        file_watcher_running_.store(false);
        utils::Logger::error("Failed to start file watcher: " + std::string(e.what()));
        throw;
    }
}

void ExchangePluginManager::stop_file_watcher() {
    if (!file_watcher_running_.load()) {
        return; // Already stopped
    }

    try {
        // Signal the thread to stop
        file_watcher_running_.store(false);
        
        // Wait for the thread to finish
        if (file_watcher_thread_ && file_watcher_thread_->joinable()) {
            file_watcher_thread_->join();
        }
        
        file_watcher_thread_.reset();
        
        // Clear file timestamps
        {
            auto write_lock = get_write_lock();
            file_timestamps_.clear();
        }
        
        utils::Logger::info("File watcher stopped successfully");
    } catch (const std::exception& e) {
        utils::Logger::error("Error stopping file watcher: " + std::string(e.what()));
    }
}

void ExchangePluginManager::scan_for_changes() {
    if (!std::filesystem::exists(plugin_directory_)) {
        utils::Logger::warn("Plugin directory does not exist: " + plugin_directory_);
        return;
    }

    try {
        bool changes_detected = false;
        std::vector<std::string> added_files;
        std::vector<std::string> modified_files;
        std::vector<std::string> removed_files;
        
        // Get current state of files
        std::unordered_map<std::string, std::filesystem::file_time_type> current_files;
        
        for (const auto& entry : std::filesystem::directory_iterator(plugin_directory_)) {
            if (entry.is_regular_file() && is_library_file(entry.path().filename().string())) {
                try {
                    current_files[entry.path().string()] = std::filesystem::last_write_time(entry.path());
                } catch (const std::filesystem::filesystem_error& e) {
                    utils::Logger::error("Failed to get timestamp for file: " + entry.path().string() + " - " + e.what());
                }
            }
        }

        // Compare with stored timestamps
        {
            auto write_lock = get_write_lock();
            
            // Check for new or modified files
            for (const auto& [file_path, timestamp] : current_files) {
                auto it = file_timestamps_.find(file_path);
                if (it == file_timestamps_.end()) {
                    // New file
                    added_files.push_back(file_path);
                    changes_detected = true;
                } else if (it->second != timestamp) {
                    // Modified file
                    modified_files.push_back(file_path);
                    changes_detected = true;
                }
            }
            
            // Check for removed files
            for (const auto& [file_path, timestamp] : file_timestamps_) {
                if (current_files.find(file_path) == current_files.end()) {
                    removed_files.push_back(file_path);
                    changes_detected = true;
                }
            }
            
            // Update stored timestamps
            if (changes_detected) {
                file_timestamps_ = std::move(current_files);
                last_scan_time_ = std::chrono::system_clock::now();
            }
        }

        // Handle detected changes
        if (changes_detected) {
            utils::Logger::info("Plugin directory changes detected - Added: " + 
                std::to_string(added_files.size()) + ", Modified: " + 
                std::to_string(modified_files.size()) + ", Removed: " + 
                std::to_string(removed_files.size()));

            // Handle removed files first
            for (const auto& file_path : removed_files) {
                handle_file_change(file_path); // This will unload the plugin
            }
            
            // Handle modified files (reload)
            for (const auto& file_path : modified_files) {
                handle_file_change(file_path);
            }
            
            // Handle new files (load)
            for (const auto& file_path : added_files) {
                handle_file_change(file_path);
            }
        }

    } catch (const std::exception& e) {
        utils::Logger::error("Error during directory scan: " + std::string(e.what()));
    }
}

void ExchangePluginManager::handle_file_change(const std::string& file_path) {
    try {
        std::string filename = std::filesystem::path(file_path).filename().string();
        std::string plugin_id = filename.substr(0, filename.find_last_of('.'));
        
        if (!std::filesystem::exists(file_path)) {
            // File was removed - unload plugin if it exists
            if (is_plugin_loaded(plugin_id)) {
                utils::Logger::info("Plugin file removed, unloading: " + plugin_id);
                unload_plugin(plugin_id);
            }
            return;
        }

        // File was added or modified
        if (is_plugin_loaded(plugin_id)) {
            if (hot_reload_enabled_.load()) {
                utils::Logger::info("Plugin file modified, hot reloading: " + plugin_id);
                
                // Stop the plugin first
                stop_plugin(plugin_id);
                
                // Unload and reload
                unload_plugin(plugin_id);
                if (load_plugin(file_path)) {
                    utils::Logger::info("Plugin hot reloaded successfully: " + plugin_id);
                } else {
                    utils::Logger::error("Failed to hot reload plugin: " + plugin_id);
                }
            } else {
                utils::Logger::info("Plugin file modified but hot reload is disabled: " + plugin_id);
            }
        } else {
            // New plugin file - load it
            utils::Logger::info("New plugin file detected, loading: " + plugin_id);
            if (load_plugin(file_path)) {
                utils::Logger::info("New plugin loaded successfully: " + plugin_id);
            } else {
                utils::Logger::error("Failed to load new plugin: " + plugin_id);
            }
        }

    } catch (const std::exception& e) {
        utils::Logger::error("Error handling file change for " + file_path + ": " + std::string(e.what()));
    }
}

} // namespace exchange
} // namespace ats