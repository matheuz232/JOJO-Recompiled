# PS1 MAX³ OMEGA Runtime and Synthetic Hardening Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans task-by-task. Unexpected failures require systematic-debugging before fixes.

**Goal:** Prove OMEGA continuation is architecturally safe across R3000A loads, delay/interrupt boundaries, aliases, state identity, context capture, and repeated frontiers, using only deterministic synthetic fixtures.

**Spec:** `docs/superpowers/specs/2026-09-10-ps1-max3-omega-deep-consolidation-design.md`

**Ordering:** Run after Plan A and before Plan B. This plan defines frontier context; Plan B serializes it later.

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
- [ ] Define explicit canonical execution-state identity used by exact dedup/cycle detection. Near-cycle pruning remains disabled unless a second equivalence key is proven to omit only named non-semantic history fields.
- [ ] Commit: `test: lock PS1 diagnostic state identity`.

### Task C2 — Supported load continuation matrix

**Modify:** `tests/test_ps1_diagnostic_frontier.cpp`; `tests/mips_test_encode.h` only for missing encoders.

- [ ] Table-drive `LB/LBU/LH/LHU/LW` widths, negative/positive values, pending load absent/present, exact retired count.
- [ ] Before fallback: PC at blocked instruction, destination/pending load unchanged by blocked instruction, retired count excludes it.
- [ ] After fallback/retry: verify architectural pending-load/load-delay and final GPR.
- [ ] Add physical/KSEG0/KSEG1 equivalent-access cases where valid; same physical semantics, guest identity preserved diagnostically.
- [ ] Commit: `test: expand PS1 diagnostic load matrix`.

### Task C3 — Fail-closed override/race boundary matrix

**Modify:** `tests/test_ps1_memory_bus.cpp`, `tests/test_ps1_diagnostic_frontier.cpp`, `tests/test_ps1_interrupt_continuation.cpp`.

- [ ] Wrong address, width, direction, PC/state identity => no accidental consumption.
- [ ] Successful consumption exactly once; second access returns real behavior.
- [ ] Supported MMIO cannot be replaced by diagnostic override.
- [ ] Runtime copy A/B with different candidates remains isolated.
- [ ] Interrupt becoming pending between frontier and retry cannot consume override from unrelated access; exact retry or fail-closed terminal diagnostic.
- [ ] Delay-slot and existing pending-load boundary cases.
- [ ] Commit: `test: harden PS1 diagnostic override boundaries`.

### Task C4 — Optional `LWL/LWR` eligibility gate

**Modify:** `tests/test_r3000a_unaligned.cpp`, `tests/test_ps1_diagnostic_frontier.cpp`; modify `src/core/ps1_boot_runtime.cpp` only when RED cases prove exact eligibility.

- [ ] Verify offsets 0..3 merge semantics using reference executor.
- [ ] Test diagnostic stop/retry with preexisting destination/pending load.
- [ ] If exact identity/retry is proven, minimally extend eligibility; otherwise lock `LWL/LWR` non-expandable with the missing invariant named in test.
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

- [ ] Assert exact strict stop first for every builder.
- [ ] Then assert deep/omega provenance, convergence/cycle, progress metrics.
- [ ] No copied commercial instruction blocks.
- [ ] Commit: `test: add synthetic MAX3 OMEGA corpus`.

### Task C6 — Deterministic property/metamorphic matrix

**Create:** `tests/test_ps1_max3_property_matrix.cpp`; modify `CMakeLists.txt`.

Finite dimensions: load family, width, fallback source/value class, alias, pending-load state, legal normal/delay-slot context, match/mismatch override identity.

- [ ] Invariants: no partial retire; one-shot consumption; canonical unarmed state identity; equivalent aliases -> equivalent device semantics; history-only changes do not change future-state hash; strict mode independent of candidate policy.
- [ ] Run matrix twice in process and compare aggregate deterministic hash/counts.
- [ ] Include checked 64-bit counter/size arithmetic edge cases required by OMEGA limits.
- [ ] Commit: `test: add deterministic MAX3 property matrix`.

### Task C7 — Exact bounded frontier context and tails

**Modify:** `src/core/ps1_max3_explorer.{h,cpp}`, `tests/test_ps1_max3_explorer.cpp`.

Append exact context model:

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

Tail caps: trace 16; BIOS 8; MMIO 8; CD 8. Use newest segment entries. Do not add a fake DMA transfer getter; path/report DMA counter already exists.

- [ ] RED capture equals current bus/CD/GPU public getters.
- [ ] RED tails retain latest exact N and explorer records omitted counts in context metadata.
- [ ] GREEN read-only capture; no hardware behavior changes.
- [ ] Plan B later owns all serialization tests.
- [ ] Commit: `feat: capture MAX3 frontier context`.

### Task C8 — CPU/BIOS/IRQ context completeness

**Modify:** `src/core/ps1_max3_explorer.{h,cpp}`, `tests/test_ps1_max3_explorer.cpp`.

Append available scalar metadata without raw memory:

- CPU: delay-slot active/branch PC, pending-load valid/reg, exception code/fault address/width.
- BIOS: table/selector, A0–A3, RA/callsite from existing recent BIOS event.
- IRQ: accepted count, available COP0 EPC/status/cause summary, continuation status using already-modeled state.

- [ ] RED synthetic BIOS/IRQ context tests.
- [ ] GREEN scalar/bounded metadata only.
- [ ] Plan B serializes these fields after Plan C promotion.
- [ ] Commit: `feat: enrich MAX3 CPU and IRQ context`.

### Task C9 — Permanent regressions for prior commercial frontiers

**Modify:** existing PS1 CDROM/MAX³/interrupt/memory tests.

Synthetic regressions cover I_STAT read32 `0x1F801070`, HSTS `0x1F801800`, RESULT FIFO `0x1F801801` at index 1, interrupt guest callback/B0:17, pending-load interrupt boundary, no phantom A0:35, and diagnostic override behavior.

- [ ] Corpus assertion: none reappear as strict terminal dependencies.
- [ ] Full Linux suite.
- [ ] Windows/MSVC exact-SHA CI required before Plan B.
- [ ] Commit: `test: preserve PS1 commercial frontier regressions`.

## Plan C promotion gate

No partial retire/stale override in covered boundaries; future-state identity is complete; exact cycles safe; context/tails bounded/legal; generated matrix deterministic; prior frontiers fixed; Linux + Windows/MSVC green on one SHA.
