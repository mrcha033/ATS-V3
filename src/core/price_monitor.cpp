#include "price_monitor.hpp"
#include "../utils/config_manager.hpp"
#include "../utils/logger.hpp"

namespace ats {

PriceMonitor::PriceMonitor(ConfigManager* config_manager)
    : config_manager_(config_manager), running_(false),
      total_updates_(0), websocket_updates_(0), rest_updates_(0), failed_updates_(0) {
    
    start_time_ = std::chrono::steady_clock::now();
}

PriceMonitor::~PriceMonitor() {
    Stop();
}

bool PriceMonitor::Initialize() {
    try {
        LOG_INFO("Initializing Price Monitor...");
        
        // Initialize price cache
        price_cache_ = std::make_unique<PriceCache>(1000, 100);
        
        // Initialize market data feed
        market_data_feed_ = std::make_unique<MarketDataFeed>();
        
        // Load configuration
        config_.symbols = config_manager_->GetTradingPairs();
        config_.update_interval = std::chrono::milliseconds(100);
        config_.use_websocket = true;
        config_.enable_caching = true;
        
        LOG_INFO("Price Monitor initialized for {} symbols", config_.symbols.size());
        return true;
        
    } catch (const std::exception& e) {
        LOG_ERROR("Failed to initialize Price Monitor: {}", e.what());
        return false;
    }
}

void PriceMonitor::Start() {
    if (running_.load()) {
        LOG_WARNING("Price Monitor is already running");
        return;
    }
    
    running_ = true;
    
    // Start monitoring threads
    monitor_thread_ = std::thread(&PriceMonitor::MonitorLoop, this);
    
    if (config_.use_websocket) {
        websocket_thread_ = std::thread(&PriceMonitor::WebSocketLoop, this);
    }
    
    LOG_INFO("Price Monitor started");
}

void PriceMonitor::Stop() {
    if (!running_.load()) {
        return;
    }
    
    running_ = false;
    
    // Join threads
    if (monitor_thread_.joinable()) {
        monitor_thread_.join();
    }
    
    if (websocket_thread_.joinable()) {
        websocket_thread_.join();
    }
    
    LOG_INFO("Price Monitor stopped");
}

void PriceMonitor::AddExchange(ExchangeInterface* exchange) {
    if (!exchange) {
        LOG_ERROR("Cannot add null exchange to Price Monitor");
        return;
    }
    
    std::lock_guard<std::mutex> lock(exchanges_mutex_);
    exchanges_.push_back(exchange);
    
    LOG_INFO("Added exchange {} to Price Monitor", exchange->GetName());
}

void PriceMonitor::RemoveExchange(const std::string& exchange_name) {
    std::lock_guard<std::mutex> lock(exchanges_mutex_);
    
    auto it = std::remove_if(exchanges_.begin(), exchanges_.end(),
        [&exchange_name](ExchangeInterface* exchange) {
            return exchange->GetName() == exchange_name;
        });
    
    if (it != exchanges_.end()) {
        exchanges_.erase(it, exchanges_.end());
        LOG_INFO("Removed exchange {} from Price Monitor", exchange_name);
    }
}

std::vector<std::string> PriceMonitor::GetActiveExchanges() const {
    std::lock_guard<std::mutex> lock(exchanges_mutex_);
    std::vector<std::string> names;
    
    for (const auto* exchange : exchanges_) {
        names.push_back(exchange->GetName());
    }
    
    return names;
}

void PriceMonitor::AddSymbol(const std::string& symbol) {
    auto it = std::find(config_.symbols.begin(), config_.symbols.end(), symbol);
    if (it == config_.symbols.end()) {
        config_.symbols.push_back(symbol);
        LOG_INFO("Added symbol {} to monitoring", symbol);
    }
}

void PriceMonitor::RemoveSymbol(const std::string& symbol) {
    auto it = std::find(config_.symbols.begin(), config_.symbols.end(), symbol);
    if (it != config_.symbols.end()) {
        config_.symbols.erase(it);
        LOG_INFO("Removed symbol {} from monitoring", symbol);
    }
}

std::vector<std::string> PriceMonitor::GetMonitoredSymbols() const {
    return config_.symbols;
}

bool PriceMonitor::GetLatestPrice(const std::string& exchange, const std::string& symbol, Price& price) {
    if (config_.enable_caching && price_cache_) {
        if (price_cache_->GetPrice(exchange, symbol, price)) {
            return true;
        }
    }
    
    return market_data_feed_->GetLatestPrice(exchange, symbol, price);
}

bool PriceMonitor::GetLatestOrderBook(const std::string& exchange, const std::string& symbol, OrderBook& orderbook) {
    if (config_.enable_caching && price_cache_) {
        if (price_cache_->GetOrderBook(exchange, symbol, orderbook)) {
            return true;
        }
    }
    
    return market_data_feed_->GetLatestOrderBook(exchange, symbol, orderbook);
}

PriceComparison PriceMonitor::ComparePrices(const std::string& symbol) {
    std::vector<std::string> exchanges = GetActiveExchanges();
    return market_data_feed_->ComparePrices(symbol, exchanges);
}

bool PriceMonitor::IsHealthy() const {
    if (!running_.load()) {
        return false;
    }
    
    // Check if we're receiving updates
    auto now = std::chrono::steady_clock::now();
    auto elapsed = std::chrono::duration_cast<std::chrono::seconds>(now - start_time_);
    
    if (elapsed.count() > 60 && total_updates_.load() == 0) {
        return false; // No updates in 60 seconds
    }
    
    return GetSuccessRate() > 50.0; // At least 50% success rate
}

std::string PriceMonitor::GetStatus() const {
    if (!running_.load()) {
        return "STOPPED";
    }
    
    if (IsHealthy()) {
        return "HEALTHY";
    }
    
    return "UNHEALTHY";
}

double PriceMonitor::GetUpdateRate() const {
    auto now = std::chrono::steady_clock::now();
    auto elapsed = std::chrono::duration_cast<std::chrono::seconds>(now - start_time_);
    
    if (elapsed.count() == 0) {
        return 0.0;
    }
    
    return static_cast<double>(total_updates_.load()) / elapsed.count();
}

double PriceMonitor::GetSuccessRate() const {
    long long total = total_updates_.load();
    if (total == 0) {
        return 0.0;
    }
    
    long long successful = total - failed_updates_.load();
    return static_cast<double>(successful) / total * 100.0;
}

void PriceMonitor::LogStatistics() const {
    LOG_INFO("=== Price Monitor Statistics ===");
    LOG_INFO("Total updates: {}", total_updates_.load());
    LOG_INFO("WebSocket updates: {}", websocket_updates_.load());
    LOG_INFO("REST updates: {}", rest_updates_.load());
    LOG_INFO("Failed updates: {}", failed_updates_.load());
    LOG_INFO("Update rate: {:.1f} updates/sec", GetUpdateRate());
    LOG_INFO("Success rate: {:.1f}%", GetSuccessRate());
    LOG_INFO("Active exchanges: {}", GetActiveExchanges().size());
    LOG_INFO("Monitored symbols: {}", config_.symbols.size());
}

void PriceMonitor::ResetStatistics() {
    total_updates_ = 0;
    websocket_updates_ = 0;
    rest_updates_ = 0;
    failed_updates_ = 0;
    start_time_ = std::chrono::steady_clock::now();
}

void PriceMonitor::MonitorLoop() {
    LOG_INFO("Price Monitor main loop started");
    
    while (running_.load()) {
        try {
            if (!config_.use_websocket || websocket_updates_.load() == 0) {
                // Use REST API fallback
                CollectPricesViaRest();
            }
            
            std::this_thread::sleep_for(config_.update_interval);
            
        } catch (const std::exception& e) {
            LOG_ERROR("Error in price monitor loop: {}", e.what());
            failed_updates_++;
            std::this_thread::sleep_for(std::chrono::seconds(1));
        }
    }
    
    LOG_INFO("Price Monitor main loop stopped");
}

void PriceMonitor::WebSocketLoop() {
    LOG_INFO("Price Monitor WebSocket loop started");
    
    // TODO: Implement WebSocket data collection
    // This will be implemented when WebSocket client is ready
    
    while (running_.load()) {
        std::this_thread::sleep_for(std::chrono::seconds(1));
    }
    
    LOG_INFO("Price Monitor WebSocket loop stopped");
}

void PriceMonitor::CollectPricesViaRest() {
    std::lock_guard<std::mutex> lock(exchanges_mutex_);
    
    for (auto* exchange : exchanges_) {
        if (!exchange->IsHealthy()) {
            continue;
        }
        
        for (const auto& symbol : config_.symbols) {
            try {
                Price price;
                if (exchange->GetPrice(symbol, price)) {
                    ProcessPriceUpdate(exchange->GetName(), price);
                    rest_updates_++;
                } else {
                    failed_updates_++;
                }
                
            } catch (const std::exception& e) {
                LOG_ERROR("Error getting price for {} from {}: {}", 
                         symbol, exchange->GetName(), e.what());
                failed_updates_++;
            }
        }
    }
}

void PriceMonitor::CollectPricesViaWebSocket() {
    // TODO: Implement WebSocket price collection
    LOG_DEBUG("WebSocket price collection - TODO");
}

void PriceMonitor::SetupWebSocketSubscriptions() {
    // TODO: Set up WebSocket subscriptions for all exchanges
    LOG_DEBUG("Setting up WebSocket subscriptions - TODO");
}

void PriceMonitor::ProcessPriceUpdate(const std::string& exchange, const Price& price) {
    total_updates_++;
    
    // Update cache
    if (config_.enable_caching && price_cache_) {
        price_cache_->SetPrice(exchange, price.symbol, price);
    }
    
    // Update market data feed
    if (market_data_feed_) {
        market_data_feed_->UpdatePrice(exchange, price);
    }
    
    // Call user callback if set
    if (price_callback_) {
        PriceUpdate update(exchange, price.symbol, price);
        price_callback_(update);
    }
    
    UpdateLastUpdateTime(exchange + ":" + price.symbol);
}

void PriceMonitor::ProcessOrderBookUpdate(const std::string& exchange, const OrderBook& orderbook) {
    // Update cache
    if (config_.enable_caching && price_cache_) {
        price_cache_->SetOrderBook(exchange, orderbook.symbol, orderbook);
    }
    
    // Update market data feed
    if (market_data_feed_) {
        market_data_feed_->UpdateOrderBook(exchange, orderbook);
    }
}

void PriceMonitor::UpdateLastUpdateTime(const std::string& key) {
    std::lock_guard<std::mutex> lock(update_times_mutex_);
    last_update_times_[key] = std::chrono::steady_clock::now();
}

// TODO: Implement remaining methods as needed for WebSocket functionality

bool PriceMonitor::ParseWebSocketMessage(const std::string& exchange, const std::string& message, 
                                        Price& price, OrderBook& orderbook) {
    // TODO: Implement exchange-specific message parsing
    return false;
}

void PriceMonitor::OnWebSocketMessage(const std::string& exchange, const std::string& message) {
    // TODO: Handle WebSocket messages
}

void PriceMonitor::OnWebSocketStateChange(const std::string& exchange, WebSocketState state) {
    // TODO: Handle WebSocket state changes
}

void PriceMonitor::OnWebSocketError(const std::string& exchange, const std::string& error) {
    LOG_ERROR("WebSocket error for {}: {}", exchange, error);
    failed_updates_++;
}

} // namespace ats 