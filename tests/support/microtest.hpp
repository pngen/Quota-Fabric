#pragma once
// Minimal, dependency-free test harness. Each TEST(name) registers a case; the
// shared main runs all cases and reports pass/fail counts. No timeouts, no
// fixtures beyond plain functions — tests terminate naturally.
#include <cstdio>
#include <string>
#include <vector>
#include <functional>
#include <sstream>

namespace microtest_internal {
struct Case { std::string name; std::function<void()> fn; };
inline std::vector<Case>& registry() { static std::vector<Case> r; return r; }
inline int& failures() { static int f = 0; return f; }
inline int& checks() { static int c = 0; return c; }
inline const char*& current_name() { static const char* n = ""; return n; }
inline void record_fail(const char* expr, const char* file, int line, const std::string& extra) {
  ++failures();
  std::printf("  [FAIL] %s:%d  %s %s\n", file, line, expr, extra.c_str());
}
inline std::string val_of(double v) { return std::to_string(v); }
template <class T> inline std::string val_of(const T& t) { std::ostringstream o; o << t; return o.str(); }
struct Registrar { Registrar(const char* n, std::function<void()> f) { registry().push_back({n, std::move(f)}); } };
inline int run_all() {
  int ran = 0, failed_cases = 0;
  for (auto& c : registry()) {
    current_name() = c.name.c_str();
    const int before = failures();
    ++checks();  // one per case
    try { c.fn(); }
    catch (const std::exception& e) { record_fail("uncaught exception", __FILE__, __LINE__, e.what()); }
    catch (...) { record_fail("uncaught unknown exception", __FILE__, __LINE__, ""); }
    ++ran;
    if (failures() > before) { ++failed_cases; std::printf("[FAIL] case: %s\n", c.name.c_str()); }
    else std::printf("[ ok ] %s\n", c.name.c_str());
  }
  std::printf("\n=== %d test(s), %d failure(s) ===\n", ran, failures());
  return failed_cases;
}
}  // namespace microtest_internal

#define TEST(name) \
  static void mt_case_##name(); \
  static ::microtest_internal::Registrar mt_reg_##name(#name, &mt_case_##name); \
  static void mt_case_##name()

#define CHECK(cond) do { if (!(cond)) ::microtest_internal::record_fail(#cond, __FILE__, __LINE__, ""); } while (0)
#define CHECK_MSG(cond, msg) do { if (!(cond)) ::microtest_internal::record_fail(#cond, __FILE__, __LINE__, (msg)); } while (0)
#define CHECK_EQ(a, b) do { const auto& _a = (a); const auto& _b = (b); if (!(_a == _b)) \
  ::microtest_internal::record_fail(#a " == " #b, __FILE__, __LINE__, \
    " [got " + ::microtest_internal::val_of(_a) + " vs " + ::microtest_internal::val_of(_b) + "]"); } while (0)
#define REQUIRE(cond) do { if (!(cond)) { ::microtest_internal::record_fail("REQUIRE " #cond, __FILE__, __LINE__, ""); return; } } while (0)
#define REQUIRE_MSG(cond, msg) do { if (!(cond)) { ::microtest_internal::record_fail("REQUIRE " #cond, __FILE__, __LINE__, (msg)); return; } } while (0)
#define QUOTAFABRIC_MICROTEST_RUN() ::microtest_internal::run_all()
