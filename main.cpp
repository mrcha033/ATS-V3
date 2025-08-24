#include <iostream>
#include <csignal>
#include <thread>
#include <chrono>
#include <atomic>
#include <memory>

#include "utils/logger.hpp"
#include "exchange/exchange_plugin_manager.hpp"

using namespace ats;

class ATSTradingSystem {
private:
    std::atomic<bool> running_{true};
    std::unique_ptr<exchange::ExchangePluginManager> plugin_manager_;
    
public:
    ATSTradingSystem() {
        plugin_manager_ = std::make_unique<exchange::ExchangePluginManager>();
    }
    
    void initialize() {
        utils::Logger::info("=== ATS-V3 Production Trading System Starting ===");
        
        // Initialize plugin manager
        plugin_manager_->initialize();
        
        // Load available exchange plugins
        plugin_manager_->load_plugin("sample"); // Load sample plugin for testing
        
        utils::Logger::info("Trading system initialized successfully");
    }
    
    void run() {
        utils::Logger::info("Starting main trading loop...");
        
        while (running_.load()) {
            try {
                // Main trading loop operations
                process_market_data();
                execute_trading_logic();
                update_positions();
                
                // Sleep for 100ms between iterations
                std::this_thread::sleep_for(std::chrono::milliseconds(100));
                
            } catch (const std::exception& e) {
                utils::Logger::error("Error in trading loop: " + std::string(e.what()));
                std::this_thread::sleep_for(std::chrono::seconds(1));
            }
        }
        
        utils::Logger::info("Trading loop stopped");
    }
    
    void shutdown() {
        utils::Logger::info("Shutting down ATS-V3 trading system...");
        running_.store(false);
        
        if (plugin_manager_) {
            plugin_manager_->shutdown();
        }
        
        utils::Logger::info("=== ATS-V3 Shutdown Complete ===");
    }
    
private:
    void process_market_data() {
        // Process real-time market data
        // This would integrate with exchange APIs
        static int tick = 0;
        if (++tick % 100 == 0) {
            utils::Logger::debug("Processing market data tick: " + std::to_string(tick));
        }
    }
    
    void execute_trading_logic() {
        // Execute trading algorithms and strategies
        // This is where your trading logic would go
    }
    
    void update_positions() {
        // Update position tracking and risk management
        // Monitor P&L, risk metrics, etc.
    }
};

// Global trading system instance
std::unique_ptr<ATSTradingSystem> g_trading_system;

// Signal handler for graceful shutdown
void signal_handler(int signal) {
    switch (signal) {
        case SIGINT:
            utils::Logger::info("Received SIGINT, initiating shutdown...");
            break;
        case SIGTERM:
            utils::Logger::info("Received SIGTERM, initiating shutdown...");
            break;
        default:
            utils::Logger::warn("Received unknown signal: " + std::to_string(signal));
            break;
    }
    
    if (g_trading_system) {
        g_trading_system->shutdown();
    }
}

int main() {
    try {
        // Set up signal handlers for graceful shutdown
        std::signal(SIGINT, signal_handler);
        std::signal(SIGTERM, signal_handler);
        
        // Create and initialize trading system
        g_trading_system = std::make_unique<ATSTradingSystem>();
        g_trading_system->initialize();
        
        // Run the main trading loop
        g_trading_system->run();
        
        return 0;
        
    } catch (const std::exception& e) {
        utils::Logger::error("Fatal error in main: " + std::string(e.what()));
        std::cerr << "FATAL: " << e.what() << std::endl;
        return 1;
    }
}