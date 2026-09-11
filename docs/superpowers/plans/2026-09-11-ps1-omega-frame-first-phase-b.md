# PS1 OMEGA Frame-First Phase B Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Make OMEGA strict-first for ranking and commercial authority while preserving speculative exploration and adding no new PS1 hardware semantics.

**Architecture:** `Ps1Max3SearchScore` becomes lexicographically strict-first, and the same evidence rule governs state dominance, best-node selection, and frontier-cluster ranking. `best_node`, `best_path`, and `best_report` remain the single authoritative result and must always refer to a strict node; speculative descendants stay in `nodes`/`frontiers` for diagnosis. The existing local-evidence compatibility API remains unchanged and becomes strict-safe because it already returns `best_report`.

**Tech Stack:** C++20, CMake/CTest, GitHub Actions Linux + Windows x64/MSVC 2022.

**Spec:** `docs/superpowers/specs/2026-09-11-ps1-omega-frame-first-phase-b-design.md`

## Global Constraints

- Baseline is `40549e6988ef90a351970cd9166a4c58dc14a62c`.
- Execute from a feature branch created from the approved design branch so the spec and this plan travel with the implementation.
- `strict` evidence unconditionally outranks `speculative` evidence before all progress counters.
- Within the same evidence class, preserve this order: presented frames, VRAM writes, GP0, GP1, DMA, CD-ROM, interrupt/callback progress, new frontiers, new coverage, fewer assumptions, smaller speculative depth, greater retired count, earlier insertion sequence.
- A speculative state must never dominate a strict state with the same diagnostic hash.
- `best_node`, `best_path`, and `best_report` are strict-authoritative; do not add a parallel strict-best report model.
- Speculative descendants must continue to be generated and explored within existing budgets.
- Frontier clusters containing strict evidence outrank purely speculative clusters before unlock/progress score.
- Do not change MAX3 budgets or candidate generation.
- Do not add GPU, DMA, IRQ, timer, CD-ROM, GTE, BIOS, SIO, SPU, CPU, or memory semantics.
- Preserve deterministic ordering and serialization.
- Phase B completion requires green Linux CI, green Windows x64/MSVC CI, and artifact `JOJO-Recompiled-Windows-x64` from the exact Phase B HEAD.

---

### Task 1: Make search score ordering and dominance strict-first

**Files:**
- Modify: `tests/test_ps1_max3_search_policy.cpp`
- Modify: `src/core/ps1_max3_search_policy.cpp`
- Preserve: `src/core/ps1_max3_search_policy.h`

**Interfaces:**
- Consumes: `Ps1Max3SearchScore`, `Ps1Max3EvidenceClass`.
- Produces unchanged signatures:

```cpp
[[nodiscard]] bool ps1_max3_search_outranks(
    const Ps1Max3SearchScore& candidate,
    const Ps1Max3SearchScore& current) noexcept;

[[nodiscard]] bool ps1_max3_state_dominates(
    const Ps1Max3SearchScore& incumbent,
    const Ps1Max3SearchScore& candidate) noexcept;
```

- [ ] **Step 1: Add RED coverage proving strict beats every speculative progress dimension**

Extend `tests/test_ps1_max3_search_policy.cpp`:

```cpp
void test_strict_evidence_beats_speculative_progress_unconditionally() {
    const auto check = [](auto inflate) {
        auto strict = baseline();
        auto speculative = baseline();
        strict.evidence = jojo::Ps1Max3EvidenceClass::strict;
        speculative.evidence = jojo::Ps1Max3EvidenceClass::speculative;
        inflate(speculative);
        CHECK(jojo::ps1_max3_search_outranks(strict, speculative));
        CHECK(!jojo::ps1_max3_search_outranks(speculative, strict));
    };

    check([](auto& s) { s.presented_frames = 1000u; });
    check([](auto& s) { s.vram_write_count = 1000u; });
    check([](auto& s) { s.gpu_gp0_command_count = 1000u; });
    check([](auto& s) { s.gpu_gp1_command_count = 1000u; });
    check([](auto& s) { s.dma_transfer_count = 1000u; });
    check([](auto& s) { s.cdrom_command_count = 1000u; });
    check([](auto& s) { s.interrupt_callback_progress = 1000u; });
    check([](auto& s) { s.new_frontier_count = 1000u; });
    check([](auto& s) { s.new_coverage_count = 1000u; });
}
```

Also update `test_priority_order_is_lexicographic_and_deterministic()` so evidence is exercised before every progress field.

- [ ] **Step 2: Add dominance contracts**

Add:

```cpp
void test_evidence_authority_controls_state_dominance() {
    auto strict = baseline();
    auto speculative = strict;
    strict.evidence = jojo::Ps1Max3EvidenceClass::strict;
    speculative.evidence = jojo::Ps1Max3EvidenceClass::speculative;

    CHECK(jojo::ps1_max3_state_dominates(strict, speculative));
    CHECK(!jojo::ps1_max3_state_dominates(speculative, strict));

    speculative.presented_frames = 1u;
    CHECK(!jojo::ps1_max3_state_dominates(strict, speculative));
}
```

The last assertion preserves `progress_not_worse`: strict authority does not erase the other dominance requirements.

- [ ] **Step 3: Run the search-policy target and verify RED**

```bash
cmake --build build --target jojo_ps1_max3_search_policy_tests --parallel
ctest --test-dir build --output-on-failure -R '^jojo_ps1_max3_search_policy_tests$'
```

Expected: FAIL because Phase A compares progress before evidence.

- [ ] **Step 4: Move evidence comparison to the first decision in `ps1_max3_search_outranks()`**

```cpp
bool ps1_max3_search_outranks(const Ps1Max3SearchScore& candidate,
                              const Ps1Max3SearchScore& current) noexcept {
    if (candidate.evidence != current.evidence)
        return evidence_outranks(candidate.evidence, current.evidence);
    if (candidate.presented_frames != current.presented_frames)
        return candidate.presented_frames > current.presented_frames;
    // preserve the existing progress and deterministic tie-break order
}
```

Remove the later duplicate evidence comparison. Do not reorder fields within the same evidence class.

- [ ] **Step 5: Keep dominance evidence-aware without weakening progress checks**

Retain the existing structure:

```cpp
if (incumbent.state_hash != candidate.state_hash) return false;
if (!progress_not_worse(incumbent, candidate)) return false;
if (!evidence_not_worse(incumbent.evidence, candidate.evidence)) return false;
if (incumbent.assumption_count > candidate.assumption_count) return false;
if (incumbent.speculative_depth > candidate.speculative_depth) return false;
return true;
```

No new state-equivalence heuristic is introduced.

- [ ] **Step 6: Run GREEN search-policy tests**

```bash
cmake --build build --target jojo_ps1_max3_search_policy_tests --parallel
ctest --test-dir build --output-on-failure -R '^jojo_ps1_max3_search_policy_tests$'
```

Expected: PASS.

- [ ] **Step 7: Commit the policy slice**

```bash
git add src/core/ps1_max3_search_policy.cpp tests/test_ps1_max3_search_policy.cpp
git commit -m "refactor: rank MAX3 strict evidence first"
```

---

### Task 2: Make the single MAX3 best result strict-authoritative while preserving speculation

**Files:**
- Modify: `tests/test_ps1_max3_explorer.cpp`
- Modify: `tests/test_ps1_local_evidence.cpp`
- Modify: `src/core/ps1_max3_explorer.h`
- Modify: `src/core/ps1_max3_explorer.cpp`
- Preserve: `src/core/runtime.cpp`
- Preserve: `src/core/runtime.h`

**Interfaces:**
- Consumes: strict-first `ps1_max3_search_outranks()` from Task 1.
- Produces: `best_node`, `best_path`, and `best_report` remain the existing single result but are always strict-authoritative.
- Produces one invariant helper:

```cpp
[[nodiscard]] bool ps1_max3_best_is_strict_authoritative(
    const Ps1Max3Report& report) noexcept;
```

- [ ] **Step 1: Replace the old speculative-best expectation with a RED strict-best contract**

In `test_bounds_and_progress_ranking_are_deterministic()` use:

```cpp
CHECK(ranked.value.best_node < ranked.value.nodes.size());
CHECK(ranked.value.nodes[ranked.value.best_node].evidence ==
      jojo::Ps1Max3EvidenceClass::strict);
CHECK(ranked.value.best_path.empty());
CHECK(ranked.value.best_report.stop_reason ==
      jojo::Ps1BootStopReason::bios_call_unimplemented);
CHECK(std::any_of(ranked.value.nodes.begin(), ranked.value.nodes.end(), [](const auto& node) {
    return node.evidence == jojo::Ps1Max3EvidenceClass::speculative;
}));
```

This proves strict owns the authoritative result while speculative descendants still exist.

- [ ] **Step 2: Add a RED invariant helper contract covering commercial-frame provenance**

```cpp
void test_best_report_authority_requires_strict_best_node() {
    jojo::Ps1Max3Report speculative{};
    speculative.nodes.resize(1u);
    speculative.nodes[0].evidence = jojo::Ps1Max3EvidenceClass::speculative;
    speculative.best_node = 0u;
    speculative.best_report.stop_reason =
        jojo::Ps1BootStopReason::commercial_frame_presented;
    CHECK(!jojo::ps1_max3_best_is_strict_authoritative(speculative));

    jojo::Ps1Max3Report strict = speculative;
    strict.nodes[0].evidence = jojo::Ps1Max3EvidenceClass::strict;
    CHECK(jojo::ps1_max3_best_is_strict_authoritative(strict));
}
```

The same commercial stop reason is authoritative only when the selected node is strict.

- [ ] **Step 3: Add the local-evidence RED assertion before implementing the helper**

In `tests/test_ps1_local_evidence.cpp`, after MAX3 exploration succeeds:

```cpp
CHECK(jojo::ps1_max3_best_is_strict_authoritative(max3.value));
```

For the compatibility call, require the fixture's strict root blocker:

```cpp
CHECK(checkpoint);
if (checkpoint) {
    CHECK(checkpoint.value.stop_reason ==
          jojo::Ps1BootStopReason::bios_call_unimplemented);
}
```

Because `bootstrap_runtime_local_evidence_to_file()` already returns `max3.value.best_report`, no runtime API change is required when best-report authority is fixed at the explorer level.

- [ ] **Step 4: Run explorer/local-evidence tests and verify RED**

```bash
cmake --build build --target \
  jojo_ps1_max3_explorer_tests \
  jojo_ps1_local_evidence_tests --parallel
ctest --test-dir build --output-on-failure \
  -R '^jojo_ps1_(max3_explorer|local_evidence)_tests$'
```

Expected: compile/test FAIL because `ps1_max3_best_is_strict_authoritative()` does not exist yet; on the Phase A policy the local-evidence best can also be speculative.

- [ ] **Step 5: Add the invariant helper without adding report fields**

Declare in `src/core/ps1_max3_explorer.h` after `Ps1Max3Report`:

```cpp
[[nodiscard]] bool ps1_max3_best_is_strict_authoritative(
    const Ps1Max3Report& report) noexcept;
```

Implement in `src/core/ps1_max3_explorer.cpp`:

```cpp
bool ps1_max3_best_is_strict_authoritative(const Ps1Max3Report& report) noexcept {
    return !report.nodes.empty() &&
           report.best_node < report.nodes.size() &&
           report.nodes[report.best_node].evidence == Ps1Max3EvidenceClass::strict;
}
```

Do not inspect `stop_reason`; provenance is the authority invariant for every current and future best report.

- [ ] **Step 6: Keep best selection centralized in the existing search policy**

`Explorer::process()` already uses `ps1_max3_search_outranks()` to replace `best_node`, `best_path`, and `best_report`. Do not add a second best-selection path or special-case `commercial_frame_presented`.

Do not suppress speculative nodes, rewrite their individual stop reasons, or stop their expansion.

- [ ] **Step 7: Run GREEN explorer/local-evidence tests**

```bash
cmake --build build --target \
  jojo_ps1_max3_explorer_tests \
  jojo_ps1_local_evidence_tests \
  jojo_ps1_runtime_installation_tests --parallel
ctest --test-dir build --output-on-failure \
  -R '^jojo_ps1_(max3_explorer|local_evidence|runtime_installation)_tests$'
```

Expected: PASS. The compatibility API remains source-compatible and returns the strict `best_report`; speculative descendants remain present in the MAX3 report.

- [ ] **Step 8: Commit the strict-best slice**

```bash
git add \
  src/core/ps1_max3_explorer.h \
  src/core/ps1_max3_explorer.cpp \
  tests/test_ps1_max3_explorer.cpp \
  tests/test_ps1_local_evidence.cpp
git commit -m "feat: enforce strict MAX3 best authority"
```

---

### Task 3: Make frontier-cluster ordering lexicographically strict-first

**Files:**
- Modify: `tests/test_ps1_max3_frontier_priority.cpp`
- Modify: `src/core/ps1_max3_frontier_priority.cpp`

**Interfaces:**
- Consumes: `Ps1Max3FrontierCluster::evidence`, existing cluster progress/descendant metrics.
- Produces: `cluster_and_rank_ps1_max3_frontiers()` with evidence class as the first ordering key.

- [ ] **Step 1: Add a RED test that saturates speculative progress beyond the current strict bonus**

The test file already includes `<limits>`. Add:

```cpp
static void test_strict_cluster_always_precedes_speculative_progress() {
    jojo::Ps1Max3Report report{};
    report.frontiers.push_back(mmio_frontier(
        0u, 0x80010000u, false, jojo::Ps1Max3EvidenceClass::strict, 1u));
    report.frontiers.push_back(mmio_frontier(
        1u, 0x80020000u, true, jojo::Ps1Max3EvidenceClass::speculative, 1u));

    jojo::Ps1Max3NodeSummary speculative_progress{};
    speculative_progress.frontier = 1u;
    speculative_progress.evidence = jojo::Ps1Max3EvidenceClass::speculative;
    speculative_progress.path_presented_frames =
        std::numeric_limits<std::uint64_t>::max();
    report.nodes.push_back(speculative_progress);

    const auto clusters = jojo::cluster_and_rank_ps1_max3_frontiers(report);
    CHECK(clusters.size() == 2u);
    if (clusters.size() == 2u) {
        CHECK(clusters[0].evidence == jojo::Ps1Max3EvidenceClass::strict);
        CHECK(clusters[1].evidence == jojo::Ps1Max3EvidenceClass::speculative);
    }
}
```

Different MMIO direction keeps the frontiers in separate clusters. The speculative `presented_frames` multiplication saturates to `UINT64_MAX`, which intentionally exceeds the current finite strict bonus.

- [ ] **Step 2: Run frontier-priority tests and verify RED**

```bash
cmake --build build --target jojo_ps1_max3_frontier_priority_tests --parallel
ctest --test-dir build --output-on-failure -R '^jojo_ps1_max3_frontier_priority_tests$'
```

Expected: FAIL because the current implementation encodes strictness as a finite numeric bonus instead of a lexicographic key.

- [ ] **Step 3: Remove strictness from numeric `priority_score()`**

Replace the strict bonus initialization with:

```cpp
std::uint64_t score = 0u;
```

Keep descendant count, callsite count, occurrence count, and `progress_score()` unchanged for comparisons inside the same evidence class.

- [ ] **Step 4: Make evidence the first stable-sort key**

```cpp
std::stable_sort(clusters.begin(), clusters.end(), [](const auto& lhs, const auto& rhs) {
    if (lhs.evidence != rhs.evidence) {
        return lhs.evidence == Ps1Max3EvidenceClass::strict;
    }
    if (lhs.priority_score != rhs.priority_score) {
        return lhs.priority_score > rhs.priority_score;
    }
    return lhs.index < rhs.index;
});
```

This removes saturation risk while preserving deterministic within-class ranking.

- [ ] **Step 5: Run frontier-priority GREEN tests twice**

```bash
cmake --build build --target jojo_ps1_max3_frontier_priority_tests --parallel
ctest --test-dir build --output-on-failure -R '^jojo_ps1_max3_frontier_priority_tests$'
ctest --test-dir build --output-on-failure -R '^jojo_ps1_max3_frontier_priority_tests$'
```

Expected: PASS both times with identical ordering assertions.

- [ ] **Step 6: Commit the frontier-priority slice**

```bash
git add src/core/ps1_max3_frontier_priority.cpp tests/test_ps1_max3_frontier_priority.cpp
git commit -m "refactor: rank strict MAX3 frontiers first"
```

---

### Task 4: Prove speculative exploration, report compatibility, and deterministic OMEGA behavior remain intact

**Files:**
- Verify: `src/core/ps1_max3_explorer.cpp`
- Verify: `src/core/ps1_max3_report_io.cpp`
- Verify: `src/core/runtime.cpp`
- Verify: `tests/test_ps1_max3_deep_expansion.cpp`
- Verify: `tests/test_ps1_max3_candidate_engine.cpp`
- Verify: `tests/test_ps1_max3_coverage.cpp`
- Verify: `tests/test_ps1_max3_budget.cpp`
- Verify: `tests/test_ps1_boot_report_io.cpp`
- Modify only if a real regression is exposed.

**Interfaces:**
- Consumes: completed Tasks 1–3.
- Produces: proof that Phase B changes authority only, not speculative exploration, budgets, candidate generation, or report format.

- [ ] **Step 1: Run the complete MAX3 test family**

```bash
cmake --build build --target \
  jojo_ps1_max3_explorer_tests \
  jojo_ps1_max3_profile_tests \
  jojo_ps1_max3_candidate_engine_tests \
  jojo_ps1_max3_coverage_tests \
  jojo_ps1_max3_search_policy_tests \
  jojo_ps1_max3_budget_tests \
  jojo_ps1_max3_deep_expansion_tests \
  jojo_ps1_max3_frontier_priority_tests \
  jojo_ps1_boot_report_io_tests \
  jojo_ps1_local_evidence_tests --parallel
ctest --test-dir build --output-on-failure \
  -R '^jojo_ps1_(max3_|boot_report_io|local_evidence)'
```

Expected: PASS.

- [ ] **Step 2: Confirm speculative expansion is still observable**

At least one explorer/deep-expansion contract must continue to establish:

```cpp
std::any_of(report.nodes.begin(), report.nodes.end(), [](const auto& node) {
    return node.evidence == Ps1Max3EvidenceClass::speculative;
});
```

Do not fix a failure by disabling fallbacks, shrinking budgets, or deleting candidate classes.

- [ ] **Step 3: Run report-producing tests twice for determinism**

```bash
ctest --test-dir build --output-on-failure \
  -R '^jojo_ps1_(max3_explorer|boot_report_io|local_evidence)_tests$'
ctest --test-dir build --output-on-failure \
  -R '^jojo_ps1_(max3_explorer|boot_report_io|local_evidence)_tests$'
```

Expected: PASS twice. No randomness or wall-clock ordering may appear.

- [ ] **Step 4: Audit scope against the Phase A baseline**

```bash
git diff --check
git diff --stat 40549e6988ef90a351970cd9166a4c58dc14a62c
```

Expected production changes are limited to MAX3 search policy, explorer authority helper, and frontier-priority ordering. `runtime.cpp` remains unchanged because it already returns `max3.value.best_report`. No PS1 device implementation file may gain semantic changes.

- [ ] **Step 5: Commit only if this regression sweep exposed a genuine fix**

If no files changed, do not create an empty commit. If a deterministic test regression needed correction, commit only the necessary paths with a narrow message.

---

### Task 5: Run authoritative Phase B verification and produce the Windows artifact

**Files:**
- No source changes unless CI exposes a real portability regression.

**Interfaces:**
- Produces: the green Phase B HEAD eligible to become the baseline for the first evidence-backed GPU/DMA blocker batch.

- [ ] **Step 1: Run the full local build and CTest suite**

```bash
cmake --build build --parallel
ctest --test-dir build --output-on-failure
```

Expected: 100% PASS.

- [ ] **Step 2: Verify Phase B scope**

```bash
git diff --check
git diff --stat 40549e6988ef90a351970cd9166a4c58dc14a62c
```

Expected: no whitespace errors; no new hardware semantics; no MAX3 budget or candidate-generation changes.

- [ ] **Step 3: Push `feature/ps1-omega-frame-first-phase-b` and require Linux CI success**

Required Linux steps:

```text
Configure
Build
Production readiness gate
PS1 active architecture gate
Test
Observed disc revision contract
R2.5 direct UDP transport contract
```

All must conclude `success`.

- [ ] **Step 4: Require Windows x64/MSVC 2022 success**

Required Windows steps:

```text
Configure
Build Release
Production readiness gate
PS1 active architecture gate
Test Release
Observed disc revision contract
R2.5 direct UDP transport contract
Upload single executable
```

All must conclude `success`.

- [ ] **Step 5: Verify the exact Windows artifact**

Expected:

```text
name=JOJO-Recompiled-Windows-x64
head_sha=<exact Phase B HEAD>
expired=false
```

Record artifact ID, byte size, digest, and expiry date.

- [ ] **Step 6: Record final evidence**

```text
phase_b_head=<exact SHA>
strict_first_search=true
strict_first_dominance=true
strict_first_frontier_clusters=true
best_report_strict_authoritative=true
speculative_expansion_preserved=true
linux_job=success
windows_job=success
windows_artifact=JOJO-Recompiled-Windows-x64
new_ps1_hardware_semantics=false
```

- [ ] **Step 7: Stop at the Phase B boundary**

Do not implement GP0 commands, DMA modes, IRQ/timer behavior, CD-ROM commands, GTE commands, BIOS selectors, SIO operations, or SPU behavior in this plan. The next plan must be selected from the next real strict JoJo checkpoint evidence.

## Plan Self-Review Results

- **Spec coverage:** Task 1 implements strict-first search ordering and preserves evidence-aware state dominance. Task 2 makes the existing single best result strict-authoritative, proves commercial-frame provenance, and verifies the unchanged local-evidence API returns that strict result. Task 3 makes frontier clusters lexicographically strict-first. Task 4 proves preserved speculation, determinism, budgets, candidates, and report compatibility. Task 5 covers Linux/Windows/artifact exit gates.
- **Placeholder scan:** No TBD, TODO, unspecified test, or deferred implementation placeholder remains. Commands, expected failures, interfaces, and exact invariants are explicit.
- **Type consistency:** `Ps1Max3SearchScore`, `Ps1Max3EvidenceClass`, `Ps1Max3Report`, `Ps1BootReport`, `Ps1BootStopReason`, and `ps1_max3_best_is_strict_authoritative()` use one consistent naming scheme throughout.
- **Scope check:** No PS1 hardware semantics, MAX3 budgets, candidate-generation rules, or runtime public APIs are changed. Phase B remains one reviewable authority/gating subproject.
