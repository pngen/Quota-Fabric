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
  auto ef = make_engine(); TenantId t; ef.create_tenant("t", std::nullopt, 50, &t);
  ResourceQuota q = Q(t); q.guaranteed = MR(1<<30); q.soft_limit = q.guaranteed; q.hard_limit = q.guaranteed; ef.set_tenant_quota(t, q);
  ef.apply_observation(OBS(t, ResourceDimension::ModelResidencyBytes, 256<<20, ObservationKind::MODEL_RESIDENCY_ADDED));
  auto env = ef.envelope(t); std::printf("model residency consumed=%lld / guaranteed=%lld\n", (long long)env.current_consumption.get(ResourceDimension::ModelResidencyBytes), (long long)env.guaranteed.get(ResourceDimension::ModelResidencyBytes));
  return 0; }