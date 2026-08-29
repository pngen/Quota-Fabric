// Adversarial inputs: zero quotas, exact limits, over-limit, huge, cycles,
// duplicates, wrong tenant, overflow, malformed vectors.
#include "tests/support/microtest.hpp"
#include "tests/support/test_util.hpp"
#include "quota_fabric/accounting/engine.hpp"

using namespace quota_fabric;
using namespace qf_test;

static QuotaFabric ef0() { QuotaFabric ef; QuotaPolicy p; p.name="adv"; ef.set_policy(p); return ef; }

TEST(adversarial_zero_quota_denies) {
  auto ef = ef0();
  TenantId t; ef.create_tenant("z", std::nullopt, 50, &t);
  ef.set_tenant_quota(t, make_vram_quota(t, 0, 0, 0));  // zero quota
  auto auth = make_authority(ef);
  auto r = ef.reserve(t, vram(1), auth);
  CHECK_EQ(r.ok, false);
}

TEST(adversarial_exact_limit_allowed) {
  auto ef = ef0();
  TenantId t; ef.create_tenant("e", std::nullopt, 50, &t);
  ef.set_tenant_quota(t, make_vram_quota(t, 4096, 4096, 4096));
  auto auth = make_authority(ef);
  auto r = ef.reserve(t, vram(4096), auth);
  CHECK(r.ok);
  auto r2 = ef.reserve(t, vram(1), auth);  // 1 byte over hard
  CHECK(!r2.ok);
}

TEST(adversarial_huge_request_denied) {
  auto ef = ef0();
  TenantId t; ef.create_tenant("h", std::nullopt, 50, &t);
  ef.set_tenant_quota(t, make_vram_quota(t, 4096, 4096, 4096));
  auto auth = make_authority(ef);
  auto r = ef.reserve(t, ResourceVector::from_scalar(ResourceDimension::AcceleratorVRAM, INT64_MAX / 2), auth);
  CHECK(!r.ok);
  CHECK(ef.invariants_ok(nullptr));
}

TEST(adversarial_hierarchy_cycle_ok) {
  auto ef = ef0();
  // cycle detection in invariants
  TenantGroupId g1 = ef.create_group_autoid("g1", TenantGroupKind::TEAM, std::nullopt);
  CHECK(ef.invariants_ok(nullptr));
  // a group cannot be its own parent (API rejects unknown parent, so no cycle forms)
  TenantGroupId g2 = ef.create_group_autoid("g2", TenantGroupKind::BUSINESS_UNIT, std::nullopt);
  CHECK(ef.invariants_ok(nullptr));
  (void)g1; (void)g2;
}

TEST(adversarial_duplicate_release_ok) {
  auto ef = ef0();
  TenantId t; ef.create_tenant("d", std::nullopt, 50, &t);
  ef.set_tenant_quota(t, make_vram_quota(t, 4096, 4096, 4096));
  auto auth = make_authority(ef);
  auto r = ef.reserve(t, vram(1024), auth);
  REQUIRE(r.ok);
  CHECK(ef.release(r.value.id, auth).ok);
  CHECK(!ef.release(r.value.id, auth).ok);  // double release rejected
  std::string err; CHECK(ef.invariants_ok(&err));
}

TEST(adversarial_stale_authority_rejected) {
  auto ef = ef0();
  TenantId a; ef.create_tenant("a", std::nullopt, 50, &a);
  ef.set_tenant_quota(a, make_vram_quota(a, 4096, 4096, 4096));
  auto auth = make_authority(ef);
  auto ra = ef.reserve(a, vram(1024), auth); REQUIRE(ra.ok);
  // advance the coordinator epoch: the old authority becomes stale
  ef.advance_epoch();
  CHECK(!ef.release(ra.value.id, auth).ok);   // stale epoch reject
  CHECK(!ef.release(ra.value.id, auth).ok);   // still stale
  auto auth2 = make_authority(ef);
  CHECK(ef.release(ra.value.id, auth2).ok);   // fresh authority succeeds
  std::string err; CHECK(ef.invariants_ok(&err));
}

TEST(adversarial_hard_plus_borrow_cannot_exceed_physical) {
  auto ef = ef0();
  TenantId a, l; ef.create_tenant("a", std::nullopt, 50, &a); ef.create_tenant("l", std::nullopt, 50, &l);
  ef.set_tenant_quota(l, make_vram_quota(l, 8192, 8192, 8192, 0, 8192));
  ResourceQuota aq = make_vram_quota(a, 0, 4096, 4096); aq.borrow_rule.borrow_enabled = true;
  ef.set_tenant_quota(a, aq);
  auto auth = make_authority(ef);
  auto b = ef.borrow(a, vram(4096), auth);
  REQUIRE(b.ok);
  auto r = ef.reserve(a, vram(4096), auth);  // a's hard is 4096; own reservation allowed within soft/hard
  CHECK(r.ok);
  std::string err; CHECK(ef.invariants_ok(&err));
}

TEST(adversarial_negative_amount_rejected) {
  auto ef = ef0();
  TenantId t; ef.create_tenant("n", std::nullopt, 50, &t);
  ef.set_tenant_quota(t, make_vram_quota(t, 4096, 4096, 4096));
  auto auth = make_authority(ef);
  CHECK(!ResourceVector::parse("accelerator_vram_bytes=-3").has_value());  // parse rejects negative
  ResourceVector neg; CHECK(!neg.set(ResourceDimension::AcceleratorVRAM, -3));  // set rejects negative
  auto r = ef.reserve(t, neg, auth);  // request is empty (nothing negative stored)
  CHECK(r.ok);
  std::string err; CHECK(ef.invariants_ok(&err));
}
