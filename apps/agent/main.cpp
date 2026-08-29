// quota-fabric-agent: registers with a NEW AgentBootId on every start (old
// process authority dies). Reports inventory and usage to the coordinator.
#include "quota_fabric/protocol/message.hpp"
#include "quota_fabric/core/enum_defs.hpp"

#include <iostream>
#include <sstream>
#include <vector>
#include <cstring>
#include <thread>
#include <chrono>

using namespace quota_fabric;

int main(int argc, char** argv) {
  std::uint16_t coord_port = 0;
  std::string coord_host = "127.0.0.1";
  std::string name = "agent";
  std::string backend = "cpu";
  std::string inventory = "";
  std::string fixed_agent_id;
  std::vector<std::pair<ResourceDimension, std::int64_t>> observe;
  for (int i = 1; i < argc; ++i) {
    std::string a = argv[i];
    if (a == "--port" && i + 1 < argc) coord_port = static_cast<std::uint16_t>(std::stoi(argv[++i]));
    else if (a == "--host" && i + 1 < argc) coord_host = argv[++i];
    else if (a == "--name" && i + 1 < argc) name = argv[++i];
    else if (a == "--backend" && i + 1 < argc) backend = argv[++i];
    else if (a == "--inventory" && i + 1 < argc) inventory = argv[++i];
    else if (a == "--agent-id" && i + 1 < argc) fixed_agent_id = argv[++i];
    else if (a == "--observe" && i + 1 < argc) {
      auto v = ResourceVector::parse(argv[++i]);
      if (v) for (const auto d : all_resource_dimensions()) if (v->present(d)) observe.emplace_back(d, v->get(d));
    }
  }
  if (coord_port == 0) { std::cerr << "usage: quota-fabric-agent --port N\n"; return 2; }
  auto sock = TcpSocket::connect({coord_host, coord_port}, nullptr);
  if (!sock) { std::cerr << "cannot connect to coordinator\n"; return 1; }
  std::string err;
  // HELLO with fresh boot identity
  WireRequest hello;
  hello.type = MessageType::HELLO;
  hello.agent_id = fixed_agent_id.empty() ? AgentId::make() : AgentId::from_hex(fixed_agent_id);
  hello.agent_boot = AgentBootId::make();   // new boot for this process (stable agent_id across restarts)
  hello.node_id = NodeId::make();
  hello.node_boot = NodeBootGeneration::initial();
  hello.name = name; hello.path = backend;
  hello.agent.agent_id = hello.agent_id; hello.agent.boot_id = hello.agent_boot;
  hello.agent.node_id = hello.node_id; hello.agent.node_boot_generation = hello.node_boot;
  hello.agent.protocol_version = 1; hello.agent.backend = backend;
  hello.agent.device_identity = backend.compare(0, 4, "cuda") == 0 ? "RTX-5090" : "cpu";
  if (!inventory.empty()) { auto v = ResourceVector::parse(inventory); if (v) hello.agent.inventory = *v; }
  if (!send_frame(*sock, MessageType::HELLO, 1, 0, encode_request(hello), &err)) { std::cerr << "hello send failed\n"; return 1; }
  auto reply = recv_frame(*sock, 0, &err);
  if (!reply) { std::cerr << "no welcome\n"; return 1; }
  auto welcome = decode_response(reply->payload);
  // Print identity so a harness can read the boot id (used for restart fencing).
  std::cout << "AGENT_ID=" << hello.agent_id.to_hex()
            << " BOOT_ID=" << hello.agent_boot.to_hex()
            << " EPOCH=" << welcome.epoch.value()
            << " QUOTA_GEN=" << welcome.quota_generation.value()
            << "\n" << std::flush;

  // Push any requested observations under current authority.
  Authority auth;
  auth.epoch = welcome.epoch; auth.quota_generation = welcome.quota_generation; auth.policy_generation = welcome.policy_generation;
  auth.agent = hello.agent_id; auth.boot = hello.agent_boot; auth.resource_generation = ResourceGeneration::initial();
  std::uint64_t corr = 100;
  for (const auto& [dim, amt] : observe) {
    WireRequest o; o.type = MessageType::OBSERVE; o.auth = auth;
    o.observation.tenant = TenantId{}; // filled by coordinator from registered agent? keep empty
    o.observation.dimension = dim; o.observation.amount = amt; o.observation.kind = ObservationKind::USAGE_DELTA;
    o.observation.resource_generation = ResourceGeneration::initial(); o.observation.agent = hello.agent_id; o.observation.boot = hello.agent_boot;
    o.observation.at = mono_now_nanos();
    if (!send_frame(*sock, MessageType::OBSERVE, corr++, 0, encode_request(o), &err)) break;
    auto r = recv_frame(*sock, 0, &err); if (r) decode_response(r->payload);
  }

  // Idle until killed.
  for (;;) std::this_thread::sleep_for(std::chrono::seconds(1));
  return 0;
}
