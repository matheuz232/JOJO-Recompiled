# PS1 OMEGA Frame-First — Design

Date: 2026-09-11
Design branch: `design/ps1-omega-frame-first`
Primary integration target: `main`
Source diagnostic line to consolidate: `feature/ps1-gp0-dma2-bios-frontier-v0`

## 1. Purpose

Advance JoJo PS1 toward its first real commercial frame as quickly as possible while preserving strict architectural truth.

This design consolidates the mature MAX³ OMEGA diagnostic line with the current `main` line and then uses an explicitly frame-first development order. The objective is not to complete PlayStation emulation broadly before progress can be observed. The objective is to identify the exact blockers on JoJo's real boot path, implement only the minimum semantically correct behavior required to cross those blockers, and repeat until the first commercial frame is produced by strict execution.

OMEGA remains a diagnostic exploration system. It may speculate to discover likely downstream blockers, but speculative continuation is evidence only. It must never turn a speculative path into a claim of commercial boot, rendering, or frame presentation.

## 2. Success criterion

The milestone is complete only when JoJo reaches `commercial_frame_presented` through a strict execution path using implemented CPU, BIOS/HLE, memory, and device semantics.

A speculative OMEGA descendant may reveal a likely GPU/DMA/IRQ/CD/GTE path toward a frame, but it cannot satisfy the milestone.

The first-frame hierarchy is:

1. first strict GP0 command observed;
2. first strict DMA transfer targeting the GPU;
3. first strict VRAM mutation attributable to JoJo execution;
4. first strict valid display configuration;
5. first strict presentable framebuffer state;
6. first strict `commercial_frame_presented` event.

Each earlier item is a progress landmark, not a substitute for the final criterion.

## 3. Architectural baseline and consolidation

Before new frame-first work begins, the repository must have one authoritative development line containing both:

- current `main`, including the modular `Ps1HleBios` boundary and the validated MAX³ multicycle integration;
- the mature OMEGA line from `feature/ps1-gp0-dma2-bios-frontier-v0`, including its deeper diagnostic search, coverage tracking, budgets, candidate engine, frontier prioritization, interrupt continuation, CD-ROM state, GPU/GTE work, and related tests/specs.

The branches currently diverge from a common historical base. Consolidation therefore must be treated as a semantic merge, not a blind ref update.

Required consolidation rules:

- preserve the modular HLE ownership boundary from current `main`;
- preserve the broader BIOS/kernel behavior from the mature OMEGA line where it is already evidence-backed;
- preserve strict runtime behavior and keep diagnostic fallback outside production semantics;
- preserve deterministic diagnostic hashing and MAX³ deduplication;
- preserve all Linux and Windows gates;
- do not drop OMEGA coverage, candidate, budget, priority, CD-ROM, interrupt, GPU, or GTE tests merely to make the merge easier;
- resolve duplicate or conflicting HLE implementations into one canonical `Ps1HleBios` component.

## 4. Frame-first subsystem order

Implementation priority is determined by the next real JoJo blocker, not by subsystem completeness.

Default priority order is:

1. GPU control/data path: GP1, GP0, VRAM-visible effects, display state;
2. DMA needed to feed GPU work, especially GPU DMA paths;
3. IRQ and timers when they block GPU/DMA progress or guest kernel scheduling;
4. CD-ROM runtime behavior when required to load data needed before frame presentation;
5. GTE transfers and mathematical commands when required by the observed rendering path;
6. BIOS/kernel HLE services required by the same path;
7. SIO/controller only if input initialization blocks boot before first frame;
8. SPU only if audio-side initialization is proven to block progress before first frame.

This order is advisory. Evidence from strict JoJo execution overrides it immediately.

## 5. OMEGA's role

OMEGA is the blocker discovery engine for frame-first development.

It must classify and rank frontiers across at least:

- BIOS A0/B0/C0/SYS services;
- GP0 writes and command boundaries;
- GP1 control traffic;
- DMA register access and channel activation;
- IRQ controller behavior;
- root counters/timers;
- CD-ROM registers, commands, responses, IRQs, and sector-flow boundaries;
- GTE/COP2 control, data-transfer, and command boundaries;
- SIO/controller accesses;
- SPU accesses;
- unsupported CPU or memory behavior.

OMEGA should prefer frontiers that move the run toward one of the first-frame landmarks.

Examples of high-priority frontiers include:

- first unsupported GP0 command after otherwise valid GPU setup;
- first DMA2 activation toward GP0;
- IRQ behavior immediately following a GPU/DMA/CD event;
- a GTE command on a path that has already reached real graphics traffic;
- a CD-ROM command required to load assets after display initialization.

## 6. Strict versus speculative execution

Strict execution is authoritative.

Diagnostic speculation may only be used to expose additional downstream dependencies. Existing OMEGA invariants remain in force:

- unknown side-effectful hardware behavior is not silently fabricated;
- speculative evidence remains speculative for all descendants;
- deterministic candidate generation is required;
- the same inputs and options must produce the same search order and reports;
- unsupported writes, device commands, DMA starts, GPU drawing, CD state transitions, timer transitions, interrupt sources, SPU behavior, controller behavior, VRAM writes, or frame presentation may not be invented merely to continue farther.

A path that contains any speculative assumption is ineligible to emit `commercial_frame_presented`.

If a speculative path reaches a state that would otherwise qualify as presentable, OMEGA records it as a diagnostic milestone candidate with provenance and gives the blocking strict frontier maximum implementation priority.

## 7. Minimal-correct semantics policy

When strict execution reaches a new blocker, implementation work should target the smallest semantically correct unit that explains and crosses that exact boundary.

Examples:

- implement a documented GP0 command family only when JoJo reaches that command;
- implement DMA channel behavior only to the extent required by the observed transfer mode and device endpoint;
- implement IRQ acknowledgement/admission behavior only when required by the captured path;
- implement the exact CD command/response/IRQ sequence reached by JoJo before adding unrelated commands;
- implement the exact GTE command(s) reached by JoJo before broad GTE coverage;
- implement SIO or SPU only if they become pre-frame blockers.

The policy forbids two failure modes:

1. pretending unsupported hardware succeeded to make the boot advance;
2. spending large amounts of time implementing unrelated PS1 functionality that JoJo has not yet demonstrated it needs before the first frame.

## 8. GPU and frame presentation boundary

The project must distinguish GPU control from real frame production.

GP1 initialization alone is not rendering.

A valid strict frame milestone requires all of the following to be grounded in implemented semantics:

- guest-visible GPU state is valid for the observed path;
- any GP0 work that produced the framebuffer was processed by implemented command semantics;
- any DMA transfer feeding GP0 was executed by implemented DMA semantics;
- VRAM state reflects those real operations;
- display start/mode/range state identifies a valid presentable region;
- the frame-present event is emitted only from strict state.

A blank or reset-only GPU state does not count as a commercial frame unless JoJo itself has strictly produced and selected that state as the actual displayed output.

## 9. Diagnostic reporting

OMEGA reports must make frame-first progress obvious.

At minimum, preserve existing frontier and provenance data and add or retain explicit counters/landmarks for:

- GP0 command count;
- GP1 command count;
- GPU DMA transfer count/bytes where semantically available;
- VRAM mutation count or equivalent bounded progress metric;
- display configuration validity;
- presented-frame count;
- DMA channel activity;
- IRQ/timer activity relevant to the path;
- CD command/IRQ/sector progress;
- GTE transfer/command progress;
- strict versus speculative provenance for each landmark.

Reports must remain bounded and derived. They must not serialize proprietary game data, unrestricted guest RAM, BIOS bytes, sectors, textures, audio, or other copyrighted payloads.

## 10. Search priority

OMEGA queue ordering should prioritize progress toward the first frame.

A deterministic score should reward, in descending importance:

- new strict first-frame landmarks;
- new strict subsystem coverage on the current graphics/boot path;
- speculative paths that reveal a likely immediate blocker behind an already strict frontier;
- new unique root-cause frontiers;
- new architectural/device states;
- deeper execution only when it continues producing meaningful coverage.

A large instruction count without new state, frontier, or subsystem coverage is not progress by itself.

## 11. Test strategy

All implementation after consolidation follows RED → GREEN.

Required test classes:

- merge-preservation tests proving current modular HLE behavior remains intact;
- OMEGA regression tests proving coverage, budgets, candidate generation, frontier ranking, and determinism remain intact;
- per-device unit tests for each newly implemented JoJo-required behavior;
- boot-runtime integration tests for exact observed boundaries;
- strict/speculative provenance tests;
- negative tests proving unsupported side effects still stop explicitly;
- frame-gating tests proving speculative descendants cannot emit `commercial_frame_presented`;
- deterministic replay tests covering diagnostic hashes and OMEGA ordering;
- Linux and Windows CI gates for all production changes.

Synthetic tests prove semantics only. They do not by themselves prove JoJo commercial boot or rendering.

## 12. Commercial validation loop

After each evidence-backed implementation batch:

1. build and run the full Linux/Windows CI suite;
2. produce the Windows artifact;
3. run OMEGA against the user's locally prepared legal JoJo installation;
4. inspect only bounded derived diagnostics;
5. identify the next strict blocker and any speculative downstream hints;
6. implement the smallest correct semantics for the highest-priority blocker;
7. repeat until strict first-frame success.

No commercial game image, extracted proprietary payload, BIOS ROM, unrestricted memory dump, raw sector dump, texture dump, or audio dump is committed to the repository or CI.

## 13. Non-goals before first frame

Unless strict JoJo evidence requires them first, the following are explicitly deferred:

- full PS1 compatibility;
- unrelated BIOS services;
- complete DMA behavior for unused channels;
- full timer accuracy;
- complete CD-ROM command coverage;
- complete GTE instruction coverage;
- complete SPU synthesis;
- full controller/memory-card support;
- timing-perfect GPU rasterization beyond what JoJo needs for the observed frame path;
- native x64 recompilation work unrelated to reaching the frame;
- support for unrelated PS1 games.

## 14. Completion definition

The OMEGA Frame-First milestone is complete when:

- current `main` and mature OMEGA functionality have been consolidated into one validated line;
- Linux and Windows CI are green;
- JoJo reaches `commercial_frame_presented` on a strict path;
- the presented frame is backed by implemented CPU/BIOS/device semantics rather than diagnostic fabrication;
- the checkpoint report marks the frame as strict and preserves the provenance needed to reproduce the path.

Anything short of this remains progress evidence, not completion.

## 15. Implementation decomposition

This design is intentionally an umbrella architecture. It is too large for one implementation plan and must be executed as a sequence of independently reviewable subprojects.

### Phase A — semantic consolidation

Create one integration line from current `main` and `feature/ps1-gp0-dma2-bios-frontier-v0`.

Primary goal: preserve both the current modular HLE architecture and the mature OMEGA capabilities without adding new device semantics.

Exit gate: Linux and Windows CI green, all retained contracts passing, one canonical HLE implementation, deterministic OMEGA regression coverage intact.

### Phase B — frame-first observability and gating

Add or normalize first-frame landmarks, provenance, subsystem classification, ranking, and the hard rule that speculative descendants cannot emit `commercial_frame_presented`.

Primary goal: make the next commercial checkpoint identify the highest-value frame blocker unambiguously.

Exit gate: deterministic synthetic contracts for frame landmarks and strict/speculative gating, plus green Linux/Windows CI.

### Phase C — first GPU/DMA blocker batch

Use strict commercial evidence from the consolidated OMEGA checkpoint to implement the smallest correct GPU/GP0/DMA semantics required by the highest-ranked pre-frame blocker.

This phase is evidence-dependent: its exact commands, registers, transfer modes, and tests are not selected in advance.

Exit gate: the captured strict blocker is crossed by implemented semantics and the next checkpoint reaches a strictly later frontier or first-frame landmark.

### Phase D — iterative evidence-driven unlocks

Repeat small RED → GREEN batches for IRQ/timers, CD-ROM, GTE, BIOS/kernel, SIO, SPU, or additional GPU/DMA behavior only when strict JoJo execution proves that subsystem is the next pre-frame blocker.

Each blocker class receives its own implementation plan rather than accumulating unrelated device work in one giant plan.

Exit gate for each batch: the exact strict frontier is crossed without weakening unsupported-behavior boundaries or diagnostic provenance.

### Phase E — strict first-frame validation

Once a presentable framebuffer path exists, validate the complete strict provenance chain from guest execution through any required BIOS, DMA, GPU, VRAM, and display state to `commercial_frame_presented`.

Exit gate: repeatable strict commercial frame evidence, green Linux/Windows CI, and no speculative ancestor in the frame path.

The first implementation plan produced from this architecture must cover **Phase A only**. Later phases are planned from the evidence produced by the preceding phase.
