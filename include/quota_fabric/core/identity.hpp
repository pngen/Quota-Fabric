#pragma once
// System-model identity types. Each ID is strongly typed (see ids.hpp); each
// generation/epoch is a distinct tagged scalar so authorities cannot be mixed.

#include "quota_fabric/core/ids.hpp"

#include <cstdint>
#include <string>
#include <string_view>

namespace quota_fabric {

// A tagged, monotonically increasing generation/epoch value.
template <class Tag>
class Generation {
 public:
  using TagType = Tag;
  Generation() = default;
  explicit Generation(std::uint64_t v) noexcept : v_(v) {}

  static Generation next(const Generation& prev) noexcept {
    return Generation(prev.v_ + 1);
  }
  static Generation initial() noexcept { return Generation(1); }

  std::uint64_t value() const noexcept { return v_; }
  bool is_valid() const noexcept { return v_ != 0; }

  Generation& operator++() noexcept { ++v_; return *this; }
  Generation operator++(int) noexcept { Generation t = *this; ++v_; return t; }

  friend bool operator==(const Generation& a, const Generation& b) noexcept { return a.v_ == b.v_; }
  friend bool operator!=(const Generation& a, const Generation& b) noexcept { return a.v_ != b.v_; }
  friend bool operator<(const Generation& a, const Generation& b) noexcept  { return a.v_ < b.v_; }
  friend bool operator<=(const Generation& a, const Generation& b) noexcept { return a.v_ <= b.v_; }
  friend bool operator>(const Generation& a, const Generation& b) noexcept  { return a.v_ > b.v_; }
  friend bool operator>=(const Generation& a, const Generation& b) noexcept { return a.v_ >= b.v_; }

  std::string to_string() const { return std::to_string(v_); }
  friend std::ostream& operator<<(std::ostream& os, const Generation& g) { return os << g.v_; }

 private:
  std::uint64_t v_ = 0;
};

// --- identity tag declarations (empty) ---
struct TenantTag{};            struct TenantGroupTag{};
struct QuotaTag{};             struct ResourceClassTag{};
struct ReservationTag{};       struct AllocationTag{};
struct LeaseTag{};             struct NodeTag{};
struct AgentTag{};             struct AgentBootTag{};
struct ModelTag{};             struct WorkloadTag{};
struct RequestTag{};           struct BorrowTag{};
struct LendingTag{};           struct ProjectTag{};

// --- tagged generations ---
struct QuotaGenTag{};          struct PolicyGenTag{};
struct EpochTag{};             struct SnapshotGenTag{};
struct ResourceGenTag{};       struct NodeBootTag{};

using TenantId       = StrongId<TenantTag>;
using TenantGroupId  = StrongId<TenantGroupTag>;
using ProjectId      = StrongId<ProjectTag>;
using QuotaId        = StrongId<QuotaTag>;
using ResourceClassId= StrongId<ResourceClassTag>;
using ReservationId  = StrongId<ReservationTag>;
using AllocationId   = StrongId<AllocationTag>;
using LeaseId        = StrongId<LeaseTag>;
using NodeId         = StrongId<NodeTag>;
using AgentId        = StrongId<AgentTag>;
using AgentBootId    = StrongId<AgentBootTag>;
using ModelId        = StrongId<ModelTag>;
using WorkloadId     = StrongId<WorkloadTag>;
using RequestId      = StrongId<RequestTag>;
using BorrowId       = StrongId<BorrowTag>;
using LendingId      = StrongId<LendingTag>;

using QuotaGeneration          = Generation<QuotaGenTag>;
using PolicyGeneration         = Generation<PolicyGenTag>;
using CoordinatorEpoch         = Generation<EpochTag>;
using ResourceSnapshotGeneration = Generation<SnapshotGenTag>;
using ResourceGeneration        = Generation<ResourceGenTag>;
using NodeBootGeneration        = Generation<NodeBootTag>;

}  // namespace quota_fabric
