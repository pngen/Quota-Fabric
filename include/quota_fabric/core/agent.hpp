#pragma once
#include "quota_fabric/core/identity.hpp"
#include "quota_fabric/core/enums.hpp"
#include "quota_fabric/core/resource_vector.hpp"

#include <string>

namespace quota_fabric {

// Registration/identity of a governing agent. The same logical agent that
// restarts must carry a NEW AgentBootId (old process authority dies).
struct AgentDescriptor {
  AgentId agent_id;
  AgentBootId boot_id;
  NodeId node_id;
  NodeBootGeneration node_boot_generation;
  std::uint32_t protocol_version = 1;
  ResourceVector inventory;        // physical/govened capacity offered
  ResourceSnapshotGeneration snapshot_generation;
  std::string backend;             // "cuda-120" / "cpu"
  std::string device_identity;     // e.g. "RTX-5090" / deviceUUID
  AgentState state = AgentState::REGISTERED;
  CoordinatorEpoch epoch;
  std::string host;
  std::uint16_t port = 0;
};

}  // namespace quota_fabric
