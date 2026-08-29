#pragma once
// ResourceVector: a multidimensional, dimension-typed amount vector.
//
// Storage is a fixed-size array indexed by ResourceDimension, so a byte amount
// can never be silently combined with a count amount across dimensions. Each
// dimension carries a "present" bit so an EXPLICIT zero limit (deny capacity)
// is distinguishable from an unset dimension (no constraint / no entitlement).
// All arithmetic is checked; additions/underflows that would overflow or go
// negative are rejected rather than silently wrapping.

#include "quota_fabric/core/resource_dimension.hpp"

#include <array>
#include <cstdint>
#include <optional>
#include <string>
#include <string_view>
#include <algorithm>
#include <vector>

namespace quota_fabric {

enum class ArithmeticStatus : std::uint8_t {
  Ok,
  Overflow,
  Underflow,
  Negative,
};

class ResourceVector {
 public:
  using Storage = std::array<std::int64_t, kResourceDimensionCount>;

  ResourceVector() = default;
  explicit ResourceVector(const Storage& s) : v_(s) { present_.fill(true); }

  static ResourceVector from_scalar(ResourceDimension d, std::int64_t amount) {
    ResourceVector r;
    r.set(d, amount);
    return r;
  }

  std::int64_t get(ResourceDimension d) const noexcept {
    return v_[static_cast<std::size_t>(d)];
  }
  bool present(ResourceDimension d) const noexcept { return present_[static_cast<std::size_t>(d)]; }
  bool has(ResourceDimension d) const noexcept { return present(d) && get(d) != 0; }

  // Set an amount; marks the dimension present. Rejects negative values.
  bool set(ResourceDimension d, std::int64_t amount) noexcept {
    if (amount < 0) return false;
    const auto i = static_cast<std::size_t>(d);
    v_[i] = amount; present_[i] = true;
    return true;
  }
  void unset(ResourceDimension d) noexcept {
    const auto i = static_cast<std::size_t>(d);
    v_[i] = 0; present_[i] = false;
  }
  void clear() noexcept { v_.fill(0); present_.fill(false); }
  bool is_empty() const noexcept {
    for (const auto p : present_) if (p) return false;
    return true;
  }
  std::size_t present_count() const noexcept {
    std::size_t c = 0; for (const auto p : present_) if (p) ++c; return c;
  }

  ArithmeticStatus add(const ResourceVector& o, bool saturate = false) noexcept {
    for (std::size_t i = 0; i < v_.size(); ++i) {
      const auto a = v_[i], b = o.v_[i];
      const bool pb = o.present_[i];
      if (!pb) continue;
      if (b > 0 && a > INT64_MAX - b) {
        if (saturate) { v_[i] = INT64_MAX; present_[i] = true; continue; }
        return ArithmeticStatus::Overflow;
      }
      v_[i] = a + b; present_[i] = true;
    }
    return ArithmeticStatus::Ok;
  }

  ArithmeticStatus sub(const ResourceVector& o, bool saturate = false) noexcept {
    for (std::size_t i = 0; i < v_.size(); ++i) {
      const auto a = v_[i], b = o.v_[i];
      const bool pa = present_[i], pb = o.present_[i];
      if (!pb || !pa) continue;
      if (a < b) {
        if (saturate) { set(static_cast<ResourceDimension>(i), 0); continue; }
        return ArithmeticStatus::Underflow;
      }
      v_[i] = a - b;
    }
    return ArithmeticStatus::Ok;
  }

  void scale(std::uint64_t factor) noexcept {
    for (std::size_t i = 0; i < v_.size(); ++i) {
      if (!present_[i]) continue;
      if (factor != 0 && v_[i] > 0 && static_cast<std::uint64_t>(v_[i]) > UINT64_MAX / factor) {
        v_[i] = INT64_MAX;
      } else {
        v_[i] = static_cast<std::int64_t>(static_cast<std::uint64_t>(v_[i]) * factor);
      }
    }
  }

  // a covers b: every dimension b constrains, a also constrains and >= b.
  bool covers(const ResourceVector& o) const noexcept {
    for (std::size_t i = 0; i < v_.size(); ++i)
      if (o.present_[i] && (!present_[i] || v_[i] < o.v_[i])) return false;
    return true;
  }

  static ResourceVector min(const ResourceVector& a, const ResourceVector& b) noexcept {
    ResourceVector r;
    for (std::size_t i = 0; i < kResourceDimensionCount; ++i) {
      if (!a.present_[i] && !b.present_[i]) continue;
      if (!a.present_[i]) { r.set(static_cast<ResourceDimension>(i), b.v_[i]); continue; }
      if (!b.present_[i]) { r.set(static_cast<ResourceDimension>(i), a.v_[i]); continue; }
      r.set(static_cast<ResourceDimension>(i), std::min(a.v_[i], b.v_[i]));
    }
    return r;
  }
  static ResourceVector max(const ResourceVector& a, const ResourceVector& b) noexcept {
    ResourceVector r;
    for (std::size_t i = 0; i < kResourceDimensionCount; ++i) {
      if (!a.present_[i] && !b.present_[i]) continue;
      if (!a.present_[i]) { r.set(static_cast<ResourceDimension>(i), b.v_[i]); continue; }
      if (!b.present_[i]) { r.set(static_cast<ResourceDimension>(i), a.v_[i]); continue; }
      r.set(static_cast<ResourceDimension>(i), std::max(a.v_[i], b.v_[i]));
    }
    return r;
  }

  // per-dimension sum across many vectors (used for subtree aggregation)
  static ResourceVector sum(const std::vector<ResourceVector>& items) {
    ResourceVector r;
    for (const auto& it : items) r.add(it);
    return r;
  }
  // element-wise difference clamped at zero (a - b), used for available capacity
  static ResourceVector diff_clamp(const ResourceVector& a, const ResourceVector& b) {
    ResourceVector r;
    for (std::size_t i = 0; i < kResourceDimensionCount; ++i) {
      if (!a.present_[i] && !b.present_[i]) continue;
      const auto av = a.present_[i] ? a.v_[i] : 0;
      const auto bv = b.present_[i] ? b.v_[i] : 0;
      const auto d = av - bv;
      r.set(static_cast<ResourceDimension>(i), d > 0 ? d : 0);
    }
    return r;
  }

  std::size_t dimension_count() const noexcept { return v_.size(); }

  friend bool operator==(const ResourceVector& a, const ResourceVector& b) noexcept {
    return a.v_ == b.v_ && a.present_ == b.present_;
  }
  friend bool operator!=(const ResourceVector& a, const ResourceVector& b) noexcept { return !(a == b); }

  std::string to_string() const;
  static std::optional<ResourceVector> parse(std::string_view text);

  const Storage& storage() const noexcept { return v_; }
  const std::array<bool, kResourceDimensionCount>& present_mask() const noexcept { return present_; }

 private:
  Storage v_{};
  std::array<bool, kResourceDimensionCount> present_{};
};

// A signed, element-wise vector used only for DERIVED quantities (e.g. "how much
// guaranteed capacity remains"). It may legitimately hold negative values to
// express deficit. It is never stored as a persistent absolute amount.
class SignedResourceVector {
 public:
  using Storage = std::array<std::int64_t, kResourceDimensionCount>;
  SignedResourceVector() = default;
  explicit SignedResourceVector(const Storage& s) : v_(s) {}

  static SignedResourceVector from_scalar(ResourceDimension d, std::int64_t a) {
    SignedResourceVector r; r.set(d, a); return r;
  }
  std::int64_t get(ResourceDimension d) const noexcept { return v_[static_cast<std::size_t>(d)]; }
  void set(ResourceDimension d, std::int64_t a) noexcept { v_[static_cast<std::size_t>(d)] = a; }
  bool has(ResourceDimension d) const noexcept { return v_[static_cast<std::size_t>(d)] != 0; }
  void clear() noexcept { v_.fill(0); }
  std::size_t dimension_count() const noexcept { return v_.size(); }

  static SignedResourceVector from(const ResourceVector& r) {
    SignedResourceVector s;
    for (std::size_t i = 0; i < s.v_.size(); ++i)
      s.v_[i] = r.present(static_cast<ResourceDimension>(i)) ? r.get(static_cast<ResourceDimension>(i)) : 0;
    return s;
  }

  ArithmeticStatus add(const SignedResourceVector& o, bool saturate = false) noexcept {
    for (std::size_t i = 0; i < v_.size(); ++i) {
      const auto a = v_[i], b = o.v_[i];
      if ((b > 0 && a > INT64_MAX - b) || (b < 0 && a < INT64_MIN - b)) {
        if (saturate) { v_[i] = b > 0 ? INT64_MAX : INT64_MIN; continue; }
        return ArithmeticStatus::Overflow;
      }
      v_[i] = a + b;
    }
    return ArithmeticStatus::Ok;
  }
  ArithmeticStatus sub(const SignedResourceVector& o, bool saturate = false) noexcept {
    for (std::size_t i = 0; i < v_.size(); ++i) {
      const auto a = v_[i], b = o.v_[i];
      if ((b < 0 && a > INT64_MAX + b) || (b > 0 && a < INT64_MIN + b)) {
        if (saturate) { v_[i] = b < 0 ? INT64_MAX : INT64_MIN; continue; }
        return ArithmeticStatus::Overflow;
      }
      v_[i] = a - b;
    }
    return ArithmeticStatus::Ok;
  }

  void clamp_non_negative() noexcept { for (auto& a : v_) if (a < 0) a = 0; }
  ResourceVector deficit() const noexcept {
    ResourceVector r;
    for (std::size_t i = 0; i < v_.size(); ++i)
      if (v_[i] < 0) r.set(static_cast<ResourceDimension>(i), -v_[i]);
    return r;
  }
  bool has_negative() const noexcept { for (const auto a : v_) if (a < 0) return true; return false; }
  bool is_zero() const noexcept { for (const auto a : v_) if (a != 0) return false; return true; }
  const Storage& storage() const noexcept { return v_; }
 private:
  Storage v_{};
};

}  // namespace quota_fabric
