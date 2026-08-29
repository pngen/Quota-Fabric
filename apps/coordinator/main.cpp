// quota-fabric-coordinator: authoritative quota authority over framed TCP.
#include "quota_fabric/accounting/engine.hpp"
#include "quota_fabric/protocol/message.hpp"
#include "quota_fabric/core/enum_defs.hpp"

#include <atomic>
#include <thread>
#include <unordered_map>
#include <iostream>
#include <sstream>
#include <filesystem>

using namespace quota_fabric;

static std::atomic<bool> g_running{true};

static std::string snapshot_text(QuotaFabric& ef) {
  auto s = ef.snapshot();
  std::ostringstream os;
  os << "snapshot epoch=" << s.epoch << " quota_gen=" << s.quota_generation << " policy_gen=" << s.policy_generation << "\n";
  os << "  groups=" << s.groups.size() << " tenants=" << s.tenants.size() << "\n";
  os << "  quotas=" << s.quotas.size() << " envelopes=" << s.envelopes.size() << "\n";
  os << "  reservations=" << s.reservations.size() << " allocations=" << s.allocations.size() << "\n";
  os << "  borrows=" << s.borrow_records.size() << " lends=" << s.lending_records.size() << "\n";
  for (const auto& t : s.tenants)
    os << "  tenant " << t.id << " [" << t.name << "] group=" << (t.group ? t.group->to_hex() : "-") << " prio=" << t.priority << "\n";
  for (const auto& e : s.envelopes)
    os << "  " << e.tenant << " committed=" << e.committed_usage.to_string() << " compliance=" << to_string(e.compliance) << "\n";
  return os.str();
}

// Validate authority for a mutating/query request coming from the wire.
static QuotaStatus check_authority(QuotaFabric& ef, const WireRequest& req) {
  if (req.auth.epoch != ef.epoch()) return QuotaStatus::failure(ViolationCode::STALE_AUTHORITY, "stale coordinator epoch");
  if (req.auth.quota_generation != ef.quota_generation()) return QuotaStatus::failure(ViolationCode::STALE_QUOTA_GENERATION, "stale quota generation");
  if (req.auth.policy_generation != ef.current_policy().generation) return QuotaStatus::failure(ViolationCode::STALE_POLICY, "stale policy generation");
  return QuotaStatus::success();
}

static void handle_session(TcpSocket sock, QuotaFabric& ef, std::unordered_map<AgentId, AgentBootId>& agents, std::mutex& agents_mtx) {
  std::string err;
  for (;;) {
    auto f = recv_frame(sock, 0, &err);
    if (!f) break;
    WireRequest req;
    try { req = decode_request(f->payload); }
    catch (const std::exception& e) {
      WireResponse r = response_from(QuotaStatus::failure(ViolationCode::STALE_RESERVATION, std::string("malformed request: ") + e.what()));
      send_frame(sock, MessageType::RESULT_ERR, f->correlation, 0, encode_response(r), &err);
      continue;
    }
    WireResponse resp;
    resp.epoch = ef.epoch(); resp.quota_generation = ef.quota_generation(); resp.policy_generation = ef.current_policy().generation;

    switch (req.type) {
      case MessageType::HELLO: {
        // register agent; new boot identity always carries new authority
        std::lock_guard<std::mutex> lk(agents_mtx);
        agents[req.agent_id] = req.agent_boot;
        resp.ok = true; resp.text = "registered agent_id=" + req.agent_id.to_hex() + " boot=" + req.agent_boot.to_hex();
        break;
      }
      case MessageType::PING: {
        resp.ok = true; resp.text = "pong epoch=" + std::to_string(ef.epoch().value()) + " quota_gen=" + std::to_string(ef.quota_generation().value());
        break;
      }
      case MessageType::CREATE_GROUP: {
        auto st = check_authority(ef, req); if (!st) { resp = response_from(st); break; }
        resp.ok = ef.create_group(req.name, req.kind, req.group.is_null() ? std::nullopt : std::optional<TenantGroupId>(req.group)).ok;
        resp.text = "group created";
        break;
      }
      case MessageType::CREATE_TENANT: {
        auto st = check_authority(ef, req); if (!st) { resp = response_from(st); break; }
        TenantId tid;
        auto r = ef.create_tenant(req.name, req.group.is_null() ? std::nullopt : std::optional<TenantGroupId>(req.group), req.priority, &tid);
        resp.ok = r.ok; resp.message = r.message; resp.text = tid.to_hex();
        break;
      }
      case MessageType::SET_TENANT_QUOTA: {
        auto st = check_authority(ef, req); if (!st) { resp = response_from(st); break; }
        resp = response_from(ef.set_tenant_quota(req.tenant, req.quota));
        break;
      }
      case MessageType::SET_GROUP_QUOTA: {
        auto st = check_authority(ef, req); if (!st) { resp = response_from(st); break; }
        resp = response_from(ef.set_group_quota(req.group, req.quota));
        break;
      }
      case MessageType::EVALUATE: {
        auto st = check_authority(ef, req); if (!st) { resp = response_from(st); break; }
        resp.ok = true; resp.decision = ef.evaluate_reservation(req.tenant, req.req);
        break;
      }
      case MessageType::RESERVE: {
        auto st = check_authority(ef, req); if (!st) { resp = response_from(st); break; }
        auto r = ef.reserve(req.tenant, req.req, req.auth);
        resp = response_from(r.ok ? QuotaStatus::success() : QuotaStatus::failure(r.status.code, r.status.message));
        if (r.ok) resp.reservation = r.value;
        break;
      }
      case MessageType::COMMIT_RESERVATION: {
        auto st = check_authority(ef, req); if (!st) { resp = response_from(st); break; }
        resp = response_from(ef.commit_reservation(req.reservation, req.auth));
        break;
      }
      case MessageType::RELEASE: {
        auto st = check_authority(ef, req); if (!st) { resp = response_from(st); break; }
        resp = response_from(ef.release(req.reservation, req.auth));
        break;
      }
      case MessageType::START_ALLOCATION: {
        auto st = check_authority(ef, req); if (!st) { resp = response_from(st); break; }
        auto r = ef.start_allocation(req.reservation, req.req, req.auth);
        resp = response_from(r.ok ? QuotaStatus::success() : QuotaStatus::failure(r.status.code, r.status.message));
        if (r.ok) resp.allocation = r.value;
        break;
      }
      case MessageType::RELEASE_ALLOCATION: {
        auto st = check_authority(ef, req); if (!st) { resp = response_from(st); break; }
        resp = response_from(ef.release_allocation(req.allocation, req.auth));
        break;
      }
      case MessageType::BORROW: {
        auto st = check_authority(ef, req); if (!st) { resp = response_from(st); break; }
        auto r = ef.borrow(req.tenant, req.req, req.auth);
        resp = response_from(r.ok ? QuotaStatus::success() : QuotaStatus::failure(r.status.code, r.status.message));
        if (r.ok) resp.borrow = r.value;
        break;
      }
      case MessageType::LEND: {
        auto st = check_authority(ef, req); if (!st) { resp = response_from(st); break; }
        auto r = ef.lend(req.tenant, req.tenant2, req.req, req.auth);
        resp = response_from(r.ok ? QuotaStatus::success() : QuotaStatus::failure(r.status.code, r.status.message));
        if (r.ok) resp.lend = r.value;
        break;
      }
      case MessageType::RECALL_BORROW: {
        auto st = check_authority(ef, req); if (!st) { resp = response_from(st); break; }
        resp = response_from(ef.recall_borrow(req.borrow, req.auth));
        break;
      }
      case MessageType::RECALL_DECISION: {
        auto st = check_authority(ef, req); if (!st) { resp = response_from(st); break; }
        resp.ok = true; resp.recall = ef.recall_decision(req.tenant);
        break;
      }
      case MessageType::OBSERVE: {
        auto st = check_authority(ef, req); if (!st) { resp = response_from(st); break; }
        { std::lock_guard<std::mutex> lk(agents_mtx);
          auto it = agents.find(req.observation.agent);
          if (it == agents.end() || it->second != req.observation.boot) {
            resp = response_from(QuotaStatus::failure(ViolationCode::STALE_AUTHORITY, "stale agent boot identity"));
            break;
          } }
        resp = response_from(ef.apply_observation(req.observation));
        break;
      }
      case MessageType::ENVELOPE: {
        auto st = check_authority(ef, req); if (!st) { resp = response_from(st); break; }
        resp.ok = true; resp.envelope = ef.envelope(req.tenant);
        break;
      }
      case MessageType::SNAPSHOT: {
        auto st = check_authority(ef, req); if (!st) { resp = response_from(st); break; }
        resp.ok = true; resp.text = snapshot_text(ef);
        break;
      }
      case MessageType::ADVANCE_EPOCH: {
        resp = response_from(ef.advance_epoch());
        resp.epoch = ef.epoch();
        break;
      }
      default:
        resp = response_from(QuotaStatus::failure(ViolationCode::STALE_RESERVATION, "unsupported message type"));
        break;
    }
    // re-read updated authority
    resp.epoch = ef.epoch(); resp.quota_generation = ef.quota_generation(); resp.policy_generation = ef.current_policy().generation;
    send_frame(sock, resp.ok ? MessageType::RESULT_OK : MessageType::RESULT_ERR, f->correlation, 0, encode_response(resp), &err);
  }
}

int main(int argc, char** argv) {
  std::uint16_t port = 0;
  std::string state_path;
  for (int i = 1; i < argc; ++i) {
    std::string a = argv[i];
    if (a == "--port" && i + 1 < argc) port = static_cast<std::uint16_t>(std::stoi(argv[++i]));
    else if (a == "--state" && i + 1 < argc) state_path = argv[++i];
  }
  if (port == 0) { std::cerr << "usage: quota-fabric-coordinator --port N [--state path]\n"; return 2; }
  QuotaFabric ef;
  QuotaPolicy p; p.name = "quota-fabric-default"; p.description = "default coordinator policy";
  ef.set_policy(p);
  if (!state_path.empty() && std::filesystem::exists(state_path)) { std::string err; ef.load_from(state_path, &err); }
  auto l = TcpSocket::listen({ "127.0.0.1", port }, nullptr);
  if (!l) { std::cerr << "cannot listen on port " << port << "\n"; return 1; }
  std::cout << "coordinator listening on " << port << " epoch=" << ef.epoch().value() << "\n" << std::flush;
  std::unordered_map<AgentId, AgentBootId> agents;
  std::mutex agents_mtx;
  std::vector<std::thread> threads;
  while (g_running) {
    std::string err;
    auto conn = l->accept(&err);
    if (!conn) { std::this_thread::sleep_for(std::chrono::milliseconds(50)); continue; }
    threads.emplace_back(handle_session, std::move(*conn), std::ref(ef), std::ref(agents), std::ref(agents_mtx));
  }
  for (auto& t : threads) if (t.joinable()) t.join();
  return 0;
}
