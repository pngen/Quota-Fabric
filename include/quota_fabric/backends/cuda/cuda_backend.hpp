#pragma once
// Real CUDA quota-validation backend (RTX 5090, sm_120). This is not a toy: it
// performs real cudaMalloc/cudaMemcpy/kernel launch/synchronize and reports
// measured durations and byte counts so quota decisions consume real usage.
#include <cstdint>
#include <string>
#include <vector>
#include <optional>

namespace quota_fabric {

struct DeviceInfo {
  char name[256] = {};
  std::int64_t total_memory = 0;
  std::int64_t free_memory = 0;
  int major = 0, minor = 0;
  std::string display_name() const { return std::string(name) + " sm_" + std::to_string(major) + std::to_string(minor); }
};

struct CudaBackend {
  bool available = false;
  DeviceInfo device;
  std::string error;

  static CudaBackend discover(int device_index = 0);

  // Real allocation/free. Never approaches physical OOM.
  bool alloc(std::int64_t bytes, void** ptr);
  bool free_mem(void* ptr);
  bool h2d(void* dst, const void* src, std::int64_t bytes);
  bool d2h(void* dst, const void* src, std::int64_t bytes);

  // Execute a real kernel over n elements, returning measured wall duration (ms).
  bool run_kernel(std::int64_t n, double* elapsed_ms);

  // Combined H2D + kernel + D2H with measured transfer bytes and computed duration.
  bool run_workload(std::int64_t elements, std::int64_t* bytes_moved, double* elapsed_ms, std::int64_t* checksum);
};

}  // namespace quota_fabric
