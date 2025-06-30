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
#include "core/event_loop.hpp"

// Global application state
ats::AppState app_state;
ats::EventLoop* event_loop_ptr = nullptr;

// Signal handler for graceful shutdown
void signal_handler(int signal) {
    if (signal == SIGINT || signal == SIGTERM) {
        ats::Logger::info("Shutdown signal received. Initiating graceful shutdown...");
        app_state.shutdown();
        if (event_loop_ptr) {
            event_loop_ptr->stop();
        }
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
    for (const auto& exchange : exchanges) {
        trade_executor->AddExchange(exchange);
    }

    // Initialize exchanges
    auto exchanges = ats::ExchangeFactory::create_exchanges(config_manager.get_exchanges_config(), &app_state);
    if (exchanges.empty()) {
        ats::Logger::error("No exchanges initialized. Exiting.");
        return 1;
    }

    // Initialize core components
    auto price_monitor = std::make_unique<ats::PriceMonitor>(&config_manager, exchanges);
    auto opportunity_detector = std::make_unique<ats::OpportunityDetector>(&config_manager, config_manager.get_symbols());
    auto arbitrage_engine = std::make_unique<ats::ArbitrageEngine>(risk_manager.get(), trade_executor.get());

    // Initialize monitoring components
    auto health_check = std::make_unique<ats::HealthCheck>();
    auto system_monitor = std::make_unique<ats::SystemMonitor>();

    // Initialize event loop
    ats::EventLoop event_loop(opportunity_detector.get(), arbitrage_engine.get());
    event_loop_ptr = &event_loop;

    // Set up dependencies
    price_monitor->set_event_pusher(&event_loop);
    opportunity_detector->set_event_pusher(&event_loop);

    // Start all components in separate threads
    std::vector<std::thread> threads;
    threads.emplace_back([&]() {
        while (app_state.is_running()) {
            price_monitor->check_prices();
            std::this_thread::sleep_for(std::chrono::milliseconds(1000));
        }
    });
    threads.emplace_back([&]() { health_check->Start(); });
    threads.emplace_back([&]() { system_monitor->Start(); });

    ats::Logger::info("ATS-V3 is running.");

    // Start the event loop
    event_loop.run();

    // Start the event loop
    event_loop.run();

    // Stop all components
    ats::Logger::info("Stopping all components...");
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