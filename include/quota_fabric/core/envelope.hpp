#pragma once
#include "quota_fabric/core/quota.hpp"
#include "quota_fabric/core/explanation.hpp"

#include <optional>

namespace quota_fabric {

// The canonical inspected "how much is left" contract for a tenant/resource.
// Signed *available* values may be negative only where a genuine deficit exists;
// stored usage and limits are always non-negative. Comprehension is explicit:
// a reader can always tell guaranteed vs borrowed vs bust vs reserved.
struct QuotaEnvelope {
  TenantId tenant;
  ResourceClassId resource_class;

  ResourceVector guaranteed;
  ResourceVector soft_limit;
  ResourceVector hard_limit;
  ResourceVector burst_limit;

  ResourceVector current_consumption;  // actively consumed
  ResourceVector reserved;             // reserved but not yet consumed
  ResourceVector committed_usage;      // consumption + reserved
  ResourceVector borrowed;             // consumed from others' guarantee
  ResourceVector burst_usage;          // amount currently in burst
  ResourceVector lent;                 // amount lent to others
  ResourceVector debt;

  SignedResourceVector available_guaranteed;
  SignedResourceVector available_soft;
  SignedResourceVector available_burst;

  ComplianceState compliance = ComplianceState::COMPLIANT;
  std::optional<ViolationCode> primary_violation;

  QuotaGeneration generation;
  PolicyGeneration policy_generation;
  CoordinatorEpoch epoch;

  Nanos burst_expires_at = 0;   // monotonic instant; 0 means none outstanding
  bool burst_active = false;

  std::string to_string() const;
};

}  // namespace quota_fabric
