#include "arbitrage_engine.hpp"
#include "../utils/logger.hpp"
#include "price_monitor.hpp"
#include "opportunity_detector.hpp"
#include "risk_manager.hpp"
#include "trade_executor.hpp"
#include "portfolio_manager.hpp"
#include "../exchange/binance_exchange.hpp"
#include "../exchange/upbit_exchange.hpp"

namespace ats {

ArbitrageEngine::ArbitrageEngine(ConfigManager* config_manager)
    : config_manager_(config_manager), running_(false),
      opportunities_found_(0), trades_executed_(0), total_profit_(0.0) {
}

ArbitrageEngine::~ArbitrageEngine() {
    Stop();
}

bool ArbitrageEngine::Initialize() {
    try {
        LOG_INFO("Initializing Arbitrage Engine...");
        
        // Initialize components
        if (!InitializeExchanges()) {
            LOG_ERROR("Failed to initialize exchanges");
            return false;
        }
        
        if (!InitializeComponents()) {
            LOG_ERROR("Failed to initialize components");
            return false;
        }
        
        LOG_INFO("Arbitrage Engine initialized successfully");
        return true;
        
    } catch (const std::exception& e) {
        LOG_ERROR("Exception during initialization: {}", e.what());
        return false;
    }
}

void ArbitrageEngine::Start() {
    if (running_.load()) {
        LOG_WARNING("Arbitrage Engine is already running");
        return;
    }
    
    running_ = true;
    main_thread_ = std::thread(&ArbitrageEngine::MainLoop, this);
    LOG_INFO("Arbitrage Engine started");
}

void ArbitrageEngine::Stop() {
    if (!running_.load()) {
        return;
    }
    
    running_ = false;
    
    if (main_thread_.joinable()) {
        main_thread_.join();
    }
    
    LOG_INFO("Arbitrage Engine stopped");
}

bool ArbitrageEngine::AddExchange(std::unique_ptr<ExchangeInterface> exchange) {
    std::lock_guard<std::mutex> lock(engine_mutex_);
    
    if (!exchange) {
        LOG_ERROR("Cannot add null exchange");
        return false;
    }
    
    std::string name = exchange->GetName();
    exchanges_.push_back(std::move(exchange));
    
    LOG_INFO("Added exchange: {}", name);
    return true;
}

std::vector<ExchangeInterface*> ArbitrageEngine::GetExchanges() {
    std::lock_guard<std::mutex> lock(engine_mutex_);
    std::vector<ExchangeInterface*> result;
    
    for (const auto& exchange : exchanges_) {
        result.push_back(exchange.get());
    }
    
    return result;
}

ExchangeInterface* ArbitrageEngine::GetExchange(const std::string& name) {
    std::lock_guard<std::mutex> lock(engine_mutex_);
    
    for (const auto& exchange : exchanges_) {
        if (exchange->GetName() == name) {
            return exchange.get();
        }
    }
    
    return nullptr;
}

bool ArbitrageEngine::IsHealthy() const {
    if (!running_.load()) {
        return false;
    }
    
    // Check if at least 2 exchanges are connected
    int connected_count = 0;
    for (const auto& exchange : exchanges_) {
        if (exchange->IsHealthy()) {
            connected_count++;
        }
    }
    
    return connected_count >= 2;
}

std::string ArbitrageEngine::GetStatus() const {
    if (!running_.load()) {
        return "STOPPED";
    }
    
    if (IsHealthy()) {
        return "RUNNING";
    }
    
    return "UNHEALTHY";
}

void ArbitrageEngine::MainLoop() {
    LOG_INFO("Arbitrage Engine main loop started");
    
    // Start all components
    if (price_monitor_) price_monitor_->Start();
    if (opportunity_detector_) opportunity_detector_->Start();
    if (trade_executor_) trade_executor_->Start();
    
    while (running_.load()) {
        try {
            // Perform health checks
            PerformHealthChecks();
            
            // Update portfolio metrics
            if (portfolio_manager_) {
                portfolio_manager_->UpdateAll();
            }
            
            // Check for emergency conditions
            if (risk_manager_ && risk_manager_->IsKillSwitchActive()) {
                LOG_WARNING("Kill switch is active - halting operations");
                std::this_thread::sleep_for(std::chrono::seconds(5));
                continue;
            }
            
            std::this_thread::sleep_for(std::chrono::milliseconds(1000));
            
        } catch (const std::exception& e) {
            LOG_ERROR("Error in main loop: {}", e.what());
            std::this_thread::sleep_for(std::chrono::seconds(1));
        }
    }
    
    // Stop all components
    if (trade_executor_) trade_executor_->Stop();
    if (opportunity_detector_) opportunity_detector_->Stop();
    if (price_monitor_) price_monitor_->Stop();
    
    LOG_INFO("Arbitrage Engine main loop stopped");
}

bool ArbitrageEngine::InitializeExchanges() {
    // Get exchange configurations
    auto exchange_configs = config_manager_->GetExchangeConfigs();
    
    if (exchange_configs.empty()) {
        LOG_WARNING("No exchange configurations found");
        return true; // Not an error, just no exchanges to initialize
    }
    
    LOG_INFO("Found {} exchange configurations", exchange_configs.size());
    
    // Create and initialize exchange instances
    for (const auto& config : exchange_configs) {
        if (!config.enabled) {
            LOG_INFO("Skipping disabled exchange: {}", config.name);
            continue;
        }
        
        std::unique_ptr<ExchangeInterface> exchange = CreateExchange(config);
        if (!exchange) {
            LOG_ERROR("Failed to create exchange instance for: {}", config.name);
            continue;
        }
        
        // Connect to exchange
        if (!exchange->Connect()) {
            LOG_ERROR("Failed to connect to exchange: {}", config.name);
            continue;
        }
        
        // Verify connection
        if (!exchange->IsHealthy()) {
            LOG_WARNING("Exchange {} connected but health check failed: {}", 
                       config.name, exchange->GetLastError());
        }
        
        // Add to exchanges list
        exchanges_.push_back(std::move(exchange));
        LOG_INFO("Successfully initialized exchange: {}", config.name);
    }
    
    if (exchanges_.empty()) {
        LOG_ERROR("No exchanges were successfully initialized");
        return false;
    }
    
    LOG_INFO("Initialized {} out of {} configured exchanges", 
             exchanges_.size(), exchange_configs.size());
    return true;
}

bool ArbitrageEngine::InitializeComponents() {
    try {
        // Initialize price monitor
        price_monitor_ = std::make_unique<PriceMonitor>(config_manager_);
        if (!price_monitor_->Initialize()) {
            LOG_ERROR("Failed to initialize price monitor");
            return false;
        }
        
        // Initialize risk manager
        risk_manager_ = std::make_unique<RiskManager>(config_manager_);
        if (!risk_manager_->Initialize()) {
            LOG_ERROR("Failed to initialize risk manager");
            return false;
        }
        
        // Initialize portfolio manager
        portfolio_manager_ = std::make_unique<PortfolioManager>(config_manager_);
        if (!portfolio_manager_->Initialize()) {
            LOG_ERROR("Failed to initialize portfolio manager");
            return false;
        }
        
        // Initialize opportunity detector
        opportunity_detector_ = std::make_unique<OpportunityDetector>(config_manager_, price_monitor_.get());
        if (!opportunity_detector_->Initialize()) {
            LOG_ERROR("Failed to initialize opportunity detector");
            return false;
        }
        
        // Initialize trade executor
        trade_executor_ = std::make_unique<TradeExecutor>(config_manager_, risk_manager_.get());
        if (!trade_executor_->Initialize()) {
            LOG_ERROR("Failed to initialize trade executor");
            return false;
        }
        
        // Set up component connections
        SetupComponentCallbacks();
        
        LOG_INFO("All Phase 3 components initialized successfully");
        return true;
        
    } catch (const std::exception& e) {
        LOG_ERROR("Exception initializing components: {}", e.what());
        return false;
    }
}

void ArbitrageEngine::ProcessOpportunity(const ArbitrageOpportunity& opportunity) {
    // TODO: Implement opportunity processing in Phase 3
    opportunities_found_++;
    LOG_DEBUG("Processing arbitrage opportunity for {}: {:.2f}% profit", 
              opportunity.symbol, opportunity.profit_percent);
}

void ArbitrageEngine::UpdateStatistics(const ArbitrageOpportunity& opportunity, bool executed) {
    if (executed) {
        trades_executed_++;
        double current_profit = total_profit_.load();
        while (!total_profit_.compare_exchange_weak(current_profit, current_profit + opportunity.profit_percent)) {
            // Retry until successful
        }
    }
}

void ArbitrageEngine::PerformHealthChecks() {
    // Check exchange health
    for (const auto& exchange : exchanges_) {
        if (!exchange->IsHealthy()) {
            LOG_WARNING("Exchange {} is unhealthy: {}", 
                       exchange->GetName(), exchange->GetLastError());
        }
    }
    
    // Check component health
    if (price_monitor_ && !price_monitor_->IsHealthy()) {
        LOG_WARNING("Price monitor is unhealthy");
    }
    
    if (opportunity_detector_ && !opportunity_detector_->IsHealthy()) {
        LOG_WARNING("Opportunity detector is unhealthy");
    }
    
    if (trade_executor_ && !trade_executor_->IsHealthy()) {
        LOG_WARNING("Trade executor is unhealthy");
    }
    
    if (risk_manager_ && !risk_manager_->IsHealthy()) {
        LOG_WARNING("Risk manager is unhealthy");
    }
    
    if (portfolio_manager_ && !portfolio_manager_->IsHealthy()) {
        LOG_WARNING("Portfolio manager is unhealthy");
    }
}

void ArbitrageEngine::SetupComponentCallbacks() {
    // Set up opportunity detection callback
    if (opportunity_detector_) {
        opportunity_detector_->SetOpportunityCallback(
            [this](const ArbitrageOpportunity& opportunity) {
                this->OnOpportunityDetected(opportunity);
            }
        );
    }
    
    // Set up trade execution callback
    if (trade_executor_) {
        trade_executor_->SetExecutionCallback(
            [this](const ExecutionResult& result) {
                this->OnTradeCompleted(result);
            }
        );
    }
    
    // Set up exchanges for all components
    for (const auto& exchange : exchanges_) {
        if (price_monitor_) {
            price_monitor_->AddExchange(exchange.get());
        }
        if (trade_executor_) {
            trade_executor_->AddExchange(exchange->GetName(), exchange.get());
        }
        if (portfolio_manager_) {
            portfolio_manager_->AddExchange(exchange->GetName(), exchange.get());
        }
    }
}

void ArbitrageEngine::OnOpportunityDetected(const ArbitrageOpportunity& opportunity) {
    opportunities_found_++;
    
    try {
        LOG_INFO("Opportunity detected: {} {:.2f}% profit between {} and {}", 
                opportunity.symbol, opportunity.profit_percent,
                opportunity.buy_exchange, opportunity.sell_exchange);
        
        // Risk assessment
        if (!risk_manager_) {
            LOG_ERROR("Risk manager not available for opportunity assessment");
            return;
        }
        
        auto risk_assessment = risk_manager_->AssessOpportunity(opportunity);
        if (!risk_assessment.is_approved) {
            LOG_DEBUG("Opportunity rejected by risk manager: {}", 
                     risk_assessment.rejections.empty() ? "Unknown reason" : risk_assessment.rejections[0]);
            return;
        }
        
        // Calculate optimal position size
        double position_size = std::min(opportunity.max_volume, risk_assessment.position_size_limit);
        
        // Execute trade
        if (trade_executor_) {
            std::string trade_id = trade_executor_->ExecuteTrade(opportunity, position_size);
            if (!trade_id.empty()) {
                LOG_INFO("Trade execution started: {}", trade_id);
            } else {
                LOG_ERROR("Failed to start trade execution for {}", opportunity.symbol);
            }
        }
        
    } catch (const std::exception& e) {
        LOG_ERROR("Error processing opportunity: {}", e.what());
    }
}

void ArbitrageEngine::OnTradeCompleted(const ExecutionResult& result) {
    trades_executed_++;
    double current_profit = total_profit_.load();
    while (!total_profit_.compare_exchange_weak(current_profit, current_profit + result.realized_pnl)) {
        // Retry until successful
    }
    
    LOG_INFO("Trade {} completed: state={}, PnL=${:.2f}, execution_time={:.1f}ms",
             result.trade_id,
             static_cast<int>(result.final_state),
             result.realized_pnl,
             result.total_execution_time_ms);
    
    // Update risk manager
    if (risk_manager_) {
        if (result.final_state == TradeState::COMPLETED) {
            risk_manager_->RecordTradeComplete(result.trade_id, result.realized_pnl, result.total_fees);
        } else {
            risk_manager_->RecordTradeFailed(result.trade_id, 
                result.errors.empty() ? "Unknown error" : result.errors[0]);
        }
        
        // Update P&L
        risk_manager_->UpdatePnL(result.realized_pnl);
    }
}

std::unique_ptr<ExchangeInterface> ArbitrageEngine::CreateExchange(const ConfigManager::ExchangeConfig& config) {
    try {
        if (config.name == "binance") {
            auto exchange = std::make_unique<BinanceExchange>(config.api_key, config.secret_key);
            LOG_INFO("Created Binance exchange instance");
            return std::move(exchange);
        } else if (config.name == "upbit") {
            auto exchange = std::make_unique<UpbitExchange>(config.api_key, config.secret_key);
            LOG_INFO("Created Upbit exchange instance");
            return std::move(exchange);
        } else {
            LOG_ERROR("Unknown exchange type: {}", config.name);
            return nullptr;
        }
    } catch (const std::exception& e) {
        LOG_ERROR("Exception creating exchange {}: {}", config.name, e.what());
        return nullptr;
    }
}

} // namespace ats 
