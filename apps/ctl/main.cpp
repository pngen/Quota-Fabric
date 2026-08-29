// quota-fabricctl: command-line client speaking the framed protocol to a
// coordinator. Every command uses the real runtime API (over the wire).
#include "quota_fabric/protocol/message.hpp"
#include "quota_fabric/core/enum_defs.hpp"

#include <iostream>
#include <sstream>
#include <cstring>
#include <map>
#include <string>

using namespace quota_fabric;

static std::string host = "127.0.0.1";
static std::uint16_t port = 6100;
static bool json = false;
static Authority g_auth;

struct Rpc {
  TcpSocket sock;
  std::uint64_t corr = 1;
  bool conn = false;
  Rpc() { auto s = TcpSocket::connect({host, port}, nullptr); conn = s.has_value(); if (conn) sock = std::move(*s); }
  WireResponse call(const WireRequest& req) {
    WireResponse resp;
    if (!conn) { resp.ok = false; resp.message = "cannot connect to coordinator"; return resp; }
    std::string err;
    if (!send_frame(sock, req.type, corr++, 0, encode_request(req), &err)) { resp.ok = false; resp.message = "send: " + err; return resp; }
    auto f = recv_frame(sock, 0, &err);
    if (!f) { resp.ok = false; resp.message = "recv: " + err; return resp; }
    try { resp = decode_response(f->payload); } catch (const std::exception& e) { resp.ok = false; resp.message = e.what(); }
    if (resp.epoch.value() != 0) { g_auth.epoch = resp.epoch; g_auth.quota_generation = resp.quota_generation; g_auth.policy_generation = resp.policy_generation; g_auth.resource_generation = ResourceGeneration::initial(); }
    return resp;
  }
};

static std::string get(const std::map<std::string, std::string>& m, const std::string& k, const std::string& d = "") {
  auto it = m.find(k); return it == m.end() ? d : it->second;
}

static void print_resp(const WireResponse& r) {
  if (!r.ok) { std::cout << "ERROR " << to_string(r.code) << (r.message.empty() ? "" : ": " + r.message) << "\n"; return; }
  if (!r.text.empty()) std::cout << r.text << "\n";
  if (r.envelope.tenant) std::cout << r.envelope.to_string() << "\n";
  if (r.reservation.id) std::cout << "reservation=" << r.reservation.id << " status=" << to_string(r.reservation.status) << " resources=" << r.reservation.resources.to_string() << "\n";
  if (r.allocation.id) std::cout << "allocation=" << r.allocation.id << " active=" << (r.allocation.active ? "true" : "false") << " used=" << r.allocation.used.to_string() << "\n";
  if (r.borrow.id) std::cout << "borrow=" << r.borrow.id << " borrower=" << r.borrow.borrower << " lender=" << r.borrow.lender << " amount=" << r.borrow.amount.to_string() << "\n";
  if (r.lend.id) std::cout << "lend=" << r.lend.id << " lender=" << r.lend.lender << " borrower=" << r.lend.borrower << " amount=" << r.lend.amount.to_string() << "\n";
  if (r.decision.code != DecisionCode::ALLOW || !r.decision.requested.is_empty()) {
    std::cout << "decision=" << to_string(r.decision.code)
              << " requested=" << r.decision.requested.to_string()
              << " limit=" << (r.decision.limiting_dimension ? std::string(resource_dimension_name(*r.decision.limiting_dimension)) : "-")
              << "\n" << r.decision.explanation.human();
  }
  if (!r.recall.empty()) { std::cout << "recall:"; for (auto a : r.recall) std::cout << " " << to_string(a); std::cout << "\n"; }
}

int main(int argc, char** argv) {
  if (argc < 2) { std::cout << "usage: quota-fabricctl [--coordinator host:port] [--json] <command> [field=value ...]\n"; return 2; }
  int i = 1;
  std::map<std::string, std::string> m;
  std::string cmd;
  for (; i < argc; ++i) {
    std::string a = argv[i];
    if (a == "--coordinator" && i + 1 < argc) { std::string hp = argv[++i]; auto c = hp.find(':'); host = hp.substr(0, c); port = static_cast<std::uint16_t>(std::stoi(hp.substr(c + 1))); }
    else if (a == "--json") json = true;
    else if (cmd.empty() && a[0] != '-') cmd = a;
    else {
      auto eq = a.find('=');
      if (eq != std::string::npos) m[a.substr(0, eq)] = a.substr(eq + 1);
    }
  }
  if (cmd.empty()) { std::cout << "no command\n"; return 2; }

  WireRequest req;
  if (cmd == "create-tenant") { req.type = MessageType::CREATE_TENANT; req.name = get(m, "name", "tenant"); req.priority = static_cast<std::uint32_t>(std::stoul(get(m, "priority", "100"))); if (m.count("group")) req.group = TenantGroupId::from_hex(get(m, "group")); }
  else if (cmd == "create-group") { req.type = MessageType::CREATE_GROUP; req.name = get(m, "name", "group"); req.kind = parse_enum<TenantGroupKind>(get(m, "kind", "TEAM")).value_or(TenantGroupKind::TEAM); if (m.count("parent")) req.group = TenantGroupId::from_hex(get(m, "parent")); }
  else if (cmd == "set-quota") { req.type = MessageType::SET_TENANT_QUOTA; req.tenant = TenantId::from_hex(get(m, "tenant")); req.quota.tenant = req.tenant; req.quota.guaranteed = *ResourceVector::parse(get(m, "guaranteed", "accelerator_vram_bytes=0")); req.quota.soft_limit = *ResourceVector::parse(get(m, "soft", get(m, "guaranteed", "accelerator_vram_bytes=0"))); req.quota.hard_limit = *ResourceVector::parse(get(m, "hard", get(m, "guaranteed", "accelerator_vram_bytes=0"))); req.quota.burst_limit = *ResourceVector::parse(get(m, "burst", "accelerator_vram_bytes=0")); req.quota.borrowable = *ResourceVector::parse(get(m, "borrowable", "accelerator_vram_bytes=0")); req.quota.burst_rule.window = 30'000'000'000LL; }
  else if (cmd == "show-quota" || cmd == "show-tenant" || cmd == "show-usage") { req.type = MessageType::ENVELOPE; req.tenant = TenantId::from_hex(get(m, "tenant")); }
  else if (cmd == "reserve") { req.type = MessageType::RESERVE; req.tenant = TenantId::from_hex(get(m, "tenant")); req.req = *ResourceVector::parse(get(m, "amount")); }
  else if (cmd == "release") { req.type = MessageType::RELEASE; req.reservation = ReservationId::from_hex(get(m, "reservation")); }
  else if (cmd == "consume") { req.type = MessageType::START_ALLOCATION; req.reservation = ReservationId::from_hex(get(m, "reservation")); req.req = *ResourceVector::parse(get(m, "amount")); }
  else if (cmd == "borrow") { req.type = MessageType::BORROW; req.tenant = TenantId::from_hex(get(m, "tenant")); req.req = *ResourceVector::parse(get(m, "amount")); }
  else if (cmd == "lend") { req.type = MessageType::LEND; req.tenant = TenantId::from_hex(get(m, "lender")); req.tenant2 = TenantId::from_hex(get(m, "borrower")); req.req = *ResourceVector::parse(get(m, "amount")); }
  else if (cmd == "recall") { req.type = MessageType::RECALL_BORROW; req.borrow = BorrowId::from_hex(get(m, "borrow")); }
  else if (cmd == "explain") { req.type = MessageType::EVALUATE; req.tenant = TenantId::from_hex(get(m, "tenant")); req.req = *ResourceVector::parse(get(m, "amount")); }
  else if (cmd == "snapshot" || cmd == "show-agents" || cmd == "show-reservations" || cmd == "show-policy" || cmd == "show-tree" || cmd == "validate") { req.type = MessageType::SNAPSHOT; }
  else if (cmd == "advance-epoch") { req.type = MessageType::ADVANCE_EPOCH; }
  else if (cmd == "ping") { req.type = MessageType::PING; }
  else { std::cout << "unknown command: " << cmd << "\n"; return 2; }

  Rpc rpc;
  if (!rpc.conn) { std::cout << "ERROR cannot connect to " << host << ":" << port << "\n"; return 1; }
  WireRequest hello; hello.type = MessageType::PING;
  rpc.call(hello);
  req.auth.epoch = g_auth.epoch; req.auth.quota_generation = g_auth.quota_generation; req.auth.policy_generation = g_auth.policy_generation;
  req.auth.resource_generation = ResourceGeneration::initial();
  auto resp = rpc.call(req);
  if (json) { std::cout << "{ \"ok\":" << (resp.ok ? "true" : "false") << ", \"code\":\"" << to_string(resp.code) << "\", \"msg\":\"" << resp.message << "\" }\n"; return 0; }
  print_resp(resp);
  return resp.ok ? 0 : 1;
}
