# PS1 OMEGA Frame-First Phase B — Strict-First Ranking and Commercial Success Gating

Date: 2026-09-11
Design branch: `design/ps1-omega-frame-first-phase-b`
Baseline: `40549e6988ef90a351970cd9166a4c58dc14a62c`
Parent design: `docs/superpowers/specs/2026-09-11-ps1-omega-frame-first-design.md`

## 1. Purpose

Phase B changes OMEGA's authority model before any new PS1 hardware semantics are added.

The objective is to make strict execution dominate diagnostic speculation in ranking and commercial success reporting. OMEGA may continue to expand speculative descendants for blocker discovery, but a speculative path must never outrank an available strict path merely because it reached more graphics, DMA, CD-ROM, IRQ, or frame-progress counters.

This phase does not implement new GPU, DMA, IRQ, timer, CD-ROM, GTE, BIOS, SIO, or SPU behavior.

## 2. Approved policy

The approved ranking policy is strict-first.

For any two OMEGA search candidates:

1. `strict` evidence outranks `speculative` evidence unconditionally.
2. Only candidates in the same evidence class are compared by frame-first progress.
3. Existing deterministic tie-breakers remain in force after progress comparison.

The intended order is:

1. evidence class: `strict` > `speculative`;
2. `presented_frames`;
3. `vram_write_count`;
4. `gpu_gp0_command_count`;
5. `gpu_gp1_command_count`;
6. `dma_transfer_count`;
7. `cdrom_command_count`;
8. interrupt/callback progress;
9. new frontier count;
10. new coverage count;
11. fewer assumptions;
12. smaller speculative depth;
13. greater retired count;
14. earlier insertion sequence.

This order applies to queue ranking, best-node selection, and any other decision that uses `Ps1Max3SearchScore` ordering.

## 3. State dominance

State dominance must use the same authority rule.

A speculative state may not dominate a strict state with the same diagnostic state hash, even if the speculative state has larger progress counters.

A strict state may dominate a speculative state when all other dominance requirements are met.

Within the same evidence class, the existing progress-not-worse and assumption/speculative-depth rules remain unchanged.

## 4. Commercial frame gating

`commercial_frame_presented` is authoritative only on strict execution.

If a node or segment is speculative and its underlying runtime report would otherwise contain `Ps1BootStopReason::commercial_frame_presented`, OMEGA must not expose that result as a commercial success.

The required behavior is:

- speculative frame-like progress may remain visible through diagnostic counters and node provenance;
- speculative descendants remain eligible for diagnostic exploration;
- speculative results must not become an authoritative `best_report` commercial success;
- any API that returns the checkpoint result used to judge commercial boot must not convert a speculative descendant into success;
- a strict `commercial_frame_presented` remains valid and must outrank every speculative result.

This gate is defensive even if the current runtime cannot yet produce a commercial frame. The invariant must exist before later GPU/DMA work can make such a state reachable.

## 5. Search behavior

This phase intentionally reduces the ability of speculative progress to displace strict progress in the priority queue.

OMEGA still expands speculative descendants according to existing branch-depth, speculative-depth, queue, descendant, frontier, and retired budgets. Candidate generation is unchanged.

The policy does not disable speculation. It changes its authority:

- strict paths are always searched first when both strict and speculative candidates are pending;
- speculative paths are searched when no higher-ranked strict candidate remains or when budgets allow;
- speculative paths continue to reveal downstream blockers for engineering guidance;
- speculative progress is never evidence that commercial boot succeeded.

## 6. Frontier cluster ranking

Frontier clusters already record strict and speculative occurrence counts. Phase B aligns cluster ranking with the same authority rule.

A cluster containing strict evidence must outrank a purely speculative cluster before descendant count, occurrence count, callsite count, or progress score are considered.

Within the same evidence class, existing deterministic cluster scoring can continue to reward frame-first progress.

This keeps the highest-ranked implementation target anchored to an observed strict blocker rather than a deeper speculative branch.

## 7. Data model changes

Prefer minimal data-model changes.

Existing fields are sufficient for the core policy:

- `Ps1Max3EvidenceClass`;
- `Ps1Max3NodeSummary::evidence`;
- `Ps1Max3NodeSummary::speculative_depth`;
- `Ps1Max3Frontier::evidence`;
- `Ps1Max3FrontierCluster::evidence`;
- existing path progress counters;
- `Ps1BootStopReason::commercial_frame_presented`.

Do not add a second parallel evidence system.

Add a dedicated report field only if implementation proves it is required to expose the best strict result without ambiguity. If added, it must be derived, deterministic, bounded, and additive to existing report compatibility.

## 8. Runtime boundary

`Ps1BootRuntime` remains unaware of OMEGA evidence class.

The runtime executes concrete semantics and emits `Ps1BootReport`. OMEGA owns strict/speculative provenance because speculation is created by OMEGA diagnostic fallbacks.

Therefore:

- do not add speculative flags to production GPU, DMA, CD-ROM, BIOS, CPU, or memory semantics;
- do not move ranking or provenance policy into `Ps1BootRuntime`;
- keep `apply_diagnostic_bios_fallback()` and `apply_diagnostic_mmio_read_fallback()` as the points that create speculative descendants;
- preserve strict runtime stop behavior for unsupported operations.

## 9. Local-evidence API behavior

`bootstrap_runtime_local_evidence_to_file()` must remain diagnostic and must not claim commercial success from speculation.

If the existing `Ps1Max3Report::best_report` can become speculative after future work, Phase B must ensure the value returned through the local-evidence API is strict-safe.

Acceptable implementations are:

- make `best_report` strict-authoritative under the new ranking; or
- keep a diagnostic best result and expose a separate strict-best result used by the API.

The implementation plan should choose the smaller design that preserves report compatibility and makes the invariant testable.

## 10. Determinism

Strict-first ranking must be deterministic.

Given the same executable, options, candidate generation, and runtime semantics:

- queue order must be stable;
- best-node selection must be stable;
- dominance decisions must be stable;
- frontier-cluster order must be stable;
- serialized reports must remain deterministic apart from intentionally added deterministic fields.

No randomness or wall-clock data is introduced.

## 11. RED → GREEN contracts

Implementation must begin with tests that fail against the Phase A baseline.

Required RED contracts:

1. a strict candidate with less graphics progress outranks a speculative candidate with more `presented_frames`;
2. the same rule holds for VRAM, GP0, GP1, DMA, CD-ROM, interrupt progress, frontier count, and coverage count;
3. a speculative state cannot dominate an equivalent strict state;
4. a strict state can dominate an equivalent speculative state when all other dominance requirements are satisfied;
5. best-node selection remains strict-first;
6. frontier clusters with strict evidence outrank purely speculative clusters regardless of deeper speculative progress;
7. a speculative descendant cannot surface authoritative `commercial_frame_presented` through OMEGA/local-evidence result paths;
8. strict `commercial_frame_presented` remains valid;
9. repeated runs produce identical ordering and report output.

Tests must include negative coverage proving that speculation still expands; Phase B must not accidentally disable diagnostic exploration.

## 12. Files expected to change

Primary expected files:

- `src/core/ps1_max3_search_policy.cpp`
- `src/core/ps1_max3_search_policy.h` only if interface changes are necessary
- `src/core/ps1_max3_explorer.cpp`
- `src/core/ps1_max3_explorer.h` only if an additive strict-best field is required
- `src/core/ps1_max3_frontier_priority.cpp`
- `src/core/ps1_max3_report_io.cpp` only if an additive field is introduced
- `src/core/runtime.cpp` only if local-evidence needs an explicit strict-best handoff
- tests covering search policy, explorer, frontier priority, report I/O, and local evidence
- `CMakeLists.txt` only if a new dedicated test target is preferred

No device implementation file should change unless a compile-only interface dependency requires a mechanical update. Any semantic device change is out of scope.

## 13. Non-goals

Phase B does not:

- implement new GP0 commands;
- implement VRAM rasterization;
- implement GPU DMA transfers;
- implement new DMA channels or modes;
- implement IRQ/timer semantics;
- implement new CD-ROM commands or sector transfer;
- implement new GTE commands;
- implement additional BIOS/kernel services;
- implement SIO/controller behavior;
- implement SPU behavior;
- enlarge OMEGA budgets;
- alter candidate value generation;
- claim that JoJo is playable;
- claim that a synthetic frame is a commercial frame.

## 14. Exit gate

Phase B is complete when:

- strict evidence unconditionally outranks speculative evidence in search ordering;
- state dominance obeys the same authority rule;
- frontier-cluster priority obeys strict-first authority;
- speculative exploration still works within existing budgets;
- speculative descendants cannot create authoritative commercial-frame success;
- strict commercial-frame success remains valid;
- deterministic regression tests pass;
- full Linux CI is green;
- full Windows x64/MSVC CI is green;
- `JOJO-Recompiled-Windows-x64` is produced from the exact Phase B HEAD;
- no new PS1 hardware semantics were introduced.

The next phase starts from this green baseline and uses the next real JoJo checkpoint to select the first evidence-backed GPU/DMA blocker batch.
