#include "quota_fabric/backends/cuda/cuda_backend.hpp"
#include <cstdio>
using namespace quota_fabric;
int main() {
  auto cuda = CudaBackend::discover(0);
  if (!cuda.available) { std::printf("CUDA unavailable: %s\n", cuda.error.data()); return 1; }
  std::printf("Device: %s total=%lld GiB free=%lld GiB\n", cuda.device.display_name().c_str(), (long long)(cuda.device.total_memory >> 30), (long long)(cuda.device.free_memory >> 30));
  void* ptr = nullptr; const std::int64_t bytes = 1ll << 30;
  if (!cuda.alloc(bytes, &ptr)) { std::printf("cudaMalloc failed\n"); return 1; }
  double ms = 0; std::int64_t moved = 0, cksum = 0;
  if (!cuda.run_workload(1 << 22, &moved, &ms, &cksum)) { std::printf("workload failed\n"); cuda.free_mem(ptr); return 1; }
  std::printf("real kernel: elapsed=%.3f ms, moved=%lld bytes, checksum=%lld\n", ms, (long long)moved, (long long)cksum);
  cuda.free_mem(ptr); return 0; }