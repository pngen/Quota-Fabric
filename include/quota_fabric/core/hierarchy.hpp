#pragma once
#include "quota_fabric/core/quota.hpp"

#include <string>
#include <vector>
#include <optional>

namespace quota_fabric {

enum class TenantGroupKind : std::uint8_t {
  ORGANIZATION = 0,
  BUSINESS_UNIT = 1,
  TEAM = 2,
  PROJECT = 3,
  ENVIRONMENT = 4,
  SERVICE_CLASS = 5,
};

// A quota domain. Groups form a strict forest: a group has at most one parent,
// groups may contain groups, and tenants attach to a single (immediate) group.
struct TenantGroup {
  TenantGroupId id;
  TenantGroupKind kind = TenantGroupKind::TEAM;
  std::optional<TenantGroupId> parent;   // null => root
  std::string name;

  // group-level quota lattice (inherited/overridden by children)
  ResourceQuota quota;
  PolicyGeneration policy_generation;
};

// A tenant: a first-class consuming principal with tenant-local quota.
struct Tenant {
  TenantId id;
  std::optional<TenantGroupId> group;  // immediate parent domain
  std::string name;
  std::uint32_t priority = 100;         // lower = more important (used for ties)
  std::string service_class;
};

}  // namespace quota_fabric
