#pragma once
#include "quota_fabric/core/identity.hpp"
#include "quota_fabric/core/resource_vector.hpp"
#include "quota_fabric/core/enums.hpp"
#include "quota_fabric/core/time.hpp"

#include <string>
#include <vector>
#include <optional>

namespace quota_fabric {

// Bounded burst rules. Burst never exceeds hard physical/governed ceilings and
// is always time-bounded (no "infinite" temporary burst).
struct BurstRule {
  double percent_above_soft = 0.0;  // burst as a share above soft/guarantee
  ResourceVector absolute_burst;    // explicit absolute burst capacity
  Nanos window = 0;                 // burst time window (monotonic)
  std::uint32_t max_simultaneous = 0;
  Nanos cooldown = 0;               // cooldown before another burst
  bool allow_debt = false;          // whether burst may create entitlement debt
  bool operator==(const BurstRule&) const = default;
};

// Explicit borrow/lend rules. Borrowing never destroys the lender's guarantee.
struct BorrowRule {
  bool borrow_enabled = false;
  bool lend_enabled = false;
  bool recall_before_new_lend = true;  // outstanding borrows must be recallable
  Nanos max_borrow_duration = 0;       // 0 = no explicit cap
  bool preserves_guarantee = true;     // lending must keep guarantee restorable
  std::uint32_t max_borrows = 0;
  bool operator==(const BorrowRule&) const = default;
};

// A tenant/resource-class quota lattice: every limit kind is a per-dimension
// vector, so multidimensional quotas are the default rather than an extension.
struct ResourceQuota {
  TenantId tenant;
  ResourceClassId resource_class;

  ResourceVector guaranteed;    // protected baseline
  ResourceVector soft_limit;    // preferred ceiling (observable exceedance)
  ResourceVector hard_limit;    // absolute ceiling
  ResourceVector burst_limit;   // bounded burst headroom (max simultaneous)
  ResourceVector borrowable;    // headroom that may be lent to others
  ResourceVector physical_capacity;  // physical cap for this resource class

  BurstRule burst_rule;
  BorrowRule borrow_rule;

  OvercommitMode overcommit = OvercommitMode::NONE;
  double overcommit_factor = 1.0;

  ResourceVector shared_pool;    // chargeable to a shared/system pool

  QuotaGeneration generation;
  PolicyGeneration policy_generation;

  bool enforces_hard() const noexcept { return !hard_limit.is_empty(); }
  bool has_guarantee() const noexcept { return !guaranteed.is_empty(); }
};

}  // namespace quota_fabric
