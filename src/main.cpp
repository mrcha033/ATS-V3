#include <iostream>
#include <memory>
#include <vector>
#include <csignal>
#include <thread>
#include <chrono>

#include "utils/config_manager.hpp"
#include "utils/logger.hpp"
#include "core/app_state.hpp"
#include "core/price_monitor.hpp"
#include "core/opportunity_detector.hpp"
#include "core/arbitrage_engine.hpp"
#include "core/portfolio_manager.hpp"
#include "core/risk_manager.hpp"
#include "core/trade_executor.hpp"
#include "data/database_manager.hpp"
#include "exchange/exchange_factory.hpp"
#include "monitoring/health_check.hpp"
#include "monitoring/system_monitor.hpp"

// Global application state
ats::AppState app_state;

// Signal handler for graceful shutdown
void signal_handler(int signal) {
    if (signal == SIGINT || signal == SIGTERM) {
        ats::Logger::info("Shutdown signal received. Initiating graceful shutdown...");
        app_state.shutdown();
    }
}

int main(int argc, char* argv[]) {
    // Register signal handlers
    signal(SIGINT, signal_handler);
    signal(SIGTERM, signal_handler);

    // Initialize logger
    ats::Logger::init("ats_v3.log");
    ats::Logger::info("Starting ATS-V3...");

    // Load configuration
    ats::ConfigManager config_manager;
    if (!config_manager.load("config/settings.json")) {
        ats::Logger::error("Failed to load configuration. Exiting.");
        return 1;
    }

    // Initialize application components
    auto db_manager = std::make_unique<ats::DatabaseManager>(config_manager.get_db_path());
    if (!db_manager->Open()) {
        ats::Logger::error("Failed to open database. Exiting.");
        return 1;
    }

    auto portfolio_manager = std::make_unique<ats::PortfolioManager>(&config_manager);
    auto risk_manager = std::make_unique<ats::RiskManager>(&config_manager, db_manager.get());
    auto trade_executor = std::make_unique<ats::TradeExecutor>(&config_manager, portfolio_manager.get(), risk_manager.get());

    // Initialize exchanges
    auto exchanges = ats::ExchangeFactory::create_exchanges(config_manager.get_exchanges_config(), &app_state);
    if (exchanges.empty()) {
        ats::Logger::error("No exchanges initialized. Exiting.");
        return 1;
    }

    // Initialize core components
    auto price_monitor = std::make_unique<ats::PriceMonitor>(exchanges);
    auto opportunity_detector = std::make_unique<ats::OpportunityDetector>(config_manager.get_symbols());
    auto arbitrage_engine = std::make_unique<ats::ArbitrageEngine>(risk_manager.get(), trade_executor.get());

    // Initialize monitoring components
    auto health_check = std::make_unique<ats::HealthCheck>();
    auto system_monitor = std::make_unique<ats::SystemMonitor>();

    // Set up dependencies
    price_monitor->set_update_callback([&](const ats::PriceComparison& comp) {
        opportunity_detector->update_prices(comp);
    });

    opportunity_detector->set_opportunity_callback([&](const ats::ArbitrageOpportunity& opp) {
        arbitrage_engine->evaluate_opportunity(opp);
    });

    // Start all components in separate threads
    std::vector<std::thread> threads;
    threads.emplace_back([&]() { price_monitor->start(); });
    threads.emplace_back([&]() { opportunity_detector->start(); });
    threads.emplace_back([&]() { arbitrage_engine->start(); });
    threads.emplace_back([&]() { health_check->Start(); });
    threads.emplace_back([&]() { system_monitor->Start(); });

    ats::Logger::info("ATS-V3 is running.");

    // Main loop to keep the application alive
    while (app_state.is_running()) {
        // Perform periodic tasks, e.g., logging status
        if (app_state.is_running()) {
            ats::Logger::info("ATS-V3 main loop is running...");
            std::this_thread::sleep_for(std::chrono::seconds(60));
        }
    }

    // Stop all components
    ats::Logger::info("Stopping all components...");
    price_monitor->stop();
    opportunity_detector->stop();
    arbitrage_engine->stop();
    health_check->Stop();
    system_monitor->Stop();

    // Wait for all threads to complete
    for (auto& t : threads) {
        if (t.joinable()) {
            t.join();
        }
    }

    ats::Logger::info("ATS-V3 has shut down gracefully.");
    return 0;
}