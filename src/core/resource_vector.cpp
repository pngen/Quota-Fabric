#include "quota_fabric/core/resource_vector.hpp"
#include "quota_fabric/core/resource_dimension.hpp"

#include <sstream>
#include <vector>

namespace quota_fabric {

std::string ResourceVector::to_string() const {
  std::ostringstream os;
  bool first = true;
  for (const auto d : all_resource_dimensions()) {
    if (!present(d)) continue;
    if (!first) os << ",";
    first = false;
    os << resource_dimension_name(d) << "=" << get(d);
  }
  return os.str();
}

std::optional<ResourceVector> ResourceVector::parse(std::string_view text) {
  ResourceVector r;
  std::size_t pos = 0;
  if (text.empty()) return r;
  while (pos < text.size()) {
    const auto comma = text.find(',', pos);
    const auto tok = text.substr(pos, comma == std::string_view::npos ? text.size() - pos : comma - pos);
    const auto eq = tok.find('=');
    if (eq == std::string_view::npos) return std::nullopt;
    const auto name = tok.substr(0, eq);
    const auto valstr = tok.substr(eq + 1);
    auto d = parse_resource_dimension(name);
    if (!d) return std::nullopt;
    std::int64_t val = 0;
    try { val = std::stoll(std::string(valstr)); } catch (...) { return std::nullopt; }
    if (val < 0) return std::nullopt;
    if (!r.set(*d, val)) return std::nullopt;
    if (comma == std::string_view::npos) break;
    pos = comma + 1;
  }
  return r;
}

}  // namespace quota_fabric
