#pragma once
// Resource dimensions and their explicit units. Quota Fabric keeps dimensions
// extensible and never hard-codes quota logic around bytes alone. Each level of
// a ResourceVector is typed to a dimension; combining incompatible dimensions
// is therefore impossible at the type/vector level, and each dimension carries
// its canonical unit.

#include <array>
#include <cstdint>
#include <string>
#include <string_view>
#include <optional>

namespace quota_fabric {

enum class ResourceUnit : std::uint16_t {
  COUNT,                 // dimensionless countable items
  BYTES,                 // byte quantities
  MILLISECONDS,          // durations (compute time)
  BYTES_PER_SECOND,      // transfer/storage bandwidth
  SLOTS,                 // execution/concurrency slots
};

enum class ResourceDimension : std::uint16_t {
  AcceleratorVRAM = 0,
  PageableHostRAM,
  PinnedHostRAM,
  KVCacheBytes,
  TensorStateBytes,
  PersistentCacheBytes,
  ModelResidencyBytes,
  AdapterResidencyBytes,
  AcceleratorComputeTime,
  AcceleratorExecutionSlots,
  ConcurrentSequences,
  ConcurrentRequests,
  TransferBytes,
  TransferBandwidth,
  StorageBandwidth,
  PersistentStorageBytes,
  CompilationCacheBytes,
  _Count,
};

inline constexpr std::size_t kResourceDimensionCount =
    static_cast<std::size_t>(ResourceDimension::_Count);

std::string_view resource_dimension_name(ResourceDimension d) noexcept;
std::optional<ResourceDimension> parse_resource_dimension(std::string_view name) noexcept;
ResourceUnit resource_dimension_unit(ResourceDimension d) noexcept;
std::string_view resource_unit_name(ResourceUnit u) noexcept;
const std::array<ResourceDimension, kResourceDimensionCount>& all_resource_dimensions() noexcept;

}  // namespace quota_fabric
