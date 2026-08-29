#pragma once
// Strongly-typed opaque identifiers for Quota Fabric.
//
// Every identity in the system is a distinct C++ type so that a TenantId can
// never be silently used where an AllocationId is required. A StrongId is an
// opaque 128-bit token with a deterministic, byte-stable serialization form.

#include <cstdint>
#include <cstddef>
#include <string>
#include <string_view>
#include <functional>
#include <random>
#include <ostream>

namespace quota_fabric {

namespace detail {

// Small, fast, seedable RNG (xoshiro256**) used for ID generation.
struct Rng {
  std::uint64_t s[4] = {};
  std::uint64_t next() noexcept {
    const std::uint64_t result = s[1] * 5;
    const std::uint64_t t = s[1] << 17;
    s[2] ^= s[0]; s[3] ^= s[1]; s[1] ^= s[2]; s[0] ^= s[3];
    s[2] ^= t; s[3] = (s[3] << 45) | (s[3] >> 19);
    return result;
  }
  static Rng system() noexcept {
    Rng r;
    std::random_device rd;
    for (int i = 0; i < 4; ++i) r.s[i] = (std::uint64_t(rd()) << 32) ^ rd();
    if ((r.s[0] | r.s[1] | r.s[2] | r.s[3]) == 0) r.s[0] = 0x9E3779B97F4A7C15ull;
    return r;
  }
};

inline std::uint64_t mix64(std::uint64_t x) noexcept {
  x += 0x9E3779B97F4A7C15ull;
  x = (x ^ (x >> 30)) * 0xBF58476D1CE4E5B9ull;
  x = (x ^ (x >> 27)) * 0x94D049BB133111EBull;
  return x ^ (x >> 31);
}

inline int hex_digit(char c) noexcept {
  if (c >= '0' && c <= '9') return c - '0';
  if (c >= 'a' && c <= 'f') return 10 + (c - 'a');
  if (c >= 'A' && c <= 'F') return 10 + (c - 'A');
  return -1;
}

}  // namespace detail

template <class Tag>
class StrongId {
 public:
  using TagType = Tag;
  StrongId() = default;
  explicit StrongId(std::uint64_t hi, std::uint64_t lo) noexcept : hi_(hi), lo_(lo) {}

  static StrongId make() noexcept {
    static detail::Rng rng = detail::Rng::system();
    static std::uint64_t counter = 0;
    const std::uint64_t a = rng.next() ^ detail::mix64(++counter);
    const std::uint64_t b = rng.next();
    return StrongId(a, b);
  }

  static StrongId from_hex(std::string_view hex) {
    if (hex.starts_with("0x") || hex.starts_with("0X")) hex.remove_prefix(2);
    if (hex.size() != 32) return StrongId{};
    std::uint64_t hi = 0, lo = 0;
    for (std::size_t i = 0; i < 32; ++i) {
      const int nib = detail::hex_digit(hex[i]);
      if (nib < 0) return StrongId{};
      if (i < 16) hi = (hi << 4) | std::uint64_t(nib);
      else        lo = (lo << 4) | std::uint64_t(nib);
    }
    return StrongId(hi, lo);
  }

  bool is_null() const noexcept { return hi_ == 0 && lo_ == 0; }
  explicit operator bool() const noexcept { return !is_null(); }
  std::uint64_t hi() const noexcept { return hi_; }
  std::uint64_t lo() const noexcept { return lo_; }

  std::string to_hex() const {
    std::string out;
    out.reserve(32);
    for (int j = 0; j < 32; ++j) {
      const std::uint64_t val = (j < 16) ? (hi_ >> (15 - j) * 4) : (lo_ >> (31 - j) * 4);
      out.push_back("0123456789abcdef"[val & 0xF]);
    }
    return out;
  }

  friend bool operator==(const StrongId& a, const StrongId& b) noexcept {
    return a.hi_ == b.hi_ && a.lo_ == b.lo_;
  }
  friend bool operator!=(const StrongId& a, const StrongId& b) noexcept { return !(a == b); }
  friend bool operator<(const StrongId& a, const StrongId& b) noexcept {
    return a.hi_ < b.hi_ || (a.hi_ == b.hi_ && a.lo_ < b.lo_);
  }
  friend std::ostream& operator<<(std::ostream& os, const StrongId& id) { return os << id.to_hex(); }

 private:
  std::uint64_t hi_ = 0;
  std::uint64_t lo_ = 0;
};

template <class Tag>
struct std::hash<quota_fabric::StrongId<Tag>> {
  std::size_t operator()(const quota_fabric::StrongId<Tag>& id) const noexcept {
    return static_cast<std::size_t>(
        quota_fabric::detail::mix64(id.hi() ^ (0x9E3779B97F4A7C15ull * id.lo())));
  }
};

}  // namespace quota_fabric
