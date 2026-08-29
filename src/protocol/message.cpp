#include "quota_fabric/protocol/message.hpp"
#include "quota_fabric/persistence/codec.hpp"

namespace quota_fabric {

namespace {
template <class Tag> void w_id(BinaryWriter& w, const StrongId<Tag>& id) { w.id(id); }
template <class Tag> StrongId<Tag> r_id(BinaryReader& r) { return BinaryReader::read_id<StrongId<Tag>>(r); }
// Remove the ambiguous "no-op" overload: only the generic w_id is needed.

void w_quota(BinaryWriter& w, const ResourceQuota& q) {
  w_id(w, q.tenant); w_id(w, q.resource_class);
  w.vector(q.guaranteed); w.vector(q.soft_limit); w.vector(q.hard_limit); w.vector(q.burst_limit);
  w.vector(q.borrowable); w.vector(q.physical_capacity); w.vector(q.shared_pool);
  w.u64(std::bit_cast<std::uint64_t>(q.burst_rule.percent_above_soft)); w.vector(q.burst_rule.absolute_burst);
  w.u64(static_cast<std::uint64_t>(q.burst_rule.window)); w.u32(q.burst_rule.max_simultaneous);
  w.u64(static_cast<std::uint64_t>(q.burst_rule.cooldown)); w.u8(q.burst_rule.allow_debt ? 1 : 0);
  w.u8(q.borrow_rule.borrow_enabled ? 1 : 0); w.u8(q.borrow_rule.lend_enabled ? 1 : 0);
  w.u8(q.borrow_rule.recall_before_new_lend ? 1 : 0); w.u64(static_cast<std::uint64_t>(q.borrow_rule.max_borrow_duration));
  w.u8(q.borrow_rule.preserves_guarantee ? 1 : 0); w.u32(q.borrow_rule.max_borrows);
  w.u8(static_cast<std::uint8_t>(q.overcommit)); w.u64(std::bit_cast<std::uint64_t>(q.overcommit_factor));
  w.u64(q.generation.value()); w.u64(q.policy_generation.value());
}
void r_quota(BinaryReader& r, ResourceQuota& q) {
  q.tenant = r_id<TenantTag>(r); q.resource_class = r_id<ResourceClassTag>(r);
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
void w_authority(BinaryWriter& w, const Authority& a) {
  w.u64(a.epoch.value()); w.u64(a.quota_generation.value()); w.u64(a.policy_generation.value());
  w_id(w, a.agent); w_id(w, a.boot); w.u64(a.resource_generation.value());
}
Authority r_authority(BinaryReader& r) {
  Authority a;
  a.epoch = CoordinatorEpoch(r.u64()); a.quota_generation = QuotaGeneration(r.u64()); a.policy_generation = PolicyGeneration(r.u64());
  a.agent = r_id<AgentTag>(r); a.boot = r_id<AgentBootTag>(r); a.resource_generation = ResourceGeneration(r.u64());
  return a;
}
void w_obs(BinaryWriter& w, const Observation& o) {
  w_id(w, o.tenant); w_id(w, o.allocation); w.u16(static_cast<std::uint16_t>(o.dimension)); w.i64(o.amount);
  w.u8(static_cast<std::uint8_t>(o.kind)); w.u64(o.at > 0 ? static_cast<std::uint64_t>(o.at) : 0);
  w.u64(o.resource_generation.value()); w_id(w, o.agent); w_id(w, o.boot); w.string(o.source);
}
Observation r_obs(BinaryReader& r) {
  Observation o;
  o.tenant = r_id<TenantTag>(r); o.allocation = r_id<AllocationTag>(r);
  o.dimension = static_cast<ResourceDimension>(r.u16()); o.amount = r.i64();
  o.kind = static_cast<ObservationKind>(r.u8()); o.at = static_cast<Nanos>(r.u64());
  o.resource_generation = ResourceGeneration(r.u64()); o.agent = r_id<AgentTag>(r); o.boot = r_id<AgentBootTag>(r); o.source = r.string();
  return o;
}
}  // namespace

std::vector<std::uint8_t> encode_request(const WireRequest& q) {
  BinaryWriter w;
  w.u8(static_cast<std::uint8_t>(q.type));
  w_authority(w, q.auth);
  w_id(w, q.tenant); w_id(w, q.tenant2);
  w.u8(q.group.is_null() ? 0 : 1); if (!q.group.is_null()) w_id(w, q.group);
  w.u8(static_cast<std::uint8_t>(q.kind));
  w_id(w, q.reservation); w_id(w, q.allocation); w_id(w, q.borrow);
  w_id(w, q.agent_id); w_id(w, q.agent_boot); w_id(w, q.node_id); w.u64(q.node_boot.value());
  w.string(q.name); w.string(q.name2); w.string(q.service_class); w.string(q.path);
  w.u32(q.priority); w.u32(q.recall_priority);
  w.vector(q.req); w.vector(q.req2);
  w_quota(w, q.quota);
  w_obs(w, q.observation);
  w.u64(q.amount);
  return w.take();
}
WireRequest decode_request(const std::vector<std::uint8_t>& p) {
  BinaryReader r(p.data(), p.size());
  WireRequest q;
  q.type = static_cast<MessageType>(r.u8());
  q.auth = r_authority(r);
  q.tenant = r_id<TenantTag>(r); q.tenant2 = r_id<TenantTag>(r);
  if (r.u8()) q.group = r_id<TenantGroupTag>(r);
  q.kind = static_cast<TenantGroupKind>(r.u8());
  q.reservation = r_id<ReservationTag>(r); q.allocation = r_id<AllocationTag>(r); q.borrow = r_id<BorrowTag>(r);
  q.agent_id = r_id<AgentTag>(r); q.agent_boot = r_id<AgentBootTag>(r); q.node_id = r_id<NodeTag>(r); q.node_boot = NodeBootGeneration(r.u64());
  q.name = r.string(); q.name2 = r.string(); q.service_class = r.string(); q.path = r.string();
  q.priority = r.u32(); q.recall_priority = r.u32();
  q.req = r.vector(); q.req2 = r.vector();
  r_quota(r, q.quota);
  q.observation = r_obs(r);
  q.amount = r.u64();
  return q;
}

WireResponse response_from(QuotaStatus st) {
  WireResponse r;
  r.ok = st.ok; r.code = st.code; r.message = st.message;
  return r;
}

// ---- response serialization ----
static void w_env(BinaryWriter& w, const QuotaEnvelope& e) {
  w_id(w, e.tenant); w_id(w, e.resource_class);
  w.vector(e.guaranteed); w.vector(e.soft_limit); w.vector(e.hard_limit); w.vector(e.burst_limit);
  w.vector(e.current_consumption); w.vector(e.reserved); w.vector(e.committed_usage); w.vector(e.borrowed);
  w.vector(e.burst_usage); w.vector(e.lent); w.vector(e.debt);
  w.signed_vector(e.available_guaranteed); w.signed_vector(e.available_soft); w.signed_vector(e.available_burst);
  w.u8(static_cast<std::uint8_t>(e.compliance)); w.u8(e.primary_violation ? static_cast<std::uint8_t>(*e.primary_violation) : 0);
  w.u64(e.generation.value()); w.u64(e.policy_generation.value()); w.u64(e.epoch.value());
  w.u64(static_cast<std::uint64_t>(e.burst_expires_at)); w.u8(e.burst_active ? 1 : 0);
}
static void r_env(BinaryReader& r, QuotaEnvelope& e) {
  e.tenant = r_id<TenantTag>(r); e.resource_class = r_id<ResourceClassTag>(r);
  e.guaranteed = r.vector(); e.soft_limit = r.vector(); e.hard_limit = r.vector(); e.burst_limit = r.vector();
  e.current_consumption = r.vector(); e.reserved = r.vector(); e.committed_usage = r.vector(); e.borrowed = r.vector();
  e.burst_usage = r.vector(); e.lent = r.vector(); e.debt = r.vector();
  e.available_guaranteed = r.signed_vector(); e.available_soft = r.signed_vector(); e.available_burst = r.signed_vector();
  e.compliance = static_cast<ComplianceState>(r.u8()); const auto pv = r.u8(); if (pv) e.primary_violation = static_cast<ViolationCode>(pv);
  e.generation = QuotaGeneration(r.u64()); e.policy_generation = PolicyGeneration(r.u64()); e.epoch = CoordinatorEpoch(r.u64());
  e.burst_expires_at = static_cast<Nanos>(r.u64()); e.burst_active = r.u8() != 0;
}
static void w_res(BinaryWriter& w, const Reservation& r) {
  w_id(w, r.id); w_id(w, r.tenant); w_id(w, r.resource_class); w.vector(r.resources); w.vector(r.consumed);
  w.u8(static_cast<std::uint8_t>(r.status)); w.u64(static_cast<std::uint64_t>(r.created_at)); w.u64(static_cast<std::uint64_t>(r.expires_at));
  w.u64(r.quota_generation.value()); w.u64(r.policy_generation.value()); w.u64(r.epoch.value());
  w_id(w, r.owner_agent); w_id(w, r.owner_boot); w.string(r.label); w.u64(r.resource_generation.value());
}
static void r_res(BinaryReader& rd, Reservation& r) {
  r.id = r_id<ReservationTag>(rd); r.tenant = r_id<TenantTag>(rd); r.resource_class = r_id<ResourceClassTag>(rd);
  r.resources = rd.vector(); r.consumed = rd.vector(); r.status = static_cast<ReservationStatus>(rd.u8());
  r.created_at = static_cast<Nanos>(rd.u64()); r.expires_at = static_cast<Nanos>(rd.u64());
  r.quota_generation = QuotaGeneration(rd.u64()); r.policy_generation = PolicyGeneration(rd.u64()); r.epoch = CoordinatorEpoch(rd.u64());
  r.owner_agent = r_id<AgentTag>(rd); r.owner_boot = r_id<AgentBootTag>(rd); r.label = rd.string(); r.resource_generation = ResourceGeneration(rd.u64());
}
static void w_alloc(BinaryWriter& w, const Allocation& a) {
  w_id(w, a.id); w_id(w, a.tenant); w_id(w, a.reservation); w_id(w, a.resource_class); w.vector(a.resources); w.vector(a.used);
  w.u64(static_cast<std::uint64_t>(a.started_at)); w.u64(static_cast<std::uint64_t>(a.ended_at)); w.u8(a.active ? 1 : 0);
  w.u64(a.quota_generation.value()); w.u64(a.policy_generation.value()); w.u64(a.epoch.value());
  w_id(w, a.owner_agent); w_id(w, a.owner_boot); w.string(a.label);
}
static void r_alloc(BinaryReader& rd, Allocation& a) {
  a.id = r_id<AllocationTag>(rd); a.tenant = r_id<TenantTag>(rd); a.reservation = r_id<ReservationTag>(rd); a.resource_class = r_id<ResourceClassTag>(rd);
  a.resources = rd.vector(); a.used = rd.vector(); a.started_at = static_cast<Nanos>(rd.u64()); a.ended_at = static_cast<Nanos>(rd.u64()); a.active = rd.u8() != 0;
  a.quota_generation = QuotaGeneration(rd.u64()); a.policy_generation = PolicyGeneration(rd.u64()); a.epoch = CoordinatorEpoch(rd.u64());
  a.owner_agent = r_id<AgentTag>(rd); a.owner_boot = r_id<AgentBootTag>(rd); a.label = rd.string();
}
static void w_borrow(BinaryWriter& w, const BorrowRecord& b) {
  w_id(w, b.id); w_id(w, b.borrower); w_id(w, b.lender); w_id(w, b.lending);
  w.u8(b.reservation ? 1 : 0); if (b.reservation) w_id(w, *b.reservation);
  w.vector(b.amount); w.vector(b.consumed); w.u64(static_cast<std::uint64_t>(b.started_at)); w.u64(static_cast<std::uint64_t>(b.expires_at));
  w.u32(b.recall_priority); w.vector(b.debt); w.u64(b.policy_generation.value()); w.u64(b.quota_generation.value()); w.u64(b.epoch.value()); w.u8(b.recalled ? 1 : 0);
}
static void r_borrow(BinaryReader& rd, BorrowRecord& b) {
  b.id = r_id<BorrowTag>(rd); b.borrower = r_id<TenantTag>(rd); b.lender = r_id<TenantTag>(rd); b.lending = r_id<LendingTag>(rd);
  if (rd.u8()) b.reservation = r_id<ReservationTag>(rd);
  b.amount = rd.vector(); b.consumed = rd.vector(); b.started_at = static_cast<Nanos>(rd.u64()); b.expires_at = static_cast<Nanos>(rd.u64()); b.recall_priority = rd.u32();
  b.debt = rd.vector(); b.policy_generation = PolicyGeneration(rd.u64()); b.quota_generation = QuotaGeneration(rd.u64()); b.epoch = CoordinatorEpoch(rd.u64()); b.recalled = rd.u8() != 0;
}
static void w_lend(BinaryWriter& w, const LendingRecord& l) {
  w_id(w, l.id); w_id(w, l.lender); w_id(w, l.borrower); w_id(w, l.resource_class); w.vector(l.amount); w.vector(l.outstanding);
  w.u64(static_cast<std::uint64_t>(l.started_at)); w.u64(static_cast<std::uint64_t>(l.expires_at)); w.u32(l.recall_priority); w.u8(l.revocable ? 1 : 0);
  w.u64(l.policy_generation.value()); w.u64(l.quota_generation.value()); w.u64(l.epoch.value());
}
static void r_lend(BinaryReader& rd, LendingRecord& l) {
  l.id = r_id<LendingTag>(rd); l.lender = r_id<TenantTag>(rd); l.borrower = r_id<TenantTag>(rd); l.resource_class = r_id<ResourceClassTag>(rd);
  l.amount = rd.vector(); l.outstanding = rd.vector(); l.started_at = static_cast<Nanos>(rd.u64()); l.expires_at = static_cast<Nanos>(rd.u64()); l.recall_priority = rd.u32(); l.revocable = rd.u8() != 0;
  l.policy_generation = PolicyGeneration(rd.u64()); l.quota_generation = QuotaGeneration(rd.u64()); l.epoch = CoordinatorEpoch(rd.u64());
}

std::vector<std::uint8_t> encode_response(const WireResponse& r) {
  BinaryWriter w;
  w.u8(r.ok ? 1 : 0); w.u8(static_cast<std::uint8_t>(r.code)); w.string(r.message);
  w_env(w, r.envelope);
  w.u8(static_cast<std::uint8_t>(r.decision.code)); w.string(r.decision.explanation.human());
  w.vector(r.decision.requested); w.vector(r.decision.guarantee); w.vector(r.decision.soft_limit); w.vector(r.decision.hard_limit);
  w.u8(r.decision.limiting_dimension ? static_cast<std::uint8_t>(*r.decision.limiting_dimension) : 255);
  w_res(w, r.reservation); w_alloc(w, r.allocation); w_borrow(w, r.borrow); w_lend(w, r.lend);
  w.u32(static_cast<std::uint32_t>(r.recall.size()));
  for (auto a : r.recall) w.u8(static_cast<std::uint8_t>(a));
  w.u64(r.epoch.value()); w.u64(r.quota_generation.value()); w.u64(r.policy_generation.value());
  w.string(r.text);
  return w.take();
}
WireResponse decode_response(const std::vector<std::uint8_t>& p) {
  BinaryReader r(p.data(), p.size());
  WireResponse resp;
  resp.ok = r.u8() != 0; resp.code = static_cast<ViolationCode>(r.u8()); resp.message = r.string();
  r_env(r, resp.envelope);
  resp.decision.code = static_cast<DecisionCode>(r.u8());
  resp.decision.explanation = Explanation{};
  resp.decision.explanation.lines.push_back({ViolationCode::NONE, r.string()});
  resp.decision.requested = r.vector(); resp.decision.guarantee = r.vector(); resp.decision.soft_limit = r.vector(); resp.decision.hard_limit = r.vector();
  const auto ld = r.u8(); if (ld != 255) resp.decision.limiting_dimension = static_cast<ResourceDimension>(ld);
  r_res(r, resp.reservation); r_alloc(r, resp.allocation); r_borrow(r, resp.borrow); r_lend(r, resp.lend);
  const auto rc = r.u32(); resp.recall.reserve(rc);
  for (std::uint32_t i = 0; i < rc; ++i) resp.recall.push_back(static_cast<RecallAction>(r.u8()));
  resp.epoch = CoordinatorEpoch(r.u64()); resp.quota_generation = QuotaGeneration(r.u64()); resp.policy_generation = PolicyGeneration(r.u64());
  resp.text = r.string();
  return resp;
}
}  // namespace quota_fabric
