#include "quota_fabric/accounting/engine.hpp"
#include "quota_fabric/core/enum_defs.hpp"
#include <cstdio>
using namespace quota_fabric;
namespace {
[[maybe_unused]] ResourceVector V(std::int64_t b) { return ResourceVector::from_scalar(ResourceDimension::AcceleratorVRAM, b); }
[[maybe_unused]] ResourceVector CT(std::int64_t ms) { return ResourceVector::from_scalar(ResourceDimension::AcceleratorComputeTime, ms); }
[[maybe_unused]] ResourceVector TB(std::int64_t b) { return ResourceVector::from_scalar(ResourceDimension::TransferBytes, b); }
[[maybe_unused]] ResourceVector MR(std::int64_t b) { return ResourceVector::from_scalar(ResourceDimension::ModelResidencyBytes, b); }
[[maybe_unused]] ResourceVector KV(std::int64_t b) { return ResourceVector::from_scalar(ResourceDimension::KVCacheBytes, b); }
[[maybe_unused]] ResourceQuota Q(TenantId t) { ResourceQuota q; q.tenant = t; q.burst_rule.window = 30'000'000'000LL; return q; }
[[maybe_unused]] ResourceQuota QV(TenantId t, std::int64_t g, std::int64_t soft, std::int64_t hard, std::int64_t burst = 0, std::int64_t borrow = 0) {
  ResourceQuota q; q.tenant = t;
  q.guaranteed = V(g); q.soft_limit = V(soft); q.hard_limit = V(hard); q.burst_limit = V(burst); q.borrowable = V(borrow);
  q.burst_rule.window = 30'000'000'000LL; q.borrow_rule.borrow_enabled = true; q.borrow_rule.lend_enabled = true; return q;
}
[[maybe_unused]] QuotaFabric make_engine() { QuotaFabric ef; QuotaPolicy p; p.name = "example"; ef.set_policy(p); return ef; }
[[maybe_unused]] Authority AUTH(const QuotaFabric& e) { Authority a; a.epoch=e.epoch(); a.quota_generation=e.quota_generation(); a.policy_generation=e.current_policy().generation; a.resource_generation=ResourceGeneration::initial(); return a; }
[[maybe_unused]] Observation OBS(TenantId t, ResourceDimension d, std::int64_t amt, ObservationKind k) {
  Observation o; o.tenant=t; o.dimension=d; o.amount=amt; o.kind=k; o.at=mono_now_nanos(); o.resource_generation=ResourceGeneration::initial(); o.agent=AgentId::make(); o.boot=AgentBootId::make(); return o;
}
}  // namespace
int main() {
  auto ef = make_engine();
  auto org = ef.create_group_autoid("org", TenantGroupKind::ORGANIZATION, std::nullopt);
  auto team = ef.create_group_autoid("team", TenantGroupKind::TEAM, org);
  ResourceQuota oq = QV(TenantId{}, 10000, 10000, 10000); ef.set_group_quota(org, oq);
  ResourceQuota tq = QV(TenantId{}, 8000, 8000, 8000); ef.set_group_quota(team, tq);
  TenantId a; ef.create_tenant("a", team, 50, &a); auto auth = AUTH(ef);
  auto r = ef.reserve(a, V(4000), auth); std::printf("reserve 4000 under parent ceiling: %s\n", r.ok?"ALLOW":"DENY");
  auto r2 = ef.reserve(a, V(8000), auth); std::printf("reserve 8000 (exceeds team 8000 after 4000): %s (%s)\n", r2.ok?"ALLOW":"DENY", to_string(r2.status.code).data());
  return 0; }