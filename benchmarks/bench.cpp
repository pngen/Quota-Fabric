#include "quota_fabric/accounting/engine.hpp"
#include "quota_fabric/core/enum_defs.hpp"
#include <chrono>
#include <cstdio>
#include <thread>
#include <atomic>
#include <vector>

using namespace quota_fabric;

static ResourceVector V(std::int64_t b) { return ResourceVector::from_scalar(ResourceDimension::AcceleratorVRAM, b); }
static QuotaFabric new_engine(int tenants, std::vector<TenantId>& ts) {
  QuotaFabric ef; QuotaPolicy p; p.name = "bench"; ef.set_policy(p);
  for (int i = 0; i < tenants; ++i) { TenantId t; ef.create_tenant("t"+std::to_string(i), std::nullopt, 50, &t); ts.push_back(t); }
  return ef;
}
static Authority auth(const QuotaFabric& e) { Authority a; a.epoch=e.epoch(); a.quota_generation=e.quota_generation(); a.policy_generation=e.current_policy().generation; a.resource_generation=ResourceGeneration::initial(); return a; }

int main() {
  std::setvbuf(stdout, nullptr, _IONBF, 0);
  std::printf("Quota Fabric benchmarks (completed work)\n");
  std::vector<TenantId> ts; auto ef = new_engine(100, ts);
  for (auto t : ts) { ResourceQuota q; q.tenant=t; q.guaranteed=V(16384); q.soft_limit=V(16384); q.hard_limit=V(16384); ef.set_tenant_quota(t, q); }
  auto a = auth(ef);

  { constexpr int N = 200000; auto start=std::chrono::steady_clock::now();
    for (int i=0;i<N;++i) ef.evaluate_reservation(ts[i%ts.size()], V(1024));
    auto end=std::chrono::steady_clock::now(); double s=std::chrono::duration<double>(end-start).count();
    std::printf("quota evaluation: %d ops in %.3f s = %.0f ops/s (%.2f us/op)\n", N, s, N/s, s/N*1e6); }

  { constexpr int N = 100000; auto start=std::chrono::steady_clock::now();
    for (int i=0;i<N;++i) { auto r=ef.reserve(ts[i%ts.size()], V(1024), a); if (r.ok) ef.release(r.value.id, a); }
    auto end=std::chrono::steady_clock::now(); double s=std::chrono::duration<double>(end-start).count();
    std::printf("reservation commit+release: %d ops in %.3f s = %.0f ops/s\n", N, s, N/s); }

  { constexpr int N = 2000; std::vector<ReservationId> ids;
    for (int i=0;i<N;++i) { auto r=ef.reserve(ts[i%ts.size()], V(1), a); if (r.ok) ids.push_back(r.value.id); }
    auto start=std::chrono::steady_clock::now();
    for (int i=0;i<N;++i) { auto s=ef.snapshot(); (void)s; }
    auto end=std::chrono::steady_clock::now(); double s=std::chrono::duration<double>(end-start).count();
    std::printf("snapshot (%d tenants, %zu live res): %d snapshots in %.3f s = %.0f snapshots/s\n", (int)ts.size(), ids.size(), N, s, N/s);
    for (auto id : ids) ef.release(id, a); }

  { constexpr int kThreads = 4; constexpr int kOps = 20000; auto start=std::chrono::steady_clock::now();
    std::vector<std::thread> ths;
    for (int th=0; th<kThreads; ++th) ths.emplace_back([&]{ auto a2=auth(ef); for (int i=0;i<kOps;++i){ auto r=ef.reserve(ts[0], V(16), a2); if (r.ok) ef.release(r.value.id, a2);} });
    for (auto& th: ths) if (th.joinable()) th.join();
    auto end=std::chrono::steady_clock::now(); double s=std::chrono::duration<double>(end-start).count();
    std::printf("concurrent reservation churn: %d threads x %d ops = %d ops in %.3f s = %.0f ops/s\n", kThreads, kOps, kThreads*kOps, s, kThreads*kOps/s); }

  { constexpr int N = 500; char path[256]; std::snprintf(path, sizeof(path), "%s/qf_bench.qf", "."); std::string err;
    auto start=std::chrono::steady_clock::now();
    for (int i=0;i<N;++i) if (!ef.save_to(path, &err).ok) { std::printf("save err %s\n", err.c_str()); break; }
    auto end=std::chrono::steady_clock::now(); double s=std::chrono::duration<double>(end-start).count();
    std::printf("persistence save: %d writes in %.3f s = %.0f writes/s\n", N, s, N/s);
    std::remove(path); }
  return 0;
}
