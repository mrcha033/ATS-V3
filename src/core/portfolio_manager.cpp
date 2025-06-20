#include "portfolio_manager.hpp"
#include "../utils/config_manager.hpp"
#include "../utils/logger.hpp"
#include "../exchange/exchange_interface.hpp"

namespace ats {

PortfolioManager::PortfolioManager(ConfigManager* config_manager)
    : config_manager_(config_manager), max_history_size_(1000),
      balance_update_interval_(std::chrono::seconds(30)),
      metrics_update_interval_(std::chrono::seconds(60)) {
    
    last_balance_update_ = std::chrono::system_clock::now();
    last_metrics_update_ = std::chrono::system_clock::now();
    last_price_update_ = std::chrono::system_clock::now();
}

bool PortfolioManager::Initialize() {
    try {
        LOG_INFO("Initializing Portfolio Manager...");
        
        // Initialize basic asset prices (simplified)
        asset_prices_usd_["BTC"] = 50000.0;
        asset_prices_usd_["ETH"] = 3000.0;
        asset_prices_usd_["USDT"] = 1.0;
        asset_prices_usd_["USDC"] = 1.0;
        
        LOG_INFO("Portfolio Manager initialized");
        return true;
        
    } catch (const std::exception& e) {
        LOG_ERROR("Failed to initialize Portfolio Manager: {}", e.what());
        return false;
    }
}

void PortfolioManager::UpdateAll() {
    auto now = std::chrono::system_clock::now();
    
    // Update balances if needed
    if (now - last_balance_update_ >= balance_update_interval_) {
        UpdateAllBalances();
        last_balance_update_ = now;
    }
    
    // Update metrics if needed
    if (now - last_metrics_update_ >= metrics_update_interval_) {
        UpdateMetrics();
        last_metrics_update_ = now;
    }
    
    // Update asset prices periodically
    if (now - last_price_update_ >= std::chrono::minutes(5)) {
        UpdateAssetPrices();
        last_price_update_ = now;
    }
}

void PortfolioManager::AddExchange(const std::string& name, ExchangeInterface* exchange) {
    std::lock_guard<std::mutex> lock(exchanges_mutex_);
    exchanges_[name] = exchange;
    LOG_INFO("Added exchange to portfolio manager: {}", name);
}

void PortfolioManager::RemoveExchange(const std::string& name) {
    std::lock_guard<std::mutex> lock(exchanges_mutex_);
    exchanges_.erase(name);
    LOG_INFO("Removed exchange from portfolio manager: {}", name);
}

std::vector<std::string> PortfolioManager::GetExchanges() const {
    std::lock_guard<std::mutex> lock(exchanges_mutex_);
    std::vector<std::string> names;
    for (const auto& pair : exchanges_) {
        names.push_back(pair.first);
    }
    return names;
}

void PortfolioManager::UpdateBalance(const std::string& exchange, const std::string& asset, 
                                    double available, double locked) {
    std::lock_guard<std::mutex> lock(balances_mutex_);
    
    std::string key = MakeBalanceKey(exchange, asset);
    AssetBalance& balance = balances_[key];
    
    balance.exchange = exchange;
    balance.asset = asset;
    balance.available = available;
    balance.locked = locked;
    balance.total = available + locked;
    balance.usd_value = ConvertToUSD(asset, balance.total);
    balance.last_update = std::chrono::system_clock::now();
    
    LOG_DEBUG("Updated balance: {} {} = {:.6f} (${:.2f})", 
             exchange, asset, balance.total, balance.usd_value);
}

void PortfolioManager::UpdateAllBalances() {
    std::lock_guard<std::mutex> lock(exchanges_mutex_);
    
    for (const auto& exchange_pair : exchanges_) {
        UpdateExchangeBalances(exchange_pair.first);
    }
}

void PortfolioManager::UpdateExchangeBalances(const std::string& exchange) {
    auto exchange_interface = GetExchange(exchange);
    if (!exchange_interface) {
        LOG_WARNING("Exchange {} not found for balance update", exchange);
        return;
    }
    
    // TODO: Get actual balances from exchange
    // For now, simulate some balances
    if (exchange == "binance") {
        UpdateBalance(exchange, "BTC", 0.1, 0.0);
        UpdateBalance(exchange, "ETH", 1.5, 0.0);
        UpdateBalance(exchange, "USDT", 5000.0, 0.0);
    } else if (exchange == "upbit") {
        UpdateBalance(exchange, "BTC", 0.05, 0.0);
        UpdateBalance(exchange, "ETH", 0.8, 0.0);
        UpdateBalance(exchange, "USDT", 3000.0, 0.0);
    }
}

AssetBalance PortfolioManager::GetBalance(const std::string& exchange, const std::string& asset) const {
    std::lock_guard<std::mutex> lock(balances_mutex_);
    
    std::string key = MakeBalanceKey(exchange, asset);
    auto it = balances_.find(key);
    return (it != balances_.end()) ? it->second : AssetBalance();
}

double PortfolioManager::GetTotalAssetBalance(const std::string& asset) const {
    std::lock_guard<std::mutex> lock(balances_mutex_);
    
    double total = 0.0;
    for (const auto& pair : balances_) {
        if (pair.second.asset == asset) {
            total += pair.second.total;
        }
    }
    return total;
}

double PortfolioManager::GetAvailableBalance(const std::string& exchange, const std::string& asset) const {
    auto balance = GetBalance(exchange, asset);
    return balance.available;
}

double PortfolioManager::GetTotalAvailableBalance(const std::string& asset) const {
    std::lock_guard<std::mutex> lock(balances_mutex_);
    
    double total = 0.0;
    for (const auto& pair : balances_) {
        if (pair.second.asset == asset) {
            total += pair.second.available;
        }
    }
    return total;
}

std::vector<AssetBalance> PortfolioManager::GetAllBalances() const {
    std::lock_guard<std::mutex> lock(balances_mutex_);
    
    std::vector<AssetBalance> balances;
    for (const auto& pair : balances_) {
        balances.push_back(pair.second);
    }
    return balances;
}

std::vector<AssetBalance> PortfolioManager::GetExchangeBalances(const std::string& exchange) const {
    std::lock_guard<std::mutex> lock(balances_mutex_);
    
    std::vector<AssetBalance> balances;
    for (const auto& pair : balances_) {
        if (pair.second.exchange == exchange) {
            balances.push_back(pair.second);
        }
    }
    return balances;
}

std::vector<AssetBalance> PortfolioManager::GetAssetBalances(const std::string& asset) const {
    std::lock_guard<std::mutex> lock(balances_mutex_);
    
    std::vector<AssetBalance> balances;
    for (const auto& pair : balances_) {
        if (pair.second.asset == asset) {
            balances.push_back(pair.second);
        }
    }
    return balances;
}

double PortfolioManager::GetPortfolioValueUSD() const {
    std::lock_guard<std::mutex> lock(balances_mutex_);
    
    double total_value = 0.0;
    for (const auto& pair : balances_) {
        total_value += pair.second.usd_value;
    }
    return total_value;
}

double PortfolioManager::GetExchangeValueUSD(const std::string& exchange) const {
    std::lock_guard<std::mutex> lock(balances_mutex_);
    
    double total_value = 0.0;
    for (const auto& pair : balances_) {
        if (pair.second.exchange == exchange) {
            total_value += pair.second.usd_value;
        }
    }
    return total_value;
}

double PortfolioManager::GetAssetValueUSD(const std::string& asset) const {
    double total_balance = GetTotalAssetBalance(asset);
    return ConvertToUSD(asset, total_balance);
}

void PortfolioManager::UpdateAssetPrices() {
    std::lock_guard<std::mutex> lock(prices_mutex_);
    
    // TODO: Get real-time prices from price monitor or exchanges
    // For now, simulate some price updates
    asset_prices_usd_["BTC"] *= (0.99 + (rand() % 20) / 1000.0); // ±1% variation
    asset_prices_usd_["ETH"] *= (0.99 + (rand() % 20) / 1000.0);
    
    LOG_DEBUG("Updated asset prices: BTC=${:.2f}, ETH=${:.2f}", 
             asset_prices_usd_["BTC"], asset_prices_usd_["ETH"]);
}

void PortfolioManager::SetAllocationTarget(const std::string& asset, const AllocationTarget& target) {
    std::lock_guard<std::mutex> lock(targets_mutex_);
    allocation_targets_[asset] = target;
    LOG_INFO("Set allocation target for {}: {:.1f}%", asset, target.target_percentage);
}

AllocationTarget PortfolioManager::GetAllocationTarget(const std::string& asset) const {
    std::lock_guard<std::mutex> lock(targets_mutex_);
    auto it = allocation_targets_.find(asset);
    return (it != allocation_targets_.end()) ? it->second : AllocationTarget();
}

void PortfolioManager::RemoveAllocationTarget(const std::string& asset) {
    std::lock_guard<std::mutex> lock(targets_mutex_);
    allocation_targets_.erase(asset);
    LOG_INFO("Removed allocation target for {}", asset);
}

double PortfolioManager::GetCurrentAllocation(const std::string& asset) const {
    double asset_value = GetAssetValueUSD(asset);
    double portfolio_value = GetPortfolioValueUSD();
    
    return (portfolio_value > 0) ? (asset_value / portfolio_value) * 100.0 : 0.0;
}

std::unordered_map<std::string, double> PortfolioManager::GetAllAllocations() const {
    std::unordered_map<std::string, double> allocations;
    
    auto assets = GetAvailableAssets();
    for (const auto& asset : assets) {
        allocations[asset] = GetCurrentAllocation(asset);
    }
    
    return allocations;
}

std::vector<RebalanceAction> PortfolioManager::GetRebalanceRecommendations() const {
    std::vector<RebalanceAction> actions;
    
    std::lock_guard<std::mutex> lock(targets_mutex_);
    
    for (const auto& target_pair : allocation_targets_) {
        const auto& asset = target_pair.first;
        const auto& target = target_pair.second;
        
        if (!target.enable_rebalancing) continue;
        
        double current_allocation = GetCurrentAllocation(asset);
        double deviation = std::abs(current_allocation - target.target_percentage);
        
        if (deviation > target.rebalance_threshold) {
            auto action = CreateRebalanceAction(asset, target, current_allocation);
            actions.push_back(action);
        }
    }
    
    return actions;
}

bool PortfolioManager::IsRebalancingNeeded() const {
    auto recommendations = GetRebalanceRecommendations();
    return !recommendations.empty();
}

void PortfolioManager::ExecuteRebalancing(const std::vector<RebalanceAction>& actions) {
    LOG_INFO("Executing {} rebalancing actions", actions.size());
    
    for (const auto& action : actions) {
        LOG_INFO("Rebalance: {} {:.6f} {} from {} to {} (${:.2f})",
                action.asset, action.amount, action.from_exchange, 
                action.to_exchange, action.usd_value);
        
        // TODO: Implement actual rebalancing logic
        // This would involve transferring assets between exchanges
    }
}

PortfolioMetrics PortfolioManager::GetMetrics() const {
    std::lock_guard<std::mutex> lock(metrics_mutex_);
    return current_metrics_;
}

void PortfolioManager::UpdateMetrics() {
    std::lock_guard<std::mutex> lock(metrics_mutex_);
    UpdateMetricsInternal();
}

double PortfolioManager::GetTotalPnL() const {
    // TODO: Calculate P&L from historical data
    return 0.0;
}

double PortfolioManager::GetDailyPnL() const {
    return CalculateDailyPnL();
}

double PortfolioManager::GetMaxDrawdown() const {
    return CalculateMaxDrawdownInternal();
}

double PortfolioManager::CalculatePortfolioRisk() const {
    // Simple risk calculation based on allocation concentration
    auto allocations = GetAllAllocations();
    
    double risk_score = 0.0;
    for (const auto& allocation : allocations) {
        if (allocation.second > 50.0) { // High concentration risk
            risk_score += 0.3;
        } else if (allocation.second > 30.0) {
            risk_score += 0.1;
        }
    }
    
    return std::min(1.0, risk_score);
}

double PortfolioManager::CalculateConcentrationRisk() const {
    auto allocations = GetAllAllocations();
    
    // Calculate Herfindahl-Hirschman Index
    double hhi = 0.0;
    for (const auto& allocation : allocations) {
        double share = allocation.second / 100.0;
        hhi += share * share;
    }
    
    return hhi; // 0 = perfectly diversified, 1 = single asset
}

double PortfolioManager::CalculateExchangeRisk() const {
    std::unordered_map<std::string, double> exchange_values;
    
    auto exchanges = GetExchanges();
    for (const auto& exchange : exchanges) {
        exchange_values[exchange] = GetExchangeValueUSD(exchange);
    }
    
    double total_value = GetPortfolioValueUSD();
    double max_exposure = 0.0;
    
    for (const auto& pair : exchange_values) {
        double exposure = (total_value > 0) ? pair.second / total_value : 0.0;
        max_exposure = std::max(max_exposure, exposure);
    }
    
    return max_exposure;
}

std::unordered_map<std::string, double> PortfolioManager::GetRiskByAsset() const {
    std::unordered_map<std::string, double> risk_by_asset;
    
    auto allocations = GetAllAllocations();
    for (const auto& allocation : allocations) {
        // Simple risk calculation: higher allocation = higher risk
        risk_by_asset[allocation.first] = allocation.second / 100.0;
    }
    
    return risk_by_asset;
}

void PortfolioManager::RecordSnapshot() {
    std::lock_guard<std::mutex> lock(history_mutex_);
    
    HistoricalSnapshot snapshot;
    snapshot.total_value = GetPortfolioValueUSD();
    snapshot.timestamp = std::chrono::system_clock::now();
    
    value_history_.push_back(snapshot);
    
    // Keep only recent history
    if (value_history_.size() > max_history_size_) {
        value_history_.erase(value_history_.begin());
    }
}

std::vector<PortfolioManager::HistoricalSnapshot> PortfolioManager::GetValueHistory(size_t days) const {
    std::lock_guard<std::mutex> lock(history_mutex_);
    
    auto cutoff = std::chrono::system_clock::now() - std::chrono::hours(24 * days);
    std::vector<HistoricalSnapshot> recent_history;
    
    for (const auto& snapshot : value_history_) {
        if (snapshot.timestamp >= cutoff) {
            recent_history.push_back(snapshot);
        }
    }
    
    return recent_history;
}

double PortfolioManager::CalculateVolatility(size_t days) const {
    return CalculateVolatilityInternal(days);
}

double PortfolioManager::CalculateSharpeRatio(size_t days) const {
    return CalculateSharpeRatioInternal(days);
}

bool PortfolioManager::HasSufficientBalance(const std::string& exchange, 
                                           const std::string& asset, 
                                           double amount) const {
    double available = GetAvailableBalance(exchange, asset);
    return available >= amount;
}

double PortfolioManager::GetMaxTradeAmount(const std::string& exchange, const std::string& asset) const {
    return GetAvailableBalance(exchange, asset);
}

std::vector<std::string> PortfolioManager::GetAvailableAssets() const {
    std::lock_guard<std::mutex> lock(balances_mutex_);
    
    std::set<std::string> unique_assets;
    for (const auto& pair : balances_) {
        if (pair.second.total > 0) { // Only assets with positive balance
            unique_assets.insert(pair.second.asset);
        }
    }
    
    return std::vector<std::string>(unique_assets.begin(), unique_assets.end());
}

bool PortfolioManager::ValidateBalances() const {
    std::lock_guard<std::mutex> lock(balances_mutex_);
    
    for (const auto& pair : balances_) {
        const auto& balance = pair.second;
        
        // Check for negative balances
        if (balance.available < 0 || balance.locked < 0 || balance.total < 0) {
            return false;
        }
        
        // Check consistency
        if (std::abs(balance.total - (balance.available + balance.locked)) > 1e-8) {
            return false;
        }
    }
    
    return true;
}

std::vector<std::string> PortfolioManager::GetBalanceWarnings() const {
    std::vector<std::string> warnings;
    
    std::lock_guard<std::mutex> lock(balances_mutex_);
    
    auto now = std::chrono::system_clock::now();
    auto stale_threshold = std::chrono::minutes(10);
    
    for (const auto& pair : balances_) {
        const auto& balance = pair.second;
        
        // Check for stale data
        if (balance.IsStale(stale_threshold)) {
            warnings.push_back("Stale balance data for " + balance.exchange + ":" + balance.asset);
        }
        
        // Check for zero balances
        if (balance.total == 0) {
            warnings.push_back("Zero balance for " + balance.exchange + ":" + balance.asset);
        }
    }
    
    return warnings;
}

void PortfolioManager::LogPortfolioSummary() const {
    LOG_INFO("=== Portfolio Summary ===");
    LOG_INFO("Total portfolio value: ${:.2f}", GetPortfolioValueUSD());
    
    auto allocations = GetAllAllocations();
    for (const auto& allocation : allocations) {
        LOG_INFO("{}: {:.1f}% (${:.2f})", allocation.first, allocation.second, 
                GetAssetValueUSD(allocation.first));
    }
    
    auto exchanges = GetExchanges();
    for (const auto& exchange : exchanges) {
        LOG_INFO("{} value: ${:.2f}", exchange, GetExchangeValueUSD(exchange));
    }
    
    LOG_INFO("Portfolio risk: {:.2f}", CalculatePortfolioRisk());
    LOG_INFO("Concentration risk: {:.2f}", CalculateConcentrationRisk());
    LOG_INFO("Exchange risk: {:.2f}", CalculateExchangeRisk());
}

bool PortfolioManager::IsHealthy() const {
    return ValidateBalances() && 
           GetPortfolioValueUSD() > 0 &&
           CalculatePortfolioRisk() < 0.8;
}

std::string PortfolioManager::GetStatus() const {
    if (!ValidateBalances()) {
        return "INVALID_BALANCES";
    }
    
    if (GetPortfolioValueUSD() <= 0) {
        return "ZERO_VALUE";
    }
    
    if (CalculatePortfolioRisk() > 0.8) {
        return "HIGH_RISK";
    }
    
    return "HEALTHY";
}

// Private helper methods
std::string PortfolioManager::MakeBalanceKey(const std::string& exchange, const std::string& asset) const {
    return exchange + ":" + asset;
}

double PortfolioManager::ConvertToUSD(const std::string& asset, double amount) const {
    std::lock_guard<std::mutex> lock(prices_mutex_);
    
    auto it = asset_prices_usd_.find(asset);
    if (it != asset_prices_usd_.end()) {
        return amount * it->second;
    }
    
    // Default to 1:1 if price not found
    return amount;
}

void PortfolioManager::UpdateMetricsInternal() {
    current_metrics_.total_value_usd = GetPortfolioValueUSD();
    current_metrics_.daily_pnl = CalculateDailyPnL();
    current_metrics_.max_drawdown = CalculateMaxDrawdownInternal();
    current_metrics_.volatility = CalculateVolatilityInternal(30);
    current_metrics_.sharpe_ratio = CalculateSharpeRatioInternal(30);
    
    // Update allocations
    current_metrics_.asset_allocations = GetAllAllocations();
    
    auto exchanges = GetExchanges();
    for (const auto& exchange : exchanges) {
        double value = GetExchangeValueUSD(exchange);
        double percentage = (current_metrics_.total_value_usd > 0) ? 
                           (value / current_metrics_.total_value_usd) * 100.0 : 0.0;
        current_metrics_.exchange_allocations[exchange] = percentage;
    }
    
    current_metrics_.last_update = std::chrono::system_clock::now();
}

double PortfolioManager::CalculateDailyPnL() const {
    // TODO: Implement based on historical snapshots
    return 0.0;
}

double PortfolioManager::CalculateMaxDrawdownInternal() const {
    std::lock_guard<std::mutex> lock(history_mutex_);
    
    if (value_history_.size() < 2) return 0.0;
    
    double max_value = 0.0;
    double max_drawdown = 0.0;
    
    for (const auto& snapshot : value_history_) {
        max_value = std::max(max_value, snapshot.total_value);
        double drawdown = (max_value - snapshot.total_value) / max_value;
        max_drawdown = std::max(max_drawdown, drawdown);
    }
    
    return max_drawdown;
}

double PortfolioManager::CalculateVolatilityInternal(size_t days) const {
    auto history = GetValueHistory(days);
    if (history.size() < 2) return 0.0;
    
    // Calculate daily returns
    std::vector<double> returns;
    for (size_t i = 1; i < history.size(); ++i) {
        if (history[i-1].total_value > 0) {
            double return_pct = (history[i].total_value - history[i-1].total_value) / history[i-1].total_value;
            returns.push_back(return_pct);
        }
    }
    
    if (returns.empty()) return 0.0;
    
    // Calculate standard deviation
    double mean = 0.0;
    for (double ret : returns) {
        mean += ret;
    }
    mean /= returns.size();
    
    double variance = 0.0;
    for (double ret : returns) {
        variance += (ret - mean) * (ret - mean);
    }
    variance /= returns.size();
    
    return std::sqrt(variance);
}

double PortfolioManager::CalculateSharpeRatioInternal(size_t days) const {
    // TODO: Implement Sharpe ratio calculation
    return 0.0;
}

RebalanceAction PortfolioManager::CreateRebalanceAction(const std::string& asset,
                                                       const AllocationTarget& target,
                                                       double current_allocation) const {
    RebalanceAction action;
    action.asset = asset;
    action.reason = "Allocation deviation: " + std::to_string(current_allocation - target.target_percentage) + "%";
    action.priority = CalculateRebalancePriority(asset, current_allocation - target.target_percentage);
    
    // TODO: Implement actual rebalancing logic
    // This would determine the optimal exchanges to move assets between
    
    return action;
}

double PortfolioManager::CalculateRebalancePriority(const std::string& asset, double deviation) const {
    return std::abs(deviation); // Higher deviation = higher priority
}

ExchangeInterface* PortfolioManager::GetExchange(const std::string& name) const {
    std::lock_guard<std::mutex> lock(exchanges_mutex_);
    auto it = exchanges_.find(name);
    return (it != exchanges_.end()) ? it->second : nullptr;
}

} // namespace ats 