# PS1 OMEGA Frame-First Consolidation Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Consolidate current `main` with the mature OMEGA diagnostic line into one authoritative, fully tested PS1 runtime without adding new hardware semantics in this phase.

**Architecture:** Start from the mature OMEGA head because it already contains the broader GPU/GTE/CD-ROM/interrupt/MAX³ diagnostic architecture, then merge current `main` into it and resolve overlaps semantically. Preserve current `main`'s modular `Ps1HleBios` ownership boundary while retaining OMEGA's richer call/result contract, device state, interrupt continuation, deep/omega search infrastructure, deterministic hashing, and report compatibility. The phase ends only when targeted PS1 suites and the full Linux/Windows gates are green on the consolidated head.

**Tech Stack:** C++20, CMake 3.20+, CTest, Git, GitHub Actions, Win32/MSVC 2022, Linux portable core.

**Spec:** `docs/superpowers/specs/2026-09-11-ps1-omega-frame-first-design.md`

## Global Constraints

- `commercial_frame_presented` is a valid milestone only on a strict execution path using implemented CPU, BIOS/HLE, memory, and device semantics.
- Speculative OMEGA descendants may expose blockers but may never satisfy the first-frame milestone.
- Preserve the modular HLE ownership boundary from current `main`.
- Preserve the broader evidence-backed BIOS/kernel/device behavior from the mature OMEGA line.
- Diagnostic fallbacks remain runtime/MAX³ policy, outside production HLE semantics.
- Preserve deterministic diagnostic hashing and MAX³ deduplication.
- Preserve OMEGA coverage, candidates, budgets, priorities, CD-ROM, interrupt, GPU, and GTE tests.
- Add no new GP0, DMA, IRQ/timer, CD-ROM, GTE, SIO, SPU, or BIOS semantics in Phase A.
- Unsupported behavior remains explicit; unknown side-effectful hardware must never silently succeed.
- Linux and Windows x64 CI must both pass before consolidation is complete.
- Commercial-image evidence remains bounded and derived; never commit game images, BIOS ROMs, raw sectors, unrestricted RAM dumps, textures, or audio.

## Pinned Inputs

- `main`: `fbde3f2a455845f062ab94d7f8db3040eb042889`
- Mature OMEGA: `8402c574bba0b58fe107af66740349c43f6dcd31`
- Historical merge base: `0fa7a3c52d16c5c9a26e381fab667a744e5c1983`
- Implementation branch: `feature/ps1-omega-frame-first-consolidation`

## File Structure

- `src/core/ps1_hle_bios.h/.cpp` — sole BIOS A0/B0/C0/SYS semantic owner.
- `src/core/ps1_boot_runtime.h/.cpp` — CPU loop, BIOS/SYS detection, strict stops, diagnostics, interrupt continuation, telemetry.
- `src/core/ps1_memory_bus.h/.cpp` — RAM/MMIO routing, device ownership, unsupported-access evidence, one-shot diagnostic read override.
- `src/core/ps1_gpu_state.h/.cpp` — existing OMEGA GPU semantics; no expansion here.
- `src/core/ps1_cdrom_state.h/.cpp` — existing OMEGA CD-ROM semantics; no expansion here.
- `src/core/ps1_interrupt_continuation.h/.cpp` — existing OMEGA guest interrupt continuation.
- `src/core/r3000a_state.h`, `src/core/r3000a_reference_executor.cpp` — existing OMEGA GTE/COP2 state and transfers.
- `src/core/ps1_max3_*.h/.cpp` — OMEGA explorer, candidate engine, coverage, policy, budgets, priority, report I/O.
- `CMakeLists.txt` — canonical union of both development lines.
- `tests/test_ps1_omega_consolidation.cpp` — narrow integration sentinel proving modular HLE + OMEGA coexist in one build.

---

### Task 1: Create an isolated consolidation branch and reproduce the merge

**Files:**
- Carry into branch: `docs/superpowers/specs/2026-09-11-ps1-omega-frame-first-design.md`
- Carry into branch: `docs/superpowers/plans/2026-09-11-ps1-omega-frame-first-consolidation.md`

**Interfaces:**
- Consumes: pinned OMEGA and `main` commits.
- Produces: an in-progress semantic merge with both design documents present and all overlapping code conflicts visible.

- [ ] **Step 1: Create the worktree from the OMEGA head**

```bash
git fetch origin main feature/ps1-gp0-dma2-bios-frontier-v0 design/ps1-omega-frame-first
git worktree add ../jojo-omega-frame-first \
  -b feature/ps1-omega-frame-first-consolidation \
  8402c574bba0b58fe107af66740349c43f6dcd31
cd ../jojo-omega-frame-first
git rev-parse HEAD
```

Expected: `8402c574bba0b58fe107af66740349c43f6dcd31`.

- [ ] **Step 2: Verify the source OMEGA head before touching it**

```bash
cmake -S . -B build -DCMAKE_BUILD_TYPE=Debug
cmake --build build --parallel
ctest --test-dir build --output-on-failure
```

Expected: all source-branch tests pass. If the pinned source is already red, record that separately and do not blame consolidation.

- [ ] **Step 3: Carry the approved design and this plan into the implementation branch**

```bash
git checkout origin/design/ps1-omega-frame-first -- \
  docs/superpowers/specs/2026-09-11-ps1-omega-frame-first-design.md \
  docs/superpowers/plans/2026-09-11-ps1-omega-frame-first-consolidation.md
git add docs/superpowers/specs/2026-09-11-ps1-omega-frame-first-design.md \
        docs/superpowers/plans/2026-09-11-ps1-omega-frame-first-consolidation.md
git commit -m "docs: carry Omega frame-first consolidation design"
```

Expected: spec and plan now travel with the code branch.

- [ ] **Step 4: Start the semantic merge without committing**

```bash
git merge --no-commit --no-ff fbde3f2a455845f062ab94d7f8db3040eb042889
```

Expected: overlapping HLE/runtime/CMake/test files may conflict; non-overlapping OMEGA modules remain present.

- [ ] **Step 5: Record conflicts and confirm merge state**

```bash
git diff --name-only --diff-filter=U | sort
git rev-parse -q --verify MERGE_HEAD
```

Expected: `MERGE_HEAD` resolves successfully. Do not use wholesale `--ours`/`--theirs` for `ps1_hle_bios.*`, `ps1_boot_runtime.*`, `CMakeLists.txt`, or PS1 regression tests.

### Task 2: Resolve the canonical HLE boundary with a RED→GREEN sentinel

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

Canonical merged HLE API:

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
```

The OMEGA call/result API is the canonical superset. Current-main behavior for A0/39, A0/56, A0/72, B0/19, B0/5B, and C0/0A remains required.

- [ ] **Step 1: Add the consolidation sentinel**

Create `tests/test_ps1_omega_consolidation.cpp`:

```cpp
#include "core/ps1_hle_bios.h"
#include "core/ps1_memory_bus.h"

#include <cassert>

int main() {
    jojo::Ps1HleBios bios;
    jojo::Ps1MemoryBus bus;
    jojo::R3000aState cpu{};
    cpu.gpr[31] = 0x80012000u;

    jojo::Ps1HleBiosCall call{};
    call.domain = jojo::Ps1HleBiosDomain::a0;
    call.selector = 0x39u;
    call.a0 = 0x80040000u;
    call.a1 = 0x1000u;
    call.ra = cpu.gpr[31];

    const auto handled = bios.dispatch(call, cpu, bus);
    assert(handled.disposition == jojo::Ps1HleBiosDisposition::handled);
    assert(bios.heap_state().has_value());
    assert(bios.heap_state()->base == 0x80040000u);
    assert(bios.heap_state()->size == 0x1000u);
    assert(cpu.pc == 0x80012000u);
    assert(cpu.gpr[0] == 0u);

    const auto hash = bios.diagnostic_state_hash();
    call.selector = 0xFFFFu;
    const auto unsupported = bios.dispatch(call, cpu, bus);
    assert(unsupported.disposition == jojo::Ps1HleBiosDisposition::unsupported);
    assert(bios.diagnostic_state_hash() == hash);
    return 0;
}
```

Register:

```cmake
add_jojo_test(jojo_ps1_omega_consolidation_tests tests/test_ps1_omega_consolidation.cpp)
```

- [ ] **Step 2: Build to verify the unresolved merge is RED**

```bash
cmake -S . -B build -DCMAKE_BUILD_TYPE=Debug
cmake --build build --target jojo_ps1_omega_consolidation_tests --parallel
```

Expected: FAIL until the canonical merged HLE interface is resolved.

- [ ] **Step 3: Resolve HLE to the modular superset**

Keep a single `Ps1HleBios` owner with both overloads:

```cpp
[[nodiscard]] Ps1HleBiosResult dispatch(
    const Ps1HleBiosCall& call,
    R3000aState& cpu) noexcept;
[[nodiscard]] Ps1HleBiosResult dispatch(
    const Ps1HleBiosCall& call,
    R3000aState& cpu,
    Ps1MemoryBus& bus) noexcept;
```

Retain current-main accessors and OMEGA additions:

```cpp
[[nodiscard]] const std::optional<Ps1BiosHeapState>& heap_state() const noexcept;
[[nodiscard]] const std::optional<std::uint32_t>& interrupt_hook_address() const noexcept;
[[nodiscard]] std::optional<std::uint32_t> interrupt_priority_head(std::uint32_t priority) const noexcept;
[[nodiscard]] const std::optional<bool>& pad_card_auto_ack_enabled() const noexcept;
[[nodiscard]] std::optional<bool> root_counter_auto_ack_enabled(std::uint32_t counter) const noexcept;
[[nodiscard]] bool iso9660_removed() const noexcept;
```

A0/39 remains:

```cpp
case 0x39u:
    heap_state_ = Ps1BiosHeapState{call.a0, call.a1};
    return_from_bios_vector(cpu);
    return {Ps1HleBiosDisposition::handled};
```

Unknown selectors return `unsupported` without state mutation. Bus-dependent services invoked through the CPU-only overload return `unsupported`, not fabricated success.

- [ ] **Step 4: Run HLE regression suites**

```bash
cmake --build build --target \
  jojo_ps1_omega_consolidation_tests \
  jojo_ps1_hle_bios_tests \
  jojo_ps1_kernel_hle_frontier_tests \
  jojo_ps1_96_remove_tests \
  jojo_ps1_changeclearrcnt_tests --parallel
ctest --test-dir build --output-on-failure \
  -R "jojo_ps1_(omega_consolidation|hle_bios|kernel_hle_frontier|96_remove|changeclearrcnt)_tests"
```

Expected: PASS.

- [ ] **Step 5: Stage the HLE resolution**

```bash
git add src/core/ps1_hle_bios.h src/core/ps1_hle_bios.cpp \
        tests/test_ps1_omega_consolidation.cpp CMakeLists.txt
```

### Task 3: Resolve `Ps1BootRuntime` while keeping policy separate from HLE

**Files:**
- Modify: `src/core/ps1_boot_runtime.h`
- Modify: `src/core/ps1_boot_runtime.cpp`
- Preserve: `tests/test_ps1_boot_runtime.cpp`
- Preserve: `tests/test_ps1_diagnostic_frontier.cpp`
- Preserve: `tests/test_ps1_cdrom_boot_runtime.cpp`
- Preserve: `tests/test_ps1_interrupt_continuation.cpp`

**Interfaces:**

Merged runtime keeps:

```cpp
struct Ps1DiagnosticMmioReadFrontier {
    std::uint32_t pc{};
    std::uint32_t opcode{};
    Ps1UnsupportedAccess access{};
};
```

Private ownership:

```cpp
Ps1MemoryBus bus_{};
R3000aState cpu_{};
Ps1HleBios bios_{};
Ps1InterruptContinuation interrupt_continuation_{};
bool diagnostic_bios_frontier_pending_{};
std::optional<Ps1DiagnosticMmioReadFrontier> diagnostic_mmio_read_frontier_{};
```

- [ ] **Step 1: Resolve the header with no duplicated BIOS semantic fields**

Do not keep heap/interrupt/pad/root-counter/ISO fields in `Ps1BootRuntime`; they remain inside `Ps1HleBios` and public runtime accessors forward to `bios_`.

- [ ] **Step 2: Route BIOS/SYS boundaries through the call object**

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

Handle `handled`, `unsupported`, `terminal`, and `return_from_exception` explicitly. Selector semantics must not move back into the runtime.

- [ ] **Step 3: Preserve diagnostic-only continuations**

Keep `apply_diagnostic_bios_fallback()` and `apply_diagnostic_mmio_read_fallback()` as MAX³/runtime policy. They do not extend production HLE coverage.

- [ ] **Step 4: Merge runtime hashing**

`diagnostic_state_hash()` incorporates CPU, bus, `bios_.diagnostic_state_hash()`, interrupt-continuation state when already supported by OMEGA, and pending diagnostic frontier state. Require determinism/state discrimination, not historical numeric equality.

- [ ] **Step 5: Run runtime/frontier suites**

```bash
cmake --build build --target \
  jojo_ps1_boot_runtime_tests \
  jojo_ps1_diagnostic_frontier_tests \
  jojo_ps1_cdrom_boot_runtime_tests \
  jojo_ps1_interrupt_continuation_tests --parallel
ctest --test-dir build --output-on-failure \
  -R "jojo_ps1_(boot_runtime|diagnostic_frontier|cdrom_boot_runtime|interrupt_continuation)_tests"
```

Expected: PASS.

- [ ] **Step 6: Stage runtime resolution**

```bash
git add src/core/ps1_boot_runtime.h src/core/ps1_boot_runtime.cpp
```

### Task 4: Preserve the existing OMEGA device and CPU architecture

**Files:**
- Resolve: `src/core/ps1_memory_bus.h/.cpp`
- Preserve: `src/core/ps1_gpu_state.h/.cpp`
- Preserve: `src/core/ps1_cdrom_state.h/.cpp`
- Preserve: `src/core/ps1_interrupt_continuation.h/.cpp`
- Resolve: `src/core/r3000a_state.h`
- Resolve: `src/core/r3000a_reference_executor.cpp`
- Test: `tests/test_ps1_memory_bus.cpp`
- Test: `tests/test_ps1_gpu_state.cpp`
- Test: `tests/test_ps1_gp1_boot_report.cpp`
- Test: `tests/test_ps1_cdrom_state.cpp`
- Test: `tests/test_ps1_cdrom_strict_widths.cpp`
- Test: `tests/test_r3000a_cop2_control.cpp`

**Interfaces:**
- Consumes: current RAM/scratchpad behavior plus already-implemented OMEGA device mappings.
- Produces: the exact OMEGA MMIO/GPU/CD-ROM/GTE behavior already covered by tests; no new commands.

- [ ] **Step 1: Resolve the bus as a semantic union**

Keep current-main RAM/scratchpad behavior and OMEGA's existing device routes plus diagnostic one-shot read override. Supported addresses route to real existing components; unsupported accesses record evidence and return `R3000aBusStatus::unsupported`.

- [ ] **Step 2: Preserve one-shot MMIO fallback identity**

Wrong address, width, direction, PC, stale state, or second consumption fails closed.

- [ ] **Step 3: Preserve existing GTE/COP2 state only**

Keep OMEGA control-register state and tested CTC2/CFC2 behavior. Do not add mathematical GTE commands in Phase A.

- [ ] **Step 4: Run device/CPU regression suites**

```bash
cmake --build build --target \
  jojo_ps1_memory_bus_tests \
  jojo_ps1_gpu_state_tests \
  jojo_ps1_gp1_boot_report_tests \
  jojo_ps1_cdrom_state_tests \
  jojo_ps1_cdrom_strict_widths_tests \
  jojo_r3000a_cop2_control_tests \
  jojo_r3000a_boundary_tests --parallel
ctest --test-dir build --output-on-failure \
  -R "jojo_ps1_(memory_bus|gpu_state|gp1_boot_report|cdrom_state|cdrom_strict_widths)_tests|jojo_r3000a_(cop2_control|boundary)_tests"
```

Expected: PASS.

- [ ] **Step 5: Stage the device/CPU resolution**

```bash
git add src/core/ps1_memory_bus.h src/core/ps1_memory_bus.cpp \
        src/core/ps1_gpu_state.h src/core/ps1_gpu_state.cpp \
        src/core/ps1_cdrom_state.h src/core/ps1_cdrom_state.cpp \
        src/core/ps1_interrupt_continuation.h src/core/ps1_interrupt_continuation.cpp \
        src/core/r3000a_state.h src/core/r3000a_reference_executor.cpp
```

### Task 5: Preserve the complete OMEGA search architecture and CMake union

**Files:**
- Preserve: `src/core/ps1_max3_explorer.h/.cpp`
- Preserve: `src/core/ps1_max3_candidate_engine.h/.cpp`
- Preserve: `src/core/ps1_max3_coverage.h/.cpp`
- Preserve: `src/core/ps1_max3_search_policy.h/.cpp`
- Preserve: `src/core/ps1_max3_budget.h/.cpp`
- Preserve: `src/core/ps1_max3_frontier_priority.h/.cpp`
- Preserve: `src/core/ps1_max3_report_io.cpp`
- Resolve: `CMakeLists.txt`

**Interfaces:**

Keep:

```cpp
enum class Ps1Max3Profile : std::uint8_t { strict, deep, omega };
[[nodiscard]] Ps1Max3Options ps1_max3_options(Ps1Max3Profile profile) noexcept;
[[nodiscard]] Ps1Max3Options ps1_max3_local_evidence_options() noexcept;
[[nodiscard]] Result<Ps1Max3Report> explore_ps1_max3(
    const Ps1Executable& executable,
    const Ps1Max3Options& options);
```

- [ ] **Step 1: Resolve `CMakeLists.txt` as a strict union**

`jojo_core` must contain each OMEGA module exactly once:

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

Keep all OMEGA test targets plus `jojo_ps1_omega_consolidation_tests`.

- [ ] **Step 2: Freeze profile budgets during consolidation**

Do not enlarge strict/deep/omega budgets in this phase. Preserve the source-OMEGA values from `8402c574...`.

- [ ] **Step 3: Run OMEGA suites**

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

Expected: PASS with deterministic ordering/pruning.

- [ ] **Step 4: Stage OMEGA/CMake resolution**

```bash
git add CMakeLists.txt src/core/ps1_max3_* tests/test_ps1_max3_*
```

### Task 6: Preserve report, installation, and Windows checkpoint compatibility

**Files:**
- Resolve if touched: `src/core/ps1_boot_report.h`
- Resolve if touched: `src/core/ps1_boot_report_io.cpp`
- Verify: `src/core/runtime.cpp`
- Verify: `src/app_win32/main.cpp`
- Test: `tests/test_ps1_boot_report_io.cpp`
- Test: `tests/test_ps1_local_evidence.cpp`
- Test: `tests/test_ps1_runtime_installation.cpp`

**Interfaces:**

The local-evidence path remains:

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

Retain current report compatibility and existing OMEGA counters/frontier serialization. Do not remove old keys because richer structures exist.

- [ ] **Step 2: Verify `EXECUTAR CHECKPOINT` remains diagnostic**

The Win32 action continues to invoke installation-backed local evidence. Do not redirect it to `bootstrap_runtime()` or claim playability.

- [ ] **Step 3: Run compatibility suites**

```bash
cmake --build build --target \
  jojo_ps1_boot_report_io_tests \
  jojo_ps1_local_evidence_tests \
  jojo_ps1_runtime_installation_tests --parallel
ctest --test-dir build --output-on-failure \
  -R "jojo_ps1_(boot_report_io|local_evidence|runtime_installation)_tests"
```

Expected: PASS; prepared installation contents remain unmodified except documented diagnostic outputs.

- [ ] **Step 4: Stage only paths actually changed**

```bash
git add src/core/ps1_boot_report.h src/core/ps1_boot_report_io.cpp \
        src/core/runtime.cpp src/app_win32/main.cpp tests/test_ps1_boot_report_io.cpp
```

If a listed path has no merge change, omit it from `git add`.

### Task 7: Complete the merge only after the semantic union is green

**Files:**
- All staged consolidation files.
- No new hardware-feature implementation files beyond those already present in one of the input heads.

**Interfaces:**
- Produces: one merge commit preserving both source histories.

- [ ] **Step 1: Confirm no unresolved conflicts or conflict markers remain**

```bash
test -z "$(git diff --name-only --diff-filter=U)"
git diff --check
```

Expected: both commands succeed.

- [ ] **Step 2: Run the complete local suite before committing**

```bash
cmake --build build --parallel
ctest --test-dir build --output-on-failure
```

Expected: 100% pass.

- [ ] **Step 3: Review scope from both source heads**

```bash
git diff --stat 8402c574bba0b58fe107af66740349c43f6dcd31
git diff --stat fbde3f2a455845f062ab94d7f8db3040eb042889
```

Expected: changes are explainable as the semantic union, design/plan docs, and the consolidation sentinel. No unrelated network/mod/presentation refactor.

- [ ] **Step 4: Create the merge commit**

```bash
git commit -m "merge: consolidate PS1 OMEGA with modular HLE main"
```

- [ ] **Step 5: Verify both source heads are ancestors**

```bash
git merge-base --is-ancestor 8402c574bba0b58fe107af66740349c43f6dcd31 HEAD
git merge-base --is-ancestor fbde3f2a455845f062ab94d7f8db3040eb042889 HEAD
git show --no-patch --format='%P' HEAD
```

Expected: both ancestry checks exit 0 and the merge commit has two parents.

### Task 8: Run authoritative Linux/Windows CI and verify the Windows artifact

**Files:**
- No source changes unless CI exposes a real portability regression.

**Interfaces:**
- Produces: green Phase A consolidation head; handoff baseline for the next Frame-First plan.

- [ ] **Step 1: Push the branch**

```bash
git push -u origin feature/ps1-omega-frame-first-consolidation
```

- [ ] **Step 2: Require Linux success**

```text
Configure
Build
Production readiness gate
PS1 active architecture gate
Test
Observed disc revision contract
R2.5 direct UDP transport contract
```

- [ ] **Step 3: Require Windows x64/MSVC 2022 success**

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

- [ ] **Step 4: Verify artifact identity**

Expected artifact:

```text
JOJO-Recompiled-Windows-x64
```

It must be produced from the exact consolidation head SHA.

- [ ] **Step 5: Record evidence**

```text
consolidation_head=<exact SHA>
linux_job=success
windows_job=success
windows_artifact=JOJO-Recompiled-Windows-x64
omega_source_ancestor=true
main_source_ancestor=true
```

- [ ] **Step 6: Stop at the Phase A boundary**

Do not add new GPU/DMA/IRQ/CD/GTE behavior. The next implementation plan starts from this green consolidated head and addresses Frame-First observability/gating or the highest strict blocker defined by the approved spec.

## Plan Self-Review Results

- **Spec coverage:** Phase A covers the spec's required consolidation precondition: current `main` + mature OMEGA, modular HLE ownership, strict/diagnostic separation, deterministic hashing, regression preservation, and Linux/Windows gates. Frame-first hardware expansion is intentionally deferred to later plans.
- **Placeholder scan:** No TBD/TODO/"implement later" placeholder exists; commands, interfaces, tests, and expected outcomes are explicit.
- **Type consistency:** `Ps1HleBiosCall`, `Ps1HleBiosResult`, `Ps1HleBiosDisposition`, and `Ps1HleBiosDomain` are canonical. `Ps1BootRuntime` owns exactly one `Ps1HleBios bios_`; OMEGA consumes runtime frontiers rather than duplicating BIOS semantics.
- **Scope check:** No new PS1 hardware semantics are introduced. The terminal deliverable is a green semantic union suitable for Frame-First Phase B.
