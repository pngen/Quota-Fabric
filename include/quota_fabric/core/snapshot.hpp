#pragma once
#include "quota_fabric/core/hierarchy.hpp"
#include "quota_fabric/core/envelope.hpp"
#include "quota_fabric/core/reservation.hpp"
#include "quota_fabric/core/borrow.hpp"
#include "quota_fabric/core/allocation.hpp"
#include "quota_fabric/core/event.hpp"

#include <vector>

namespace quota_fabric {

// An immutable, deterministic snapshot of the whole governance surface.
struct QuotaSnapshot {
  CoordinatorEpoch epoch;
  QuotaGeneration quota_generation;
  PolicyGeneration policy_generation;

  std::vector<TenantGroup> groups;
  std::vector<Tenant> tenants;
  std::vector<ResourceQuota> quotas;        // tenant-local + group lattices
  std::vector<QuotaEnvelope> envelopes;
  std::vector<Reservation> reservations;
  std::vector<Allocation> allocations;
  std::vector<BorrowRecord> borrow_records;
  std::vector<LendingRecord> lending_records;
  std::vector<QuotaEvent> recent_events;
  std::vector<std::pair<TenantId, ResourceVector>> debts;
  std::vector<std::string> violations;
  std::uint64_t sequence = 0;
};

}  // namespace quota_fabric
