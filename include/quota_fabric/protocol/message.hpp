#pragma once
#include "quota_fabric/protocol/frame.hpp"
#include "quota_fabric/accounting/engine.hpp"
#include "quota_fabric/core/snapshot.hpp"

namespace quota_fabric {

// A serializable request envelope. Fields are used depending on MessageType.
struct WireRequest {
  MessageType type = MessageType::PING;
  Authority auth;
  TenantId tenant;
  TenantId tenant2;
  TenantGroupId group;
  TenantGroupKind kind = TenantGroupKind::TEAM;
  ReservationId reservation;
  AllocationId allocation;
  BorrowId borrow;
  AgentId agent_id;
  AgentBootId agent_boot;
  NodeId node_id;
  NodeBootGeneration node_boot;
  std::string name;
  std::string name2;
  std::string service_class;
  std::string path;
  std::uint32_t priority = 100;
  std::uint32_t recall_priority = 0;
  ResourceVector req;
  ResourceVector req2;
  ResourceQuota quota;
  AgentDescriptor agent;
  Observation observation;
  std::uint64_t amount = 0;
};

struct WireResponse {
  bool ok = false;
  ViolationCode code = ViolationCode::NONE;
  std::string message;
  QuotaEnvelope envelope;
  QuotaDecision decision;
  Reservation reservation;
  Allocation allocation;
  BorrowRecord borrow;
  LendingRecord lend;
  std::vector<RecallAction> recall;
  QuotaSnapshot snapshot;
  CoordinatorEpoch epoch;
  QuotaGeneration quota_generation;
  PolicyGeneration policy_generation;
  std::string text;
};

// Build a response wrapper from a status + optional payload.
WireResponse response_from(QuotaStatus st);

std::vector<std::uint8_t> encode_request(const WireRequest& r);
WireRequest decode_request(const std::vector<std::uint8_t>& p);
std::vector<std::uint8_t> encode_response(const WireResponse& r);
WireResponse decode_response(const std::vector<std::uint8_t>& p);

}  // namespace quota_fabric
