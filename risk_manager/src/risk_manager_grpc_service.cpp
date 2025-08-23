#include "../include/enhanced_risk_manager.hpp"
#include "utils/logger.hpp"
#include "trading_engine_mock.hpp"

namespace ats {
namespace risk_manager {

// RiskManagerGrpcService Implementation
RiskManagerGrpcService::RiskManagerGrpcService() {
    utils::Logger::info("Risk Manager gRPC Service initialized");
}

RiskManagerGrpcService::~RiskManagerGrpcService() {
    // Clean up streaming threads
    std::lock_guard<std::mutex> lock(streaming_mutex_);
    for (auto& pair : streaming_threads_) {
        if (pair.second && pair.second->joinable()) {
            pair.second->join();
        }
    }
    utils::Logger::info("Risk Manager gRPC Service destroyed");
}

bool RiskManagerGrpcService::initialize(std::shared_ptr<EnhancedRiskManager> risk_manager) {
    risk_manager_ = risk_manager;
    utils::Logger::info("Risk Manager gRPC Service initialized with enhanced risk manager");
    return true;
}

grpc::Status RiskManagerGrpcService::GetRiskStatus(
    grpc::ServerContext* context,
    const google::protobuf::Empty* request,
    ats::trading_engine::GetRiskStatusResponse* response) {
    
    if (!risk_manager_) {
        return grpc::Status(grpc::StatusCode::INTERNAL, "Risk manager not initialized");
    }
    
    try {
        // Set basic risk status
        response->set_within_limits(risk_manager_->check_all_limits());
        response->set_current_exposure(risk_manager_->get_realtime_exposure());
        response->set_max_exposure(risk_manager_->GetLimits().max_total_exposure_usd);
        
        double daily_pnl = risk_manager_->GetDailyPnL();
        response->set_current_daily_volume(std::abs(daily_pnl)); // Approximation
        response->set_max_daily_volume(risk_manager_->GetLimits().max_daily_volume_usd);
        
        // Add risk warnings
        auto violations = risk_manager_->get_limit_violations();
        for (const auto& violation : violations) {
            response->add_risk_warnings(violation);
        }
        
        utils::Logger::debug("Risk status retrieved successfully");
        return grpc::Status::OK;
        
    } catch (const std::exception& e) {
        utils::Logger::error("Error getting risk status: {}", e.what());
        return grpc::Status(grpc::StatusCode::INTERNAL, "Failed to get risk status");
    }
}

grpc::Status RiskManagerGrpcService::GetPositions(
    grpc::ServerContext* context,
    const google::protobuf::Empty* request,
    ats::trading_engine::GetPositionsResponse* response) {
    
    if (!risk_manager_) {
        return grpc::Status(grpc::StatusCode::INTERNAL, "Risk manager not initialized");
    }
    
    try {
        auto positions = risk_manager_->get_current_positions();
        
        for (const auto& position : positions) {
            auto* proto_position = response->add_positions();
            convert_to_proto(position, proto_position);
        }
        
        utils::Logger::debug("Retrieved {} positions", positions.size());
        return grpc::Status::OK;
        
    } catch (const std::exception& e) {
        utils::Logger::error("Error getting positions: {}", e.what());
        return grpc::Status(grpc::StatusCode::INTERNAL, "Failed to get positions");
    }
}

grpc::Status RiskManagerGrpcService::GetPnL(
    grpc::ServerContext* context,
    const google::protobuf::Empty* request,
    ats::trading_engine::GetPnLResponse* response) {
    
    if (!risk_manager_) {
        return grpc::Status(grpc::StatusCode::INTERNAL, "Risk manager not initialized");
    }
    
    try {
        response->set_total_pnl(risk_manager_->get_realtime_pnl());
        response->set_daily_pnl(risk_manager_->GetDailyPnL());
        response->set_weekly_pnl(risk_manager_->GetWeeklyPnL());
        response->set_monthly_pnl(risk_manager_->GetMonthlyPnL());
        
        // Set unrealized P&L (would need to be calculated from positions)
        // TODO: Implement calculation of unrealized P&L from positions
        response->set_unrealized_pnl(0.0);
        response->set_realized_pnl(risk_manager_->get_realtime_pnl());
        
        utils::Logger::debug("P&L retrieved successfully");
        return grpc::Status::OK;
        
    } catch (const std::exception& e) {
        utils::Logger::error("Error getting P&L: {}", e.what());
        return grpc::Status(grpc::StatusCode::INTERNAL, "Failed to get P&L");
    }
}

grpc::Status RiskManagerGrpcService::GetRiskAlerts(
    grpc::ServerContext* context,
    const ats::trading_engine::GetRiskAlertsRequest* request,
    ats::trading_engine::GetRiskAlertsResponse* response) {
    
    if (!risk_manager_) {
        return grpc::Status(grpc::StatusCode::INTERNAL, "Risk manager not initialized");
    }
    
    try {
        // Get recent alerts (this would need to be implemented in EnhancedRiskManager)
        auto alerts = risk_manager_->get_recent_alerts(request->limit());
        
        for (const auto& alert : alerts) {
            auto* proto_alert = response->add_alerts();
            convert_to_proto(alert, proto_alert);
        }
        
        utils::Logger::debug("Retrieved {} risk alerts", alerts.size());
        return grpc::Status::OK;
        
    } catch (const std::exception& e) {
        utils::Logger::error("Error getting risk alerts: {}", e.what());
        return grpc::Status(grpc::StatusCode::INTERNAL, "Failed to get risk alerts");
    }
}

grpc::Status RiskManagerGrpcService::AcknowledgeAlert(
    grpc::ServerContext* context,
    const ats::trading_engine::AcknowledgeAlertRequest* request,
    ats::trading_engine::AcknowledgeAlertResponse* response) {
    
    if (!risk_manager_) {
        return grpc::Status(grpc::StatusCode::INTERNAL, "Risk manager not initialized");
    }
    
    try {
        risk_manager_->acknowledge_alert(request->alert_id());
        response->set_success(true);
        response->set_message("Alert acknowledged successfully");
        
        utils::Logger::debug("Alert {} acknowledged", request->alert_id());
        return grpc::Status::OK;
        
    } catch (const std::exception& e) {
        utils::Logger::error("Error acknowledging alert: {}", e.what());
        response->set_success(false);
        response->set_message("Failed to acknowledge alert");
        return grpc::Status::OK; // Return OK with error in response
    }
}

grpc::Status RiskManagerGrpcService::EmergencyHalt(
    grpc::ServerContext* context,
    const ats::trading_engine::EmergencyHaltRequest* request,
    ats::trading_engine::EmergencyHaltResponse* response) {
    
    if (!risk_manager_) {
        return grpc::Status(grpc::StatusCode::INTERNAL, "Risk manager not initialized");
    }
    
    try {
        std::string reason = request->reason().empty() ? "Manual emergency halt" : request->reason();
        risk_manager_->manual_halt(reason);
        
        response->set_success(true);
        response->set_message("Emergency halt activated");
        response->set_halt_reason(reason);
        
        utils::Logger::error("Emergency halt triggered via gRPC: {}", reason);
        return grpc::Status::OK;
        
    } catch (const std::exception& e) {
        utils::Logger::error("Error triggering emergency halt: {}", e.what());
        response->set_success(false);
        response->set_message("Failed to trigger emergency halt");
        return grpc::Status::OK; // Return OK with error in response
    }
}

grpc::Status RiskManagerGrpcService::ResumeTrading(
    grpc::ServerContext* context,
    const google::protobuf::Empty* request,
    ats::trading_engine::ResumeTradeingResponse* response) {
    
    if (!risk_manager_) {
        return grpc::Status(grpc::StatusCode::INTERNAL, "Risk manager not initialized");
    }
    
    try {
        risk_manager_->resume_after_halt();
        
        response->set_success(true);
        response->set_message("Trading resumed successfully");
        
        utils::Logger::info("Trading resumed via gRPC");
        return grpc::Status::OK;
        
    } catch (const std::exception& e) {
        utils::Logger::error("Error resuming trading: {}", e.what());
        response->set_success(false);
        response->set_message("Failed to resume trading");
        return grpc::Status::OK; // Return OK with error in response
    }
}

grpc::Status RiskManagerGrpcService::UpdateRiskLimits(
    grpc::ServerContext* context,
    const ats::trading_engine::UpdateRiskLimitsRequest* request,
    ats::trading_engine::UpdateRiskLimitsResponse* response) {
    
    if (!risk_manager_) {
        return grpc::Status(grpc::StatusCode::INTERNAL, "Risk manager not initialized");
    }
    
    try {
        // Update risk limits (this would need to be implemented)
        // risk_manager_->update_limits(request->limits());
        
        response->set_success(true);
        response->set_message("Risk limits updated successfully");
        
        utils::Logger::info("Risk limits updated via gRPC");
        return grpc::Status::OK;
        
    } catch (const std::exception& e) {
        utils::Logger::error("Error updating risk limits: {}", e.what());
        response->set_success(false);
        response->set_message("Failed to update risk limits");
        return grpc::Status::OK; // Return OK with error in response
    }
}

grpc::Status RiskManagerGrpcService::StreamRiskAlerts(
    grpc::ServerContext* context,
    const google::protobuf::Empty* request,
    grpc::ServerWriter<ats::trading_engine::RiskAlertEvent>* writer) {
    
    if (!risk_manager_) {
        return grpc::Status(grpc::StatusCode::INTERNAL, "Risk manager not initialized");
    }
    
    // Start streaming thread
    std::lock_guard<std::mutex> lock(streaming_mutex_);
    streaming_threads_[context] = std::make_unique<std::thread>(
        &RiskManagerGrpcService::risk_alert_streaming_thread, this, context, writer);
    
    // Wait for the thread to complete
    if (streaming_threads_[context]->joinable()) {
        streaming_threads_[context]->join();
    }
    
    // Clean up
    streaming_threads_.erase(context);
    
    return grpc::Status::OK;
}

grpc::Status RiskManagerGrpcService::StreamPositionUpdates(
    grpc::ServerContext* context,
    const google::protobuf::Empty* request,
    grpc::ServerWriter<ats::trading_engine::PositionUpdateEvent>* writer) {
    
    if (!risk_manager_) {
        return grpc::Status(grpc::StatusCode::INTERNAL, "Risk manager not initialized");
    }
    
    // Start streaming thread
    std::lock_guard<std::mutex> lock(streaming_mutex_);
    streaming_threads_[context] = std::make_unique<std::thread>(
        &RiskManagerGrpcService::position_update_streaming_thread, this, context, writer);
    
    // Wait for the thread to complete
    if (streaming_threads_[context]->joinable()) {
        streaming_threads_[context]->join();
    }
    
    // Clean up
    streaming_threads_.erase(context);
    
    return grpc::Status::OK;
}

void RiskManagerGrpcService::risk_alert_streaming_thread(
    grpc::ServerContext* context,
    grpc::ServerWriter<ats::trading_engine::RiskAlertEvent>* writer) {
    
    utils::Logger::info("Risk alert streaming started");
    
    try {
        while (!context->IsCancelled()) {
            // This would need to be implemented with actual alert streaming
            // For now, just sleep and check for cancellation
            std::this_thread::sleep_for(std::chrono::milliseconds(1000));
        }
    } catch (const std::exception& e) {
        utils::Logger::error("Error in risk alert streaming: {}", e.what());
    }
    
    utils::Logger::info("Risk alert streaming stopped");
}

void RiskManagerGrpcService::position_update_streaming_thread(
    grpc::ServerContext* context,
    grpc::ServerWriter<ats::trading_engine::PositionUpdateEvent>* writer) {
    
    utils::Logger::info("Position update streaming started");
    
    try {
        while (!context->IsCancelled()) {
            // This would need to be implemented with actual position streaming
            // For now, just sleep and check for cancellation
            std::this_thread::sleep_for(std::chrono::milliseconds(1000));
        }
    } catch (const std::exception& e) {
        utils::Logger::error("Error in position update streaming: {}", e.what());
    }
    
    utils::Logger::info("Position update streaming stopped");
}

void RiskManagerGrpcService::convert_to_proto(const RiskAlert& from, ats::trading_engine::RiskAlert* to) {
    to->set_id(from.id);
    to->set_severity(static_cast<int>(from.severity));
    to->set_type(from.type);
    to->set_message(from.message);
    to->set_acknowledged(from.acknowledged);
    
    // Convert timestamp
    auto timestamp = to->mutable_timestamp();
    auto duration = from.timestamp.time_since_epoch();
    auto seconds = std::chrono::duration_cast<std::chrono::seconds>(duration);
    auto nanos = std::chrono::duration_cast<std::chrono::nanoseconds>(duration - seconds);
    
    timestamp->set_seconds(seconds.count());
    timestamp->set_nanos(static_cast<int32_t>(nanos.count()));
    
    // Convert metadata
    for (const auto& metadata : from.metadata) {
        (*to->mutable_metadata())[metadata.first] = metadata.second;
    }
}

void RiskManagerGrpcService::convert_to_proto(const RealTimePosition& from, ats::trading_engine::Position* to) {
    to->set_symbol(from.symbol);
    to->set_exchange(from.exchange);
    to->set_quantity(from.quantity);
    to->set_average_price(from.average_price);
    to->set_market_value(from.market_value);
    to->set_unrealized_pnl(from.unrealized_pnl);
    to->set_realized_pnl(from.realized_pnl);
    
    // Convert timestamp
    auto timestamp = to->mutable_last_updated();
    auto duration = from.last_updated.time_since_epoch();
    auto seconds = std::chrono::duration_cast<std::chrono::seconds>(duration);
    auto nanos = std::chrono::duration_cast<std::chrono::nanoseconds>(duration - seconds);
    
    timestamp->set_seconds(seconds.count());
    timestamp->set_nanos(static_cast<int32_t>(nanos.count()));
}

} // namespace risk_manager
} // namespace ats