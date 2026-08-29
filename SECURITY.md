# Security

Quota Fabric governs entitlement to scarce infrastructure, so adversarial input is treated as a first-class concern. Reporting a vulnerability: please open a security advisory against the private fork in preference to a public issue.

## Threat surface and mitigations

- **Malicious quota requests.** Requests are validated for non-negative amounts, bounded dimensions, and presence; a request cannot go negative or overflow. Explicit zero-quota is distinguishable from an unset dimension.
- **Resource-exhaustion protection.** Tenants, groups, hierarchy depth, reservations, allocations, borrows, events, rolling-window samples, protocol frame sizes, and serialization counts are all bounded (`EngineSettings` / policy limits). Absurd input is rejected rather than accepted.
- **Hierarchy depth / cycle bounds.** Parent chains are bounded and cycle-detected; `invariants_ok` rejects any hierarchy cycle and verifies every subtree never exceeds its parent ceiling.
- **Protocol bounds.** Frames are bounded (`kMaxFrameSize`); malformed, truncated, oversized, unsupported-version, and trailing-invalid data are rejected. Full read/write loops never assume one `recv` equals one frame. A frame's declared payload length must match the available bytes and its checksum (for persistence blobs).
- **Persistence integrity.** Persisted blobs carry a magic, format version, explicit length, and CRC32 checksum; corruption, truncation, malformed counts, impossible enums, and duplicate ids are rejected. Atomic `temp → flush → close → rename` replacement means a crash mid-write never leaves a half-written authoritative state.
- **Stale authority.** State-mutating messages carry coordinator epoch, quota generation, policy generation, and (for agent-sourced messages) an `AgentBootId`. A restart produces a new boot identity; stale epoch / boot / generation / reservation authority is rejected deterministically. Replayed stale usage cannot mutate current quota state.
- **Quota escalation / double accounting.** Releasing a transaction prunes it; releasing twice, reserving for the wrong tenant, and double-decrementing are rejected. Accounting is recomputed from records, and `invariants_ok` cross-checks counters against records.
- **Integer overflow / negative values.** Resource arithmetic is checked/saturating; no counter silently overflows; negative-equivalent values and NaN/Inf never enter (quantities are integers; the only floating-point fields are policy weights and overcommit factors, which are validated).
- **Shared-attribution attacks.** Attribution is explicit (`PHYSICAL_OWNER`, `PROPORTIONAL`, `EQUAL_SHARE`, `LOGICAL_FULL_CHARGE`, `SHARED_SYSTEM_POOL`); invalid shared attribution is rejected. Shared state is never naively charged to every tenant.
- **No raw-pointer serialization.** Only canonical, explicit field encoding is persisted or sent; no raw ABI structs cross the boundary.
- **CUDA trust boundary.** The CUDA backend only performs device discovery, allocation, copies, kernel execution, and verification within a governed pool well below physical capacity. It never approaches physical OOM and validates every runtime status.

## Bounds reference

| Bound | Default |
|---|---|
| max tenants | 1,000,000 |
| max groups | 100,000 |
| max hierarchy depth | 32 |
| max reservations | 1,000,000 |
| max borrows | 100,000 |
| max event retention | 4,096 |
| max frame size | 16 MiB |
