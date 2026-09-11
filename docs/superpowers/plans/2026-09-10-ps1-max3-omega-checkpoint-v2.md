# PS1 MAX³ OMEGA Checkpoint v2 Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Replace the v1 MAX³ text dump with a deterministic, validated, provenance-complete `jojo-max3-checkpoint-v2` format that has a compact decision-oriented summary and a full forensic body without exposing proprietary payloads.

**Architecture:** Keep the public `format_ps1_max3_report()`/atomic-save API for callers, but delegate v2 formatting/parsing/validation to a focused checkpoint module. The format remains line-oriented and deterministic. Parser/validator is strict enough to detect truncation, inconsistent counts, duplicate IDs, invalid evidence promotion, and malformed typed decisions.

**Tech Stack:** C++20, STL string/stream/filesystem, existing MAX³ report model, CMake/CTest.

**Spec:** `docs/superpowers/specs/2026-09-10-ps1-max3-omega-deep-consolidation-design.md`

## Global Constraints

- Header must be exactly `format=jojo-max3-checkpoint-v2`.
- Preserve all v1 information semantically: options, termination, nodes, dependencies, best path, best report.
- Add profile/build/schema identity, strict/speculative counts, frontier graph, clusters, typed decisions, stats, health, and ranking.
- Serialization order is deterministic and must not depend on unordered-container iteration.
- Never serialize raw game sectors, guest RAM dumps, BIOS bytes, VRAM dumps, arbitrary executable bytes, or proprietary assets.
- Atomic save remains write-temp + replace.
- Size controls truncate only bounded diagnostic histories according to explicit report counters; they never silently drop structural frontier/node/provenance records.

---

### Task B1: Centralize v2 names and typed enum formatting

**Files:**
- Create: `src/core/ps1_max3_checkpoint_v2.h`
- Create: `src/core/ps1_max3_checkpoint_v2.cpp`
- Modify: `src/core/ps1_boot_report_io.h`
- Modify: `src/core/ps1_max3_report_io.cpp`
- Create: `tests/test_ps1_max3_checkpoint_v2.cpp`
- Modify: `CMakeLists.txt`

**Interfaces:**
- Produces:

```cpp
std::string ps1_max3_profile_name(Ps1Max3Profile profile) noexcept;
std::string ps1_max3_evidence_name(Ps1Max3EvidenceClass evidence) noexcept;
std::string ps1_max3_decision_kind_name(Ps1Max3DecisionKind kind) noexcept;
std::string ps1_max3_frontier_kind_name(Ps1Max3FrontierKind kind) noexcept;
std::string ps1_max3_expansion_stop_name(Ps1Max3ExpansionStop reason) noexcept;
std::string ps1_max3_subsystem_name(Ps1Max3Subsystem subsystem) noexcept;
std::string ps1_max3_candidate_source_name(Ps1Max3CandidateSource source) noexcept;
```

- [ ] **Step 1: Write RED enum-name coverage test**

Enumerate every enum value currently defined by Plan A and require stable lowercase snake-case names. Include `frontier_limit` termination.

- [ ] **Step 2: Run RED**

```bash
cmake -S . -B build -DCMAKE_BUILD_TYPE=Release
cmake --build build --parallel 2 --target jojo_ps1_max3_checkpoint_v2_tests
```

Expected: target/functions missing.

- [ ] **Step 3: Implement exhaustive switch functions**

Every known enum value has an explicit case; default returns `unknown` only for invalid casted values. Resolve existing missing `frontier_limit` switch warning.

- [ ] **Step 4: Run GREEN and commit**

```bash
ctest --test-dir build -R jojo_ps1_max3_checkpoint_v2_tests --output-on-failure
git add CMakeLists.txt src/core/ps1_max3_checkpoint_v2.* src/core/ps1_boot_report_io.h src/core/ps1_max3_report_io.cpp tests/test_ps1_max3_checkpoint_v2.cpp
git commit -m "feat: define MAX3 checkpoint v2 names"
```

### Task B2: Serialize v2 structural identity and options

**Files:**
- Modify: `src/core/ps1_max3_checkpoint_v2.h`
- Modify: `src/core/ps1_max3_checkpoint_v2.cpp`
- Modify: `src/core/ps1_max3_report_io.cpp`
- Test: `tests/test_ps1_max3_checkpoint_v2.cpp`

**Interfaces:**
- Produces:

```cpp
struct Ps1Max3CheckpointIdentity {
    std::string build_sha;
    std::string schema{"jojo-max3-checkpoint-v2"};
};

std::string format_ps1_max3_checkpoint_v2(
    const Ps1Max3Report& report,
    const Ps1Max3CheckpointIdentity& identity = {});
```

`format_ps1_max3_report()` delegates to v2 with empty/known build identity as configured by caller.

- [ ] **Step 1: Write RED header/options test**

Require lines for:

```text
format=jojo-max3-checkpoint-v2
profile=omega
deep_frontier_enabled=1
max_nodes=...
max_branch_depth=...
max_speculative_depth=...
max_unique_frontiers=...
max_total_retired=...
max_candidates_per_read=...
termination_reason=...
```

- [ ] **Step 2: Implement deterministic header/options section**

Use decimal for counts/budgets and canonical lowercase hex helpers for addresses/hashes.

- [ ] **Step 3: Add strict/speculative/global statistics**

At minimum serialize:

```text
strict_frontier_count=
speculative_frontier_count=
unique_frontier_count=
frontier_cluster_count=
node_count=
strict_node_count=
speculative_node_count=
deduplicated_node_count=
pruned_node_count=
total_retired=
```

- [ ] **Step 4: Run GREEN and commit**

```bash
ctest --test-dir build -R jojo_ps1_max3_checkpoint_v2_tests --output-on-failure
git add src/core/ps1_max3_checkpoint_v2.* src/core/ps1_max3_report_io.cpp tests/test_ps1_max3_checkpoint_v2.cpp
git commit -m "feat: serialize MAX3 checkpoint v2 identity"
```

### Task B3: Serialize frontier graph, clusters, nodes, and typed decisions

**Files:**
- Modify: `src/core/ps1_max3_checkpoint_v2.cpp`
- Test: `tests/test_ps1_max3_checkpoint_v2.cpp`
- Modify: `tests/test_ps1_boot_report_io.cpp`

**Interfaces:**
- Consumes: Plan A frontier/cluster/decision fields.
- Produces: deterministic structural body.

- [ ] **Step 1: Write RED frontier serialization test**

Require each frontier to include index, evidence, kind, subsystem, PC/opcode or `none`, BIOS table/selector or `none`, MMIO address/width/write/value or `none`, first node, occurrences, parent frontier, parent decision, expandable flag, stop reason and priority/cluster reference where applicable.

- [ ] **Step 2: Write RED decision/provenance test**

For BIOS decisions require table/selector/fallback. For MMIO read decisions require address/width/value/candidate source. For every speculative frontier require the assumption chain to be reconstructible from parent links or serialized chain IDs.

- [ ] **Step 3: Implement structural serialization in index order**

Never sort by incidental pointer/address. Frontiers and nodes serialize by stable index; clusters serialize by final ranked order with original cluster IDs preserved.

- [ ] **Step 4: Serialize node prune/expansion reasons**

Include evidence, speculative depth, decision, frontier, deduplicated/prune reason, path progress metrics and state hash.

- [ ] **Step 5: Run GREEN and update old report tests**

Old tests must assert semantic fields, not v1 header bytes.

```bash
ctest --test-dir build -R "jojo_ps1_(max3_checkpoint_v2|boot_report_io)_tests" --output-on-failure
```

- [ ] **Step 6: Commit**

```bash
git add src/core/ps1_max3_checkpoint_v2.cpp tests/test_ps1_max3_checkpoint_v2.cpp tests/test_ps1_boot_report_io.cpp
git commit -m "feat: serialize MAX3 frontier provenance"
```

### Task B4: Add compact summary, checkpoint health, and implementation queue

**Files:**
- Modify: `src/core/ps1_max3_checkpoint_v2.h`
- Modify: `src/core/ps1_max3_checkpoint_v2.cpp`
- Test: `tests/test_ps1_max3_checkpoint_v2.cpp`

**Interfaces:**
- Produces a summary before the forensic body with:

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
...
summary_end=1
```

- [ ] **Step 1: Write RED health classification tests**

A report stopped at a strict terminal write is `terminal_frontier`; a report truncated only by node/frontier/retired budget is `budget_limited`; a report with frame/VRAM landmark may be `progress_landmark` while still retaining actual termination details.

- [ ] **Step 2: Write RED priority queue tests**

Only strict or mixed clusters appear in the actionable queue by default; speculative-only discoveries are separately listed and never mislabeled actionable production fixes.

- [ ] **Step 3: Implement summary and health functions**

Use integer/categorical fields only. Do not infer hardware semantics from candidate values.

- [ ] **Step 4: Run GREEN and commit**

```bash
ctest --test-dir build -R jojo_ps1_max3_checkpoint_v2_tests --output-on-failure
git add src/core/ps1_max3_checkpoint_v2.* tests/test_ps1_max3_checkpoint_v2.cpp
git commit -m "feat: summarize MAX3 checkpoint health"
```

### Task B5: Add strict parser and validator

**Files:**
- Create: `src/core/ps1_max3_checkpoint_parser.h`
- Create: `src/core/ps1_max3_checkpoint_parser.cpp`
- Create: `tests/test_ps1_max3_checkpoint_parser.cpp`
- Modify: `CMakeLists.txt`

**Interfaces:**
- Produces:

```cpp
struct Ps1Max3CheckpointValidation {
    std::string format;
    Ps1Max3Profile profile{Ps1Max3Profile::strict};
    std::size_t node_count{};
    std::size_t frontier_count{};
    std::size_t cluster_count{};
    bool structurally_complete{};
};

Result<Ps1Max3CheckpointValidation> validate_ps1_max3_checkpoint_text(
    std::string_view text);
```

- [ ] **Step 1: Write RED valid-roundtrip test**

Format a synthetic report, validate it, and require counts/profile/schema to match.

- [ ] **Step 2: Write malformed-input RED table**

Reject at minimum: missing header, duplicate `frontier_N_index`, count mismatch, missing `best_report_end=1`, unknown profile, speculative node marked strict below speculative parent, MMIO decision missing value, truncated summary/forensic markers.

- [ ] **Step 3: Implement line-oriented parser with duplicate-key detection**

Use strict numeric parsing with range checks. Parser validates structure; it does not reconstruct proprietary runtime state.

- [ ] **Step 4: Run GREEN and commit**

```bash
ctest --test-dir build -R jojo_ps1_max3_checkpoint_parser_tests --output-on-failure
git add CMakeLists.txt src/core/ps1_max3_checkpoint_parser.* tests/test_ps1_max3_checkpoint_parser.cpp
git commit -m "feat: validate MAX3 checkpoint v2"
```

### Task B6: Make save deterministic, bounded, and self-validating

**Files:**
- Modify: `src/core/ps1_max3_report_io.cpp`
- Modify: `src/core/ps1_boot_report_io.h`
- Test: `tests/test_ps1_boot_report_io.cpp`
- Test: `tests/test_ps1_max3_checkpoint_parser.cpp`

**Interfaces:**
- `save_ps1_max3_report_atomic()` formats v2, validates the generated text before writing, then atomically replaces the target.

- [ ] **Step 1: Write RED self-validation test**

Expose a test seam or helper that proves invalid generated checkpoint text is never committed to the target path.

- [ ] **Step 2: Add explicit payload-size fields**

Serialize bounded-history capacities/counts and any omitted-history counters. Structural node/frontier/cluster/provenance records must not be silently dropped by this layer.

- [ ] **Step 3: Implement validate-before-replace**

Pseudo-flow:

```cpp
const auto text = format_ps1_max3_checkpoint_v2(report, identity);
auto valid = validate_ps1_max3_checkpoint_text(text);
if (!valid) return Result<void>::failure(valid.error, valid.detail);
write_temp(text);
return replace_file_max3(temp, target);
```

- [ ] **Step 4: Add deterministic byte-for-byte test**

Format the same synthetic report three times; strings must be exactly equal.

- [ ] **Step 5: Run Plan B focused and full suite**

```bash
cmake --build build --parallel 2
ctest --test-dir build -R "jojo_ps1_(max3_checkpoint|boot_report_io)_tests" --output-on-failure
ctest --test-dir build --output-on-failure
```

Expected: all PASS.

- [ ] **Step 6: Commit**

```bash
git add src/core/ps1_max3_report_io.cpp src/core/ps1_boot_report_io.h tests/test_ps1_boot_report_io.cpp tests/test_ps1_max3_checkpoint_parser.cpp
git commit -m "feat: self-validate MAX3 checkpoint v2"
```

- [ ] **Step 7: Require Windows/MSVC CI success on exact Plan B final SHA before Plan C**
