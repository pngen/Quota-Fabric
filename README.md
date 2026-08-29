# Quota Fabric

Quota Fabric is an open-source, vendor-neutral runtime for governing multi-tenant accelerator quotas across VRAM, host memory, KV/tensor state, compute time, transfer bandwidth, persistent cache, model residency, and concurrent serving capacity.

It answers the core system question:

> **How should scarce accelerator, memory, state, bandwidth, residency, and execution capacity be divided, reserved, consumed, borrowed, reclaimed, and enforced across tenants without violating isolation, fairness, or existing obligations?**

Quota Fabric owns the persistent entitlement and consumption boundary for shared AI infrastructure.

## The boundary

Quota Fabric **defines and enforces resource entitlement**. It is deliberately not the admission controller, scheduler, resource broker, memory-pressure signal, or latency governor — but it supplies the authority they consume.

| System | Responsibility |
|---|---|
| **Admission Fabric** | decides whether new work may enter expensive accelerator infrastructure |
| **Inference Scheduler** | decides what admitted work runs next and where |
| **Resource Broker** | arbitrates concrete scarce resources among active workloads |
| **Memory Pressure** | reports when memory scarcity becomes operationally dangerous |
| **Latency Governor** | governs execution decisions against latency obligations |
| **Quota Fabric** | defines and enforces how much governed infrastructure each tenant is entitled to consume, reserve, borrow, burst into, and retain over time |

Quota Fabric is **not** a rate limiter, billing system, generic RBAC layer, admission controller, scheduler, resource broker, Kubernetes quota wrapper, dashboard, or cloud-accounting toy.

## Resource model

Quota Fabric governs multidimensional resources, each with an explicit unit. Combining incompatible dimensions is impossible at the type level.

- accelerator VRAM (bytes)
- pageable host RAM (bytes)
- pinned host RAM (bytes)
- KV cache / state (bytes)
- reusable tensor state (bytes)
- persistent cache (bytes)
- model residency (bytes)
- adapter residency (bytes)
- accelerator compute time (milliseconds, time-windowed)
- accelerator execution slots (count)
- concurrent sequences / requests (count)
- transfer bytes / bandwidth (bytes, bytes/sec, time-windowed)
- storage bandwidth (bytes/sec)
- persistent storage / compilation-artifact cache (bytes)

## Quota types

A tenant may hold a **guaranteed** baseline, a **soft** preferred ceiling, a **hard** absolute ceiling, a bounded **burst** allowance, **borrowable** headroom, reserved capacity, and current committed consumption. Every limit kind is itself multidimensional, so multidimensional quotas are the default rather than an extension.

## Envelope & decisions

`QuotaEnvelope` exposes guaranteed, soft, hard, burst limits, committed/reserved/borrowed/burst/lent/debt accounting, and signed *available* capacity (legitimately in deficit). A `QuotaDecision` carries a typed reason (`ALLOW_GUARANTEED`, `ALLOW_BURST`, `ALLOW_BORROW`, `DENY_HARD`, `DENY_SOFT`, `DENY_PARENT`, `DENY_PHYSICAL`, `RECALL_REQUIRED`, ...), the limiting dimension, and a structured `Explanation`.

## Hierarchy

Quota Fabric supports hierarchical quota trees (`organization → team → project → tenant → workload`) with inheritance, overrides, parent ceilings, child guarantees, shared residual pools, sibling borrowing, and deterministic aggregation. Groups (`organization`, `business_unit`, `team`, `project`, `environment`, `service_class`) are first-class quota domains; a workload may consume from tenant-local and parent/group quota without double-accounting.

## Guarantees, burst, borrow & lend

- **Guarantees** are protected. Unused guarantee may be lent, but recall must remain possible.
- **Burst** is bounded and time-windowed; it never exceeds hard/governed ceilings and is never "infinite temporary".
- **Borrowing** draws only from capacity policy marks borrowable; it never permanently destroys the lender's guarantee.
- **Lending** is bounded, revocable, generation-aware, and explainable. Quota Fabric emits typed `RecallAction`s (`RECALL_BORROWED_CAPACITY`, `REDUCE_BURST`, `REQUEST_RECLAIM`, ...) but never directly kills workloads.

## Attribution, overcommit, and physical vs logical capacity

Shared resources are attributed explicitly via `AttributionModel`s: `PHYSICAL_OWNER`, `PROPORTIONAL`, `EQUAL_SHARE`, `LOGICAL_FULL_CHARGE`, `SHARED_SYSTEM_POOL`. Quota is never allowed to fabricate physical capacity: total quota may exceed physical only under explicit `OvercommitMode` (`NONE`, `LOGICAL`, `PREDICTION_AWARE`) policy, and overcommitted quota is never exposed as physically available.

## Policy, live updates, and explainability

Policies are validated, serializable, versioned, inspectable, and atomically replaceable. Live quota changes return an explicit state (`COMPLIANT`, `OVER_SOFT`, `OVER_HARD`, `RECALL_REQUIRED`, `RECLAIM_REQUIRED`) with well-defined semantics for grandfathered allocations. Every important decision is explainable.

## Distributed authority

- `quota-fabric-coordinator` owns authoritative quota state over real framed TCP.
- `quota-fabric-agent` reports real/local usage and capability; every start yields a **new `AgentBootId`**.
- `quota-fabricctl` is the command-line client.

Framed binary TCP uses magic, protocol version, message type, payload length, correlation id, bounded frame size, network byte order, and full read/write loops; malformed/truncated/oversized/unsupported frames are rejected. Stale authority (old coordinator epoch, old quota/policy generation, old boot identity) is rejected deterministically.

## Persistence

State is persisted with an explicit, versioned binary encoding, checksum, and atomic `temp → flush → close → rename` replacement. Corruption, truncation, malformed counts, impossible enums, duplicate ids, inconsistent accounting, invalid hierarchies, and cycles are rejected; recovery never invents capacity or usage.

## CUDA backend

A real CUDA backend validates quota enforcement on actual accelerator resources (RTX 5090, compute capability 12.0, CUDA 13.1, `sm_120`): `cudaMemGetInfo`, `cudaMalloc`, H2D, kernel launch, synchronization, D2H, output verification, `cudaFree`. It is optional at compile time; a CPU-only implementation covers portability, CI, tests, and examples.

## Building

```
cmake -S . -B build -G Ninja -DCMAKE_BUILD_TYPE=Release
cmake --build build -j
ctest --test-dir build --output-on-failure
```

On Windows/MSVC, warning flags (`/W4 /WX`) are scoped to the CXX language only, so incompatible MSVC warning flags are never forwarded to `nvcc`. CUDA is enabled with `-DQUOTAFABRIC_ENABLE_CUDA=ON -DCMAKE_CUDA_COMPILER=<nvcc> -DCMAKE_CUDA_ARCHITECTURES=120`.

## Install & downstream consumption

```
cmake --install build --prefix <prefix>
```

```cmake
find_package(QuotaFabric CONFIG REQUIRED)
target_link_libraries(app PRIVATE QuotaFabric::core)
```

## Validation results (measured on this machine)

| Area | Result |
|---|---|
| CTest | 8/8 categories pass |
| Release build | 0 warnings under `/W4 /WX` |
| Debug build | 0 warnings under `/W4 /WX` |
| Property checks | 20,000 randomized operations, seed printed, invariants hold |
| Concurrency | 8-thread reservation storm passes; 553k ops/s churn |
| Hierarchy checks | parent-ceiling / guarantee-protection pass |
| Reservation checks | atomic, rollback, double-release rejection pass |
| Borrow/lend/recall | borrow, lend, recall, lender-guarantee protection pass |
| Persistence | round-trip, corruption/truncation rejection pass |
| Protocol | framing rejection of malformed/oversized/truncated frames pass |
| Multiprocess proof | real coordinator + agents, kill/restart, stale epoch/boot/gen rejection — 3× |
| CUDA proof | RTX 5090 sm_120, 31 GiB total, real allocation/execution |
| Compute-time quota proof | real kernel duration drives decision `DENY_HARD` after budget |
| Transfer quota proof | real H2D/D2H bytes drive decision `DENY_HARD` after budget |
| Examples | 15 runnable examples, all execute |
| Benchmarks | quota eval 3.6M ops/s; reserve+release 689k ops/s; snapshot 1.5k/s; persistence 1.1k writes/s |
| Downstream `find_package` | independent consumer configures, builds, runs |

## License

Apache-2.0. Copyright 2026 Summon Software Labs.
