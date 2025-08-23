#pragma once

#include <string>
#include <memory>
#include <chrono>

// Mock gRPC/protobuf types for compilation without actual gRPC
namespace grpc {
    class Status {
    public:
        Status() = default;
        bool ok() const { return true; }
        std::string error_message() const { return ""; }
    };
    
    class ClientContext {};
    class ServerContext {};
    class Channel {};
    class ChannelCredentials {};
    
    template<typename T>
    class ServerWriter {};
    
    std::shared_ptr<Channel> CreateChannel(const std::string& target, std::shared_ptr<ChannelCredentials> creds) {
        return nullptr;
    }
    
    std::shared_ptr<ChannelCredentials> InsecureChannelCredentials() {
        return nullptr;
    }
}

namespace google::protobuf {
    class Empty {};
}

// Mock trading engine protobuf messages and service
namespace ats::trading_engine {
    
    enum ExecutionResult {
        EXECUTION_RESULT_SUCCESS = 0,
        EXECUTION_RESULT_PARTIAL_SUCCESS = 1,
        EXECUTION_RESULT_FAILED = 2
    };
    
    enum OrderStatus {
        ORDER_STATUS_PENDING = 0,
        ORDER_STATUS_FILLED = 1,
        ORDER_STATUS_PARTIALLY_FILLED = 2,
        ORDER_STATUS_CANCELED = 3
    };
    
    enum OrderSide {
        ORDER_SIDE_BUY = 0,
        ORDER_SIDE_SELL = 1
    };
    
    class GetHealthStatusResponse {
    public:
        bool healthy() const { return true; }
        void set_healthy(bool value) {}
        std::string message() const { return "OK"; }
        void set_message(const std::string& msg) {}
    };
    
    class GetRiskStatusResponse {
    public:
        void set_is_trading_halted(bool value) {}
        void set_daily_pnl(double value) {}
        void set_weekly_pnl(double value) {}
        void set_monthly_pnl(double value) {}
        void set_max_daily_loss(double value) {}
        void set_max_daily_volume(double value) {}
        void add_risk_warnings(const std::string& warning) {}
    };
    
    class GetPositionsResponse {
    public:
        class Position* add_positions() { return nullptr; }
    };
    
    class Position {
    public:
        void set_symbol(const std::string& value) {}
        void set_exchange(const std::string& value) {}
        void set_quantity(double value) {}
        void set_average_price(double value) {}
        void set_market_value(double value) {}
        void set_unrealized_pnl(double value) {}
    };
    
    class GetPnLResponse {
    public:
        void set_daily_pnl(double value) {}
        void set_weekly_pnl(double value) {}
        void set_monthly_pnl(double value) {}
        void set_unrealized_pnl(double value) {}
        void set_realized_pnl(double value) {}
    };
    
    class GetRiskAlertsRequest {
    public:
        int limit() const { return 50; }
    };
    
    class GetRiskAlertsResponse {
    public:
        class RiskAlert* add_alerts() { return nullptr; }
    };
    
    class RiskAlert {
    public:
        void set_id(const std::string& value) {}
        void set_severity(int value) {}
        void set_type(const std::string& value) {}
        void set_message(const std::string& value) {}
        void set_acknowledged(bool value) {}
    };
    
    class AcknowledgeAlertRequest {
    public:
        std::string alert_id() const { return ""; }
    };
    
    class AcknowledgeAlertResponse {
    public:
        void set_success(bool value) {}
        void set_message(const std::string& value) {}
    };
    
    class EmergencyHaltRequest {
    public:
        std::string reason() const { return ""; }
    };
    
    class EmergencyHaltResponse {
    public:
        void set_success(bool value) {}
        void set_message(const std::string& value) {}
        void set_halt_reason(const std::string& value) {}
    };
    
    class ResumeTradeingResponse {  // Note: typo in original
    public:
        void set_success(bool value) {}
        void set_message(const std::string& value) {}
    };
    
    class UpdateRiskLimitsRequest {
    public:
        // Add fields as needed
    };
    
    class UpdateRiskLimitsResponse {
    public:
        void set_success(bool value) {}
        void set_message(const std::string& value) {}
    };
    
    class RiskAlertEvent {
    public:
        const RiskAlert& alert() const { 
            static RiskAlert alert;
            return alert;
        }
    };
    
    class PositionUpdateEvent {
    public:
        const Position& position() const {
            static Position pos;
            return pos;
        }
    };
    
    class TradeExecution {
    public:
        std::string trade_id() const { return ""; }
        std::string symbol() const { return ""; }
        std::string buy_exchange() const { return ""; }
        std::string sell_exchange() const { return ""; }
        double volume() const { return 0.0; }
        double buy_price() const { return 0.0; }
        double sell_price() const { return 0.0; }
        double realized_pnl() const { return 0.0; }
        double fees_paid() const { return 0.0; }
        ExecutionResult result() const { return EXECUTION_RESULT_SUCCESS; }
    };
    
    class TradeExecutionEvent {
    public:
        const TradeExecution& execution() const { 
            static TradeExecution exec;
            return exec; 
        }
        void set_allocated_execution(TradeExecution* exec) {}
    };
    
    class Order {
    public:
        std::string order_id() const { return ""; }
        std::string symbol() const { return ""; }
        std::string exchange() const { return ""; }
        OrderSide side() const { return ORDER_SIDE_BUY; }
        double quantity() const { return 0.0; }
        double price() const { return 0.0; }
        double filled_quantity() const { return 0.0; }
        double average_fill_price() const { return 0.0; }
        OrderStatus status() const { return ORDER_STATUS_PENDING; }
        std::string error_message() const { return ""; }
    };
    
    class OrderUpdateEvent {
    public:
        const Order& order() const {
            static Order order;
            return order;
        }
        void set_allocated_order(Order* order) {}
    };
    
    class Balance {
    public:
        std::string currency() const { return ""; }
        double available() const { return 0.0; }
        double total() const { return 0.0; }
    };
    
    class BalanceUpdateEvent {
    public:
        const Balance& balance() const {
            static Balance bal;
            return bal;
        }
        void set_allocated_balance(Balance* balance) {}
    };
    
    class TradingEngineService {
    public:
        class Stub {
        public:
            grpc::Status GetHealthStatus(grpc::ClientContext* context, 
                                       const google::protobuf::Empty& request,
                                       GetHealthStatusResponse* response) {
                return grpc::Status();
            }
        };
        
        static std::unique_ptr<Stub> NewStub(std::shared_ptr<grpc::Channel> channel) {
            return std::make_unique<Stub>();
        }
    };
}