#pragma once

#include "../../src/core/risk_manager.hpp"
#include "../../trading_engine/grpc/trading_engine.grpc.pb.h"
#include "../../utils/redis_client.hpp"
#include "../../utils/influxdb_client.hpp"
#include <grpcpp/grpcpp.h>
#include <memory>
#include <unordered_map>
#include <chrono>
#include <thread>
#include <atomic>
#include <future>
#include <queue>
#include <condition_variable>

namespace ats {
namespace risk_manager {

// Real-time position tracking structure
struct RealTimePosition {
    std::string symbol;
    std::string exchange;
    double quantity;
    double average_price;
    double market_value;
    double unrealized_pnl;
    double realized_pnl;
    std::chrono::system_clock::time_point last_updated;
    
    RealTimePosition() : quantity(0.0), average_price(0.0), market_value(0.0), 
                        unrealized_pnl(0.0), realized_pnl(0.0) {}
};

// P&L calculation engine
class RealTimePnLCalculator {
public:
    RealTimePnLCalculator();
    ~RealTimePnLCalculator();
    
    bool initialize(std::shared_ptr<utils::RedisClient> redis_client);
    void shutdown();
    
    // Position updates
    void update_position(const std::string& symbol, const std::string& exchange, 
                        double quantity_change, double price);
    void update_market_prices(const std::unordered_map<std::string, double>& prices);
    
    // P&L calculations
    double calculate_unrealized_pnl(const std::string& symbol, const std::string& exchange = "");
    double calculate_realized_pnl(const std::string& symbol, const std::string& exchange = "");
    double calculate_total_pnl();
    
    // Position queries
    std::vector<RealTimePosition> get_all_positions() const;
    RealTimePosition get_position(const std::string& symbol, const std::string& exchange) const;
    double get_total_exposure() const;
    
    // Risk metrics
    double calculate_var(double confidence_level = 0.95, int lookback_days = 30) const;
    double calculate_portfolio_volatility() const;
    double calculate_beta(const std::string& benchmark_symbol = "BTC/USDT") const;
    
private:
    std::unordered_map<std::string, std::unordered_map<std::string, RealTimePosition>> positions_; // symbol -> exchange -> position
    std::unordered_map<std::string, double> market_prices_; // symbol -> current price
    mutable std::shared_mutex positions_mutex_;
    mutable std::mutex prices_mutex_;
    
    std::shared_ptr<utils::RedisClient> redis_client_;
    
    // Historical data for risk calculations
    std::queue<std::pair<std::chrono::system_clock::time_point, double>> pnl_history_;
    mutable std::mutex pnl_history_mutex_;
    
    std::string generate_position_key(const std::string& symbol, const std::string& exchange) const;
    void persist_position_to_redis(const RealTimePosition& position);
    void load_positions_from_redis();
};

// Risk alert system
struct RiskAlert {
    enum class Severity {
        INFO,
        WARNING,
        CRITICAL,
        EMERGENCY
    };
    
    std::string id;
    Severity severity;
    std::string type;
    std::string message;
    std::unordered_map<std::string, std::string> metadata;
    std::chrono::system_clock::time_point timestamp;
    bool acknowledged;
    
    RiskAlert() : severity(Severity::INFO), acknowledged(false) {
        timestamp = std::chrono::system_clock::now();
    }
};

// Enhanced risk manager with real-time capabilities
class EnhancedRiskManager : public RiskManager {
public:
    EnhancedRiskManager(ConfigManager* config_manager, DatabaseManager* db_manager);
    ~EnhancedRiskManager();
    
    // Lifecycle
    bool initialize() override;
    void shutdown();
    
    // Real-time position tracking
    bool initialize_realtime_engine(std::shared_ptr<utils::RedisClient> redis_client,
                                   std::shared_ptr<utils::InfluxDBClient> influxdb_client);
    
    // gRPC integration
    bool connect_to_trading_engine(const std::string& trading_engine_address);
    void start_position_streaming();
    void stop_position_streaming();
    
    // Enhanced risk assessment
    RiskAssessment assess_opportunity_realtime(const ArbitrageOpportunity& opportunity) override;
    bool check_exposure_limits_realtime(const std::string& symbol, double additional_quantity);
    bool check_concentration_limits(const std::string& symbol, double additional_quantity);
    
    // Real-time monitoring
    void start_realtime_monitoring();
    void stop_realtime_monitoring();\n    \n    // Automatic trading halt and limit checking\n    void check_and_trigger_halt();\n    void resume_after_halt();\n    bool check_all_limits() const;\n    std::vector<std::string> get_limit_violations() const;
    
    // P&L and position management
    void update_position_realtime(const std::string& symbol, const std::string& exchange,
                                 double quantity_change, double price);
    double get_realtime_pnl() const;
    double get_realtime_exposure() const;
    std::vector<RealTimePosition> get_current_positions() const;
    
    // Risk alerts
    void send_risk_alert(const RiskAlert& alert);
    std::vector<RiskAlert> get_recent_alerts(int limit = 50) const;
    void acknowledge_alert(const std::string& alert_id);
    
    // Automatic trading halt
    void check_and_trigger_halt();
    bool is_halt_triggered() const { return halt_triggered_.load(); }
    void manual_halt(const std::string& reason);
    void resume_after_halt();
    
    // Risk metrics
    double calculate_portfolio_var() const;
    double calculate_portfolio_stress_test(double market_shock_percent) const;
    std::unordered_map<std::string, double> calculate_position_risks() const;
    
    // Limit monitoring
    bool check_all_limits() const;
    std::vector<std::string> get_limit_violations() const;
    
    // Event handlers for gRPC streaming
    void on_trade_execution(const ats::trading_engine::TradeExecution& execution);
    void on_order_update(const ats::trading_engine::OrderUpdateEvent& update);
    void on_balance_update(const ats::trading_engine::Balance& balance);
    
private:
    std::unique_ptr<RealTimePnLCalculator> pnl_calculator_;
    std::shared_ptr<utils::RedisClient> redis_client_;
    std::shared_ptr<utils::InfluxDBClient> influxdb_client_;
    
    // gRPC connection to trading engine
    std::unique_ptr<ats::trading_engine::TradingEngineService::Stub> trading_engine_stub_;
    std::shared_ptr<grpc::Channel> trading_engine_channel_;
    
    // Real-time monitoring
    std::atomic<bool> monitoring_active_;
    std::atomic<bool> halt_triggered_;
    std::thread monitoring_thread_;
    std::thread position_streaming_thread_;
    
    // Risk alerts
    std::queue<RiskAlert> alert_queue_;
    std::mutex alert_mutex_;
    std::condition_variable alert_cv_;
    std::thread alert_processing_thread_;
    
    // Performance monitoring
    std::chrono::system_clock::time_point last_risk_check_;
    std::atomic<int> risk_checks_per_second_;
    std::atomic<int> alerts_sent_today_;
    
    // Risk thresholds (enhanced)
    struct EnhancedRiskLimits {
        double max_portfolio_var = 10000.0;           // Maximum portfolio VaR
        double max_concentration_ratio = 0.25;        // Max single position as % of portfolio
        double max_correlation_exposure = 0.5;        // Max exposure to correlated positions
        double max_leverage_ratio = 3.0;              // Maximum leverage
        double stress_test_threshold = 0.15;          // 15% market shock threshold
        double realtime_pnl_threshold = 5000.0;       // Real-time P&L alert threshold
        int max_alerts_per_hour = 20;                 // Alert rate limiting
    };
    EnhancedRiskLimits enhanced_limits_;
    
    // Helper methods
    void monitoring_loop();
    void position_streaming_loop();
    void alert_processing_loop();
    
    void check_pnl_limits();
    void check_exposure_limits();
    void check_concentration_limits();
    void check_var_limits();
    
    void persist_risk_metrics();
    void send_alert_to_redis(const RiskAlert& alert);
    void send_alert_to_influxdb(const RiskAlert& alert);
    
    std::string generate_alert_id() const;
    void log_risk_event(const std::string& event_type, const std::string& details);\n    \n    // Risk calculation helpers\n    double calculate_concentration_risk(const std::string& symbol) const;\n    double calculate_volatility_risk(const std::string& symbol) const;\n    double calculate_correlation_risk(const std::string& symbol) const;
};

// gRPC service for risk manager
class RiskManagerGrpcService final : public ats::trading_engine::RiskManagerService::Service {
public:
    RiskManagerGrpcService();
    ~RiskManagerGrpcService();
    
    bool initialize(std::shared_ptr<EnhancedRiskManager> risk_manager);
    
    // Risk status and monitoring
    grpc::Status GetRiskStatus(grpc::ServerContext* context,
                              const google::protobuf::Empty* request,
                              ats::trading_engine::GetRiskStatusResponse* response) override;
    
    grpc::Status GetPositions(grpc::ServerContext* context,
                             const google::protobuf::Empty* request,
                             ats::trading_engine::GetPositionsResponse* response) override;
    
    grpc::Status GetPnL(grpc::ServerContext* context,
                       const google::protobuf::Empty* request,
                       ats::trading_engine::GetPnLResponse* response) override;
    
    // Risk alerts
    grpc::Status GetRiskAlerts(grpc::ServerContext* context,
                              const ats::trading_engine::GetRiskAlertsRequest* request,
                              ats::trading_engine::GetRiskAlertsResponse* response) override;
    
    grpc::Status AcknowledgeAlert(grpc::ServerContext* context,
                                 const ats::trading_engine::AcknowledgeAlertRequest* request,
                                 ats::trading_engine::AcknowledgeAlertResponse* response) override;
    
    // Emergency controls
    grpc::Status EmergencyHalt(grpc::ServerContext* context,
                              const ats::trading_engine::EmergencyHaltRequest* request,
                              ats::trading_engine::EmergencyHaltResponse* response) override;
    
    grpc::Status ResumeTrading(grpc::ServerContext* context,
                              const google::protobuf::Empty* request,
                              ats::trading_engine::ResumeTradeingResponse* response) override;
    
    // Configuration
    grpc::Status UpdateRiskLimits(grpc::ServerContext* context,
                                 const ats::trading_engine::UpdateRiskLimitsRequest* request,
                                 ats::trading_engine::UpdateRiskLimitsResponse* response) override;
    
    // Streaming
    grpc::Status StreamRiskAlerts(grpc::ServerContext* context,
                                 const google::protobuf::Empty* request,
                                 grpc::ServerWriter<ats::trading_engine::RiskAlertEvent>* writer) override;
    
    grpc::Status StreamPositionUpdates(grpc::ServerContext* context,
                                      const google::protobuf::Empty* request,
                                      grpc::ServerWriter<ats::trading_engine::PositionUpdateEvent>* writer) override;
    
private:
    std::shared_ptr<EnhancedRiskManager> risk_manager_;
    
    // Streaming contexts
    std::unordered_map<grpc::ServerContext*, std::unique_ptr<std::thread>> streaming_threads_;
    std::mutex streaming_mutex_;
    
    void risk_alert_streaming_thread(grpc::ServerContext* context,
                                    grpc::ServerWriter<ats::trading_engine::RiskAlertEvent>* writer);
    void position_update_streaming_thread(grpc::ServerContext* context,
                                         grpc::ServerWriter<ats::trading_engine::PositionUpdateEvent>* writer);
    
    // Conversion helpers
    void convert_to_proto(const RiskAlert& from, ats::trading_engine::RiskAlert* to);
    void convert_to_proto(const RealTimePosition& from, ats::trading_engine::Position* to);
};

} // namespace risk_manager
} // namespace ats