#pragma once
#include "quota_fabric/core/quota.hpp"
#include "quota_fabric/core/snapshot.hpp"   // for TenantGroupKind? no; keep minimal
#include "quota_fabric/core/hierarchy.hpp"

#include <string>
#include <vector>
#include <unordered_map>
#include <optional>

namespace quota_fabric {

struct FairnessPolicy {
  double starvation_weight = 1.0;  // influence of starvation age
  double debt_weight = 1.0;        // influence of entitlement debt
  double use_weight = 1.0;         // influence of recent/current use
  double priority_weight = 1.0;
};

struct OvercommitPolicy {
  OvercommitMode mode = OvercommitMode::NONE;
  double factor = 1.0;             // allowed logical multiplier of physical
  std::vector<ResourceDimension> affected_resources;
  bool allow_if_guarantees_covered = true;
};

struct AttributionPolicy {
  AttributionModel model = AttributionModel::SHARED_SYSTEM_POOL;
  // per-resource-class override
  std::vector<std::pair<ResourceClassId, AttributionModel>> per_class;
  bool allow_proportional_dedup = true;
};

struct RecallPolicy {
  std::uint32_t recall_grace_nanos = 0;   // grace before recall becomes actionable
  std::uint32_t max_recalled_before_demand = 0;
  bool auto_emit_recall = true;
};

struct HierarchyPolicy {
  bool inherit_parent = true;         // children inherit parent lattice
  bool allow_child_override = true;   // children may override inherited values
  bool enforce_parent_ceiling = true; // child lattice capped by parent
  bool allow_sibling_borrowing = false;
};

// A versioned, validated, atomically replaceable policy document. Every quota
// transaction records the policy generation it ran under, so historical state
// always retains its governing policy.
struct QuotaPolicy {
  PolicyGeneration generation;
  std::string name;
  std::string description;
  std::uint64_t created_at = 0;

  FairnessPolicy fairness;
  OvercommitPolicy overcommit;
  AttributionPolicy attribution;
  RecallPolicy recall;
  HierarchyPolicy hierarchy;

  // default burst/borrow rules applied to tenants without a lattice rule
  BurstRule default_burst;
  BorrowRule default_borrow;

  // tenant/group/resource-specific overrides
  std::unordered_map<TenantId, ResourceQuota> tenant_overrides;
  std::unordered_map<TenantGroupId, ResourceQuota> group_overrides;
  std::vector<ResourceClassId> resource_specific_rules;

  // bounds / validation limits
  std::uint32_t max_hierarchy_depth = 32;
  std::uint32_t max_tenants = 1'000'000;
  std::uint32_t max_reservations = 1'000'000;
  std::uint64_t max_windows_sample = 4096;

  bool validate(std::string* error) const;
  PolicyGeneration policy_generation_of() const noexcept { return generation; }
};

}  // namespace quota_fabric
