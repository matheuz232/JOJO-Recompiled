# PS1 MAX³ Deep Frontier v1 Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Extend MAX³ so one diagnostic checkpoint can discover multiple downstream read-only MMIO/BIOS frontiers while preserving strict evidence, deterministic provenance, and production MMIO semantics.

**Architecture:** Add a diagnostic-only, one-shot MMIO read override to `Ps1MemoryBus`, expose it through an exact-frontier API on `Ps1BootRuntime`, then teach `Ps1Max3Explorer` to branch deterministically over BIOS fallbacks and unsupported read fallbacks. Keep legacy dependencies for compatibility, add an explicit strict/speculative frontier graph and typed decisions, and serialize the result as `jojo-max3-checkpoint-v2`.

**Tech Stack:** C++20, CMake, R3000A reference executor, synthetic PS1 executable fixtures, GitHub Actions Linux + Windows/MSVC 2022.

**Spec:** `docs/superpowers/specs/2026-09-10-ps1-max3-deep-frontier-v1-design.md`

## Global Constraints

- Branch: `feature/ps1-gp0-dma2-bios-frontier-v0`.
- Design base: `c44060e31712b9e539e7f7ed9a06015e6f81886f`.
- Do not add proprietary game, BIOS, RAM, sector, audio, image, or asset data to source, tests, CI artifacts, or reports.
- Production MMIO behavior remains strict. Diagnostic fallback state exists only to resume one exact unsupported read.
- Deep v1 may branch only on existing BIOS fallbacks and unsupported read-only `LB`, `LBU`, `LH`, `LHU`, or `LW` MMIO boundaries.
- Do not add diagnostic continuation for unsupported writes, device commands, GPU writes, DMA transfers, new CD-ROM commands, CPU architectural boundaries, unreadable callbacks, or nested-interrupt boundaries.
- MMIO fallback order is deterministic: `0`, `1`, width-masked all-ones.
- BIOS fallback order remains deterministic: `return_zero`, `return_one`, `return_minus_one`, `preserve_v0`.
- `deep_frontier_enabled` defaults to `false`; user checkpoint options enable it.
- Deep user checkpoint limits are `max_unique_frontiers=32`, `max_branch_depth=8`, `max_speculative_depth=8`; existing `max_nodes` and `max_total_retired` remain hard global bounds.
- `max_speculative_depth` is path-local. Reaching it stops only that node's expansion.
- `max_unique_frontiers` is global. Report `frontier_limit` only when the cap leaves otherwise-expandable work unexplored.
- Existing `dependency_count` and dependency records remain semantically compatible; new frontier records do not repurpose them.
- Speculative discoveries never become production evidence merely because they progress farther.
- Windows/MSVC 2022 is the authority for the user-facing executable.
- Every production change follows TDD RED → GREEN. Unexpected failures use systematic-debugging before changing production.

## File Map

- `src/core/ps1_memory_bus.h/.cpp` — owns the one-shot read override and hashes armed diagnostic state.
- `src/core/ps1_boot_runtime.h/.cpp` — recognizes an expandable blocked load, validates exact-frontier identity, arms the bus override, and re-executes through the normal R3000A executor.
- `src/core/ps1_max3_explorer.h/.cpp` — owns deep-mode options, evidence/provenance data model, deterministic frontier registration, branching, limits, deduplication, and ranking.
- `src/core/ps1_max3_report_io.cpp` — serializes `jojo-max3-checkpoint-v2` deterministically.
- `tests/test_ps1_memory_bus.cpp` — raw override identity/consumption/hash tests.
- `tests/test_ps1_diagnostic_frontier.cpp` — runtime re-execution, sign extension, load delay, exact-frontier rejection, and canonical-state tests.
- `tests/test_ps1_max3_explorer.cpp` — strict compatibility, deep branching, provenance, terminal writes, limits, deduplication, ranking, and deterministic repeat tests.
- `tests/test_ps1_boot_report_io.cpp` — v2 serialization contract.

---

### Task 1: One-shot diagnostic MMIO read override in `Ps1MemoryBus`

**Files:**
- Modify: `src/core/ps1_memory_bus.h`
- Modify: `src/core/ps1_memory_bus.cpp`
- Test: `tests/test_ps1_memory_bus.cpp`

**Interfaces:**
- Consumes: `Ps1UnsupportedAccess`, `R3000aBusResult`, existing `diagnostic_state_hash()`.
- Produces:
  ```cpp
  struct Ps1DiagnosticMmioReadOverride {
      std::uint32_t guest_address{};
      std::uint32_t physical_address{};
      std::uint8_t width{};
      std::uint32_t value{};
  };

  [[nodiscard]] bool arm_diagnostic_mmio_read_override(
      const Ps1UnsupportedAccess& access,
      std::uint32_t value) noexcept;
  [[nodiscard]] const std::optional<Ps1DiagnosticMmioReadOverride>&
      diagnostic_mmio_read_override() const noexcept;
  ```
- Private helper:
  ```cpp
  [[nodiscard]] std::optional<R3000aBusResult> take_diagnostic_mmio_read_override(
      std::uint32_t guest_address,
      std::uint32_t physical_address,
      std::uint8_t width) noexcept;
  ```
- Invariant: supported hardware/register handling executes before the override helper; the helper is checked only on paths that would otherwise become unsupported or legacy diagnostic-probe reads.

- [ ] **Step 1: Write RED tests for exact-match consumption and masking**

Add tests that arm an unsupported byte read at `0x1F801802` with value `0x12345680`, then require:

```cpp
jojo::Ps1MemoryBus bus;
const jojo::Ps1UnsupportedAccess blocked{
    0x1F801802u, 0x1F801802u, 1u, false, 0u,
};
CHECK(bus.arm_diagnostic_mmio_read_override(blocked, 0x12345680u));
CHECK(bus.diagnostic_mmio_read_override().has_value());

const auto first = bus.read8(0x1F801802u);
CHECK(first.status == jojo::R3000aBusStatus::ok);
CHECK(first.value == 0x80u);
CHECK(!bus.diagnostic_mmio_read_override().has_value());

const auto second = bus.read8(0x1F801802u);
CHECK(second.status == jojo::R3000aBusStatus::unsupported);
```

Add equivalent width masking assertions for a 16-bit override (`0x1F801800`, fallback `0x12348001` → `0x8001`) and a 32-bit override (`0x1F801800`, fallback unchanged).

Also require `arm_diagnostic_mmio_read_override` to return `false` for `write=true` and for widths other than `1`, `2`, or `4`.

- [ ] **Step 2: Write RED tests that mismatched accesses do not consume the override**

Arm a byte override for `0x1F801802`, perform a different supported read (`read32(0x1F801070)`), then verify the I_STAT result is unchanged and the override remains armed. Perform a wrong-width read of the same physical address and verify it does not consume the byte override. Finally perform the matching byte read and verify it consumes exactly once.

- [ ] **Step 3: Write RED hash/canonical-state tests**

Create identical buses `a` and `b`; require equal hashes. Arm `a` with fallback `0` and `b` with fallback `1`; require different hashes. Consume both matching overrides and require their hashes to converge again when all other state is identical.

- [ ] **Step 4: Run the focused RED test**

Run:
```bash
cmake -S . -B build -DCMAKE_BUILD_TYPE=Release
cmake --build build --parallel 2
ctest --test-dir build --output-on-failure -R '^jojo_ps1_memory_bus_tests$'
```
Expected: `jojo_ps1_memory_bus_tests` fails because the new override API/state does not exist.

Commit the RED only:
```bash
git add tests/test_ps1_memory_bus.cpp
git commit -m "test: define PS1 diagnostic MMIO read override RED"
```

- [ ] **Step 5: Implement the minimal bus state and arm contract**

Add `Ps1DiagnosticMmioReadOverride` and one optional member:
```cpp
std::optional<Ps1DiagnosticMmioReadOverride> diagnostic_mmio_read_override_{};
```

Implement `arm_diagnostic_mmio_read_override` so it:

```cpp
if (access.write) return false;
if (access.width != 1u && access.width != 2u && access.width != 4u) return false;
const auto physical = guest_to_physical(access.guest_address);
if (!physical || *physical != access.physical_address) return false;

std::uint32_t masked = value;
if (access.width == 1u) masked &= 0xFFu;
if (access.width == 2u) masked &= 0xFFFFu;
diagnostic_mmio_read_override_ = Ps1DiagnosticMmioReadOverride{
    access.guest_address,
    access.physical_address,
    access.width,
    masked,
};
return true;
```

- [ ] **Step 6: Consume the override only on an exact unsupported-read identity**

Implement `take_diagnostic_mmio_read_override` to compare guest address, physical address, and width. On exact match, copy the fallback value, clear the optional, and return `{R3000aBusStatus::ok, value}`. On mismatch, return `std::nullopt` without mutation.

In `read8`, `read16`, and `read32`:

1. preserve all currently supported RAM/scratchpad/device/register branches first;
2. before returning `unsupported` for a read, call the helper;
3. before the legacy diagnostic-shadow probe handles an otherwise unknown MMIO read, call the helper;
4. never invoke the helper from any write method.

For the CD-ROM strict branches (`0x1F801802` byte data and unsupported 16/32-bit CD register widths), invoke the helper immediately before storing `last_unsupported_` and returning unsupported.

- [ ] **Step 7: Hash the armed override and canonical empty state**

Extend `Ps1MemoryBus::diagnostic_state_hash()` with a presence byte. When armed, hash guest address, physical address, width, and fallback value. When consumed, no stale fields remain because the optional is reset.

- [ ] **Step 8: Run GREEN and the neighboring CD-ROM tests**

Run:
```bash
cmake --build build --parallel 2
ctest --test-dir build --output-on-failure -R 'jojo_ps1_(memory_bus|cdrom_state|cdrom_strict_widths)_tests'
```
Expected: all selected tests pass.

Commit:
```bash
git add src/core/ps1_memory_bus.h src/core/ps1_memory_bus.cpp tests/test_ps1_memory_bus.cpp
git commit -m "feat: add one-shot PS1 diagnostic MMIO read override"
```

**Review gate:** verify no write path consumes an override and no supported register branch was reordered behind the diagnostic helper.

---

### Task 2: Exact runtime frontier API and normal R3000A load re-execution

**Files:**
- Modify: `src/core/ps1_boot_runtime.h`
- Modify: `src/core/ps1_boot_runtime.cpp`
- Test: `tests/test_ps1_diagnostic_frontier.cpp`

**Interfaces:**
- Consumes: Task 1 `Ps1MemoryBus::arm_diagnostic_mmio_read_override`.
- Produces:
  ```cpp
  struct Ps1DiagnosticMmioReadFrontier {
      std::uint32_t pc{};
      std::uint32_t opcode{};
      Ps1UnsupportedAccess access{};
  };

  [[nodiscard]] const std::optional<Ps1DiagnosticMmioReadFrontier>&
      diagnostic_mmio_read_frontier() const noexcept;
  [[nodiscard]] bool apply_diagnostic_mmio_read_fallback(
      std::uint32_t value) noexcept;
  ```
- Runtime-private state:
  ```cpp
  std::optional<Ps1DiagnosticMmioReadFrontier> diagnostic_mmio_read_frontier_{};
  ```
- Supported expandable load opcodes in v1: `LB`, `LBU`, `LH`, `LHU`, `LW` only. `LWL`/`LWR` are not expanded in v1.

- [ ] **Step 1: Write RED runtime tests for an unsupported `LBU` frontier**

Build a synthetic executable that loads from CD-ROM data port `0x1F801802` with `LBU`, followed by two instructions that expose load delay. Disable legacy diagnostic probe in the segment options.

First run must stop with:
```cpp
CHECK(report.stop_reason == jojo::Ps1BootStopReason::mmio_unimplemented);
CHECK(runtime.diagnostic_mmio_read_frontier().has_value());
CHECK(runtime.diagnostic_mmio_read_frontier()->pc == report.last_pc);
CHECK(runtime.diagnostic_mmio_read_frontier()->access.physical_address == 0x1F801802u);
CHECK(runtime.diagnostic_mmio_read_frontier()->access.width == 1u);
CHECK(!runtime.diagnostic_mmio_read_frontier()->access.write);
```

The destination GPR must still contain its pre-load value and the blocked load must not have created a new pending load.

- [ ] **Step 2: Write RED tests for `LB`, `LBU`, `LH`, `LHU`, and `LW` semantics**

Use these fallback values:

- `LB`: `0x80` → eventual GPR value `0xFFFFFF80`;
- `LBU`: `0x80` → `0x00000080`;
- `LH`: `0x8001` → `0xFFFF8001`;
- `LHU`: `0x8001` → `0x00008001`;
- `LW`: `0x89ABCDEF` → `0x89ABCDEF`.

For byte tests use `0x1F801802`; for half/word tests use unsupported CD-ROM register widths at `0x1F801800`. After applying the fallback, run enough instructions to prove the normal load-delay rule: the immediately following instruction observes the old destination value, and the next instruction observes the loaded value.

- [ ] **Step 3: Write RED exact-frontier rejection tests**

Require:

```cpp
CHECK(runtime.apply_diagnostic_mmio_read_fallback(0u));
CHECK(!runtime.apply_diagnostic_mmio_read_fallback(1u));
```

A runtime that stopped on an unsupported write must expose no read frontier and reject the fallback. A runtime stopped on a CPU boundary or BIOS frontier must also reject it.

Copy a stopped runtime twice, apply fallback `0` to one copy and `1` to the other, and require different `diagnostic_state_hash()` values while the bus overrides are armed.

- [ ] **Step 4: Run focused RED**

Run:
```bash
cmake --build build --parallel 2
ctest --test-dir build --output-on-failure -R '^jojo_ps1_diagnostic_frontier_tests$'
```
Expected: failure because the runtime frontier API is absent.

Commit RED:
```bash
git add tests/test_ps1_diagnostic_frontier.cpp
git commit -m "test: define PS1 MMIO read frontier continuation RED"
```

- [ ] **Step 5: Recognize only provable read-only load boundaries**

Include the MIPS decoder in `ps1_boot_runtime.cpp`. Add a helper that returns true only for decoded ops `MipsOp::lb`, `lbu`, `lh`, `lhu`, or `lw` and confirms the expected width (`1`, `1`, `2`, `2`, `4`).

When a step ends in an initial-MMIO `mmio_unimplemented` boundary:

```cpp
if (report.unsupported_access &&
    !report.unsupported_access->write &&
    report.last_opcode &&
    expandable_mmio_load(*report.last_opcode, report.unsupported_access->width)) {
    diagnostic_mmio_read_frontier_ = Ps1DiagnosticMmioReadFrontier{
        report.last_pc,
        *report.last_opcode,
        *report.unsupported_access,
    };
} else {
    diagnostic_mmio_read_frontier_.reset();
}
```

Reset this frontier on successful retirement, BIOS handling, interrupt continuation, device-command terminal paths, GPU-command terminal paths, and any nonmatching terminal boundary so stale evidence cannot be applied later.

- [ ] **Step 6: Implement exact-frontier fallback arming without direct register patching**

`apply_diagnostic_mmio_read_fallback(value)` must:

1. require `diagnostic_mmio_read_frontier_`;
2. require `cpu_.pc == frontier.pc`;
3. fetch the instruction word from `frontier.pc` and require it still equals `frontier.opcode`;
4. require `guest_to_physical(frontier.access.guest_address) == frontier.access.physical_address`;
5. require `frontier.access.write == false` and the decoded opcode/width pair to remain expandable;
6. call `bus_.arm_diagnostic_mmio_read_override(frontier.access, value)`;
7. clear `diagnostic_mmio_read_frontier_` only after successful arming;
8. leave all GPRs, pending load, PC, next PC, and delay-slot state untouched.

The next `run()` re-executes the blocked load through `step_r3000a`, so sign extension and load delay come from the existing executor.

- [ ] **Step 7: Include pending-frontier identity in runtime hash**

Extend `Ps1BootRuntime::diagnostic_state_hash()` with a presence byte and, when pending, `pc`, `opcode`, guest/physical address, width, and read/write bit. The fallback value itself is hashed by the armed bus state from Task 1.

- [ ] **Step 8: Run GREEN plus executor regressions**

Run:
```bash
cmake --build build --parallel 2
ctest --test-dir build --output-on-failure -R 'jojo_ps1_diagnostic_frontier_tests|jojo_r3000a_(memory|control_flow)_tests'
```
Expected: all selected tests pass.

Commit:
```bash
git add src/core/ps1_boot_runtime.h src/core/ps1_boot_runtime.cpp tests/test_ps1_diagnostic_frontier.cpp
git commit -m "feat: resume exact PS1 diagnostic MMIO read frontier"
```

**Review gate:** prove the implementation re-executes the blocked instruction rather than writing the destination GPR directly.

---

### Task 3: MAX³ v2 evidence/provenance data model with strict-mode compatibility

**Files:**
- Modify: `src/core/ps1_max3_explorer.h`
- Modify: `src/core/ps1_max3_explorer.cpp`
- Test: `tests/test_ps1_max3_explorer.cpp`

**Interfaces:**
- Consumes: Task 2 runtime frontier API; existing `Ps1BiosFallback`.
- Produces these public model types:
  ```cpp
  enum class Ps1Max3EvidenceClass : std::uint8_t {
      strict,
      speculative,
  };

  enum class Ps1Max3DecisionKind : std::uint8_t {
      bios_fallback,
      mmio_read_fallback,
  };

  enum class Ps1Max3FrontierKind : std::uint8_t {
      bios,
      mmio_read,
      terminal_mmio_write,
      device_command,
      gpu_command,
      cpu_boundary,
      diagnostic_stall,
      execution_budget,
      fatal_runtime_error,
      other_terminal,
  };

  enum class Ps1Max3ExpansionStop : std::uint8_t {
      none,
      branch_depth,
      speculative_depth,
      deduplicated,
      terminal_frontier,
  };
  ```

Extend options:
```cpp
bool deep_frontier_enabled{false};
std::size_t max_unique_frontiers{32u};
std::size_t max_speculative_depth{8u};
```

Extend termination:
```cpp
frontier_limit,
```

Typed decision:
```cpp
struct Ps1Max3Decision {
    Ps1Max3DecisionKind kind{Ps1Max3DecisionKind::bios_fallback};
    std::uint32_t table{};
    std::uint32_t selector{};
    Ps1BiosFallback fallback{Ps1BiosFallback::return_zero};
    std::uint32_t address{};
    std::uint8_t width{};
    std::uint32_t value{};
};
```

Node additions:
```cpp
Ps1Max3EvidenceClass evidence{Ps1Max3EvidenceClass::strict};
std::size_t speculative_depth{};
std::optional<Ps1Max3Decision> decision{};
std::optional<std::size_t> frontier{};
Ps1Max3ExpansionStop expansion_stop{Ps1Max3ExpansionStop::none};
```

Frontier record:
```cpp
struct Ps1Max3Frontier {
    std::size_t index{};
    Ps1Max3EvidenceClass evidence{Ps1Max3EvidenceClass::strict};
    Ps1Max3FrontierKind kind{Ps1Max3FrontierKind::other_terminal};
    std::uint32_t pc{};
    std::optional<std::uint32_t> opcode{};
    std::uint32_t table{};
    std::uint32_t selector{};
    std::uint32_t address{};
    std::uint8_t width{};
    bool write{};
    std::uint32_t value{};
    std::size_t first_node{};
    std::size_t occurrence_count{};
    std::optional<std::size_t> parent_frontier{};
    std::optional<Ps1Max3Decision> parent_decision{};
    std::vector<Ps1Max3Decision> assumption_chain;
    bool expandable{};
    Ps1BootStopReason stop_reason{Ps1BootStopReason::none};
};
```

Add `std::vector<Ps1Max3Frontier> frontiers;` to `Ps1Max3Report`.

- [ ] **Step 1: Write RED strict-mode compatibility/provenance tests**

Using the existing `one_frontier_then_loop()` fixture, set `deep_frontier_enabled=false` and require the same behavioral branching as before: root plus four BIOS fallback children. Add assertions:

```cpp
CHECK(report.nodes[0].evidence == jojo::Ps1Max3EvidenceClass::strict);
for (std::size_t i = 1; i < report.nodes.size(); ++i) {
    CHECK(report.nodes[i].evidence == jojo::Ps1Max3EvidenceClass::speculative);
    CHECK(report.nodes[i].speculative_depth == 1u);
    CHECK(report.nodes[i].decision.has_value());
    CHECK(report.nodes[i].decision->kind == jojo::Ps1Max3DecisionKind::bios_fallback);
}
CHECK(report.frontiers.size() == 1u);
CHECK(report.frontiers[0].kind == jojo::Ps1Max3FrontierKind::bios);
CHECK(report.frontiers[0].evidence == jojo::Ps1Max3EvidenceClass::strict);
```

Keep all existing dependency assertions unchanged.

- [ ] **Step 2: Write RED terminal-frontier classification tests**

For the existing unsupported GPU write fixture require a frontier with `kind=gpu_command`, `write=true`, `expandable=false`, root evidence strict, and `expansion_stop=terminal_frontier`.

Add a synthetic unsupported ordinary MMIO write fixture and require `kind=terminal_mmio_write` with no child nodes.

- [ ] **Step 3: Run RED**

Run:
```bash
cmake --build build --parallel 2
ctest --test-dir build --output-on-failure -R '^jojo_ps1_max3_explorer_tests$'
```
Expected: compile/test failure because v2 provenance types/fields are absent.

Commit RED:
```bash
git add tests/test_ps1_max3_explorer.cpp
git commit -m "test: define MAX3 v2 provenance model RED"
```

- [ ] **Step 4: Add v2 types/options without changing search order**

Implement the enums/struct fields exactly as above. Keep `deep_frontier_enabled=false` by default. Preserve `Ps1Max3Dependency`, dependency deduplication, and existing BIOS fallback order.

- [ ] **Step 5: Register a frontier for every terminal node**

Create an internal deterministic `FrontierKey` that includes:

- frontier kind;
- PC and opcode-presence/opcode;
- BIOS table/selector when applicable;
- MMIO address/width/write/value when applicable;
- runtime diagnostic state hash.

Maintain a map from `FrontierKey` to stable frontier index. The first observation pushes `Ps1Max3Frontier`; repeated observations increment `occurrence_count` only.

Classification rules:

```text
bios_call_unimplemented + BIOS table PC -> bios
mmio_unimplemented + unsupported read -> mmio_read
mmio_unimplemented + unsupported write -> terminal_mmio_write
device_command_unimplemented -> device_command
gpu_command_unimplemented -> gpu_command
cpu_boundary -> cpu_boundary
diagnostic_stall -> diagnostic_stall
execution_budget_exhausted -> execution_budget
fatal_runtime_error -> fatal_runtime_error
anything else terminal -> other_terminal
```

A frontier is `expandable=true` only for BIOS or, when deep mode later enables it, a provable runtime MMIO read frontier.

- [ ] **Step 6: Thread node evidence and typed BIOS decisions**

Change `visit(...)` to receive:

```cpp
Ps1Max3EvidenceClass evidence,
std::size_t speculative_depth,
std::optional<Ps1Max3Decision> parent_decision,
std::optional<std::size_t> parent_frontier,
std::vector<Ps1Max3Decision> assumption_chain
```

Root uses strict/0/no decision. Every BIOS fallback child uses speculative, increments speculative depth, stores a typed `bios_fallback` decision, and appends it to the assumption chain. Existing `fallback` may remain in `Ps1Max3NodeSummary` for compatibility during v1→v2 transition.

- [ ] **Step 7: Run GREEN and preserve old dependency contracts**

Run:
```bash
cmake --build build --parallel 2
ctest --test-dir build --output-on-failure -R '^jojo_ps1_max3_explorer_tests$'
```
Expected: all MAX³ explorer tests pass and old dependency assertions still pass.

Commit:
```bash
git add src/core/ps1_max3_explorer.h src/core/ps1_max3_explorer.cpp tests/test_ps1_max3_explorer.cpp
git commit -m "feat: add MAX3 v2 evidence and frontier provenance"
```

**Review gate:** strict mode must still generate no MMIO fallback children.

---

### Task 4: Deep MMIO-read branching, limits, deduplication, and ranking

**Files:**
- Modify: `src/core/ps1_max3_explorer.cpp`
- Modify: `src/core/ps1_max3_explorer.h`
- Test: `tests/test_ps1_max3_explorer.cpp`

**Interfaces:**
- Consumes: Task 2 `diagnostic_mmio_read_frontier()` + `apply_diagnostic_mmio_read_fallback(value)` and Task 3 provenance types.
- Produces: deterministic deep traversal and `Ps1Max3TerminationReason::frontier_limit`.

- [ ] **Step 1: Write RED fixture that discovers two MMIO read frontiers in one run**

Create a synthetic executable with this control flow:

1. `LBU` from unsupported `0x1F801802`;
2. after load delay, branch so every fallback value reaches a second unsupported `LHU` at `0x1F801800`;
3. after the second read, enter a stable loop.

Run with:
```cpp
auto options = fast_options();
options.deep_frontier_enabled = true;
options.max_branch_depth = 4u;
options.max_speculative_depth = 4u;
options.max_unique_frontiers = 16u;
options.segment_options.diagnostic_mmio_probe = false;
```

Require:

```cpp
CHECK(report.frontiers.size() >= 2u);
CHECK(report.frontiers[0].evidence == jojo::Ps1Max3EvidenceClass::strict);
CHECK(report.frontiers[0].kind == jojo::Ps1Max3FrontierKind::mmio_read);
CHECK(report.frontiers[0].address == 0x1F801802u);

const auto second = std::find_if(report.frontiers.begin(), report.frontiers.end(), [](const auto& f) {
    return f.kind == jojo::Ps1Max3FrontierKind::mmio_read && f.address == 0x1F801800u;
});
CHECK(second != report.frontiers.end());
if (second != report.frontiers.end()) {
    CHECK(second->evidence == jojo::Ps1Max3EvidenceClass::speculative);
    CHECK(!second->assumption_chain.empty());
    CHECK(second->assumption_chain.front().kind == jojo::Ps1Max3DecisionKind::mmio_read_fallback);
}
```

- [ ] **Step 2: Write RED deterministic fallback-order tests**

For an 8-bit MMIO read require child decisions in exact order `0x00`, `0x01`, `0xFF`. For 16-bit require `0x0000`, `0x0001`, `0xFFFF`. For 32-bit require `0`, `1`, `0xFFFFFFFF`.

Every MMIO child is speculative and increments `speculative_depth` by one.

- [ ] **Step 3: Write RED write/device strictness tests**

Require that a strict unsupported write, unsupported CD-ROM command, and unsupported GPU command each produce exactly one terminal node at that path and zero `mmio_read_fallback` children, even with deep mode enabled.

- [ ] **Step 4: Write RED deduplication and limit tests**

Add fixtures/assertions for:

1. repeated identical read frontier/state → one frontier record, `occurrence_count > 1`;
2. same address/width but different diagnostic state hash → distinct frontier records;
3. `max_unique_frontiers=1` where a second new expandable frontier is reachable → global `termination_reason=frontier_limit`;
4. `max_unique_frontiers` reached only after all work is already complete → do not report `frontier_limit`;
5. `max_speculative_depth=1` → first fallback children exist, their next expandable frontier sets node `expansion_stop=speculative_depth`, sibling fallback paths still execute;
6. `max_branch_depth=1` with larger speculative depth → affected nodes use `expansion_stop=branch_depth`;
7. a deduplicated frontier node uses `expansion_stop=deduplicated`.

- [ ] **Step 5: Write RED ranking and repeatability tests**

Construct two otherwise-equal paths where one has speculative depth 1 and the other depth 2. Require the depth-1 path to win before cumulative retired instructions are considered.

Run the same deep fixture twice and compare, field-by-field:

- frontier count/order/kinds/evidence/addresses;
- node count/order/parents/evidence/speculative depth;
- typed decisions;
- best node/path;
- termination reason.

- [ ] **Step 6: Run RED**

Run:
```bash
cmake --build build --parallel 2
ctest --test-dir build --output-on-failure -R '^jojo_ps1_max3_explorer_tests$'
```
Expected: failures showing no MMIO read branching/limits yet.

Commit RED:
```bash
git add tests/test_ps1_max3_explorer.cpp
git commit -m "test: define MAX3 deep MMIO exploration RED"
```

- [ ] **Step 7: Implement width-specific fallback enumeration**

Use a helper returning the stable ordered vector:

```cpp
std::array<std::uint32_t, 3> mmio_read_fallbacks(std::uint8_t width) noexcept {
    switch (width) {
        case 1u: return {0u, 1u, 0xFFu};
        case 2u: return {0u, 1u, 0xFFFFu};
        case 4u: return {0u, 1u, 0xFFFFFFFFu};
        default: return {0u, 0u, 0u};
    }
}
```

Only call it for widths proven valid by the runtime frontier API.

- [ ] **Step 8: Expand MMIO read frontiers through copied runtimes**

A node may generate MMIO children only when all are true:

```cpp
options_.deep_frontier_enabled
runtime.diagnostic_mmio_read_frontier().has_value()
depth < options_.max_branch_depth
speculative_depth < options_.max_speculative_depth
```

For each fallback value:

```cpp
auto child = runtime;
if (!child.apply_diagnostic_mmio_read_fallback(value)) continue;
Ps1Max3Decision decision{};
decision.kind = Ps1Max3DecisionKind::mmio_read_fallback;
decision.address = frontier.address;
decision.width = frontier.width;
decision.value = value;
```

Append the decision to the assumption chain and visit the copied runtime. Do not mutate device state directly.

- [ ] **Step 9: Enforce global unique-frontier cap without suppressing duplicates**

When registering a new `FrontierKey`:

- if it already exists, increment occurrence count regardless of cap;
- if it is new and `frontiers.size() < max_unique_frontiers`, insert it;
- if it is new, cap is already reached, and that path would otherwise be expandable, set a `frontier_limit_truncated_` flag and do not expand that unseen frontier;
- finalize global termination as `frontier_limit` only if `frontier_limit_truncated_` is true and neither node nor retired hard limit already supplied a stronger global termination.

`max_nodes` and `max_total_retired` remain immediate global stops exactly as before.

- [ ] **Step 10: Implement path-local expansion-stop reasons**

Set one mutually exclusive node reason in this priority:

1. `deduplicated` when the exact expandable frontier/state was already expanded;
2. `branch_depth` when total diagnostic depth reached `max_branch_depth`;
3. `speculative_depth` when speculative depth reached `max_speculative_depth`;
4. `terminal_frontier` when the frontier class is non-expandable;
5. `none` when expansion proceeds or no frontier exists.

Depth-limited nodes do not stop sibling traversal.

- [ ] **Step 11: Update ranking**

Preserve metric order through dependency/frontier progress. Before comparing `cumulative_retired`, prefer lower `speculative_depth`:

```cpp
if (candidate.path_dependency_count != current.path_dependency_count)
    return candidate.path_dependency_count > current.path_dependency_count;
if (candidate.speculative_depth != current.speculative_depth)
    return candidate.speculative_depth < current.speculative_depth;
return candidate.cumulative_retired > current.cumulative_retired;
```

Do not allow speculative progress to rewrite a frontier's evidence class to strict.

- [ ] **Step 12: Run GREEN and full explorer repeatability suite**

Run:
```bash
cmake --build build --parallel 2
ctest --test-dir build --output-on-failure -R '^jojo_ps1_max3_explorer_tests$'
```
Expected: all MAX³ explorer tests pass.

Commit:
```bash
git add src/core/ps1_max3_explorer.h src/core/ps1_max3_explorer.cpp tests/test_ps1_max3_explorer.cpp
git commit -m "feat: explore PS1 MAX3 deep read frontiers"
```

**Review gate:** verify there is no branch that calls a diagnostic fallback for `write=true`, `device_command_unimplemented`, `gpu_command_unimplemented`, or `cpu_boundary`.

---

### Task 5: Deterministic `jojo-max3-checkpoint-v2` serialization

**Files:**
- Modify: `src/core/ps1_max3_report_io.cpp`
- Test: `tests/test_ps1_boot_report_io.cpp`

**Interfaces:**
- Consumes: Task 3/4 v2 options, frontiers, node provenance, typed decisions, `frontier_limit`.
- Produces: deterministic line-oriented `format=jojo-max3-checkpoint-v2` text.

- [ ] **Step 1: Write RED v2 header/options/count tests**

Format a synthetic `Ps1Max3Report` and require these exact fields:

```text
format=jojo-max3-checkpoint-v2
deep_frontier_enabled=1
max_unique_frontiers=32
max_speculative_depth=8
strict_frontier_count=1
speculative_frontier_count=1
unique_frontier_count=2
```

Retain existing `dependency_count`, dependency records, node metrics, best path, and `best_report_begin/end` fields.

- [ ] **Step 2: Write RED typed-decision and frontier provenance tests**

Create one strict BIOS frontier and one speculative MMIO-read frontier. Require deterministic fields that encode:

```text
frontier_0_evidence=strict
frontier_0_kind=bios
frontier_0_table=0x000000a0
frontier_0_selector=0x00000033
frontier_0_occurrence_count=1
frontier_0_expandable=1

frontier_1_evidence=speculative
frontier_1_kind=mmio_read
frontier_1_pc=0x80010020
frontier_1_opcode=0x...
frontier_1_address=0x1f801802
frontier_1_width=1
frontier_1_write=0
frontier_1_parent_frontier=0
frontier_1_assumption_count=1
frontier_1_assumption_0_kind=bios_fallback
```

For node provenance require:

```text
node_0_evidence=strict
node_0_speculative_depth=0
node_0_expansion_stop=none
node_1_evidence=speculative
node_1_speculative_depth=1
```

For an MMIO decision require `kind=mmio_read_fallback`, address, width, and chosen value. For BIOS require `kind=bios_fallback`, table, selector, and policy.

- [ ] **Step 3: Write RED `frontier_limit` and no-payload tests**

Require `termination_reason=frontier_limit` serialization. Also assert the output contains no keys named `ram_dump`, `bios_bytes`, `sector_data`, `disc_bytes`, or `asset_payload`.

- [ ] **Step 4: Run RED**

Run:
```bash
cmake --build build --parallel 2
ctest --test-dir build --output-on-failure -R '^jojo_ps1_boot_report_io_tests$'
```
Expected: failure because serializer still emits v1 and lacks v2 provenance.

Commit RED:
```bash
git add tests/test_ps1_boot_report_io.cpp
git commit -m "test: define MAX3 checkpoint v2 report RED"
```

- [ ] **Step 5: Add deterministic enum-name helpers**

Implement total switch helpers returning exactly:

```text
evidence: strict | speculative
decision: bios_fallback | mmio_read_fallback
frontier kind: bios | mmio_read | terminal_mmio_write | device_command | gpu_command | cpu_boundary | diagnostic_stall | execution_budget | fatal_runtime_error | other_terminal
expansion stop: none | branch_depth | speculative_depth | deduplicated | terminal_frontier
termination: completed | node_limit | total_retired_limit | frontier_limit
```

- [ ] **Step 6: Serialize v2 options, counts, typed best path, frontiers, and node provenance**

Change header to `format=jojo-max3-checkpoint-v2`.

Compute strict/speculative frontier counts from `report.frontiers` at formatting time. Emit frontiers in vector/index order and assumptions in stored order. Use `none` for inapplicable optional fields rather than omitting them, preserving line-oriented determinism.

For best-path decisions emit a `kind` line before kind-specific fields so consumers can distinguish BIOS and MMIO decisions without inference.

Keep the detailed existing `best_report` block unchanged.

- [ ] **Step 7: Verify byte-for-byte deterministic formatting for the same report object**

In the test call `format_ps1_max3_report(report)` twice and require exact string equality.

- [ ] **Step 8: Run GREEN**

Run:
```bash
cmake --build build --parallel 2
ctest --test-dir build --output-on-failure -R 'jojo_ps1_(boot_report_io|max3_explorer)_tests'
```
Expected: both report and explorer suites pass.

Commit:
```bash
git add src/core/ps1_max3_report_io.cpp tests/test_ps1_boot_report_io.cpp
git commit -m "feat: serialize PS1 MAX3 checkpoint v2"
```

**Review gate:** verify v2 still emits every legacy dependency field and the full best-report block.

---

### Task 6: Enable Deep Frontier for user checkpoints and close the milestone

**Files:**
- Modify: `src/core/ps1_max3_explorer.cpp`
- Test: `tests/test_ps1_max3_explorer.cpp`
- Test: `tests/test_ps1_boot_report_io.cpp`
- Verify: `.github/workflows/build.yml` through CI only; do not change workflow unless an existing gate is broken by this feature.

**Interfaces:**
- Consumes: Tasks 1–5.
- Produces: user checkpoint defaults that generate v2 deep reports and a Windows artifact from one exact green SHA.

- [ ] **Step 1: Write RED local-evidence option contract**

Require:

```cpp
const auto options = jojo::ps1_max3_local_evidence_options();
CHECK(options.deep_frontier_enabled);
CHECK(options.max_unique_frontiers == 32u);
CHECK(options.max_branch_depth == 8u);
CHECK(options.max_speculative_depth == 8u);
CHECK(options.max_nodes == 5461u);
CHECK(options.max_total_retired == 1000000000ull);
CHECK(options.segment_options.diagnostic_mmio_probe);
```

Do not change the existing large trace/MMIO/BIOS capacities unless a test proves the v2 fields exceed them; the frontier graph is stored separately and does not require larger event rings.

- [ ] **Step 2: Write RED end-to-end deep checkpoint fixture**

Use a synthetic executable that reaches:

1. a strict unsupported read;
2. a second unsupported read after fallback;
3. an unsupported write after the second fallback.

Run `explore_ps1_max3` with `ps1_max3_local_evidence_options()` but lower `max_nodes`, `max_total_retired`, and event capacities inside the test for speed while leaving deep/depth/frontier settings unchanged.

Require:

- at least two unique read frontiers;
- first frontier strict;
- downstream read frontier speculative;
- final write frontier speculative and non-expandable;
- no child generated from the write;
- formatted report starts `jojo-max3-checkpoint-v2`;
- formatted report contains the full assumption chain.

- [ ] **Step 3: Run RED**

Run:
```bash
cmake --build build --parallel 2
ctest --test-dir build --output-on-failure -R 'jojo_ps1_(max3_explorer|boot_report_io)_tests'
```
Expected: local evidence option test fails because deep mode is still disabled/default depth 6.

Commit RED:
```bash
git add tests/test_ps1_max3_explorer.cpp tests/test_ps1_boot_report_io.cpp
git commit -m "test: require deep MAX3 local checkpoint defaults RED"
```

- [ ] **Step 4: Enable the approved user-checkpoint limits**

Change `ps1_max3_local_evidence_options()` to:

```cpp
Ps1Max3Options options{};
options.max_nodes = 5461u;
options.max_branch_depth = 8u;
options.max_total_retired = 1000000000ull;
options.deep_frontier_enabled = true;
options.max_unique_frontiers = 32u;
options.max_speculative_depth = 8u;
options.segment_options = Ps1BootOptions{
    std::numeric_limits<std::uint64_t>::max(),
    131072u,
    true,
    65536u,
    65536u,
    2000000u,
};
```

No other production defaults change.

- [ ] **Step 5: Run focused GREEN**

Run:
```bash
cmake --build build --parallel 2
ctest --test-dir build --output-on-failure -R 'jojo_ps1_(max3_explorer|boot_report_io|diagnostic_frontier|memory_bus)_tests'
```
Expected: all selected deep-frontier suites pass.

Commit:
```bash
git add src/core/ps1_max3_explorer.cpp tests/test_ps1_max3_explorer.cpp tests/test_ps1_boot_report_io.cpp
git commit -m "feat: enable PS1 MAX3 deep checkpoint defaults"
```

- [ ] **Step 6: Run the complete Linux verification locally or in CI**

Run where available:
```bash
cmake -S . -B build -DCMAKE_BUILD_TYPE=Release
cmake --build build --parallel 2
ctest --test-dir build --output-on-failure
cmake -DJOJO_SOURCE_DIR="$PWD" -P cmake/CheckProductionReadiness.cmake
cmake -DJOJO_SOURCE_DIR="$PWD" -P cmake/CheckProductionReadinessNegative.cmake
cmake -DJOJO_SOURCE_DIR="$PWD" -P cmake/CheckPs1ActiveArchitecture.cmake
c++ -std=c++20 -Wall -Wextra -Wpedantic -Isrc tests/test_observed_disc_revision.cpp build/libjojo_core.a -ldl -pthread -o observed_disc_revision_tests
./observed_disc_revision_tests
c++ -std=c++20 -Wall -Wextra -Wpedantic -Isrc tests/test_network_transport.cpp src/core/network_protocol.cpp -o network_transport_tests
./network_transport_tests
```
Expected: every command succeeds.

- [ ] **Step 7: Audit scope before final CI**

Compare the final implementation head to `16ab90deeb5a3d37d1110b89da2296e87c694a20` and verify production changes are limited to:

```text
src/core/ps1_memory_bus.h
src/core/ps1_memory_bus.cpp
src/core/ps1_boot_runtime.h
src/core/ps1_boot_runtime.cpp
src/core/ps1_max3_explorer.h
src/core/ps1_max3_explorer.cpp
src/core/ps1_max3_report_io.cpp
```

Tests may change only the test files listed by this plan. No CD-ROM command semantics, DMA implementation, GPU state, presentation, renderer, BIOS HLE call table, or R3000A executor implementation should change.

- [ ] **Step 8: Require final GitHub Actions evidence on one exact SHA**

On the final candidate SHA, require the existing workflow to finish:

- Linux job: success, full CTest suite green, production readiness green, PS1 architecture green, observed-disc green, UDP green.
- Windows/MSVC 2022 job: success, full CTest suite green, production readiness green, PS1 architecture green, observed-disc green, UDP green, artifact upload green.

Do not claim completion from an earlier SHA or from partially complete jobs.

- [ ] **Step 9: Materially verify the Windows artifact**

Download the `JOJO-Recompiled-Windows-x64` artifact from the final run. Verify:

```text
artifact workflow head_sha == final branch SHA
ZIP contains exactly JOJO-Recompiled.exe
local ZIP SHA-256 == GitHub artifact digest
EXE SHA-256 is recorded for the user
```

- [ ] **Step 10: Hand off the first commercial Deep Frontier checkpoint**

Deliver the verified ZIP and instruct one user run of `EXECUTAR CHECKPOINT`. The generated file must identify itself as `jojo-max3-checkpoint-v2` and will be used to evaluate whether multiple downstream commercial frontiers are now discovered in one run.

Do **not** implement speculative commercial frontiers automatically. Classify each reported frontier as strict or speculative and use strict evidence/authoritative hardware documentation for subsequent production changes.

---

## Final Definition of Done

The feature is complete only when all of these are true on one final SHA:

- one-shot diagnostic read override is exact-match and one-use;
- normal R3000A execution performs fallback load semantics;
- strict mode still has no MMIO fallback branching;
- BIOS fallback descendants are explicitly speculative;
- deep mode discovers multiple read frontiers in one synthetic run;
- terminal writes/device/GPU/CPU boundaries do not gain new fallbacks;
- frontier graph contains stable evidence/provenance/assumption chains;
- frontier deduplication, `frontier_limit`, branch depth, and speculative depth are deterministic;
- lower speculative depth wins equal-progress ranking before retired-instruction count;
- v2 report retains legacy dependencies and best-report payload while adding deep fields;
- local checkpoint defaults are deep-enabled at `32` frontiers and depth `8`;
- Linux and Windows/MSVC gates are green;
- exact Windows artifact is verified and ready for one commercial Deep Frontier checkpoint.
