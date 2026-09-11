# PS1 MAX³ OMEGA Explorer Core Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Replace BIOS-only recursive MAX³ expansion with a deterministic profile-aware explorer that safely crosses eligible MMIO reads, generates representative candidates, tracks coverage, prunes duplicate/dominated states, adapts fixed deterministic budgets, and ranks frontiers for batch work.

**Architecture:** Keep `Ps1BootRuntime` as the execution authority and split OMEGA policy out of `ps1_max3_explorer.cpp`. New small components own candidates, coverage, queue ordering, and budgets; the explorer orchestrates them and never fabricates side effects. Existing public aggregate field order remains compatibility-sensitive.

**Tech Stack:** C++20, STL containers, existing R3000A/MAX³ runtime, CMake/CTest.

**Spec:** `docs/superpowers/specs/2026-09-10-ps1-max3-omega-deep-consolidation-design.md`

## Global Constraints

- Strict mode must not branch through MMIO reads.
- Deep mode candidate baseline is deterministic: `0`, `1`, width-masked all-ones, then strict-observed value if available and distinct.
- Omega mode may add deterministic equivalence-class candidates, capped by options.
- Unsupported writes/device/GPU/DMA/CPU boundaries remain terminal.
- Never alter GPR/PC/pending-load directly to continue a frontier.
- Preserve public aggregate prefix layout in `Ps1Max3Options`, `Ps1Max3Decision`, and `Ps1Max3NodeSummary`; append new fields only.
- No randomness, host timing, or free-memory observations.

---

### Task A1: Add explicit `strict` / `deep` / `omega` profile API

**Files:**
- Modify: `src/core/ps1_max3_explorer.h`
- Modify: `src/core/ps1_max3_explorer.cpp`
- Test: `tests/test_ps1_max3_explorer.cpp`

**Interfaces:**
- Produces: `enum class Ps1Max3Profile : std::uint8_t { strict, deep, omega };`
- Produces: `Ps1Max3Options ps1_max3_options(Ps1Max3Profile profile) noexcept;`
- Preserves: `ps1_max3_local_evidence_options()` as a compatibility wrapper until Plan D.

- [ ] **Step 1: Write failing profile tests**

Add tests equivalent to:

```cpp
static void test_profile_defaults_are_explicit_and_bounded() {
    const auto strict = jojo::ps1_max3_options(jojo::Ps1Max3Profile::strict);
    CHECK(strict.profile == jojo::Ps1Max3Profile::strict);
    CHECK(!strict.deep_frontier_enabled);

    const auto deep = jojo::ps1_max3_options(jojo::Ps1Max3Profile::deep);
    CHECK(deep.profile == jojo::Ps1Max3Profile::deep);
    CHECK(deep.deep_frontier_enabled);
    CHECK(deep.max_candidates_per_read >= 3u);

    const auto omega = jojo::ps1_max3_options(jojo::Ps1Max3Profile::omega);
    CHECK(omega.profile == jojo::Ps1Max3Profile::omega);
    CHECK(omega.max_nodes == 16383u);
    CHECK(omega.max_branch_depth == 32u);
    CHECK(omega.max_speculative_depth == 24u);
    CHECK(omega.max_unique_frontiers == 96u);
    CHECK(omega.max_total_retired == 3000000000ull);
    CHECK(omega.max_candidates_per_read == 8u);
}
```

Append fields to `Ps1Max3Options` only:

```cpp
Ps1Max3Profile profile{Ps1Max3Profile::strict};
std::size_t max_candidates_per_read{3u};
std::size_t max_unique_states{65536u};
std::size_t max_queued_states{16384u};
```

- [ ] **Step 2: Run RED**

```bash
cmake -S . -B build -DCMAKE_BUILD_TYPE=Release
cmake --build build --parallel 2 --target jojo_ps1_max3_explorer_tests
```

Expected: compile failure because profile API/fields do not exist.

- [ ] **Step 3: Implement minimal profile constructors**

Implement a switch returning exact deterministic presets. Keep `ps1_max3_local_evidence_options()` returning `ps1_max3_options(Ps1Max3Profile::deep)` for now so Plan D controls the eventual commercial switch.

- [ ] **Step 4: Run GREEN**

```bash
ctest --test-dir build -R jojo_ps1_max3_explorer_tests --output-on-failure
```

Expected: PASS.

- [ ] **Step 5: Commit**

```bash
git add src/core/ps1_max3_explorer.h src/core/ps1_max3_explorer.cpp tests/test_ps1_max3_explorer.cpp
git commit -m "feat: add explicit MAX3 diagnostic profiles"
```

### Task A2: Create deterministic MMIO read candidate engine

**Files:**
- Create: `src/core/ps1_max3_candidate_engine.h`
- Create: `src/core/ps1_max3_candidate_engine.cpp`
- Create: `tests/test_ps1_max3_candidate_engine.cpp`
- Modify: `CMakeLists.txt`

**Interfaces:**
- Produces:

```cpp
enum class Ps1Max3CandidateSource : std::uint8_t {
    baseline_zero,
    baseline_one,
    baseline_all_ones,
    sign_bit,
    strict_observed,
    zero_test_class,
    mask_class,
    threshold_class,
};

struct Ps1Max3ReadCandidate {
    std::uint32_t value{};
    Ps1Max3CandidateSource source{Ps1Max3CandidateSource::baseline_zero};
};

struct Ps1Max3CandidateContext {
    Ps1Max3Profile profile{Ps1Max3Profile::strict};
    std::uint8_t width{};
    std::uint32_t pc{};
    std::uint32_t load_opcode{};
    std::vector<std::uint32_t> following_opcodes;
    std::vector<std::uint32_t> strict_observed_values;
    std::size_t max_candidates{};
};

std::vector<Ps1Max3ReadCandidate> generate_ps1_max3_read_candidates(
    const Ps1Max3CandidateContext& context);
```

- [ ] **Step 1: Write RED tests for baseline order and width masking**

```cpp
CHECK(values(deep8) == std::vector<std::uint32_t>({0u, 1u, 0xffu}));
CHECK(values(deep16) == std::vector<std::uint32_t>({0u, 1u, 0xffffu}));
CHECK(values(deep32) == std::vector<std::uint32_t>({0u, 1u, 0xffffffffu}));
```

Also test strict returns no candidates and duplicate strict-observed values are removed without reordering.

- [ ] **Step 2: Run RED**

```bash
cmake --build build --parallel 2 --target jojo_ps1_max3_candidate_engine_tests
```

Expected: target/source missing.

- [ ] **Step 3: Implement baseline candidate generation**

Use a stable append-if-new helper and mask by width. Do not inspect device type.

- [ ] **Step 4: Add Omega equivalence-class RED tests**

Build local instruction windows with `ANDI rt,rt,1`, `BEQ rt,zero`, and `SLTIU`; require representative clear/set or below/at/above-threshold candidates without enumerating the full value domain.

- [ ] **Step 5: Implement bounded local decode inference**

Use existing MIPS opcode fields directly or `mips_decoder` helpers; examine at most a fixed constant window such as 8 following instructions. Candidate generation must remain deterministic and stop at `max_candidates`.

- [ ] **Step 6: Run GREEN and full candidate test**

```bash
ctest --test-dir build -R jojo_ps1_max3_candidate_engine_tests --output-on-failure
```

Expected: PASS.

- [ ] **Step 7: Commit**

```bash
git add CMakeLists.txt src/core/ps1_max3_candidate_engine.* tests/test_ps1_max3_candidate_engine.cpp
git commit -m "feat: add deterministic MAX3 read candidates"
```

### Task A3: Add explicit coverage model

**Files:**
- Create: `src/core/ps1_max3_coverage.h`
- Create: `src/core/ps1_max3_coverage.cpp`
- Create: `tests/test_ps1_max3_coverage.cpp`
- Modify: `CMakeLists.txt`

**Interfaces:**
- Produces:

```cpp
struct Ps1Max3CoverageDelta {
    std::size_t new_pcs{};
    std::size_t new_opcodes{};
    std::size_t new_bios_pairs{};
    std::size_t new_mmio_tuples{};
    std::size_t new_frontiers{};
    std::size_t new_landmarks{};
};

class Ps1Max3Coverage {
public:
    Ps1Max3CoverageDelta observe(const Ps1BootReport& segment,
                                 const Ps1Max3Frontier* frontier);
    [[nodiscard]] std::uint64_t deterministic_hash() const noexcept;
};
```

- [ ] **Step 1: Write failing coverage-delta tests**

Construct synthetic `Ps1BootReport` values containing repeated/new trace PCs, BIOS calls, MMIO events, CD/GPU/DMA counters and frontier identity. Require repeats to return zero delta.

- [ ] **Step 2: Run RED**

```bash
cmake --build build --parallel 2 --target jojo_ps1_max3_coverage_tests
```

- [ ] **Step 3: Implement stable set-based coverage**

Store semantic keys only; do not include wall-clock data or report vector addresses.

- [ ] **Step 4: Verify deterministic hash insertion-order independence**

Feed the same semantic keys in two different input orders and require identical final hash.

- [ ] **Step 5: Run GREEN and commit**

```bash
ctest --test-dir build -R jojo_ps1_max3_coverage_tests --output-on-failure
git add CMakeLists.txt src/core/ps1_max3_coverage.* tests/test_ps1_max3_coverage.cpp
git commit -m "feat: track deterministic MAX3 coverage"
```

### Task A4: Add deterministic queue scoring and dominance model

**Files:**
- Create: `src/core/ps1_max3_search_policy.h`
- Create: `src/core/ps1_max3_search_policy.cpp`
- Create: `tests/test_ps1_max3_search_policy.cpp`
- Modify: `CMakeLists.txt`

**Interfaces:**
- Produces:

```cpp
struct Ps1Max3SearchScore {
    std::uint64_t presented_frames{};
    std::uint64_t vram_writes{};
    std::uint64_t gp0_commands{};
    std::uint64_t dma_transfers{};
    std::size_t new_frontiers{};
    std::size_t new_subsystem_coverage{};
    std::size_t new_instruction_coverage{};
    std::size_t speculative_depth{};
    std::size_t assumption_count{};
    std::uint64_t insertion_sequence{};
};

bool ps1_max3_higher_priority(const Ps1Max3SearchScore& lhs,
                              const Ps1Max3SearchScore& rhs) noexcept;

bool ps1_max3_dominates(const Ps1Max3NodeSummary& lhs,
                        const Ps1Max3NodeSummary& rhs) noexcept;
```

- [ ] **Step 1: Write RED tests for comparator ordering**

Require observable progress first, then new frontier/coverage, then fewer assumptions/speculative depth, then stable insertion order.

- [ ] **Step 2: Write RED dominance tests**

Same state hash + no worse progress + lower/equal speculative depth/assumptions dominates; different state hash never dominates.

- [ ] **Step 3: Implement comparator and dominance predicate**

Keep all comparisons centralized in this component.

- [ ] **Step 4: Run GREEN and commit**

```bash
ctest --test-dir build -R jojo_ps1_max3_search_policy_tests --output-on-failure
git add CMakeLists.txt src/core/ps1_max3_search_policy.* tests/test_ps1_max3_search_policy.cpp
git commit -m "feat: add MAX3 search and pruning policy"
```

### Task A5: Add deterministic adaptive budget manager

**Files:**
- Create: `src/core/ps1_max3_budget.h`
- Create: `src/core/ps1_max3_budget.cpp`
- Create: `tests/test_ps1_max3_budget.cpp`
- Modify: `CMakeLists.txt`

**Interfaces:**
- Produces:

```cpp
struct Ps1Max3BudgetUsage {
    std::size_t nodes{};
    std::size_t unique_frontiers{};
    std::size_t unique_states{};
    std::size_t queued_states{};
    std::uint64_t retired{};
    std::size_t recent_new_information{};
};

struct Ps1Max3BudgetEnvelope {
    std::size_t max_nodes{};
    std::size_t max_unique_frontiers{};
    std::size_t max_unique_states{};
    std::size_t max_queued_states{};
    std::uint64_t max_total_retired{};
};

class Ps1Max3BudgetManager {
public:
    explicit Ps1Max3BudgetManager(const Ps1Max3Options& options) noexcept;
    [[nodiscard]] const Ps1Max3BudgetEnvelope& current() const noexcept;
    bool maybe_expand(const Ps1Max3BudgetUsage& usage) noexcept;
};
```

- [ ] **Step 1: Write RED tests for no-expansion and deterministic expansion**

Require no expansion when no new information was produced; require exact integer growth step when the configured threshold is met; require never exceeding spec ceilings.

- [ ] **Step 2: Implement integer-only growth policy**

Use constants defined in source, not host memory. Example: double node/frontier/state envelopes up to ceilings only after a fixed recent-information threshold.

- [ ] **Step 3: Run GREEN and commit**

```bash
ctest --test-dir build -R jojo_ps1_max3_budget_tests --output-on-failure
git add CMakeLists.txt src/core/ps1_max3_budget.* tests/test_ps1_max3_budget.cpp
git commit -m "feat: add deterministic MAX3 budget expansion"
```

### Task A6: Integrate deep/omega MMIO read expansion into explorer

**Files:**
- Modify: `src/core/ps1_max3_explorer.h`
- Modify: `src/core/ps1_max3_explorer.cpp`
- Test: `tests/test_ps1_max3_explorer.cpp`

**Interfaces:**
- Consumes: candidate engine, coverage, search policy, budget manager, `Ps1BootRuntime::diagnostic_mmio_read_frontier()`, `apply_diagnostic_mmio_read_fallback()`.
- Produces: real `Ps1Max3DecisionKind::mmio_read_fallback` child nodes with candidate source appended to decision fields.

Append to `Ps1Max3Decision`:

```cpp
Ps1Max3CandidateSource candidate_source{Ps1Max3CandidateSource::baseline_zero};
```

- [ ] **Step 1: Write RED chained-read fixture**

Create a synthetic program that reads unsupported MMIO A, branches/continues, then reads unsupported MMIO B. In strict mode expect one terminal frontier and one node. In deep mode expect children for A and at least one downstream B frontier marked speculative.

- [ ] **Step 2: Write RED terminal-write fixture**

After a speculative read, execute an unsupported MMIO write. Require no children from that frontier.

- [ ] **Step 3: Run RED**

```bash
cmake --build build --parallel 2 --target jojo_ps1_max3_explorer_tests
ctest --test-dir build -R jojo_ps1_max3_explorer_tests --output-on-failure
```

Expected: chained-read/deep assertions fail because explorer still expands BIOS only.

- [ ] **Step 4: Replace recursive BIOS-only expansion with explicit queued work items**

Use an internal work item carrying runtime copy, parent node/frontier, evidence, depth, speculative depth, path decisions, metrics, dependencies, and stable insertion sequence. Strict root is queued first. BIOS children use existing fallback order; MMIO children use candidate-engine order.

- [ ] **Step 5: Add exact-state dedup and dominance recording**

Track expanded state hash plus assumption cost; dominated nodes remain reportable but are not re-expanded. Preserve `deduplicated` and add an appended prune reason enum rather than changing existing field order.

- [ ] **Step 6: Enforce all path/global limits**

Check branch depth, speculative depth, frontier count, state count, queue count, nodes, retired budget and current adaptive envelope before enqueue/expand. `max_unique_frontiers` only causes global `frontier_limit` when work was actually truncated.

- [ ] **Step 7: Run GREEN**

```bash
ctest --test-dir build -R jojo_ps1_max3_explorer_tests --output-on-failure
```

Expected: strict fixture unchanged; deep chained-read works; write stays terminal.

- [ ] **Step 8: Commit**

```bash
git add src/core/ps1_max3_explorer.* tests/test_ps1_max3_explorer.cpp
git commit -m "feat: explore deterministic MAX3 MMIO read frontiers"
```

### Task A7: Add frontier clustering and implementation priority

**Files:**
- Modify: `src/core/ps1_max3_explorer.h`
- Create: `src/core/ps1_max3_frontier_priority.h`
- Create: `src/core/ps1_max3_frontier_priority.cpp`
- Create: `tests/test_ps1_max3_frontier_priority.cpp`
- Modify: `CMakeLists.txt`

**Interfaces:**
- Produces:

```cpp
enum class Ps1Max3Subsystem : std::uint8_t {
    bios, cpu, irq, cdrom, gpu, dma, timer, mmio, other,
};

struct Ps1Max3FrontierCluster {
    std::size_t index{};
    Ps1Max3Subsystem subsystem{Ps1Max3Subsystem::other};
    std::vector<std::size_t> frontier_indices;
    std::size_t total_occurrences{};
    std::size_t strict_occurrences{};
    std::size_t speculative_occurrences{};
    std::size_t descendant_frontier_count{};
    std::uint64_t priority_score{};
};

std::vector<Ps1Max3FrontierCluster> cluster_and_rank_ps1_max3_frontiers(
    const Ps1Max3Report& report);
```

- [ ] **Step 1: Write RED clustering tests**

Same address/width/direction across different PCs remains separate frontier records but belongs to one cluster. Same BIOS table/selector clusters. Read vs write never clusters together.

- [ ] **Step 2: Write RED ranking tests**

Strict occurrence weight must exceed speculative-only occurrence weight; clusters with more descendants/unlock value rank above isolated equal-confidence clusters.

- [ ] **Step 3: Implement subsystem classification and integer priority score**

No floating-point scoring. Store clusters in `Ps1Max3Report` as appended field.

- [ ] **Step 4: Run GREEN and commit**

```bash
ctest --test-dir build -R jojo_ps1_max3_frontier_priority_tests --output-on-failure
git add CMakeLists.txt src/core/ps1_max3_explorer.h src/core/ps1_max3_frontier_priority.* tests/test_ps1_max3_frontier_priority.cpp
git commit -m "feat: rank MAX3 frontier root causes"
```

### Task A8: Determinism, limit, and promotion regression suite

**Files:**
- Modify: `tests/test_ps1_max3_explorer.cpp`
- Create: `tests/test_ps1_max3_omega_determinism.cpp`
- Modify: `CMakeLists.txt`

**Interfaces:**
- Consumes: all Plan A APIs.
- Produces: Plan A promotion evidence.

- [ ] **Step 1: Add repeated-run determinism test**

Run the same synthetic OMEGA fixture at least three times and compare node order, frontier identities, decisions, best node/path, clusters, termination reason, and selected candidate values.

- [ ] **Step 2: Add each-budget-stop test**

Exercise node, retired, frontier, queued-state, unique-state, branch-depth and speculative-depth limits separately. Sibling paths must continue for path-local depth stops.

- [ ] **Step 3: Add strict-vs-omega isolation test**

Run identical executable in strict then omega; strict must contain no MMIO fallback decisions and bus/runtime production semantics must be unchanged.

- [ ] **Step 4: Run Plan A focused suite**

```bash
cmake --build build --parallel 2
ctest --test-dir build -R "jojo_ps1_max3_(candidate_engine|coverage|search_policy|budget|explorer|frontier_priority|omega_determinism)_tests" --output-on-failure
```

Expected: all PASS.

- [ ] **Step 5: Run full Linux suite**

```bash
ctest --test-dir build --output-on-failure
```

Expected: all PASS.

- [ ] **Step 6: Commit**

```bash
git add CMakeLists.txt tests/test_ps1_max3_explorer.cpp tests/test_ps1_max3_omega_determinism.cpp
git commit -m "test: lock MAX3 OMEGA explorer determinism"
```

- [ ] **Step 7: Require Windows/MSVC CI success on the exact final Plan A SHA before Plan B**
