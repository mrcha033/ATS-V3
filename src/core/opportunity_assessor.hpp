#pragma once

#include "types.hpp"

namespace ats {

class RiskManager;

class OpportunityAssessor {
public:
    OpportunityAssessor(const RiskManager& risk_manager);
    RiskAssessment AssessOpportunity(const ArbitrageOpportunity& opportunity) const;

private:
    const RiskManager& risk_manager_;
};

} // namespace ats
