# PS1 MAX³ Deep Frontier v1 — Design

Date: 2026-09-10
Branch: `feature/ps1-gp0-dma2-bios-frontier-v0`
Design base: `c44060e31712b9e539e7f7ed9a06015e6f81886f`

## 1. Purpose

The current checkpoint workflow intentionally stops at the first unsupported commercial frontier and produces strong evidence, but that makes progress iterative: one checkpoint usually reveals one blocking MMIO/BIOS frontier, followed by one implementation cycle and another checkpoint.

MAX³ Deep Frontier v1 adds a second, explicitly speculative exploration layer so one checkpoint can discover several *possible downstream* blockers without weakening the strict evidence path. The goal is to turn repeated `checkpoint -> bounded fix -> checkpoint` cycles into larger evidence-backed batches where possible.

This feature does **not** promise that one checkpoint can reveal every future blocker. Frontiers whose continuation would require inventing real side effects remain terminal.

## 2. Existing behavior that must remain authoritative

The existing MAX³ explorer already:

- creates a real `Ps1BootRuntime` root;
- runs strict runtime segments;
- records BIOS, speculative-MMIO, and terminal-MMIO dependencies;
- branches four ways only for unimplemented BIOS calls (`return_zero`, `return_one`, `return_minus_one`, `preserve_v0`);
- deduplicates BIOS frontiers using `(table, selector, state_hash)`;
- ranks paths by observable progress (presented frames, VRAM writes, GPU/CD/DMA progress, dependency count, retired instructions);
- serializes the result as `jojo-max3-checkpoint-v1`.

Deep Frontier v1 must preserve all strict results byte-for-byte in meaning. A strict frontier is never silently converted into a supported production behavior.

## 3. Core principle: strict evidence and speculative discovery are different products

Every discovered frontier is assigned an evidence class:

1. `strict` — reached by real runtime behavior only. This is production-grade evidence and may justify a bounded implementation.
2. `speculative` — reached only after one or more diagnostic continuations. It is discovery evidence, not proof that production should implement the path.

A frontier reached through a speculative ancestor can never be promoted to `strict` inside the same exploration, even when the final access itself is deterministic.

The report must make this distinction explicit at node, dependency/frontier, and path levels.

The existing BIOS fallback branches are diagnostic continuations and therefore become explicitly classified as speculative in v2 reporting, even when deep MMIO exploration is disabled. This is a reporting clarification, not a behavioral change to existing BIOS branching.

## 4. Scope of speculative continuation in v1

### 4.1 Supported speculative frontier classes

Deep Frontier v1 may expand only these classes:

- unimplemented BIOS calls already handled by `apply_diagnostic_bios_fallback`;
- unsupported **MMIO reads** that are represented as a normal R3000A load and have no write side effect.

### 4.2 MMIO read fallback values

For an unsupported MMIO read, the explorer may branch on a deterministic ordered value set, masked to the access width:

1. `0x00000000`;
2. `0x00000001`;
3. all ones for the width (`0xFF`, `0xFFFF`, or `0xFFFFFFFF`).

Duplicate values after masking are removed while preserving order. No address-derived, random, timing-derived, or device-specific guesses are allowed.

These values are deliberately small and generic. They are not hardware claims.

### 4.3 Frontiers that remain terminal

Deep Frontier v1 must **not** continue through:

- unsupported MMIO writes;
- unimplemented device commands that require side effects;
- GPU command writes that are unsupported;
- DMA starts/transfers that are unsupported;
- CD-ROM commands/parameter/data operations that are unsupported;
- CPU instruction/exception boundaries without an existing strict runtime implementation;
- unreadable guest memory needed by a callback/interrupt continuation;
- nested-interrupt boundaries;
- any frontier for which the runtime cannot prove the blocked operation is a read-only load.

These are recorded and exploration stops on that path.

## 5. One-shot MMIO read override

A new diagnostic-only runtime mechanism will allow the explorer to continue exactly one unsupported MMIO read without changing production MMIO semantics.

### 5.1 Contract

`Ps1BootRuntime` receives a diagnostic API conceptually equivalent to:

- inspect whether the most recent terminal boundary is an expandable unsupported MMIO read;
- apply one fallback value to that exact blocked access;
- resume execution by re-executing the blocked load through normal R3000A execution.

The override is one-shot and must be keyed strongly enough that it cannot affect a later unrelated read. At minimum the identity includes:

- blocked guest PC;
- physical MMIO address;
- access width;
- read direction.

The override is consumed by that one access and then cleared. Applying an override is rejected unless the runtime is still stopped at the exact matching frontier.

### 5.2 Load semantics

The continuation must preserve the semantics of the original load instruction, including:

- destination register;
- sign/zero extension for the actual opcode;
- R3000A load-delay behavior;
- PC/next-PC behavior;
- pending-load retirement rules already implemented by the reference executor.

The explorer must not patch registers directly if doing so would bypass these semantics. The implementation must use a one-shot diagnostic bus read override consumed while the executor re-executes the blocked instruction.

Tests must also prove that reaching an unsupported read does not partially retire that blocked load before the fallback is applied.

### 5.3 Production isolation

The override API is diagnostic-only:

- normal runtime runs never install it;
- no supported MMIO address is changed;
- no fallback value is retained after consumption;
- the diagnostic override participates in `diagnostic_state_hash()` while armed so MAX³ cannot deduplicate distinct pending speculative states incorrectly.

## 6. Deep exploration model

### 6.1 Options and limits

`Ps1Max3Options` gains explicit deep-mode controls. Defaults preserve current behavior.

Required controls:

- `deep_frontier_enabled` — default `false`;
- `max_unique_frontiers` — default `32` when deep mode is enabled;
- `max_speculative_depth` — default `8` when deep mode is enabled;
- existing `max_branch_depth` remains the total diagnostic-continuation depth guard;
- existing `max_nodes` and `max_total_retired` remain hard global bounds.

A child may be expanded only when both conditions hold:

- `depth < max_branch_depth`;
- `speculative_depth < max_speculative_depth`.

For compatibility, strict/default test options keep their existing `max_branch_depth` behavior. `ps1_max3_local_evidence_options()` enables Deep Frontier v1 for user checkpoints and sets both `max_branch_depth` and `max_speculative_depth` to `8`, so the new speculative-depth limit is not accidentally shadowed by the old default depth of `6`.

`max_speculative_depth` is **path-local**. Reaching it stops expansion of that node only; sibling paths continue.

`max_unique_frontiers` is **global**. Once the unique-frontier cap has been reached, no new frontier-producing branch may be expanded. If this leaves otherwise-expandable work unexplored, the report termination reason is `frontier_limit`; if no work was truncated, the report may still terminate as `completed`.

`Ps1Max3TerminationReason` therefore gains `frontier_limit`. It does not gain a global `speculative_depth_limit` reason.

### 6.2 Node provenance

Each MAX³ node records:

- whether the node itself is strict or speculative;
- speculative depth;
- the continuation decision used from its parent;
- frontier identity/type that produced that decision;
- existing state hash and progress metrics;
- an optional path-local expansion stop reason (`branch_depth`, `speculative_depth`, `deduplicated`, or terminal frontier class).

Root is always strict with speculative depth zero.

A BIOS or MMIO fallback child is speculative. All descendants remain speculative even if later segments run entirely on implemented behavior.

### 6.3 Frontier identity and deduplication

A unique frontier key must include enough information to avoid merging semantically different blockers.

For BIOS:

- class;
- table;
- selector;
- diagnostic state hash.

For MMIO read:

- class;
- physical address;
- width;
- guest PC;
- opcode;
- diagnostic state hash.

For terminal writes/device/CPU boundaries, the key includes the corresponding stop class plus the available PC/opcode/address/width/value identity needed to distinguish materially different blockers.

Repeated observations of the same key increase an occurrence count rather than consuming another unique-frontier slot.

### 6.4 Search order

Search remains deterministic depth-first using stable fallback order.

BIOS fallback order remains:

1. return zero;
2. return one;
3. return minus one;
4. preserve v0.

MMIO read fallback order is:

1. zero;
2. one;
3. width-masked all ones.

Global termination reasons remain explicit: `completed`, `node_limit`, `total_retired_limit`, or `frontier_limit`. Path-local depth limits do not terminate unrelated siblings and do not by themselves change the global termination reason.

## 7. Frontier graph and dependency chains

The report must expose enough structure to answer not only “what blockers exist?” but also “what had to be assumed to reach each blocker?”

Each unique frontier record includes:

- stable report index;
- evidence class (`strict` or `speculative`);
- frontier kind (`bios`, `mmio_read`, `terminal_mmio_write`, `device_command`, `cpu_boundary`, or existing stop reason mapped explicitly);
- PC and opcode when available;
- BIOS table/selector when applicable;
- address/width/read-write/value when applicable;
- first node where observed;
- occurrence count;
- parent frontier/decision chain needed to reach it;
- whether it was expandable in v1;
- reason it was not expandable when terminal.

A decision record includes its type (`bios_fallback` or `mmio_read_fallback`) and exact chosen value/policy.

This graph is diagnostic provenance. It is not a claim that speculative branches are executable on hardware.

## 8. Ranking

The existing observable-progress ranking remains primary:

1. presented frames;
2. VRAM writes;
3. GP0 commands;
4. GP1 commands;
5. CD-ROM commands;
6. DMA transfers;
7. unique dependencies/frontiers;
8. cumulative retired instructions.

Deep mode adds one tie-break principle **before cumulative retired instructions**: when two paths have otherwise equal progress through item 7, prefer the path with lower speculative depth. A strict path therefore outranks an otherwise-equal speculative path.

Only after speculative depth ties does cumulative retired instruction count break the tie.

This prevents a long speculative path from automatically displacing a shorter evidence-strong path merely because it retired more instructions.

## 9. Report format v2

Deep Frontier v1 introduces `format=jojo-max3-checkpoint-v2`.

The v2 text remains line-oriented and deterministic so existing manual/file tooling stays simple.

It must include:

- all existing run limits and progress metrics;
- `deep_frontier_enabled`;
- `max_unique_frontiers`;
- `max_speculative_depth`;
- `strict_frontier_count`;
- `speculative_frontier_count`;
- `unique_frontier_count`;
- per-frontier provenance fields from section 7;
- per-node evidence class, speculative depth, and path-local expansion-stop reason;
- typed continuation decisions;
- best path with its provenance;
- the existing detailed `best_report` block.

The old `dependency_count` and dependency records remain for compatibility, but v2 separately exposes the unique frontier records and evidence classes. Existing dependency semantics are not repurposed to mean speculative graph nodes.

No proprietary game bytes, RAM dumps, sectors, BIOS code, or assets are added to the report. Existing bounded trace/MMIO/BIOS event diagnostics remain the maximum payload.

## 10. Strict-mode compatibility

When `deep_frontier_enabled=false`:

- no MMIO fallback branches are generated;
- BIOS branching behaves as it does before this feature;
- BIOS fallback descendants are simply labeled `speculative` in v2 provenance;
- strict frontier semantics remain unchanged;
- diagnostic MMIO overrides are never armed.

The serializer uses v2 after the feature lands, but all existing v1 information remains present with the same meaning. Tests must cover semantic equivalence for representative existing fixtures rather than requiring byte-identical v1 text.

## 11. Safety against false paths

Deep Frontier v1 intentionally favors false negatives over false confidence.

Rules:

- speculative discoveries are never labeled strict;
- unsupported writes are never skipped;
- no device state is fabricated to continue a write/command;
- no random fallback values are used;
- one-shot read overrides cannot leak into later accesses;
- reaching VRAM writes or presented frames on a speculative path is reported as speculative progress, not proof of working rendering;
- a production implementation must still be justified by strict commercial evidence or independently authoritative hardware documentation plus a bounded validation path.

## 12. Determinism and MAX³ state identity

Deep mode must remain deterministic across Linux and Windows for synthetic fixtures.

`diagnostic_state_hash()` must distinguish any armed diagnostic continuation state that could change subsequent execution. After an override is consumed, the runtime returns to the canonical unarmed state so dead diagnostic data cannot cause hash divergence.

Repeated runs of the same fixture/options must produce the same:

- node order;
- frontier order;
- decisions;
- best node/path;
- termination reason;
- relevant report text.

## 13. Test strategy

All tests use synthetic PS1 executable fixtures only.

### Runtime tests

- unsupported `LBU` MMIO read can be resumed through a one-shot zero fallback;
- `LB`, `LBU`, `LH`, `LHU`, and `LW` preserve sign/zero extension and load delay;
- blocked load is not partially retired before fallback application;
- override rejects wrong PC/address/width/write direction;
- override is consumed exactly once;
- supported MMIO is never replaced by a diagnostic override;
- diagnostic hash differs while two different overrides are armed and returns to canonical identity after consumption.

### Explorer tests

- strict mode preserves current BIOS-only branching behavior;
- existing BIOS fallback descendants are classified speculative in v2;
- deep mode discovers a second MMIO-read frontier after speculatively crossing the first;
- a downstream frontier is marked speculative and records its assumption chain;
- MMIO writes remain terminal and do not branch;
- unique frontier deduplication counts repeated blockers once;
- `max_unique_frontiers` truncates deterministically and reports `frontier_limit` only when work is actually left unexplored;
- `max_speculative_depth` stops only the affected path and siblings still run;
- `max_branch_depth` and `max_speculative_depth` both gate expansion;
- state-hash deduplication does not merge distinct pending overrides;
- ranking prefers lower speculative depth when progress is otherwise equal;
- repeated exploration yields identical frontier and node ordering.

### Report tests

- v2 header and deep-mode options;
- strict/speculative counts;
- typed decisions;
- provenance chain serialization;
- path-local expansion-stop serialization;
- global `frontier_limit` serialization;
- no proprietary payload fields.

### Integration/CI

- Linux full suite;
- Windows/MSVC full suite;
- production-readiness gate;
- PS1 active-architecture gate;
- observed-disc revision contract;
- UDP contract;
- Windows artifact upload.

Windows/MSVC remains the authority for the user-facing executable.

## 14. Non-goals

Deep Frontier v1 does not implement or relax:

- new CD-ROM commands;
- CD-ROM parameter/data FIFO semantics beyond already-supported production behavior;
- CD-ROM interrupt ACK semantics not already implemented;
- DMA3 or new DMA transfers;
- GPU drawing commands;
- VRAM renderer/presentation;
- timers/VBlank;
- new BIOS/HLE calls;
- guest callback architecture beyond what is already implemented;
- native x64 lowering;
- automatic production patches based solely on speculative evidence.

## 15. Success criteria

The milestone is complete when:

1. a synthetic fixture with chained unsupported MMIO reads produces multiple unique frontiers in one deep run;
2. downstream speculative frontiers are unambiguously labeled and carry their full assumption chain;
3. unsupported writes remain terminal;
4. strict mode remains behaviorally compatible with current MAX³ exploration;
5. deterministic state/hash/report tests pass on Linux and Windows;
6. all repository gates are green on one exact final SHA;
7. a Windows x64 artifact from that SHA is produced for the next commercial deep checkpoint.

The first commercial Deep Frontier checkpoint is a **validation artifact**, not part of the implementation proof. Its purpose is to determine whether one user run now exposes several useful downstream frontiers and therefore reduces checkpoint cadence.