#include "quota_fabric/policy/policy.hpp"

namespace quota_fabric {

bool QuotaPolicy::validate(std::string* error) const {
  auto bad = [&](std::string m) { if (error) *error = std::move(m); return false; };
  if (name.empty()) return bad("policy must have a name");
  if (max_hierarchy_depth < 1 || max_hierarchy_depth > 4096) return bad("max_hierarchy_depth out of range");
  if (max_tenants == 0 || max_tenants > 100'000'000) return bad("max_tenants out of range");
  if (max_reservations == 0) return bad("max_reservations must be positive");
  if (max_windows_sample == 0) return bad("max_windows_sample must be positive");
  if (overcommit.mode != OvercommitMode::NONE && overcommit.factor < 1.0) return bad("overcommit factor must be >= 1");
  if (overcommit.mode == OvercommitMode::NONE && overcommit.factor != 1.0) return bad("overcommit factor must be 1.0 when mode is NONE");
  if (fairness.starvation_weight < 0 || fairness.debt_weight < 0 || fairness.use_weight < 0) return bad("fairness weights must be non-negative");
  if (recall.recall_grace_nanos < 0) return bad("recall grace must be non-negative");
  if (default_burst.window < 0 || default_burst.cooldown < 0) return bad("burst window/cooldown must be non-negative");
  if (default_burst.percent_above_soft < 0) return bad("burst percent must be non-negative");
  for (const auto& [tid, q] : tenant_overrides) {
    (void)tid;
    for (const auto d : all_resource_dimensions()) {
      if (q.guaranteed.present(d) && q.hard_limit.present(d) && q.guaranteed.get(d) > q.hard_limit.get(d)) return bad("tenant override guarantee > hard");
      if (q.soft_limit.present(d) && q.hard_limit.present(d) && q.soft_limit.get(d) > q.hard_limit.get(d)) return bad("tenant override soft > hard");
    }
  }
  return true;
}

}  // namespace quota_fabric
