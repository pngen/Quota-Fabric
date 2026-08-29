#include "quota_fabric/accounting/engine.hpp"
#include <cstdio>
int main() {
  quota_fabric::QuotaFabric ef;
  quota_fabric::TenantId t;
  ef.create_tenant("consumer", std::nullopt, 50, &t);
  quota_fabric::ResourceQuota q; q.tenant = t;
  q.guaranteed = quota_fabric::ResourceVector::from_scalar(quota_fabric::ResourceDimension::AcceleratorVRAM, 4096);
  q.soft_limit = q.guaranteed; q.hard_limit = q.guaranteed;
  ef.set_tenant_quota(t, q);
  quota_fabric::Authority a; a.epoch = ef.epoch(); a.quota_generation = ef.quota_generation();
  a.policy_generation = ef.current_policy().generation; a.resource_generation = quota_fabric::ResourceGeneration::initial();
  auto r = ef.reserve(t, q.guaranteed, a);
  std::printf("downstream consumer: reserve %s (reservations=%zu)\n", r.ok ? "allowed" : "denied", ef.snapshot().reservations.size());
  return r.ok ? 0 : 1;
}
