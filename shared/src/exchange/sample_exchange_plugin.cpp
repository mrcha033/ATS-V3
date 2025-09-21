#include "exchange/base_exchange_plugin.hpp"
#include "exchange/exchange_plugin_manager.hpp"
#include <random>
#include <thread>
#include <chrono>
#include <set>
#include <map>
#include <algorithm>

namespace ats {
namespace exchange {

// Sample exchange plugin for demonstration purposes
// This plugin simulates a fictional exchange called "DemoExchange"
class SampleExchangePlugin : public BaseExchangePlugin {
public:
    SampleExchangePlugin();
    ~SampleExchangePlugin() override;
    
    ExchangePluginMetadata get_metadata() const override;
    
protected:
    // BaseExchangePlugin abstract method implementations
    ExchangePluginMetadata create_metadata() const override;
    bool do_connect() override;
    void do_disconnect() override;
    bool do_subscribe_ticker(const std::string& symbol) override;
    bool do_subscribe_orderbook(const std::string& symbol, int depth) override;
    bool do_subscribe_trades(const std::string& symbol) override;
    bool do_unsubscribe_ticker(const std::string& symbol) override;
    bool do_unsubscribe_orderbook(const std::string& symbol) override;
    bool do_unsubscribe_trades(const std::string& symbol) override;
    bool do_unsubscribe_all() override;
    std::vector<types::Ticker> do_get_all_tickers() override;
    types::Ticker do_get_ticker(const std::string& symbol) override;
    std::vector<std::string> do_get_supported_symbols() override;
    types::OrderBook do_get_orderbook(const std::string& symbol, int depth) override;
    
    // Optional overrides
    bool do_initialize(const types::ExchangeConfig& config) override;
    bool do_start() override;
    void do_stop() override;
    void do_cleanup() override;
    
private:
    // Simulation data and state
    std::atomic<bool> simulation_running_;
    std::unique_ptr<std::thread> simulation_thread_;
    std::mutex subscriptions_mutex_;
    std::set<std::string> ticker_subscriptions_;
    std::set<std::string> orderbook_subscriptions_;
    std::set<std::string> trade_subscriptions_;
    
    // Price simulation
    std::map<std::string, double> current_prices_;
    std::random_device random_device_;
    std::mt19937 random_generator_;
    std::uniform_real_distribution<double> price_change_dist_;
    
    // Configuration
    bool simulate_connection_issues_;
    std::chrono::milliseconds update_interval_;
    
    // Helper methods
    void start_price_simulation();
    void stop_price_simulation();
    void simulate_market_data();
    void generate_ticker_update(const std::string& symbol);
    void generate_orderbook_update(const std::string& symbol, int depth);
    void generate_trade_update(const std::string& symbol);
    double get_random_price_change();
    types::Ticker create_sample_ticker(const std::string& symbol, double price);
    types::OrderBook create_sample_orderbook(const std::string& symbol, double price, int depth);
    types::Trade create_sample_trade(const std::string& symbol, double price);
    
    // Static configuration
    static const std::vector<std::string> SUPPORTED_SYMBOLS;
    static const std::map<std::string, double> INITIAL_PRICES;
};

// Static member definitions
const std::vector<std::string> SampleExchangePlugin::SUPPORTED_SYMBOLS = {
    "BTCUSDT", "ETHUSDT", "ADAUSDT", "DOTUSDT", "LINKUSDT",
    "LTCUSDT", "XRPUSDT", "BCHUSDT", "EOSUSDT", "XLMUSDT"
};

const std::map<std::string, double> SampleExchangePlugin::INITIAL_PRICES = {
    {"BTCUSDT", 45000.0},
    {"ETHUSDT", 3200.0},
    {"ADAUSDT", 1.2},
    {"DOTUSDT", 35.0},
    {"LINKUSDT", 28.0},
    {"LTCUSDT", 180.0},
    {"XRPUSDT", 0.85},
    {"BCHUSDT", 450.0},
    {"EOSUSDT", 4.5},
    {"XLMUSDT", 0.35}
};

SampleExchangePlugin::SampleExchangePlugin()
    : simulation_running_(false)
    , current_prices_(INITIAL_PRICES)
    , random_generator_(random_device_())
    , price_change_dist_(-0.02, 0.02)  // +/- 2% price changes
    , simulate_connection_issues_(false)
    , update_interval_(std::chrono::milliseconds(1000)) {  // 1 second updates
}

SampleExchangePlugin::~SampleExchangePlugin() {
    stop_price_simulation();
}

ExchangePluginMetadata SampleExchangePlugin::get_metadata() const {
    return create_metadata();
}

ExchangePluginMetadata SampleExchangePlugin::create_metadata() const {
    return create_plugin_metadata<SampleExchangePlugin>(
        "sample_exchange",                    // plugin_id
        "Sample Exchange Plugin",             // plugin_name  
        "1.0.0",                             // version
        "Demonstration exchange plugin with simulated market data", // description
        "ATS Development Team",               // author
        SUPPORTED_SYMBOLS,                    // supported_symbols
        "https://api.sample-exchange.com",   // api_base_url
        "wss://stream.sample-exchange.com",  // websocket_url
        true,                                // supports_rest_api
        true,                                // supports_websocket
        true,                                // supports_orderbook
        true,                                // supports_trades
        1200                                 // rate_limit_per_minute
    );
}

bool SampleExchangePlugin::do_initialize(const types::ExchangeConfig& config) {
    log_info("Initializing sample exchange plugin");
    
    // Check for simulation configuration
    auto it = config.parameters.find("simulate_connection_issues");
    if (it != config.parameters.end()) {
        simulate_connection_issues_ = (it->second == "true");
    }
    
    it = config.parameters.find("update_interval_ms");
    if (it != config.parameters.end()) {
        int interval_ms = std::stoi(it->second);
        update_interval_ = std::chrono::milliseconds(interval_ms);
    }
    
    log_info("Sample exchange plugin initialized with simulation settings");
    return true;
}

bool SampleExchangePlugin::do_start() {
    log_info("Starting sample exchange plugin");
    start_price_simulation();
    return true;
}

void SampleExchangePlugin::do_stop() {
    log_info("Stopping sample exchange plugin");
    stop_price_simulation();
}

void SampleExchangePlugin::do_cleanup() {
    log_info("Cleaning up sample exchange plugin");
    stop_price_simulation();
}

bool SampleExchangePlugin::do_connect() {
    log_info("Connecting to sample exchange");
    
    // Simulate connection delay
    std::this_thread::sleep_for(std::chrono::milliseconds(500));
    
    // Simulate occasional connection failures
    if (simulate_connection_issues_) {
        std::uniform_real_distribution<double> failure_dist(0.0, 1.0);
        if (failure_dist(random_generator_) < 0.1) {  // 10% failure rate
            log_error("Simulated connection failure");
            return false;
        }
    }
    
    log_info("Connected to sample exchange successfully");
    return true;
}

void SampleExchangePlugin::do_disconnect() {
    log_info("Disconnecting from sample exchange");
    std::this_thread::sleep_for(std::chrono::milliseconds(100));
    log_info("Disconnected from sample exchange");
}

bool SampleExchangePlugin::do_subscribe_ticker(const std::string& symbol) {
    if (!is_connected()) {
        return false;
    }
    
    std::lock_guard<std::mutex> lock(subscriptions_mutex_);
    ticker_subscriptions_.insert(symbol);
    
    log_info("Subscribed to ticker: " + symbol);
    return true;
}

bool SampleExchangePlugin::do_subscribe_orderbook(const std::string& symbol, int depth) {
    if (!is_connected()) {
        return false;
    }
    
    std::lock_guard<std::mutex> lock(subscriptions_mutex_);
    orderbook_subscriptions_.insert(symbol);
    
    log_info("Subscribed to orderbook: " + symbol + " (depth: " + std::to_string(depth) + ")");
    return true;
}

bool SampleExchangePlugin::do_subscribe_trades(const std::string& symbol) {
    if (!is_connected()) {
        return false;
    }
    
    std::lock_guard<std::mutex> lock(subscriptions_mutex_);
    trade_subscriptions_.insert(symbol);
    
    log_info("Subscribed to trades: " + symbol);
    return true;
}

bool SampleExchangePlugin::do_unsubscribe_ticker(const std::string& symbol) {
    std::lock_guard<std::mutex> lock(subscriptions_mutex_);
    ticker_subscriptions_.erase(symbol);
    
    log_info("Unsubscribed from ticker: " + symbol);
    return true;
}

bool SampleExchangePlugin::do_unsubscribe_orderbook(const std::string& symbol) {
    std::lock_guard<std::mutex> lock(subscriptions_mutex_);
    orderbook_subscriptions_.erase(symbol);
    
    log_info("Unsubscribed from orderbook: " + symbol);
    return true;
}

bool SampleExchangePlugin::do_unsubscribe_trades(const std::string& symbol) {
    std::lock_guard<std::mutex> lock(subscriptions_mutex_);
    trade_subscriptions_.erase(symbol);
    
    log_info("Unsubscribed from trades: " + symbol);
    return true;
}

bool SampleExchangePlugin::do_unsubscribe_all() {
    std::lock_guard<std::mutex> lock(subscriptions_mutex_);
    ticker_subscriptions_.clear();
    orderbook_subscriptions_.clear();
    trade_subscriptions_.clear();
    
    log_info("Unsubscribed from all streams");
    return true;
}

std::vector<types::Ticker> SampleExchangePlugin::do_get_all_tickers() {
    log_info("Fetching all tickers");
    
    std::vector<types::Ticker> tickers;
    tickers.reserve(SUPPORTED_SYMBOLS.size());
    
    for (const auto& symbol : SUPPORTED_SYMBOLS) {
        auto it = current_prices_.find(symbol);
        if (it != current_prices_.end()) {
            tickers.push_back(create_sample_ticker(symbol, it->second));
        }
    }
    
    return tickers;
}

types::Ticker SampleExchangePlugin::do_get_ticker(const std::string& symbol) {
    log_info("Fetching ticker for: " + symbol);
    
    auto it = current_prices_.find(symbol);
    if (it != current_prices_.end()) {
        return create_sample_ticker(symbol, it->second);
    }
    
    return {};
}

std::vector<std::string> SampleExchangePlugin::do_get_supported_symbols() {
    return SUPPORTED_SYMBOLS;
}

types::OrderBook SampleExchangePlugin::do_get_orderbook(const std::string& symbol, int depth) {
    log_info("Fetching orderbook for: " + symbol + " (depth: " + std::to_string(depth) + ")");
    
    auto it = current_prices_.find(symbol);
    if (it != current_prices_.end()) {
        return create_sample_orderbook(symbol, it->second, depth);
    }
    
    return {};
}

// Private helper methods

void SampleExchangePlugin::start_price_simulation() {
    if (simulation_running_) {
        return;
    }
    
    simulation_running_ = true;
    simulation_thread_ = std::make_unique<std::thread>(&SampleExchangePlugin::simulate_market_data, this);
    
    log_info("Started price simulation");
}

void SampleExchangePlugin::stop_price_simulation() {
    if (!simulation_running_) {
        return;
    }
    
    simulation_running_ = false;
    
    if (simulation_thread_ && simulation_thread_->joinable()) {
        simulation_thread_->join();
        simulation_thread_.reset();
    }
    
    log_info("Stopped price simulation");
}

void SampleExchangePlugin::simulate_market_data() {
    while (simulation_running_) {
        if (is_connected()) {
            // Update prices
            for (auto& pair : current_prices_) {
                double change = get_random_price_change();
                pair.second = pair.second * (1.0 + change);
                pair.second = (pair.second < 0.01) ? 0.01 : pair.second;  // Minimum price
            }
            
            // Generate updates for subscribed symbols
            std::lock_guard<std::mutex> lock(subscriptions_mutex_);
            
            for (const auto& symbol : ticker_subscriptions_) {
                generate_ticker_update(symbol);
            }
            
            for (const auto& symbol : orderbook_subscriptions_) {
                generate_orderbook_update(symbol, 20);  // Default depth
            }
            
            for (const auto& symbol : trade_subscriptions_) {
                generate_trade_update(symbol);
            }
        }
        
        std::this_thread::sleep_for(update_interval_);
    }
}

void SampleExchangePlugin::generate_ticker_update(const std::string& symbol) {
    auto it = current_prices_.find(symbol);
    if (it != current_prices_.end()) {
        auto ticker = create_sample_ticker(symbol, it->second);
        notify_ticker(ticker);
    }
}

void SampleExchangePlugin::generate_orderbook_update(const std::string& symbol, int depth) {
    auto it = current_prices_.find(symbol);
    if (it != current_prices_.end()) {
        auto orderbook = create_sample_orderbook(symbol, it->second, depth);
        notify_orderbook(orderbook);
    }
}

void SampleExchangePlugin::generate_trade_update(const std::string& symbol) {
    auto it = current_prices_.find(symbol);
    if (it != current_prices_.end()) {
        auto trade = create_sample_trade(symbol, it->second);
        notify_trade(trade);
    }
}

double SampleExchangePlugin::get_random_price_change() {
    return price_change_dist_(random_generator_);
}

types::Ticker SampleExchangePlugin::create_sample_ticker(const std::string& symbol, double price) {
    types::Ticker ticker;
    ticker.symbol = symbol;
    ticker.exchange = get_plugin_id();
    ticker.price = price;
    ticker.bid = price * 0.999;  // Slightly lower bid
    ticker.ask = price * 1.001;  // Slightly higher ask
    ticker.volume = 1000.0 + (price * 10.0);  // Simulated volume
    ticker.timestamp = std::chrono::duration_cast<std::chrono::milliseconds>(
        std::chrono::system_clock::now().time_since_epoch()).count();
    
    return ticker;
}

types::OrderBook SampleExchangePlugin::create_sample_orderbook(const std::string& symbol, double price, int depth) {
    types::OrderBook orderbook;
    orderbook.symbol = symbol;
    orderbook.exchange = get_plugin_id();
    orderbook.timestamp = std::chrono::system_clock::now();
    
    // Generate bids (below current price)
    for (int i = 0; i < depth; ++i) {
        double bid_price = price * (1.0 - 0.001 * (i + 1));
        double bid_quantity = 100.0 + (i * 10.0);
        orderbook.bids.emplace_back(bid_price, bid_quantity);
    }
    
    // Generate asks (above current price)
    for (int i = 0; i < depth; ++i) {
        double ask_price = price * (1.0 + 0.001 * (i + 1));
        double ask_quantity = 100.0 + (i * 10.0);
        orderbook.asks.emplace_back(ask_price, ask_quantity);
    }
    
    return orderbook;
}

types::Trade SampleExchangePlugin::create_sample_trade(const std::string& symbol, double price) {
    types::Trade trade;
    trade.symbol = symbol;
    trade.exchange = get_plugin_id();
    trade.price = price + get_random_price_change() * price * 0.1;  // Small price variation
    trade.quantity = 10.0 + (get_random_price_change() * 50.0);
    trade.timestamp = std::chrono::duration_cast<std::chrono::milliseconds>(
        std::chrono::system_clock::now().time_since_epoch()).count();
    trade.is_buyer_maker = (get_random_price_change() > 0.0);
    
    return trade;
}

} // namespace exchange
} // namespace ats

// Built-in plugin registration - temporarily disabled for build
// REGISTER_BUILTIN_EXCHANGE_PLUGIN("sample_exchange", ats::exchange::SampleExchangePlugin);