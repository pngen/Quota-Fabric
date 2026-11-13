#include "tests/support/microtest.hpp"
#include "tests/support/test_util.hpp"
#include "quota_fabric/accounting/engine.hpp"

// Regression checks: the burst limit bounds everything held above soft, and
// rolling windows drop expired samples.

using namespace quota_fabric;
using namespace qf_test;

static QuotaFabric regression_engine() {
  QuotaFabric ef;
  QuotaPolicy p; p.name = "regression-policy"; p.description = "regression";
  ef.set_policy(p);
  return ef;
}

// The burst limit bounds the whole amount held above soft, including the
// first burst of a window.
TEST(burst_limit_bounds_usage_above_soft) {
  auto ef = regression_engine();
  TenantId t; REQUIRE(ef.create_tenant("t", std::nullopt, 50, &t).ok);
  REQUIRE(ef.set_tenant_quota(t, make_vram_quota(t, 0, 2048, 4096, 1024, 0)).ok);
  auto auth = make_authority(ef);
  auto r = ef.reserve(t, vram(4096), auth);   // 2048 above soft: twice the burst limit
  CHECK(!r.ok);
  CHECK(r.status.code == ViolationCode::SOFT_LIMIT_EXCEEDED);
  auto r2 = ef.reserve(t, vram(3072), auth);  // exactly the burst limit above soft
  CHECK(r2.ok);
  CHECK(ef.envelope(t).burst_usage.get(ResourceDimension::AcceleratorVRAM) == 1024);
  std::string err; CHECK_MSG(ef.invariants_ok(&err), err);
}

// Usage that has left the window no longer counts, even with no new sample.
TEST(rolling_window_drops_expired_samples) {
  RollingWindow w; w.window_ns = 1000;
  w.add(100, 50);
  CHECK(w.usage(600) == 50);
  CHECK(w.usage(5000) == 0);
}
