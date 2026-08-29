#include "quota_fabric/core/resource_dimension.hpp"

#include <unordered_map>

namespace quota_fabric {

std::string_view resource_dimension_name(ResourceDimension d) noexcept {
  switch (d) {
    case ResourceDimension::AcceleratorVRAM: return "accelerator_vram_bytes";
    case ResourceDimension::PageableHostRAM: return "pageable_host_ram_bytes";
    case ResourceDimension::PinnedHostRAM: return "pinned_host_ram_bytes";
    case ResourceDimension::KVCacheBytes: return "kv_cache_bytes";
    case ResourceDimension::TensorStateBytes: return "tensor_state_bytes";
    case ResourceDimension::PersistentCacheBytes: return "persistent_cache_bytes";
    case ResourceDimension::ModelResidencyBytes: return "model_residency_bytes";
    case ResourceDimension::AdapterResidencyBytes: return "adapter_residency_bytes";
    case ResourceDimension::AcceleratorComputeTime: return "accelerator_compute_ms";
    case ResourceDimension::AcceleratorExecutionSlots: return "accelerator_execution_slots";
    case ResourceDimension::ConcurrentSequences: return "concurrent_sequences";
    case ResourceDimension::ConcurrentRequests: return "concurrent_requests";
    case ResourceDimension::TransferBytes: return "transfer_bytes";
    case ResourceDimension::TransferBandwidth: return "transfer_bandwidth_bytes_per_sec";
    case ResourceDimension::StorageBandwidth: return "storage_bandwidth_bytes_per_sec";
    case ResourceDimension::PersistentStorageBytes: return "persistent_storage_bytes";
    case ResourceDimension::CompilationCacheBytes: return "compilation_cache_bytes";
    default: return "unknown_dimension";
  }
}

std::optional<ResourceDimension> parse_resource_dimension(std::string_view name) noexcept {
  static const std::unordered_map<std::string_view, ResourceDimension> map = {
    {"accelerator_vram_bytes", ResourceDimension::AcceleratorVRAM},
    {"pageable_host_ram_bytes", ResourceDimension::PageableHostRAM},
    {"pinned_host_ram_bytes", ResourceDimension::PinnedHostRAM},
    {"kv_cache_bytes", ResourceDimension::KVCacheBytes},
    {"tensor_state_bytes", ResourceDimension::TensorStateBytes},
    {"persistent_cache_bytes", ResourceDimension::PersistentCacheBytes},
    {"model_residency_bytes", ResourceDimension::ModelResidencyBytes},
    {"adapter_residency_bytes", ResourceDimension::AdapterResidencyBytes},
    {"accelerator_compute_ms", ResourceDimension::AcceleratorComputeTime},
    {"accelerator_execution_slots", ResourceDimension::AcceleratorExecutionSlots},
    {"concurrent_sequences", ResourceDimension::ConcurrentSequences},
    {"concurrent_requests", ResourceDimension::ConcurrentRequests},
    {"transfer_bytes", ResourceDimension::TransferBytes},
    {"transfer_bandwidth_bytes_per_sec", ResourceDimension::TransferBandwidth},
    {"storage_bandwidth_bytes_per_sec", ResourceDimension::StorageBandwidth},
    {"persistent_storage_bytes", ResourceDimension::PersistentStorageBytes},
    {"compilation_cache_bytes", ResourceDimension::CompilationCacheBytes},
  };
  const auto it = map.find(name);
  if (it == map.end()) return std::nullopt;
  return it->second;
}

ResourceUnit resource_dimension_unit(ResourceDimension d) noexcept {
  switch (d) {
    case ResourceDimension::AcceleratorComputeTime: return ResourceUnit::MILLISECONDS;
    case ResourceDimension::AcceleratorExecutionSlots: return ResourceUnit::SLOTS;
    case ResourceDimension::ConcurrentSequences:
    case ResourceDimension::ConcurrentRequests: return ResourceUnit::COUNT;
    case ResourceDimension::TransferBandwidth:
    case ResourceDimension::StorageBandwidth: return ResourceUnit::BYTES_PER_SECOND;
    default: return ResourceUnit::BYTES;
  }
}

std::string_view resource_unit_name(ResourceUnit u) noexcept {
  switch (u) {
    case ResourceUnit::COUNT: return "count";
    case ResourceUnit::BYTES: return "bytes";
    case ResourceUnit::MILLISECONDS: return "ms";
    case ResourceUnit::BYTES_PER_SECOND: return "bytes/sec";
    case ResourceUnit::SLOTS: return "slots";
    default: return "unit";
  }
}

const std::array<ResourceDimension, kResourceDimensionCount>& all_resource_dimensions() noexcept {
  static const std::array<ResourceDimension, kResourceDimensionCount> dims = {{
    ResourceDimension::AcceleratorVRAM, ResourceDimension::PageableHostRAM,
    ResourceDimension::PinnedHostRAM, ResourceDimension::KVCacheBytes,
    ResourceDimension::TensorStateBytes, ResourceDimension::PersistentCacheBytes,
    ResourceDimension::ModelResidencyBytes, ResourceDimension::AdapterResidencyBytes,
    ResourceDimension::AcceleratorComputeTime, ResourceDimension::AcceleratorExecutionSlots,
    ResourceDimension::ConcurrentSequences, ResourceDimension::ConcurrentRequests,
    ResourceDimension::TransferBytes, ResourceDimension::TransferBandwidth,
    ResourceDimension::StorageBandwidth, ResourceDimension::PersistentStorageBytes,
    ResourceDimension::CompilationCacheBytes,
  }};
  return dims;
}

}  // namespace quota_fabric
