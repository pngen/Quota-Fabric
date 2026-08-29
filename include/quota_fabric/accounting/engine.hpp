#pragma once
#include "quota_fabric/core/snapshot.hpp"
#include "quota_fabric/core/status.hpp"
#include "quota_fabric/core/event.hpp"
#include "quota_fabric/core/agent.hpp"
#include "quota_fabric/core/allocation.hpp"
#include "quota_fabric/policy/policy.hpp"
#include "quota_fabric/core/resource_vector.hpp"
#include "quota_fabric/core/quota.hpp"
#include "quota_fabric/core/borrow.hpp"
#include "quota_fabric/core/reservation.hpp"
#include "quota_fabric/core/envelope.hpp"
#include "quota_fabric/core/decision.hpp"
#include "quota_fabric/core/hierarchy.hpp"
#include "quota_fabric/core/enum_defs.hpp"

#include <string>
#include <vector>
#include <unordered_map>
#include <mutex>
#include <optional>
#include <deque>
#include <memory>

namespace quota_fabric {

// Authority that must accompany any state-mutating message. Stale authority is
// rejected deterministically: a message only mutates state when its epoch, boot
// identity, quota generation, policy generation and (where relevant) reservation
// generation all still belong to the current authority.
struct Authority {
  CoordinatorEpoch epoch;
  AgentId agent;
  AgentBootId boot;
  QuotaGeneration quota_generation;
  PolicyGeneration policy_generation;
  ResourceGeneration resource_generation;
};

enum class ObservationKind : std::uint8_t {
  USAGE_DELTA = 0,
  COMPUTE_INTERVAL = 1,
  TRANSFER_CONSUMED = 2,
  MODEL_RESIDENCY_ADDED = 3,
  MODEL_RESIDENCY_REMOVED = 4,
  KV_USAGE_CHANGE = 5,
  TENSOR_USAGE_CHANGE = 6,
  ALLOCATION_START = 7,
  ALLOCATION_RESIZE = 8,
  ALLOCATION_RELEASE = 9,
  RESERVATION_COMMITTED = 10,
};

// One observation from a real backend. Observations carry identity + generation
// so duplicates and stale updates are rejected.
struct Observation {
  TenantId tenant;
  AllocationId allocation;
  ResourceDimension dimension;
  std::int64_t amount;             // positive delta for this observation kind
  ObservationKind kind = ObservationKind::USAGE_DELTA;
  Nanos at = 0;
  ResourceGeneration resource_generation;
  AgentId agent;
  AgentBootId boot;
  std::string source;
};

struct EngineSettings {
  std::uint32_t max_hierarchy_depth = 32;
  std::uint32_t max_tenants = 1'000'000;
  std::uint32_t max_groups = 100'000;
  std::uint32_t max_reservations = 1'000'000;
  std::uint32_t max_event_retention = 4096;
  std::uint32_t max_window_samples = 4096;
  std::uint32_t max_borrows = 100'000;
  std::uint32_t max_protocol_oversized_frame = 16UL * 1024 * 1024;
  bool enable_failure_injection = false;
};

// A bounded per-dimension rolling window for consumable resources.
struct RollingWindow {
  Nanos window_ns = 0;
  std::deque<std::pair<Nanos, std::int64_t>> samples;
  std::int64_t total = 0;
  void add(Nanos at, std::int64_t amount);
  std::int64_t usage(Nanos now) const;
  void expire(Nanos now);
  void reset();
};

// The authoritative quota engine. All state is owned here and mutated under one
// master lock; no blocking backend/network/file work is ever done under it.
class QuotaFabric {
 public:
  QuotaFabric();
  explicit QuotaFabric(EngineSettings settings);

  QuotaFabric(const QuotaFabric&) = delete;
  QuotaFabric& operator=(const QuotaFabric&) = delete;
  QuotaFabric(QuotaFabric&&) noexcept;
  QuotaFabric& operator=(QuotaFabric&&) noexcept;
  ~QuotaFabric();

  // ---- policy / identity ----
  QuotaStatus set_policy(QuotaPolicy policy);      // atomically replaces policy
  const QuotaPolicy& current_policy() const;
  QuotaStatus advance_epoch();                      // coordinator epoch bump
  CoordinatorEpoch epoch() const;
  QuotaGeneration quota_generation() const;

  QuotaStatus create_group(std::string name, TenantGroupKind kind,
                           std::optional<TenantGroupId> parent);
  TenantGroupId create_group_autoid(std::string name, TenantGroupKind kind,
                                    std::optional<TenantGroupId> parent);
  QuotaStatus create_tenant(std::string name, std::optional<TenantGroupId> group,
                            std::uint32_t priority, TenantId* out);
  QuotaStatus create_tenant(TenantId id, std::string name, std::optional<TenantGroupId> group,
                            std::uint32_t priority);
  QuotaStatus set_tenant_quota(TenantId, ResourceQuota);
  QuotaStatus set_group_quota(TenantGroupId, ResourceQuota);
  bool tenant_exists(TenantId) const;
  bool group_exists(TenantGroupId) const;

  // ---- evaluation ----
  QuotaDecision evaluate_reservation(TenantId, const ResourceVector& req) const;
  QuotaEnvelope envelope(TenantId) const;
  ComplianceState compliance_after_lowering(TenantId) const;

  // ---- reservation lifecycle (atomic multi-dim) ----
  QuotaResult<Reservation> reserve(TenantId, const ResourceVector& req,
                                   const Authority& auth);
  QuotaStatus commit_reservation(ReservationId, const Authority& auth);
  QuotaResult<Allocation> start_allocation(ReservationId, const ResourceVector& req,
                                           const Authority& auth);
  QuotaStatus resize_allocation(AllocationId, const ResourceVector& delta,
                                const Authority& auth);
  QuotaStatus release(ReservationId, const Authority& auth);
  QuotaStatus release_allocation(AllocationId, const Authority& auth);
  QuotaStatus apply_observation(const Observation& obs);

  // ---- borrow / lend / recall ----
  QuotaResult<BorrowRecord> borrow(TenantId borrower, const ResourceVector& amount,
                                   const Authority& auth);
  QuotaResult<LendingRecord> lend(TenantId lender, TenantId borrower,
                                  const ResourceVector& amount, const Authority& auth);
  QuotaStatus recall_borrow(BorrowId, const Authority& auth);
  std::vector<RecallAction> recall_decision(TenantId lender) const;

  // ---- explainability ----
  Explanation explain(TenantId, const ResourceVector& req) const;

  // ---- snapshots / persistence ----
  QuotaSnapshot snapshot() const;
  QuotaStatus save_to(std::string_view path, std::string* error) const;
  QuotaStatus load_from(std::string_view path, std::string* error);

  // ---- invariant checking (used by property/adversarial tests) ----
  bool invariants_ok(std::string* error) const;

  // ---- accessors used by the coordinator/protocol ----
  const std::deque<QuotaEvent>& recent_events() const;
  std::vector<Reservation> reservations_for(TenantId) const;
  std::vector<BorrowRecord> borrows_for(TenantId) const;
  std::vector<LendingRecord> lends_for(TenantId) const;
  bool reservation_exists(ReservationId) const;
  std::size_t reservation_count() const;

 private:
  struct Impl;
  std::unique_ptr<Impl> impl_;
  friend struct EngineAccess;
};

}  // namespace quota_fabric
