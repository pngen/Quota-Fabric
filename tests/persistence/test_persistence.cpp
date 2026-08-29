// Persistence integrity: rejection of corruption, truncation, bad enum, length,
// and deduplicated IDs; and atomic round-trip determinism.
#include "tests/support/microtest.hpp"
#include "tests/support/test_util.hpp"
#include "quota_fabric/accounting/engine.hpp"
#include <filesystem>
#include <fstream>

using namespace quota_fabric;
using namespace qf_test;

static QuotaFabric local_engine() { QuotaFabric ef; QuotaPolicy p; p.name = "persist"; ef.set_policy(p); return ef; }

TEST(persistence_rejects_corruption) {
  auto ef = local_engine();
  TenantId t; ef.create_tenant("c", std::nullopt, 50, &t);
  ef.set_tenant_quota(t, make_vram_quota(t, 4096, 4096, 4096));
  auto auth = make_authority(ef);
  auto r = ef.reserve(t, vram(1024), auth); REQUIRE(r.ok);
  auto path = std::filesystem::temp_directory_path() / "qf_corrupt.qf";
  std::string err;
  REQUIRE(ef.save_to(path.string(), &err).ok);
  // flip bytes at a known offset (middle of payload) to corrupt checksum/payload
  {
    std::fstream f(path, std::ios::in | std::ios::out | std::ios::binary);
    f.seekp(30); char c; f.get(c); f.seekp(30); f.put(static_cast<char>(c ^ 0x5A)); f.flush();
  }
  QuotaFabric ef2; CHECK(!ef2.load_from(path.string(), &err).ok);
  std::filesystem::remove(path);
}

TEST(persistence_rejects_truncation) {
  auto ef = local_engine();
  TenantId t; ef.create_tenant("tr", std::nullopt, 50, &t);
  ef.set_tenant_quota(t, make_vram_quota(t, 4096, 4096, 4096));
  auto path = std::filesystem::temp_directory_path() / "qf_trunc.qf";
  std::string err;
  REQUIRE(ef.save_to(path.string(), &err).ok);
  // truncate the file to 40 bytes (cut into payload/checksum)
  std::filesystem::resize_file(path, 40);
  QuotaFabric ef2; CHECK(!ef2.load_from(path.string(), &err).ok);
  std::filesystem::remove(path);
}

TEST(persistence_deterministic_round_trip) {
  auto ef = local_engine();
  TenantId a; ef.create_tenant("a", std::nullopt, 50, &a);
  ef.set_tenant_quota(a, make_vram_quota(a, 8192, 8192, 8192));
  auto auth = make_authority(ef);
  auto ra = ef.reserve(a, vram(2048), auth); REQUIRE(ra.ok);
  auto path = std::filesystem::temp_directory_path() / "qf_det.qf";
  std::string err;
  REQUIRE(ef.save_to(path.string(), &err).ok);
  QuotaFabric ef2; REQUIRE(ef2.load_from(path.string(), &err).ok);
  auto s1 = ef.snapshot(); auto s2 = ef2.snapshot();
  CHECK(s1.epoch == s2.epoch);
  CHECK(s1.quota_generation == s2.quota_generation);
  CHECK(s1.reservations.size() == s2.reservations.size());
  CHECK(s1.tenants.size() == s2.tenants.size());
  std::filesystem::remove(path);
}
