#include "opportunity_assessor.hpp"
#include "risk_manager.hpp"
#include "../utils/logger.hpp"

namespace ats {

OpportunityAssessor::OpportunityAssessor(const RiskManager& risk_manager)
    : risk_manager_(risk_manager) {}

RiskAssessment OpportunityAssessor::AssessOpportunity(const ArbitrageOpportunity& opportunity) const {
    RiskAssessment assessment;
    
    try {
        // Check if trading is allowed
        if (risk_manager_.IsKillSwitchActive() || risk_manager_.IsTradingHalted()) {
            assessment.rejections.push_back("Trading is halted");
            return assessment;
        }
        
        // Check basic opportunity validity
        if (!opportunity.is_executable) {
            assessment.rejections.push_back("Opportunity is not executable");
            return assessment;
        }
        
        // Check reward:risk ratio
        double max_position = risk_manager_.CalculateMaxPositionSize(opportunity);
        if (max_position <= 0) {
            assessment.rejections.push_back("Position size limit exceeded");
            return assessment;
        }
        
        double reward_risk_ratio = risk_manager_.CalculateRewardRiskRatio(opportunity, max_position);
        if (reward_risk_ratio < risk_manager_.GetLimits().min_reward_risk_ratio) {
            assessment.rejections.push_back("Reward:risk ratio below minimum threshold");
            LOG_DEBUG("Opportunity rejected: reward:risk ratio {} < minimum {}", 
                     reward_risk_ratio, risk_manager_.GetLimits().min_reward_risk_ratio);
            return assessment;
        }
        
        // Check minimum profit threshold after reward:risk validation
        if (opportunity.net_profit_percent < 0.05) { // 0.05% minimum profit
            assessment.rejections.push_back("Profit below minimum threshold");
            return assessment;
        }
        
        // Check daily loss limits
        if (!risk_manager_.CheckLossLimits()) {
            assessment.rejections.push_back("Daily loss limit reached");
            return assessment;
        }
        
        // Check trade rate limits
        if (!risk_manager_.CheckTradeRate()) {
            assessment.rejections.push_back("Trade rate limit exceeded");
            return assessment;
        }
        
        // Calculate risk score
        assessment.risk_score = risk_manager_.CalculatePositionRisk(opportunity, max_position);
        assessment.position_size_limit = max_position;
        assessment.is_approved = true;
        
    } catch (const std::exception& e) {
        LOG_ERROR("Error assessing opportunity: {}", e.what());
        assessment.rejections.push_back("Assessment error");
    }
    
    return assessment;
}

} // namespace ats
