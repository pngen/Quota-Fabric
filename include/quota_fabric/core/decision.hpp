#pragma once
#include "quota_fabric/core/envelope.hpp"
#include "quota_fabric/core/explanation.hpp"

namespace quota_fabric {

// A typed governing decision. A decision is for a requested ResourceVector; the
// limiting dimension (if any) is nominated so callers can see which resource
// actually constrained the request.
struct QuotaDecision {
  DecisionCode code = DecisionCode::ALLOW;

  TenantId tenant;
  ResourceClassId resource_class;
  ResourceVector requested;

  // Snapshot of the state at evaluation time.
  ResourceVector current_usage;
  ResourceVector reserved_usage;
  ResourceVector guarantee;
  ResourceVector soft_limit;
  ResourceVector hard_limit;
  ResourceVector burst;
  ResourceVector borrowed;
  ResourceVector debt;
  ResourceVector parent_available;

  std::optional<ResourceDimension> limiting_dimension;
  ComplianceState compliance = ComplianceState::COMPLIANT;
  RecallAction recall_action = RecallAction::NO_ACTION;

  QuotaGeneration generation;
  PolicyGeneration policy_generation;
  CoordinatorEpoch epoch;

  Explanation explanation;

  bool allowed() const noexcept {
    return code == DecisionCode::ALLOW || code == DecisionCode::ALLOW_GUARANTEED ||
           code == DecisionCode::ALLOW_BURST || code == DecisionCode::ALLOW_BORROW;
  }
  bool via_guarantee() const noexcept { return code == DecisionCode::ALLOW_GUARANTEED; }
  bool via_burst() const noexcept { return code == DecisionCode::ALLOW_BURST; }
  bool via_borrow() const noexcept { return code == DecisionCode::ALLOW_BORROW; }
};

}  // namespace quota_fabric
