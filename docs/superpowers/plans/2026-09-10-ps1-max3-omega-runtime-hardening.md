# PS1 MAX³ OMEGA Runtime and Synthetic Hardening Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Prove that OMEGA's diagnostic continuation is architecturally safe across R3000A loads, delay semantics, interrupts, aliases, state hashing, and repeated frontiers, while building a large deterministic synthetic regression corpus that contains no proprietary payloads.

**Architecture:** Keep the production executor/device semantics untouched unless a failing test proves a real implementation defect. Most work belongs in focused tests and synthetic fixture helpers. Diagnostic-only production additions are limited to future-relevant state identity/context capture required by the approved OMEGA architecture.

**Tech Stack:** C++20, existing R3000A reference executor, `Ps1MemoryBus`, `Ps1BootRuntime`, MAX³, synthetic PS-X EXE fixtures, CMake/CTest.

**Spec:** `docs/superpowers/specs/2026-09-10-ps1-max3-omega-deep-consolidation-design.md`

## Global Constraints

- Never use the user's game bytes in tests.
- A blocked load must not partially retire before fallback application.
- Diagnostic fallback arms the bus only; normal R3000A execution owns sign extension, zero extension, load delay, PC, exceptions, and pending-load retirement.
- Supported MMIO behavior must always take priority over any diagnostic override.
- Wrong/stale/double-consumed overrides fail closed.
- Extend `LWL/LWR` diagnostic eligibility only if exact merge semantics and continuation identity are independently proven; otherwise leave them terminal.
- State hashing includes only future-relevant architectural/device/diagnostic state, not historical formatting counters.

---

### Task C1: Audit and lock diagnostic state identity

**Files:**
- Modify: `src/core/ps1_boot_runtime.cpp`
- Modify: `src/core/ps1_memory_bus.cpp`
- Modify: `src/core/ps1_memory_bus.h`
- Create: `tests/test_ps1_diagnostic_state_hash.cpp`
- Modify: `CMakeLists.txt`

**Interfaces:**
- Consumes: existing `Ps1BootRuntime::diagnostic_state_hash()` and `Ps1MemoryBus::diagnostic_state_hash()`.
- Produces: tests proving every future-relevant field changes the hash and dead history does not.

- [ ] **Step 1: Write RED hash-sensitivity matrix**

Create two identical runtimes/buses and alter one future-relevant field at a time: GPR, HI/LO, PC/next-PC, pending load, delay slot, COP0 status/cause/EPC, external IRQ pending, HLE state, CD-ROM state, GPU state, interrupt continuation state, armed diagnostic override. Require unequal hashes.

- [ ] **Step 2: Write dead-history invariance tests**

If diagnostic history/capacity counters do not alter future execution, changing only those must not change the execution-state identity. If current hash includes such data, isolate canonical execution-state hashing from report-history hashing rather than deleting useful diagnostics.

- [ ] **Step 3: Run RED and inspect root cause before changing production**

```bash
cmake -S . -B build -DCMAKE_BUILD_TYPE=Release
cmake --build build --parallel 2 --target jojo_ps1_diagnostic_state_hash_tests
ctest --test-dir build -R jojo_ps1_diagnostic_state_hash_tests --output-on-failure
```

- [ ] **Step 4: Make only missing future-relevant state participate**

Use stable byte-wise hashing already used by runtime/bus. No pointer values or host addresses.

- [ ] **Step 5: Run GREEN and commit**

```bash
ctest --test-dir build -R jojo_ps1_diagnostic_state_hash_tests --output-on-failure
git add CMakeLists.txt src/core/ps1_boot_runtime.cpp src/core/ps1_memory_bus.* tests/test_ps1_diagnostic_state_hash.cpp
git commit -m "test: lock PS1 diagnostic state identity"
```

### Task C2: Expand supported-load continuation matrix

**Files:**
- Modify: `tests/test_ps1_diagnostic_frontier.cpp`
- Modify: `tests/mips_test_encode.h` only if missing encoding helpers are needed.

**Interfaces:**
- Consumes: `diagnostic_mmio_read_frontier()` and `apply_diagnostic_mmio_read_fallback()`.
- Produces: matrix proving `LB`, `LBU`, `LH`, `LHU`, `LW` exact continuation.

- [ ] **Step 1: Build a table-driven fixture**

Use entries containing opcode, width, fallback raw value, expected pending-load value after retry, and expected value after one architectural retirement. Cover negative `LB/LH`, positive `LBU/LHU`, and full `LW`.

Example shape:

```cpp
struct LoadCase {
    std::uint8_t opcode;
    std::uint8_t width;
    std::uint32_t fallback;
    std::uint32_t expected_loaded;
};
```

- [ ] **Step 2: Assert blocked load did not retire**

Before fallback: PC remains at blocked instruction; destination GPR and pending load reflect only pre-existing architectural state; retired count excludes the blocked instruction.

- [ ] **Step 3: Apply fallback and run exactly enough instructions to observe load delay**

Require pending-load behavior to match the executor's normal memory-load tests.

- [ ] **Step 4: Add KSEG0/KSEG1/physical alias cases**

Same physical MMIO frontier through aliases must map to the same physical address while retaining exact guest PC/access identity.

- [ ] **Step 5: Run focused GREEN and commit**

```bash
cmake --build build --parallel 2 --target jojo_ps1_diagnostic_frontier_tests
ctest --test-dir build -R jojo_ps1_diagnostic_frontier_tests --output-on-failure
git add tests/test_ps1_diagnostic_frontier.cpp tests/mips_test_encode.h
git commit -m "test: expand PS1 diagnostic load matrix"
```

### Task C3: Fail-closed override and race-boundary matrix

**Files:**
- Modify: `tests/test_ps1_memory_bus.cpp`
- Modify: `tests/test_ps1_diagnostic_frontier.cpp`
- Modify: `tests/test_ps1_interrupt_continuation.cpp`

**Interfaces:**
- Produces regression proof for wrong PC/address/width/direction, stale override, double consumption, supported-address priority, interrupt before retry, and copied-runtime branch isolation.

- [ ] **Step 1: Add one-shot misuse tests**

Require rejection/no-consumption for wrong address, wrong width and write direction. Consume once successfully, then require second access to revert to real unsupported behavior.

- [ ] **Step 2: Add supported-MMIO priority test**

Arm an override targeting a supported register identity where API validation should reject it; if an armed state can exist only internally, prove supported dispatch executes before diagnostic override consumption.

- [ ] **Step 3: Add runtime-copy isolation test**

Copy one frontier runtime into candidate A and B; arm different values; consuming A must not affect B's armed override or state hash.

- [ ] **Step 4: Add interrupt-before-retry test**

Create a synthetic state where an interrupt becomes pending after the frontier. The fallback must not be silently consumed by an unrelated instruction/access; either the exact blocked load retries according to runtime contract or the continuation fails closed and remains diagnosable.

- [ ] **Step 5: Run GREEN and commit**

```bash
ctest --test-dir build -R "jojo_ps1_(memory_bus|diagnostic_frontier|interrupt_continuation)_tests" --output-on-failure
git add tests/test_ps1_memory_bus.cpp tests/test_ps1_diagnostic_frontier.cpp tests/test_ps1_interrupt_continuation.cpp
git commit -m "test: harden PS1 diagnostic override boundaries"
```

### Task C4: Gate optional `LWL/LWR` continuation with exact merge tests

**Files:**
- Modify: `tests/test_r3000a_unaligned.cpp`
- Modify: `tests/test_ps1_diagnostic_frontier.cpp`
- Modify: `src/core/ps1_boot_runtime.cpp` only if all RED cases prove current frontier recognition can safely support these opcodes.

**Interfaces:**
- Produces either: proven `LWL/LWR` diagnostic eligibility, or explicit tests that they remain non-expandable.

- [ ] **Step 1: Enumerate little-endian merge cases for offsets 0..3**

Use existing unaligned executor semantics as the reference. For each offset, pre-seed destination register/pending load and assert expected merged value.

- [ ] **Step 2: Attempt diagnostic frontier RED for `LWL/LWR`**

If runtime currently rejects eligibility, tests should document that outcome first.

- [ ] **Step 3: Decide from evidence**

If blocked execution preserves enough information to re-run exact merge semantics through the normal executor, minimally extend the frontier load-opcode eligibility table. Otherwise keep `LWL/LWR` terminal and add a comment/test naming the missing invariant.

- [ ] **Step 4: Run GREEN and commit**

```bash
ctest --test-dir build -R "jojo_(r3000a_unaligned|ps1_diagnostic_frontier)_tests" --output-on-failure
git add tests/test_r3000a_unaligned.cpp tests/test_ps1_diagnostic_frontier.cpp src/core/ps1_boot_runtime.cpp
git commit -m "test: define unaligned MAX3 frontier eligibility"
```

### Task C5: Create deterministic synthetic OMEGA program corpus

**Files:**
- Create: `tests/ps1_max3_synthetic_programs.h`
- Create: `tests/test_ps1_max3_synthetic_corpus.cpp`
- Modify: `CMakeLists.txt`

**Interfaces:**
- Produces pure synthetic fixture builders for repeated use by explorer/report/runtime tests.

Required builders:

```cpp
std::vector<std::uint32_t> omega_two_read_chain();
std::vector<std::uint32_t> omega_read_then_write_terminal();
std::vector<std::uint32_t> omega_read_then_bios_frontier();
std::vector<std::uint32_t> omega_bios_then_read_frontier();
std::vector<std::uint32_t> omega_converging_candidates();
std::vector<std::uint32_t> omega_exact_cycle();
std::vector<std::uint32_t> omega_irq_during_speculation();
std::vector<std::uint32_t> omega_cdrom_progress_then_frontier();
std::vector<std::uint32_t> omega_gpu_progress_then_frontier();
```

- [ ] **Step 1: Implement builders only after tests describe expected stop/progress**

Every builder is generated from `test_mips` encoders and constants, never copied from commercial traces.

- [ ] **Step 2: Verify each builder's strict behavior first**

The test must document exact strict stop reason/frontier before any OMEGA branch assertions.

- [ ] **Step 3: Verify deep/omega behavior separately**

Require provenance, convergence/cycle behavior, and progress counters appropriate to each synthetic fixture.

- [ ] **Step 4: Run GREEN and commit**

```bash
ctest --test-dir build -R jojo_ps1_max3_synthetic_corpus_tests --output-on-failure
git add CMakeLists.txt tests/ps1_max3_synthetic_programs.h tests/test_ps1_max3_synthetic_corpus.cpp
git commit -m "test: add synthetic MAX3 OMEGA corpus"
```

### Task C6: Add deterministic property-style matrix without randomness

**Files:**
- Create: `tests/test_ps1_max3_property_matrix.cpp`
- Modify: `CMakeLists.txt`

**Interfaces:**
- Consumes: synthetic builders/encoders and Plan A APIs.
- Produces: broad combinatorial coverage from finite deterministic domains.

- [ ] **Step 1: Enumerate finite dimensions**

Use nested loops over:

- load type: `LB/LBU/LH/LHU/LW`;
- guest alias: physical/KSEG0/KSEG1 where legal;
- fallback class: zero/one/all-ones/sign-bit;
- pre-existing pending-load state: absent/present;
- branch context: normal/delay-slot where the executor allows the access;
- read width/alignment combinations valid for the opcode.

- [ ] **Step 2: Assert invariants, not hand-written output for every case**

For each generated case require deterministic frontier identity, no partial retire, one-shot consumption, canonical hash after consumption, and equal final state across equivalent physical aliases except guest-address diagnostic fields.

- [ ] **Step 3: Run the matrix twice in one test process**

Require aggregate deterministic hash/counts to match.

- [ ] **Step 4: Run GREEN and commit**

```bash
ctest --test-dir build -R jojo_ps1_max3_property_matrix_tests --output-on-failure
git add CMakeLists.txt tests/test_ps1_max3_property_matrix.cpp
git commit -m "test: add deterministic MAX3 property matrix"
```

### Task C7: Add subsystem context snapshots without new hardware semantics

**Files:**
- Modify: `src/core/ps1_max3_explorer.h`
- Modify: `src/core/ps1_max3_explorer.cpp`
- Modify: `tests/test_ps1_max3_explorer.cpp`
- Modify: `tests/test_ps1_max3_checkpoint_v2.cpp`

**Interfaces:**
- Appends a lightweight context to `Ps1Max3Frontier` using only already-modeled state:

```cpp
struct Ps1Max3FrontierContext {
    std::uint16_t interrupt_status{};
    std::uint16_t interrupt_mask{};
    std::uint8_t cdrom_index{};
    std::uint8_t cdrom_interrupt_status{};
    std::uint64_t cdrom_command_count{};
    std::uint64_t gp0_command_count{};
    std::uint64_t gp1_command_count{};
    std::uint64_t dma_transfer_count{};
};
```

If exact getters do not exist for one field, omit that field rather than inventing or exposing private state solely for diagnostics without need.

- [ ] **Step 1: Write RED snapshot test around existing CD/GPU/IRQ fixture**

Require context values equal public bus/device getters at frontier registration time.

- [ ] **Step 2: Implement read-only capture**

No diagnostic context field participates in hardware behavior. Future-relevant device state remains covered by device diagnostic hashes separately.

- [ ] **Step 3: Serialize context in checkpoint v2**

Add only scalar counters/status registers already modeled.

- [ ] **Step 4: Run GREEN and commit**

```bash
ctest --test-dir build -R "jojo_ps1_(max3_explorer|max3_checkpoint_v2)_tests" --output-on-failure
git add src/core/ps1_max3_explorer.* tests/test_ps1_max3_explorer.cpp tests/test_ps1_max3_checkpoint_v2.cpp
git commit -m "feat: capture MAX3 frontier device context"
```

### Task C8: Lock all previously fixed commercial frontiers as regressions

**Files:**
- Modify: `tests/test_ps1_cdrom_strict_widths.cpp`
- Modify: `tests/test_ps1_max3_explorer.cpp`
- Modify: `tests/test_ps1_interrupt_continuation.cpp`
- Modify: `tests/test_ps1_memory_bus.cpp`

**Interfaces:**
- Produces synthetic regression coverage for previously observed blockers without storing proprietary code/data.

- [ ] **Step 1: Ensure regression cases exist for**

- `I_STAT` 32-bit read at `0x1F801070`;
- CD-ROM HSTS byte read `0x1F801800`;
- RESULT FIFO `0x1F801801` with bank/index 1;
- interrupt guest callback continuation / B0:17 return;
- no fake A0:35 MAX³ frontier;
- one-shot MMIO fallback semantics from earlier Deep tasks.

- [ ] **Step 2: Add a single corpus assertion that none reappear as strict terminal dependencies**

Use synthetic programs only.

- [ ] **Step 3: Run full Linux suite**

```bash
cmake --build build --parallel 2
ctest --test-dir build --output-on-failure
```

Expected: all PASS.

- [ ] **Step 4: Commit**

```bash
git add tests/test_ps1_cdrom_strict_widths.cpp tests/test_ps1_max3_explorer.cpp tests/test_ps1_interrupt_continuation.cpp tests/test_ps1_memory_bus.cpp
git commit -m "test: preserve PS1 commercial frontier regressions"
```

- [ ] **Step 5: Require Windows/MSVC CI success on exact Plan C final SHA before Plan D**
