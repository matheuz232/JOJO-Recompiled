# PS1 OMEGA Frame-First Consolidation Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Consolidate current `main` with the mature OMEGA diagnostic line into one authoritative, fully tested PS1 runtime without adding new hardware semantics in this phase.

**Architecture:** Start from the mature OMEGA head because it already contains the broader GPU/GTE/CD-ROM/interrupt/MAX³ diagnostic architecture, then merge current `main` into it and resolve conflicts semantically. Preserve current `main`'s modular `Ps1HleBios` ownership boundary while retaining OMEGA's richer `Ps1HleBiosCall`/`Ps1HleBiosResult` contract, device state, interrupt continuation, deep/omega search infrastructure, deterministic hashing, and report compatibility. Complete the phase only when targeted PS1 suites and the full Linux/Windows gates are green on the consolidated head.

**Tech Stack:** C++20, CMake 3.20+, CTest, Git, GitHub Actions, Win32/MSVC 2022, Linux/GCC/Clang-compatible portable core.

**Spec:** `docs/superpowers/specs/2026-09-11-ps1-omega-frame-first-design.md`

## Global Constraints

- The first-frame milestone is complete only when JoJo reaches `commercial_frame_presented` through a strict execution path using implemented CPU, BIOS/HLE, memory, and device semantics.
- Speculative OMEGA descendants may expose likely blockers but may never satisfy the first-frame milestone.
- Preserve the modular HLE ownership boundary from current `main`.
- Preserve the broader evidence-backed BIOS/kernel behavior from `feature/ps1-gp0-dma2-bios-frontier-v0`.
- Preserve strict runtime behavior; diagnostic fallbacks remain outside production semantics.
- Preserve deterministic diagnostic hashing and MAX³ deduplication.
- Preserve OMEGA coverage, candidate generation, budget, priority, CD-ROM, interrupt, GPU, and GTE tests.
- Do not add new GP0, DMA, IRQ/timer, CD-ROM, GTE, SIO, SPU, or BIOS semantics during this consolidation phase.
- Unsupported behavior must remain explicit; do not convert unsupported writes or side-effectful device operations into silent success.
- Linux and Windows x64 CI gates must both pass before consolidation is considered complete.
- Commercial-image evidence remains bounded and derived; do not commit game images, BIOS ROMs, raw sectors, unrestricted guest-memory dumps, textures, or audio.

## Pinned Inputs

- Current `main`: `fbde3f2a455845f062ab94d7f8db3040eb042889`
- Mature OMEGA source: `8402c574bba0b58fe107af66740349c43f6dcd31`
- Historical merge base: `0fa7a3c52d16c5c9a26e381fab667a744e5c1983`
- Expected implementation branch: `feature/ps1-omega-frame-first-consolidation`

## File Structure

The consolidation keeps the OMEGA source tree and resolves ownership around these units:

- `src/core/ps1_hle_bios.h/.cpp` — single owner of BIOS A0/B0/C0/SYS semantics and logical HLE state.
- `src/core/ps1_boot_runtime.h/.cpp` — CPU execution loop, BIOS/SYS boundary detection, strict stop policy, diagnostic frontier ownership, interrupt continuation, and telemetry.
- `src/core/ps1_memory_bus.h/.cpp` — RAM/MMIO routing, GPU/CD-ROM device ownership, unsupported-access evidence, and one-shot diagnostic MMIO read override.
- `src/core/ps1_gpu_state.h/.cpp` — already-implemented GPU control/data state retained from OMEGA.
- `src/core/ps1_cdrom_state.h/.cpp` — already-implemented CD-ROM state retained from OMEGA.
- `src/core/ps1_interrupt_continuation.h/.cpp` — already-implemented guest interrupt continuation retained from OMEGA.
- `src/core/r3000a_state.h` and `src/core/r3000a_reference_executor.cpp` — OMEGA GTE/COP2 architectural state and transfer semantics retained.
- `src/core/ps1_max3_*.h/.cpp` — OMEGA explorer, candidate engine, coverage, search policy, budgets, priority, report I/O retained as independent focused modules.
- `CMakeLists.txt` — canonical union of current-main and OMEGA sources/tests.
- Existing PS1 tests — authoritative regression contracts; no deletions to make the merge pass.
- `tests/test_ps1_omega_consolidation.cpp` — new narrow integration sentinel proving the canonical HLE boundary and OMEGA runtime can coexist in one build.

---

### Task 1: Create the consolidation branch and reproduce the semantic merge

**Files:**
- No code changes yet.
- Merge inputs: `fbde3f2a455845f062ab94d7f8db3040eb042889`, `8402c574bba0b58fe107af66740349c43f6dcd31`

**Interfaces:**
- Consumes: exact pinned commits listed above.
- Produces: an in-progress semantic merge on `feature/ps1-omega-frame-first-consolidation` with all conflicts visible and no force-updates to `main`.

- [ ] **Step 1: Create an isolated worktree from the OMEGA head**

```bash
git fetch origin main feature/ps1-gp0-dma2-bios-frontier-v0 design/ps1-omega-frame-first
git worktree add ../jojo-omega-frame-first -b feature/ps1-omega-frame-first-consolidation 8402c574bba0b58fe107af66740349c43f6dcd31
cd ../jojo-omega-frame-first
```

Expected: `git rev-parse HEAD` prints `8402c574bba0b58fe107af66740349c43f6dcd31`.

- [ ] **Step 2: Verify the OMEGA source head before merging**

```bash
cmake -S . -B build -DCMAKE_BUILD_TYPE=Debug
cmake --build build --parallel
ctest --test-dir build --output-on-failure
```

Expected: all existing OMEGA tests pass before current `main` is introduced. If the pinned source head is already red, stop and record that as a pre-existing failure instead of attributing it to consolidation.

- [ ] **Step 3: Start the merge without committing**

```bash
git merge --no-commit --no-ff fbde3f2a455845f062ab94d7f8db3040eb042889
```

Expected: Git reports conflicts in overlapping HLE/runtime/CMake/test files while automatically carrying non-conflicting OMEGA modules.

- [ ] **Step 4: Record the conflict set**

```bash
git diff --name-only --diff-filter=U | sort
```

Expected: the list is finite and centered on files changed independently since `0fa7a3c...`; do not resolve by choosing `--ours` or `--theirs` wholesale for `ps1_hle_bios.*`, `ps1_boot_runtime.*`, `CMakeLists.txt`, or PS1 regression tests.

- [ ] **Step 5: Verify the merge remains uncommitted**

```bash
test -f .git/MERGE_HEAD || git rev-parse -q --verify MERGE_HEAD
```

Expected: merge state exists; no merge commit has been created yet.

### Task 2: Establish the canonical merged HLE contract

**Files:**
- Modify: `src/core/ps1_hle_bios.h`
- Modify: `src/core/ps1_hle_bios.cpp`
- Create: `tests/test_ps1_omega_consolidation.cpp`
- Modify: `CMakeLists.txt`
- Preserve: `tests/test_ps1_hle_bios.cpp`
- Preserve: `tests/test_ps1_kernel_hle_frontier.cpp`
- Preserve: `tests/test_ps1_96_remove.cpp`
- Preserve: `tests/test_ps1_changeclearrcnt.cpp`

**Interfaces:**
- Consumes: `R3000aState`, `Ps1MemoryBus`.
- Produces:

```cpp
enum class Ps1HleBiosDomain : std::uint8_t { a0, b0, c0, sys };

struct Ps1HleBiosCall {
    Ps1HleBiosDomain domain{};
    std::uint32_t selector{};
    std::uint32_t pc{};
    std::uint32_t a0{};
    std::uint32_t a1{};
    std::uint32_t a2{};
    std::uint32_t a3{};
    std::uint32_t ra{};
};

enum class Ps1HleBiosDisposition : std::uint8_t {
    handled,
    unsupported,
    terminal,
    return_from_exception,
};

struct Ps1HleBiosResult {
    Ps1HleBiosDisposition disposition{Ps1HleBiosDisposition::unsupported};
};

class Ps1HleBios {
public:
    [[nodiscard]] Ps1HleBiosResult dispatch(
        const Ps1HleBiosCall& call,
        R3000aState& cpu) noexcept;
    [[nodiscard]] Ps1HleBiosResult dispatch(
        const Ps1HleBiosCall& call,
        R3000aState& cpu,
        Ps1MemoryBus& bus) noexcept;
    [[nodiscard]] std::uint64_t diagnostic_state_hash() const noexcept;
};
```

The richer OMEGA call/result API is the canonical superset. Current-main semantics for A0/39, A0/56, A0/72, B0/19, B0/5B, and C0/0A remain required and are not rewritten back into `Ps1BootRuntime`.

- [ ] **Step 1: Add a failing consolidation sentinel**

Create `tests/test_ps1_omega_consolidation.cpp`:

```cpp
#include "core/ps1_hle_bios.h"
#include "core/ps1_memory_bus.h"

#include <cassert>
#include <cstdint>

int main() {
    jojo::Ps1HleBios bios;
    jojo::Ps1MemoryBus bus;
    jojo::R3000aState cpu{};

    cpu.gpr[31] = 0x80012000u;
    jojo::Ps1HleBiosCall init_heap{};
    init_heap.domain = jojo::Ps1HleBiosDomain::a0;
    init_heap.selector = 0x39u;
    init_heap.a0 = 0x80040000u;
    init_heap.a1 = 0x1000u;
    init_heap.ra = cpu.gpr[31];

    const auto init_result = bios.dispatch(init_heap, cpu, bus);
    assert(init_result.disposition == jojo::Ps1HleBiosDisposition::handled);
    assert(bios.heap_state().has_value());
    assert(bios.heap_state()->base == 0x80040000u);
    assert(bios.heap_state()->size == 0x1000u);
    assert(cpu.pc == 0x80012000u);
    assert(cpu.gpr[0] == 0u);

    const auto before_hash = bios.diagnostic_state_hash();
    jojo::Ps1HleBiosCall unknown{};
    unknown.domain = jojo::Ps1HleBiosDomain::a0;
    unknown.selector = 0xFFFFu;
    const auto unknown_result = bios.dispatch(unknown, cpu, bus);
    assert(unknown_result.disposition == jojo::Ps1HleBiosDisposition::unsupported);
    assert(bios.diagnostic_state_hash() == before_hash);

    return 0;
}
```

Register it in `CMakeLists.txt`:

```cmake
add_jojo_test(jojo_ps1_omega_consolidation_tests tests/test_ps1_omega_consolidation.cpp)
```

- [ ] **Step 2: Build the sentinel to verify the unresolved interface fails**

```bash
cmake -S . -B build -DCMAKE_BUILD_TYPE=Debug
cmake --build build --target jojo_ps1_omega_consolidation_tests --parallel
```

Expected before semantic resolution: compile/link failure if the conflict resolution has not yet produced the canonical superset API.

- [ ] **Step 3: Resolve `ps1_hle_bios.h/.cpp` to the canonical superset**

Use the OMEGA `Ps1HleBiosCall`/`Ps1HleBiosResult`/domain API and retain these state/accessor contracts:

```cpp
[[nodiscard]] const std::optional<Ps1BiosHeapState>& heap_state() const noexcept;
[[nodiscard]] const std::optional<std::uint32_t>& interrupt_hook_address() const noexcept;
[[nodiscard]] std::optional<std::uint32_t> interrupt_priority_head(std::uint32_t priority) const noexcept;
[[nodiscard]] const std::optional<bool>& pad_card_auto_ack_enabled() const noexcept;
[[nodiscard]] std::optional<bool> root_counter_auto_ack_enabled(std::uint32_t counter) const noexcept;
[[nodiscard]] bool iso9660_removed() const noexcept;
```

For A0/39, preserve current-main semantics through the call object:

```cpp
case 0x39u:
    heap_state_ = Ps1BiosHeapState{call.a0, call.a1};
    return_from_bios_vector(cpu);
    return {Ps1HleBiosDisposition::handled};
```

Unknown selectors must return `unsupported` without mutating CPU/HLE state. Bus-dependent services must return `unsupported` when called through the CPU-only overload rather than inventing memory effects.

- [ ] **Step 4: Run canonical HLE regression tests**

```bash
cmake --build build --target \
  jojo_ps1_omega_consolidation_tests \
  jojo_ps1_hle_bios_tests \
  jojo_ps1_kernel_hle_frontier_tests \
  jojo_ps1_96_remove_tests \
  jojo_ps1_changeclearrcnt_tests --parallel
ctest --test-dir build --output-on-failure -R "jojo_ps1_(omega_consolidation|hle_bios|kernel_hle_frontier|96_remove|changeclearrcnt)_tests"
```

Expected: all listed tests pass.

- [ ] **Step 5: Stage the HLE resolution without finishing the merge**

```bash
git add src/core/ps1_hle_bios.h src/core/ps1_hle_bios.cpp \
        tests/test_ps1_omega_consolidation.cpp CMakeLists.txt
```

Expected: HLE conflicts disappear from `git diff --name-only --diff-filter=U`.

### Task 3: Merge `Ps1BootRuntime` around the canonical HLE owner

**Files:**
- Modify: `src/core/ps1_boot_runtime.h`
- Modify: `src/core/ps1_boot_runtime.cpp`
- Preserve: `tests/test_ps1_boot_runtime.cpp`
- Preserve: `tests/test_ps1_diagnostic_frontier.cpp`
- Preserve: `tests/test_ps1_cdrom_boot_runtime.cpp`
- Preserve: `tests/test_ps1_interrupt_continuation.cpp`

**Interfaces:**
- Consumes: canonical `Ps1HleBios`, `Ps1MemoryBus`, `Ps1InterruptContinuation`.
- Produces:

```cpp
struct Ps1DiagnosticMmioReadFrontier {
    std::uint32_t pc{};
    std::uint32_t opcode{};
    Ps1UnsupportedAccess access{};
};

class Ps1BootRuntime {
public:
    [[nodiscard]] bool apply_diagnostic_bios_fallback(Ps1BiosFallback fallback) noexcept;
    [[nodiscard]] const std::optional<Ps1DiagnosticMmioReadFrontier>&
        diagnostic_mmio_read_frontier() const noexcept;
    [[nodiscard]] bool apply_diagnostic_mmio_read_fallback(std::uint32_t value) noexcept;
    [[nodiscard]] std::uint64_t diagnostic_state_hash() const noexcept;
};
```

Ownership remains:

```cpp
Ps1MemoryBus bus_{};
R3000aState cpu_{};
Ps1HleBios bios_{};
Ps1InterruptContinuation interrupt_continuation_{};
bool diagnostic_bios_frontier_pending_{};
std::optional<Ps1DiagnosticMmioReadFrontier> diagnostic_mmio_read_frontier_{};
```

Use the member name `bios_` consistently to preserve current-main's explicit modular owner while importing OMEGA behavior.

- [ ] **Step 1: Resolve the runtime header with no BIOS semantic state duplicated**

The private section must not contain heap/interrupt/pad/root-counter/ISO state fields outside `Ps1HleBios`. Keep only runtime policy/frontier state.

- [ ] **Step 2: Route BIOS/SYS detection through `Ps1HleBiosCall`**

At each A0/B0/C0/SYS boundary, construct a call from architectural state:

```cpp
Ps1HleBiosCall call{};
call.domain = domain;
call.selector = selector;
call.pc = cpu_.pc;
call.a0 = cpu_.gpr[4];
call.a1 = cpu_.gpr[5];
call.a2 = cpu_.gpr[6];
call.a3 = cpu_.gpr[7];
call.ra = cpu_.gpr[31];
const auto result = bios_.dispatch(call, cpu_, bus_);
```

Disposition handling must remain explicit:

```cpp
switch (result.disposition) {
case Ps1HleBiosDisposition::handled:
    diagnostic_bios_frontier_pending_ = false;
    break;
case Ps1HleBiosDisposition::return_from_exception:
    // Delegate only to the already-implemented interrupt continuation path.
    break;
case Ps1HleBiosDisposition::terminal:
    // Preserve the concrete runtime/device stop produced by the bus/HLE operation.
    break;
case Ps1HleBiosDisposition::unsupported:
    diagnostic_bios_frontier_pending_ = true;
    report.stop_reason = Ps1BootStopReason::bios_call_unimplemented;
    return report;
}
```

Do not copy selector-specific semantics back into the runtime.

- [ ] **Step 3: Preserve diagnostic fallback separation**

`apply_diagnostic_bios_fallback()` and `apply_diagnostic_mmio_read_fallback()` remain runtime/MAX³ policy. They may mutate diagnostic continuation state, but must not expand production HLE coverage.

- [ ] **Step 4: Merge diagnostic hashing**

`Ps1BootRuntime::diagnostic_state_hash()` must hash CPU state, bus state, `bios_.diagnostic_state_hash()`, interrupt-continuation state when provided by the existing OMEGA implementation, and pending diagnostic frontier state. Do not require numeric equality with historical hashes; require determinism and state discrimination.

- [ ] **Step 5: Run runtime/frontier tests**

```bash
cmake --build build --target \
  jojo_ps1_boot_runtime_tests \
  jojo_ps1_diagnostic_frontier_tests \
  jojo_ps1_cdrom_boot_runtime_tests \
  jojo_ps1_interrupt_continuation_tests --parallel
ctest --test-dir build --output-on-failure -R "jojo_ps1_(boot_runtime|diagnostic_frontier|cdrom_boot_runtime|interrupt_continuation)_tests"
```

Expected: all pass, including one-shot diagnostic MMIO fallback and interrupt callback continuation behavior.

- [ ] **Step 6: Stage runtime resolution**

```bash
git add src/core/ps1_boot_runtime.h src/core/ps1_boot_runtime.cpp
```

### Task 4: Preserve OMEGA bus, GPU, CD-ROM, and GTE state exactly through the merge

**Files:**
- Modify/resolve: `src/core/ps1_memory_bus.h`
- Modify/resolve: `src/core/ps1_memory_bus.cpp`
- Preserve/add from OMEGA: `src/core/ps1_gpu_state.h`
- Preserve/add from OMEGA: `src/core/ps1_gpu_state.cpp`
- Preserve/add from OMEGA: `src/core/ps1_cdrom_state.h`
- Preserve/add from OMEGA: `src/core/ps1_cdrom_state.cpp`
- Preserve/add from OMEGA: `src/core/ps1_interrupt_continuation.h`
- Preserve/add from OMEGA: `src/core/ps1_interrupt_continuation.cpp`
- Modify/resolve: `src/core/r3000a_state.h`
- Modify/resolve: `src/core/r3000a_reference_executor.cpp`
- Test: `tests/test_ps1_memory_bus.cpp`
- Test: `tests/test_ps1_gpu_state.cpp`
- Test: `tests/test_ps1_gp1_boot_report.cpp`
- Test: `tests/test_ps1_cdrom_state.cpp`
- Test: `tests/test_ps1_cdrom_strict_widths.cpp`
- Test: `tests/test_r3000a_cop2_control.cpp`

**Interfaces:**
- Consumes: current main RAM/scratchpad/bus behavior plus OMEGA device mappings.
- Produces: the exact already-tested OMEGA MMIO/device/GTE surface; no new commands are added.

- [ ] **Step 1: Resolve bus conflicts in favor of the union, not one side**

The merged bus must keep current-main RAM/scratchpad behavior and OMEGA's existing mapped devices/diagnostic read override. Known supported MMIO routes to their device component. Unsupported accesses continue to populate `last_unsupported_access()` and return `R3000aBusStatus::unsupported`.

- [ ] **Step 2: Preserve one-shot diagnostic MMIO read identity**

The armed fallback remains bound to the exact blocked access identity. Wrong address, width, direction, PC, stale runtime state, or second consumption fails closed.

- [ ] **Step 3: Preserve OMEGA GTE/COP2 architectural state**

Keep the OMEGA `R3000aCop2Gte` control-register state in `R3000aState` and retain already-tested CTC2/CFC2 semantics. Do not add GTE mathematical commands in this phase.

- [ ] **Step 4: Run device and CPU-state regression suites**

```bash
cmake --build build --target \
  jojo_ps1_memory_bus_tests \
  jojo_ps1_gpu_state_tests \
  jojo_ps1_gp1_boot_report_tests \
  jojo_ps1_cdrom_state_tests \
  jojo_ps1_cdrom_strict_widths_tests \
  jojo_r3000a_cop2_control_tests \
  jojo_r3000a_boundary_tests --parallel
ctest --test-dir build --output-on-failure -R "jojo_ps1_(memory_bus|gpu_state|gp1_boot_report|cdrom_state|cdrom_strict_widths)_tests|jojo_r3000a_(cop2_control|boundary)_tests"
```

Expected: all pass.

- [ ] **Step 5: Stage device/CPU resolution**

```bash
git add src/core/ps1_memory_bus.h src/core/ps1_memory_bus.cpp \
        src/core/ps1_gpu_state.h src/core/ps1_gpu_state.cpp \
        src/core/ps1_cdrom_state.h src/core/ps1_cdrom_state.cpp \
        src/core/ps1_interrupt_continuation.h src/core/ps1_interrupt_continuation.cpp \
        src/core/r3000a_state.h src/core/r3000a_reference_executor.cpp
```

### Task 5: Preserve the complete OMEGA search architecture and CMake registration

**Files:**
- Preserve/add: `src/core/ps1_max3_explorer.h/.cpp`
- Preserve/add: `src/core/ps1_max3_candidate_engine.h/.cpp`
- Preserve/add: `src/core/ps1_max3_coverage.h/.cpp`
- Preserve/add: `src/core/ps1_max3_search_policy.h/.cpp`
- Preserve/add: `src/core/ps1_max3_budget.h/.cpp`
- Preserve/add: `src/core/ps1_max3_frontier_priority.h/.cpp`
- Preserve/add: `src/core/ps1_max3_report_io.cpp`
- Modify/resolve: `CMakeLists.txt`
- Test: `tests/test_ps1_max3_explorer.cpp`
- Test: `tests/test_ps1_max3_profiles.cpp`
- Test: `tests/test_ps1_max3_candidate_engine.cpp`
- Test: `tests/test_ps1_max3_coverage.cpp`
- Test: `tests/test_ps1_max3_search_policy.cpp`
- Test: `tests/test_ps1_max3_budget.cpp`
- Test: `tests/test_ps1_max3_deep_expansion.cpp`
- Test: `tests/test_ps1_max3_frontier_priority.cpp`

**Interfaces:**
- Consumes: `Ps1BootRuntime`, diagnostic BIOS/MMIO frontiers, device/report counters.
- Produces: unchanged OMEGA public profile and report contracts, including:

```cpp
enum class Ps1Max3Profile : std::uint8_t { strict, deep, omega };

[[nodiscard]] Ps1Max3Options ps1_max3_options(Ps1Max3Profile profile) noexcept;
[[nodiscard]] Ps1Max3Options ps1_max3_local_evidence_options() noexcept;
[[nodiscard]] Result<Ps1Max3Report> explore_ps1_max3(
    const Ps1Executable& executable,
    const Ps1Max3Options& options);
```

- [ ] **Step 1: Resolve `CMakeLists.txt` as a strict union**

Ensure `jojo_core` contains all of these OMEGA modules exactly once:

```cmake
src/core/ps1_cdrom_state.cpp
src/core/ps1_interrupt_continuation.cpp
src/core/ps1_gpu_state.cpp
src/core/ps1_hle_bios.cpp
src/core/ps1_max3_explorer.cpp
src/core/ps1_max3_candidate_engine.cpp
src/core/ps1_max3_coverage.cpp
src/core/ps1_max3_search_policy.cpp
src/core/ps1_max3_budget.cpp
src/core/ps1_max3_frontier_priority.cpp
src/core/ps1_max3_report_io.cpp
```

Ensure all OMEGA test targets from the source branch remain registered, plus `jojo_ps1_omega_consolidation_tests`.

- [ ] **Step 2: Verify profile defaults are unchanged during consolidation**

Do not enlarge OMEGA budgets here. `strict`, `deep`, and `omega` must retain the values from `8402c574...`; budget expansion belongs to a later Frame-First phase only if the approved spec requires it.

- [ ] **Step 3: Run the OMEGA module suites**

```bash
cmake --build build --target \
  jojo_ps1_max3_explorer_tests \
  jojo_ps1_max3_profile_tests \
  jojo_ps1_max3_candidate_engine_tests \
  jojo_ps1_max3_coverage_tests \
  jojo_ps1_max3_search_policy_tests \
  jojo_ps1_max3_budget_tests \
  jojo_ps1_max3_deep_expansion_tests \
  jojo_ps1_max3_frontier_priority_tests --parallel
ctest --test-dir build --output-on-failure -R "jojo_ps1_max3_"
```

Expected: all OMEGA suites pass with deterministic ordering and pruning.

- [ ] **Step 4: Stage OMEGA modules and CMake**

```bash
git add CMakeLists.txt src/core/ps1_max3_* tests/test_ps1_max3_*
```

### Task 6: Preserve report/runtime compatibility and the Windows checkpoint path

**Files:**
- Resolve if touched: `src/core/ps1_boot_report.h`
- Resolve if touched: `src/core/ps1_boot_report_io.cpp`
- Verify: `src/core/runtime.cpp`
- Verify: `src/app_win32/main.cpp`
- Test: `tests/test_ps1_boot_report_io.cpp`
- Test: `tests/test_ps1_local_evidence.cpp`
- Test: `tests/test_ps1_runtime_installation.cpp`

**Interfaces:**
- Consumes: consolidated OMEGA report and runtime.
- Produces: the existing installation-backed local-evidence API continues to call MAX³/OMEGA and returns `best_report` without changing commercial installation bytes.

Required runtime path remains conceptually:

```cpp
auto max3 = bootstrap_runtime_max3_local_evidence_to_file(
    install_root,
    report_path,
    ps1_max3_local_evidence_options());
if (!max3) {
    return Result<Ps1BootReport>::failure(max3.error, max3.detail);
}
return Result<Ps1BootReport>::success(std::move(max3.value.best_report));
```

- [ ] **Step 1: Resolve report fields additively**

Retain current-main report compatibility and all OMEGA counters/frontier fields already serialized by the source branch. Do not remove old keys merely because richer OMEGA structures exist.

- [ ] **Step 2: Verify the Windows UI still runs local evidence, not production boot**

`EXECUTAR CHECKPOINT` must continue to invoke the installation-backed diagnostic path. Do not repoint the button to `bootstrap_runtime()` or claim commercial playability.

- [ ] **Step 3: Run report/install/local-evidence tests**

```bash
cmake --build build --target \
  jojo_ps1_boot_report_io_tests \
  jojo_ps1_local_evidence_tests \
  jojo_ps1_runtime_installation_tests --parallel
ctest --test-dir build --output-on-failure -R "jojo_ps1_(boot_report_io|local_evidence|runtime_installation)_tests"
```

Expected: all pass and no test mutates the prepared installation unexpectedly.

- [ ] **Step 4: Stage compatibility resolutions**

```bash
git add src/core/ps1_boot_report.h src/core/ps1_boot_report_io.cpp \
        src/core/runtime.cpp src/app_win32/main.cpp tests/test_ps1_boot_report_io.cpp
```

Use `git add` only for paths actually modified by the merge.

### Task 7: Finish the semantic merge commit only after all targeted suites are green

**Files:**
- All staged consolidation files.
- No new hardware feature files beyond those already present on one of the two input heads.

**Interfaces:**
- Consumes: all resolved tasks above.
- Produces: one merge commit with first parent `8402c574...` lineage and second parent current `main`, preserving both histories.

- [ ] **Step 1: Confirm no unresolved conflicts remain**

```bash
test -z "$(git diff --name-only --diff-filter=U)"
git diff --check
```

Expected: no unresolved paths and no whitespace errors.

- [ ] **Step 2: Run the complete portable test suite before committing**

```bash
cmake --build build --parallel
ctest --test-dir build --output-on-failure
```

Expected: 100% pass on the local platform.

- [ ] **Step 3: Inspect scope**

```bash
git diff --stat 8402c574bba0b58fe107af66740349c43f6dcd31
git diff --stat fbde3f2a455845f062ab94d7f8db3040eb042889
```

Expected: differences are explainable as the semantic union plus the single consolidation sentinel test and design/plan documentation. No unrelated gameplay/mod/network/presentation refactor should appear.

- [ ] **Step 4: Create the merge commit**

```bash
git commit -m "merge: consolidate PS1 OMEGA with modular HLE main"
```

Expected: `git show --no-patch --format='%P' HEAD` contains two parent SHAs.

- [ ] **Step 5: Verify ancestry**

```bash
git merge-base --is-ancestor 8402c574bba0b58fe107af66740349c43f6dcd31 HEAD
git merge-base --is-ancestor fbde3f2a455845f062ab94d7f8db3040eb042889 HEAD
```

Expected: both commands exit 0.

### Task 8: Run authoritative Linux/Windows CI and verify the Windows artifact

**Files:**
- No source changes unless CI exposes a real portability regression.

**Interfaces:**
- Consumes: consolidation merge commit.
- Produces: a green cross-platform consolidation head and Windows artifact; this is the handoff point for Frame-First Phase B, not a first-frame claim.

- [ ] **Step 1: Push the consolidation branch**

```bash
git push -u origin feature/ps1-omega-frame-first-consolidation
```

- [ ] **Step 2: Verify Linux CI gates**

Required successful stages:

```text
Configure
Build
Production readiness gate
PS1 active architecture gate
Test
Observed disc revision contract
R2.5 direct UDP transport contract
```

Expected: all success.

- [ ] **Step 3: Verify Windows x64/MSVC 2022 gates**

Required successful stages:

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

Expected: all success.

- [ ] **Step 4: Verify the artifact**

Expected artifact name:

```text
JOJO-Recompiled-Windows-x64
```

The artifact must be produced from the exact consolidation head SHA.

- [ ] **Step 5: Record final consolidation evidence**

Record in the implementation report, not by changing commercial-readiness claims:

```text
consolidation_head=<sha>
linux_job=success
windows_job=success
windows_artifact=JOJO-Recompiled-Windows-x64
omega_source_ancestor=true
main_source_ancestor=true
```

- [ ] **Step 6: Stop at the Phase A boundary**

Do not implement new GPU/DMA/IRQ/CD/GTE behavior yet. The next approved plan must begin Frame-First Phase B from this green consolidated head.

## Plan Self-Review Results

- **Spec coverage:** This plan covers only the spec's required precondition: semantic consolidation of current `main` with the mature OMEGA line while preserving strict/diagnostic boundaries, deterministic hashing, tests, and cross-platform gates. Later Frame-First subsystem work is intentionally excluded and requires separate plans.
- **Placeholder scan:** No TBD/TODO/"implement later" placeholders are permitted. Every task has concrete files, commands, interfaces, and expected results.
- **Type consistency:** The canonical HLE API uses `Ps1HleBiosCall`, `Ps1HleBiosResult`, `Ps1HleBiosDisposition`, and `Ps1HleBiosDomain` consistently. `Ps1BootRuntime` owns one `Ps1HleBios bios_`; OMEGA consumes runtime frontiers rather than duplicating BIOS semantics.
- **Scope check:** This phase adds no new PS1 hardware semantics. Its terminal deliverable is a green semantic union suitable as the baseline for the next Frame-First plan.
