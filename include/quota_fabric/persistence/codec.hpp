#pragma once
// Canonical, explicit, versioned binary serialization. Never persist raw ABI
// structs: every field is encoded explicitly in canonical (fixed) order with
// explicit lengths and checked decoding. Reads reject truncation, malformed
// counts, impossible enums, and trailing-invalid data.

#include "quota_fabric/core/ids.hpp"
#include "quota_fabric/core/resource_vector.hpp"
#include "quota_fabric/core/enum_defs.hpp"

#include <cstdint>
#include <cstring>
#include <string>
#include <vector>
#include <array>
#include <optional>
#include <exception>

namespace quota_fabric {

// 4-byte magic that prefixes every persisted blob / protocol payload header.
constexpr std::uint32_t kQuotaFabricMagic = 0x51FA0134u;

class BinaryWriter {
 public:
  void u8(int v) { std::uint8_t b = static_cast<std::uint8_t>(v); push(1, &b); }
  void u16(std::uint16_t v) { push(2, &v); }
  void u32(std::uint32_t v) { push(4, &v); }
  void u64(std::uint64_t v) { push(8, &v); }
  void i64(std::int64_t v) { push(8, &v); }
  void bytes(const void* p, std::size_t n) { const auto* b = static_cast<const unsigned char*>(p); buf_.insert(buf_.end(), b, b + n); }
  void string(std::string_view s) { u64(s.size()); bytes(s.data(), s.size()); }
  template <class Tag>
  void id(const StrongId<Tag>& id) { u64(id.hi()); u64(id.lo()); }
  void generation_u64(std::uint64_t v) { u64(v); }
  void raw_enum(std::uint8_t e) { u8(e); }

  template <class Enum>
  void Enum(Enum e) { u8(static_cast<std::uint8_t>(e)); }

  void vector(const ResourceVector& v) {
    u16(static_cast<std::uint16_t>(v.dimension_count()));
    for (std::size_t i = 0; i < v.dimension_count(); ++i) {
      const auto d = static_cast<ResourceDimension>(i);
      u8(v.present(d) ? 1 : 0);
      i64(v.present(d) ? v.get(d) : 0);
    }
  }
  void signed_vector(const SignedResourceVector& v) {
    u16(static_cast<std::uint16_t>(v.dimension_count()));
    for (std::size_t i = 0; i < v.dimension_count(); ++i)
      i64(v.get(static_cast<ResourceDimension>(i)));
  }

  std::vector<std::uint8_t> take() { return std::move(buf_); }
  const std::vector<std::uint8_t>& data() const { return buf_; }
  std::size_t size() const { return buf_.size(); }

 private:
  void push(std::size_t n, const void* p) {
    const auto* b = static_cast<const unsigned char*>(p);
    buf_.insert(buf_.end(), b, b + n);
  }
  std::vector<std::uint8_t> buf_;
};


class decode_error : public std::exception {
 public:
  explicit decode_error(std::string m) : msg_(std::move(m)) {}
  const char* what() const noexcept override { return msg_.c_str(); }
 private:
  std::string msg_;
};

class BinaryReader {
 public:
  BinaryReader(const std::uint8_t* p, std::size_t n) : p_(p), n_(n) {}
  explicit BinaryReader(const std::vector<std::uint8_t>& v) : p_(v.data()), n_(v.size()) {}

  std::uint8_t u8() { need(1); return p_[off_++]; }
  std::uint16_t u16() { need(2); std::uint16_t r = std::uint16_t(p_[off_]) | (std::uint16_t(p_[off_+1]) << 8); off_ += 2; return r; }
  std::uint32_t u32() { need(4); std::uint32_t r = 0; for (int i = 0; i < 4; ++i) r |= std::uint32_t(p_[off_+i]) << (8*i); off_ += 4; return r; }
  std::uint64_t u64() { need(8); std::uint64_t r = 0; for (int i = 0; i < 8; ++i) r |= std::uint64_t(p_[off_+i]) << (8*i); off_ += 8; return r; }
  std::int64_t i64() { return static_cast<std::int64_t>(u64()); }
  std::string string() {
    const auto len = u64();
    if (len > max_string_) throw decode_error("string length exceeds bound");
    need(len);
    std::string s(reinterpret_cast<const char*>(p_ + off_), len);
    off_ += len;
    return s;
  }
  void bytes(void* dst, std::size_t n) { need(n); std::memcpy(dst, p_ + off_, n); off_ += n; }
  std::uint8_t raw_enum() { return u8(); }

  template <class Enum>
  Enum Enum(std::uint16_t max_valid = 64) {
    const auto e = u8();
    if (e > max_valid) throw decode_error("enum value out of range");
    return static_cast<Enum>(e);
  }

  ResourceVector vector() {
    const auto count = u16();
    ResourceVector r;
    if (count > r.dimension_count()) throw decode_error("vector dimension count exceeds bound");
    for (std::size_t i = 0; i < count; ++i) {
      const auto d = static_cast<ResourceDimension>(i);
      const auto present = u8() != 0;
      const auto val = i64();
      if (val < 0) throw decode_error("negative vector amount");
      if (present) { if (!r.set(d, val)) throw decode_error("invalid vector amount"); }
    }
    return r;
  }
  SignedResourceVector signed_vector() {
    const auto count = u16();
    SignedResourceVector r;
    if (count > r.dimension_count()) throw decode_error("signed vector dimension count exceeds bound");
    for (std::size_t i = 0; i < count; ++i) r.set(static_cast<ResourceDimension>(i), i64());
    return r;
  }

  template <class Id>
  static Id read_id(BinaryReader& r) { const auto hi = r.u64(); const auto lo = r.u64(); return Id(hi, lo); }

  std::size_t remaining() const { return n_ - off_; }
  bool at_end() const { return off_ == n_; }
  std::size_t offset() const { return off_; }
  void require_end() { if (!at_end()) throw decode_error("trailing invalid data"); }

  static void memcpy(void* dst, const void* src, std::size_t n) { std::memcpy(dst, src, n); }

 private:
  void need(std::size_t n) const {
    if (n_ - off_ < n) throw decode_error("truncated data");
  }
  const std::uint8_t* p_;
  std::size_t n_;
  std::size_t off_ = 0;
  std::size_t max_string_ = 1UL << 24;
};

}  // namespace quota_fabric
