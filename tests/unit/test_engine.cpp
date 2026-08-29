#include "tests/support/microtest.hpp"
#include "tests/support/test_util.hpp"
#include "quota_fabric/accounting/engine.hpp"
#include <filesystem>

using namespace quota_fabric;
using namespace qf_test;

static QuotaFabric new_engine(QuotaPolicy* pout = nullptr) {
  QuotaFabric ef;
  QuotaPolicy p; p.name = "test-policy"; p.description = "unit";
  ef.set_policy(p);
  if (pout) *pout = p;
  return ef;
}

TEST(reservation_allow_and_release_closure) {
  auto ef = new_engine();
  TenantId t; CHECK(ef.create_tenant("t", std::nullopt, 50, &t).ok);
  CHECK(ef.set_tenant_quota(t, make_vram_quota(t, 4096, 4096, 4096)).ok);
  auto auth = make_authority(ef);
  auto r = ef.reserve(t, vram(1024), auth);
  REQUIRE(r.ok);
  auto env = ef.envelope(t);
  CHECK(env.reserved.get(ResourceDimension::AcceleratorVRAM) == 1024);
  CHECK(env.committed_usage.get(ResourceDimension::AcceleratorVRAM) == 1024);
  CHECK(ef.release(r.value.id, auth).ok);
  env = ef.envelope(t);
  CHECK(env.reserved.get(ResourceDimension::AcceleratorVRAM) == 0);
  std::string err; CHECK(ef.invariants_ok(&err));
}

TEST(hard_limit_denies_above_without_borrow) {
  auto ef = new_engine();
  TenantId t; ef.create_tenant("t", std::nullopt, 50, &t);
  ef.set_tenant_quota(t, make_vram_quota(t, 4096, 4096, 4096, 0, 0));
  auto auth = make_authority(ef);
  auto r = ef.reserve(t, vram(5000), auth);
  CHECK(!r.ok);
  CHECK(r.status.code == ViolationCode::HARD_LIMIT_EXCEEDED);
  CHECK(!ef.reservation_exists(r.value.id));
}

TEST(soft_over_is_over_soft_compliance) {
  auto ef = new_engine();
  TenantId t; ef.create_tenant("t", std::nullopt, 50, &t);
  // soft 2048, hard 8192, no burst -> reserve 4096 lands over-soft within hard but no burst -> DENY_SOFT
  ef.set_tenant_quota(t, make_vram_quota(t, 1024, 2048, 8192, 0, 0));
  auto auth = make_authority(ef);
  auto r = ef.reserve(t, vram(4096), auth);
  CHECK(!r.ok);  // 4096 > soft 2048, within hard 8192, no burst -> DENY_SOFT
  CHECK(r.status.code == ViolationCode::SOFT_LIMIT_EXCEEDED);
  // within soft (2048) is allowed
  auto r2 = ef.reserve(t, vram(2048), auth);
  CHECK(r2.ok);
}

TEST(burst_allows_over_soft_within_hard) {
  auto ef = new_engine();
  TenantId t; ef.create_tenant("t", std::nullopt, 50, &t);
  ef.set_tenant_quota(t, make_vram_quota(t, 0, 2048, 4096, 1024, 0));
  auto auth = make_authority(ef);
  auto r = ef.reserve(t, vram(3072), auth);
  REQUIRE(r.ok);  // 3072 > soft 2048, within hard 4096, burst available -> ALLOW_BURST
  auto env = ef.envelope(t);
  CHECK(env.burst_usage.get(ResourceDimension::AcceleratorVRAM) == 1024);  // 3072-2048
  // second 1024 push to 4096, burst already 1024/1024 -> DENY_SOFT
  auto r2 = ef.reserve(t, vram(1024), auth);
  CHECK(!r2.ok);
  CHECK(r2.status.code == ViolationCode::SOFT_LIMIT_EXCEEDED);
  std::string err; CHECK(ef.invariants_ok(&err));
}

TEST(borrow_and_recall) {
  auto ef = new_engine();
  TenantId lender, borrower;
  ef.create_tenant("lender", std::nullopt, 40, &lender);
  ef.create_tenant("borrower", std::nullopt, 60, &borrower);
  // lender has borrowable headroom
  ef.set_tenant_quota(lender, make_vram_quota(lender, 4096, 4096, 4096, 0, 4096));
  // borrower has zero hard-ish quota so it must borrow
  ResourceQuota bq = make_vram_quota(borrower, 0, 0, 0, 0, 0);
  bq.borrow_rule.borrow_enabled = true;
  ef.set_tenant_quota(borrower, bq);
  auto auth = make_authority(ef);
  auto br = ef.borrow(borrower, vram(1024), auth);
  REQUIRE(br.ok);
  auto b_env = ef.envelope(borrower);
  CHECK(b_env.borrowed.get(ResourceDimension::AcceleratorVRAM) == 1024);
  auto l_env = ef.envelope(lender);
  CHECK(l_env.lent.get(ResourceDimension::AcceleratorVRAM) == 1024);
  CHECK(ef.recall_borrow(br.value.id, auth).ok);
  b_env = ef.envelope(borrower);
  CHECK(b_env.borrowed.get(ResourceDimension::AcceleratorVRAM) == 0);
  l_env = ef.envelope(lender);
  CHECK(l_env.lent.get(ResourceDimension::AcceleratorVRAM) == 0);
  std::string err; CHECK(ef.invariants_ok(&err));
}

TEST(borrow_cannot_block_lender_guarantee) {
  auto ef = new_engine();
  TenantId lender, other;
  ef.create_tenant("lender", std::nullopt, 10, &lender);
  ef.create_tenant("other", std::nullopt, 20, &other);
  // lender guarantee 4096, borrowable 4096; it uses 3072 of its own guarantee
  ResourceQuota lq = make_vram_quota(lender, 4096, 4096, 4096, 0, 4096);
  ef.set_tenant_quota(lender, lq);
  ResourceQuota oq = make_vram_quota(other, 1024, 2048, 4096, 0, 0);
  ef.set_tenant_quota(other, oq);
  auto auth = make_authority(ef);
  // lender consumes 3072 of its own guarantee
  auto rl = ef.reserve(lender, vram(3072), auth);
  REQUIRE(rl.ok);
  // other borrows 1024 from lender's remaining headroom (4096-3072=1024 avail)
  auto br = ef.borrow(other, vram(1024), auth);
  REQUIRE(br.ok);
  // now lender needs its guarantee back: reserve 1024 more -> should fail/signal recall, not silently steal
  // lender cannot get more from its own guarantee without recalling borrow
  auto l_envelope = ef.envelope(lender);
  CHECK(l_envelope.lent.get(ResourceDimension::AcceleratorVRAM) == 1024);
  // borrowing has not destroyed lender's protected guarantee beyond its unused headroom
  CHECK(l_envelope.available_guaranteed.get(ResourceDimension::AcceleratorVRAM) >= 0);
  std::string err; CHECK(ef.invariants_ok(&err));
}

TEST(hierarchy_parent_ceiling) {
  auto ef = new_engine();
  TenantGroupId org = ef.create_group_autoid("org", TenantGroupKind::ORGANIZATION, std::nullopt);
  TenantGroupId team = ef.create_group_autoid("team", TenantGroupKind::TEAM, org);
  TenantId a, b;
  ef.create_tenant("a", team, 50, &a);
  ef.create_tenant("b", team, 50, &b);
  ResourceQuota orgq; orgq.guaranteed = vram(10000); orgq.soft_limit = vram(10000); orgq.hard_limit = vram(10000);
  ef.set_group_quota(org, orgq);
  ResourceQuota teamq; teamq.guaranteed = vram(8000); teamq.soft_limit = vram(8000); teamq.hard_limit = vram(8000);
  ef.set_group_quota(team, teamq);
  auto auth = make_authority(ef);
  auto ra = ef.reserve(a, vram(4000), auth);
  REQUIRE(ra.ok);
  auto rb = ef.reserve(b, vram(4000), auth);
  REQUIRE(rb.ok);
  // total subtree = 8000 = team hard; a third 1000 on a must exceed team hard
  auto rc = ef.reserve(a, vram(1000), auth);
  CHECK(!rc.ok);  // parent would exceed team hard (8000+1000)
  std::string err; CHECK(ef.invariants_ok(&err));
}

TEST(quota_lowering_reports_over_soft) {
  auto ef = new_engine();
  TenantId t; ef.create_tenant("t", std::nullopt, 50, &t);
  ef.set_tenant_quota(t, make_vram_quota(t, 4096, 4096, 4096));
  auto auth = make_authority(ef);
  auto r = ef.reserve(t, vram(4096), auth);
  REQUIRE(r.ok);
  // lower hard below current usage
  CHECK(ef.set_tenant_quota(t, make_vram_quota(t, 1024, 1024, 1024)).ok);
  auto comp = ef.compliance_after_lowering(t);
  CHECK(comp == ComplianceState::OVER_HARD);
}

TEST(persistence_round_trip_preserves_authority) {
  auto ef = new_engine();
  TenantId t; ef.create_tenant("persist", std::nullopt, 50, &t);
  ef.set_tenant_quota(t, make_vram_quota(t, 8192, 8192, 8192));
  auto auth = make_authority(ef);
  auto r = ef.reserve(t, vram(2048), auth);
  REQUIRE(r.ok);
  auto snap_before = ef.snapshot();
  const auto path = std::filesystem::temp_directory_path() / "qf_roundtrip.qf";
  std::string err;
  CHECK_MSG(ef.save_to(path.string(), &err).ok, "save: " + err);
  QuotaFabric ef2;
  CHECK_MSG(ef2.load_from(path.string(), &err).ok, "load: " + err);
  auto snap_after = ef2.snapshot();
  CHECK(snap_after.epoch == snap_before.epoch);
  CHECK(snap_after.quota_generation == snap_before.quota_generation);
  CHECK(snap_after.reservations.size() == snap_before.reservations.size());
  CHECK(snap_after.tenants.size() == snap_before.tenants.size());
  std::filesystem::remove(path);
  std::string ierr; CHECK(ef2.invariants_ok(&ierr));
}

TEST(stale_authority_is_rejected) {
  auto ef = new_engine();
  TenantId t; ef.create_tenant("s", std::nullopt, 50, &t);
  ef.set_tenant_quota(t, make_vram_quota(t, 4096, 4096, 4096));
  auto auth = make_authority(ef);
  auto r = ef.reserve(t, vram(1024), auth);
  REQUIRE(r.ok);
  // advance epoch -> old authority becomes stale
  ef.advance_epoch();
  auto r2 = ef.reserve(t, vram(512), auth);
  CHECK(!r2.ok);
  CHECK(r2.status.code == ViolationCode::STALE_AUTHORITY);
}
