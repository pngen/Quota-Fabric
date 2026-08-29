// Deterministic randomized/property test with printed seed. Runs thousands of
// mixed operations and asserts core invariants after every op.
#include "tests/support/microtest.hpp"
#include "tests/support/test_util.hpp"
#include "quota_fabric/accounting/engine.hpp"

#include <random>
#include <vector>
#include <cstdio>

using namespace quota_fabric;
using namespace qf_test;

static std::uint64_t splitmix(std::uint64_t& x) {
  x += 0x9E3779B97F4A7C15ull;
  std::uint64_t z = x;
  z = (z ^ (z >> 30)) * 0xBF58476D1CE4E5B9ull;
  z = (z ^ (z >> 27)) * 0x94D049BB133111EBull;
  return z ^ (z >> 31);
}

TEST(property_random_ops_preserve_invariants) {
  const std::uint64_t seed = 0x1234567890ABCDEFull;
  std::printf("property seed = 0x%016llx\n", (unsigned long long)seed);
  std::uint64_t rng = seed;
  const int kTenants = 4;
  const int kOps = 20000;

  QuotaFabric ef;
  QuotaPolicy p; p.name = "property"; ef.set_policy(p);
  std::vector<TenantId> ts;
  for (int i = 0; i < kTenants; ++i) { TenantId t; ef.create_tenant("t" + std::to_string(i), std::nullopt, 50, &t); ts.push_back(t); }
  // each tenant: guarantee 4096, soft 8192, hard 16384, burst 4096, borrowable 4096
  for (auto t : ts) {
    ResourceQuota q = make_vram_quota(t, 4096, 8192, 16384, 4096, 4096);
    q.burst_rule.window = 30'000'000'000LL;
    ef.set_tenant_quota(t, q);
  }
  auto auth = make_authority(ef);
  std::vector<ReservationId> active;
  std::string err;
  for (int op = 0; op < kOps; ++op) {
    std::uint64_t x = splitmix(rng);
    const int t = static_cast<int>(x % kTenants);
    const std::int64_t amt = static_cast<std::int64_t>(1 + (splitmix(rng) % 512));
    const int kind = static_cast<int>((splitmix(rng) >> 8) % 5);
    if (kind == 0 || active.empty()) {
      auto r = ef.reserve(ts[t], vram(amt), auth);
      if (r.ok) active.push_back(r.value.id);
    } else if (kind == 1) {
      const auto rid = active[static_cast<std::size_t>(splitmix(rng) % active.size())];
      if (ef.reservation_exists(rid)) ef.release(rid, auth);
    } else if (kind == 2) {
      ef.borrow(ts[t], vram(amt), auth);
    } else if (kind == 3) {
      ef.reserve(ts[static_cast<std::size_t>(splitmix(rng) % kTenants)], vram(amt), auth);
    } else {
      ef.evaluate_reservation(ts[t], vram(amt));
    }
    CHECK_MSG(ef.invariants_ok(&err), err);
  }
  // release everything from the snapshot; accounting must return to zero
  auto snap = ef.snapshot();
  for (const auto& rs : snap.reservations) ef.release(rs.id, auth);
  for (const auto& b : snap.borrow_records) ef.recall_borrow(b.id, auth);
  bool all_zero = true;
  for (auto t : ts) { auto e = ef.envelope(t); if (e.committed_usage.present_count() != 0) all_zero = false; }
  CHECK(all_zero);
  std::string err2; CHECK(ef.invariants_ok(&err2));
}
