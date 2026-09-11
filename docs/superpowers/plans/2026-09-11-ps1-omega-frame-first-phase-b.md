# PS1 OMEGA Frame-First Phase B Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Make OMEGA strict-first for ranking and commercial authority while preserving speculative exploration and adding no new PS1 hardware semantics.

**Architecture:** `Ps1Max3SearchScore` becomes lexicographically strict-first, and the same evidence rule governs state dominance, best-node selection, and frontier-cluster ranking. `best_node`, `best_path`, and `best_report` remain the single authoritative result and must always refer to a strict node; speculative descendants stay in `nodes`/`frontiers` for diagnosis. The local-evidence compatibility API defensively refuses a MAX3 report whose authoritative best node is not strict.

**Tech Stack:** C++20, CMake/CTest, GitHub Actions Linux + Windows x64/MSVC 2022.

**Spec:** `docs/superpowers/specs/2026-09-11-ps1-omega-frame-first-phase-b-design.md`

## Global Constraints

- Baseline is `40549e6988ef90a351970cd9166a4c58dc14a62c`.
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
- Produces: unchanged signatures:

```cpp
[[nodiscard]] bool ps1_max3_search_outranks(
    const Ps1Max3SearchScore& candidate,
    const Ps1Max3SearchScore& current) noexcept;

[[nodiscard]] bool ps1_max3_state_dominates(
    const Ps1Max3SearchScore& incumbent,
    const Ps1Max3SearchScore& candidate) noexcept;
```

- [ ] **Step 1: Add RED coverage proving strict beats every speculative progress dimension**

Extend `tests/test_ps1_max3_search_policy.cpp` with a helper and a table-style test:

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

Also update `test_priority_order_is_lexicographic_and_deterministic()` so evidence is tested before progress rather than after it.

- [ ] **Step 2: Add RED dominance contracts**

Add:

```cpp
void test_evidence_authority_controls_state_dominance() {
    auto strict = baseline();
    auto speculative = strict;
    strict.evidence = jojo::Ps1Max3EvidenceClass::strict;
    speculative.evidence = jojo::Ps1Max3EvidenceClass::speculative;

    CHECK(jojo::ps1_max3_state_dominates(strict, speculative));
    CHECK(!jojo::ps1_max3_state_dominates(speculative, strict));

    auto strict_with_less_progress = strict;
    speculative.presented_frames = 1u;
    CHECK(!jojo::ps1_max3_state_dominates(strict_with_less_progress, speculative));
}
```

The last assertion preserves the existing `progress_not_worse` rule: strict authority does not erase the remaining dominance requirements.

- [ ] **Step 3: Run the search-policy target and verify RED**

```bash
cmake --build build --target jojo_ps1_max3_search_policy_tests --parallel
ctest --test-dir build --output-on-failure -R '^jojo_ps1_max3_search_policy_tests$'
```

Expected: FAIL because a speculative score with a larger high-priority progress field still outranks strict on the Phase A implementation.

- [ ] **Step 4: Move evidence comparison to the first decision in `ps1_max3_search_outranks()`**

The implementation order must begin:

```cpp
bool ps1_max3_search_outranks(const Ps1Max3SearchScore& candidate,
                              const Ps1Max3SearchScore& current) noexcept {
    if (candidate.evidence != current.evidence)
        return evidence_outranks(candidate.evidence, current.evidence);
    if (candidate.presented_frames != current.presented_frames)
        return candidate.presented_frames > current.presented_frames;
    // preserve existing progress and deterministic tie-break order here
}
```

Remove the later duplicate evidence comparison. Do not reorder fields within the same evidence class.

- [ ] **Step 5: Keep dominance evidence-aware without weakening progress checks**

Retain the existing `progress_not_worse()` requirement and `evidence_not_worse()` requirement. The desired structure remains:

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

### Task 2: Make `best_node`, `best_path`, and `best_report` strict-authoritative while preserving speculation

**Files:**
- Modify: `tests/test_ps1_max3_explorer.cpp`
- Modify: `src/core/ps1_max3_explorer.h`
- Modify: `src/core/ps1_max3_explorer.cpp`

**Interfaces:**
- Consumes: strict-first `ps1_max3_search_outranks()` from Task 1.
- Produces: existing `Ps1Max3Report::best_node`, `best_path`, `best_report` semantics become strict-authoritative.
- Produces one invariant helper:

```cpp
[[nodiscard]] bool ps1_max3_best_is_strict_authoritative(
    const Ps1Max3Report& report) noexcept;
```

- [ ] **Step 1: Rewrite the existing explorer ranking expectation as a RED strict-authority contract**

In `test_bounds_and_progress_ranking_are_deterministic()`, replace the expectation that the best path is speculative with:

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

This test proves two things simultaneously: strict owns the authoritative best result, and speculative descendants still exist.

- [ ] **Step 2: Add a pure invariant test for commercial-frame authority**

Add a test constructing two synthetic reports:

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

A strict commercial frame remains valid; the same stop reason attached to a speculative authoritative node is rejected.

- [ ] **Step 3: Run explorer tests and verify RED**

```bash
cmake --build build --target jojo_ps1_max3_explorer_tests --parallel
ctest --test-dir build --output-on-failure -R '^jojo_ps1_max3_explorer_tests$'
```

Expected: compile/test FAIL because the invariant helper does not exist yet and the old best-node expectation is no longer valid.

- [ ] **Step 4: Add the invariant helper without adding report fields**

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

Do not inspect `stop_reason` here. The helper validates authority provenance for every returned best report, including future commercial-frame states.

- [ ] **Step 5: Verify explorer best selection needs no special-case commercial code**

`Explorer::process()` already uses `ps1_max3_search_outranks()` for best-node replacement. Keep that single mechanism. With Task 1, a speculative node cannot replace a strict root/best node regardless of counters.

Do not suppress speculative nodes, rewrite their individual `stop_reason`, or stop their expansion.

- [ ] **Step 6: Run explorer GREEN tests**

```bash
cmake --build build --target jojo_ps1_max3_explorer_tests --parallel
ctest --test-dir build --output-on-failure -R '^jojo_ps1_max3_explorer_tests$'
```

Expected: PASS with a strict authoritative best and retained speculative descendants.

- [ ] **Step 7: Commit the explorer authority slice**

```bash
git add src/core/ps1_max3_explorer.h src/core/ps1_max3_explorer.cpp tests/test_ps1_max3_explorer.cpp
git commit -m "feat: enforce strict MAX3 best authority"
```

---

### Task 3: Make frontier-cluster ordering lexicographically strict-first

**Files:**
- Modify: `tests/test_ps1_max3_frontier_priority.cpp`
- Modify: `src/core/ps1_max3_frontier_priority.cpp`

**Interfaces:**
- Consumes: `Ps1Max3FrontierCluster::evidence`, existing cluster progress/descendant metrics.
- Produces: `cluster_and_rank_ps1_max3_frontiers()` with strict evidence as the first ordering key.

- [ ] **Step 1: Add a RED test where speculative progress is intentionally enormous**

Add:

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
    speculative_progress.path_presented_frames = 1000000u;
    speculative_progress.path_vram_write_count = 1000000u;
    speculative_progress.path_gpu_gp0_command_count = 1000000u;
    speculative_progress.path_dma_transfer_count = 1000000u;
    report.nodes.push_back(speculative_progress);

    const auto clusters = jojo::cluster_and_rank_ps1_max3_frontiers(report);
    CHECK(clusters.size() == 2u);
    if (clusters.size() == 2u) {
        CHECK(clusters[0].evidence == jojo::Ps1Max3EvidenceClass::strict);
        CHECK(clusters[1].evidence == jojo::Ps1Max3EvidenceClass::speculative);
    }
}
```

Use different MMIO direction as above so the frontiers form separate clusters.

- [ ] **Step 2: Run frontier-priority tests and verify RED**

```bash
cmake --build build --target jojo_ps1_max3_frontier_priority_tests --parallel
ctest --test-dir build --output-on-failure -R '^jojo_ps1_max3_frontier_priority_tests$'
```

Expected: FAIL if speculative progress saturates/outweighs the current numeric strict bonus.

- [ ] **Step 3: Remove strictness from the numeric score and make it a sort key**

In `priority_score()`, remove `kStrictEvidenceWeight` and start the score at zero:

```cpp
std::uint64_t score = 0u;
```

Keep descendant/callsite/occurrence/progress scoring unchanged for comparisons within the same evidence class.

Change final stable sort to:

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

This makes strictness lexicographic rather than a saturating numeric approximation.

- [ ] **Step 4: Run frontier-priority GREEN tests twice for determinism**

```bash
cmake --build build --target jojo_ps1_max3_frontier_priority_tests --parallel
ctest --test-dir build --output-on-failure -R '^jojo_ps1_max3_frontier_priority_tests$'
ctest --test-dir build --output-on-failure -R '^jojo_ps1_max3_frontier_priority_tests$'
```

Expected: PASS both times with identical ordering assertions.

- [ ] **Step 5: Commit the frontier-priority slice**

```bash
git add src/core/ps1_max3_frontier_priority.cpp tests/test_ps1_max3_frontier_priority.cpp
git commit -m "refactor: rank strict MAX3 frontiers first"
```

---

### Task 4: Defensively gate the local-evidence compatibility API on strict authority

**Files:**
- Modify: `src/core/runtime.cpp`
- Modify: `tests/test_ps1_local_evidence.cpp`
- Preserve: `src/core/runtime.h`

**Interfaces:**
- Consumes: `ps1_max3_best_is_strict_authoritative(const Ps1Max3Report&)` from Task 2.
- Produces: unchanged public signature:

```cpp
Result<Ps1BootReport> bootstrap_runtime_local_evidence_to_file(
    const std::filesystem::path& install_root,
    const std::filesystem::path& report_path);
```

- [ ] **Step 1: Add RED local-evidence assertions for strict returned authority**

After `bootstrap_runtime_max3_local_evidence_to_file()` succeeds in `tests/test_ps1_local_evidence.cpp`, add:

```cpp
CHECK(jojo::ps1_max3_best_is_strict_authoritative(max3.value));
CHECK(max3.value.best_node < max3.value.nodes.size());
if (max3.value.best_node < max3.value.nodes.size()) {
    CHECK(max3.value.nodes[max3.value.best_node].evidence ==
          jojo::Ps1Max3EvidenceClass::strict);
}
```

For the compatibility call, assert the returned stop remains the strict root blocker for the fixture:

```cpp
CHECK(checkpoint);
if (checkpoint) {
    CHECK(checkpoint.value.stop_reason ==
          jojo::Ps1BootStopReason::bios_call_unimplemented);
}
```

The fixture intentionally creates an unknown BIOS frontier and speculative descendants, so the compatibility result must remain strict despite deeper diagnostic branches.

- [ ] **Step 2: Run local-evidence test and verify RED against the old authority behavior**

```bash
cmake --build build --target jojo_ps1_local_evidence_tests --parallel
ctest --test-dir build --output-on-failure -R '^jojo_ps1_local_evidence_tests$'
```

Expected before the strict-first implementation is complete: FAIL because the previous best path can be speculative. After Tasks 1–3, the assertions should pass; keep the next defensive gate regardless.

- [ ] **Step 3: Add an invariant guard before returning the compatibility `best_report`**

Change `bootstrap_runtime_local_evidence_to_file()` to:

```cpp
auto max3 = bootstrap_runtime_max3_local_evidence_to_file(
    install_root, report_path, ps1_max3_local_evidence_options());
if (!max3) {
    return Result<Ps1BootReport>::failure(max3.error, max3.detail);
}
if (!ps1_max3_best_is_strict_authoritative(max3.value)) {
    return Result<Ps1BootReport>::failure(
        ErrorCode::backend_unavailable,
        "MAX3 local-evidence best report is not strict-authoritative");
}
return Result<Ps1BootReport>::success(std::move(max3.value.best_report));
```

This is a defense-in-depth invariant. It does not change runtime device semantics and does not create another best-report representation.

- [ ] **Step 4: Run local-evidence and runtime-installation GREEN tests**

```bash
cmake --build build --target \
  jojo_ps1_local_evidence_tests \
  jojo_ps1_runtime_installation_tests --parallel
ctest --test-dir build --output-on-failure \
  -R '^jojo_ps1_(local_evidence|runtime_installation)_tests$'
```

Expected: PASS. The generated MAX3 report still exists, installation contents remain unchanged, and the compatibility API returns only a strict best report.

- [ ] **Step 5: Commit the API gate**

```bash
git add src/core/runtime.cpp tests/test_ps1_local_evidence.cpp
git commit -m "feat: gate local evidence on strict MAX3 authority"
```

---

### Task 5: Prove speculative exploration, report compatibility, and deterministic OMEGA regressions remain intact

**Files:**
- Verify: `src/core/ps1_max3_explorer.cpp`
- Verify: `src/core/ps1_max3_report_io.cpp`
- Verify: `tests/test_ps1_max3_deep_expansion.cpp`
- Verify: `tests/test_ps1_max3_candidate_engine.cpp`
- Verify: `tests/test_ps1_max3_coverage.cpp`
- Verify: `tests/test_ps1_max3_budget.cpp`
- Verify: `tests/test_ps1_boot_report_io.cpp`
- Modify only if a real regression is exposed.

**Interfaces:**
- Consumes: completed Tasks 1–4.
- Produces: evidence that Phase B changes authority only, not exploration capability or report format.

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
  jojo_ps1_boot_report_io_tests --parallel
ctest --test-dir build --output-on-failure -R 'jojo_ps1_(max3_|boot_report_io)'
```

Expected: PASS.

- [ ] **Step 2: Explicitly inspect the speculative-expansion contract**

Confirm existing explorer/deep-expansion tests still observe speculative nodes after a strict frontier. The required invariant is:

```cpp
std::any_of(report.nodes.begin(), report.nodes.end(), [](const auto& node) {
    return node.evidence == Ps1Max3EvidenceClass::speculative;
});
```

Do not "fix" a failing test by disabling fallbacks or reducing budgets.

- [ ] **Step 3: Run report-producing tests twice**

```bash
ctest --test-dir build --output-on-failure -R '^jojo_ps1_(max3_explorer|boot_report_io|local_evidence)_tests$'
ctest --test-dir build --output-on-failure -R '^jojo_ps1_(max3_explorer|boot_report_io|local_evidence)_tests$'
```

Expected: PASS twice. No random or time-derived ordering may appear.

- [ ] **Step 4: Audit scope against the Phase A baseline**

```bash
git diff --stat 40549e6988ef90a351970cd9166a4c58dc14a62c
```

Expected production changes are limited to MAX3 policy/explorer/frontier-priority and the runtime local-evidence guard, plus tests/spec/plan. No PS1 device implementation file should have semantic changes.

- [ ] **Step 5: Commit only if Task 5 exposed and fixed a genuine regression**

If no source changes were needed, do not create an empty commit. If a test-only deterministic regression fix was required, commit only those paths with a narrow message such as:

```bash
git commit -m "test: preserve MAX3 speculative exploration"
```

---

### Task 6: Run the authoritative Phase B verification and produce the Windows artifact

**Files:**
- No source changes unless CI exposes a real portability regression.

**Interfaces:**
- Produces: the green Phase B HEAD that is eligible to become the baseline for the first evidence-backed GPU/DMA blocker phase.

- [ ] **Step 1: Run the full local build and CTest suite**

```bash
cmake --build build --parallel
ctest --test-dir build --output-on-failure
```

Expected: 100% PASS.

- [ ] **Step 2: Verify Phase B scope before push**

```bash
git diff --check
git diff --stat 40549e6988ef90a351970cd9166a4c58dc14a62c
```

Expected: no whitespace errors; no new hardware semantics; no budget/candidate changes.

- [ ] **Step 3: Push the implementation branch and require Linux CI success**

Use an implementation branch derived from the green Phase A baseline, e.g. `feature/ps1-omega-frame-first-phase-b`, carrying the approved spec and this plan.

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

- [ ] **Step 6: Record final Phase B evidence**

Record:

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

Do not implement a GP0 command, DMA mode, IRQ/timer behavior, CD-ROM command, GTE command, BIOS selector, SIO operation, or SPU behavior in this plan. The next plan must be selected from the next real strict JoJo checkpoint evidence.

## Plan Self-Review Results

- **Spec coverage:** Tasks 1–4 implement strict-first search authority, dominance, strict-authoritative best result, frontier-cluster priority, speculative commercial-frame gating, and the local-evidence defensive boundary. Task 5 proves determinism and preserved speculation. Task 6 covers Linux/Windows/artifact exit gates.
- **Placeholder scan:** No TBD, TODO, unspecified test, or deferred implementation placeholder remains. Commands, expected failures, interfaces, and exact invariants are explicit.
- **Type consistency:** `Ps1Max3SearchScore`, `Ps1Max3EvidenceClass`, `Ps1Max3Report`, `Ps1BootReport`, `Ps1BootStopReason`, and `ps1_max3_best_is_strict_authoritative()` use one consistent naming scheme throughout the plan.
- **Scope check:** No PS1 hardware semantics, MAX3 budgets, or candidate-generation rules are changed. Phase B remains one reviewable authority/gating subproject.
