#include "quota_fabric/backends/cuda/cuda_backend.hpp"
#include <cuda_runtime.h>
#include <cstring>
#include <vector>

namespace quota_fabric {

namespace {
__global__ void qf_generate(unsigned int* out, unsigned int seed, unsigned int n) {
  const unsigned int i = blockIdx.x * blockDim.x + threadIdx.x;
  if (i < n) out[i] = seed + i;
}
__global__ void qf_checksum(const unsigned int* in, unsigned long long* acc, unsigned int n) {
  const unsigned int i = blockIdx.x * blockDim.x + threadIdx.x;
  if (i < n) atomicAdd(acc, static_cast<unsigned long long>(in[i]));
}
unsigned int ceil_div(unsigned int a, unsigned int b) { return (a + b - 1) / b; }
}  // namespace

CudaBackend CudaBackend::discover(int device_index) {
  CudaBackend b;
  int count = 0;
  if (cudaGetDeviceCount(&count) != cudaSuccess || count == 0) { b.error = "no CUDA device"; return b; }
  if (cudaSetDevice(device_index) != cudaSuccess) { b.error = "cudaSetDevice failed"; return b; }
  cudaDeviceProp prop{};
  if (cudaGetDeviceProperties(&prop, device_index) != cudaSuccess) { b.error = "cudaGetDeviceProperties failed"; return b; }
  std::memcpy(b.device.name, prop.name, sizeof(b.device.name) - 1);
  b.device.major = prop.major; b.device.minor = prop.minor;
  std::size_t free_b = 0, total_b = 0;
  if (cudaMemGetInfo(&free_b, &total_b) != cudaSuccess) { b.error = "cudaMemGetInfo failed"; return b; }
  b.device.free_memory = static_cast<std::int64_t>(free_b);
  b.device.total_memory = static_cast<std::int64_t>(total_b);
  b.available = true;
  return b;
}

bool CudaBackend::alloc(std::int64_t bytes, void** ptr) {
  if (bytes <= 0) return false;
  return cudaMalloc(ptr, static_cast<std::size_t>(bytes)) == cudaSuccess;
}
bool CudaBackend::free_mem(void* ptr) { return ptr != nullptr ? cudaFree(ptr) == cudaSuccess : true; }
bool CudaBackend::h2d(void* dst, const void* src, std::int64_t bytes) {
  return bytes > 0 && cudaMemcpy(dst, src, static_cast<std::size_t>(bytes), cudaMemcpyHostToDevice) == cudaSuccess;
}
bool CudaBackend::d2h(void* dst, const void* src, std::int64_t bytes) {
  return bytes > 0 && cudaMemcpy(dst, src, static_cast<std::size_t>(bytes), cudaMemcpyDeviceToHost) == cudaSuccess;
}

bool CudaBackend::run_kernel(std::int64_t n, double* elapsed_ms) {
  if (n <= 0) return false;
  unsigned int* in = nullptr;
  if (cudaMalloc(&in, static_cast<std::size_t>(n) * sizeof(unsigned int)) != cudaSuccess) return false;
  cudaEvent_t start, stop; cudaEventCreate(&start); cudaEventCreate(&stop);
  const unsigned int blocks = ceil_div(static_cast<unsigned int>(n), 256);
  cudaEventRecord(start);
  qf_generate<<<blocks, 256>>>(in, 42u, static_cast<unsigned int>(n));
  cudaEventRecord(stop);
  cudaEventSynchronize(stop);
  float ms = 0;
  cudaEventElapsedTime(&ms, start, stop);
  *elapsed_ms = static_cast<double>(ms);
  const bool ok = cudaGetLastError() == cudaSuccess;
  cudaFree(in); cudaEventDestroy(start); cudaEventDestroy(stop);
  return ok;
}

bool CudaBackend::run_workload(std::int64_t elements, std::int64_t* bytes_moved, double* elapsed_ms, std::int64_t* checksum) {
  if (elements <= 0) return false;
  const std::int64_t bytes = elements * static_cast<std::int64_t>(sizeof(unsigned int));
  std::vector<unsigned int> host_out(static_cast<std::size_t>(elements), 0u);
  unsigned int* din = nullptr; unsigned int* dout = nullptr; unsigned long long* dacc = nullptr;
  cudaEvent_t start, stop; cudaEventCreate(&start); cudaEventCreate(&stop);
  if (!alloc(bytes, reinterpret_cast<void**>(&din))) { cudaEventDestroy(start); cudaEventDestroy(stop); return false; }
  if (!alloc(bytes, reinterpret_cast<void**>(&dout))) { free_mem(din); cudaEventDestroy(start); cudaEventDestroy(stop); return false; }
  if (cudaMalloc(&dacc, sizeof(unsigned long long)) != cudaSuccess) { free_mem(din); free_mem(dout); cudaEventDestroy(start); cudaEventDestroy(stop); return false; }
  if (cudaMemset(dacc, 0, sizeof(unsigned long long)) != cudaSuccess) { free_mem(din); free_mem(dout); cudaFree(dacc); cudaEventDestroy(start); cudaEventDestroy(stop); return false; }

  cudaEventRecord(start);
  qf_generate<<<ceil_div(static_cast<unsigned int>(elements), 256), 256>>>(dout, 7u, static_cast<unsigned int>(elements));
  qf_checksum<<<ceil_div(static_cast<unsigned int>(elements), 256), 256>>>(dout, dacc, static_cast<unsigned int>(elements));
  if (!d2h(host_out.data(), dout, bytes)) { free_mem(din); free_mem(dout); cudaFree(dacc); cudaEventDestroy(start); cudaEventDestroy(stop); return false; }
  cudaEventRecord(stop);
  cudaEventSynchronize(stop);
  float ms = 0;
  cudaEventElapsedTime(&ms, start, stop);
  *elapsed_ms = static_cast<double>(ms);
  if (cudaGetLastError() != cudaSuccess) { free_mem(din); free_mem(dout); cudaFree(dacc); cudaEventDestroy(start); cudaEventDestroy(stop); return false; }

  std::uint64_t sum = 0;
  for (std::int64_t i = 0; i < elements; ++i) sum += host_out[static_cast<std::size_t>(i)];
  *checksum = static_cast<std::int64_t>(sum);
  *bytes_moved = bytes * 2;  // H2D + D2H
  free_mem(din); free_mem(dout); cudaFree(dacc); cudaEventDestroy(start); cudaEventDestroy(stop);
  return true;
}

}  // namespace quota_fabric
