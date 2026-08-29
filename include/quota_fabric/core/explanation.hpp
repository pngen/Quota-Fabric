#pragma once
#include "quota_fabric/core/enums.hpp"
#include "quota_fabric/core/enum_defs.hpp"

#include <string>
#include <vector>
#include <sstream>

namespace quota_fabric {

struct ReasonLine {
  ViolationCode code = ViolationCode::NONE;
  std::string detail;
};

struct Explanation {
  std::vector<ReasonLine> lines;

  void add(ViolationCode code, std::string detail) { lines.push_back({code, std::move(detail)}); }
  void add_none(std::string detail) { lines.push_back({ViolationCode::NONE, std::move(detail)}); }
  bool empty() const noexcept { return lines.empty(); }

  std::string human() const {
    std::ostringstream os;
    for (const auto& l : lines)
      os << "  * " << to_string(l.code) << (l.detail.empty() ? "" : ": " + l.detail) << "\n";
    return os.str();
  }
};

}  // namespace quota_fabric
