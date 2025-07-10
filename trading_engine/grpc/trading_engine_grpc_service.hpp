#pragma once

#include "trading_engine_service.hpp"
#include "order_router.hpp"
#include "spread_calculator.hpp"
#include "trading_engine.grpc.pb.h"
#include <grpcpp/grpcpp.h>
#include <memory>
#include <unordered_map>
#include <queue>
#include <thread>
#include <atomic>

namespace ats {
namespace trading_engine {

// gRPC service implementation for Trading Engine
class TradingEngineGrpcService final : public TradingEngineService::Service {
public:
    TradingEngineGrpcService();
    ~TradingEngineGrpcService();
    
    // Initialize with trading engine components
    bool initialize(std::shared_ptr<ats::trading_engine::TradingEngineService> trading_engine,
                    std::shared_ptr<OrderRouter> order_router,
                    std::shared_ptr<SpreadCalculator> spread_calculator);
    
    // Service lifecycle
    grpc::Status StartEngine(grpc::ServerContext* context,
                           const StartEngineRequest* request,
                           StartEngineResponse* response) override;
    
    grpc::Status StopEngine(grpc::ServerContext* context,
                          const google::protobuf::Empty* request,
                          StopEngineResponse* response) override;
    
    grpc::Status GetEngineStatus(grpc::ServerContext* context,
                               const google::protobuf::Empty* request,
                               EngineStatusResponse* response) override;
    
    // Trading operations
    grpc::Status ExecuteArbitrage(grpc::ServerContext* context,
                                const ExecuteArbitrageRequest* request,
                                ExecuteArbitrageResponse* response) override;
    
    grpc::Status SubmitManualTrade(grpc::ServerContext* context,
                                 const SubmitManualTradeRequest* request,
                                 SubmitManualTradeResponse* response) override;
    
    grpc::Status CancelTrade(grpc::ServerContext* context,
                           const CancelTradeRequest* request,
                           CancelTradeResponse* response) override;
    
    // Order management
    grpc::Status GetActiveOrders(grpc::ServerContext* context,
                               const GetActiveOrdersRequest* request,
                               GetActiveOrdersResponse* response) override;
    
    grpc::Status GetOrderStatus(grpc::ServerContext* context,
                              const GetOrderStatusRequest* request,
                              GetOrderStatusResponse* response) override;
    
    grpc::Status CancelOrder(grpc::ServerContext* context,
                           const CancelOrderRequest* request,
                           CancelOrderResponse* response) override;
    
    // Portfolio and balances
    grpc::Status GetPortfolio(grpc::ServerContext* context,
                            const google::protobuf::Empty* request,
                            GetPortfolioResponse* response) override;
    
    grpc::Status GetExchangeBalances(grpc::ServerContext* context,
                                   const google::protobuf::Empty* request,
                                   GetExchangeBalancesResponse* response) override;
    
    grpc::Status GetBalance(grpc::ServerContext* context,
                          const GetBalanceRequest* request,
                          GetBalanceResponse* response) override;
    
    // Trading statistics and monitoring
    grpc::Status GetTradingStatistics(grpc::ServerContext* context,
                                    const google::protobuf::Empty* request,
                                    GetTradingStatisticsResponse* response) override;
    
    grpc::Status GetPerformanceMetrics(grpc::ServerContext* context,
                                     const GetPerformanceMetricsRequest* request,
                                     GetPerformanceMetricsResponse* response) override;
    
    grpc::Status GetHealthStatus(grpc::ServerContext* context,
                               const google::protobuf::Empty* request,
                               GetHealthStatusResponse* response) override;
    
    // Configuration management
    grpc::Status UpdateConfiguration(grpc::ServerContext* context,
                                   const UpdateConfigurationRequest* request,
                                   UpdateConfigurationResponse* response) override;
    
    grpc::Status GetConfiguration(grpc::ServerContext* context,
                                const google::protobuf::Empty* request,
                                GetConfigurationResponse* response) override;
    
    // Risk management
    grpc::Status EmergencyStop(grpc::ServerContext* context,
                             const google::protobuf::Empty* request,
                             EmergencyStopResponse* response) override;
    
    grpc::Status GetRiskStatus(grpc::ServerContext* context,
                             const google::protobuf::Empty* request,
                             GetRiskStatusResponse* response) override;
    
    // Streaming APIs
    grpc::Status StreamTradeExecutions(grpc::ServerContext* context,
                                      const google::protobuf::Empty* request,
                                      grpc::ServerWriter<TradeExecutionEvent>* writer) override;
    
    grpc::Status StreamArbitrageOpportunities(grpc::ServerContext* context,
                                            const google::protobuf::Empty* request,
                                            grpc::ServerWriter<ArbitrageOpportunityEvent>* writer) override;
    
    grpc::Status StreamOrderUpdates(grpc::ServerContext* context,
                                   const google::protobuf::Empty* request,
                                   grpc::ServerWriter<OrderUpdateEvent>* writer) override;
    
    grpc::Status StreamSystemEvents(grpc::ServerContext* context,
                                   const google::protobuf::Empty* request,
                                   grpc::ServerWriter<SystemEvent>* writer) override;

private:
    std::shared_ptr<ats::trading_engine::TradingEngineService> trading_engine_;
    std::shared_ptr<OrderRouter> order_router_;
    std::shared_ptr<SpreadCalculator> spread_calculator_;
    
    // Streaming support
    struct StreamingContext {
        std::atomic<bool> active{false};
        std::thread streaming_thread;
        std::queue<TradeExecutionEvent> trade_events;
        std::queue<ArbitrageOpportunityEvent> opportunity_events;
        std::queue<OrderUpdateEvent> order_events;
        std::queue<SystemEvent> system_events;
        std::mutex events_mutex;
        std::condition_variable events_cv;
    };
    
    std::unordered_map<grpc::ServerContext*, std::unique_ptr<StreamingContext>> streaming_contexts_;
    std::mutex streaming_mutex_;
    
    // Event handlers for streaming
    void on_trade_execution(const TradeExecution& execution);
    void on_arbitrage_opportunity(const ArbitrageOpportunity& opportunity);
    void on_order_update(const OrderExecutionDetails& order, const std::string& update_type);
    void on_system_event(const std::string& event_type, const std::string& component, 
                        const std::string& message);
    
    // Conversion helpers
    void convert_to_proto(const TradeExecution& from, TradeExecution* to);
    void convert_to_proto(const ArbitrageOpportunity& from, ArbitrageOpportunity* to);
    void convert_to_proto(const OrderExecutionDetails& from, OrderExecutionDetails* to);
    void convert_to_proto(const types::Ticker& from, Ticker* to);
    void convert_to_proto(const types::Balance& from, Balance* to);
    void convert_to_proto(const types::Order& from, Order* to);
    
    void convert_from_proto(const ArbitrageOpportunity& from, ats::trading_engine::ArbitrageOpportunity* to);
    void convert_from_proto(const Order& from, types::Order* to);
    void convert_from_proto(const TradingEngineConfiguration& from, TradingEngineConfig* to);
    
    // Streaming thread functions
    void trade_execution_streaming_thread(grpc::ServerContext* context,
                                        grpc::ServerWriter<TradeExecutionEvent>* writer);
    void arbitrage_opportunity_streaming_thread(grpc::ServerContext* context,
                                              grpc::ServerWriter<ArbitrageOpportunityEvent>* writer);
    void order_update_streaming_thread(grpc::ServerContext* context,
                                      grpc::ServerWriter<OrderUpdateEvent>* writer);
    void system_event_streaming_thread(grpc::ServerContext* context,
                                      grpc::ServerWriter<SystemEvent>* writer);
    
    // Utility methods
    google::protobuf::Timestamp to_proto_timestamp(std::chrono::system_clock::time_point time);
    std::chrono::system_clock::time_point from_proto_timestamp(const google::protobuf::Timestamp& timestamp);
    
    ExecutionResult convert_execution_result(ats::trading_engine::ExecutionResult result);
    OrderStatus convert_order_status(OrderExecutionStatus status);
    OrderSide convert_order_side(types::OrderSide side);
    OrderType convert_order_type(types::OrderType type);
    
    void cleanup_streaming_context(grpc::ServerContext* context);
};

// gRPC service implementation for Spread Calculator
class SpreadCalculatorGrpcService final : public SpreadCalculatorService::Service {
public:
    SpreadCalculatorGrpcService();
    ~SpreadCalculatorGrpcService();
    
    bool initialize(std::shared_ptr<SpreadCalculator> spread_calculator);
    
    // Spread analysis
    grpc::Status AnalyzeSpread(grpc::ServerContext* context,
                             const AnalyzeSpreadRequest* request,
                             AnalyzeSpreadResponse* response) override;
    
    grpc::Status FindBestOpportunities(grpc::ServerContext* context,
                                     const FindBestOpportunitiesRequest* request,
                                     FindBestOpportunitiesResponse* response) override;
    
    grpc::Status DetectArbitrageOpportunities(grpc::ServerContext* context,
                                            const DetectArbitrageOpportunitiesRequest* request,
                                            DetectArbitrageOpportunitiesResponse* response) override;
    
    // Fee and slippage calculations
    grpc::Status CalculateTradingFee(grpc::ServerContext* context,
                                   const CalculateTradingFeeRequest* request,
                                   CalculateTradingFeeResponse* response) override;
    
    grpc::Status EstimateSlippage(grpc::ServerContext* context,
                                const EstimateSlippageRequest* request,
                                EstimateSlippageResponse* response) override;
    
    grpc::Status CalculateBreakevenSpread(grpc::ServerContext* context,
                                        const CalculateBreakevenSpreadRequest* request,
                                        CalculateBreakevenSpreadResponse* response) override;
    
    // Market data updates
    grpc::Status UpdateMarketDepth(grpc::ServerContext* context,
                                 const UpdateMarketDepthRequest* request,
                                 UpdateMarketDepthResponse* response) override;
    
    grpc::Status UpdateTicker(grpc::ServerContext* context,
                            const UpdateTickerRequest* request,
                            UpdateTickerResponse* response) override;
    
    // Configuration
    grpc::Status UpdateFeeStructures(grpc::ServerContext* context,
                                   const UpdateFeeStructuresRequest* request,
                                   UpdateFeeStructuresResponse* response) override;
    
    grpc::Status UpdateSlippageModels(grpc::ServerContext* context,
                                    const UpdateSlippageModelsRequest* request,
                                    UpdateSlippageModelsResponse* response) override;
    
    // Statistics
    grpc::Status GetSpreadStatistics(grpc::ServerContext* context,
                                   const google::protobuf::Empty* request,
                                   GetSpreadStatisticsResponse* response) override;

private:
    std::shared_ptr<SpreadCalculator> spread_calculator_;
    
    // Conversion helpers
    void convert_to_proto(const ats::trading_engine::SpreadAnalysis& from, SpreadAnalysis* to);
    void convert_to_proto(const ats::trading_engine::MarketDepth& from, MarketDepth* to);
    void convert_from_proto(const MarketDepth& from, ats::trading_engine::MarketDepth* to);
    void convert_from_proto(const Ticker& from, types::Ticker* to);
};

// gRPC service implementation for Order Router
class OrderRouterGrpcService final : public OrderRouterService::Service {
public:
    OrderRouterGrpcService();
    ~OrderRouterGrpcService();
    
    bool initialize(std::shared_ptr<OrderRouter> order_router);
    
    // Order operations
    grpc::Status PlaceOrder(grpc::ServerContext* context,
                          const PlaceOrderRequest* request,
                          PlaceOrderResponse* response) override;
    
    grpc::Status CancelOrder(grpc::ServerContext* context,
                           const CancelOrderRequest* request,
                           CancelOrderResponse* response) override;
    
    grpc::Status ModifyOrder(grpc::ServerContext* context,
                           const ModifyOrderRequest* request,
                           ModifyOrderResponse* response) override;
    
    // Simultaneous execution
    grpc::Status ExecuteArbitrageOrders(grpc::ServerContext* context,
                                      const ExecuteArbitrageOrdersRequest* request,
                                      ExecuteArbitrageOrdersResponse* response) override;
    
    // Order monitoring
    grpc::Status GetOrderStatus(grpc::ServerContext* context,
                              const GetOrderStatusRequest* request,
                              GetOrderStatusResponse* response) override;
    
    grpc::Status GetActiveOrders(grpc::ServerContext* context,
                               const GetActiveOrdersRequest* request,
                               GetActiveOrdersResponse* response) override;
    
    // Balance and portfolio
    grpc::Status GetAllBalances(grpc::ServerContext* context,
                              const google::protobuf::Empty* request,
                              GetAllBalancesResponse* response) override;
    
    grpc::Status GetExchangeBalance(grpc::ServerContext* context,
                                  const GetExchangeBalanceRequest* request,
                                  GetExchangeBalanceResponse* response) override;
    
    // Performance metrics
    grpc::Status GetPerformanceMetrics(grpc::ServerContext* context,
                                     const google::protobuf::Empty* request,
                                     OrderRouterPerformanceResponse* response) override;
    
    grpc::Status GetHealthStatus(grpc::ServerContext* context,
                               const google::protobuf::Empty* request,
                               OrderRouterHealthResponse* response) override;
    
    // Configuration
    grpc::Status UpdateConfiguration(grpc::ServerContext* context,
                                   const UpdateOrderRouterConfigRequest* request,
                                   UpdateOrderRouterConfigResponse* response) override;
    
    grpc::Status AddExchange(grpc::ServerContext* context,
                           const AddExchangeRequest* request,
                           AddExchangeResponse* response) override;
    
    grpc::Status RemoveExchange(grpc::ServerContext* context,
                              const RemoveExchangeRequest* request,
                              RemoveExchangeResponse* response) override;

private:
    std::shared_ptr<OrderRouter> order_router_;
    
    // Conversion helpers
    void convert_to_proto(const ats::trading_engine::SimultaneousExecutionResult& from, 
                         SimultaneousExecutionResult* to);
    void convert_to_proto(const OrderRouter::PerformanceMetrics& from,
                         OrderRouterPerformanceResponse* to);
    void convert_from_proto(const OrderRouterConfiguration& from, OrderRouterConfig* to);
};

// gRPC server manager
class TradingEngineGrpcServer {
public:
    TradingEngineGrpcServer();
    ~TradingEngineGrpcServer();
    
    // Server lifecycle
    bool initialize(const std::string& server_address,
                    std::shared_ptr<ats::trading_engine::TradingEngineService> trading_engine,
                    std::shared_ptr<OrderRouter> order_router,
                    std::shared_ptr<SpreadCalculator> spread_calculator);
    
    bool start();
    void stop();
    bool is_running() const;
    
    // Server configuration
    void set_max_receive_message_size(int size);
    void set_max_send_message_size(int size);
    void enable_reflection(bool enable);
    void enable_health_check(bool enable);
    
    // SSL/TLS configuration
    bool configure_ssl(const std::string& cert_file, const std::string& key_file);
    bool configure_client_auth(const std::string& ca_cert_file);
    
    // Monitoring
    size_t get_active_connections() const;
    std::vector<std::string> get_connected_clients() const;
    
private:
    std::unique_ptr<grpc::Server> server_;
    std::unique_ptr<TradingEngineGrpcService> trading_engine_service_;
    std::unique_ptr<SpreadCalculatorGrpcService> spread_calculator_service_;
    std::unique_ptr<OrderRouterGrpcService> order_router_service_;
    
    std::string server_address_;
    std::atomic<bool> running_{false};
    
    grpc::ServerBuilder builder_;
    
    // Server configuration
    bool ssl_enabled_ = false;
    bool reflection_enabled_ = false;
    bool health_check_enabled_ = false;
    int max_receive_message_size_ = 4 * 1024 * 1024; // 4MB
    int max_send_message_size_ = 4 * 1024 * 1024;    // 4MB
};

} // namespace trading_engine
} // namespace ats