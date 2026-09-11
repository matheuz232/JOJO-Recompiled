# PS1 MAX³ OMEGA Explorer Core Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans task-by-task. Every behavior change is RED→GREEN→review→CI.

**Goal:** Replace BIOS-only recursive MAX³ expansion with a deterministic profile-aware explorer that safely crosses eligible MMIO reads, generates representative candidates, tracks coverage/landmarks, prunes duplicate/dominated/cyclic states, adapts deterministic budgets, clusters frontiers, and ranks implementation priorities.

**Spec:** `docs/superpowers/specs/2026-09-10-ps1-max3-omega-deep-consolidation-design.md`

## Fixed architecture decisions

- `src/core/ps1_max3_explorer.h` remains the public model owner. Public enums/types used by `Ps1Max3Decision`, `Ps1Max3Frontier`, and `Ps1Max3Report` are declared there to avoid circular includes.
- `Ps1Max3CandidateSource`, `Ps1Max3Subsystem`, `Ps1Max3PruneReason`, `Ps1Max3SearchStats`, and `Ps1Max3FrontierCluster` are public model types in `ps1_max3_explorer.h`.
- Candidate/coverage/search/budget modules may include `ps1_max3_explorer.h`; `ps1_max3_explorer.h` must not include those policy modules.
- Preserve the existing aggregate prefix/layout of public structs. Append fields only.
- Strict mode never branches through MMIO reads. Unknown writes, device commands, GPU/DMA side effects, CPU boundaries, and unsupported load forms remain terminal.
- No randomness, wall-clock, CPU speed, or host free-memory inputs.

## Public option additions

Append to `Ps1Max3Options`:

```cpp
Ps1Max3Profile profile{Ps1Max3Profile::strict};
std::size_t max_candidates_per_read{3u};
std::size_t max_unique_states{65536u};
std::size_t max_queued_states{16384u};
std::size_t max_descendants_per_frontier{512u};
std::size_t max_cycle_repeats{1u};
std::uint64_t max_serialized_diagnostic_bytes{64ull * 1024ull * 1024ull};
```

OMEGA initial values remain: nodes 16383, branch depth 32, speculative depth 24, frontiers 96, retired 3,000,000,000, candidates/read 8. Hard ceilings: nodes 262143, branch/spec depth 64, frontiers 1024, retired 10,000,000,000, candidates/read 16. State/queue/descendant/output ceilings are explicit constants in the budget policy and must be tested before use.

---

### Task A1 — Profiles and public model types

**Files:** `src/core/ps1_max3_explorer.h`, `src/core/ps1_max3_explorer.cpp`, `tests/test_ps1_max3_explorer.cpp`

Add:

```cpp
enum class Ps1Max3Profile : std::uint8_t { strict, deep, omega };
enum class Ps1Max3CandidateSource : std::uint8_t {
    baseline_zero, baseline_one, baseline_all_ones, sign_bit,
    strict_observed, zero_test_class, mask_class, threshold_class,
    coverage_retained,
};
enum class Ps1Max3Subsystem : std::uint8_t {
    bios, cpu, irq, cdrom, gpu, dma, timer, mmio, other,
};
enum class Ps1Max3PruneReason : std::uint8_t {
    none, exact_duplicate, dominated, exact_cycle, branch_depth,
    speculative_depth, frontier_budget, state_budget, queue_budget,
    descendant_budget, retired_budget,
};
```

Append `candidate_source` to `Ps1Max3Decision`; append `prune_reason`, assumption count, and coverage/landmark summary fields to `Ps1Max3NodeSummary`; append `Ps1Max3SearchStats` and `std::vector<Ps1Max3FrontierCluster>` to `Ps1Max3Report`.

- [ ] RED: profile/default tests compile-fail before API exists.
- [ ] GREEN: implement `ps1_max3_options(Ps1Max3Profile)` exact presets; compatibility wrapper returns `deep` until Plan D.
- [ ] Verify legacy aggregate initializers still compile.
- [ ] Commit: `feat: add explicit MAX3 OMEGA public model`.

### Task A2 — Deterministic candidate engine

**Create:** `src/core/ps1_max3_candidate_engine.{h,cpp}`, `tests/test_ps1_max3_candidate_engine.cpp`; modify `CMakeLists.txt`.

Candidate engine owns only:

```cpp
struct Ps1Max3ReadCandidate { std::uint32_t value{}; Ps1Max3CandidateSource source{}; };
struct Ps1Max3CandidateContext {
    Ps1Max3Profile profile{}; std::uint8_t width{}; std::uint32_t pc{};
    std::uint32_t load_opcode{}; std::vector<std::uint32_t> following_opcodes;
    std::vector<std::uint32_t> strict_observed_values; std::size_t max_candidates{};
};
std::vector<Ps1Max3ReadCandidate> generate_ps1_max3_read_candidates(const Ps1Max3CandidateContext&);
```

- [ ] RED baseline order: deep 8/16/32-bit -> `0,1,all-ones`; strict -> none; width-mask + stable dedup.
- [ ] GREEN baseline implementation.
- [ ] RED OMEGA local constraints: bounded 8-instruction window for `ANDI`, zero/nonzero branches, mask-bit classes, and `SLTIU` threshold classes.
- [ ] GREEN deterministic equivalence representatives; add sign-bit and strict-observed values; never use speculative observations as strict candidates.
- [ ] Cap at `max_candidates_per_read`; record source for every value.
- [ ] Commit: `feat: add deterministic MAX3 read candidates`.

### Task A3 — Coverage census and progress landmarks

**Create:** `src/core/ps1_max3_coverage.{h,cpp}`, `tests/test_ps1_max3_coverage.cpp`; modify `CMakeLists.txt`.

Track semantic sets/counters for unique state hashes, PCs, opcode families, BIOS pairs, MMIO tuples, frontier classes, CD register/command context, currently visible GP0/GP1 families, DMA channel/mode context already modeled, exception/IRQ classes, plus first/max landmarks: interrupt, CD command, DMA progress, GP0, GP1, VRAM write, presented frame.

- [ ] RED: new report items produce delta; repeated items produce zero delta.
- [ ] RED: first/max landmarks advance once and stay stable.
- [ ] GREEN: stable ordered/canonical key hashing; insertion order cannot change final coverage hash.
- [ ] Add `Ps1Max3CoverageDelta` and deterministic coverage fingerprint.
- [ ] Commit: `feat: track deterministic MAX3 coverage`.

### Task A4 — Search priority, dominance, and cycle policy

**Create:** `src/core/ps1_max3_search_policy.{h,cpp}`, `tests/test_ps1_max3_search_policy.cpp`; modify `CMakeLists.txt`.

Central comparator order: presented frames, VRAM, GP0, GP1, DMA, CD, interrupt/callback progress, new frontier/coverage, evidence quality, fewer assumptions, lower speculative depth, retired instructions, stable insertion sequence.

- [ ] RED comparator tests.
- [ ] RED dominance: same diagnostic state + no better progress + equal/greater assumption cost is dominated; different state never dominates.
- [ ] RED exact-cycle: same state hash reappearing on one ancestor chain stops expansion after `max_cycle_repeats` and records `exact_cycle`.
- [ ] GREEN implementation.
- [ ] Near-cycle pruning remains **disabled in OMEGA v1** unless Plan C proves an explicit canonical state key differing only in non-semantic report-history counters. Add a regression asserting semantically different device/CPU states never near-merge.
- [ ] Commit: `feat: add MAX3 search pruning policy`.

### Task A5 — Deterministic adaptive resource manager

**Create:** `src/core/ps1_max3_budget.{h,cpp}`, `tests/test_ps1_max3_budget.cpp`; modify `CMakeLists.txt`.

`Ps1Max3BudgetEnvelope` includes nodes, frontiers, states, queue, retired, branch depth, speculative depth, candidates/read, descendants/frontier, and serialized diagnostic byte budget. `Ps1Max3BudgetUsage` includes recent-new-information count and all consumed dimensions.

- [ ] RED: no new information => no expansion.
- [ ] RED: fixed threshold => exact integer growth; no dimension exceeds ceiling.
- [ ] RED: candidate/depth expansion never makes speculative depth exceed branch depth; branch ceiling remains >= speculative ceiling.
- [ ] RED: state/queue/descendant/output caps produce distinct deterministic budget status.
- [ ] GREEN integer-only policy. Never inspect host memory/time.
- [ ] Commit: `feat: add deterministic MAX3 budget expansion`.

### Task A6 — Queue-based deep/OMEGA MMIO expansion

**Modify:** `src/core/ps1_max3_explorer.{h,cpp}`, `tests/test_ps1_max3_explorer.cpp`.

Replace recursive BIOS-only traversal with explicit queued work items containing copied runtime, parent node/frontier, evidence, branch/spec depth, assumption chain, path metrics, dependencies, ancestor state hashes, and insertion sequence.

- [ ] RED chained-read fixture: strict = one terminal read; deep = downstream second read discovered as speculative.
- [ ] RED read→write fixture: write remains terminal/no children.
- [ ] RED BIOS→read and read→BIOS mixed provenance.
- [ ] RED copied runtime candidates stay isolated.
- [ ] GREEN: BIOS fallback stable order; MMIO candidates from engine; `apply_diagnostic_mmio_read_fallback()` only for eligible loads.
- [ ] Add exact-state dedup, dominance, exact-cycle ancestor check, path/global budgets, descendants/frontier cap, and stable priority queue.
- [ ] Never enqueue from unsupported writes/device/GPU/DMA/CPU boundaries.
- [ ] Distinct resource-stop reasons recorded; global termination only changes when otherwise-expandable work was truncated.
- [ ] Commit: `feat: explore deterministic MAX3 MMIO frontiers`.

### Task A7 — Frontier clustering and implementation priority

**Create:** `src/core/ps1_max3_frontier_priority.{h,cpp}`, `tests/test_ps1_max3_frontier_priority.cpp`; modify `CMakeLists.txt`.

`Ps1Max3FrontierCluster` is declared in explorer public model; priority module only declares/implements:

```cpp
std::vector<Ps1Max3FrontierCluster> cluster_and_rank_ps1_max3_frontiers(const Ps1Max3Report&);
```

- [ ] RED: same address/width/direction across PCs clusters but original frontiers remain separate; same BIOS selector clusters; read/write never cluster.
- [ ] RED: strict evidence outweighs speculative-only; frequency/callsites/descendant count/progress landmarks raise priority deterministically.
- [ ] GREEN: integer scoring only, stable tie order by cluster ID.
- [ ] Append ranked clusters to report.
- [ ] Commit: `feat: rank MAX3 frontier root causes`.

### Task A8 — Search stats, determinism, overflow/limit promotion suite

**Create:** `tests/test_ps1_max3_omega_determinism.cpp`; modify `tests/test_ps1_max3_explorer.cpp`, `CMakeLists.txt`.

`Ps1Max3SearchStats` must report at least states visited/deduplicated/dominated, cycles cut, candidates considered/expanded/pruned, queue high-water mark, max branch/spec depth, budget expansions, and progress landmark maxima.

- [ ] Same synthetic OMEGA fixture run 3 times -> identical nodes/frontiers/decisions/clusters/stats/best path/termination.
- [ ] Each cap separately: node, retired, frontier, state, queue, descendants/frontier, branch depth, speculative depth, candidate count, output-budget metadata.
- [ ] 64-bit overflow-edge tests use checked/saturating guard behavior rather than wrapping counters.
- [ ] Strict vs omega isolation: strict contains no MMIO fallback decisions.
- [ ] Synthetic milestone-hunter tests show ranking favors real counter progress but never creates GPU/DMA writes.
- [ ] Full focused + full Linux suite.
- [ ] Windows/MSVC exact-SHA CI required before Plan B.
- [ ] Commit: `test: lock MAX3 OMEGA explorer determinism`.

## Plan A promotion gate

Strict behavior preserved; chained MMIO reads work in deep/omega; candidate and queue ordering deterministic; exact cycles terminate; budgets fail closed; writes/device side effects never branch; clustering/ranking stable; Linux + Windows/MSVC green on one exact SHA.
