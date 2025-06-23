#include "price_monitor.hpp"
#include "../utils/config_manager.hpp"
#include "../utils/logger.hpp"
#include "../exchange/exchange_interface.hpp"
#include "../network/websocket_client.hpp"
#include "../data/price_cache.hpp"
#include "../data/market_data.hpp"

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
    
    // Check for duplicates
    auto it = std::find_if(exchanges_.begin(), exchanges_.end(),
        [exchange](const ExchangeInterface* existing) {
            return existing->GetName() == exchange->GetName();
        });
    
    if (it != exchanges_.end()) {
        LOG_WARNING("Exchange {} already exists in Price Monitor, skipping", exchange->GetName());
        return;
    }
    
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
    
    // Set up WebSocket subscriptions for all exchanges
    SetupWebSocketSubscriptions();
    
    while (running_.load()) {
        try {
            // Check WebSocket health and reconnect if needed
            CheckWebSocketHealth();
            
            // Process any pending WebSocket messages
            ProcessWebSocketQueue();
            
            std::this_thread::sleep_for(std::chrono::milliseconds(100));
            
        } catch (const std::exception& e) {
            LOG_ERROR("Error in WebSocket loop: {}", e.what());
            failed_updates_++;
            std::this_thread::sleep_for(std::chrono::seconds(1));
        }
    }
    
    // Clean up WebSocket connections
    CleanupWebSocketConnections();
    
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
    std::lock_guard<std::mutex> lock(exchanges_mutex_);
    
    for (auto* exchange : exchanges_) {
        if (!exchange->IsHealthy()) {
            continue;
        }
        
        std::string exchange_name = exchange->GetName();
        auto ws_it = websocket_clients_.find(exchange_name);
        
        if (ws_it != websocket_clients_.end() && ws_it->second->IsConnected()) {
            // WebSocket is already handling data collection via callbacks
            continue;
        } else {
            // Fallback to REST if WebSocket is not available
            for (const auto& symbol : config_.symbols) {
                try {
                    Price price;
                    if (exchange->GetPrice(symbol, price)) {
                        ProcessPriceUpdate(exchange_name, price);
                        rest_updates_++;
                    } else {
                        failed_updates_++;
                    }
                } catch (const std::exception& e) {
                    LOG_ERROR("Error getting price for {} from {}: {}", 
                             symbol, exchange_name, e.what());
                    failed_updates_++;
                }
            }
        }
    }
}

void PriceMonitor::SetupWebSocketSubscriptions() {
    std::lock_guard<std::mutex> lock(exchanges_mutex_);
    
    for (auto* exchange : exchanges_) {
        std::string exchange_name = exchange->GetName();
        
        try {
            // Create WebSocket client for this exchange
            auto ws_client = std::make_unique<WebSocketClient>();
            
            // Configure WebSocket client
            ws_client->SetAutoReconnect(true, 5000);
            ws_client->SetMaxReconnectAttempts(10);
            ws_client->SetReconnectDelay(std::chrono::seconds(5));
            
            // Set callbacks
            ws_client->SetMessageCallback([this, exchange_name](const std::string& message) {
                OnWebSocketMessage(exchange_name, message);
            });
            
            ws_client->SetStateCallback([this, exchange_name](WebSocketState state) {
                OnWebSocketStateChange(exchange_name, state);
            });
            
            ws_client->SetErrorCallback([this, exchange_name](const std::string& error) {
                OnWebSocketError(exchange_name, error);
            });
            
            // Get WebSocket URL for this exchange
            std::string ws_url = GetWebSocketUrl(exchange_name);
            if (ws_url.empty()) {
                LOG_WARNING("No WebSocket URL available for exchange {}", exchange_name);
                continue;
            }
            
            // Connect to WebSocket
            if (ws_client->Connect(ws_url)) {
                websocket_clients_[exchange_name] = std::move(ws_client);
                
                // Subscribe to price updates for all symbols
                SubscribeToSymbols(exchange_name, config_.symbols);
                
                LOG_INFO("WebSocket subscription set up for {}", exchange_name);
            } else {
                LOG_ERROR("Failed to connect WebSocket for {}", exchange_name);
            }
            
        } catch (const std::exception& e) {
            LOG_ERROR("Error setting up WebSocket for {}: {}", exchange_name, e.what());
        }
    }
}

bool PriceMonitor::ParseWebSocketMessage(const std::string& exchange, const std::string& message, 
                                        Price& price, OrderBook& orderbook) {
    try {
        if (exchange == "binance") {
            return ParseBinanceMessage(message, price, orderbook);
        } else if (exchange == "upbit") {
            return ParseUpbitMessage(message, price, orderbook);
        } else {
            LOG_WARNING("Unknown exchange for WebSocket message parsing: {}", exchange);
            return false;
        }
    } catch (const std::exception& e) {
        LOG_ERROR("Error parsing WebSocket message from {}: {}", exchange, e.what());
        return false;
    }
}

void PriceMonitor::OnWebSocketMessage(const std::string& exchange, const std::string& message) {
    try {
        Price price;
        OrderBook orderbook;
        
        if (ParseWebSocketMessage(exchange, message, price, orderbook)) {
            // Process price update
            if (!price.symbol.empty()) {
                ProcessPriceUpdate(exchange, price);
                websocket_updates_++;
            }
            
            // Process order book update
            if (!orderbook.symbol.empty()) {
                ProcessOrderBookUpdate(exchange, orderbook);
            }
        }
        
    } catch (const std::exception& e) {
        LOG_ERROR("Error processing WebSocket message from {}: {}", exchange, e.what());
        failed_updates_++;
    }
}

void PriceMonitor::OnWebSocketStateChange(const std::string& exchange, WebSocketState state) {
    LOG_INFO("WebSocket state change for {}: {}", exchange, static_cast<int>(state));
    
    if (state == WebSocketState::CONNECTED) {
        // Re-subscribe to symbols after reconnection
        SubscribeToSymbols(exchange, config_.symbols);
    } else if (state == WebSocketState::DISCONNECTED || state == WebSocketState::ERROR) {
        // Handle disconnection - the WebSocket client will auto-reconnect
        LOG_WARNING("WebSocket disconnected for {}, will attempt reconnection", exchange);
    }
}

void PriceMonitor::OnWebSocketError(const std::string& exchange, const std::string& error) {
    LOG_ERROR("WebSocket error for {}: {}", exchange, error);
    failed_updates_++;
    
    // Optionally implement fallback to REST API
    HandleWebSocketFailure(exchange);
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

void PriceMonitor::CheckWebSocketHealth() {
    std::lock_guard<std::mutex> lock(exchanges_mutex_);
    
    for (const auto& pair : websocket_clients_) {
        const std::string& exchange_name = pair.first;
        const auto& ws_client = pair.second;
        
        if (!ws_client->IsHealthy()) {
            LOG_WARNING("WebSocket unhealthy for {}, attempting reconnection", exchange_name);
            ws_client->ForceReconnect();
        }
    }
}

void PriceMonitor::ProcessWebSocketQueue() {
    // Process any pending WebSocket operations
    // This could include queued subscription requests, etc.
    // For now, this is a placeholder for future enhancements
}

void PriceMonitor::CleanupWebSocketConnections() {
    std::lock_guard<std::mutex> lock(exchanges_mutex_);
    
    for (auto& pair : websocket_clients_) {
        const std::string& exchange_name = pair.first;
        auto& ws_client = pair.second;
        
        LOG_INFO("Cleaning up WebSocket connection for {}", exchange_name);
        ws_client->Disconnect();
    }
    
    websocket_clients_.clear();
}

void PriceMonitor::SubscribeToSymbols(const std::string& exchange, const std::vector<std::string>& symbols) {
    auto ws_it = websocket_clients_.find(exchange);
    if (ws_it == websocket_clients_.end() || !ws_it->second->IsConnected()) {
        LOG_WARNING("WebSocket not available for {}, cannot subscribe to symbols", exchange);
        return;
    }
    
    try {
        std::string subscription_msg = BuildSubscriptionMessage(exchange, symbols);
        if (!subscription_msg.empty()) {
            if (ws_it->second->SendMessage(subscription_msg)) {
                LOG_INFO("Subscribed to {} symbols on {}", symbols.size(), exchange);
            } else {
                LOG_ERROR("Failed to send subscription message to {}", exchange);
            }
        }
    } catch (const std::exception& e) {
        LOG_ERROR("Error subscribing to symbols on {}: {}", exchange, e.what());
    }
}

std::string PriceMonitor::GetWebSocketUrl(const std::string& exchange) const {
    if (exchange == "binance") {
        return "wss://stream.binance.com:9443/ws";
    } else if (exchange == "upbit") {
        return "wss://api.upbit.com/websocket/v1";
    }
    
    return "";
}

std::string PriceMonitor::BuildSubscriptionMessage(const std::string& exchange, const std::vector<std::string>& symbols) const {
    try {
        if (exchange == "binance") {
            // Build Binance WebSocket subscription message
            std::ostringstream oss;
            oss << "{\"method\":\"SUBSCRIBE\",\"params\":[";
            
            for (size_t i = 0; i < symbols.size(); ++i) {
                if (i > 0) oss << ",";
                std::string binance_symbol = ConvertSymbolToBinance(symbols[i]);
                oss << "\"" << binance_symbol << "@ticker\"";
                oss << ",\"" << binance_symbol << "@depth5\"";
            }
            
            oss << "],\"id\":1}";
            return oss.str();
            
        } else if (exchange == "upbit") {
            // Build Upbit WebSocket subscription message
            std::ostringstream oss;
            oss << "[{\"ticket\":\"ats-v3\"},{\"type\":\"ticker\",\"codes\":[";
            
            for (size_t i = 0; i < symbols.size(); ++i) {
                if (i > 0) oss << ",";
                std::string upbit_symbol = ConvertSymbolToUpbit(symbols[i]);
                oss << "\"" << upbit_symbol << "\"";
            }
            
            oss << "]}]";
            return oss.str();
        }
        
    } catch (const std::exception& e) {
        LOG_ERROR("Error building subscription message for {}: {}", exchange, e.what());
    }
    
    return "";
}

void PriceMonitor::HandleWebSocketFailure(const std::string& exchange) {
    LOG_WARNING("Handling WebSocket failure for {}, switching to REST fallback", exchange);
    
    // For now, the system will automatically fall back to REST API
    // Future implementation could include more sophisticated fallback strategies
}

bool PriceMonitor::ParseBinanceMessage(const std::string& message, Price& price, OrderBook& orderbook) {
    try {
        auto json = JsonParser::ParseString(message);
        
        // Check if this is a ticker update
        if (ats::json::HasPath(json, "s") && ats::json::HasPath(json, "c")) {
            std::string symbol = ats::json::AsString(ats::json::GetPath(json, "s"));
            price.symbol = ConvertSymbolFromBinance(symbol);
            price.last = std::stod(ats::json::AsString(ats::json::GetPath(json, "c")));
            
            if (ats::json::HasPath(json, "b")) {
                price.bid = std::stod(ats::json::AsString(ats::json::GetPath(json, "b")));
            }
            if (ats::json::HasPath(json, "a")) {
                price.ask = std::stod(ats::json::AsString(ats::json::GetPath(json, "a")));
            }
            if (ats::json::HasPath(json, "v")) {
                price.volume = std::stod(ats::json::AsString(ats::json::GetPath(json, "v")));
            }
            
            auto now = std::chrono::system_clock::now();
            price.timestamp = std::chrono::duration_cast<std::chrono::milliseconds>(now.time_since_epoch()).count();
            
            return true;
        }
        
    } catch (const std::exception& e) {
        LOG_ERROR("Error parsing Binance message: {}", e.what());
    }
    
    return false;
}

bool PriceMonitor::ParseUpbitMessage(const std::string& message, Price& price, OrderBook& orderbook) {
    try {
        auto json = JsonParser::ParseString(message);
        
        // Check if this is a ticker update
        if (ats::json::HasPath(json, "type") && 
            ats::json::AsString(ats::json::GetPath(json, "type")) == "ticker") {
            
            if (ats::json::HasPath(json, "code")) {
                std::string symbol = ats::json::AsString(ats::json::GetPath(json, "code"));
                price.symbol = ConvertSymbolFromUpbit(symbol);
                
                if (ats::json::HasPath(json, "trade_price")) {
                    price.last = ats::json::AsDouble(ats::json::GetPath(json, "trade_price"));
                }
                if (ats::json::HasPath(json, "highest_52_week_price")) {
                    // Upbit doesn't directly provide bid/ask in ticker, use last price as approximation
                    price.bid = price.last * 0.999;
                    price.ask = price.last * 1.001;
                }
                if (ats::json::HasPath(json, "acc_trade_volume_24h")) {
                    price.volume = ats::json::AsDouble(ats::json::GetPath(json, "acc_trade_volume_24h"));
                }
                
                auto now = std::chrono::system_clock::now();
                price.timestamp = std::chrono::duration_cast<std::chrono::milliseconds>(now.time_since_epoch()).count();
                
                return true;
            }
        }
        
    } catch (const std::exception& e) {
        LOG_ERROR("Error parsing Upbit message: {}", e.what());
    }
    
    return false;
}

std::string PriceMonitor::ConvertSymbolToBinance(const std::string& symbol) const {
    // Convert standard symbol format to Binance format
    // e.g., "BTC/USDT" -> "BTCUSDT"
    std::string binance_symbol = symbol;
    size_t pos = binance_symbol.find('/');
    if (pos != std::string::npos) {
        binance_symbol.erase(pos, 1);
    }
    std::transform(binance_symbol.begin(), binance_symbol.end(), binance_symbol.begin(), ::toupper);
    return binance_symbol;
}

std::string PriceMonitor::ConvertSymbolFromBinance(const std::string& symbol) const {
    // Convert Binance format back to standard format
    // This is a simplified conversion - real implementation would need a mapping table
    if (symbol.length() >= 6 && symbol.substr(symbol.length() - 4) == "USDT") {
        return symbol.substr(0, symbol.length() - 4) + "/USDT";
    } else if (symbol.length() >= 6 && symbol.substr(symbol.length() - 3) == "BTC") {
        return symbol.substr(0, symbol.length() - 3) + "/BTC";
    }
    return symbol;
}

std::string PriceMonitor::ConvertSymbolToUpbit(const std::string& symbol) const {
    // Convert standard symbol format to Upbit format
    // e.g., "BTC/KRW" -> "KRW-BTC"
    size_t pos = symbol.find('/');
    if (pos != std::string::npos) {
        std::string base = symbol.substr(0, pos);
        std::string quote = symbol.substr(pos + 1);
        return quote + "-" + base;
    }
    return symbol;
}

std::string PriceMonitor::ConvertSymbolFromUpbit(const std::string& symbol) const {
    // Convert Upbit format back to standard format
    // e.g., "KRW-BTC" -> "BTC/KRW"
    size_t pos = symbol.find('-');
    if (pos != std::string::npos) {
        std::string quote = symbol.substr(0, pos);
        std::string base = symbol.substr(pos + 1);
        return base + "/" + quote;
    }
    return symbol;
}

} // namespace ats 
