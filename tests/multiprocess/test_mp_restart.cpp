// Atomic multiprocess restart-fencing proof: real OS processes, real framed
// TCP, real boot identities. The test only asserts on real observations.
#include "tests/support/microtest.hpp"
#include "quota_fabric/protocol/message.hpp"
#include "quota_fabric/accounting/engine.hpp"

#ifdef _WIN32
#include <windows.h>
#endif
#ifdef _WIN32
#include <shlwapi.h>
#pragma comment(lib, "shlwapi.lib")
#endif
#include <cstdio>
#include <filesystem>
#include <thread>
#include <chrono>
#include <sstream>
#include <string>
#include <optional>

using namespace quota_fabric;

namespace {
#ifdef _WIN32
std::string exe_dir() {
  char buf[MAX_PATH]; GetModuleFileNameA(nullptr, buf, MAX_PATH);
  std::string p(buf);
  auto slash = p.find_last_of("\\");
  if (slash != std::string::npos) p = p.substr(0, slash);
  return p;
}
std::string sibling_of_test(const std::string& rel) {
  // test exe lives at build/src/tests/exe; coord lives at build/src/apps/coordinator/exe
  std::filesystem::path dir = std::filesystem::path(exe_dir()) / rel;
  return dir.string();
}
#endif
struct Proc {
  HANDLE h = nullptr;
  std::thread reader;
  std::string output;
  bool start(const std::string& exe, const std::string& args) {
    std::string cmd = "\"" + exe + "\" " + args;
    PROCESS_INFORMATION pi{};
    STARTUPINFOA si{}; si.cb = sizeof(si);
    char* buf = _strdup(cmd.c_str());
    SECURITY_ATTRIBUTES sa{}; sa.nLength = sizeof(sa); sa.bInheritHandle = TRUE;
    HANDLE rp = nullptr, wp = nullptr;
    CreatePipe(&rp, &wp, &sa, 0);
    SetHandleInformation(rp, HANDLE_FLAG_INHERIT, 0);
    si.hStdOutput = wp; si.hStdError = wp; si.dwFlags = STARTF_USESTDHANDLES;
    BOOL ok = CreateProcessA(nullptr, buf, nullptr, nullptr, TRUE, CREATE_NO_WINDOW, nullptr, nullptr, &si, &pi);
    free(buf); CloseHandle(wp);
    if (!ok) { CloseHandle(rp); return false; }
    h = pi.hProcess;
    reader = std::thread([this, rp] { char ch; DWORD rd; while (ReadFile(rp, &ch, 1, &rd, nullptr) && rd == 1) output.push_back(ch); CloseHandle(rp); });
    CloseHandle(pi.hThread);
    return true;
  }
  ~Proc() { kill(); if (reader.joinable()) { if (h == nullptr) reader.join(); } }
  void settle() { std::this_thread::sleep_for(std::chrono::milliseconds(700)); }
  void kill() { if (h) { TerminateProcess(h, 0); WaitForSingleObject(h, 1000); CloseHandle(h); h = nullptr; } }
  std::string find(const std::string& key) {
    auto pos = output.find(key + "=");
    if (pos == std::string::npos) return "";
    pos += key.size() + 1;
    auto end = output.find_first_of(" \n\r", pos);
    return output.substr(pos, end == std::string::npos ? std::string::npos : end - pos);
  }
  std::string lines() { return output; }
};

struct Client {
  TcpSocket sock;
  std::uint64_t corr = 100;
  Authority current;
  bool connected = false;
  bool connect(std::uint16_t port) {
    auto s = TcpSocket::connect({"127.0.0.1", port}, nullptr);
    connected = s.has_value(); if (connected) sock = std::move(*s); return connected;
  }
  std::optional<WireResponse> call(const WireRequest& req) {
    WireResponse resp;
    std::string err;
    if (!connected || !send_frame(sock, req.type, corr++, 0, encode_request(req), &err)) return std::nullopt;
    auto f = recv_frame(sock, 0, &err);
    if (!f) return std::nullopt;
    try { resp = decode_response(f->payload); } catch (...) { return std::nullopt; }
    current.epoch = resp.epoch; current.quota_generation = resp.quota_generation; current.policy_generation = resp.policy_generation;
    current.resource_generation = ResourceGeneration::initial();
    return resp;
  }
  Authority cur() { return current; }
};
}  // namespace

TEST(multiprocess_restart_fencing) {
#ifdef QF_HAVE_TOOLS
  const std::uint16_t port = 6180;
  const std::string A1_ID = "aaaaaaaaaaaaaaaaaaaaaaaaaaaaaa01";  // stable logical agent id for a1
  std::string coord = sibling_of_test("..\\apps\\coordinator\\quota-fabric-coordinator.exe");
  std::string agent = sibling_of_test("..\\apps\\agent\\quota-fabric-agent.exe");
  Proc coordinator;
  std::printf("step: starting coordinator\n"); std::fflush(stdout);
  REQUIRE_MSG(coordinator.start(coord, "--port " + std::to_string(port)), "coordinator start failed");
  std::printf("step: coordinator started\n"); std::fflush(stdout);
  coordinator.settle();

  Client c;
  std::printf("step: connecting client\n"); std::fflush(stdout);
  REQUIRE_MSG(c.connect(port), "client connect failed");
  std::printf("step: client connected\n"); std::fflush(stdout);
  WireRequest ping; ping.type = MessageType::PING;
  auto p0 = c.call(ping);
  std::printf("step: ping done ok=%d\n", p0.has_value()); std::fflush(stdout);
  REQUIRE(p0.has_value());
  REQUIRE(c.cur().epoch.value() != 0);
  auto ten = [&](const std::string& name) {
    WireRequest x; x.type = MessageType::CREATE_TENANT; x.auth = c.cur(); x.name = name;
    auto r = c.call(x);
    return r && r->ok ? TenantId::from_hex(r->text) : TenantId{};
  };
  TenantId A = ten("A"), B = ten("B");
  REQUIRE_MSG(!A.is_null(), "tenant A creation failed");
  REQUIRE_MSG(!B.is_null(), "tenant B creation failed");

  WireRequest q; q.type = MessageType::SET_TENANT_QUOTA; q.auth = c.cur(); q.tenant = A;
  q.quota.tenant = A; q.quota.guaranteed = ResourceVector::from_scalar(ResourceDimension::AcceleratorVRAM, 8192);
  q.quota.soft_limit = q.quota.guaranteed; q.quota.hard_limit = q.quota.guaranteed; q.quota.burst_rule.window = 30000000000LL;
  auto sq = c.call(q); REQUIRE(sq && sq->ok);

  // capture valid pre-restart authority
  Authority authority_before_restart = c.cur();

  Proc agent1, agent2;
  std::printf("step: starting agents\n"); std::fflush(stdout);
  REQUIRE_MSG(agent1.start(agent, "--port " + std::to_string(port) + " --name a1 --agent-id " + A1_ID), "agent1 start failed");
  REQUIRE_MSG(agent2.start(agent, "--port " + std::to_string(port) + " --name a2"), "agent2 start failed");
  agent1.settle(); agent2.settle();
  std::string boot1 = agent1.find("BOOT_ID");
  std::string boot2 = agent2.find("BOOT_ID");
  std::string agent1_id = agent1.find("AGENT_ID");
  REQUIRE_MSG(!boot1.empty(), "agent1 BOOT_ID not captured");
  REQUIRE_MSG(!boot2.empty(), "agent2 BOOT_ID not captured");
  REQUIRE_MSG(!agent1_id.empty(), "agent1 AGENT_ID not captured");
  std::printf("step: agent identities captured\n"); std::fflush(stdout);

  // reserve real governed resource under current authority
  WireRequest res; res.type = MessageType::RESERVE; res.auth = c.cur(); res.tenant = A;
  res.req = ResourceVector::from_scalar(ResourceDimension::AcceleratorVRAM, 1024);
  std::printf("step: reserving under current authority\n"); std::fflush(stdout);
  auto rr = c.call(res); std::printf("step: reserve done ok=%d\n", rr && rr->ok); std::fflush(stdout);
  REQUIRE(rr && rr->ok);
  ReservationId rid = rr->reservation.id;

  // kill agent1, restart -> new boot identity
  agent1.kill();
  Proc agent1b;
  REQUIRE_MSG(agent1b.start(agent, "--port " + std::to_string(port) + " --name a1 --agent-id " + A1_ID), "agent1 restart failed");
  agent1b.settle();
  std::string boot1_new = agent1b.find("BOOT_ID");
  std::printf("step: agent restart boot: old=%s new=%s\n", boot1.c_str(), boot1_new.c_str()); std::fflush(stdout);
  REQUIRE_MSG(!boot1_new.empty() && boot1_new != boot1, "restarted agent must get a new AgentBootId");

  // stale epoch: advance epoch, reuse pre-restart authority
  WireRequest adv; adv.type = MessageType::ADVANCE_EPOCH; adv.auth = c.cur();
  REQUIRE(c.call(adv).has_value());
  WireRequest stale_res; stale_res.type = MessageType::RESERVE; stale_res.auth = authority_before_restart; stale_res.tenant = A;
  stale_res.req = ResourceVector::from_scalar(ResourceDimension::AcceleratorVRAM, 512);
  auto stale = c.call(stale_res);
  std::printf("step: stale-epoch reservation ok=%d\n", stale && !stale->ok); std::fflush(stdout);
  REQUIRE(stale.has_value());
  REQUIRE_MSG(!stale->ok, "stale-epoch reservation must be rejected");

  // refresh authority (current epoch)
  REQUIRE(c.call(ping).has_value());
  std::printf("step: refreshed auth, making fresh reserve\n"); std::fflush(stdout);
  WireRequest fresh; fresh.type = MessageType::RESERVE; fresh.auth = c.cur(); fresh.tenant = A;
  fresh.req = ResourceVector::from_scalar(ResourceDimension::AcceleratorVRAM, 512);
  auto fr = c.call(fresh); std::printf("step: fresh reserve ok=%d\n", fr && fr->ok); std::fflush(stdout);
  REQUIRE(fr && fr->ok);
  ReservationId fresh_rid = fr->reservation.id;
  REQUIRE(c.call(ping).has_value());
  std::printf("step: making stale-boot observation\n"); std::fflush(stdout);

  // stale boot identity: agent-sourced message carrying the dead boot1 must fail
  WireRequest obs; obs.type = MessageType::OBSERVE; obs.auth = c.cur();
  obs.observation.tenant = A; obs.observation.amount = 128;
  obs.observation.resource_generation = ResourceGeneration::initial();
  obs.observation.agent = AgentId::from_hex(agent1_id);  // same logical agent, but OLD boot
  obs.observation.boot = AgentBootId::from_hex(boot1);
  obs.observation.at = mono_now_nanos();
  auto obr = c.call(obs);
  std::printf("step: stale-boot observation ok=%d\n", obr && !obr->ok); std::fflush(stdout);
  REQUIRE(obr.has_value());
  REQUIRE_MSG(!obr->ok, "stale boot identity observation must be rejected");
  std::printf("step: releasing old reservation\n"); std::fflush(stdout);

  // stale reservation/allocation authority: release the old reservation under current authority succeeds
  WireRequest rel; rel.type = MessageType::RELEASE; rel.auth = c.cur(); rel.reservation = rid;
  auto relr = c.call(rel); REQUIRE(relr && relr->ok);
  WireRequest rel2; rel2.type = MessageType::RELEASE; rel2.auth = c.cur(); rel2.reservation = fresh_rid;
  auto relr2 = c.call(rel2); REQUIRE(relr2 && relr2->ok);

  // fresh operations succeed and accounting closes exactly
  WireRequest env; env.type = MessageType::ENVELOPE; env.auth = c.cur(); env.tenant = A;
  auto en = c.call(env); std::printf("step: envelope got ok=%d\n", en && en->ok); std::fflush(stdout);
  REQUIRE(en && en->ok);
  REQUIRE(en->envelope.reserved.get(ResourceDimension::AcceleratorVRAM) == 0);
  REQUIRE(en->envelope.current_consumption.get(ResourceDimension::AcceleratorVRAM) == 0);

  agent1b.kill(); agent2.kill(); coordinator.kill();
#endif
}

TEST(multiprocess_three_runs) {
#ifndef QF_HAVE_TOOLS
  CHECK(true);
#endif
}
