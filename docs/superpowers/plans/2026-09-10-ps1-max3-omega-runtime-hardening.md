# PS1 MAX³ OMEGA Runtime and Synthetic Hardening Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans task-by-task. Unexpected failures require systematic-debugging before fixes.

**Goal:** Prove OMEGA continuation is architecturally safe across R3000A loads, delay/interrupt boundaries, aliases, state identity, context capture, and repeated frontiers, using only deterministic synthetic fixtures.

**Spec:** `docs/superpowers/specs/2026-09-10-ps1-max3-omega-deep-consolidation-design.md`

## Fixed constraints

- No commercial game/BIOS/RAM/sector bytes in tests.
- Do not add mutable/test-only CPU accessors merely to test hashing. Build paired synthetic runtimes that naturally reach states differing in one field; mutate bus/device state through public APIs.
- Blocked loads cannot partially retire. Fallback arms only the bus; R3000A executor owns architectural effects.
- Supported MMIO always wins over diagnostic override. Wrong/stale/double-consumed override fails closed.
- `LWL/LWR` become expandable only if exact merge/retry semantics are proven; otherwise tests lock them terminal.

---

### Task C1 — Canonical future-state identity audit

**Files:** `src/core/ps1_boot_runtime.cpp`, `src/core/ps1_memory_bus.{h,cpp}`, `tests/test_ps1_diagnostic_state_hash.cpp`, `CMakeLists.txt`.

- [ ] RED paired-runtime tests for GPR, HI/LO, PC/next-PC, pending load, delay-slot, COP0, external IRQ, HLE, CD-ROM, GPU, interrupt-continuation, RAM/device state, and armed diagnostic override.
- [ ] Use synthetic instruction sequences to produce CPU differences; do not expose mutable CPU internals for tests.
- [ ] RED history invariance: report-only trace/history/capacity metadata cannot split architectural state identity.
- [ ] GREEN add only missing future-relevant state to diagnostic hash; pointer/host addresses forbidden.
- [ ] Define an explicit canonical execution-state key/hash used by exact dedup/cycle detection. Near-cycle pruning remains disabled unless this task can prove a second equivalence key omitting only named non-semantic history fields.
- [ ] Commit: `test: lock PS1 diagnostic state identity`.

### Task C2 — Supported load continuation matrix

**Modify:** `tests/test_ps1_diagnostic_frontier.cpp`; `tests/mips_test_encode.h` only for missing encoders.

Table-drive `LB/LBU/LH/LHU/LW` with widths, negative/positive values, pending load absent/present, and exact retired count.

- [ ] Before fallback: PC at blocked instruction, destination/pending load unchanged by blocked instruction, retired count excludes it.
- [ ] After fallback/retry: verify pending-load/load-delay then final GPR value through normal executor.
- [ ] Add physical/KSEG0/KSEG1 equivalent-access cases where valid; same physical MMIO semantics, guest identity preserved diagnostically.
- [ ] Commit: `test: expand PS1 diagnostic load matrix`.

### Task C3 — Fail-closed override/race boundary matrix

**Modify:** `tests/test_ps1_memory_bus.cpp`, `tests/test_ps1_diagnostic_frontier.cpp`, `tests/test_ps1_interrupt_continuation.cpp`.

- [ ] Wrong address, width, direction, PC/state identity => no accidental consumption.
- [ ] Successful consumption exactly once; second access returns real behavior.
- [ ] Supported MMIO cannot be replaced by diagnostic override.
- [ ] Runtime copy A/B with different candidates remains isolated.
- [ ] Interrupt becoming pending between frontier and retry cannot consume override from an unrelated access; either exact retry contract holds or continuation fails closed and remains diagnosable.
- [ ] Delay-slot and existing pending-load boundary cases.
- [ ] Commit: `test: harden PS1 diagnostic override boundaries`.

### Task C4 — Optional `LWL/LWR` eligibility gate

**Modify:** `tests/test_r3000a_unaligned.cpp`, `tests/test_ps1_diagnostic_frontier.cpp`, and only if proven `src/core/ps1_boot_runtime.cpp`.

- [ ] RED/verify offsets 0..3 merge semantics using existing executor behavior.
- [ ] Test diagnostic stop/retry with preexisting destination/pending load.
- [ ] If exact identity/retry is provable, minimally extend eligibility table and GREEN all cases.
- [ ] Otherwise explicitly lock `LWL/LWR` as non-expandable with a named missing invariant; do not fake support.
- [ ] Commit: `test: define unaligned MAX3 frontier eligibility`.

### Task C5 — Deterministic synthetic OMEGA corpus

**Create:** `tests/ps1_max3_synthetic_programs.h`, `tests/test_ps1_max3_synthetic_corpus.cpp`; modify `CMakeLists.txt`.

Builders generated only from `test_mips` encoders:

```cpp
omega_two_read_chain();
omega_read_then_write_terminal();
omega_read_then_bios_frontier();
omega_bios_then_read_frontier();
omega_converging_candidates();
omega_exact_cycle();
omega_irq_during_speculation();
omega_cdrom_progress_then_frontier();
omega_gpu_progress_then_frontier();
omega_landmark_ranking_fixture();
```

- [ ] For every builder document/assert strict stop first.
- [ ] Then assert deep/omega provenance, convergence/cycle behavior, and progress metrics.
- [ ] No copied commercial instruction blocks.
- [ ] Commit: `test: add synthetic MAX3 OMEGA corpus`.

### Task C6 — Deterministic property/metamorphic matrix

**Create:** `tests/test_ps1_max3_property_matrix.cpp`; modify `CMakeLists.txt`.

Finite dimensions: load family, width, fallback source/value class, alias, pending-load state, normal/delay-slot context where legal, match/mismatch override identity.

- [ ] Invariants: no partial retire; exact one-shot consumption; canonical unarmed state identity after consumption; equivalent aliases produce equivalent device semantics; history-only changes do not alter future-state hash; strict mode independent of candidate policy.
- [ ] Run matrix twice in process and compare aggregate deterministic hash/counts.
- [ ] Include checked 64-bit counter/size arithmetic edge cases required by OMEGA limits.
- [ ] Commit: `test: add deterministic MAX3 property matrix`.

### Task C7 — Exact bounded frontier context and tails

**Modify:** `src/core/ps1_max3_explorer.{h,cpp}`, `tests/test_ps1_max3_explorer.cpp`, `tests/test_ps1_max3_checkpoint_v2.cpp`.

Append this exact public context model (using only current public getters/state already present in `Ps1BootReport`):

```cpp
struct Ps1Max3FrontierContext {
    std::uint16_t interrupt_status{};
    std::uint16_t interrupt_mask{};
    std::uint32_t dma_interrupt{};
    std::uint16_t timer1_counter{};
    std::uint16_t timer1_mode{};
    std::uint8_t cdrom_index{};
    std::uint8_t cdrom_drive_status{};
    std::uint8_t cdrom_interrupt_enable{};
    std::uint8_t cdrom_interrupt_status{};
    bool cdrom_irq_line{};
    std::uint64_t cdrom_command_count{};
    std::uint64_t gp0_command_count{};
    std::uint64_t gp1_command_count{};
    std::uint8_t gpu_dma_direction{};
    bool gpu_display_disabled{};
    std::vector<Ps1TraceSample> trace_tail;
    std::vector<Ps1BiosCallSummary> bios_tail;
    std::vector<Ps1MmioSummary> mmio_tail;
    std::vector<Ps1CdromCommandSummary> cdrom_tail;
};
```

Tail caps are exact: trace 16; BIOS 8; MMIO 8; CD 8. Use the newest entries from segment report. Do **not** add `dma_transfer_count` as a fake device getter; path/report DMA counter already exists.

- [ ] RED capture equals `Ps1MemoryBus`/`Ps1CdromState`/`Ps1GpuState` public getters at registration time.
- [ ] RED tails keep latest N and report omitted counts through checkpoint serializer.
- [ ] GREEN read-only capture only; no device semantics change.
- [ ] Commit: `feat: capture MAX3 frontier context`.

### Task C8 — CPU/BIOS/IRQ context completeness

Append available structural fields to `Ps1Max3Frontier`/context without exposing raw memory:

- CPU: delay-slot active/branch PC, pending-load valid/reg (value only if already treated as scalar diagnostic state), exception code/fault address/width.
- BIOS frontier: table/selector, A0–A3, RA/callsite from existing recent BIOS event.
- IRQ: accepted interrupt count, COP0 EPC/status/cause summary when available from boot report/runtime diagnostic, continuation status as existing modeled state allows.

- [ ] RED synthetic BIOS/IRQ frontier context tests.
- [ ] GREEN copy scalar/bounded metadata only.
- [ ] Checkpoint v2 tests serialize it.
- [ ] Commit: `feat: enrich MAX3 CPU and IRQ context`.

### Task C9 — Permanent regressions for prior commercial frontiers

**Modify:** existing PS1 CDROM/MAX³/interrupt/memory tests.

Synthetic regressions must cover:

- I_STAT read32 `0x1F801070`;
- HSTS read8 `0x1F801800`;
- RESULT FIFO read8 `0x1F801801` while index=1;
- interrupt guest callback continuation and B0:17 ReturnFromException;
- pending-load interrupt boundary;
- no phantom A0:35;
- diagnostic read override behavior.

- [ ] Add one corpus assertion that these do not reappear as strict terminal dependencies.
- [ ] Full Linux suite.
- [ ] Windows/MSVC exact-SHA CI required before Plan D.
- [ ] Commit: `test: preserve PS1 commercial frontier regressions`.

## Plan C promotion gate

No partial retire/stale override in covered boundaries; state identity is future-complete; exact cycles are safe; context/tails are bounded/legal; generated matrix deterministic; prior frontiers remain fixed; Linux + Windows/MSVC green on one SHA.
