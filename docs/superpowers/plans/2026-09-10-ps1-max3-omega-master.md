# PS1 MAX³ OMEGA Deep Consolidation — Master Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Deliver a deterministic `strict`/`deep`/`omega` MAX³ diagnostic system that discovers, classifies, prunes, ranks, serializes, validates, and exposes many downstream PS1 frontiers from one commercial checkpoint without fabricating unknown hardware side effects.

**Architecture:** OMEGA is implemented as four independently testable sub-projects rather than growing `ps1_max3_explorer.cpp` into a monolith. The core explorer owns profiles, candidates, coverage, queueing, pruning, budgets, and frontier ranking; reporting owns checkpoint v2 serialization/validation; runtime hardening owns CPU/MMIO invariants and synthetic corpus coverage; commercial integration owns Windows activation, CI, artifact integrity, and user-facing checkpoint flow.

**Tech Stack:** C++20, CMake/CTest, GNU/Linux CI, Windows x64/MSVC 2022 CI, Win32 UI, existing PS1 R3000A reference runtime/MAX³ infrastructure.

**Spec:** `docs/superpowers/specs/2026-09-10-ps1-max3-omega-deep-consolidation-design.md`

## Global Constraints

- Design base: `dda482e45e0c7eacd927beca6bf23c6ba5556e84`; documentation-only OMEGA spec commits follow that base.
- Preserve strict production semantics: no diagnostic fallback is active in normal runtime execution.
- A speculative ancestor permanently makes descendants speculative for that exploration path.
- Never fabricate unknown MMIO writes, CD-ROM commands/state transitions, DMA transfers, GPU drawing, IRQ sources, timers, VRAM writes, frame presentation, or guest callbacks.
- Diagnostic read continuation must re-execute the blocked load through the normal R3000A executor; never patch destination GPR/PC/pending-load state directly.
- Search/order/candidate generation/adaptive expansion must be deterministic and independent of wall clock, host speed, free RAM, or randomness.
- OMEGA initial envelope: `max_nodes=16383`, `max_branch_depth=32`, `max_speculative_depth=24`, `max_unique_frontiers=96`, `max_total_retired=3000000000`, `max_candidates_per_read=8`.
- OMEGA hard ceilings: `max_nodes<=262143`, `max_branch_depth<=64`, `max_speculative_depth<=64`, `max_unique_frontiers<=1024`, `max_total_retired<=10000000000`, `max_candidates_per_read<=16`.
- Only synthetic fixtures and legally derived metadata may be committed; never commit proprietary game/BIOS/RAM/sector payloads.
- Every production change follows RED→GREEN, review, then Linux + Windows/MSVC evidence on the exact SHA before promotion.
- Windows/MSVC remains the authority for the user-facing executable.

---

## Plan Suite and Required Order

### Plan A — Explorer Core

`docs/superpowers/plans/2026-09-10-ps1-max3-omega-explorer-core.md`

Delivers:

- profile/preset API;
- deterministic candidate engine;
- coverage model;
- deterministic priority queue;
- exact-state deduplication and dominance pruning;
- deterministic adaptive budget manager;
- deep/omega MMIO-read branching;
- frontier clustering and priority ranking;
- bounds/determinism integration tests.

**Promotion gate A:** strict fixtures preserve previous behavior; deep fixture crosses chained MMIO reads; omega candidate ordering and queue ordering are deterministic; unknown writes/device commands never branch; Linux and MSVC are green.

### Plan B — Checkpoint v2 and Provenance

`docs/superpowers/plans/2026-09-10-ps1-max3-omega-checkpoint-v2.md`

Consumes Plan A public types. Delivers:

- `jojo-max3-checkpoint-v2` serializer;
- parser/validator;
- strict/speculative provenance and typed decisions;
- frontier graph/root-cause cluster records;
- compact summary + forensic body;
- health/stats/priority sections;
- deterministic text and atomic size-bounded save.

**Promotion gate B:** round-trip parser validates emitted reports, deterministic serialization matches byte-for-byte for identical synthetic runs, v1 information remains semantically present, no raw proprietary payload fields exist, Linux/MSVC green.

### Plan C — Runtime and Synthetic Hardening

`docs/superpowers/plans/2026-09-10-ps1-max3-omega-runtime-hardening.md`

Consumes Plans A/B interfaces. Delivers:

- diagnostic state-hash audit;
- full supported load matrix and fail-closed override tests;
- interrupt/load-delay/delay-slot stress cases;
- optional `LWL/LWR` eligibility only if exact behavior is proven by tests;
- deterministic generated synthetic corpus;
- device/frontier context snapshots using already-modeled state;
- property-style and replay-style regression corpus.

**Promotion gate C:** no partial retirement or stale override is possible in tested boundaries; state hash distinguishes future-relevant state; all previously fixed commercial frontiers remain covered; Linux/MSVC green.

### Plan D — Commercial Activation, CI, and Artifact

`docs/superpowers/plans/2026-09-10-ps1-max3-omega-commercial-ci.md`

Consumes A/B/C. Delivers:

- `strict`, `deep`, `omega` preset functions through runtime API;
- `EXECUTAR CHECKPOINT` wired to `omega` only after all prior gates pass;
- explicit build/profile/schema identity in report;
- expanded CTest registration and Linux/MSVC workflow gates;
- sanitizer lane for portable OMEGA core where supported;
- artifact integrity checks and final Windows ZIP;
- docs/status updates.

**Promotion gate D:** one exact SHA passes full Linux and Windows/MSVC suites, readiness/architecture/observed-disc/UDP contracts, OMEGA determinism and checkpoint validator; Windows artifact digest is verified before user delivery.

---

## Master Execution Tasks

### Task 1: Establish OMEGA execution branch baseline

**Files:**
- No production file changes.

**Interfaces:**
- Consumes: approved OMEGA spec and this plan suite.
- Produces: a recorded baseline SHA and green CI evidence used by every sub-plan.

- [ ] **Step 1: Verify branch head and approved spec are present**

Run:

```bash
git rev-parse HEAD
git status --short
test -f docs/superpowers/specs/2026-09-10-ps1-max3-omega-deep-consolidation-design.md
```

Expected: clean working tree; HEAD contains the approved spec and plan suite.

- [ ] **Step 2: Run baseline Linux suite before Plan A**

```bash
cmake -S . -B build -DCMAKE_BUILD_TYPE=Release
cmake --build build --parallel 2
ctest --test-dir build --output-on-failure
```

Expected: all baseline tests pass.

- [ ] **Step 3: Record exact baseline SHA in the implementation ledger**

```bash
git rev-parse HEAD
```

Expected: exact SHA copied into the first Plan A implementation review.

### Task 2: Execute Plan A to its promotion gate

**Files:**
- Follow Plan A exactly.

**Interfaces:**
- Consumes: current MAX³/runtime APIs.
- Produces: stable OMEGA explorer-core interfaces required by B/C/D.

- [ ] **Step 1: Execute each Plan A task RED→GREEN**
- [ ] **Step 2: Review each task before proceeding**
- [ ] **Step 3: Require Linux and Windows/MSVC success on Plan A final SHA**
- [ ] **Step 4: Do not start Plan B until promotion gate A is satisfied**

### Task 3: Execute Plan B to its promotion gate

**Files:**
- Follow Plan B exactly.

**Interfaces:**
- Consumes: Plan A public model.
- Produces: validated checkpoint v2 format used by C/D.

- [ ] **Step 1: Execute each Plan B task RED→GREEN**
- [ ] **Step 2: Run round-trip/determinism checks before integration**
- [ ] **Step 3: Require Linux and Windows/MSVC success on Plan B final SHA**
- [ ] **Step 4: Do not start Plan C until promotion gate B is satisfied**

### Task 4: Execute Plan C to its promotion gate

**Files:**
- Follow Plan C exactly.

**Interfaces:**
- Consumes: explorer core + checkpoint model.
- Produces: hardened runtime/test corpus safe for commercial OMEGA activation.

- [ ] **Step 1: Execute each Plan C task RED→GREEN**
- [ ] **Step 2: Treat every unexpected CPU/runtime failure with systematic-debugging before code changes**
- [ ] **Step 3: Require Linux and Windows/MSVC success on Plan C final SHA**
- [ ] **Step 4: Do not enable the Win32 OMEGA button until promotion gate C is satisfied**

### Task 5: Execute Plan D and produce the commercial artifact

**Files:**
- Follow Plan D exactly.

**Interfaces:**
- Consumes: all prior OMEGA interfaces.
- Produces: user-facing OMEGA checkpoint build.

- [ ] **Step 1: Wire commercial checkpoint only after strict/deep/omega tests are green**
- [ ] **Step 2: Run final full Linux and Windows/MSVC CI on one exact SHA**
- [ ] **Step 3: Download and verify the Windows artifact digest and EXE SHA-256**
- [ ] **Step 4: Deliver the ZIP and request one OMEGA checkpoint**

### Task 6: Commercial OMEGA checkpoint analysis gate

**Files:**
- No proprietary payload committed.

**Interfaces:**
- Consumes: user-generated `jojo-max3-checkpoint-v2` report.
- Produces: ranked batch of strict evidence-backed implementation candidates.

- [ ] **Step 1: Verify checkpoint schema/profile/build SHA**
- [ ] **Step 2: Separate strict frontiers from speculative descendants**
- [ ] **Step 3: Group repeated frontiers/root-cause clusters**
- [ ] **Step 4: Rank batch fixes by strict evidence and descendant-unlock value**
- [ ] **Step 5: Require a new bounded/architectural approval only when a proposed production hardware implementation changes semantics beyond the already-approved OMEGA diagnostic architecture**

---

## Final Definition of Done

OMEGA is complete only when all four promotion gates pass and the user receives a Windows x64 artifact from the exact final SHA. A commercial OMEGA checkpoint may reveal no additional safe read frontier because a real side-effectful frontier can still be terminal; that outcome is valid if the report explains it precisely. OMEGA success means dramatically better discovery/provenance/batching capability, not pretending unsupported hardware is implemented.
