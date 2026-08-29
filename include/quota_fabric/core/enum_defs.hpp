#pragma once
#include "quota_fabric/core/enums.hpp"
#include "quota_fabric/core/hierarchy.hpp"

namespace quota_fabric {

// Macro: specialize EnumTraits<ENUM> with a name() and parse() only. to_string /
// parse_enum are the shared template entry points (see enums.hpp).
#define QF_ENUM_TRAITS(ENUM, ...)                                       \
  template <>                                                           \
  struct EnumTraits<ENUM> {                                             \
    static std::string_view name(ENUM v) noexcept {                     \
      using E = ENUM;                                                   \
      switch (v) {                                                      \
        __VA_ARGS__                                                     \
      }                                                                 \
      return "UNKNOWN";                                                 \
    }                                                                   \
    static std::optional<ENUM> parse(std::string_view s) noexcept {     \
      for (std::uint16_t i = 0; i < 256; ++i) {                         \
        const auto e = static_cast<ENUM>(i);                            \
        if (name(e) == s) return e;                                     \
      }                                                                 \
      return std::nullopt;                                              \
    }                                                                   \
  };

QF_ENUM_TRAITS(QuotaType,
  case E::HARD: return "HARD";
  case E::SOFT: return "SOFT";
  case E::GUARANTEED: return "GUARANTEED";
  case E::BURST: return "BURST";
  case E::BORROWABLE: return "BORROWABLE";
  case E::SHARED_POOL: return "SHARED_POOL";
)

QF_ENUM_TRAITS(OvercommitMode,
  case E::NONE: return "NONE";
  case E::LOGICAL: return "LOGICAL";
  case E::PREDICTION_AWARE: return "PREDICTION_AWARE";
)

QF_ENUM_TRAITS(DecisionCode,
  case E::ALLOW: return "ALLOW";
  case E::ALLOW_GUARANTEED: return "ALLOW_GUARANTEED";
  case E::ALLOW_BURST: return "ALLOW_BURST";
  case E::ALLOW_BORROW: return "ALLOW_BORROW";
  case E::DEFER: return "DEFER";
  case E::DENY_SOFT: return "DENY_SOFT";
  case E::DENY_HARD: return "DENY_HARD";
  case E::DENY_PARENT: return "DENY_PARENT";
  case E::DENY_PHYSICAL: return "DENY_PHYSICAL";
  case E::RECALL_REQUIRED: return "RECALL_REQUIRED";
  case E::RECLAIM_REQUIRED: return "RECLAIM_REQUIRED";
)

QF_ENUM_TRAITS(ViolationCode,
  case E::NONE: return "NONE";
  case E::SOFT_LIMIT_EXCEEDED: return "SOFT_LIMIT_EXCEEDED";
  case E::HARD_LIMIT_EXCEEDED: return "HARD_LIMIT_EXCEEDED";
  case E::PARENT_LIMIT_EXCEEDED: return "PARENT_LIMIT_EXCEEDED";
  case E::EXPIRED_BURST: return "EXPIRED_BURST";
  case E::EXPIRED_BORROW: return "EXPIRED_BORROW";
  case E::STALE_RESERVATION: return "STALE_RESERVATION";
  case E::INVALID_SHARED_ATTRIBUTION: return "INVALID_SHARED_ATTRIBUTION";
  case E::PHYSICAL_CAPACITY_MISMATCH: return "PHYSICAL_CAPACITY_MISMATCH";
  case E::STALE_POLICY: return "STALE_POLICY";
  case E::STALE_QUOTA_GENERATION: return "STALE_QUOTA_GENERATION";
  case E::STALE_AUTHORITY: return "STALE_AUTHORITY";
  case E::DUPLICATE_RESERVATION: return "DUPLICATE_RESERVATION";
  case E::DUPLICATE_RELEASE: return "DUPLICATE_RELEASE";
  case E::WRONG_TENANT: return "WRONG_TENANT";
  case E::INVALID_HIERARCHY: return "INVALID_HIERARCHY";
  case E::IMPOSSIBLE_DEBT: return "IMPOSSIBLE_DEBT";
  case E::ATTRIBUTION_MISMATCH: return "ATTRIBUTION_MISMATCH";
  case E::RESOURCE_OVERFLOW: return "RESOURCE_OVERFLOW";
)

QF_ENUM_TRAITS(RecallAction,
  case E::NO_ACTION: return "NO_ACTION";
  case E::STOP_NEW_BORROWING: return "STOP_NEW_BORROWING";
  case E::RECALL_BORROWED_CAPACITY: return "RECALL_BORROWED_CAPACITY";
  case E::REDUCE_BURST: return "REDUCE_BURST";
  case E::REQUEST_RECLAIM: return "REQUEST_RECLAIM";
  case E::REQUEST_DEMOTION: return "REQUEST_DEMOTION";
  case E::REQUEST_PREEMPTION: return "REQUEST_PREEMPTION";
  case E::DENY_NEW_RESERVATIONS: return "DENY_NEW_RESERVATIONS";
)

QF_ENUM_TRAITS(AttributionModel,
  case E::PHYSICAL_OWNER: return "PHYSICAL_OWNER";
  case E::PROPORTIONAL: return "PROPORTIONAL";
  case E::EQUAL_SHARE: return "EQUAL_SHARE";
  case E::LOGICAL_FULL_CHARGE: return "LOGICAL_FULL_CHARGE";
  case E::SHARED_SYSTEM_POOL: return "SHARED_SYSTEM_POOL";
)

QF_ENUM_TRAITS(ReservationStatus,
  case E::PROVISIONAL: return "PROVISIONAL";
  case E::COMMITTED: return "COMMITTED";
  case E::CONSUMED: return "CONSUMED";
  case E::RELEASED: return "RELEASED";
  case E::EXPIRED: return "EXPIRED";
  case E::ROLLED_BACK: return "ROLLED_BACK";
)

QF_ENUM_TRAITS(ComplianceState,
  case E::COMPLIANT: return "COMPLIANT";
  case E::OVER_SOFT: return "OVER_SOFT";
  case E::OVER_HARD: return "OVER_HARD";
  case E::RECALL_REQUIRED: return "RECALL_REQUIRED";
  case E::RECLAIM_REQUIRED: return "RECLAIM_REQUIRED";
)

QF_ENUM_TRAITS(EventType,
  case E::QUOTA_UPDATE: return "QUOTA_UPDATE";
  case E::RESERVATION: return "RESERVATION";
  case E::RELEASE: return "RELEASE";
  case E::BURST: return "BURST";
  case E::BORROW: return "BORROW";
  case E::LEND: return "LEND";
  case E::RECALL: return "RECALL";
  case E::VIOLATION: return "VIOLATION";
  case E::STALE_AUTHORITY_REJECTION: return "STALE_AUTHORITY_REJECTION";
  case E::NODE_LOSS: return "NODE_LOSS";
  case E::RECOVERY: return "RECOVERY";
  case E::AGENT_REGISTRATION: return "AGENT_REGISTRATION";
)

QF_ENUM_TRAITS(AgentState,
  case E::REGISTERED: return "REGISTERED";
  case E::ACTIVE: return "ACTIVE";
  case E::LOST: return "LOST";
)

QF_ENUM_TRAITS(TenantGroupKind,
  case E::ORGANIZATION: return "ORGANIZATION";
  case E::BUSINESS_UNIT: return "BUSINESS_UNIT";
  case E::TEAM: return "TEAM";
  case E::PROJECT: return "PROJECT";
  case E::ENVIRONMENT: return "ENVIRONMENT";
  case E::SERVICE_CLASS: return "SERVICE_CLASS";
)

#undef QF_ENUM_TRAITS

}  // namespace quota_fabric
