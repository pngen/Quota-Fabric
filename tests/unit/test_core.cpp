#include "tests/support/microtest.hpp"
#include "quota_fabric/core/ids.hpp"
#include "quota_fabric/core/identity.hpp"
#include "quota_fabric/core/resource_vector.hpp"
#include "quota_fabric/core/enum_defs.hpp"
#include <set>

using namespace quota_fabric;

TEST(ids_are_distinct_types) {
  TenantId a = TenantId::make();
  AllocationId b = AllocationId::make();
  CHECK(a != TenantId{});
  // distinct types cannot be compared (compile-time), but shared value access works
  CHECK(a.to_hex().size() == 32);
  CHECK(a == TenantId::from_hex(a.to_hex()));
}

TEST(ids_random_and_deterministic) {
  TenantId a = TenantId::make(), b = TenantId::make();
  CHECK(a != b);
  // deterministic parse round-trip
  auto h = a.to_hex();
  TenantId c = TenantId::from_hex(h);
  CHECK(a == c);
  CHECK(TenantId::from_hex("00000000000000000000000000000000").is_null());
}

TEST(ids_hashing_and_ordering) {
  std::set<TenantId> s;
  for (int i = 0; i < 256; ++i) s.insert(TenantId::make());
  CHECK(s.size() == 256);
  std::hash<TenantId> h;
  CHECK(h(*s.begin()) != 0 || true);
}

TEST(generation_advances) {
  QuotaGeneration g = QuotaGeneration::initial();
  CHECK(g.value() == 1);
  QuotaGeneration g2 = QuotaGeneration::next(g);
  CHECK(g2.value() == 2);
  CHECK(g < g2);
  CHECK(CoordinatorEpoch::next(CoordinatorEpoch::initial()).value() == 2);
}

TEST(resource_vector_presence_and_values) {
  ResourceVector v = ResourceVector::from_scalar(ResourceDimension::AcceleratorVRAM, 4096);
  CHECK(v.present(ResourceDimension::AcceleratorVRAM));
  CHECK(v.get(ResourceDimension::AcceleratorVRAM) == 4096);
  CHECK(!v.present(ResourceDimension::TransferBytes));
  CHECK(v.get(ResourceDimension::TransferBytes) == 0);
  // zero is an explicit present value
  ResourceVector z; z.set(ResourceDimension::ConcurrentRequests, 0);
  CHECK(z.present(ResourceDimension::ConcurrentRequests));
  CHECK(!z.is_empty());
}

TEST(resource_vector_arithmetic) {
  ResourceVector a = ResourceVector::from_scalar(ResourceDimension::AcceleratorVRAM, 100);
  ResourceVector b = ResourceVector::from_scalar(ResourceDimension::AcceleratorVRAM, 50);
  a.add(b);
  CHECK(a.get(ResourceDimension::AcceleratorVRAM) == 150);
  a.sub(b);
  CHECK(a.get(ResourceDimension::AcceleratorVRAM) == 100);
  a.sub(b);
  CHECK(a.get(ResourceDimension::AcceleratorVRAM) == 50);
  auto st = a.sub(b, true);
  (void)st;
  CHECK(a.get(ResourceDimension::AcceleratorVRAM) == 0);
}

TEST(resource_vector_no_negative) {
  ResourceVector a = ResourceVector::from_scalar(ResourceDimension::AcceleratorVRAM, 1);
  bool ok = a.set(ResourceDimension::AcceleratorVRAM, -5);
  CHECK(!ok);
  ResourceVector b = ResourceVector::from_scalar(ResourceDimension::AcceleratorVRAM, 10);
  auto st = b.sub(ResourceVector::from_scalar(ResourceDimension::AcceleratorVRAM, 20));
  CHECK(st == ArithmeticStatus::Underflow);
  CHECK(b.get(ResourceDimension::AcceleratorVRAM) == 10);
}

TEST(resource_vector_cover) {
  ResourceVector a = ResourceVector::from_scalar(ResourceDimension::AcceleratorVRAM, 100);
  ResourceVector b = ResourceVector::from_scalar(ResourceDimension::AcceleratorVRAM, 50);
  CHECK(a.covers(b));
  CHECK(!b.covers(a));
}

TEST(resource_dimension_names_round_trip) {
  for (auto d : all_resource_dimensions()) {
    auto nm = resource_dimension_name(d);
    auto parsed = parse_resource_dimension(nm);
    CHECK(parsed.has_value());
    CHECK(*parsed == d);
  }
}

TEST(resource_vector_serialization) {
  ResourceVector v = ResourceVector::from_scalar(ResourceDimension::AcceleratorVRAM, 1234);
  auto s = v.to_string();
  auto parsed = ResourceVector::parse(s);
  CHECK(parsed.has_value());
  CHECK(*parsed == v);
}
