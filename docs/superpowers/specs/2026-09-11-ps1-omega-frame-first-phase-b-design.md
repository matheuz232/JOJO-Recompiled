# PS1 OMEGA Frame-First Phase B — Strict-First Ranking and Commercial Success Gating

Date: 2026-09-11
Design branch: `design/ps1-omega-frame-first-phase-b`
Baseline: `40549e6988ef90a351970cd9166a4c58dc14a62c`
Parent design: `docs/superpowers/specs/2026-09-11-ps1-omega-frame-first-design.md`

## 1. Purpose

Phase B changes OMEGA's authority model before any new PS1 hardware semantics are added.

The objective is to make strict execution dominate diagnostic speculation in ranking and commercial success reporting. OMEGA may continue to expand speculative descendants for blocker discovery, but a speculative path must never outrank an available strict path merely because it reached more graphics, DMA, CD-ROM, IRQ, or frame-progress counters.

This phase does not implement new GPU, DMA, IRQ, timer, CD-ROM, GTE, BIOS, SIO, or SPU behavior.

The umbrella design called for frame-first observability and gating. For this phase, the already-existing GP0, GP1, DMA, VRAM, CD-ROM, interrupt, and presented-frame counters are the observability substrate. No parallel landmark model is introduced until strict commercial evidence proves that an additional representation is necessary.

## 2. Approved policy

The approved ranking policy is strict-first.

For any two OMEGA search candidates:

1. `strict` evidence outranks `speculative` evidence unconditionally.
2. Only candidates in the same evidence class are compared by frame-first progress.
3. Existing deterministic tie-breakers remain in force after progress comparison.

The required order is:

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

This order applies to queue ranking, best-node selection, and every decision that uses `Ps1Max3SearchScore` ordering.

## 3. State dominance

State dominance must use the same authority rule.

A speculative state may not dominate a strict state with the same diagnostic state hash, even if the speculative state has larger progress counters.

A strict state may dominate a speculative state when all other dominance requirements are met.

Within the same evidence class, the existing progress-not-worse and assumption/speculative-depth rules remain unchanged.

## 4. Authoritative best result

`Ps1Max3Report::best_node`, `best_path`, and `best_report` are strict-authoritative in Phase B.

The root OMEGA work item is strict, so every successful exploration has at least one strict node. The best-result fields therefore never need to fall back to a speculative node.

Speculative descendants remain in `nodes`, `frontiers`, dependency data, search statistics, and diagnostic reports. They may show deeper graphics or device progress, but they cannot replace the strict-authoritative best result.

This is deliberately different from the Phase A policy, where progress counters could cause a speculative node to become the best node.

## 5. Commercial frame gating

`commercial_frame_presented` is authoritative only on strict execution.

If a speculative node's underlying runtime segment reaches `Ps1BootStopReason::commercial_frame_presented`, that occurrence is diagnostic evidence only. It may remain represented inside the node-level diagnostic history with `evidence == speculative`, but it must not become:

- `Ps1Max3Report::best_report`;
- the result returned by `bootstrap_runtime_local_evidence_to_file()`;
- a successful `bootstrap_runtime()` outcome;
- a strict first-frame claim in user-facing or serialized summary fields.

For this design, “emit `commercial_frame_presented`” means exposing it through an authoritative best/checkpoint/commercial-success result. Raw speculative node evidence is allowed only when its speculative provenance is preserved.

A strict `commercial_frame_presented` is valid, must outrank every speculative result, and may become the authoritative best report.

This gate is defensive even if the current runtime cannot yet produce a commercial frame. The invariant must exist before later GPU/DMA work can make such a state reachable.

## 6. Search behavior

This phase intentionally reduces the ability of speculative progress to displace strict progress in the priority queue.

OMEGA still expands speculative descendants according to existing branch-depth, speculative-depth, queue, descendant, frontier, and retired budgets. Candidate generation is unchanged.

The policy does not disable speculation. It changes its authority:

- strict paths are always searched first when both strict and speculative candidates are pending;
- speculative paths are searched when no higher-ranked strict candidate remains or when budgets allow;
- speculative paths continue to reveal downstream blockers for engineering guidance;
- speculative progress is never evidence that commercial boot succeeded.

## 7. Frontier cluster ranking

Frontier clusters already record strict and speculative occurrence counts. Phase B aligns cluster ranking with the same authority rule.

A cluster containing strict evidence must outrank a purely speculative cluster before descendant count, occurrence count, callsite count, or progress score are considered.

The implementation must compare evidence class explicitly before numeric cluster priority so strict-first remains unconditional rather than depending on the magnitude of a weight constant.

Within the same evidence class, existing deterministic cluster scoring can continue to reward frame-first progress.

This keeps the highest-ranked implementation target anchored to an observed strict blocker rather than a deeper speculative branch.

## 8. Data model

No new parallel evidence or landmark model is introduced in Phase B.

Existing fields are the canonical source of truth:

- `Ps1Max3EvidenceClass`;
- `Ps1Max3NodeSummary::evidence`;
- `Ps1Max3NodeSummary::speculative_depth`;
- `Ps1Max3Frontier::evidence`;
- `Ps1Max3FrontierCluster::evidence`;
- existing path progress counters;
- `Ps1BootStopReason::commercial_frame_presented`.

`Ps1Max3Report::best_node`, `best_path`, and `best_report` are redefined by policy as strict-authoritative; no `strict_best_*` duplicate fields are added.

Report format changes are unnecessary unless tests expose an existing serialized field that would otherwise falsely label speculative progress as authoritative success. Any such fix must be additive or compatibility-preserving and deterministic.

## 9. Runtime boundary

`Ps1BootRuntime` remains unaware of OMEGA evidence class.

The runtime executes concrete semantics and emits `Ps1BootReport`. OMEGA owns strict/speculative provenance because speculation is created by OMEGA diagnostic fallbacks.

Therefore:

- do not add speculative flags to production GPU, DMA, CD-ROM, BIOS, CPU, or memory semantics;
- do not move ranking or provenance policy into `Ps1BootRuntime`;
- keep `apply_diagnostic_bios_fallback()` and `apply_diagnostic_mmio_read_fallback()` as the points that create speculative descendants;
- preserve strict runtime stop behavior for unsupported operations.

## 10. Local-evidence API behavior

`bootstrap_runtime_local_evidence_to_file()` continues to return `max3.value.best_report`.

Because Phase B makes `best_report` strict-authoritative, this existing API becomes strict-safe without introducing a second result channel.

Consequences:

- the saved full OMEGA report may still contain speculative nodes and downstream hints;
- the `Ps1BootReport` returned by the local-evidence API is always derived from a strict node;
- a speculative descendant can never make this API return `commercial_frame_presented`;
- no public API signature changes are required.

## 11. Determinism

Strict-first ranking must be deterministic.

Given the same executable, options, candidate generation, and runtime semantics:

- queue order must be stable;
- best-node selection must be stable;
- dominance decisions must be stable;
- frontier-cluster order must be stable;
- serialized reports must remain deterministic.

No randomness or wall-clock data is introduced.

## 12. RED → GREEN contracts

Implementation must begin with tests that fail against the Phase A baseline.

Required RED contracts:

1. a strict candidate with less graphics progress outranks a speculative candidate with more `presented_frames`;
2. the same rule holds for VRAM, GP0, GP1, DMA, CD-ROM, interrupt progress, frontier count, and coverage count;
3. a speculative state cannot dominate an equivalent strict state;
4. a strict state can dominate an equivalent speculative state when all other dominance requirements are satisfied;
5. best-node selection remains strict-first even when a speculative descendant has greater frame-first counters;
6. `best_report` and `best_path` correspond to that strict best node;
7. frontier clusters with strict evidence outrank purely speculative clusters regardless of deeper speculative progress;
8. a speculative descendant cannot surface authoritative `commercial_frame_presented` through `best_report` or the local-evidence result path;
9. strict `commercial_frame_presented` remains valid and becomes authoritative when it is the highest-ranked strict node;
10. speculation still expands under deep/omega profiles after strict-first ranking is introduced;
11. repeated runs produce identical ordering and report output.

## 13. Files expected to change

Primary expected files:

- `src/core/ps1_max3_search_policy.cpp`
- `src/core/ps1_max3_explorer.cpp`
- `src/core/ps1_max3_frontier_priority.cpp`
- existing tests for search policy, explorer, frontier priority, and local evidence

Possible compatibility-only files if a failing test proves they are needed:

- `src/core/ps1_max3_report_io.cpp`
- `src/core/runtime.cpp`
- `CMakeLists.txt` only if a dedicated new test target is clearer than extending existing suites

No device implementation file should change. Any semantic GPU, DMA, CD-ROM, IRQ, timer, GTE, BIOS, SIO, SPU, CPU, or memory change is out of scope.

## 14. Non-goals

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
- add a duplicate strict-best report model;
- claim that JoJo is playable;
- claim that speculative frame evidence is a commercial frame.

## 15. Exit gate

Phase B is complete when:

- strict evidence unconditionally outranks speculative evidence in search ordering;
- `best_node`, `best_path`, and `best_report` are strict-authoritative;
- state dominance obeys the same authority rule;
- frontier-cluster priority obeys strict-first authority through explicit evidence comparison;
- speculative exploration still works within existing budgets;
- speculative descendants cannot create authoritative commercial-frame success;
- strict commercial-frame success remains valid;
- deterministic regression tests pass;
- full Linux CI is green;
- full Windows x64/MSVC CI is green;
- `JOJO-Recompiled-Windows-x64` is produced from the exact Phase B HEAD;
- no new PS1 hardware semantics were introduced.

The next phase starts from this green baseline and uses the next real JoJo checkpoint to select the first evidence-backed GPU/DMA blocker batch.
