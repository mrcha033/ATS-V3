#pragma once

#include <memory>
#include <vector>
#include <thread>
#include <atomic>
#include <mutex>
#include <algorithm>

#include "../utils/config_manager.hpp"
#include "../exchange/exchange_interface.hpp"
#include "../core/types.hpp"

namespace ats {

class ArbitrageEngine {
private:
    ConfigManager* config_manager_;
    
    // Core components
    std::unique_ptr<PriceMonitor> price_monitor_;
    std::unique_ptr<OpportunityDetector> opportunity_detector_;
    std::unique_ptr<TradeExecutor> trade_executor_;
    std::unique_ptr<RiskManager> risk_manager_;
    std::unique_ptr<PortfolioManager> portfolio_manager_;
    
    // Exchange connections
    std::vector<std::unique_ptr<ExchangeInterface>> exchanges_;
    
    // Threading
    std::thread main_thread_;
    std::atomic<bool> running_;
    std::mutex engine_mutex_;
    
    // Statistics
    std::atomic<long long> opportunities_found_;
    std::atomic<long long> trades_executed_;
    std::atomic<double> total_profit_;
    
public:
    explicit ArbitrageEngine(ConfigManager* config_manager);
    ~ArbitrageEngine();
    
    // Lifecycle management
    bool Initialize();
    void Start();
    void Stop();
    bool IsRunning() const { return running_.load(); }
    
    // Exchange management
    bool AddExchange(std::unique_ptr<ExchangeInterface> exchange);
    std::vector<ExchangeInterface*> GetExchanges();
    ExchangeInterface* GetExchange(const std::string& name);
    
    // Statistics
    long long GetOpportunitiesFound() const { return opportunities_found_.load(); }
    long long GetTradesExecuted() const { return trades_executed_.load(); }
    double GetTotalProfit() const { return total_profit_.load(); }
    
    // Status and health
    bool IsHealthy() const;
    std::string GetStatus() const;
    
private:
    void MainLoop();
    bool InitializeExchanges();
    bool InitializeComponents();
    void ProcessOpportunity(const ArbitrageOpportunity& opportunity);
    void UpdateStatistics(const ArbitrageOpportunity& opportunity, bool executed);
    void PerformHealthChecks();
    
    // Exchange creation
    std::unique_ptr<ExchangeInterface> CreateExchange(const ConfigManager::ExchangeConfig& config);
    
    // Component integration
    void SetupComponentCallbacks();
    void OnOpportunityDetected(const ArbitrageOpportunity& opportunity);
    void OnTradeCompleted(const ExecutionResult& result);
};

} // namespace ats 