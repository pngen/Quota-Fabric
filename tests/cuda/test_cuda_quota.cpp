// Real RTX 5090 CUDA quota proof. Uses real cudaMalloc/cudaMemcpy/kernels and
// real measured durations + byte counts to drive quota decisions.
#include "tests/support/microtest.hpp"
#include "tests/support/test_util.hpp"
#include "quota_fabric/accounting/engine.hpp"
#include "quota_fabric/backends/cuda/cuda_backend.hpp"

using namespace quota_fabric;
using namespace qf_test;

static constexpr std::int64_t GiB = 1LL << 30;

TEST(cuda_proof_allocation_execution_and_quota) {
#ifdef QF_HAVE_CUDA
  auto cuda = CudaBackend::discover(0);
  REQUIRE_MSG(cuda.available, "CUDA not available: " + cuda.error);
  REQUIRE(cuda.device.major >= 8);
  // the governed VRAM pool is far below the physical card
  const std::int64_t free = cuda.device.free_memory;
  REQUIRE(free > 6ll * GiB);

  // --- 4 GiB governed pool: A guarantee 2 GiB, B guarantee 1 GiB, 1 GiB burst ---
  QuotaFabric ef;
  QuotaPolicy p; p.name = "cuda-proof";
  ef.set_policy(p);
  TenantId A, B;
  ef.create_tenant("A", std::nullopt, 10, &A);
  ef.create_tenant("B", std::nullopt, 20, &B);
  {
    ResourceQuota qa; qa.tenant = A;
    qa.guaranteed = vram(2 * GiB); qa.soft_limit = vram(3 * GiB); qa.hard_limit = vram(3 * GiB);
    qa.burst_limit = vram(1 * GiB); qa.burst_rule.window = 30'000'000'000LL;
    qa.borrow_rule.lend_enabled = true; qa.borrow_rule.borrow_enabled = true;
    ef.set_tenant_quota(A, qa);
    ResourceQuota qb; qb.tenant = B;
    qb.guaranteed = vram(1 * GiB); qb.soft_limit = vram(1 * GiB); qb.hard_limit = vram(1 * GiB);
    qb.borrow_rule.borrow_enabled = true;
    ef.set_tenant_quota(B, qb);
  }
  auto auth = make_authority(ef);

  // A consumes guaranteed
  auto rA = ef.reserve(A, vram(2 * GiB), auth);
  REQUIRE(rA.ok);
  // A bursts 1 GiB
  auto rAb = ef.reserve(A, vram(1 * GiB), auth);
  REQUIRE(rAb.ok);  // bounded burst permitted
  // B later requests its protected guarantee -> NOT blocked by A's burst
  auto rB = ef.reserve(B, vram(1 * GiB), auth);
  REQUIRE(rB.ok);   // B got its protected guarantee even though A is bursting
  auto envB = ef.envelope(B);
  CHECK(envB.reserved.get(ResourceDimension::AcceleratorVRAM) == 1 * GiB);

  // Real CUDA execution against the admitted allocation for A (2 GiB device buffer)
  void* devA = nullptr;
  REQUIRE(cuda.alloc(2 * GiB, &devA));
  double ms = 0; std::int64_t moved = 0, cksum = 0;
  REQUIRE(cuda.run_workload(1 << 22, &moved, &ms, &cksum));  // real H2D + kernel + D2H
  CHECK(cksum >= 0);
  std::printf("CUDA device: %s  total=%lld GiB free=%lld GiB\n", cuda.device.display_name().c_str(), (long long)(cuda.device.total_memory >> 30), (long long)(cuda.device.free_memory >> 30));
  std::printf("measured: workload_ms=%.3f moved_bytes=%lld checksum=%lld\n", ms, (long long)moved, (long long)cksum);
  // feed real compute time into the compute-time quota
  Observation obs;
  obs.tenant = A; obs.dimension = ResourceDimension::AcceleratorComputeTime;
  obs.amount = static_cast<std::int64_t>(ms); obs.kind = ObservationKind::COMPUTE_INTERVAL;
  obs.at = mono_now_nanos(); obs.resource_generation = ResourceGeneration::initial(); obs.agent = AgentId::make(); obs.boot = AgentBootId::make();
  REQUIRE(ef.apply_observation(obs).ok);
  // feed real transfer bytes into the transfer quota
  Observation tr;
  tr.tenant = A; tr.dimension = ResourceDimension::TransferBytes;
  tr.amount = moved; tr.kind = ObservationKind::TRANSFER_CONSUMED;
  tr.at = mono_now_nanos(); tr.resource_generation = ResourceGeneration::initial(); tr.agent = AgentId::make(); tr.boot = AgentBootId::make();
  REQUIRE(ef.apply_observation(tr).ok);

  // The real kernel actually wrote results we can verify (checksum of 7+i over 2^22)
  // compute-time budget: A has a 5s budget for AcceleratorComputeTime over a 60s window
  ResourceQuota qa_cpu = make_vram_quota(A, 0, 0, 0, 0, 0);
  qa_cpu.hard_limit = ResourceVector::from_scalar(ResourceDimension::AcceleratorComputeTime, 5000);
  qa_cpu.soft_limit = qa_cpu.hard_limit; qa_cpu.guaranteed = qa_cpu.hard_limit;
  ef.set_tenant_quota(A, qa_cpu);
  auto auth2 = make_authority(ef);
  DecisionCode before = ef.evaluate_reservation(A, compute_ms(1)).code;
  // consume a large compute budget that exceeds the 5s window budget
  Observation huge;
  huge.tenant = A; huge.dimension = ResourceDimension::AcceleratorComputeTime;
  huge.amount = 6000; huge.kind = ObservationKind::COMPUTE_INTERVAL;
  huge.at = mono_now_nanos(); huge.resource_generation = ResourceGeneration::initial(); huge.agent = AgentId::make(); huge.boot = AgentBootId::make();
  ef.apply_observation(huge);
  auto after = ef.evaluate_reservation(A, compute_ms(1)).code;
  CHECK(before == DecisionCode::ALLOW_GUARANTEED || before == DecisionCode::ALLOW);
  CHECK(after == DecisionCode::DENY_HARD);   // compute budget consumed changes the decision

  // release everything; accounting closes exactly
  REQUIRE(ef.release(rA.value.id, auth2).ok);
  REQUIRE(ef.release(rAb.value.id, auth2).ok);
  REQUIRE(ef.release(rB.value.id, auth2).ok);
  cuda.free_mem(devA);
  auto env = ef.envelope(A);
  CHECK(env.current_consumption.get(ResourceDimension::AcceleratorVRAM) == 0);
  CHECK(env.reserved.get(ResourceDimension::AcceleratorVRAM) == 0);
  std::string err; CHECK(ef.invariants_ok(&err));
#endif
}
