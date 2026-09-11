# PS1 MAX³ OMEGA Deep Consolidation — Master Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans. Each behavior change is RED→GREEN→review→CI.

**Goal:** Deliver deterministic `strict`/`deep`/`omega` MAX³ diagnostics that can discover, classify, prune, rank, serialize, validate, and expose many downstream PS1 frontiers from one commercial checkpoint without fabricating unknown hardware side effects.

**Spec:** `docs/superpowers/specs/2026-09-10-ps1-max3-omega-deep-consolidation-design.md`

## Invariants

- Production runtime remains strict.
- Speculative ancestry never becomes strict evidence downstream.
- No unknown MMIO write, CD command/state transition, GPU drawing, DMA transfer/start, IRQ source, timer, sector delivery, VRAM write, frame presentation, or guest callback is fabricated.
- Diagnostic reads retry the original load through the R3000A executor; no direct GPR/PC/pending-load patching.
- Search, candidates, budgets, pruning, ranking, and report ordering are deterministic and independent of host time/speed/free memory/randomness.
- Public MAX³ aggregate compatibility is append-only where required.
- Only synthetic fixtures and legally derived metadata enter the repo.
- Linux + Windows/MSVC exact-SHA promotion gates separate major plans; Windows/MSVC is the user-facing artifact authority.

## Required execution order

### Plan A — Explorer Core
`docs/superpowers/plans/2026-09-10-ps1-max3-omega-explorer-core.md`

Delivers profiles/public model, candidate engine, coverage/landmarks, deterministic search/dominance/cycles, adaptive budgets, deep/omega MMIO read branching, clustering/priority, search stats and determinism/limit tests.

**Gate A:** strict fixtures preserve behavior; chained reads work in deep/omega; writes/side effects stay terminal; cycles/budgets fail closed; Linux + MSVC green.

### Plan C — Runtime and Synthetic Hardening
`docs/superpowers/plans/2026-09-10-ps1-max3-omega-runtime-hardening.md`

Runs **after A and before B** because it defines `Ps1Max3FrontierContext` consumed by the checkpoint serializer. Delivers state-identity audit, load/override/IRQ hardening, optional LWL/LWR gate, deterministic synthetic/property corpus, bounded CPU/BIOS/MMIO/CD/GPU/IRQ context/tails, and permanent regressions.

**Gate C:** no partial retire/stale override; context/tails legal and bounded; prior frontiers fixed; Linux + MSVC green.

### Plan B — Checkpoint v2 and Provenance
`docs/superpowers/plans/2026-09-10-ps1-max3-omega-checkpoint-v2.md`

Runs after C. Delivers v2 identity/config fingerprint, structural graph/clusters/nodes, bounded per-frontier context serialization, best strict/overall paths, summary/health/implementation queue, parser/validator, deterministic self-validating atomic save.

**Gate B:** round-trip validator passes, identical input yields byte-identical output, v1 semantics remain, no proprietary payload, Linux + MSVC green.

### Plan D — Commercial Activation and CI
`docs/superpowers/plans/2026-09-10-ps1-max3-omega-commercial-ci.md`

Runs last. Delivers build SHA identity, runtime profile API, Windows `EXECUTAR CHECKPOINT` -> OMEGA, all CTest registrations, ASan/UBSan lane, artifact contract, final exact-SHA Linux/MSVC/sanitizer validation and Windows artifact.

**Gate D:** all required jobs success on one SHA; checkpoint identifies build/profile/schema; ZIP contains only `JOJO-Recompiled.exe`; hashes verified.

---

## Master Tasks

### Task 1 — Baseline before Plan A

- [ ] Confirm branch/spec/plans present and clean execution workspace.
- [ ] Record `git rev-parse HEAD` as implementation baseline.
- [ ] Fresh Release configure/build/full CTest before changing production.
- [ ] Confirm latest baseline GitHub Actions Linux + Windows success.

### Task 2 — Execute Plan A

- [ ] Every A task RED→GREEN.
- [ ] Review scope and public compatibility after each task.
- [ ] Stop on unexpected failure and use systematic-debugging.
- [ ] Require Gate A exact-SHA CI before C.

### Task 3 — Execute Plan C

- [ ] Every C task RED→GREEN.
- [ ] No test-only mutable CPU escape hatches or proprietary fixtures.
- [ ] Require Gate C exact-SHA CI before B.

### Task 4 — Execute Plan B

- [ ] Every B task RED→GREEN.
- [ ] Round-trip + deterministic serialization + malformed-input rejection before integration.
- [ ] Require Gate B exact-SHA CI before D.

### Task 5 — Execute Plan D

- [ ] Build identity first, then runtime profile, Win32 OMEGA wiring, test registration, sanitizer lane, artifact contract.
- [ ] Fresh final verification on one SHA.
- [ ] Download/verify Windows artifact before user delivery.

### Task 6 — First commercial OMEGA checkpoint analysis

- [ ] Validate schema/profile/build SHA/fingerprint.
- [ ] Separate strict frontiers from speculative descendants.
- [ ] Group frontier clusters and frequency/callsites/descendant unlock value.
- [ ] Rank strict/mixed evidence-backed batch fixes.
- [ ] Speculative-only results remain investigation leads, never automatic production patches.
- [ ] Any newly exposed hardware semantic change outside approved OMEGA diagnostic architecture gets its own bounded/architectural gate before production implementation.

## Definition of Done

OMEGA is complete when A→C→B→D all pass their promotion gates and the user receives a Windows x64 artifact from the exact final SHA. A commercial run is successful even if a true side-effectful frontier prevents further speculation, provided checkpoint v2 explains that blocker precisely. The success metric is useful distinct evidence per checkpoint, not raw branch count.
