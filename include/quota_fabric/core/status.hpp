#pragma once
#include "quota_fabric/core/enums.hpp"
#include "quota_fabric/core/enum_defs.hpp"

#include <string>
#include <optional>

namespace quota_fabric {

// Uniform status for all fallible governance operations. No C++ exceptions are
// used for flow control in the engine; errors are returned explicitly.
struct QuotaStatus {
  bool ok = true;
  ViolationCode code = ViolationCode::NONE;
  std::string message;

  static QuotaStatus success(std::string msg = {}) { return {true, ViolationCode::NONE, std::move(msg)}; }
  static QuotaStatus failure(ViolationCode c, std::string msg) { return {false, c, std::move(msg)}; }
  explicit operator bool() const noexcept { return ok; }
  std::string to_string() const {
    return ok ? ("OK" + (message.empty() ? std::string() : " (" + message + ")"))
              : std::string(quota_fabric::to_string(code)) + (message.empty() ? std::string() : ": " + message);
  }
};

template <class T>
struct QuotaResult {
  bool ok = false;
  T value{};
  QuotaStatus status;

  static QuotaResult success(T v) { return {true, std::move(v), QuotaStatus::success()}; }
  static QuotaResult failure(QuotaStatus s) { return {false, T{}, std::move(s)}; }
  explicit operator bool() const noexcept { return ok; }
  T& operator*() { return value; }
  const T& operator*() const { return value; }
  T* operator->() { return &value; }
  const T* operator->() const { return &value; }
};

}  // namespace quota_fabric
