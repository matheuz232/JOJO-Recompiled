# PS1 MAX³ OMEGA Checkpoint v2 Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans task-by-task. RED→GREEN is mandatory.

**Goal:** Deliver deterministic, self-validating `jojo-max3-checkpoint-v2` output with compact summary, full provenance/frontier graph, bounded per-frontier context/tails, compatibility fields, and strict parser validation.

**Spec:** `docs/superpowers/specs/2026-09-10-ps1-max3-omega-deep-consolidation-design.md`

## Fixed interfaces

Create in `src/core/ps1_max3_checkpoint_v2.h`:

```cpp
struct Ps1Max3CheckpointIdentity {
    std::string build_sha{"unknown"};
    std::string schema{"jojo-max3-checkpoint-v2"};
    std::string explorer_version{"omega-v1"};
};

std::string format_ps1_max3_checkpoint_v2(
    const Ps1Max3Report& report,
    const Ps1Max3CheckpointIdentity& identity = {});
```

Update the public save signature in `src/core/ps1_boot_report_io.h` exactly to:

```cpp
Result<void> save_ps1_max3_report_atomic(
    const std::filesystem::path& path,
    const Ps1Max3Report& report,
    const Ps1Max3CheckpointIdentity& identity = {});
```

`format_ps1_max3_report(const Ps1Max3Report&)` remains as compatibility API and delegates to v2 using default identity.

Never serialize sectors, RAM/VRAM dumps, BIOS/game bytes, arbitrary executable bytes, or proprietary assets.

---

### Task B1 — Centralize enum/name formatting

**Create:** `src/core/ps1_max3_checkpoint_v2.{h,cpp}`, `tests/test_ps1_max3_checkpoint_v2.cpp`; modify `src/core/ps1_boot_report_io.h`, `src/core/ps1_max3_report_io.cpp`, `CMakeLists.txt`.

- [ ] RED: exhaustive name tests for profile, evidence, decision, frontier, expansion/prune reason, subsystem, candidate source, termination/resource-stop reason.
- [ ] GREEN: explicit switch cases; invalid casts only -> `unknown`.
- [ ] Eliminate existing unhandled `frontier_limit` warning.
- [ ] Commit: `feat: define MAX3 checkpoint v2 names`.

### Task B2 — Identity, configuration fingerprint, health/stats header

Serialize exact header fields including:

```text
format=jojo-max3-checkpoint-v2
build_sha=...
explorer_version=omega-v1
profile=omega
configuration_fingerprint=...
max_nodes=...
max_branch_depth=...
max_speculative_depth=...
max_unique_frontiers=...
max_unique_states=...
max_queued_states=...
max_descendants_per_frontier=...
max_total_retired=...
max_candidates_per_read=...
max_serialized_diagnostic_bytes=...
termination_reason=...
```

Also counts: nodes strict/speculative/deduplicated/pruned; frontiers strict/speculative; clusters; states; candidates; cycles; queue high-water; max depths; landmark maxima; best strict progress and best overall progress.

- [ ] RED exact header/stat assertions.
- [ ] GREEN deterministic fingerprint built only from schema/profile/options, not pointer/time/host data.
- [ ] Canonical decimal counters + lowercase fixed-width hex for addresses/hashes.
- [ ] Commit: `feat: serialize MAX3 checkpoint v2 identity`.

### Task B3 — Frontier graph, clusters, nodes, typed decisions

- [ ] RED each frontier: stable index, evidence, kind, subsystem, PC/opcode, BIOS fields, MMIO fields, occurrence stats, first node, parent frontier/decision, expandable/terminal, stop/prune status, cluster ID, descendant count, progress summary.
- [ ] RED typed decisions: BIOS table/selector/fallback; MMIO address/width/value/candidate source; speculative depth/evidence after decision.
- [ ] RED provenance reconstruction: every speculative frontier traces back to the strict ancestor through parent decision references.
- [ ] GREEN serialize frontiers/nodes by stable IDs and clusters by ranked order while preserving cluster ID.
- [ ] Serialize both best strict path and best overall diagnostic path when they differ.
- [ ] Preserve v1 dependency/best-report semantics as additive compatibility fields.
- [ ] Commit: `feat: serialize MAX3 frontier provenance`.

### Task B4 — Bounded per-frontier context and event tails

Consumes `Ps1Max3FrontierContext` from Plan C. Structural records are never silently dropped. Per-frontier tails have exact caps:

- trace tail: 16 entries;
- BIOS tail: 8;
- MMIO tail: 8;
- CD-ROM command tail: 8.

- [ ] RED serialization includes scalar CPU/IRQ/CD/GPU/timer context and bounded tail counts.
- [ ] RED when source history exceeds cap: serialize latest N plus `*_omitted_count`.
- [ ] GREEN no raw memory/sector/VRAM payloads.
- [ ] If total formatted text would exceed `max_serialized_diagnostic_bytes`, stop optional node/event expansion first, preserve all frontier/cluster/provenance structural records, set deterministic truncation metadata, and never emit a structurally invalid file.
- [ ] Commit: `feat: bound MAX3 frontier forensic context`.

### Task B5 — Compact summary and ranked implementation queue

Summary must precede forensic body:

```text
summary_begin=1
checkpoint_health=terminal_frontier|budget_limited|progress_landmark|completed
strict_actionable_cluster_count=N
speculative_discovery_cluster_count=N
priority_0_cluster=...
priority_0_confidence=strict|mixed|speculative
priority_0_subsystem=...
priority_0_occurrences=...
priority_0_descendants=...
summary_end=1
```

- [ ] RED health classification for terminal vs budget-limited vs landmark.
- [ ] RED actionable queue never promotes speculative-only cluster to strict production evidence.
- [ ] RED highest strict/unlock-value clusters appear first deterministically.
- [ ] GREEN integer/categorical logic only.
- [ ] Commit: `feat: summarize MAX3 checkpoint health`.

### Task B6 — Strict parser/validator

**Create:** `src/core/ps1_max3_checkpoint_parser.{h,cpp}`, `tests/test_ps1_max3_checkpoint_parser.cpp`; modify `CMakeLists.txt`.

Interface:

```cpp
struct Ps1Max3CheckpointValidation {
    std::string format;
    Ps1Max3Profile profile{Ps1Max3Profile::strict};
    std::size_t node_count{};
    std::size_t frontier_count{};
    std::size_t cluster_count{};
    bool structurally_complete{};
};
Result<Ps1Max3CheckpointValidation> validate_ps1_max3_checkpoint_text(std::string_view text);
```

- [ ] RED valid round-trip.
- [ ] RED malformed table: missing header/fingerprint, duplicate IDs/keys, bad parent reference, count mismatch, unknown profile/evidence, strict child below speculative parent, MMIO decision missing value/source, missing summary/forensic/best-report terminator, invalid truncation marker.
- [ ] GREEN strict numeric range parsing + duplicate-key detection.
- [ ] Commit: `feat: validate MAX3 checkpoint v2`.

### Task B7 — Self-validating deterministic atomic save

Modify `src/core/ps1_max3_report_io.cpp`, tests.

Exact flow:

```cpp
const auto text = format_ps1_max3_checkpoint_v2(report, identity);
auto valid = validate_ps1_max3_checkpoint_text(text);
if (!valid) return Result<void>::failure(valid.error, valid.detail);
// existing temp-write + atomic replace
```

- [ ] RED invalid formatted text can never replace destination.
- [ ] RED same report+identity formatted three times is byte-identical.
- [ ] RED generated v2 contains all semantic v1 compatibility fields.
- [ ] GREEN validate-before-write/replace.
- [ ] Full Plan B tests + full Linux suite.
- [ ] Windows/MSVC exact-SHA CI required before Plan C.
- [ ] Commit: `feat: self-validate MAX3 checkpoint v2`.

## Plan B promotion gate

Round-trip validator passes, deterministic bytes repeat, provenance/summary/tails are complete, v1 semantics remain, structural output is never silently lost, no proprietary payload field exists, Linux + Windows/MSVC green on one SHA.
