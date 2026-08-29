#pragma once
#include "quota_fabric/core/quota.hpp"

#include <string>
#include <vector>

namespace quota_fabric {

struct BorrowRecord {
  BorrowId id;
  TenantId borrower;
  TenantId lender;
  LendingId lending;        // the lending offer this borrow draws from
  std::optional<ReservationId> reservation;  // reservation that triggered the borrow, if any
  ResourceClassId resource_class;
  ResourceVector amount;      // borrowed (max)
  ResourceVector consumed;    // amount of borrowed capacity in use
  Nanos started_at = 0;
  Nanos expires_at = 0;       // 0 = policy default/never
  std::uint32_t recall_priority = 0;  // lower recalled first
  ResourceVector debt;        // entitlement debt implied by this borrow
  PolicyGeneration policy_generation;
  QuotaGeneration quota_generation;
  CoordinatorEpoch epoch;
  bool recalled = false;      // recall decision emitted
};

struct LendingRecord {
  LendingId id;
  TenantId lender;
  TenantId borrower;
  ResourceClassId resource_class;
  ResourceVector amount;      // lent (max)
  ResourceVector outstanding; // still outstanding (consumed by borrower)
  Nanos started_at = 0;
  Nanos expires_at = 0;
  std::uint32_t recall_priority = 0;
  bool revocable = true;
  PolicyGeneration policy_generation;
  QuotaGeneration quota_generation;
  CoordinatorEpoch epoch;
};

}  // namespace quota_fabric
