#pragma once
#include "quota_fabric/core/allocation.hpp"
#include "quota_fabric/core/enums.hpp"

namespace quota_fabric {

struct Reservation {
  ReservationId id;
  TenantId tenant;
  ResourceClassId resource_class;
  ResourceVector resources;      // requested/reserved capacity
  ResourceVector consumed;       // consumed under this reservation
  ReservationStatus status = ReservationStatus::PROVISIONAL;
  Nanos created_at = 0;
  Nanos expires_at = 0;          // 0 = never
  QuotaGeneration quota_generation;
  PolicyGeneration policy_generation;
  CoordinatorEpoch epoch;
  AgentId owner_agent;
  AgentBootId owner_boot;
  std::string label;

  // authority the reservation was created under (used for commit/authority checks)
  ResourceGeneration resource_generation;
};

}  // namespace quota_fabric
