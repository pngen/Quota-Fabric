#pragma once
// Central typed enums for Quota Fabric. Each has a stable numeric value (for
// persistence/protocol) and a deterministic string name (for CLI/API). The
// traits struct is specialized in enum_defs.hpp; to_string/parse_enum are the
// single template entry points (no return-type-only overloads).

#include <cstdint>
#include <string>
#include <string_view>
#include <optional>

namespace quota_fabric {

enum class QuotaType : std::uint8_t {
  HARD = 0, SOFT = 1, GUARANTEED = 2, BURST = 3, BORROWABLE = 4, SHARED_POOL = 5,
};
enum class OvercommitMode : std::uint8_t { NONE = 0, LOGICAL = 1, PREDICTION_AWARE = 2 };
enum class DecisionCode : std::uint8_t {
  ALLOW = 0, ALLOW_GUARANTEED = 1, ALLOW_BURST = 2, ALLOW_BORROW = 3,
  DEFER = 4, DENY_SOFT = 5, DENY_HARD = 6, DENY_PARENT = 7, DENY_PHYSICAL = 8,
  RECALL_REQUIRED = 9, RECLAIM_REQUIRED = 10,
};
enum class ViolationCode : std::uint8_t {
  NONE = 0, SOFT_LIMIT_EXCEEDED = 1, HARD_LIMIT_EXCEEDED = 2, PARENT_LIMIT_EXCEEDED = 3,
  EXPIRED_BURST = 4, EXPIRED_BORROW = 5, STALE_RESERVATION = 6, INVALID_SHARED_ATTRIBUTION = 7,
  PHYSICAL_CAPACITY_MISMATCH = 8, STALE_POLICY = 9, STALE_QUOTA_GENERATION = 10,
  STALE_AUTHORITY = 11, DUPLICATE_RESERVATION = 12, DUPLICATE_RELEASE = 13,
  WRONG_TENANT = 14, INVALID_HIERARCHY = 15, IMPOSSIBLE_DEBT = 16,
  ATTRIBUTION_MISMATCH = 17, RESOURCE_OVERFLOW = 18,
};
enum class RecallAction : std::uint8_t {
  NO_ACTION = 0, STOP_NEW_BORROWING = 1, RECALL_BORROWED_CAPACITY = 2, REDUCE_BURST = 3,
  REQUEST_RECLAIM = 4, REQUEST_DEMOTION = 5, REQUEST_PREEMPTION = 6, DENY_NEW_RESERVATIONS = 7,
};
enum class AttributionModel : std::uint8_t {
  PHYSICAL_OWNER = 0, PROPORTIONAL = 1, EQUAL_SHARE = 2, LOGICAL_FULL_CHARGE = 3, SHARED_SYSTEM_POOL = 4,
};
enum class ReservationStatus : std::uint8_t {
  PROVISIONAL = 0, COMMITTED = 1, CONSUMED = 2, RELEASED = 3, EXPIRED = 4, ROLLED_BACK = 5,
};
enum class ComplianceState : std::uint8_t {
  COMPLIANT = 0, OVER_SOFT = 1, OVER_HARD = 2, RECALL_REQUIRED = 3, RECLAIM_REQUIRED = 4,
};
enum class EventType : std::uint8_t {
  QUOTA_UPDATE = 0, RESERVATION = 1, RELEASE = 2, BURST = 3, BORROW = 4, LEND = 5,
  RECALL = 6, VIOLATION = 7, STALE_AUTHORITY_REJECTION = 8, NODE_LOSS = 9, RECOVERY = 10,
  AGENT_REGISTRATION = 11,
};
enum class AgentState : std::uint8_t {
  REGISTERED = 0, ACTIVE = 1, LOST = 2,
};

template <class E>
struct EnumTraits;

template <class E>
inline std::string_view to_string(E v) noexcept { return EnumTraits<E>::name(v); }

template <class E>
inline std::optional<E> parse_enum(std::string_view s) noexcept { return EnumTraits<E>::parse(s); }

}  // namespace quota_fabric
