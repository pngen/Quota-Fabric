#pragma once
#include "quota_fabric/core/enums.hpp"
#include "quota_fabric/core/time.hpp"
#include "quota_fabric/core/identity.hpp"
#include "quota_fabric/core/resource_vector.hpp"

#include <string>
#include <optional>

namespace quota_fabric {

// A bounded, append-only governance event. Retention is bounded by the engine.
struct QuotaEvent {
  EventType type = EventType::QUOTA_UPDATE;
  Nanos at = 0;
  TenantId tenant;
  std::optional<TenantGroupId> group;
  std::optional<ReservationId> reservation;
  std::optional<AllocationId> allocation;
  std::optional<BorrowId> borrow;
  std::optional<LendingId> lend;
  std::optional<ResourceDimension> dimension;
  std::int64_t amount = 0;
  QuotaGeneration quota_generation;
  PolicyGeneration policy_generation;
  CoordinatorEpoch epoch;
  std::optional<ViolationCode> violation;
  std::string detail;
};

}  // namespace quota_fabric
