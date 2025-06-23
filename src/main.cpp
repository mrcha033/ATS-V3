#include <iostream>
#include <memory>
#include <signal.h>
#include <thread>
#include <chrono>
#include <atomic>

#include "utils/logger.hpp"
#include "utils/config_manager.hpp"
#include "core/arbitrage_engine.hpp"
#include "monitoring/system_monitor.hpp"
#include "monitoring/health_check.hpp"

// Global atomic flag for shutdown - moved outside namespace for proper linkage
std::atomic<bool> g_shutdown_requested{false};

// Signal handler for graceful shutdown
void SignalHandler(int signal) {
    // Only use async-signal-safe operations in signal handler
    g_shutdown_requested.store(true);
}

namespace ats {

class Application {
private:
    std::unique_ptr<ConfigManager> config_manager_;
    std::unique_ptr<ArbitrageEngine> arbitrage_engine_;
    std::unique_ptr<SystemMonitor> system_monitor_;
    std::unique_ptr<HealthCheck> health_check_;
    
    std::atomic<bool> running_{true};
    
public:
    bool Initialize() {
        try {
            // Initialize logger first
            Logger::Initialize();
            LOG_INFO("ATS V3 Starting...");
            
            // Load configuration
            config_manager_ = std::make_unique<ConfigManager>();
            if (!config_manager_->LoadConfig("config/settings.json")) {
                LOG_ERROR("Failed to load configuration");
                return false;
            }
            
            // Initialize system monitor
            system_monitor_ = std::make_unique<SystemMonitor>();
            system_monitor_->Start();
            
            // Initialize health check
            health_check_ = std::make_unique<HealthCheck>();
            if (!health_check_->Initialize()) {
                LOG_ERROR("Failed to initialize health check");
                return false;
            }
            health_check_->Start();
            
            // Initialize arbitrage engine
            arbitrage_engine_ = std::make_unique<ArbitrageEngine>(config_manager_.get());
            if (!arbitrage_engine_->Initialize()) {
                LOG_ERROR("Failed to initialize arbitrage engine");
                return false;
            }
            
            LOG_INFO("ATS V3 Initialized successfully");
            return true;
            
        } catch (const std::exception& e) {
            LOG_ERROR("Initialization failed: {}", e.what());
            return false;
        }
    }
    
    void Run() {
        LOG_INFO("ATS V3 Starting main loop");
        
        // Start arbitrage engine
        arbitrage_engine_->Start();
        
        // Main application loop
        while (running_ && !g_shutdown_requested.load()) {
            try {
                // Health check
                if (!health_check_->CheckSystem()) {
                    LOG_WARNING("System health check failed");
                }
                
                // Check system resources
                auto metrics = system_monitor_->GetCurrentMetrics();
                LOG_INFO("System Status - CPU: {:.1f}%, Memory: {:.1f}%, Disk: {:.1f}%", 
                        metrics.cpu_usage_percent, metrics.memory_usage_percent, metrics.disk_usage_percent);
                
                // Sleep for 1 second
                std::this_thread::sleep_for(std::chrono::seconds(1));
                
            } catch (const std::exception& e) {
                LOG_ERROR("Error in main loop: {}", e.what());
                std::this_thread::sleep_for(std::chrono::seconds(5));
            }
        }
        
        // Handle shutdown signal
        if (g_shutdown_requested.load()) {
            LOG_INFO("Shutdown signal received, shutting down gracefully...");
            Shutdown();
        }
        
        LOG_INFO("ATS V3 Shutting down...");
    }
    
    void Shutdown() {
        running_.store(false);
        
        if (arbitrage_engine_) {
            arbitrage_engine_->Stop();
        }
        
        if (system_monitor_) {
            system_monitor_->Stop();
        }
        
        if (health_check_) {
            health_check_->Stop();
        }
        
        LOG_INFO("ATS V3 Shutdown complete");
    }
};

// Global application instance
std::unique_ptr<Application> g_app;

} // namespace ats

int main(int argc, char* argv[]) {
    try {
        // Install signal handlers
        signal(SIGINT, SignalHandler);
        signal(SIGTERM, SignalHandler);
        
        // Create and initialize application
        ats::g_app = std::make_unique<ats::Application>();
        
        if (!ats::g_app->Initialize()) {
            std::cerr << "Failed to initialize ATS V3" << std::endl;
            return 1;
        }
        
        // Run application
        ats::g_app->Run();
        
        return 0;
        
    } catch (const std::exception& e) {
        std::cerr << "Fatal error: " << e.what() << std::endl;
        return 1;
    }
} 
