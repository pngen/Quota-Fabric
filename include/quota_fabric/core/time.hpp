#pragma once
// Monotonic time for enforcement windows and wall-clock time only for operator
// facing timestamps. Quota Fabric never uses wall-clock to drive enforcement.

#include <chrono>
#include <cstdint>
#include <string>

namespace quota_fabric {

using Nanos = std::int64_t;   // signed so deltas can be negative safely
using Micros = std::int64_t;
using Millis = std::int64_t;

inline Nanos mono_now_nanos() noexcept {
  return std::chrono::duration_cast<std::chrono::nanoseconds>(
      std::chrono::steady_clock::now().time_since_epoch()).count();
}

inline Millis mono_now_millis() noexcept { return mono_now_nanos() / 1000000; }

// Operator-facing wall-clock timestamp (not used for enforcement).
std::string wall_timestamp();

}  // namespace quota_fabric
