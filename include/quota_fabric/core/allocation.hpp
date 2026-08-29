#pragma once
#include "quota_fabric/core/identity.hpp"
#include "quota_fabric/core/resource_vector.hpp"
#include "quota_fabric/core/time.hpp"

#include <string>

namespace quota_fabric {

// A live consumption unit. Distinct from a Reservation: a reservation is the
// authority to consume; an allocation is the active consumption on a backend.
struct Allocation {
  AllocationId id;
  TenantId tenant;
  ReservationId reservation;
  ResourceClassId resource_class;
  ResourceVector resources;   // committed capacity
  ResourceVector used;        // bytes/elements actually used
  Nanos started_at = 0;
  Nanos ended_at = 0;
  bool active = false;
  QuotaGeneration quota_generation;
  PolicyGeneration policy_generation;
  CoordinatorEpoch epoch;
  AgentId owner_agent;
  AgentBootId owner_boot;
  std::string label;
};

}  // namespace quota_fabric
