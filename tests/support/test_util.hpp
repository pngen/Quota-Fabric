#pragma once
#include "quota_fabric/accounting/engine.hpp"
#include "quota_fabric/core/enum_defs.hpp"

namespace qf_test {

inline quota_fabric::Authority make_authority(const quota_fabric::QuotaFabric& e) {
  quota_fabric::Authority a;
  a.epoch = e.epoch();
  a.quota_generation = e.quota_generation();
  a.policy_generation = e.current_policy().generation;
  a.agent = quota_fabric::AgentId::make();
  a.boot = quota_fabric::AgentBootId::make();
  a.resource_generation = quota_fabric::ResourceGeneration::initial();
  return a;
}

inline quota_fabric::ResourceVector vram(std::int64_t bytes) {
  return quota_fabric::ResourceVector::from_scalar(quota_fabric::ResourceDimension::AcceleratorVRAM, bytes);
}
inline quota_fabric::ResourceVector compute_ms(std::int64_t ms) {
  return quota_fabric::ResourceVector::from_scalar(quota_fabric::ResourceDimension::AcceleratorComputeTime, ms);
}
inline quota_fabric::ResourceVector transfer(std::int64_t bytes) {
  return quota_fabric::ResourceVector::from_scalar(quota_fabric::ResourceDimension::TransferBytes, bytes);
}

inline quota_fabric::ResourceQuota make_vram_quota(quota_fabric::TenantId t,
                                                   std::int64_t guaranteed,
                                                   std::int64_t soft = -1,
                                                   std::int64_t hard = -1,
                                                   std::int64_t burst = 0,
                                                   std::int64_t borrowable = 0) {
  quota_fabric::ResourceQuota q;
  q.tenant = t;
  q.guaranteed = vram(guaranteed);
  q.soft_limit = vram(soft >= 0 ? soft : guaranteed);
  q.hard_limit = vram(hard >= 0 ? hard : guaranteed);
  q.burst_limit = vram(burst);
  q.borrowable = vram(borrowable);
  q.burst_rule.window = 30'000'000'000LL;  // 30s
  q.borrow_rule.borrow_enabled = true;
  q.borrow_rule.lend_enabled = true;
  q.borrow_rule.preserves_guarantee = true;
  q.borrow_rule.max_borrows = 1000;
  return q;
}

}  // namespace qf_test
