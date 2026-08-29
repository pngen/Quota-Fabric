// Concurrency stress: heavy mutation on a shared engine with multiple threads.
#include "tests/support/microtest.hpp"
#include "tests/support/test_util.hpp"
#include "quota_fabric/accounting/engine.hpp"
#include <thread>
#include <atomic>
#include <vector>

using namespace quota_fabric;
using namespace qf_test;

TEST(concurrency_reservation_storm) {
  const int kThreads = 8;
  const int kOps = 4000;
  QuotaFabric ef;
  QuotaPolicy p; p.name = "conc"; ef.set_policy(p);
  TenantId t; ef.create_tenant("t", std::nullopt, 50, &t);
  ResourceQuota q = make_vram_quota(t, 1LL << 40, 1LL << 40, 1LL << 40);  // 1 TiB pool
  ef.set_tenant_quota(t, q);
  // The engine is internally synchronized; each thread does its own reserve/release.
  std::atomic<std::uint64_t> committed{0};
  std::atomic<bool> bad{false};
  std::vector<std::thread> ths;
  for (int th = 0; th < kThreads; ++th) {
    ths.emplace_back([&] {
      auto auth = make_authority(ef);
      for (int i = 0; i < kOps; ++i) {
        auto r = ef.reserve(t, vram(4096), auth);
        if (r.ok) {
          committed += 4096;
          ef.release(r.value.id, auth);
          committed -= 4096;
        }
      }
      std::string err;
      if (!ef.invariants_ok(&err)) bad = true;
    });
  }
  for (auto& th : ths) if (th.joinable()) th.join();
  CHECK(!bad.load());
  CHECK_EQ(committed.load(), 0ull);
  auto env = ef.envelope(t);
  CHECK(env.reserved.get(ResourceDimension::AcceleratorVRAM) == 0);
}
