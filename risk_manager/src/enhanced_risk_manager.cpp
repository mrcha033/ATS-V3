#include "../include/enhanced_risk_manager.hpp"
#include "utils/logger.hpp"
#include "utils/crypto_utils.hpp"
#include "trading_engine_mock.hpp"
#include <algorithm>
#include <numeric>
#include <cmath>
#include <random>
#include <sstream>
#include <iomanip>

namespace ats {
namespace risk_manager {

// RealTimePnLCalculator Implementation
RealTimePnLCalculator::RealTimePnLCalculator() {
    utils::Logger::info("Initializing Real-Time P&L Calculator");
}

RealTimePnLCalculator::~RealTimePnLCalculator() {
    utils::Logger::info("Shutting down Real-Time P&L Calculator");
}

bool RealTimePnLCalculator::initialize(std::shared_ptr<utils::RedisClient> redis_client) {
    try {
        redis_client_ = redis_client;
        
        // Load existing positions from Redis
        load_positions_from_redis();
        
        utils::Logger::info("Real-Time P&L Calculator initialized successfully");
        return true;
    } catch (const std::exception& e) {
        utils::Logger::error("Failed to initialize Real-Time P&L Calculator: {}", e.what());
        return false;
    }
}

void RealTimePnLCalculator::shutdown() {
    utils::Logger::info("Shutting down Real-Time P&L Calculator");
    
    // Persist final positions to Redis
    std::shared_lock<std::shared_mutex> lock(positions_mutex_);
    for (const auto& symbol_positions : positions_) {
        for (const auto& exchange_position : symbol_positions.second) {
            persist_position_to_redis(exchange_position.second);
        }
    }
}

void RealTimePnLCalculator::update_position(const std::string& symbol, const std::string& exchange, 
                                           double quantity_change, double price) {
    std::unique_lock<std::shared_mutex> lock(positions_mutex_);
    
    auto& position = positions_[symbol][exchange];
    position.symbol = symbol;
    position.exchange = exchange;
    
    // Update position with weighted average price
    if (std::abs(quantity_change) > 1e-8) {
        double old_quantity = position.quantity;
        double new_quantity = old_quantity + quantity_change;
        
        if (std::abs(new_quantity) > 1e-8) {
            // Calculate weighted average price
            double total_value = (old_quantity * position.average_price) + (quantity_change * price);
            position.average_price = total_value / new_quantity;
            position.quantity = new_quantity;
        } else {
            // Position closed
            position.quantity = 0.0;
            position.average_price = 0.0;
        }
        
        // Update realized P&L for position reductions
        if ((old_quantity > 0 && quantity_change < 0) || (old_quantity < 0 && quantity_change > 0)) {
            double closed_quantity = std::min(std::abs(old_quantity), std::abs(quantity_change));
            double pnl_per_unit = (old_quantity > 0) ? (price - position.average_price) : (position.average_price - price);
            position.realized_pnl += closed_quantity * pnl_per_unit;
        }
        
        position.last_updated = std::chrono::system_clock::now();
        
        // Persist to Redis
        persist_position_to_redis(position);
        
        utils::Logger::debug("Updated position {}/{}: quantity={:.6f}, avg_price={:.2f}, realized_pnl={:.2f}",
                 symbol, exchange, position.quantity, position.average_price, position.realized_pnl);
    }
}

void RealTimePnLCalculator::update_market_prices(const std::unordered_map<std::string, double>& prices) {
    std::lock_guard<std::mutex> lock(prices_mutex_);
    
    for (const auto& price_update : prices) {
        market_prices_[price_update.first] = price_update.second;
    }
    
    // Update unrealized P&L for all positions
    std::shared_lock<std::shared_mutex> pos_lock(positions_mutex_);
    for (auto& symbol_positions : positions_) {
        const std::string& symbol = symbol_positions.first;
        auto price_it = market_prices_.find(symbol);
        
        if (price_it != market_prices_.end()) {
            double current_price = price_it->second;
            
            for (auto& exchange_position : symbol_positions.second) {
                auto& position = exchange_position.second;
                
                if (std::abs(position.quantity) > 1e-8) {
                    position.market_value = position.quantity * current_price;
                    position.unrealized_pnl = position.quantity * (current_price - position.average_price);
                    position.last_updated = std::chrono::system_clock::now();
                }
            }
        }
    }
}

double RealTimePnLCalculator::calculate_unrealized_pnl(const std::string& symbol, const std::string& exchange) {
    std::shared_lock<std::shared_mutex> lock(positions_mutex_);
    
    double total_unrealized = 0.0;
    
    if (symbol.empty()) {
        // Calculate for all positions
        for (const auto& symbol_positions : positions_) {
            for (const auto& exchange_position : symbol_positions.second) {
                total_unrealized += exchange_position.second.unrealized_pnl;
            }
        }
    } else {
        auto symbol_it = positions_.find(symbol);
        if (symbol_it != positions_.end()) {
            if (exchange.empty()) {
                // Calculate for all exchanges for this symbol
                for (const auto& exchange_position : symbol_it->second) {
                    total_unrealized += exchange_position.second.unrealized_pnl;
                }
            } else {
                // Calculate for specific symbol and exchange
                auto exchange_it = symbol_it->second.find(exchange);
                if (exchange_it != symbol_it->second.end()) {
                    total_unrealized = exchange_it->second.unrealized_pnl;
                }
            }
        }
    }
    
    return total_unrealized;
}

double RealTimePnLCalculator::calculate_realized_pnl(const std::string& symbol, const std::string& exchange) {
    std::shared_lock<std::shared_mutex> lock(positions_mutex_);
    
    double total_realized = 0.0;
    
    if (symbol.empty()) {
        // Calculate for all positions
        for (const auto& symbol_positions : positions_) {
            for (const auto& exchange_position : symbol_positions.second) {
                total_realized += exchange_position.second.realized_pnl;
            }
        }
    } else {
        auto symbol_it = positions_.find(symbol);
        if (symbol_it != positions_.end()) {
            if (exchange.empty()) {
                // Calculate for all exchanges for this symbol
                for (const auto& exchange_position : symbol_it->second) {
                    total_realized += exchange_position.second.realized_pnl;
                }
            } else {
                // Calculate for specific symbol and exchange
                auto exchange_it = symbol_it->second.find(exchange);
                if (exchange_it != symbol_it->second.end()) {
                    total_realized = exchange_it->second.realized_pnl;
                }
            }
        }
    }
    
    return total_realized;
}

double RealTimePnLCalculator::calculate_total_pnl() {
    return calculate_unrealized_pnl() + calculate_realized_pnl();
}

std::vector<RealTimePosition> RealTimePnLCalculator::get_all_positions() const {
    std::shared_lock<std::shared_mutex> lock(positions_mutex_);
    
    std::vector<RealTimePosition> positions;
    for (const auto& symbol_positions : positions_) {
        for (const auto& exchange_position : symbol_positions.second) {
            if (std::abs(exchange_position.second.quantity) > 1e-8) {
                positions.push_back(exchange_position.second);
            }
        }
    }
    
    return positions;
}

RealTimePosition RealTimePnLCalculator::get_position(const std::string& symbol, const std::string& exchange) const {
    std::shared_lock<std::shared_mutex> lock(positions_mutex_);
    
    auto symbol_it = positions_.find(symbol);
    if (symbol_it != positions_.end()) {
        auto exchange_it = symbol_it->second.find(exchange);
        if (exchange_it != symbol_it->second.end()) {
            return exchange_it->second;
        }
    }
    
    return RealTimePosition(); // Return empty position if not found
}

double RealTimePnLCalculator::get_total_exposure() const {
    std::shared_lock<std::shared_mutex> lock(positions_mutex_);
    
    double total_exposure = 0.0;
    for (const auto& symbol_positions : positions_) {
        for (const auto& exchange_position : symbol_positions.second) {
            total_exposure += std::abs(exchange_position.second.market_value);
        }
    }
    
    return total_exposure;
}

double RealTimePnLCalculator::calculate_var(double confidence_level, int lookback_days) const {
    std::lock_guard<std::mutex> lock(pnl_history_mutex_);
    
    if (pnl_history_.size() < 2) {
        return 0.0;
    }
    
    // Convert queue to vector for easier manipulation
    std::vector<double> daily_pnl;
    auto temp_queue = pnl_history_;
    
    while (!temp_queue.empty()) {
        daily_pnl.push_back(temp_queue.front().second);
        temp_queue.pop();
    }
    
    // Limit to lookback period
    if (daily_pnl.size() > static_cast<size_t>(lookback_days)) {
        daily_pnl.erase(daily_pnl.begin(), daily_pnl.end() - lookback_days);
    }
    
    // Sort for percentile calculation
    std::sort(daily_pnl.begin(), daily_pnl.end());
    
    // Calculate VaR at confidence level
    size_t var_index = static_cast<size_t>((1.0 - confidence_level) * daily_pnl.size());
    if (var_index >= daily_pnl.size()) {
        var_index = daily_pnl.size() - 1;
    }
    
    return std::abs(daily_pnl[var_index]);
}

double RealTimePnLCalculator::calculate_portfolio_volatility() const {
    std::lock_guard<std::mutex> lock(pnl_history_mutex_);
    
    if (pnl_history_.size() < 2) {
        return 0.0;
    }
    
    // Calculate standard deviation of daily P&L
    std::vector<double> daily_pnl;
    auto temp_queue = pnl_history_;
    
    while (!temp_queue.empty()) {
        daily_pnl.push_back(temp_queue.front().second);
        temp_queue.pop();
    }
    
    double mean = std::accumulate(daily_pnl.begin(), daily_pnl.end(), 0.0) / daily_pnl.size();
    
    double variance = 0.0;
    for (double pnl : daily_pnl) {
        variance += (pnl - mean) * (pnl - mean);
    }
    variance /= (daily_pnl.size() - 1);
    
    return std::sqrt(variance);
}

void RealTimePnLCalculator::persist_position_to_redis(const RealTimePosition& position) {
    if (!redis_client_) {
        return;
    }
    
    try {
        std::string key = generate_position_key(position.symbol, position.exchange);
        
        // Serialize position to JSON-like string
        std::ostringstream oss;
        oss << "{"
            << "\"symbol\":\"" << position.symbol << "\","
            << "\"exchange\":\"" << position.exchange << "\","
            << "\"quantity\":" << std::fixed << std::setprecision(8) << position.quantity << ","
            << "\"average_price\":" << std::fixed << std::setprecision(2) << position.average_price << ","
            << "\"market_value\":" << std::fixed << std::setprecision(2) << position.market_value << ","
            << "\"unrealized_pnl\":" << std::fixed << std::setprecision(2) << position.unrealized_pnl << ","
            << "\"realized_pnl\":" << std::fixed << std::setprecision(2) << position.realized_pnl << ","
            << "\"last_updated\":" << std::chrono::duration_cast<std::chrono::milliseconds>(position.last_updated.time_since_epoch()).count()
            << "}";
        
        redis_client_->set(key, oss.str());
        redis_client_->expire(key, 86400); // Expire after 24 hours
        
    } catch (const std::exception& e) {
        utils::Logger::error("Failed to persist position to Redis: {}", e.what());
    }
}

void RealTimePnLCalculator::load_positions_from_redis() {
    if (!redis_client_) {
        return;
    }
    
    try {
        // Get all position keys
        auto keys = redis_client_->keys("risk_manager:position:*");
        
        for (const auto& key : keys) {
            auto value = redis_client_->get(key);
            if (!value.empty()) {
                // Parse position data (simplified JSON parsing)
                // In production, use a proper JSON library
                utils::Logger::debug("Loaded position from Redis: {}", value);
            }
        }
        
        utils::Logger::info("Loaded {} positions from Redis", keys.size());
    } catch (const std::exception& e) {
        utils::Logger::error("Failed to load positions from Redis: {}", e.what());
    }
}

std::string RealTimePnLCalculator::generate_position_key(const std::string& symbol, const std::string& exchange) const {
    return "risk_manager:position:" + symbol + ":" + exchange;
}

// EnhancedRiskManager Implementation
EnhancedRiskManager::EnhancedRiskManager(ConfigManager* config_manager, DatabaseManager* db_manager)
    : RiskManager(config_manager, db_manager), monitoring_active_(false), halt_triggered_(false),
      risk_checks_per_second_(0), alerts_sent_today_(0) {
    
    pnl_calculator_ = std::make_unique<RealTimePnLCalculator>();
    last_risk_check_ = std::chrono::system_clock::now();
    
    utils::Logger::info("Enhanced Risk Manager initialized");
}

EnhancedRiskManager::~EnhancedRiskManager() {
    shutdown();
    utils::Logger::info("Enhanced Risk Manager destroyed");
}

bool EnhancedRiskManager::initialize() {
    if (!RiskManager::Initialize()) {
        return false;
    }
    
    try {
        // Initialize enhanced limits
        enhanced_limits_.max_portfolio_var = 10000.0;
        enhanced_limits_.max_concentration_ratio = 0.25;
        enhanced_limits_.max_correlation_exposure = 0.5;
        enhanced_limits_.max_leverage_ratio = 3.0;
        enhanced_limits_.stress_test_threshold = 0.15;
        enhanced_limits_.realtime_pnl_threshold = 5000.0;
        enhanced_limits_.max_alerts_per_hour = 20;
        
        utils::Logger::info("Enhanced Risk Manager initialized successfully");
        return true;
    } catch (const std::exception& e) {
        utils::Logger::error("Failed to initialize Enhanced Risk Manager: {}", e.what());
        return false;
    }
}

void EnhancedRiskManager::shutdown() {
    utils::Logger::info("Shutting down Enhanced Risk Manager");
    
    // Stop monitoring
    stop_realtime_monitoring();
    stop_position_streaming();
    
    // Shutdown P&L calculator
    if (pnl_calculator_) {
        pnl_calculator_->shutdown();
    }
    
    // Wait for threads to finish
    if (monitoring_thread_.joinable()) {
        monitoring_thread_.join();
    }
    
    if (position_streaming_thread_.joinable()) {
        position_streaming_thread_.join();
    }
    
    if (alert_processing_thread_.joinable()) {
        alert_processing_thread_.join();
    }
}

bool EnhancedRiskManager::initialize_realtime_engine(std::shared_ptr<utils::RedisClient> redis_client,
                                                   std::shared_ptr<utils::InfluxDBClient> influxdb_client) {
    try {
        redis_client_ = redis_client;
        influxdb_client_ = influxdb_client;
        
        // Initialize P&L calculator
        if (!pnl_calculator_->initialize(redis_client)) {
            utils::Logger::error("Failed to initialize P&L calculator");
            return false;
        }
        
        // Start alert processing thread
        alert_processing_thread_ = std::thread(&EnhancedRiskManager::alert_processing_loop, this);
        
        utils::Logger::info("Real-time risk engine initialized successfully");
        return true;
    } catch (const std::exception& e) {
        utils::Logger::error("Failed to initialize real-time risk engine: {}", e.what());
        return false;
    }
}

bool EnhancedRiskManager::connect_to_trading_engine(const std::string& trading_engine_address) {
    try {
        // Create gRPC channel
        trading_engine_channel_ = grpc::CreateChannel(trading_engine_address, grpc::InsecureChannelCredentials());
        
        // Create stub
        trading_engine_stub_ = ats::trading_engine::TradingEngineService::NewStub(trading_engine_channel_);
        
        // Test connection
        grpc::ClientContext context;
        google::protobuf::Empty request;
        ats::trading_engine::GetHealthStatusResponse response;
        
        auto deadline = std::chrono::system_clock::now() + std::chrono::seconds(5);
        context.set_deadline(deadline);
        
        grpc::Status status = trading_engine_stub_->GetHealthStatus(&context, request, &response);
        
        if (status.ok()) {
            utils::Logger::info("Successfully connected to trading engine at {}", trading_engine_address);
            return true;
        } else {
            utils::Logger::error("Failed to connect to trading engine: {}", status.error_message());
            return false;
        }
    } catch (const std::exception& e) {
        utils::Logger::error("Exception connecting to trading engine: {}", e.what());
        return false;
    }
}

void EnhancedRiskManager::start_realtime_monitoring() {
    if (monitoring_active_.load()) {
        return;
    }
    
    monitoring_active_ = true;
    monitoring_thread_ = std::thread(&EnhancedRiskManager::monitoring_loop, this);
    
    utils::Logger::info("Started real-time risk monitoring");
}

void EnhancedRiskManager::stop_realtime_monitoring() {
    if (!monitoring_active_.load()) {
        return;
    }
    
    monitoring_active_ = false;
    alert_cv_.notify_all();
    
    if (monitoring_thread_.joinable()) {
        monitoring_thread_.join();
    }
    
    utils::Logger::info("Stopped real-time risk monitoring");
}

void EnhancedRiskManager::start_position_streaming() {
    if (!trading_engine_stub_) {
        utils::Logger::error("Trading engine not connected, cannot start position streaming");
        return;
    }
    
    position_streaming_thread_ = std::thread(&EnhancedRiskManager::position_streaming_loop, this);
    utils::Logger::info("Started position streaming from trading engine");
}

void EnhancedRiskManager::stop_position_streaming() {
    if (position_streaming_thread_.joinable()) {
        position_streaming_thread_.join();
    }
    utils::Logger::info("Stopped position streaming");
}

void EnhancedRiskManager::monitoring_loop() {
    utils::Logger::info("Risk monitoring loop started");
    
    while (monitoring_active_.load()) {
        try {
            auto start_time = std::chrono::high_resolution_clock::now();
            
            // Perform risk checks
            check_pnl_limits();
            check_exposure_limits();
            check_concentration_limits();
            check_var_limits();
            
            // Update performance metrics
            auto end_time = std::chrono::high_resolution_clock::now();
            auto duration = std::chrono::duration_cast<std::chrono::microseconds>(end_time - start_time);
            
            risk_checks_per_second_++;
            last_risk_check_ = std::chrono::system_clock::now();
            
            // Persist risk metrics to InfluxDB
            persist_risk_metrics();
            
            // Sleep for monitoring interval (100ms)
            std::this_thread::sleep_for(std::chrono::milliseconds(100));
            
        } catch (const std::exception& e) {
            utils::Logger::error("Error in risk monitoring loop: {}", e.what());
            std::this_thread::sleep_for(std::chrono::seconds(1));
        }
    }
    
    utils::Logger::info("Risk monitoring loop stopped");
}

void EnhancedRiskManager::check_pnl_limits() {
    double current_pnl = pnl_calculator_->calculate_total_pnl();
    double daily_pnl = GetDailyPnL();
    double weekly_pnl = GetWeeklyPnL();
    double monthly_pnl = GetMonthlyPnL();
    
    // Check real-time P&L
    if (current_pnl < -enhanced_limits_.realtime_pnl_threshold) {
        RiskAlert alert;
        alert.severity = RiskAlert::Severity::CRITICAL;
        alert.type = "PNL_LIMIT_BREACH";
        alert.message = "Real-time P&L exceeded loss threshold";
        alert.metadata["current_pnl"] = std::to_string(current_pnl);
        alert.metadata["threshold"] = std::to_string(-enhanced_limits_.realtime_pnl_threshold);
        
        send_risk_alert(alert);
        
        // Consider automatic halt
        if (current_pnl < -enhanced_limits_.realtime_pnl_threshold * 1.5) {
            manual_halt("Severe P&L loss detected");
        }
    }
    
    // Check daily P&L limits
    if (daily_pnl < -limits_.max_daily_loss_usd) {
        RiskAlert alert;
        alert.severity = RiskAlert::Severity::CRITICAL;
        alert.type = "DAILY_PNL_LIMIT_BREACH";
        alert.message = "Daily P&L loss limit exceeded";
        alert.metadata["daily_pnl"] = std::to_string(daily_pnl);
        alert.metadata["limit"] = std::to_string(-limits_.max_daily_loss_usd);
        
        send_risk_alert(alert);
        
        // Trigger halt for daily limit breach
        if (!halt_triggered_.load()) {
            manual_halt("Daily P&L loss limit exceeded");
        }
    }
    
    // Check weekly P&L limits
    if (weekly_pnl < -limits_.max_weekly_loss_usd) {
        RiskAlert alert;
        alert.severity = RiskAlert::Severity::WARNING;
        alert.type = "WEEKLY_PNL_LIMIT_BREACH";
        alert.message = "Weekly P&L loss limit exceeded";
        alert.metadata["weekly_pnl"] = std::to_string(weekly_pnl);
        alert.metadata["limit"] = std::to_string(-limits_.max_weekly_loss_usd);
        
        send_risk_alert(alert);
    }
    
    // Check monthly P&L limits
    if (monthly_pnl < -limits_.max_monthly_loss_usd) {
        RiskAlert alert;
        alert.severity = RiskAlert::Severity::WARNING;
        alert.type = "MONTHLY_PNL_LIMIT_BREACH";
        alert.message = "Monthly P&L loss limit exceeded";
        alert.metadata["monthly_pnl"] = std::to_string(monthly_pnl);
        alert.metadata["limit"] = std::to_string(-limits_.max_monthly_loss_usd);
        
        send_risk_alert(alert);
    }
    
    // Check for rapid P&L deterioration
    static double last_pnl = current_pnl;
    double pnl_change = current_pnl - last_pnl;
    
    if (pnl_change < -1000.0) { // Rapid $1000 loss
        RiskAlert alert;
        alert.severity = RiskAlert::Severity::WARNING;
        alert.type = "RAPID_PNL_DETERIORATION";
        alert.message = "Rapid P&L deterioration detected";
        alert.metadata["pnl_change"] = std::to_string(pnl_change);
        alert.metadata["current_pnl"] = std::to_string(current_pnl);
        
        send_risk_alert(alert);
    }
    
    last_pnl = current_pnl;
}

void EnhancedRiskManager::send_risk_alert(const RiskAlert& alert) {
    std::lock_guard<std::mutex> lock(alert_mutex_);
    
    // Rate limiting (per hour, not per day)
    static std::chrono::system_clock::time_point last_hour_reset = std::chrono::system_clock::now();
    auto now = std::chrono::system_clock::now();
    auto duration = std::chrono::duration_cast<std::chrono::hours>(now - last_hour_reset);
    
    if (duration.count() >= 1) {
        alerts_sent_today_ = 0;
        last_hour_reset = now;
    }
    
    if (alerts_sent_today_.load() >= enhanced_limits_.max_alerts_per_hour) {
        return;
    }
    
    alert_queue_.push(alert);
    alerts_sent_today_++;
    alert_cv_.notify_one();
    
    utils::Logger::warn("Risk alert sent: [{}] {}", static_cast<int>(alert.severity), alert.message);
}

void EnhancedRiskManager::alert_processing_loop() {
    while (monitoring_active_.load()) {
        std::unique_lock<std::mutex> lock(alert_mutex_);
        alert_cv_.wait(lock, [this] { return !alert_queue_.empty() || !monitoring_active_.load(); });
        
        while (!alert_queue_.empty()) {
            RiskAlert alert = alert_queue_.front();
            alert_queue_.pop();
            
            // Generate alert ID
            alert.id = generate_alert_id();
            
            // Send to external systems
            send_alert_to_redis(alert);
            send_alert_to_influxdb(alert);
            
            // Log the alert
            log_risk_event(alert.type, alert.message);
        }
    }
}

std::string EnhancedRiskManager::generate_alert_id() const {
    auto now = std::chrono::system_clock::now();
    auto timestamp = std::chrono::duration_cast<std::chrono::milliseconds>(now.time_since_epoch()).count();
    
    return "RISK_ALERT_" + std::to_string(timestamp) + "_" + utils::CryptoUtils::generate_uuid();
}

void EnhancedRiskManager::send_alert_to_redis(const RiskAlert& alert) {
    if (!redis_client_) {
        return;
    }
    
    try {
        // Publish alert to Redis channel
        std::string channel = "risk_alerts";
        std::string message = alert.id + "|" + std::to_string(static_cast<int>(alert.severity)) + "|" + alert.message;
        
        redis_client_->publish(channel, message);
        
    } catch (const std::exception& e) {
        utils::Logger::error("Failed to send alert to Redis: {}", e.what());
    }
}

void EnhancedRiskManager::send_alert_to_influxdb(const RiskAlert& alert) {
    if (!influxdb_client_) {
        return;
    }
    
    try {
        // Create InfluxDB line protocol entry
        std::ostringstream line;
        line << "risk_alerts,severity=" << static_cast<int>(alert.severity)
             << ",type=" << alert.type
             << " message=\"" << alert.message << "\""
             << ",alert_id=\"" << alert.id << "\""
             << " " << std::chrono::duration_cast<std::chrono::nanoseconds>(alert.timestamp.time_since_epoch()).count();
        
        influxdb_client_->write_point(line.str());
        
    } catch (const std::exception& e) {
        utils::Logger::error("Failed to send alert to InfluxDB: {}", e.what());
    }
}

void EnhancedRiskManager::check_exposure_limits() {
    double current_exposure = pnl_calculator_->get_total_exposure();
    double max_exposure = limits_.max_total_exposure_usd;
    
    if (current_exposure > max_exposure) {
        RiskAlert alert;
        alert.severity = RiskAlert::Severity::CRITICAL;
        alert.type = "EXPOSURE_LIMIT_BREACH";
        alert.message = "Total exposure exceeded maximum limit";
        alert.metadata["current_exposure"] = std::to_string(current_exposure);
        alert.metadata["max_exposure"] = std::to_string(max_exposure);
        alert.metadata["breach_percentage"] = std::to_string((current_exposure / max_exposure - 1.0) * 100.0);
        
        send_risk_alert(alert);
        
        // Consider halting if exposure is significantly over limit
        if (current_exposure > max_exposure * 1.2) {
            manual_halt("Exposure limit severely breached");
        }
    }
    
    // Check individual position exposure
    auto positions = pnl_calculator_->get_all_positions();
    for (const auto& position : positions) {
        double position_exposure = std::abs(position.market_value);
        double max_position_exposure = limits_.max_position_size_usd;
        
        if (position_exposure > max_position_exposure) {
            RiskAlert alert;
            alert.severity = RiskAlert::Severity::WARNING;
            alert.type = "POSITION_EXPOSURE_BREACH";
            alert.message = "Individual position exposure exceeded limit";
            alert.metadata["symbol"] = position.symbol;
            alert.metadata["exchange"] = position.exchange;
            alert.metadata["position_exposure"] = std::to_string(position_exposure);
            alert.metadata["max_position_exposure"] = std::to_string(max_position_exposure);
            
            send_risk_alert(alert);
        }
    }
}

void EnhancedRiskManager::check_concentration_limits() {
    auto positions = pnl_calculator_->get_all_positions();
    double total_portfolio_value = pnl_calculator_->get_total_exposure();
    
    if (total_portfolio_value < 1000.0) {
        return; // Skip concentration checks for small portfolios
    }
    
    // Check concentration by symbol
    std::unordered_map<std::string, double> symbol_exposures;
    for (const auto& position : positions) {
        symbol_exposures[position.symbol] += std::abs(position.market_value);
    }
    
    for (const auto& exposure : symbol_exposures) {
        double concentration_ratio = exposure.second / total_portfolio_value;
        
        if (concentration_ratio > enhanced_limits_.max_concentration_ratio) {
            RiskAlert alert;
            alert.severity = RiskAlert::Severity::WARNING;
            alert.type = "CONCENTRATION_LIMIT_BREACH";
            alert.message = "Symbol concentration exceeded maximum ratio";
            alert.metadata["symbol"] = exposure.first;
            alert.metadata["concentration_ratio"] = std::to_string(concentration_ratio * 100.0);
            alert.metadata["max_concentration_ratio"] = std::to_string(enhanced_limits_.max_concentration_ratio * 100.0);
            alert.metadata["exposure_amount"] = std::to_string(exposure.second);
            
            send_risk_alert(alert);
        }
    }
    
    // Check concentration by exchange
    std::unordered_map<std::string, double> exchange_exposures;
    for (const auto& position : positions) {
        exchange_exposures[position.exchange] += std::abs(position.market_value);
    }
    
    for (const auto& exposure : exchange_exposures) {
        double concentration_ratio = exposure.second / total_portfolio_value;
        
        if (concentration_ratio > enhanced_limits_.max_concentration_ratio) {
            RiskAlert alert;
            alert.severity = RiskAlert::Severity::WARNING;
            alert.type = "EXCHANGE_CONCENTRATION_BREACH";
            alert.message = "Exchange concentration exceeded maximum ratio";
            alert.metadata["exchange"] = exposure.first;
            alert.metadata["concentration_ratio"] = std::to_string(concentration_ratio * 100.0);
            alert.metadata["max_concentration_ratio"] = std::to_string(enhanced_limits_.max_concentration_ratio * 100.0);
            alert.metadata["exposure_amount"] = std::to_string(exposure.second);
            
            send_risk_alert(alert);
        }
    }
}

void EnhancedRiskManager::check_var_limits() {
    double current_var = pnl_calculator_->calculate_var(0.95, 30);
    
    if (current_var > enhanced_limits_.max_portfolio_var) {
        RiskAlert alert;
        alert.severity = RiskAlert::Severity::WARNING;
        alert.type = "VAR_LIMIT_BREACH";
        alert.message = "Portfolio VaR exceeded maximum threshold";
        alert.metadata["current_var"] = std::to_string(current_var);
        alert.metadata["max_var"] = std::to_string(enhanced_limits_.max_portfolio_var);
        alert.metadata["confidence_level"] = "95%";
        
        send_risk_alert(alert);
    }
    
    // Check portfolio volatility
    double portfolio_volatility = pnl_calculator_->calculate_portfolio_volatility();
    double volatility_threshold = enhanced_limits_.max_portfolio_var * 0.5; // 50% of VaR limit
    
    if (portfolio_volatility > volatility_threshold) {
        RiskAlert alert;
        alert.severity = RiskAlert::Severity::INFO;
        alert.type = "HIGH_VOLATILITY_DETECTED";
        alert.message = "Portfolio volatility is elevated";
        alert.metadata["portfolio_volatility"] = std::to_string(portfolio_volatility);
        alert.metadata["volatility_threshold"] = std::to_string(volatility_threshold);
        
        send_risk_alert(alert);
    }
}

bool EnhancedRiskManager::check_exposure_limits_realtime(const std::string& symbol, double additional_quantity) {
    double current_exposure = pnl_calculator_->get_total_exposure();
    
    // Estimate additional exposure from the new position
    std::lock_guard<std::mutex> lock(pnl_calculator_->prices_mutex_);
    auto price_it = pnl_calculator_->market_prices_.find(symbol);
    if (price_it == pnl_calculator_->market_prices_.end()) {
        // No price data available, use conservative estimate
        return false;
    }
    
    double estimated_additional_exposure = std::abs(additional_quantity * price_it->second);
    double projected_exposure = current_exposure + estimated_additional_exposure;
    
    if (projected_exposure > limits_.max_total_exposure_usd) {
        utils::Logger::warn("Trade rejected: would exceed exposure limit. Current: {:.2f}, Projected: {:.2f}, Limit: {:.2f}",
                   current_exposure, projected_exposure, limits_.max_total_exposure_usd);
        return false;
    }
    
    return true;
}

bool EnhancedRiskManager::check_concentration_limits(const std::string& symbol, double additional_quantity) {
    auto positions = pnl_calculator_->get_all_positions();
    double total_portfolio_value = pnl_calculator_->get_total_exposure();
    
    if (total_portfolio_value < 1000.0) {
        return true; // Skip concentration checks for small portfolios
    }
    
    // Calculate current symbol exposure
    double current_symbol_exposure = 0.0;
    for (const auto& position : positions) {
        if (position.symbol == symbol) {
            current_symbol_exposure += std::abs(position.market_value);
        }
    }
    
    // Estimate additional exposure
    std::lock_guard<std::mutex> lock(pnl_calculator_->prices_mutex_);
    auto price_it = pnl_calculator_->market_prices_.find(symbol);
    if (price_it == pnl_calculator_->market_prices_.end()) {
        return false; // No price data available
    }
    
    double estimated_additional_exposure = std::abs(additional_quantity * price_it->second);
    double projected_symbol_exposure = current_symbol_exposure + estimated_additional_exposure;
    double projected_concentration_ratio = projected_symbol_exposure / (total_portfolio_value + estimated_additional_exposure);
    
    if (projected_concentration_ratio > enhanced_limits_.max_concentration_ratio) {
        utils::Logger::warn("Trade rejected: would exceed concentration limit for {}. Current ratio: {:.2f}%, Projected: {:.2f}%, Limit: {:.2f}%",
                   symbol, (current_symbol_exposure / total_portfolio_value) * 100.0, 
                   projected_concentration_ratio * 100.0, enhanced_limits_.max_concentration_ratio * 100.0);
        return false;
    }
    
    return true;
}

RiskAssessment EnhancedRiskManager::assess_opportunity_realtime(const ArbitrageOpportunity& opportunity) {
    // Start with base risk assessment
    RiskAssessment assessment = AssessOpportunity(opportunity);
    
    if (!assessment.is_approved) {
        return assessment;
    }
    
    // Enhanced real-time checks
    if (!check_exposure_limits_realtime(opportunity.symbol, opportunity.max_volume)) {
        assessment.is_approved = false;
        assessment.rejections.push_back("Real-time exposure limit exceeded");
        return assessment;
    }
    
    if (!check_concentration_limits(opportunity.symbol, opportunity.max_volume)) {
        assessment.is_approved = false;
        assessment.rejections.push_back("Concentration limit would be exceeded");
        return assessment;
    }
    
    // Check if halt is triggered
    if (halt_triggered_.load()) {
        assessment.is_approved = false;
        assessment.rejections.push_back("Trading is halted due to risk limits");
        return assessment;
    }
    
    // Calculate enhanced risk score
    double base_risk = assessment.risk_score;
    double concentration_risk = calculate_concentration_risk(opportunity.symbol);
    double volatility_risk = calculate_volatility_risk(opportunity.symbol);
    double correlation_risk = calculate_correlation_risk(opportunity.symbol);
    
    // Weighted risk score
    assessment.risk_score = (base_risk * 0.4) + (concentration_risk * 0.2) + 
                           (volatility_risk * 0.2) + (correlation_risk * 0.2);
    
    // Additional warnings for high-risk trades
    if (assessment.risk_score > 0.8) {
        assessment.warnings.push_back("High-risk trade detected");
    }
    
    if (concentration_risk > 0.7) {
        assessment.warnings.push_back("High concentration risk");
    }
    
    return assessment;
}

double EnhancedRiskManager::calculate_concentration_risk(const std::string& symbol) const {
    auto positions = pnl_calculator_->get_all_positions();
    double total_portfolio_value = pnl_calculator_->get_total_exposure();
    
    if (total_portfolio_value < 1000.0) {
        return 0.0; // No concentration risk for small portfolios
    }
    
    double symbol_exposure = 0.0;
    for (const auto& position : positions) {
        if (position.symbol == symbol) {
            symbol_exposure += std::abs(position.market_value);
        }
    }
    
    double concentration_ratio = symbol_exposure / total_portfolio_value;
    
    // Risk increases exponentially with concentration
    return std::min(1.0, std::pow(concentration_ratio / enhanced_limits_.max_concentration_ratio, 2.0));
}

double EnhancedRiskManager::calculate_volatility_risk(const std::string& symbol) const {
    // This would typically use historical volatility data
    // For now, return a moderate risk score
    return 0.3;
}

double EnhancedRiskManager::calculate_correlation_risk(const std::string& symbol) const {
    // This would calculate correlation with existing positions
    // For now, return a moderate risk score
    return 0.3;
}

void EnhancedRiskManager::log_risk_event(const std::string& event_type, const std::string& details) {
    utils::Logger::info("Risk Event [{}]: {}", event_type, details);
}

void EnhancedRiskManager::manual_halt(const std::string& reason) {
    halt_triggered_ = true;
    HaltTrading(reason);
    
    RiskAlert alert;
    alert.severity = RiskAlert::Severity::EMERGENCY;
    alert.type = "TRADING_HALT";
    alert.message = "Trading halted: " + reason;
    
    send_risk_alert(alert);
    
    utils::Logger::error("Manual trading halt triggered: {}", reason);
}

void EnhancedRiskManager::position_streaming_loop() {
    if (!trading_engine_stub_) {
        utils::Logger::error("Trading engine stub not available for position streaming");
        return;
    }
    
    utils::Logger::info("Starting position streaming loop");
    
    while (monitoring_active_.load()) {
        try {
            grpc::ClientContext context;
            google::protobuf::Empty request;
            
            // Set streaming deadline
            auto deadline = std::chrono::system_clock::now() + std::chrono::minutes(5);
            context.set_deadline(deadline);
            
            // Start streaming trade executions
            auto stream = trading_engine_stub_->StreamTradeExecutions(&context, request);
            
            ats::trading_engine::TradeExecutionEvent event;
            while (stream->Read(&event) && monitoring_active_.load()) {
                // Process trade execution event
                on_trade_execution(event.execution());
            }
            
            grpc::Status status = stream->Finish();
            if (!status.ok()) {
                utils::Logger::warn("Trade execution stream ended: {}", status.error_message());
            }
            
        } catch (const std::exception& e) {
            utils::Logger::error("Error in position streaming loop: {}", e.what());
        }
        
        // Wait before reconnecting
        if (monitoring_active_.load()) {
            std::this_thread::sleep_for(std::chrono::seconds(5));
        }
    }
    
    utils::Logger::info("Position streaming loop stopped");
}

void EnhancedRiskManager::on_trade_execution(const TradeRecord& execution) {
    try {
        utils::Logger::debug("Processing trade execution: {}", execution.trade_id);
        
        // Update positions based on trade execution
        if (execution.is_completed && execution.failure_reason.empty()) {
            
            // Update buy side position
            if (execution.volume > 0) {
                pnl_calculator_->update_position(
                    execution.symbol,
                    execution.buy_exchange,
                    execution.volume,
                    execution.buy_price
                );
                
                // Update sell side position (negative quantity)
                pnl_calculator_->update_position(
                    execution.symbol,
                    execution.sell_exchange,
                    -execution.volume,
                    execution.sell_price
                );
            }
            
            // Update P&L with realized profit
            if (execution.realized_pnl != 0.0) {
                UpdatePnL(execution.realized_pnl);
            }
        }
        
        // Check for risk violations after position update
        check_and_trigger_halt();
        
    } catch (const std::exception& e) {
        utils::Logger::error("Error processing trade execution: {}", e.what());
        
        RiskAlert alert;
        alert.severity = RiskAlert::Severity::WARNING;
        alert.type = "POSITION_UPDATE_ERROR";
        alert.message = "Failed to update position from trade execution";
        alert.metadata["trade_id"] = execution.trade_id;
        alert.metadata["error"] = e.what();
        
        send_risk_alert(alert);
    }
}

void EnhancedRiskManager::on_order_update(const Order& order) {
    try {
        utils::Logger::debug("Processing order update: {}", order.id);
        
        // Update position tracking based on order status
        if (order.status == OrderStatus::FILLED || 
            order.status == OrderStatus::PARTIALLY_FILLED) {
            
            double filled_quantity = order.executed_quantity;
            if (order.side == OrderSide::SELL) {
                filled_quantity = -filled_quantity;
            }
            
            // Note: Order doesn't have exchange field, would need to be added
            // For now, use a placeholder
            std::string exchange = "unknown";
            
            pnl_calculator_->update_position(
                order.symbol,
                exchange,
                filled_quantity,
                order.price
            );
        }
        
        // Monitor for failed orders (using is_working flag as status indicator)
        if (!order.is_working && order.executed_quantity == 0) {
            
            RiskAlert alert;
            alert.severity = RiskAlert::Severity::WARNING;
            alert.type = "ORDER_FAILURE";
            alert.message = "Order execution failed or canceled";
            alert.metadata["order_id"] = order.id;
            alert.metadata["symbol"] = order.symbol;
            alert.metadata["client_order_id"] = order.client_order_id;
            
            send_risk_alert(alert);
        }
        
    } catch (const std::exception& e) {
        utils::Logger::error("Error processing order update: {}", e.what());
    }
}

void EnhancedRiskManager::on_balance_update(const Balance& balance) {
    try {
        double total_balance = balance.free + balance.locked;
        utils::Logger::debug("Processing balance update: {} - {:.2f}", balance.asset, total_balance);
        
        // Monitor for low balance conditions
        if (balance.free < total_balance * 0.1) { // Less than 10% available
            RiskAlert alert;
            alert.severity = RiskAlert::Severity::WARNING;
            alert.type = "LOW_BALANCE_WARNING";
            alert.message = "Low available balance detected";
            alert.metadata["currency"] = balance.asset;
            alert.metadata["available"] = std::to_string(balance.free);
            alert.metadata["total"] = std::to_string(total_balance);
            
            send_risk_alert(alert);
        }
        
        // Monitor for negative balances
        if (balance.free < 0) {
            RiskAlert alert;
            alert.severity = RiskAlert::Severity::CRITICAL;
            alert.type = "NEGATIVE_BALANCE";
            alert.message = "Negative balance detected";
            alert.metadata["currency"] = balance.asset;
            alert.metadata["available"] = std::to_string(balance.free);
            
            send_risk_alert(alert);
            
            // Consider halting for negative balances
            manual_halt("Negative balance detected for " + balance.asset);
        }
        
    } catch (const std::exception& e) {
        utils::Logger::error("Error processing balance update: {}", e.what());
    }
}

void EnhancedRiskManager::check_and_trigger_halt() {
    if (halt_triggered_.load()) {
        return; // Already halted
    }
    
    try {
        // Check all risk conditions that could trigger a halt
        bool should_halt = false;
        std::string halt_reason;
        
        // Check P&L limits
        double current_pnl = pnl_calculator_->calculate_total_pnl();
        if (current_pnl < -enhanced_limits_.realtime_pnl_threshold * 1.5) {
            should_halt = true;
            halt_reason = "Severe P&L loss threshold exceeded";
        }
        
        // Check daily loss limits
        double daily_pnl = GetDailyPnL();
        if (daily_pnl < -limits_.max_daily_loss_usd) {
            should_halt = true;
            halt_reason = "Daily loss limit exceeded";
        }
        
        // Check exposure limits
        double current_exposure = pnl_calculator_->get_total_exposure();
        if (current_exposure > limits_.max_total_exposure_usd * 1.2) {
            should_halt = true;
            halt_reason = "Exposure limit severely breached";
        }
        
        // Check VaR limits
        double current_var = pnl_calculator_->calculate_var();
        if (current_var > enhanced_limits_.max_portfolio_var * 1.5) {
            should_halt = true;
            halt_reason = "Portfolio VaR limit severely exceeded";
        }
        
        if (should_halt) {
            manual_halt(halt_reason);
        }
        
    } catch (const std::exception& e) {
        utils::Logger::error("Error in check_and_trigger_halt: {}", e.what());
    }
}

void EnhancedRiskManager::resume_after_halt() {
    if (!halt_triggered_.load()) {
        return;
    }
    
    // Perform comprehensive risk checks before resuming
    if (!check_all_limits()) {
        utils::Logger::warn("Cannot resume trading: risk limits still violated");
        return;
    }
    
    halt_triggered_ = false;
    ResumeTrading();
    
    RiskAlert alert;
    alert.severity = RiskAlert::Severity::INFO;
    alert.type = "TRADING_RESUMED";
    alert.message = "Trading resumed after halt";
    
    send_risk_alert(alert);
    
    utils::Logger::info("Trading resumed after halt");
}

bool EnhancedRiskManager::check_all_limits() const {
    try {
        // Check P&L limits
        double current_pnl = pnl_calculator_->calculate_total_pnl();
        double daily_pnl = GetDailyPnL();
        
        if (current_pnl < -enhanced_limits_.realtime_pnl_threshold || 
            daily_pnl < -limits_.max_daily_loss_usd) {
            return false;
        }
        
        // Check exposure limits
        double current_exposure = pnl_calculator_->get_total_exposure();
        if (current_exposure > limits_.max_total_exposure_usd) {
            return false;
        }
        
        // Check VaR limits
        double current_var = pnl_calculator_->calculate_var();
        if (current_var > enhanced_limits_.max_portfolio_var) {
            return false;
        }
        
        return true;
    } catch (const std::exception& e) {
        utils::Logger::error("Error checking risk limits: {}", e.what());
        return false;
    }
}

std::vector<std::string> EnhancedRiskManager::get_limit_violations() const {
    std::vector<std::string> violations;
    
    try {
        // Check P&L violations
        double current_pnl = pnl_calculator_->calculate_total_pnl();
        if (current_pnl < -enhanced_limits_.realtime_pnl_threshold) {
            violations.push_back("Real-time P&L loss threshold exceeded");
        }
        
        double daily_pnl = GetDailyPnL();
        if (daily_pnl < -limits_.max_daily_loss_usd) {
            violations.push_back("Daily loss limit exceeded");
        }
        
        // Check exposure violations
        double current_exposure = pnl_calculator_->get_total_exposure();
        if (current_exposure > limits_.max_total_exposure_usd) {
            violations.push_back("Total exposure limit exceeded");
        }
        
        // Check VaR violations
        double current_var = pnl_calculator_->calculate_var();
        if (current_var > enhanced_limits_.max_portfolio_var) {
            violations.push_back("Portfolio VaR limit exceeded");
        }
        
        // Check concentration violations
        auto positions = pnl_calculator_->get_all_positions();
        double total_portfolio_value = pnl_calculator_->get_total_exposure();
        
        if (total_portfolio_value > 1000.0) {
            std::unordered_map<std::string, double> symbol_exposures;
            for (const auto& position : positions) {
                symbol_exposures[position.symbol] += std::abs(position.market_value);
            }
            
            for (const auto& exposure : symbol_exposures) {
                double concentration_ratio = exposure.second / total_portfolio_value;
                if (concentration_ratio > enhanced_limits_.max_concentration_ratio) {
                    violations.push_back("Concentration limit exceeded for " + exposure.first);
                }
            }
        }
        
    } catch (const std::exception& e) {
        utils::Logger::error("Error getting limit violations: {}", e.what());
        violations.push_back("Error checking risk limits");
    }
    
    return violations;
}

void EnhancedRiskManager::persist_risk_metrics() {
    if (!influxdb_client_) {
        return;
    }
    
    try {
        double total_pnl = pnl_calculator_->calculate_total_pnl();
        double total_exposure = pnl_calculator_->get_total_exposure();
        double portfolio_var = pnl_calculator_->calculate_var();
        
        std::ostringstream line;
        line << "risk_metrics "
             << "total_pnl=" << std::fixed << std::setprecision(2) << total_pnl << ","
             << "total_exposure=" << std::fixed << std::setprecision(2) << total_exposure << ","
             << "portfolio_var=" << std::fixed << std::setprecision(2) << portfolio_var << ","
             << "risk_checks_per_second=" << risk_checks_per_second_.load() << ","
             << "alerts_sent_today=" << alerts_sent_today_.load() << ","
             << "halt_triggered=" << (halt_triggered_.load() ? "true" : "false")
             << " " << std::chrono::duration_cast<std::chrono::nanoseconds>(std::chrono::system_clock::now().time_since_epoch()).count();
        
        influxdb_client_->write_point(line.str());
        
    } catch (const std::exception& e) {
        utils::Logger::error("Failed to persist risk metrics: {}", e.what());
    }
}

} // namespace risk_manager
} // namespace ats

void EnhancedRiskManager::update_position_realtime(const std::string& symbol, const std::string& exchange,
                                                 double quantity_change, double price) {
    if (pnl_calculator_) {
        pnl_calculator_->update_position(symbol, exchange, quantity_change, price);
        
        // Trigger risk checks after position update
        check_and_trigger_halt();
    }
}

double EnhancedRiskManager::get_realtime_pnl() const {
    return pnl_calculator_ ? pnl_calculator_->calculate_total_pnl() : 0.0;
}

double EnhancedRiskManager::get_realtime_exposure() const {
    return pnl_calculator_ ? pnl_calculator_->get_total_exposure() : 0.0;
}

std::vector<RealTimePosition> EnhancedRiskManager::get_current_positions() const {
    return pnl_calculator_ ? pnl_calculator_->get_all_positions() : std::vector<RealTimePosition>();
}
