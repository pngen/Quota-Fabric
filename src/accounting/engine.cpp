#ifndef NOMINMAX
#define NOMINMAX
#endif
#define _CRT_SECURE_NO_WARNINGS
#include "quota_fabric/accounting/engine.hpp"
#include "quota_fabric/persistence/codec.hpp"

#include <algorithm>
#include <cstring>
#include <filesystem>
#include <cstdio>
#include <bit>
#include <unordered_set>
#include <limits>
#include <sstream>
#ifdef _WIN32
#include <windows.h>
#endif

namespace quota_fabric {

namespace {
std::uint32_t crc32(const std::uint8_t* p, std::size_t n) noexcept {
  static std::uint32_t table[256];
  static bool init = false;
  if (!init) {
    for (std::uint32_t i = 0; i < 256; ++i) {
      std::uint32_t c = i;
      for (int k = 0; k < 8; ++k) c = (c & 1) ? 0xEDB88320u ^ (c >> 1) : (c >> 1);
      table[i] = c;
    }
    init = true;
  }
  std::uint32_t crc = 0xFFFFFFFFu;
  for (std::size_t i = 0; i < n; ++i) crc = table[(crc ^ p[i]) & 0xFF] ^ (crc >> 8);
  return crc ^ 0xFFFFFFFFu;
}

std::int64_t sat_add(std::int64_t a, std::int64_t b) {
  if (b > 0 && a > INT64_MAX - b) return INT64_MAX;
  return a + b;
}
std::int64_t sat_sub(std::int64_t a, std::int64_t b) {
  if (b > 0 && a < INT64_MIN + b) return INT64_MIN;
  return a - b;
}

// ranking for decision "most restrictive wins"
int rank_of(DecisionCode c) noexcept {
  switch (c) {
    case DecisionCode::DENY_PHYSICAL: return 21;
    case DecisionCode::DENY_PARENT: return 20;
    case DecisionCode::DENY_HARD: return 19;
    case DecisionCode::DENY_SOFT: return 18;
    case DecisionCode::DEFER: return 17;
    case DecisionCode::ALLOW_BORROW: return 16;
    case DecisionCode::ALLOW_BURST: return 15;
    case DecisionCode::ALLOW: return 14;
    case DecisionCode::ALLOW_GUARANTEED: return 13;
    case DecisionCode::RECALL_REQUIRED: return 22;
    case DecisionCode::RECLAIM_REQUIRED: return 23;
    default: return 0;
  }
}
}  // namespace

// RollingWindow
void RollingWindow::add(Nanos at, std::int64_t amount) {
  if (amount < 0) return;
  samples.emplace_back(at, amount);
  total = sat_add(total, amount);
  expire(at);
}
std::int64_t RollingWindow::usage(Nanos) const { return total; }
void RollingWindow::expire(Nanos now) {
  const auto cutoff = now - window_ns;
  while (!samples.empty() && samples.front().first <= cutoff) { total -= samples.front().second; samples.pop_front(); }
}
void RollingWindow::reset() { samples.clear(); total = 0; }

// ---------------------------------------------------------------------------
// Impl
// ---------------------------------------------------------------------------
struct QuotaFabric::Impl {
  EngineSettings settings;
  mutable std::recursive_mutex lock;

  QuotaPolicy policy_;
  CoordinatorEpoch epoch_ = CoordinatorEpoch::initial();
  QuotaGeneration quota_generation_ = QuotaGeneration::initial();
  std::uint64_t sequence_ = 0;
  std::string persist_path_;
  bool loaded_ = false;

  struct Usage {
    ResourceVector consumption;
    ResourceVector reserved;
    ResourceVector borrowed_consumed;
    ResourceVector burst;
    ResourceVector lent;
    ResourceVector debt;
    std::array<RollingWindow, kResourceDimensionCount> windows;
    Nanos burst_started = 0, burst_expires = 0, cooldown_until = 0;
    bool burst_active = false;
    ResourceVector burst_peak;
  };
  // per-reservation funding metadata kept in a side map
  struct Funding { ResourceVector burst; std::vector<BorrowId> borrows; };

  std::unordered_map<TenantGroupId, TenantGroup> groups_;
  std::unordered_map<TenantId, Tenant> tenants_;
  std::unordered_map<TenantId, ResourceQuota> local_quota_;
  std::unordered_map<TenantGroupId, ResourceQuota> group_quota_;
  std::unordered_map<TenantId, Usage> usage_;
  std::unordered_map<ReservationId, Reservation> reservations_;
  std::unordered_map<ReservationId, Funding> funding_;
  std::unordered_map<AllocationId, Allocation> allocations_;
  std::unordered_map<BorrowId, BorrowRecord> borrows_;
  std::unordered_map<LendingId, LendingRecord> lends_;
  std::deque<QuotaEvent> events_;
  std::vector<std::string> violations_log_;
  std::unordered_map<AgentId, AgentDescriptor> agents_;
  std::unordered_map<AgentId, AgentBootId> agent_boot_;

  static const Usage& empty_usage() { static const Usage u; return u; }

  const Usage& usage(TenantId t) const { auto it = usage_.find(t); return it == usage_.end() ? empty_usage() : it->second; }
  Usage& usage_mut(TenantId t) { return usage_[t]; }

  static bool active_res(const Reservation& r) {
    return r.status == ReservationStatus::PROVISIONAL || r.status == ReservationStatus::COMMITTED ||
           r.status == ReservationStatus::CONSUMED;
  }
  void sync_usage(TenantId t);
  QuotaResult<BorrowRecord> borrow_impl(TenantId borrower, const ResourceVector& amount,
                                        const Authority& auth, std::optional<ReservationId> reservation);
  QuotaStatus return_borrow(BorrowId bid, const ResourceVector& amount);
  std::uint32_t borrower_priority(TenantId t) const;
  void serialize_state(BinaryWriter& w) const;
  QuotaStatus deserialize_state(BinaryReader& r, std::string* err);

  bool tenant_exists(TenantId t) const { return tenants_.find(t) != tenants_.end(); }
  bool group_exists(TenantGroupId g) const { return groups_.find(g) != groups_.end(); }
  const Tenant* tenant_ref(TenantId t) const { auto it = tenants_.find(t); return it == tenants_.end() ? nullptr : &it->second; }

  bool valid_authority(const Authority& a) const {
    return a.epoch == epoch_ && a.quota_generation == quota_generation_ && a.policy_generation == policy_.generation;
  }
  bool stale_epoch(const Authority& a) const { return a.epoch != epoch_; }
  bool stale_quota_gen(const Authority& a) const { return a.quota_generation != quota_generation_; }

  std::vector<TenantGroupId> ancestor_groups(TenantId t) const {
    std::vector<TenantGroupId> chain;
    const auto* tn = tenant_ref(t);
    if (!tn) return chain;
    std::optional<TenantGroupId> cur = tn->group;
    while (cur) {
      auto gi = groups_.find(*cur);
      if (gi == groups_.end()) break;
      chain.push_back(*cur);
      cur = gi->second.parent;
    }
    std::reverse(chain.begin(), chain.end());
    return chain;
  }
  bool has_ancestor(const Tenant& tn, TenantGroupId g) const {
    std::optional<TenantGroupId> cur = tn.group;
    while (cur) {
      if (*cur == g) return true;
      auto gi = groups_.find(*cur);
      if (gi == groups_.end()) break;
      cur = gi->second.parent;
    }
    return false;
  }

  template <class Getter>
  void merge_min(ResourceVector& out, const std::vector<const ResourceQuota*>& chain, Getter g) const {
    std::array<bool, kResourceDimensionCount> seen{};
    for (const auto* q : chain) {
      for (const auto d : all_resource_dimensions()) {
        const auto& v = g(q, d);
        const auto i = static_cast<std::size_t>(d);
        if (v.present(d)) {
          if (!seen[i] || v.get(d) < out.get(d)) { out.set(d, v.get(d)); seen[i] = true; }
        }
      }
    }
  }

  ResourceQuota effective_lattice(TenantId t) const {
    ResourceQuota eff; eff.tenant = t;
    std::vector<const ResourceQuota*> chain;
    for (auto g : ancestor_groups(t)) { auto qi = group_quota_.find(g); if (qi != group_quota_.end()) chain.push_back(&qi->second); }
    auto li = local_quota_.find(t); if (li != local_quota_.end()) chain.push_back(&li->second);
    merge_min(eff.guaranteed, chain, [](const ResourceQuota* q, ResourceDimension d) { (void)d; return q->guaranteed; });
    merge_min(eff.soft_limit, chain, [](const ResourceQuota* q, ResourceDimension d) { (void)d; return q->soft_limit; });
    merge_min(eff.hard_limit, chain, [](const ResourceQuota* q, ResourceDimension d) { (void)d; return q->hard_limit; });
    merge_min(eff.burst_limit, chain, [](const ResourceQuota* q, ResourceDimension d) { (void)d; return q->burst_limit; });
    merge_min(eff.borrowable, chain, [](const ResourceQuota* q, ResourceDimension d) { (void)d; return q->borrowable; });
    merge_min(eff.physical_capacity, chain, [](const ResourceQuota* q, ResourceDimension d) { (void)d; return q->physical_capacity; });
    if (!chain.empty()) { eff.burst_rule = chain.back()->burst_rule; eff.borrow_rule = chain.back()->borrow_rule;
      eff.overcommit = chain.back()->overcommit; eff.overcommit_factor = chain.back()->overcommit_factor; }
    eff.generation = quota_generation_; eff.policy_generation = policy_.generation;
    return eff;
  }

  ResourceVector subtree_committed(TenantGroupId g) const {
    ResourceVector sum;
    for (const auto& [tid, tn] : tenants_) {
      if (has_ancestor(tn, g)) { const auto& u = usage(tid); sum.add(u.consumption); sum.add(u.reserved); }
    }
    return sum;
  }
  ResourceVector pool_used(TenantGroupId g) const {
    ResourceVector sum;
    for (const auto& [tid, tn] : tenants_) {
      if (!has_ancestor(tn, g)) continue;
      const auto eff = effective_lattice(tid);
      const auto& u = usage(tid);
      ResourceVector committed = u.consumption; committed.add(u.reserved);
      for (const auto d : all_resource_dimensions()) {
        if (!eff.guaranteed.present(d)) continue;
        const auto amount = std::min(committed.get(d), eff.guaranteed.get(d));
        if (amount > 0) { ResourceVector tmp = ResourceVector::from_scalar(d, amount); sum.add(tmp); }
      }
    }
    return sum;
  }

  bool has_entitlement(const ResourceQuota& eff) const {
    return eff.guaranteed.present_count() || eff.soft_limit.present_count() || eff.hard_limit.present_count() ||
           eff.burst_limit.present_count() || eff.borrowable.present_count();
  }

  // Consumable dimensions are budgeted over rolling time windows rather than
  // reserved/consumed as persistent capacity.
  static bool is_consumable(ResourceDimension d) noexcept {
    return d == ResourceDimension::AcceleratorComputeTime || d == ResourceDimension::TransferBytes ||
           d == ResourceDimension::StorageBandwidth || d == ResourceDimension::TransferBandwidth;
  }
  std::int64_t current_committed_scalar(TenantId t, ResourceDimension d, const ResourceVector& committed) const {
    if (is_consumable(d)) return usage(t).windows[static_cast<std::size_t>(d)].usage(mono_now_nanos());
    return committed.get(d);
  }

  // physical allowance after overcommit
  std::int64_t physical_allowance(ResourceDimension d, const ResourceQuota& eff) const {
    if (!eff.physical_capacity.present(d)) return INT64_MAX;
    double cap = static_cast<double>(eff.physical_capacity.get(d));
    if (eff.overcommit == OvercommitMode::LOGICAL || eff.overcommit == OvercommitMode::PREDICTION_AWARE)
      cap *= std::max(1.0, eff.overcommit_factor > 0 ? eff.overcommit_factor : 1.0);
    if (cap > static_cast<double>(INT64_MAX)) return INT64_MAX;
    return static_cast<std::int64_t>(cap);
  }


  // ----- per-dimension classification -----
  QuotaDecision classify(TenantId t, const ResourceVector& req) const {
    QuotaDecision dec;
    dec.tenant = t; dec.requested = req;
    dec.generation = quota_generation_; dec.policy_generation = policy_.generation; dec.epoch = epoch_;
    const auto eff = effective_lattice(t);
    dec.guarantee = eff.guaranteed; dec.soft_limit = eff.soft_limit; dec.hard_limit = eff.hard_limit; dec.burst = eff.burst_limit;
    const auto& u = usage(t);
    dec.current_usage = u.consumption; dec.reserved_usage = u.reserved; dec.borrowed = u.borrowed_consumed; dec.debt = u.debt;

    if (req.is_empty()) { dec.code = DecisionCode::ALLOW; return dec; }
    if (!has_entitlement(eff)) {
      dec.code = DecisionCode::DENY_HARD;
      dec.limiting_dimension = req.present_count() ? first_present(req) : std::nullopt;
      dec.explanation.add(ViolationCode::HARD_LIMIT_EXCEEDED, "tenant has no configured quota entitlement");
      return dec;
    }

    // use the empty-lattice check to also populate parent availability snapshot
    std::vector<TenantGroupId> anc = ancestor_groups(t);
    // parent aggregate capacities used for parent window snapshot
    ResourceVector parent_soft = ResourceVector(), parent_hard = ResourceVector(), parent_used = ResourceVector();
    for (auto g : anc) {
      auto qi = group_quota_.find(g);
      if (qi == group_quota_.end()) continue;
      const auto s = subtree_committed(g);
      parent_used.add(s);
      parent_soft = ResourceVector::min(parent_soft.is_empty() ? qi->second.soft_limit : parent_soft, qi->second.soft_limit);
      parent_hard = ResourceVector::min(parent_hard.is_empty() ? qi->second.hard_limit : parent_hard, qi->second.hard_limit);
    }
    dec.parent_available = ResourceVector::diff_clamp(parent_soft, parent_used);

    int best_rank = rank_of(DecisionCode::ALLOW_GUARANTEED);
    DecisionCode best_code = DecisionCode::ALLOW_GUARANTEED;
    std::optional<ResourceDimension> limit_dim;

    const auto committed_now = [&]() { ResourceVector c = u.consumption; c.add(u.reserved); return c; }();

    for (const auto d : all_resource_dimensions()) {
      const std::int64_t amt = req.present(d) ? req.get(d) : 0;
      if (amt == 0 && !req.present(d)) continue;
      const std::int64_t base = current_committed_scalar(t, d, committed_now);
      if (is_consumable(d)) {
        // consumable budget check: no burst/borrow/reservation semantics
        const std::int64_t new_use = sat_add(base, amt);
        const std::int64_t g = eff.guaranteed.present(d) ? eff.guaranteed.get(d) : 0;
        const std::int64_t s = eff.soft_limit.present(d) ? eff.soft_limit.get(d) : (eff.guaranteed.present(d) ? g : INT64_MAX);
        const std::int64_t h = eff.hard_limit.present(d) ? eff.hard_limit.get(d) : INT64_MAX;
        DecisionCode dimcode = DecisionCode::ALLOW;
        if (new_use <= g) dimcode = DecisionCode::ALLOW_GUARANTEED;
        else if (new_use <= s) dimcode = DecisionCode::ALLOW;
        else if (new_use <= h) dimcode = DecisionCode::ALLOW;  // within hard: allow but flag over-soft
        else dimcode = DecisionCode::DENY_HARD;
        if (new_use > s) dec.explanation.add(ViolationCode::SOFT_LIMIT_EXCEEDED,
           "rolling " + std::string(resource_dimension_name(d)) + " budget over soft");
        const int rr = rank_of(dimcode);
        if (rr > best_rank) { best_rank = rr; best_code = dimcode; limit_dim = d; }
        continue;
      }
      const std::int64_t new_committed = sat_add(base, amt);
      const std::int64_t guarantee = eff.guaranteed.present(d) ? eff.guaranteed.get(d) : 0;
      const std::int64_t soft = eff.soft_limit.present(d) ? eff.soft_limit.get(d)
                               : (eff.guaranteed.present(d) ? guarantee : INT64_MAX);
      const std::int64_t hard = eff.hard_limit.present(d) ? eff.hard_limit.get(d) : INT64_MAX;

      // parent hard aggregate
      bool parent_hard_ok = true;
      for (auto g : anc) {
        auto qi = group_quota_.find(g);
        if (qi == group_quota_.end()) continue;
        if (qi->second.hard_limit.present(d)) {
          const auto used = subtree_committed(g).get(d);
          if (sat_add(used, amt) > qi->second.hard_limit.get(d)) { parent_hard_ok = false; break; }
        }
      }
      if (!parent_hard_ok) {
        if (rank_of(DecisionCode::DENY_PARENT) > best_rank) { best_rank = rank_of(DecisionCode::DENY_PARENT); best_code = DecisionCode::DENY_PARENT; limit_dim = d;
          dec.explanation.add(ViolationCode::PARENT_LIMIT_EXCEEDED, "parent quota hard limit would be exceeded on " + std::string(resource_dimension_name(d))); }
        continue;
      }

      // physical cheque
      const std::int64_t total_for_phys = sat_add(sat_add(new_committed, u.borrowed_consumed.get(d)), u.burst.get(d));
      const auto phys = physical_allowance(d, eff);
      if (total_for_phys > phys) {
        if (rank_of(DecisionCode::DENY_PHYSICAL) > best_rank) { best_rank = rank_of(DecisionCode::DENY_PHYSICAL); best_code = DecisionCode::DENY_PHYSICAL; limit_dim = d;
          dec.explanation.add(ViolationCode::PHYSICAL_CAPACITY_MISMATCH, "physical governed capacity would be exceeded on " + std::string(resource_dimension_name(d))); }
        continue;
      }

      // guarantee-pool protection at each ancestor
      bool pool_ok = true;
      for (auto g : anc) {
        auto qi = group_quota_.find(g);
        if (qi == group_quota_.end()) continue;
        if (!qi->second.guaranteed.present(d)) continue;
        const auto used = pool_used(g).get(d);
        if (sat_add(used, std::min(new_committed, guarantee)) > qi->second.guaranteed.get(d)) { pool_ok = false; break; }
      }

      DecisionCode dimcode;
      if (new_committed <= guarantee && pool_ok) dimcode = DecisionCode::ALLOW_GUARANTEED;
      else if (new_committed <= soft) dimcode = DecisionCode::ALLOW;
      else if (new_committed <= hard) {
        if (burst_ok(d, t, eff, new_committed, soft)) dimcode = DecisionCode::ALLOW_BURST;
        else dimcode = DecisionCode::DENY_SOFT;
      } else {
        ResourceVector nb; nb = ResourceVector::from_scalar(d, 0);
        if (borrow_available(d, t, nb)) dimcode = DecisionCode::ALLOW_BORROW;
        else dimcode = DecisionCode::DENY_HARD;
      }
      const int r = rank_of(dimcode);
      if (r > best_rank) { best_rank = r; best_code = dimcode; limit_dim = d; }
    }

    dec.code = best_code;
    dec.limiting_dimension = limit_dim;
    if (best_code == DecisionCode::DENY_SOFT)
      dec.explanation.add(ViolationCode::SOFT_LIMIT_EXCEEDED, "request would exceed soft quota without burst allowance");
    else if (best_code == DecisionCode::DENY_HARD)
      dec.explanation.add(ViolationCode::HARD_LIMIT_EXCEEDED, "request would exceed hard quota without borrowable capacity");
    else if (best_code == DecisionCode::ALLOW_BURST)
      dec.explanation.add_none("allowed via bounded burst");
    else if (best_code == DecisionCode::ALLOW_BORROW)
      dec.explanation.add_none("allowed via borrowing from available guarantee");
    return dec;
  }

  std::optional<ResourceDimension> first_present(const ResourceVector& v) const {
    for (const auto d : all_resource_dimensions()) if (v.present(d)) return d;
    return std::nullopt;
  }

  bool burst_ok(ResourceDimension d, TenantId t, const ResourceQuota& eff, std::int64_t new_committed, std::int64_t soft) const {
    (void)new_committed; (void)soft;
    const auto& u = usage(t);
    const std::int64_t burst_limit = eff.burst_limit.present(d) ? eff.burst_limit.get(d) : 0;
    const Nanos now = mono_now_nanos();
    if (u.burst_active) {
      if (now >= u.burst_expires) return false;
      return u.burst.get(d) < burst_limit;
    }
    if (now < u.cooldown_until) return false;
    if (burst_limit <= 0) return false;
    if (eff.burst_rule.window <= 0) return false;
    return true;
  }

  bool borrow_available(ResourceDimension d, TenantId, ResourceVector& candidate_borrow) const {
    for (const auto& [lid, rec] : lends_) {
      (void)lid;
      if (borrower_can_borrow_from(rec, d)) {
        const auto avail = available_to_lend(rec.lender, d);
        if (avail > 0) { candidate_borrow.set(d, avail); return true; }
      }
    }
    return false;
  }
  bool borrower_can_borrow_from(const LendingRecord& rec, ResourceDimension d) const {
    return rec.revocable && rec.outstanding.get(d) < rec.amount.get(d);
  }
  std::int64_t available_to_lend(TenantId lender, ResourceDimension d) const {
    const auto eff = effective_lattice(lender);
    const auto& u = usage(lender);
    const ResourceVector committed = [&]() { ResourceVector c = u.consumption; c.add(u.reserved); return c; }();
    const std::int64_t guarantee = eff.borrowable.present(d) ? std::min(eff.borrowable.get(d),
        eff.guaranteed.present(d) ? eff.guaranteed.get(d) : 0) : 0;
    // how much can be lent = guaranteed headroom not committed, minus what's already lent
    const std::int64_t headroom = std::max<std::int64_t>(0, guarantee - committed.get(d));
    const std::int64_t already_lent = u.lent.get(d);
    return std::max<std::int64_t>(0, headroom - already_lent);
  }

  void emit_event(EventType type, TenantId tenant, std::string detail = "");
  ComplianceState compute_compliance(TenantId t, const ResourceQuota& eff, const ResourceVector& committed) const;
  QuotaEnvelope compute_envelope(TenantId t) const;

  // ---- usage recomputation for invariant checking ----
  ResourceVector computed_reserved(TenantId t) const {
    ResourceVector sum;
    for (const auto& [rid, r] : reservations_)
      if (r.tenant == t && (r.status == ReservationStatus::PROVISIONAL || r.status == ReservationStatus::COMMITTED || r.status == ReservationStatus::CONSUMED))
        sum.add(r.resources);
    return sum;
  }
  ResourceVector computed_consumed(TenantId t) const {
    ResourceVector sum;
    for (const auto& [aid, a] : allocations_)
      if (a.tenant == t && a.active) sum.add(a.used);
    return sum;
  }
  ResourceVector computed_borrowed(TenantId t) const {
    ResourceVector sum;
    for (const auto& [bid, b] : borrows_)
      if (b.borrower == t && !b.recalled) sum.add(b.consumed);
    return sum;
  }
  ResourceVector computed_lent(TenantId t) const {
    ResourceVector sum;
    for (const auto& [lid, l] : lends_)
      if (l.lender == t && l.outstanding.present_count()) sum.add(l.outstanding);
    return sum;
  }
};

bool lattice_coherent(const ResourceQuota& q) {
  for (const auto d : all_resource_dimensions()) {
    if (q.guaranteed.present(d) && q.hard_limit.present(d) && q.guaranteed.get(d) > q.hard_limit.get(d)) return false;
    if (q.soft_limit.present(d) && q.hard_limit.present(d) && q.soft_limit.get(d) > q.hard_limit.get(d)) return false;
    if (q.guaranteed.present(d) && q.soft_limit.present(d) && q.guaranteed.get(d) > q.soft_limit.get(d)) return false;
  }
  return true;
}

// ---------------------------------------------------------------------------
// Impl member definitions
// ---------------------------------------------------------------------------
void QuotaFabric::Impl::emit_event(EventType type, TenantId tenant, std::string detail) {
  QuotaEvent e;
  e.type = type; e.at = mono_now_nanos(); e.tenant = tenant;
  e.quota_generation = quota_generation_; e.policy_generation = policy_.generation; e.epoch = epoch_;
  e.detail = std::move(detail);
  events_.push_back(e);
  while (events_.size() > settings.max_event_retention) events_.pop_front();
}

ComplianceState QuotaFabric::Impl::compute_compliance(TenantId t, const ResourceQuota& eff, const ResourceVector& committed) const {
  const auto& u = usage(t);
  bool over_soft = false, over_hard = false;
  for (const auto d : all_resource_dimensions()) {
    const std::int64_t c = is_consumable(d) ? u.windows[static_cast<std::size_t>(d)].usage(mono_now_nanos())
                                            : committed.get(d);
    const std::int64_t soft = eff.soft_limit.present(d) ? eff.soft_limit.get(d)
                            : (eff.guaranteed.present(d) ? eff.guaranteed.get(d) : INT64_MAX);
    const std::int64_t hard = eff.hard_limit.present(d) ? eff.hard_limit.get(d) : INT64_MAX;
    if (c > hard) over_hard = true;
    else if (c > soft) over_soft = true;
    if (eff.guaranteed.present(d) && u.lent.get(d) > 0 && sat_add(c, u.lent.get(d)) > eff.guaranteed.get(d)) {
      return ComplianceState::RECALL_REQUIRED;
    }
  }
  if (over_hard) return ComplianceState::OVER_HARD;
  if (over_soft) return ComplianceState::OVER_SOFT;
  return ComplianceState::COMPLIANT;
}

QuotaEnvelope QuotaFabric::Impl::compute_envelope(TenantId t) const {
  QuotaEnvelope env;
  env.tenant = t; env.generation = quota_generation_; env.policy_generation = policy_.generation; env.epoch = epoch_;
  const auto eff = effective_lattice(t);
  env.guaranteed = eff.guaranteed; env.soft_limit = eff.soft_limit; env.hard_limit = eff.hard_limit; env.burst_limit = eff.burst_limit;
  const auto& u = usage(t);
  env.current_consumption = u.consumption; env.reserved = u.reserved;
  ResourceVector committed = u.consumption; committed.add(u.reserved); env.committed_usage = committed;
  env.borrowed = u.borrowed_consumed; env.burst_usage = u.burst; env.lent = u.lent; env.debt = u.debt;
  for (const auto d : all_resource_dimensions()) {
    const std::int64_t c = is_consumable(d) ? u.windows[static_cast<std::size_t>(d)].usage(mono_now_nanos()) : committed.get(d);
    const std::int64_t g = eff.guaranteed.present(d) ? eff.guaranteed.get(d) : 0;
    const std::int64_t s = eff.soft_limit.present(d) ? eff.soft_limit.get(d) : g;
    const std::int64_t bc = eff.burst_limit.present(d) ? eff.burst_limit.get(d) : 0;
    env.available_guaranteed.set(d, g - c - u.lent.get(d));
    env.available_soft.set(d, s - c);
    env.available_burst.set(d, bc - u.burst.get(d));
  }
  env.burst_active = u.burst_active; env.burst_expires_at = u.burst_expires;
  env.compliance = compute_compliance(t, eff, committed);
  return env;
}

// ---------------------------------------------------------------------------
// QuotaFabric public methods
// ---------------------------------------------------------------------------
QuotaFabric::QuotaFabric() : impl_(std::make_unique<Impl>()) {}
QuotaFabric::QuotaFabric(EngineSettings settings) : impl_(std::make_unique<Impl>()) { impl_->settings = settings; }
QuotaFabric::QuotaFabric(QuotaFabric&&) noexcept = default;
QuotaFabric& QuotaFabric::operator=(QuotaFabric&&) noexcept = default;
QuotaFabric::~QuotaFabric() = default;

QuotaStatus QuotaFabric::set_policy(QuotaPolicy policy) {
  std::lock_guard<std::recursive_mutex> lk(impl_->lock);
  std::string err;
  if (!policy.validate(&err)) return QuotaStatus::failure(ViolationCode::INVALID_HIERARCHY, "invalid policy: " + err);
  impl_->policy_ = std::move(policy);
  impl_->emit_event(EventType::QUOTA_UPDATE, TenantId{}, "policy replaced");
  return QuotaStatus::success();
}
const QuotaPolicy& QuotaFabric::current_policy() const { return impl_->policy_; }
QuotaStatus QuotaFabric::advance_epoch() {
  std::lock_guard<std::recursive_mutex> lk(impl_->lock);
  impl_->epoch_ = CoordinatorEpoch::next(impl_->epoch_);
  impl_->emit_event(EventType::QUOTA_UPDATE, TenantId{}, "coordinator epoch advanced");
  return QuotaStatus::success();
}
CoordinatorEpoch QuotaFabric::epoch() const { return impl_->epoch_; }
QuotaGeneration QuotaFabric::quota_generation() const { return impl_->quota_generation_; }

QuotaStatus QuotaFabric::create_group(std::string name, TenantGroupKind kind, std::optional<TenantGroupId> parent) {
  std::lock_guard<std::recursive_mutex> lk(impl_->lock);
  if (parent && !impl_->group_exists(*parent)) return QuotaStatus::failure(ViolationCode::INVALID_HIERARCHY, "parent group missing");
  TenantGroup g;
  g.id = TenantGroupId::make(); g.name = std::move(name); g.kind = kind; g.parent = parent;
  g.policy_generation = impl_->policy_.generation;
  impl_->groups_.emplace(g.id, g);
  impl_->emit_event(EventType::QUOTA_UPDATE, TenantId{}, "group created");
  return QuotaStatus::success();
}
TenantGroupId QuotaFabric::create_group_autoid(std::string name, TenantGroupKind kind, std::optional<TenantGroupId> parent) {
  std::lock_guard<std::recursive_mutex> lk(impl_->lock);
  TenantGroup g;
  g.id = TenantGroupId::make(); g.name = std::move(name); g.kind = kind; g.parent = parent;
  g.policy_generation = impl_->policy_.generation;
  const auto gid = g.id;
  impl_->groups_.emplace(g.id, std::move(g));
  return gid;
}

QuotaStatus QuotaFabric::create_tenant(TenantId id, std::string name, std::optional<TenantGroupId> group, std::uint32_t priority) {
  std::lock_guard<std::recursive_mutex> lk(impl_->lock);
  if (impl_->tenants_.size() >= impl_->settings.max_tenants) return QuotaStatus::failure(ViolationCode::RESOURCE_OVERFLOW, "tenant limit reached");
  if (impl_->tenant_exists(id)) return QuotaStatus::failure(ViolationCode::DUPLICATE_RESERVATION, "tenant id already exists");
  if (group && !impl_->group_exists(*group)) return QuotaStatus::failure(ViolationCode::INVALID_HIERARCHY, "group missing");
  Tenant t; t.id = id; t.name = std::move(name); t.group = group; t.priority = priority;
  impl_->tenants_.emplace(id, std::move(t));
  impl_->emit_event(EventType::QUOTA_UPDATE, id, "tenant created");
  return QuotaStatus::success();
}
QuotaStatus QuotaFabric::create_tenant(std::string name, std::optional<TenantGroupId> group, std::uint32_t priority, TenantId* out) {
  TenantId id = TenantId::make();
  auto st = create_tenant(id, std::move(name), group, priority);
  if (st && out) *out = id;
  return st;
}

QuotaStatus QuotaFabric::set_tenant_quota(TenantId t, ResourceQuota q) {
  std::lock_guard<std::recursive_mutex> lk(impl_->lock);
  if (!impl_->tenant_exists(t)) return QuotaStatus::failure(ViolationCode::WRONG_TENANT, "tenant missing");
  if (impl_->local_quota_.find(t) != impl_->local_quota_.end() && !lattice_coherent(q))
    return QuotaStatus::failure(ViolationCode::INVALID_HIERARCHY, "incoherent lattice (guarantee>soft>hard)");
  q.tenant = t; q.generation = impl_->quota_generation_; q.policy_generation = impl_->policy_.generation;
  impl_->local_quota_[t] = std::move(q);
  impl_->quota_generation_ = QuotaGeneration::next(impl_->quota_generation_);
  impl_->emit_event(EventType::QUOTA_UPDATE, t, "tenant quota updated");
  return QuotaStatus::success();
}
QuotaStatus QuotaFabric::set_group_quota(TenantGroupId g, ResourceQuota q) {
  std::lock_guard<std::recursive_mutex> lk(impl_->lock);
  if (!impl_->group_exists(g)) return QuotaStatus::failure(ViolationCode::INVALID_HIERARCHY, "group missing");
  q.tenant = TenantId{}; q.generation = impl_->quota_generation_; q.policy_generation = impl_->policy_.generation;
  impl_->group_quota_[g] = std::move(q);
  impl_->quota_generation_ = QuotaGeneration::next(impl_->quota_generation_);
  impl_->emit_event(EventType::QUOTA_UPDATE, TenantId{}, "group quota updated");
  return QuotaStatus::success();
}
bool QuotaFabric::tenant_exists(TenantId t) const { return impl_->tenant_exists(t); }
bool QuotaFabric::group_exists(TenantGroupId g) const { return impl_->group_exists(g); }

QuotaDecision QuotaFabric::evaluate_reservation(TenantId t, const ResourceVector& req) const {
  std::lock_guard<std::recursive_mutex> lk(impl_->lock);
  return impl_->classify(t, req);
}
QuotaEnvelope QuotaFabric::envelope(TenantId t) const {
  std::lock_guard<std::recursive_mutex> lk(impl_->lock);
  return impl_->compute_envelope(t);
}
ComplianceState QuotaFabric::compliance_after_lowering(TenantId t) const {
  std::lock_guard<std::recursive_mutex> lk(impl_->lock);
  const auto eff = impl_->effective_lattice(t);
  return impl_->compute_compliance(t, eff, impl_->compute_envelope(t).committed_usage);
}

// ---------------------------------------------------------------------------
// usage sync + reservation lifecycle
// ---------------------------------------------------------------------------
void QuotaFabric::Impl::sync_usage(TenantId t) {
  auto& u = usage_mut(t);
  const auto now = mono_now_nanos();
  u.consumption = computed_consumed(t);
  u.reserved.clear();
  std::unordered_map<ReservationId, ResourceVector> alloc_used;
  for (const auto& [aid, a] : allocations_) {
    (void)aid;
    if (a.active && a.tenant == t) alloc_used[a.reservation].add(a.used);
  }
  for (const auto& [rid, r] : reservations_) {
    if (r.tenant != t || !active_res(r)) continue;
    ResourceVector rem = r.resources;
    auto it = alloc_used.find(r.id);
    if (it != alloc_used.end()) {
      ResourceVector usedc = ResourceVector::min(it->second, r.resources);
      rem.sub(usedc, true);
    }
    u.reserved.add(rem);
  }
  u.borrowed_consumed = computed_borrowed(t);
  u.lent = computed_lent(t);
  u.burst.clear();
  for (const auto& [rid, fund] : funding_) {
    auto it = reservations_.find(rid);
    if (it == reservations_.end() || it->second.tenant != t || !active_res(it->second)) continue;
    u.burst.add(fund.burst);
  }
  u.debt.clear();
  for (const auto& [bid, b] : borrows_) {
    (void)bid;
    if (b.borrower == t && !b.recalled) u.debt.add(b.debt);
  }
  // burst window expiry cleanup
  if (u.burst_active && now >= u.burst_expires) { u.burst_active = false; u.burst.clear(); }
  (void)now;
}

QuotaResult<Reservation> QuotaFabric::reserve(TenantId t, const ResourceVector& req, const Authority& auth) {
  std::lock_guard<std::recursive_mutex> lk(impl_->lock);
  if (impl_->stale_epoch(auth)) return QuotaResult<Reservation>::failure(QuotaStatus::failure(ViolationCode::STALE_AUTHORITY, "stale coordinator epoch"));
  if (impl_->stale_quota_gen(auth)) return QuotaResult<Reservation>::failure(QuotaStatus::failure(ViolationCode::STALE_QUOTA_GENERATION, "stale quota generation"));
  if (!impl_->valid_authority(auth)) return QuotaResult<Reservation>::failure(QuotaStatus::failure(ViolationCode::STALE_AUTHORITY, "stale authority"));
  if (!impl_->tenant_exists(t)) return QuotaResult<Reservation>::failure(QuotaStatus::failure(ViolationCode::WRONG_TENANT, "unknown tenant"));

  auto dec = impl_->classify(t, req);
  if (!dec.allowed()) return QuotaResult<Reservation>::failure(QuotaStatus::failure(
      dec.code == DecisionCode::DENY_HARD ? ViolationCode::HARD_LIMIT_EXCEEDED :
      dec.code == DecisionCode::DENY_SOFT ? ViolationCode::SOFT_LIMIT_EXCEEDED :
      dec.code == DecisionCode::DENY_PARENT ? ViolationCode::PARENT_LIMIT_EXCEEDED :
      ViolationCode::PHYSICAL_CAPACITY_MISMATCH, dec.explanation.human()));

  const auto eff = impl_->effective_lattice(t);
  auto& u = impl_->usage_mut(t);
  const Nanos now = mono_now_nanos();
  const auto committed_before = u.consumption;
  ResourceVector cb = committed_before; cb.add(u.reserved);

  ResourceVector burst_amount, borrow_amount;
  std::vector<BorrowId> borrow_ids;
  for (const auto d : all_resource_dimensions()) {
    if (!req.present(d) || impl_->is_consumable(d)) continue;
    const std::int64_t amt = req.get(d);
    const std::int64_t base = cb.get(d);
    const std::int64_t nc = sat_add(base, amt);
    const std::int64_t soft = eff.soft_limit.present(d) ? eff.soft_limit.get(d)
                             : (eff.guaranteed.present(d) ? eff.guaranteed.get(d) : INT64_MAX);
    const std::int64_t hard = eff.hard_limit.present(d) ? eff.hard_limit.get(d) : INT64_MAX;
    const std::int64_t before_burst = std::clamp<std::int64_t>(base - soft, 0, hard - soft);
    const std::int64_t after_burst = std::clamp<std::int64_t>(nc - soft, 0, hard - soft);
    const std::int64_t bp = std::max<std::int64_t>(0, after_burst - before_burst);
    if (bp > 0) burst_amount.set(d, bp);
    const std::int64_t before_borr = std::max<std::int64_t>(0, base - hard);
    const std::int64_t after_borr = std::max<std::int64_t>(0, nc - hard);
    const std::int64_t borr = std::max<std::int64_t>(0, after_borr - before_borr);
    if (borr > 0) {
      // perform the borrow now
      ResourceVector b = ResourceVector::from_scalar(d, borr);
      auto br = impl_->borrow_impl(t, b, auth, std::nullopt);
      if (!br.ok) {
        // rollback any prior borrows
        for (auto bid : borrow_ids) impl_->return_borrow(bid, impl_->borrows_[bid].consumed);
        return QuotaResult<Reservation>::failure(QuotaStatus::failure(br.status.code, "borrow failed: " + br.status.message));
      }
      borrow_ids.push_back(br.value.id);
      borrow_amount.set(d, borr);
    }
  }

  // burst window activation
  if (!burst_amount.is_empty()) {
    u.burst_active = true;
    u.burst_started = now;
    u.burst_expires = now + eff.burst_rule.window;
    u.cooldown_until = 0;
  }

  Reservation res;
  res.id = ReservationId::make();
  res.tenant = t; res.resources = req; res.status = ReservationStatus::COMMITTED;
  res.created_at = now; res.expires_at = 0;
  res.quota_generation = impl_->quota_generation_; res.policy_generation = impl_->policy_.generation;
  res.epoch = impl_->epoch_; res.owner_agent = auth.agent; res.owner_boot = auth.boot;
  res.resource_generation = auth.resource_generation;
  impl_->funding_[res.id] = QuotaFabric::Impl::Funding{burst_amount, borrow_ids};
  impl_->reservations_.emplace(res.id, res);
  impl_->sync_usage(t);
  impl_->emit_event(EventType::RESERVATION, t, "reservation committed (" + req.to_string() + ")");
  return QuotaResult<Reservation>::success(res);
}

QuotaStatus QuotaFabric::commit_reservation(ReservationId rid, const Authority& auth) {
  std::lock_guard<std::recursive_mutex> lk(impl_->lock);
  if (impl_->stale_epoch(auth)) return QuotaStatus::failure(ViolationCode::STALE_AUTHORITY, "stale epoch");
  if (impl_->stale_quota_gen(auth)) return QuotaStatus::failure(ViolationCode::STALE_QUOTA_GENERATION, "stale quota generation");
  auto it = impl_->reservations_.find(rid);
  if (it == impl_->reservations_.end()) return QuotaStatus::failure(ViolationCode::STALE_RESERVATION, "unknown reservation");
  if (it->second.status == ReservationStatus::COMMITTED) return QuotaStatus::success();
  if (it->second.status != ReservationStatus::PROVISIONAL) return QuotaStatus::failure(ViolationCode::STALE_RESERVATION, "reservation not provisional");
  it->second.status = ReservationStatus::COMMITTED;
  it->second.quota_generation = impl_->quota_generation_;
  impl_->sync_usage(it->second.tenant);
  impl_->emit_event(EventType::RESERVATION, it->second.tenant, "reservation committed");
  return QuotaStatus::success();
}
QuotaResult<Allocation> QuotaFabric::start_allocation(ReservationId rid, const ResourceVector& req, const Authority& auth) {
  std::lock_guard<std::recursive_mutex> lk(impl_->lock);
  if (!impl_->valid_authority(auth)) return QuotaResult<Allocation>::failure(QuotaStatus::failure(ViolationCode::STALE_AUTHORITY, "stale authority"));
  auto rit = impl_->reservations_.find(rid);
  if (rit == impl_->reservations_.end()) return QuotaResult<Allocation>::failure(QuotaStatus::failure(ViolationCode::STALE_RESERVATION, "unknown reservation"));
  auto& r = rit->second;
  if (!QuotaFabric::Impl::active_res(r)) return QuotaResult<Allocation>::failure(QuotaStatus::failure(ViolationCode::STALE_RESERVATION, "reservation not active"));
  Allocation al;
  al.id = AllocationId::make(); al.tenant = r.tenant; al.reservation = rid; al.resources = req;
  al.started_at = mono_now_nanos(); al.active = true;
  al.quota_generation = impl_->quota_generation_; al.policy_generation = impl_->policy_.generation;
  al.epoch = impl_->epoch_; al.owner_agent = auth.agent; al.owner_boot = auth.boot;
  impl_->allocations_.emplace(al.id, al);
  r.status = ReservationStatus::CONSUMED;
  impl_->sync_usage(r.tenant);
  impl_->emit_event(EventType::RESERVATION, r.tenant, "allocation started");
  return QuotaResult<Allocation>::success(al);
}
QuotaStatus QuotaFabric::resize_allocation(AllocationId aid, const ResourceVector& delta, const Authority& auth) {
  std::lock_guard<std::recursive_mutex> lk(impl_->lock);
  if (impl_->stale_quota_gen(auth)) return QuotaStatus::failure(ViolationCode::STALE_QUOTA_GENERATION, "stale quota generation");
  auto it = impl_->allocations_.find(aid);
  if (it == impl_->allocations_.end()) return QuotaStatus::failure(ViolationCode::STALE_RESERVATION, "unknown allocation");
  auto& a = it->second;
  if (!a.active) return QuotaStatus::failure(ViolationCode::STALE_RESERVATION, "allocation not active");
  ResourceVector newused = a.used; newused.add(delta, true);
  // ensure never exceeds committed resources
  a.used = ResourceVector::min(newused, a.resources);
  // recompute reservation consumed consistency and check quota
  auto dec = impl_->classify(a.tenant, a.resources);
  if (dec.code == DecisionCode::DENY_HARD || dec.code == DecisionCode::DENY_PARENT || dec.code == DecisionCode::DENY_PHYSICAL) {
    impl_->emit_event(EventType::VIOLATION, a.tenant, "allocation resize would violate quota");
    return QuotaStatus::failure(dec.code == DecisionCode::DENY_PARENT ? ViolationCode::PARENT_LIMIT_EXCEEDED : ViolationCode::HARD_LIMIT_EXCEEDED, dec.explanation.human());
  }
  impl_->sync_usage(a.tenant);
  impl_->emit_event(EventType::RESERVATION, a.tenant, "allocation resized");
  return QuotaStatus::success();
}
QuotaStatus QuotaFabric::release(ReservationId rid, const Authority& auth) {
  std::lock_guard<std::recursive_mutex> lk(impl_->lock);
  if (impl_->stale_epoch(auth)) return QuotaStatus::failure(ViolationCode::STALE_AUTHORITY, "stale epoch");
  auto it = impl_->reservations_.find(rid);
  if (it == impl_->reservations_.end()) return QuotaStatus::failure(ViolationCode::STALE_RESERVATION, "unknown reservation");
  auto& r = it->second;
  if (!QuotaFabric::Impl::active_res(r)) return QuotaStatus::failure(ViolationCode::DUPLICATE_RELEASE, "reservation already released");
  r.status = ReservationStatus::RELEASED;
  // return borrows used by this reservation
  auto fit = impl_->funding_.find(rid);
  if (fit != impl_->funding_.end()) {
    for (auto bid : fit->second.borrows) impl_->return_borrow(bid, impl_->borrows_[bid].consumed);
  }
  // release active allocations under this reservation
  for (auto& [aid, a] : impl_->allocations_) {
    if (a.reservation == rid) a.active = false;
  }
  impl_->sync_usage(r.tenant);
  impl_->emit_event(EventType::RELEASE, r.tenant, "reservation released");
  return QuotaStatus::success();
}
QuotaStatus QuotaFabric::release_allocation(AllocationId aid, const Authority& auth) {
  std::lock_guard<std::recursive_mutex> lk(impl_->lock);
  if (impl_->stale_quota_gen(auth)) return QuotaStatus::failure(ViolationCode::STALE_QUOTA_GENERATION, "stale quota generation");
  auto it = impl_->allocations_.find(aid);
  if (it == impl_->allocations_.end()) return QuotaStatus::failure(ViolationCode::STALE_RESERVATION, "unknown allocation");
  auto& a = it->second;
  if (!a.active) return QuotaStatus::failure(ViolationCode::DUPLICATE_RELEASE, "allocation already released");
  a.active = false; a.ended_at = mono_now_nanos();
  impl_->sync_usage(a.tenant);
  impl_->emit_event(EventType::RELEASE, a.tenant, "allocation released");
  return QuotaStatus::success();
}

// ---------------------------------------------------------------------------
// borrowing / lending / recall
// ---------------------------------------------------------------------------
QuotaResult<BorrowRecord> QuotaFabric::Impl::borrow_impl(TenantId borrower, const ResourceVector& amount,
                                                         const Authority& auth, std::optional<ReservationId> reservation) {
  (void)auth;
  const Nanos now = mono_now_nanos();
  // pick first lender offering enough borrowable headroom on every requested dimension
  for (const auto& [lender, tn] : tenants_) {
    (void)tn;
    if (lender == borrower) continue;
    const auto leff = effective_lattice(lender);
    bool ok = true;
    for (const auto d : all_resource_dimensions()) {
      if (!amount.present(d)) continue;
      if (!leff.borrowable.present(d)) { ok = false; break; }
      if (available_to_lend(lender, d) < amount.get(d)) { ok = false; break; }
    }
    if (!ok) continue;

    LendingRecord l;
    l.id = LendingId::make(); l.lender = lender; l.borrower = borrower;
    l.amount = amount; l.outstanding = amount; l.started_at = now; l.expires_at = 0;
    l.revocable = true;
    l.policy_generation = policy_.generation; l.quota_generation = quota_generation_; l.epoch = epoch_;
    lends_.emplace(l.id, l);

    BorrowRecord b;
    b.id = BorrowId::make(); b.borrower = borrower; b.lender = lender; b.lending = l.id;
    b.amount = amount; b.consumed = amount; b.started_at = now; b.expires_at = 0;
    b.recall_priority = borrower_priority(borrower);
    b.policy_generation = policy_.generation; b.quota_generation = quota_generation_; b.epoch = epoch_;
    b.recalled = false; b.reservation = reservation;
    // debt: amount borrowed must eventually be repaid; track as entitlement debt
    b.debt = amount;
    borrows_.emplace(b.id, b);

    usage_mut(lender).lent.add(amount);
    usage_mut(borrower).borrowed_consumed.add(amount);
    usage_mut(borrower).debt.add(amount);
    emit_event(EventType::BORROW, borrower, "borrowed " + amount.to_string() + " from lender");
    emit_event(EventType::LEND, lender, "lent " + amount.to_string() + " to borrower");
    return QuotaResult<BorrowRecord>::success(b);
  }
  return QuotaResult<BorrowRecord>::failure(QuotaStatus::failure(ViolationCode::HARD_LIMIT_EXCEEDED, "no lender with borrowable capacity"));
}

std::uint32_t QuotaFabric::Impl::borrower_priority(TenantId t) const {
  auto it = tenants_.find(t); return it == tenants_.end() ? 100 : it->second.priority;
}

QuotaStatus QuotaFabric::Impl::return_borrow(BorrowId bid, const ResourceVector& amount) {
  auto it = borrows_.find(bid);
  if (it == borrows_.end()) return QuotaStatus::failure(ViolationCode::STALE_RESERVATION, "unknown borrow");
  auto& b = it->second;
  if (b.recalled) return QuotaStatus::failure(ViolationCode::DUPLICATE_RELEASE, "borrow already returned");
  ResourceVector ret = ResourceVector::min(amount, b.consumed);
  b.consumed.sub(ret, true);
  usage_mut(b.borrower).borrowed_consumed.sub(ret, true);
  usage_mut(b.borrower).debt.sub(ret, true);
  // reduce the lending offer outstanding
  auto lit = lends_.find(b.lending);
  if (lit != lends_.end()) {
    lit->second.outstanding.sub(ret, true);
    if (lit->second.outstanding.is_empty() && lit->second.amount.present_count()) {
      usage_mut(lit->second.lender).lent.sub(lit->second.amount, true);
    }
  }
  b.recalled = true;
  emit_event(EventType::RECALL, b.borrower, "borrow returned");
  return QuotaStatus::success();
}

QuotaResult<BorrowRecord> QuotaFabric::borrow(TenantId borrower, const ResourceVector& amount, const Authority& auth) {
  std::lock_guard<std::recursive_mutex> lk(impl_->lock);
  if (impl_->stale_epoch(auth) || !impl_->valid_authority(auth)) return QuotaResult<BorrowRecord>::failure(QuotaStatus::failure(ViolationCode::STALE_AUTHORITY, "stale authority"));
  if (!impl_->tenant_exists(borrower)) return QuotaResult<BorrowRecord>::failure(QuotaStatus::failure(ViolationCode::WRONG_TENANT, "unknown borrower"));
  auto it = impl_->borrow_impl(borrower, amount, auth, std::nullopt);
  if (it.ok) impl_->sync_usage(borrower);
  return it;
}
QuotaResult<LendingRecord> QuotaFabric::lend(TenantId lender, TenantId borrower, const ResourceVector& amount, const Authority& auth) {
  std::lock_guard<std::recursive_mutex> lk(impl_->lock);
  if (!impl_->valid_authority(auth) || impl_->stale_epoch(auth)) return QuotaResult<LendingRecord>::failure(QuotaStatus::failure(ViolationCode::STALE_AUTHORITY, "stale authority"));
  if (!impl_->tenant_exists(lender)) return QuotaResult<LendingRecord>::failure(QuotaStatus::failure(ViolationCode::WRONG_TENANT, "unknown lender"));
  const auto leff = impl_->effective_lattice(lender);
  for (const auto d : all_resource_dimensions()) {
    if (amount.present(d) && (!leff.borrowable.present(d) || impl_->available_to_lend(lender, d) < amount.get(d)))
      return QuotaResult<LendingRecord>::failure(QuotaStatus::failure(ViolationCode::HARD_LIMIT_EXCEEDED, "lender lacks borrowable headroom"));
  }
  // direct lend to borrower: create offer and have borrower draw it immediately
  Authority a = auth;
  auto br = impl_->borrow_impl(borrower, amount, a, std::nullopt);
  if (!br.ok) return QuotaResult<LendingRecord>::failure(QuotaStatus::failure(br.status.code, br.status.message));
  impl_->sync_usage(lender); impl_->sync_usage(borrower);
  auto lit = impl_->lends_.find(br.value.lending);
  if (lit == impl_->lends_.end()) return QuotaResult<LendingRecord>::failure(QuotaStatus::failure(ViolationCode::INVALID_HIERARCHY, "lend record missing"));
  return QuotaResult<LendingRecord>::success(lit->second);
}
QuotaStatus QuotaFabric::recall_borrow(BorrowId bid, const Authority& auth) {
  std::lock_guard<std::recursive_mutex> lk(impl_->lock);
  if (impl_->stale_epoch(auth)) return QuotaStatus::failure(ViolationCode::STALE_AUTHORITY, "stale epoch");
  auto it = impl_->borrows_.find(bid);
  if (it == impl_->borrows_.end()) return QuotaStatus::failure(ViolationCode::STALE_RESERVATION, "unknown borrow");
  auto st = impl_->return_borrow(bid, it->second.consumed);
  if (st) { impl_->sync_usage(it->second.borrower); impl_->sync_usage(it->second.lender); impl_->emit_event(EventType::RECALL, it->second.borrower, "borrow recalled"); }
  return st;
}
std::vector<RecallAction> QuotaFabric::recall_decision(TenantId lender) const {
  std::lock_guard<std::recursive_mutex> lk(impl_->lock);
  std::vector<RecallAction> actions;
  if (!impl_->tenant_exists(lender)) return actions;
  const auto eff = impl_->effective_lattice(lender);
  const auto& u = impl_->usage(lender);
  const auto com = impl_->compute_compliance(lender, eff, [&] { ResourceVector c = u.consumption; c.add(u.reserved); return c; }());
  if (com == ComplianceState::RECALL_REQUIRED) {
    actions.push_back(RecallAction::RECALL_BORROWED_CAPACITY);
    actions.push_back(RecallAction::STOP_NEW_BORROWING);
  } else {
    actions.push_back(RecallAction::NO_ACTION);
  }
  // if lender's own usage over soft, ask borrowers to reduce burst
  for (const auto& [bid, b] : impl_->borrows_) {
    if (b.lender == lender && !b.recalled) {
      actions.push_back(RecallAction::REQUEST_RECLAIM);
      break;
    }
  }
  return actions;
}

// ---------------------------------------------------------------------------
// observations
// ---------------------------------------------------------------------------
QuotaStatus QuotaFabric::apply_observation(const Observation& obs) {
  std::lock_guard<std::recursive_mutex> lk(impl_->lock);
  if (!impl_->tenant_exists(obs.tenant)) return QuotaStatus::failure(ViolationCode::WRONG_TENANT, "unknown tenant for observation");
  auto& u = impl_->usage_mut(obs.tenant);
  const Nanos now = obs.at > 0 ? obs.at : mono_now_nanos();
  const auto idx = static_cast<std::size_t>(obs.dimension);
  switch (obs.kind) {
    case ObservationKind::COMPUTE_INTERVAL: {
      if (obs.dimension != ResourceDimension::AcceleratorComputeTime) return QuotaStatus::failure(ViolationCode::INVALID_SHARED_ATTRIBUTION, "compute interval on wrong dimension");
      auto& w = u.windows[idx];
      w.window_ns = impl_->settings.max_window_samples <= 0 ? w.window_ns : (w.window_ns > 0 ? w.window_ns : std::int64_t(3'600'000'000'000ULL));
      w.add(now, obs.amount);
      break;
    }
    case ObservationKind::TRANSFER_CONSUMED: {
      if (obs.dimension != ResourceDimension::TransferBytes) return QuotaStatus::failure(ViolationCode::INVALID_SHARED_ATTRIBUTION, "transfer consumed on wrong dimension");
      u.windows[static_cast<std::size_t>(ResourceDimension::TransferBytes)].add(now, obs.amount);
      break;
    }
    case ObservationKind::MODEL_RESIDENCY_ADDED: {
      auto ait = impl_->allocations_.find(obs.allocation);
      if (ait != impl_->allocations_.end()) ait->second.used.set(ResourceDimension::ModelResidencyBytes, u.consumption.get(ResourceDimension::ModelResidencyBytes) + obs.amount);
      u.consumption.set(ResourceDimension::ModelResidencyBytes, u.consumption.get(ResourceDimension::ModelResidencyBytes) + obs.amount);
      break;
    }
    case ObservationKind::MODEL_RESIDENCY_REMOVED: {
      const auto amt = obs.amount;
      u.consumption.set(ResourceDimension::ModelResidencyBytes, std::max<std::int64_t>(0, u.consumption.get(ResourceDimension::ModelResidencyBytes) - amt));
      break;
    }
    case ObservationKind::KV_USAGE_CHANGE:
    case ObservationKind::TENSOR_USAGE_CHANGE: {
      u.consumption.set(obs.dimension, u.consumption.get(obs.dimension) + obs.amount);
      break;
    }
    case ObservationKind::USAGE_DELTA: {
      u.consumption.set(obs.dimension, u.consumption.get(obs.dimension) + obs.amount);
      break;
    }
    case ObservationKind::ALLOCATION_START:
    case ObservationKind::ALLOCATION_RESIZE:
    case ObservationKind::ALLOCATION_RELEASE:
    case ObservationKind::RESERVATION_COMMITTED:
      break;
  }
  impl_->emit_event(EventType::VIOLATION, obs.tenant, std::string("observation ") + std::string(resource_dimension_name(obs.dimension)));
  return QuotaStatus::success();
}

// ---------------------------------------------------------------------------
// explain + snapshot
// ---------------------------------------------------------------------------
Explanation QuotaFabric::explain(TenantId t, const ResourceVector& req) const {
  std::lock_guard<std::recursive_mutex> lk(impl_->lock);
  auto dec = impl_->classify(t, req);
  return dec.explanation;
}

QuotaSnapshot QuotaFabric::snapshot() const {
  std::lock_guard<std::recursive_mutex> lk(impl_->lock);
  QuotaSnapshot s;
  s.epoch = impl_->epoch_; s.quota_generation = impl_->quota_generation_; s.policy_generation = impl_->policy_.generation;
  s.sequence = impl_->sequence_;
  for (const auto& [gid, g] : impl_->groups_) s.groups.push_back(g);
  for (const auto& [tid, t] : impl_->tenants_) s.tenants.push_back(t);
  for (const auto& [tid, q] : impl_->local_quota_) s.quotas.push_back(q);
  for (const auto& [gid, q] : impl_->group_quota_) s.quotas.push_back(q);
  for (const auto& [tid, unused] : impl_->tenants_) { (void)unused; s.envelopes.push_back(impl_->compute_envelope(tid)); }
  for (const auto& [rid, r] : impl_->reservations_) s.reservations.push_back(r);
  for (const auto& [aid, a] : impl_->allocations_) s.allocations.push_back(a);
  for (const auto& [bid, b] : impl_->borrows_) s.borrow_records.push_back(b);
  for (const auto& [lid, l] : impl_->lends_) s.lending_records.push_back(l);
  for (const auto& e : impl_->events_) s.recent_events.push_back(e);
  for (const auto& [tid, u] : impl_->usage_) if (!u.debt.is_empty()) s.debts.emplace_back(tid, u.debt);
  for (const auto& v : impl_->violations_log_) s.violations.push_back(v);
  ++impl_->sequence_;
  return s;
}

// ---------------------------------------------------------------------------
// accessors
// ---------------------------------------------------------------------------
const std::deque<QuotaEvent>& QuotaFabric::recent_events() const { return impl_->events_; }
std::vector<Reservation> QuotaFabric::reservations_for(TenantId t) const {
  std::vector<Reservation> out;
  for (const auto& [rid, r] : impl_->reservations_) if (r.tenant == t) out.push_back(r);
  return out;
}
std::vector<BorrowRecord> QuotaFabric::borrows_for(TenantId t) const {
  std::vector<BorrowRecord> out;
  for (const auto& [bid, b] : impl_->borrows_) if (b.borrower == t || b.lender == t) out.push_back(b);
  return out;
}
std::vector<LendingRecord> QuotaFabric::lends_for(TenantId t) const {
  std::vector<LendingRecord> out;
  for (const auto& [lid, l] : impl_->lends_) if (l.lender == t) out.push_back(l);
  return out;
}
bool QuotaFabric::reservation_exists(ReservationId r) const { return impl_->reservations_.find(r) != impl_->reservations_.end(); }
std::size_t QuotaFabric::reservation_count() const { return impl_->reservations_.size(); }

// ---------------------------------------------------------------------------
// serialization + persistence
// ---------------------------------------------------------------------------
template <class Tag>
static void write_id_t(BinaryWriter& w, const StrongId<Tag>& id) { w.id(id); }

static void write_quota(BinaryWriter& w, const ResourceQuota& q) {
  write_id_t(w, q.tenant); write_id_t(w, q.resource_class);
  w.vector(q.guaranteed); w.vector(q.soft_limit); w.vector(q.hard_limit); w.vector(q.burst_limit);
  w.vector(q.borrowable); w.vector(q.physical_capacity); w.vector(q.shared_pool);
  // burst rule
  w.u64(std::bit_cast<std::uint64_t>(q.burst_rule.percent_above_soft)); w.vector(q.burst_rule.absolute_burst);
  w.u64(static_cast<std::uint64_t>(q.burst_rule.window)); w.u32(q.burst_rule.max_simultaneous);
  w.u64(static_cast<std::uint64_t>(q.burst_rule.cooldown)); w.u8(q.burst_rule.allow_debt ? 1 : 0);
  // borrow rule
  w.u8(q.borrow_rule.borrow_enabled ? 1 : 0); w.u8(q.borrow_rule.lend_enabled ? 1 : 0);
  w.u8(q.borrow_rule.recall_before_new_lend ? 1 : 0); w.u64(static_cast<std::uint64_t>(q.borrow_rule.max_borrow_duration));
  w.u8(q.borrow_rule.preserves_guarantee ? 1 : 0); w.u32(q.borrow_rule.max_borrows);
  // overcommit
  w.u8(static_cast<std::uint8_t>(q.overcommit)); w.u64(std::bit_cast<std::uint64_t>(q.overcommit_factor));
  w.u64(q.generation.value()); w.u64(q.policy_generation.value());
}
static void read_quota(BinaryReader& r, ResourceQuota& q) {
  q.tenant = BinaryReader::read_id<quota_fabric::StrongId<quota_fabric::TenantTag>>(r);
  q.resource_class = BinaryReader::read_id<StrongId<ResourceClassTag>>(r);
  q.guaranteed = r.vector(); q.soft_limit = r.vector(); q.hard_limit = r.vector(); q.burst_limit = r.vector();
  q.borrowable = r.vector(); q.physical_capacity = r.vector(); q.shared_pool = r.vector();
  q.burst_rule.percent_above_soft = std::bit_cast<double>(r.u64()); q.burst_rule.absolute_burst = r.vector();
  q.burst_rule.window = static_cast<Nanos>(r.u64()); q.burst_rule.max_simultaneous = r.u32();
  q.burst_rule.cooldown = static_cast<Nanos>(r.u64()); q.burst_rule.allow_debt = r.u8() != 0;
  q.borrow_rule.borrow_enabled = r.u8() != 0; q.borrow_rule.lend_enabled = r.u8() != 0;
  q.borrow_rule.recall_before_new_lend = r.u8() != 0; q.borrow_rule.max_borrow_duration = static_cast<Nanos>(r.u64());
  q.borrow_rule.preserves_guarantee = r.u8() != 0; q.borrow_rule.max_borrows = r.u32();
  q.overcommit = static_cast<OvercommitMode>(r.u8()); q.overcommit_factor = std::bit_cast<double>(r.u64());
  q.generation = QuotaGeneration(r.u64()); q.policy_generation = PolicyGeneration(r.u64());
}

void QuotaFabric::Impl::serialize_state(BinaryWriter& w) const {
  w.u32(1);  // format version
  w.u64(epoch_.value()); w.u64(quota_generation_.value()); w.u64(policy_.generation.value()); w.u64(sequence_);
  w.string(policy_.name); w.string(policy_.description); w.u64(policy_.created_at);
  w.u8(static_cast<std::uint8_t>(policy_.overcommit.mode)); w.u16(static_cast<std::uint16_t>(policy_.max_hierarchy_depth));
  w.u16(static_cast<std::uint16_t>(settings.max_event_retention));

  // groups sorted by id
  std::vector<std::pair<TenantGroupId, TenantGroup>> groups;
  for (const auto& [id, g] : groups_) groups.emplace_back(id, g);
  std::sort(groups.begin(), groups.end(), [](auto& a, auto& b) { return a.first < b.first; });
  w.u32(static_cast<std::uint32_t>(groups.size()));
  for (const auto& [id, g] : groups) {
    write_id_t(w, g.id); w.u8(static_cast<std::uint8_t>(g.kind));
    w.u8(g.parent ? 1 : 0); if (g.parent) write_id_t(w, *g.parent);
    w.string(g.name); write_quota(w, g.quota); w.u64(g.policy_generation.value());
  }
  std::vector<std::pair<TenantId, Tenant>> ts;
  for (const auto& [id, t] : tenants_) ts.emplace_back(id, t);
  std::sort(ts.begin(), ts.end(), [](auto& a, auto& b) { return a.first < b.first; });
  w.u32(static_cast<std::uint32_t>(ts.size()));
  for (const auto& [id, t] : ts) {
    write_id_t(w, t.id); w.u8(t.group ? 1 : 0); if (t.group) write_id_t(w, *t.group);
    w.string(t.name); w.u32(t.priority); w.string(t.service_class);
  }
  std::vector<std::pair<TenantId, ResourceQuota>> lq;
  for (const auto& [id, q] : local_quota_) lq.emplace_back(id, q);
  std::sort(lq.begin(), lq.end(), [](auto& a, auto& b) { return a.first < b.first; });
  w.u32(static_cast<std::uint32_t>(lq.size()));
  for (const auto& [id, q] : lq) write_quota(w, q);
  std::vector<std::pair<TenantGroupId, ResourceQuota>> gq;
  for (const auto& [id, q] : group_quota_) gq.emplace_back(id, q);
  std::sort(gq.begin(), gq.end(), [](auto& a, auto& b) { return a.first < b.first; });
  w.u32(static_cast<std::uint32_t>(gq.size()));
  for (const auto& [id, q] : gq) { write_id_t(w, id); write_quota(w, q); }

  std::vector<std::pair<ReservationId, Reservation>> rsvs;
  for (const auto& [id, r] : reservations_) rsvs.emplace_back(id, r);
  std::sort(rsvs.begin(), rsvs.end(), [](auto& a, auto& b) { return a.first < b.first; });
  w.u32(static_cast<std::uint32_t>(rsvs.size()));
  for (const auto& [id, r] : rsvs) {
    write_id_t(w, r.id); write_id_t(w, r.tenant); write_id_t(w, r.resource_class);
    w.vector(r.resources); w.vector(r.consumed); w.u8(static_cast<std::uint8_t>(r.status));
    w.u64(static_cast<std::uint64_t>(r.created_at)); w.u64(static_cast<std::uint64_t>(r.expires_at));
    w.u64(r.quota_generation.value()); w.u64(r.policy_generation.value()); w.u64(r.epoch.value());
    write_id_t(w, r.owner_agent); write_id_t(w, r.owner_boot); w.string(r.label); w.u64(r.resource_generation.value());
    // funding
    auto fit = funding_.find(r.id);
    if (fit != funding_.end()) { w.u8(1); w.vector(fit->second.burst); w.u32(static_cast<std::uint32_t>(fit->second.borrows.size()));
      for (auto bid : fit->second.borrows) write_id_t(w, bid); }
    else w.u8(0);
  }
  std::vector<std::pair<AllocationId, Allocation>> als;
  for (const auto& [id, a] : allocations_) als.emplace_back(id, a);
  std::sort(als.begin(), als.end(), [](auto& a, auto& b) { return a.first < b.first; });
  w.u32(static_cast<std::uint32_t>(als.size()));
  for (const auto& [id, a] : als) {
    write_id_t(w, a.id); write_id_t(w, a.tenant); write_id_t(w, a.reservation); write_id_t(w, a.resource_class);
    w.vector(a.resources); w.vector(a.used); w.u64(static_cast<std::uint64_t>(a.started_at)); w.u64(static_cast<std::uint64_t>(a.ended_at));
    w.u8(a.active ? 1 : 0); w.u64(a.quota_generation.value()); w.u64(a.policy_generation.value()); w.u64(a.epoch.value());
    write_id_t(w, a.owner_agent); write_id_t(w, a.owner_boot); w.string(a.label);
  }
  std::vector<std::pair<BorrowId, BorrowRecord>> brs;
  for (const auto& [id, b] : borrows_) brs.emplace_back(id, b);
  std::sort(brs.begin(), brs.end(), [](auto& a, auto& b) { return a.first < b.first; });
  w.u32(static_cast<std::uint32_t>(brs.size()));
  for (const auto& [id, b] : brs) {
    write_id_t(w, b.id); write_id_t(w, b.borrower); write_id_t(w, b.lender); write_id_t(w, b.lending);
    w.u8(b.reservation ? 1 : 0); if (b.reservation) write_id_t(w, *b.reservation);
    w.vector(b.amount); w.vector(b.consumed); w.u64(static_cast<std::uint64_t>(b.started_at)); w.u64(static_cast<std::uint64_t>(b.expires_at));
    w.u32(b.recall_priority); w.vector(b.debt); w.u64(b.policy_generation.value()); w.u64(b.quota_generation.value()); w.u64(b.epoch.value());
    w.u8(b.recalled ? 1 : 0);
  }
  std::vector<std::pair<LendingId, LendingRecord>> lds;
  for (const auto& [id, l] : lends_) lds.emplace_back(id, l);
  std::sort(lds.begin(), lds.end(), [](auto& a, auto& b) { return a.first < b.first; });
  w.u32(static_cast<std::uint32_t>(lds.size()));
  for (const auto& [id, l] : lds) {
    write_id_t(w, l.id); write_id_t(w, l.lender); write_id_t(w, l.borrower); write_id_t(w, l.resource_class);
    w.vector(l.amount); w.vector(l.outstanding); w.u64(static_cast<std::uint64_t>(l.started_at)); w.u64(static_cast<std::uint64_t>(l.expires_at));
    w.u32(l.recall_priority); w.u8(l.revocable ? 1 : 0); w.u64(l.policy_generation.value()); w.u64(l.quota_generation.value()); w.u64(l.epoch.value());
  }
}

QuotaStatus QuotaFabric::Impl::deserialize_state(BinaryReader& r, std::string* err) {
  auto fail = [&](std::string m) { if (err) *err = m; return QuotaStatus::failure(ViolationCode::INVALID_HIERARCHY, m); };
  try {
    const auto ver = r.u32();
    if (ver != 1) return fail("unsupported persistence format version");
    epoch_ = CoordinatorEpoch(r.u64()); quota_generation_ = QuotaGeneration(r.u64());
    policy_.generation = PolicyGeneration(r.u64()); sequence_ = r.u64();
    policy_.name = r.string(); policy_.description = r.string(); policy_.created_at = r.u64();
    policy_.overcommit.mode = static_cast<OvercommitMode>(r.u8());
    policy_.max_hierarchy_depth = r.u16(); settings.max_event_retention = r.u16();

    const auto gcount = r.u32();
    if (gcount > settings.max_groups) return fail("group count exceeds bound");
    for (std::uint32_t i = 0; i < gcount; ++i) {
      TenantGroup g; g.id = BinaryReader::read_id<StrongId<TenantGroupTag>>(r);
      g.kind = static_cast<TenantGroupKind>(r.u8());
      if (r.u8()) g.parent = BinaryReader::read_id<StrongId<TenantGroupTag>>(r);
      g.name = r.string(); read_quota(r, g.quota); g.policy_generation = PolicyGeneration(r.u64());
      groups_[g.id] = std::move(g);
    }
    const auto tcount = r.u32();
    if (tcount > settings.max_tenants) return fail("tenant count exceeds bound");
    for (std::uint32_t i = 0; i < tcount; ++i) {
      Tenant t; t.id = BinaryReader::read_id<StrongId<TenantTag>>(r);
      if (r.u8()) t.group = BinaryReader::read_id<StrongId<TenantGroupTag>>(r);
      t.name = r.string(); t.priority = r.u32(); t.service_class = r.string();
      tenants_[t.id] = std::move(t);
    }
    const auto lqcount = r.u32();
    if (lqcount > settings.max_tenants) return fail("tenant quota count exceeds bound");
    for (std::uint32_t i = 0; i < lqcount; ++i) { ResourceQuota q; read_quota(r, q); local_quota_[q.tenant] = std::move(q); }
    const auto gqcount = r.u32();
    if (gqcount > settings.max_groups) return fail("group quota count exceeds bound");
    for (std::uint32_t i = 0; i < gqcount; ++i) { auto gid = BinaryReader::read_id<StrongId<TenantGroupTag>>(r); ResourceQuota q; read_quota(r, q); group_quota_[gid] = std::move(q); }

    const auto rcount = r.u32();
    if (rcount > settings.max_reservations) return fail("reservation count exceeds bound");
    for (std::uint32_t i = 0; i < rcount; ++i) {
      Reservation res; res.id = BinaryReader::read_id<StrongId<ReservationTag>>(r);
      res.tenant = BinaryReader::read_id<StrongId<TenantTag>>(r);
      res.resource_class = BinaryReader::read_id<StrongId<ResourceClassTag>>(r);
      res.resources = r.vector(); res.consumed = r.vector();
      res.status = static_cast<ReservationStatus>(r.u8());
      res.created_at = static_cast<Nanos>(r.u64()); res.expires_at = static_cast<Nanos>(r.u64());
      res.quota_generation = QuotaGeneration(r.u64()); res.policy_generation = PolicyGeneration(r.u64()); res.epoch = CoordinatorEpoch(r.u64());
      res.owner_agent = BinaryReader::read_id<StrongId<AgentTag>>(r); res.owner_boot = BinaryReader::read_id<StrongId<AgentBootTag>>(r);
      res.label = r.string(); res.resource_generation = ResourceGeneration(r.u64());
      if (r.u8()) { Funding f; f.burst = r.vector(); const auto nb = r.u32(); if (nb > settings.max_borrows) return fail("borrow refs exceed bound");
        for (std::uint32_t j = 0; j < nb; ++j) f.borrows.push_back(BinaryReader::read_id<StrongId<BorrowTag>>(r)); funding_[res.id] = std::move(f); }
      reservations_[res.id] = std::move(res);
    }
    const auto acount = r.u32();
    if (acount > settings.max_reservations) return fail("allocation count exceeds bound");
    for (std::uint32_t i = 0; i < acount; ++i) {
      Allocation a; a.id = BinaryReader::read_id<StrongId<AllocationTag>>(r);
      a.tenant = BinaryReader::read_id<StrongId<TenantTag>>(r); a.reservation = BinaryReader::read_id<StrongId<ReservationTag>>(r);
      a.resource_class = BinaryReader::read_id<StrongId<ResourceClassTag>>(r);
      a.resources = r.vector(); a.used = r.vector();
      a.started_at = static_cast<Nanos>(r.u64()); a.ended_at = static_cast<Nanos>(r.u64()); a.active = r.u8() != 0;
      a.quota_generation = QuotaGeneration(r.u64()); a.policy_generation = PolicyGeneration(r.u64()); a.epoch = CoordinatorEpoch(r.u64());
      a.owner_agent = BinaryReader::read_id<StrongId<AgentTag>>(r); a.owner_boot = BinaryReader::read_id<StrongId<AgentBootTag>>(r);
      a.label = r.string(); allocations_[a.id] = std::move(a);
    }
    const auto bcount = r.u32();
    if (bcount > settings.max_borrows) return fail("borrow count exceeds bound");
    for (std::uint32_t i = 0; i < bcount; ++i) {
      BorrowRecord b; b.id = BinaryReader::read_id<StrongId<BorrowTag>>(r);
      b.borrower = BinaryReader::read_id<StrongId<TenantTag>>(r); b.lender = BinaryReader::read_id<StrongId<TenantTag>>(r);
      b.lending = BinaryReader::read_id<StrongId<LendingTag>>(r);
      if (r.u8()) b.reservation = BinaryReader::read_id<StrongId<ReservationTag>>(r);
      b.amount = r.vector(); b.consumed = r.vector();
      b.started_at = static_cast<Nanos>(r.u64()); b.expires_at = static_cast<Nanos>(r.u64()); b.recall_priority = r.u32();
      b.debt = r.vector(); b.policy_generation = PolicyGeneration(r.u64()); b.quota_generation = QuotaGeneration(r.u64()); b.epoch = CoordinatorEpoch(r.u64());
      b.recalled = r.u8() != 0; borrows_[b.id] = std::move(b);
    }
    const auto lcount = r.u32();
    if (lcount > settings.max_borrows) return fail("lending count exceeds bound");
    for (std::uint32_t i = 0; i < lcount; ++i) {
      LendingRecord l; l.id = BinaryReader::read_id<StrongId<LendingTag>>(r);
      l.lender = BinaryReader::read_id<StrongId<TenantTag>>(r); l.borrower = BinaryReader::read_id<StrongId<TenantTag>>(r);
      l.resource_class = BinaryReader::read_id<StrongId<ResourceClassTag>>(r);
      l.amount = r.vector(); l.outstanding = r.vector();
      l.started_at = static_cast<Nanos>(r.u64()); l.expires_at = static_cast<Nanos>(r.u64()); l.recall_priority = r.u32();
      l.revocable = r.u8() != 0; l.policy_generation = PolicyGeneration(r.u64()); l.quota_generation = QuotaGeneration(r.u64()); l.epoch = CoordinatorEpoch(r.u64());
      lends_[l.id] = std::move(l);
    }
    r.require_end();
    // recompute usage from restored records
    for (const auto& [tid, unused] : tenants_) { (void)unused; sync_usage(tid); }
    loaded_ = true;
    return QuotaStatus::success();
  } catch (const decode_error& e) {
    return fail(std::string("corrupt persistence: ") + e.what());
  }
}

static bool atomic_replace(const std::string& tmp, const std::string& path, std::string* err) {
#ifdef _WIN32
  if (!MoveFileExA(tmp.c_str(), path.c_str(), MOVEFILE_REPLACE_EXISTING)) {
    if (err) *err = "MoveFileExA failed for " + path + " (error " + std::to_string(GetLastError()) + ")";
    return false;
  }
#else
  if (std::rename(tmp.c_str(), path.c_str()) != 0) { if (err) *err = "rename failed for " + path; return false; }
#endif
  return true;
}

QuotaStatus QuotaFabric::save_to(std::string_view path, std::string* err) const {
  std::lock_guard<std::recursive_mutex> lk(impl_->lock);
  try {
    BinaryWriter payload;
    impl_->serialize_state(payload);
    auto body = payload.take();
    BinaryWriter w;
    w.u32(kQuotaFabricMagic); w.u32(1); w.u64(body.size());
    w.bytes(body.data(), body.size());
    w.u32(crc32(body.data(), body.size()));
    const auto blob = w.take();
    const std::string tmp = std::string(path) + ".tmp";
    std::FILE* f = std::fopen(tmp.c_str(), "wb");
    if (!f) { if (err) *err = "cannot open temp file"; return QuotaStatus::failure(ViolationCode::RESOURCE_OVERFLOW, "cannot open temp file"); }
    const bool ok = std::fwrite(blob.data(), 1, blob.size(), f) == blob.size();
    std::fflush(f);
    const int rc = std::fclose(f);
    if (!ok || rc != 0) { if (err) *err = "write/flush failed"; return QuotaStatus::failure(ViolationCode::RESOURCE_OVERFLOW, "write failed"); }
    if (!atomic_replace(tmp, std::string(path), err)) return QuotaStatus::failure(ViolationCode::RESOURCE_OVERFLOW, *err);
    impl_->persist_path_ = std::string(path);
    impl_->emit_event(EventType::RECOVERY, TenantId{}, "state persisted");
    return QuotaStatus::success();
  } catch (const std::exception& e) {
    if (err) *err = e.what();
    return QuotaStatus::failure(ViolationCode::RESOURCE_OVERFLOW, "save failed");
  }
}

QuotaStatus QuotaFabric::load_from(std::string_view path, std::string* err) {
  std::lock_guard<std::recursive_mutex> lk(impl_->lock);
  std::FILE* f = std::fopen(std::string(path).c_str(), "rb");
  if (!f) { if (err) *err = "cannot open file"; return QuotaStatus::failure(ViolationCode::RESOURCE_OVERFLOW, "cannot open file"); }
  std::fseek(f, 0, SEEK_END); auto sz = std::ftell(f); std::fseek(f, 0, SEEK_SET);
  std::vector<std::uint8_t> blob;
  if (sz > 0) { blob.resize(static_cast<std::size_t>(sz)); const bool ok = std::fread(blob.data(), 1, blob.size(), f) == blob.size(); if (!ok) { std::fclose(f); if (err) *err = "read failed"; return QuotaStatus::failure(ViolationCode::RESOURCE_OVERFLOW, "read failed"); } }
  std::fclose(f);
  try {
    BinaryReader r(blob.data(), blob.size());
    if (r.u32() != kQuotaFabricMagic) { if (err) *err = "bad magic"; return QuotaStatus::failure(ViolationCode::RESOURCE_OVERFLOW, "bad magic"); }
    if (r.u32() != 1) { if (err) *err = "unsupported file version"; return QuotaStatus::failure(ViolationCode::RESOURCE_OVERFLOW, "unsupported file version"); }
    const auto len = r.u64();
    if (len != r.remaining() - 4) { if (err) *err = "length mismatch"; return QuotaStatus::failure(ViolationCode::RESOURCE_OVERFLOW, "length mismatch"); }
    const auto* body = blob.data() + r.offset();
    auto read_crc = [&](const std::uint8_t* p) {
      std::uint32_t v = 0;
      for (int i = 0; i < 4; ++i) v |= std::uint32_t(p[i]) << (8 * i);
      return v;
    };
    const auto expected = read_crc(body + len);
    const auto actual = crc32(body, len);
    if (expected != actual) { if (err) *err = "checksum mismatch (expected " + std::to_string(expected) + " actual " + std::to_string(actual) + " len " + std::to_string(len) + ")"; return QuotaStatus::failure(ViolationCode::RESOURCE_OVERFLOW, "checksum mismatch"); }
    BinaryReader body_reader(body, len);
    return impl_->deserialize_state(body_reader, err);
  } catch (const std::exception& e) {
    if (err) *err = e.what();
    return QuotaStatus::failure(ViolationCode::RESOURCE_OVERFLOW, "load failed");
  }
}

// ---------------------------------------------------------------------------
// invariants (used by property/adversarial tests)
// ---------------------------------------------------------------------------
bool QuotaFabric::invariants_ok(std::string* err) const {
  std::lock_guard<std::recursive_mutex> lk(impl_->lock);
  auto f = [&](std::string m) { if (err) *err = m; return false; };
  auto neg = [&](const ResourceVector& v) { return v.get(ResourceDimension::AcceleratorVRAM) < 0 || v.get(ResourceDimension::TransferBytes) < 0 || v.get(ResourceDimension::ConcurrentRequests) < 0; };

  // hierarchy acyclic
  for (const auto& [gid, g] : impl_->groups_) {
    std::unordered_set<TenantGroupId> seen; seen.insert(gid);
    std::optional<TenantGroupId> cur = g.parent;
    while (cur) {
      if (!seen.insert(*cur).second) return f("hierarchy cycle detected");
      auto it = impl_->groups_.find(*cur);
      if (it == impl_->groups_.end()) break;
      cur = it->second.parent;
    }
  }
  // no negative vectors in quotas
  for (const auto& [tid, q] : impl_->local_quota_) {
    (void)tid;
    if (neg(q.guaranteed) || neg(q.soft_limit) || neg(q.hard_limit) || neg(q.burst_limit) || neg(q.borrowable)) return f("negative quota value");
  }
  // per-tenant record/counter consistency
  for (const auto& [tid, u] : impl_->usage_) {
    const auto recompute = [&](ResourceVector& res, ResourceVector& cons, ResourceVector& borr, ResourceVector& lent) {
      res.clear(); cons.clear(); borr.clear(); lent.clear();
      for (const auto& [aid, a] : impl_->allocations_) if (a.active && a.tenant == tid) cons.add(a.used);
      std::unordered_map<ReservationId, ResourceVector> alloc_used;
      for (const auto& [aid, a] : impl_->allocations_) if (a.active && a.tenant == tid) alloc_used[a.reservation].add(a.used);
      for (const auto& [rid, r] : impl_->reservations_) {
        if (r.tenant != tid || !QuotaFabric::Impl::active_res(r)) continue;
        ResourceVector rem = r.resources;
        auto it = alloc_used.find(r.id);
        if (it != alloc_used.end()) { ResourceVector uc = ResourceVector::min(it->second, r.resources); rem.sub(uc, true); }
        res.add(rem);
      }
      for (const auto& [bid, b] : impl_->borrows_) if (b.borrower == tid && !b.recalled) borr.add(b.consumed);
      for (const auto& [lid, l] : impl_->lends_) if (l.lender == tid) lent.add(l.outstanding);
    };
    ResourceVector res, cons, borr, lent;
    recompute(res, cons, borr, lent);
    if (res != u.reserved) return f("reserved counter mismatch for tenant " + tid.to_hex());
    if (cons != u.consumption) return f("consumption counter mismatch for tenant " + tid.to_hex());
    if (borr != u.borrowed_consumed) return f("borrowed counter mismatch for tenant " + tid.to_hex());
    if (lent != u.lent) return f("lent counter mismatch for tenant " + tid.to_hex());
  }
  // borrows never exceed their lending offer amount
  for (const auto& [bid, b] : impl_->borrows_) {
    auto it = impl_->lends_.find(b.lending);
    if (it != impl_->lends_.end()) if (!it->second.amount.covers(b.amount)) return f("borrow exceeds lending offer");
  }
  // no negative resource amounts anywhere critical
  for (const auto& [bid, b] : impl_->borrows_) if (neg(b.consumed)) return f("negative borrow consumed");
  for (const auto& [rid, r] : impl_->reservations_) if (neg(r.resources)) return f("negative reservation");
  for (const auto& [aid, a] : impl_->allocations_) { (void)aid; if (ResourceVector::min(a.used, a.resources) != a.used) return f("allocation exceeds reservation"); }
  // accounting closure when empty
  for (const auto& [tid, u] : impl_->usage_) {
    if (u.consumption.is_empty() && u.reserved.is_empty() && u.borrowed_consumed.is_empty() && u.lent.is_empty() && u.debt.is_empty())
      if (!u.burst.is_empty()) return f("burst leak after zero state");
  }
  return true;
}

}  // namespace quota_fabric
