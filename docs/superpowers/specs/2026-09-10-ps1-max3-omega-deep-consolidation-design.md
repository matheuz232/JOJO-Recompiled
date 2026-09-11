# PS1 MAX³ OMEGA Deep Consolidation — Design

Date: 2026-09-10
Branch: `feature/ps1-gp0-dma2-bios-frontier-v0`
Design base: `dda482e45e0c7eacd927beca6bf23c6ba5556e84`
Supersedes operational scope of: `2026-09-10-ps1-max3-deep-frontier-v1-design.md`

## 1. Purpose

The user wants the next diagnostic build to maximize the amount of useful correction, coverage, infrastructure, and build preparation that can be extracted from each commercial checkpoint. The request to multiply the previous proposal by an extremely large factor is interpreted as a requirement to maximize useful diagnostic search and automation, **not** to create literal billions of tests, branches, commits, or brute-force executions.

MAX³ OMEGA turns the existing one-frontier-at-a-time workflow into a deterministic, coverage-guided, provenance-preserving diagnostic system. It should discover, classify, cluster, rank, and where safe continue past many downstream blockers in one run, while keeping production behavior strict and keeping every speculative assumption visible.

The primary objective is to reduce the user-visible cadence from:

`commercial checkpoint -> one blocker -> one fix -> new build -> new checkpoint`

into, whenever hardware semantics permit:

`commercial OMEGA checkpoint -> ranked frontier graph -> batch of evidence-backed fixes -> validation build/checkpoint`.

OMEGA must improve discovery capacity without weakening evidence standards. Unsupported writes, device commands, DMA starts, GPU commands, or other side-effectful hardware events are never silently fabricated merely to make the game progress farther.

## 2. Relationship to existing Deep Frontier work

OMEGA incorporates the already approved Deep Frontier v1 architecture and the implementation work completed before this design.

At design base `dda482e45e0c7eacd927beca6bf23c6ba5556e84`, the repository already has foundations for:

- one-shot diagnostic MMIO read overrides;
- exact re-execution of blocked R3000A loads through the normal reference executor;
- strict/speculative evidence classes;
- typed diagnostic decisions;
- structured frontier kinds;
- path-local speculative depth;
- unique-frontier records;
- compatibility-preserving MAX³ public aggregate layout.

These foundations are retained. OMEGA does not rewrite working infrastructure for stylistic reasons.

The unfinished Deep Frontier v1 Tasks 4–6 are absorbed into OMEGA:

- deep MMIO exploration becomes OMEGA's adaptive read-frontier engine;
- v2 serialization becomes the OMEGA checkpoint schema;
- commercial Deep activation becomes explicit `strict`, `deep`, and `omega` presets;
- limits, ranking, provenance, deterministic replay, and CI are extended substantially.

The previous Deep Frontier v1 design remains historical documentation, but where this design conflicts with it, this OMEGA design is authoritative for future implementation.

## 3. Core invariants

OMEGA is governed by the following non-negotiable invariants.

### 3.1 Strict production behavior remains authoritative

Normal execution never receives fabricated device values or side effects. Diagnostic continuation is opt-in and isolated from production semantics.

### 3.2 Speculative evidence never becomes strict evidence by propagation

Once a path depends on any diagnostic assumption, every descendant is speculative until the path terminates. A later deterministic access does not erase the earlier assumption.

### 3.3 Read continuation and side-effect fabrication are different categories

OMEGA may continue only through operations whose blocked behavior can be represented safely as a diagnostic read value and re-executed through existing CPU semantics.

OMEGA may not fabricate unknown writes, device commands, DMA transfers, GPU drawing, CD-ROM state transitions, interrupt sources, timers, sector delivery, VRAM writes, frame presentation, or guest callbacks that require behavior not already implemented.

### 3.4 Determinism is a feature, not an optimization

The same executable fixture, initial device state, options, and build must produce the same exploration order, candidate values, state hashes, frontier IDs, pruning decisions, ranking, and checkpoint serialization on repeated runs.

### 3.5 Diagnostic scale must be achieved through reduction, not blind brute force

OMEGA should explore equivalence classes and constraints rather than enumerate the full numeric domain of a read. If a downstream instruction only tests bit 0, candidate values should represent bit-0 classes instead of attempting billions of arbitrary 32-bit values.

### 3.6 Every optimization must preserve provenance

Pruning may reduce work, but it may not hide why a frontier was reached or why a branch was not explored.

## 4. Execution profiles

OMEGA introduces three explicit diagnostic profiles.

### 4.1 `strict`

`strict` is the production-evidence profile.

- no MMIO fallback branching;
- existing implemented BIOS/HLE/device semantics only;
- existing MAX³ BIOS diagnostic branching may remain available only where the current tool explicitly invokes it, but descendants are marked speculative;
- no adaptive candidate inference;
- no speculative device state;
- suitable for proof that a frontier is genuinely reached by the current implementation.

### 4.2 `deep`

`deep` is the conservative successor to Deep Frontier v1.

- expandable BIOS fallbacks already supported by the runtime;
- unsupported read-only MMIO load continuation;
- deterministic candidate set compatible with the prior design (`0`, `1`, width-masked all-ones) plus any explicitly observed already-known value when doing so does not add device semantics;
- modest node/frontier/depth budgets;
- intended for routine diagnostic checkpoints.

### 4.3 `omega`

`omega` is the aggressive commercial diagnostic profile.

It adds:

- adaptive candidate generation;
- equivalence-class and constraint-guided values;
- coverage-guided queue prioritization;
- dominance pruning;
- exact-cycle and near-cycle detection;
- root-cause frontier clustering;
- subsystem-specific context capture;
- larger adaptive resource envelopes;
- milestone hunting for first DMA/GPU/VRAM/frame progress;
- frontier priority scoring for batch implementation planning.

The user-facing `EXECUTAR CHECKPOINT` action should use the `omega` preset once OMEGA is fully validated. Normal runtime remains strict.

## 5. Resource model and adaptive budgets

OMEGA must be aggressive but bounded.

### 5.1 Budget dimensions

The explorer tracks independent hard limits for:

- total nodes;
- total retired instructions;
- total unique frontiers;
- total unique diagnostic states;
- total queued states;
- total serialized frontier/event payload;
- total branch depth;
- path-local speculative depth;
- per-frontier candidate count;
- per-frontier descendant expansion budget;
- repeated-state/cycle budget;
- stagnation budget.

No single dimension is allowed to become an implicit unlimited resource.

### 5.2 OMEGA preset

The OMEGA commercial preset uses an adaptive envelope rather than one massive fixed search.

Initial envelope:

- `max_nodes = 16383`;
- `max_branch_depth = 18`;
- `max_speculative_depth = 24`;
- `max_unique_frontiers = 96`;
- `max_total_retired = 3,000,000,000`;
- `max_candidates_per_read = 8`;
- existing large trace capacity remains bounded and should be reduced by per-frontier tails where possible.

Expandable ceiling, only when the explorer is still discovering materially new states/frontiers at a useful rate and memory safeguards allow it:

- `max_nodes <= 262143`;
- `max_branch_depth <= 64`;
- `max_speculative_depth <= 64`;
- `max_unique_frontiers <= 1024`;
- `max_total_retired <= 10,000,000,000`;
- `max_candidates_per_read <= 16`.

These ceilings are not targets. OMEGA should normally terminate much earlier through deduplication, pruning, terminal side effects, or lack of new coverage.

### 5.3 Adaptive expansion policy

The explorer may expand the current envelope only when all are true:

- recent nodes produced new state hashes, frontiers, subsystem coverage, or progress landmarks;
- queue diversity is above a deterministic threshold;
- memory/checkpoint-size guards are healthy;
- the current stop would otherwise be a budget stop rather than a true terminal hardware frontier.

The policy must be deterministic and integer-based; no wall-clock timing or random sampling may alter search results.

## 6. Diagnostic read-frontier continuation

OMEGA preserves the one-shot read override architecture.

### 6.1 Eligibility

A read frontier is expandable only if the runtime proves that:

- the terminal event is an unsupported MMIO **read**;
- the blocked operation is a normal supported R3000A load form that can be re-executed correctly;
- the exact PC, physical address, width, read direction, and opcode are known;
- no side effect was partially committed before the stop;
- the frontier is not an unsupported device command disguised as a read;
- continuation does not require writing unknown device state first.

### 6.2 Supported load families

At minimum the current continuation contract covers:

- `LB`;
- `LBU`;
- `LH`;
- `LHU`;
- `LW`.

OMEGA should add coverage and, only if the reference executor's behavior can be proven exact, may extend the diagnostic eligibility layer to `LWL`/`LWR`. Such extension must be independently TDD-proven before those opcodes become expandable.

### 6.3 One-shot identity

The armed override identity includes enough state to prevent accidental consumption by another access. At minimum:

- guest PC;
- physical address;
- access width;
- read direction;
- opcode/raw instruction when available;
- candidate value.

Consumption is exactly once. Wrong address, wrong width, wrong direction, wrong PC, stale runtime state, or second consumption must fail closed.

### 6.4 Architectural re-execution

Applying a fallback never directly patches destination GPRs, pending loads, PC, next-PC, or delay-slot state. It only arms the diagnostic bus result. The original instruction is then re-executed by the normal R3000A executor so sign extension, zero extension, load delay, pending-load retirement, exceptions, and PC semantics remain architectural.

## 7. Candidate-value intelligence

The central OMEGA scaling mechanism is choosing a small, high-value set of representative read results.

### 7.1 Candidate sources

Candidates may come from deterministic classes:

1. baseline values:
   - zero;
   - one;
   - width-masked all-ones;
2. width boundary values:
   - highest sign bit only;
   - lowest/highest unsigned boundary relevant to width;
3. immediately observed comparison/mask constraints derived from nearby guest instructions;
4. values previously observed from the same address/width in strict execution;
5. values previously selected for the same frontier that produced distinct downstream coverage;
6. values representing branch-equivalence classes inferred from deterministic local dataflow;
7. small single-bit candidates when subsequent operations clearly test individual bits.

### 7.2 Forbidden candidate sources

OMEGA must not use:

- randomness;
- wall-clock time;
- host-specific values;
- address-derived pseudo-random values;
- proprietary table dumps embedded into tests;
- undocumented device-specific magic values merely because they make execution continue;
- values learned only from a speculative descendant and then mislabeled as strict observations.

### 7.3 Local constraint reduction

OMEGA may inspect a bounded deterministic window of decoded guest instructions after the blocked load to infer value equivalence classes.

Examples:

- `ANDI rt, rt, 1` means candidates only need to represent result bit 0 clear/set for that immediate use;
- `BEQ rt, zero` means zero and representative non-zero are sufficient for that comparison;
- `SLTIU rt, ..., N` may produce representatives below/at/above the threshold;
- a mask such as `0x20` may justify testing bit-5 clear/set representatives.

This is diagnostic search reduction, not symbolic execution proof. Any candidate derived this way remains speculative.

### 7.4 Candidate canonicalization

Candidates are masked to access width, deduplicated in stable order, and capped by `max_candidates_per_read`.

The candidate record stores its source class so the checkpoint can distinguish baseline, observed, mask-derived, threshold-derived, or coverage-retained candidates.

## 8. Coverage-guided exploration

OMEGA changes search priority from pure depth-first traversal to deterministic coverage-guided ordering while preserving reproducibility.

### 8.1 Coverage dimensions

The explorer tracks at least:

- unique diagnostic state hashes;
- unique PCs reached;
- unique decoded R3000A opcodes exercised;
- exception/interrupt classes;
- BIOS table/selector pairs;
- MMIO address/width/read-write tuples;
- CD-ROM commands and register-bank contexts;
- GPU GP0/GP1 command families already visible to implemented diagnostics;
- DMA channels/modes already visible to implemented diagnostics;
- interrupt acceptance/continuation states;
- progress landmarks.

### 8.2 Queue priority

Candidate child states are prioritized deterministically by a tuple such as:

1. new strict/observable progress landmark;
2. new unique frontier class;
3. new subsystem coverage;
4. new PC/opcode/MMIO/BIOS coverage;
5. fewer speculative assumptions;
6. shallower speculative depth;
7. lower deterministic node insertion sequence.

The exact comparator must be centralized and unit tested.

### 8.3 No random fuzzing in OMEGA v1

OMEGA uses deterministic generated fixtures and candidate enumeration. Randomized fuzzing is outside this milestone because reproducibility and explainable provenance are more important for the commercial checkpoint workflow.

## 9. State identity, deduplication, and cycle detection

### 9.1 Complete diagnostic state hash

`diagnostic_state_hash()` must include every state element that can affect future execution, including:

- R3000A GPR/HI/LO/PC/next-PC;
- pending load;
- branch/delay-slot state;
- COP0 and interrupt-pending state;
- relevant GTE state already modeled;
- RAM/device state already included by the bus;
- HLE BIOS state;
- interrupt continuation state;
- CD-ROM/GPU/DMA state already modeled;
- armed diagnostic read override;
- any OMEGA continuation state that can affect the next instruction.

Purely historical counters or formatting metadata must not split otherwise identical execution states.

### 9.2 Exact-state deduplication

The same execution state under an equal-or-weaker assumption cost is not expanded twice.

### 9.3 Dominance pruning

If two paths converge to the same architectural diagnostic state and one path has:

- no better strict progress;
- no additional unique coverage needed for reporting;
- equal or greater speculative depth;
- equal or greater assumption count;

then the dominated path may be recorded but not expanded.

Dominance decisions are serialized in node diagnostics.

### 9.4 Exact-cycle detection

Repeated state hashes on one path form an exact cycle. OMEGA records the cycle and stops expanding once the deterministic repeat policy is satisfied.

### 9.5 Near-cycle detection

OMEGA may identify near-cycles only when the differing fields are explicitly classified as non-semantic diagnostic counters. It may never ignore arbitrary architectural/device state merely to force convergence.

Near-cycle equivalence rules must be explicit and covered by tests before use.

## 10. Frontier model and graph

Every blocker becomes a structured frontier.

### 10.1 Frontier classes

At minimum:

- BIOS/HLE frontier;
- expandable MMIO read;
- terminal MMIO write;
- CD-ROM command/device frontier;
- GPU command frontier;
- DMA frontier;
- interrupt/callback frontier;
- CPU instruction/exception frontier;
- diagnostic stall;
- execution-budget boundary;
- malformed/unreadable guest state;
- fatal runtime error;
- other terminal.

### 10.2 Frontier identity

Identity includes the semantically relevant subset of:

- class;
- evidence class;
- PC/opcode;
- address/width/direction/value;
- BIOS table/selector;
- device/channel/command identifiers where already modeled;
- diagnostic state hash;
- assumption-context hash where needed to avoid merging semantically different speculative states.

### 10.3 Occurrence aggregation

Repeated instances of the same unique frontier increase occurrence counters rather than creating duplicate implementation tasks.

Each frontier tracks:

- first occurrence node;
- total occurrence count;
- strict occurrence count;
- speculative occurrence count;
- maximum downstream descendant count;
- subsystem;
- expandable/terminal classification;
- parent frontier and decision when applicable;
- assumption chain or compact chain reference;
- progress immediately before the frontier.

### 10.4 Root-cause clustering

OMEGA may group multiple unique callsites into a higher-level root-cause cluster when they clearly share the same missing primitive, such as the same unsupported address/width/direction or same BIOS selector.

Clustering must preserve original frontier records and cannot erase callsite evidence.

## 11. Provenance and assumption chains

Every speculative decision is typed.

Decision kinds include:

- BIOS fallback;
- MMIO read fallback.

A read decision records:

- frontier ID;
- address/width;
- candidate value;
- candidate source class;
- parent evidence class;
- speculative depth after application.

A downstream frontier can therefore answer:

- which strict frontier started the speculative chain;
- what values were assumed;
- which BIOS fallbacks were taken;
- how many assumptions were required;
- whether another path reached the same blocker with fewer assumptions.

Assumption chains may be internally interned/deduplicated to control memory and file size, but serialization must remain human-readable.

## 12. Progress landmarks and milestone hunting

OMEGA explicitly tracks high-value milestones.

Required landmarks include first observed:

- accepted interrupt;
- CD-ROM command;
- DMA transfer/start recognized by implemented diagnostics;
- GP0 command;
- GP1 command;
- VRAM write;
- presented frame.

The explorer also records progress maxima for these counters.

### 12.1 First-VRAM hunter

The ranking engine may prioritize branches that increase GPU/DMA progress toward the first VRAM write, but may not fabricate GPU/DMA writes to achieve that goal.

### 12.2 First-frame hunter

Likewise, once VRAM activity exists, OMEGA may prioritize paths that approach presentation. A speculative `presented_frames > 0` is reported as speculative progress, not proof of working rendering.

## 13. Subsystem diagnostic context

Frontier records should contain bounded context specific to their subsystem.

### 13.1 CPU context

- PC;
- opcode;
- stop/exception class;
- delay-slot state summary;
- pending-load summary;
- relevant fault address/width when available.

### 13.2 BIOS context

- table/domain;
- selector;
- A0–A3 argument registers;
- RA/callsite;
- bounded recent BIOS-call tail.

### 13.3 MMIO context

- guest/physical address;
- width;
- read/write;
- value for writes;
- bounded recent MMIO tail;
- whether event was strict or diagnostic.

### 13.4 CD-ROM context

Only already-modeled state may be serialized, such as:

- current index/bank;
- status/result availability;
- current interrupt status/enable when implemented;
- last command summary;
- bounded recent command tail.

No proprietary sector contents are serialized.

### 13.5 GPU context

- GP0/GP1 counters;
- last known command summary available from modeled state;
- DMA relationship if already known;
- VRAM write/presentation counters.

No new drawing behavior is invented for diagnostics.

### 13.6 DMA context

Where modeled, include channel, direction, synchronization mode, addresses/counts, trigger/start state, and related terminal MMIO access.

Unknown DMA side effects remain terminal.

### 13.7 Interrupt context

- I_STAT/I_MASK summary;
- interrupt acceptance count;
- interrupted PC/EPC when available;
- continuation active/restored/terminal state;
- callback/ReturnFromException phase where modeled.

## 14. Checkpoint format: `jojo-max3-checkpoint-v2`

OMEGA standardizes the next checkpoint format as `jojo-max3-checkpoint-v2`.

### 14.1 Identity section

The file includes:

- format version;
- repository/build commit SHA when available at build time;
- diagnostic profile (`strict`, `deep`, `omega`);
- configuration fingerprint;
- all relevant limits;
- deterministic explorer/schema version identifiers.

### 14.2 Health summary

A compact summary near the top reports:

- termination reason;
- whether termination was a true terminal frontier or a resource limit;
- total retired instructions;
- total/strict/speculative frontiers;
- states visited/deduplicated/pruned;
- maximum speculative depth;
- candidate branches considered/expanded/pruned;
- progress landmarks;
- best strict progress;
- best speculative progress;
- top recommended frontier clusters.

### 14.3 Frontier table

Each frontier exposes its structured identity, evidence class, occurrence stats, parent/provenance, subsystem, context summary, and expansion status.

### 14.4 Node diagnostics

Node serialization remains available but can be bounded or summarized to prevent enormous files. Every omitted/truncated section must state its count and truncation policy.

### 14.5 Best-path section

The best strict path and best overall diagnostic path are reported separately when they differ.

### 14.6 Bounded event tails

Instead of one enormous duplicated global trace, v2 favors bounded tails associated with important frontiers, while preserving enough global best-report information for compatibility.

### 14.7 Compatibility section

Existing v1 fields that downstream tooling/tests rely on remain present with the same semantic meaning during transition. New v2 fields are additive.

## 15. Checkpoint self-validation and parser

OMEGA adds a lightweight parser/validator for the line-oriented v2 schema.

It validates at least:

- required header/version;
- unique frontier IDs;
- parent references;
- valid evidence/profile enums;
- deterministic numeric formatting;
- internally consistent counts;
- complete configuration fingerprint;
- clear truncation markers when sections are bounded.

The validator does not parse or store proprietary game payloads because such payloads are not part of the schema.

## 16. Coverage census and delta reporting

OMEGA builds internal diagnostic censuses for synthetic and commercial runs.

Census categories include:

- R3000A opcode families;
- CPU boundary/exception types;
- BIOS selectors;
- MMIO tuples;
- CD-ROM commands/register contexts;
- GPU command families visible to current diagnostics;
- DMA channels/modes visible to current diagnostics;
- frontier classes.

When two checkpoints/build reports are compared by tooling/tests, deltas may report:

- newly covered items;
- eliminated frontiers;
- newly exposed frontiers;
- reclassified strict/speculative frontiers;
- progress landmark changes;
- regression indicators.

The first OMEGA milestone need not provide a full user-facing graphical diff tool, but the underlying deterministic data must make such comparison possible.

## 17. Synthetic frontier replay and regression corpus

Commercial evidence must be converted into legal, synthetic regression fixtures whenever feasible.

A frontier replay fixture contains only the minimum synthetic program/device setup necessary to exercise the missing behavior. It must not contain copied commercial code blocks, RAM dumps, sectors, BIOS bytes, textures, audio, or other proprietary payloads.

Previously fixed regressions remain permanent coverage, including at least:

- I_STAT 32-bit read behavior;
- CD-ROM HSTS read behavior;
- RESULT FIFO read independent of CD bank index;
- interrupt guest callback continuation;
- ReturnFromException continuation;
- pending-load behavior at interrupt/diagnostic boundaries;
- no phantom BIOS frontier regressions already fixed.

OMEGA adds fixtures for every new diagnostic continuation primitive before that primitive is considered complete.

## 18. Property and metamorphic test matrices

OMEGA increases test density without manually writing millions of cases.

### 18.1 MMIO/load property matrix

Deterministically generate combinations over:

- load opcode family;
- width;
- candidate value classes;
- sign/zero extension;
- physical/KSEG0/KSEG1 aliases where valid;
- pending-load presence;
- delay-slot state;
- matching/mismatching override identity.

### 18.2 Metamorphic invariants

Examples:

- equivalent valid aliases must reach equivalent device semantics;
- consuming a one-shot override must restore canonical unarmed hash state;
- changing only diagnostic history must not change architectural state hash;
- changing a semantically relevant device register must change state identity;
- strict execution with Deep/OMEGA disabled must not depend on candidate policy.

### 18.3 Deterministic generated CPU corpus

Small synthetic R3000A programs may be generated from fixed enumerations/seeds encoded in source to exercise combinations of loads, branches, delay slots, exceptions, and interrupts. No nondeterministic fuzzing is required.

## 19. Ranking and implementation priority

OMEGA separates **path ranking** from **frontier implementation priority**.

### 19.1 Path ranking

Primary observable progress remains:

1. presented frames;
2. VRAM writes;
3. GP0 progress;
4. GP1 progress;
5. DMA progress;
6. CD-ROM progress;
7. interrupt/guest-callback progress;
8. new unique frontiers/coverage;
9. retired instructions.

Evidence quality breaks ties before raw instruction count:

- strict outranks speculative equivalent;
- fewer assumptions outrank more;
- lower speculative depth outranks higher.

### 19.2 Frontier implementation priority

Each frontier/cluster receives a deterministic priority score based on:

- strict evidence weight;
- frequency;
- number of descendant frontiers blocked behind it;
- progress landmarks reachable downstream;
- number of callsites affected;
- subsystem criticality to boot/render progression;
- confidence that one implementation resolves the cluster.

Speculative-only frontiers may inform investigation but may not outrank a comparable strict frontier simply because a speculative path ran farther.

The checkpoint presents a ranked implementation queue, not automatic production patches.

## 20. Memory, queue, and checkpoint-size safeguards

OMEGA must fail gracefully under large searches.

Safeguards include:

- hard state/queue/node caps;
- assumption-chain interning;
- event-tail interning or bounded storage;
- frontier occurrence aggregation;
- optional bounded node serialization while preserving counts;
- 64-bit counters for potentially large totals;
- explicit overflow checks before increments/multiplication where relevant;
- deterministic termination rather than host OOM.

A resource-stop reason must distinguish at least node, retired, frontier, state/queue, and serialized-output caps when materially different.

## 21. Error handling and fail-closed behavior

OMEGA never turns uncertainty into silent success.

Examples:

- malformed blocked instruction -> terminal CPU/frontier diagnostic;
- unsupported load form -> terminal, not guessed;
- override mismatch -> rejected;
- stale override -> cleared/rejected according to tested contract;
- nested interrupt state outside current continuation model -> terminal;
- unreadable guest callback memory -> terminal;
- unknown side-effectful device operation -> terminal;
- checkpoint validator inconsistency -> diagnostic failure, not a valid checkpoint.

Fatal/internal errors must include enough bounded context for reproduction without dumping proprietary memory.

## 22. CI architecture

Windows/MSVC remains the authority for the user-facing build, but OMEGA expands validation depth.

### 22.1 Required Linux lane

- Release configure/build;
- full ctest;
- production-readiness gate;
- PS1 active-architecture gate;
- observed-disc revision contract;
- UDP contract;
- OMEGA deterministic synthetic tests;
- checkpoint v2 serializer/parser round-trip tests.

### 22.2 Required Windows/MSVC lane

- Release build;
- full Windows test set;
- same readiness/architecture/contracts;
- OMEGA critical deterministic tests;
- artifact upload.

### 22.3 Sanitizer/UB lane

Where repository/tooling permits without destabilizing required runners, add a portable sanitizer lane covering the core diagnostic components. This lane may be separate from the required release artifact and must not make the Windows executable depend on sanitizer runtimes.

### 22.4 Determinism stress

A bounded synthetic OMEGA scenario is executed repeatedly in CI and its canonical report/hash is compared across repetitions. Linux/Windows may have different non-semantic build metadata, but the architectural exploration result must match where the schema defines platform-independent determinism.

### 22.5 Warning policy

Warnings introduced by OMEGA in modified code are treated as defects, especially:

- unhandled enum values;
- narrowing conversions;
- signed/unsigned overflow risks;
- unreachable switch cases;
- aggregate compatibility mistakes.

## 23. Artifact integrity and commercial checkpoint preset

The final OMEGA Windows artifact is accepted only when:

- Linux and Windows required jobs are green on the exact same SHA;
- the branch HEAD matches that SHA;
- artifact ZIP digest is recorded;
- local downloaded ZIP hash matches GitHub digest;
- EXE hash is recorded;
- ZIP contents are audited;
- no proprietary payload appears in the artifact;
- `EXECUTAR CHECKPOINT` is wired to the validated OMEGA preset;
- the checkpoint identifies its build SHA/profile/schema.

The user should not need to configure dozens of OMEGA flags manually for the normal diagnostic workflow.

## 24. Legal and repository hygiene

OMEGA continues the existing evidence policy.

Never commit or package:

- game ISO/BIN/CUE content;
- BIOS dumps;
- commercial executable bytes copied from the game;
- RAM/VRAM dumps from the commercial game;
- sectors;
- textures/audio/video/assets;
- checkpoint payloads containing prohibited raw proprietary data.

Permitted repository evidence consists of synthetic fixtures, documentation, implementation code, tests, hashes, addresses, register values, counters, bounded diagnostic metadata, and legally derived behavioral facts.

User-supplied commercial checkpoints are external evidence only.

## 25. Explicit non-goals

OMEGA is diagnostic infrastructure. This design does **not** authorize speculative production implementation of:

- new GPU drawing/rendering pipelines;
- DMA linked-list behavior;
- unsupported DMA transfers;
- new CD-ROM commands/state transitions;
- sector/data FIFO semantics not already proven;
- timers/VBlank;
- new IRQ sources;
- arbitrary BIOS/HLE implementations;
- native x64 lowering;
- presentation/framebuffer backend work;
- gameplay claims;
- automatic code generation of hardware behavior from speculative frontiers.

When OMEGA exposes strict evidence that one of these subsystems is the next true blocker, that subsystem receives its own bounded or architectural design/TDD gate as required.

## 26. Implementation decomposition

OMEGA is too large for one undifferentiated commit. The implementation plan must decompose it into independently reviewable TDD milestones.

Expected high-level sequence:

1. finish/stabilize current MAX³ v2 provenance model and eliminate warnings;
2. deep MMIO read branching using existing one-shot continuation;
3. candidate source model and deterministic baseline candidates;
4. local constraint/equivalence candidate inference;
5. state deduplication/dominance/cycle infrastructure;
6. coverage census and deterministic queue priority;
7. frontier clustering and implementation-priority scoring;
8. subsystem bounded-context capture;
9. checkpoint v2 serializer;
10. checkpoint v2 parser/self-validator;
11. property/metamorphic/generated synthetic test corpus;
12. adaptive resource manager and OMEGA preset;
13. strict/deep/omega CLI/UI/checkpoint wiring;
14. CI hardening, sanitizer/determinism lanes where viable;
15. artifact integrity and final commercial checkpoint build.

The detailed implementation plan may split these further to keep each RED→GREEN cycle small enough to review.

## 27. TDD and review policy

Every behavior-changing milestone follows:

1. RED test/fixture first;
2. confirm failure is for the intended missing behavior;
3. minimal GREEN implementation;
4. local/synthetic regression review;
5. Linux CI validation;
6. Windows/MSVC validation when the milestone touches production/runtime/public interfaces;
7. scope/diff review before the next milestone.

Unexpected failures require root-cause analysis before fixes. No pile-up of unrelated fixes is allowed simply because the overall OMEGA scope is large.

## 28. Success criteria

OMEGA v1 is complete only when all of the following are true.

### Core correctness

1. strict mode is behaviorally isolated from OMEGA speculation;
2. supported one-shot read continuation preserves R3000A semantics;
3. unsupported writes/commands/side effects remain terminal;
4. provenance survives every speculative descendant;
5. state identity covers every semantically relevant diagnostic continuation state.

### Search capability

6. a synthetic chained-read fixture discovers multiple downstream frontiers in one run;
7. candidate-equivalence reduction explores distinct branch classes without brute-force value enumeration;
8. exact-state deduplication and dominance pruning reduce redundant expansions deterministically;
9. cycle detection terminates synthetic loops without losing the frontier record;
10. coverage-guided ordering is deterministic.

### Reporting

11. `jojo-max3-checkpoint-v2` includes build/profile/config identity;
12. strict and speculative frontiers are unambiguous;
13. assumption chains identify every diagnostic decision;
14. frontier clusters/frequency/descendant counts are reported;
15. a ranked implementation queue is produced;
16. parser/self-validation accepts generated reports and rejects inconsistent fixtures;
17. compact health/progress summary makes the next blocker visible without reading the full file.

### Test density

18. generated property/metamorphic tests cover load/MMIO continuation matrices;
19. all previously fixed commercial frontiers remain in regression coverage;
20. deterministic replay tests pass repeatedly;
21. resource-limit and overflow-edge tests pass.

### CI/build

22. required Linux suite/gates pass;
23. required Windows/MSVC suite/gates pass on the same final SHA;
24. OMEGA-specific deterministic tests pass on both supported CI environments where applicable;
25. artifact ZIP/EXE hashes are verified;
26. the user-facing Windows executable contains no proprietary game data;
27. `EXECUTAR CHECKPOINT` uses the validated OMEGA preset.

### Commercial validation

28. the first OMEGA commercial checkpoint is able to report more than the first strict blocker when safe read continuations exist downstream, or clearly explains why further discovery is blocked by a true side-effectful frontier;
29. the report gives enough ranked evidence to batch multiple bounded corrections when multiple strict/authoritatively-supported frontiers are available;
30. no speculative-only discovery is presented as proof that gameplay/rendering works.

## 29. Definition of “100-billion-times larger” for this project

The user's scaling request is satisfied through **algorithmic leverage**, not literal object count.

OMEGA is designed so a small deterministic set of candidate equivalence classes can represent enormous raw value spaces; deduplication and dominance collapse repeated execution states; frontier clustering collapses repeated symptoms; generated tests cover combinatorial matrices without hand-writing every case; and one commercial run can expose a graph of blockers instead of one blocker.

Therefore the relevant scaling metric is not “number of branches created.” It is the ratio of useful distinct evidence discovered per user checkpoint and per unit of CI/runtime cost.

OMEGA should maximize that ratio while keeping every result reproducible, explainable, legally clean, and separated into strict versus speculative evidence.
