#include "quota_fabric/core/envelope.hpp"
#include <sstream>

namespace quota_fabric {

std::string QuotaEnvelope::to_string() const {
  std::ostringstream os;
  os << "tenant=" << tenant << " class=" << resource_class << "\n";
  os << "  guaranteed: " << guaranteed.to_string() << "\n";
  os << "  soft:       " << soft_limit.to_string() << "\n";
  os << "  hard:       " << hard_limit.to_string() << "\n";
  os << "  burst:      " << burst_limit.to_string() << "\n";
  os << "  consumption:" << current_consumption.to_string() << "\n";
  os << "  reserved:   " << reserved.to_string() << "\n";
  os << "  committed:  " << committed_usage.to_string() << "\n";
  os << "  borrowed:   " << borrowed.to_string() << "\n";
  os << "  burst_use:  " << burst_usage.to_string() << "\n";
  os << "  lent:       " << lent.to_string() << "\n";
  os << "  debt:       " << debt.to_string() << "\n";
  os << "  avail_guaranteed: " << available_guaranteed.get(ResourceDimension::AcceleratorVRAM)
     << " (VRAM)  compliance=" << quota_fabric::to_string(compliance)
     << "  gen=" << generation << " policy=" << policy_generation << " epoch=" << epoch;
  return os.str();
}

}  // namespace quota_fabric
