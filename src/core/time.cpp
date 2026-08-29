#include "quota_fabric/core/time.hpp"
#include <ctime>
#include <sstream>
#include <iomanip>

namespace quota_fabric {

std::string wall_timestamp() {
  const auto now = std::chrono::system_clock::now();
  const auto t = std::chrono::system_clock::to_time_t(now);
  std::tm tm{};
#if defined(_WIN32)
  localtime_s(&tm, &t);
#else
  localtime_r(&t, &tm);
#endif
  std::ostringstream os;
  os << std::put_time(&tm, "%Y-%m-%dT%H:%M:%S");
  return os.str();
}

}  // namespace quota_fabric
