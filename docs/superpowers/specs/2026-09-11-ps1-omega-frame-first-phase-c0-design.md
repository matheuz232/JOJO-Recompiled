# PS1 OMEGA Frame-First — Phase C0 Evidence Refresh Design

Date: 2026-09-11
Base branch: `feature/ps1-omega-frame-first-consolidation`
Base SHA: `be2645471daff4f43d6cae91f15b0b755b04b419`
Validated CI run: `#1688` (`34591205792`)
Windows artifact: `JOJO-Recompiled-Windows-x64` (`10195773587`)

## 1. Purpose

Phase C0 refreshes strict commercial evidence before any new hardware implementation begins.

The umbrella Frame-First design defines Phase C as evidence-dependent: the exact GP0, DMA, IRQ, CD-ROM, GTE, BIOS, SIO, or SPU behavior to implement must be chosen from the highest-ranked strict blocker observed on the current consolidated build. The most recent stored checkpoints predate fixes already present in the consolidated branch, so they are not authoritative for selecting the next implementation batch.

C0 therefore exists to establish a fresh, reproducible strict checkpoint from the exact consolidated SHA that completed Phase B.

## 2. Baseline

The only accepted binary baseline for C0 is the Windows x64 artifact produced by CI run #1688 from SHA `be2645471daff4f43d6cae91f15b0b755b04b419`.

The artifact was produced after Linux and Windows/MSVC validation of the consolidated Phase A + Phase B line. C0 must not use an older executable, an executable rebuilt from another SHA, or an earlier checkpoint artifact as the authoritative source for C1 scope selection.

## 3. Why a refresh is required

The newest stored commercial checkpoints are stale relative to the consolidated code.

Observed historical blockers include:

- `read8 0x1F801800` after 816,457 retired instructions, one GP0 command, three GP1 commands, one CD-ROM command, one accepted interrupt, zero DMA transfers, zero VRAM writes, and zero presented frames;
- `read8 0x1F801801` after 816,500 retired instructions with the same first-frame counters.

Both blockers were subsequently addressed in the mature OMEGA history by CD-ROM changes including HSTS/status exposure and response FIFO handling across banks. The consolidated branch already contains those semantics.

Therefore neither historical blocker is eligible to define C1.

## 4. C0 execution contract

C0 performs no hardware implementation.

The user runs `EXECUTAR CHECKPOINT` using the CI #1688 Windows artifact against the locally prepared legal JoJo `ps1_m1` installation.

Expected report path:

`%LOCALAPPDATA%\JOJO Recompiled\diagnostics\m3a-checkpoint.txt`

The resulting report becomes the sole commercial evidence input for C1 design.

No disc image, BIOS ROM, raw sector dump, unrestricted RAM dump, texture dump, audio dump, or other proprietary payload is committed to the repository. Only bounded derived diagnostics may be used for analysis.

## 5. Evidence requirements

The refreshed checkpoint must preserve enough derived information to identify the strict blocker without guessing:

- stop reason;
- retired instruction count;
- last PC and opcode;
- strict/speculative provenance;
- dependency/frontier classification;
- MMIO address, width, direction, and derived value where applicable;
- BIOS table/selector where applicable;
- GP0 and GP1 command counts;
- DMA transfer count;
- VRAM write count;
- presented-frame count;
- CD-ROM command progress;
- accepted interrupt count;
- relevant deterministic state/frontier hashes already present in the report.

If the refreshed report is missing a field needed to classify the next blocker, C1 must first improve bounded diagnostics rather than infer behavior from incomplete evidence.

## 6. Authority rules

Only strict evidence selects C1 implementation scope.

Speculative MAX3 descendants may be inspected as secondary hints, but they cannot outrank or replace the first strict blocker and cannot justify fabricated side effects.

`commercial_frame_presented` remains authoritative only when reached on a strict path, consistent with Phase B.

## 7. C1 selection algorithm

After the refreshed checkpoint is supplied:

1. identify the first strict terminal blocker;
2. classify it by subsystem and exact operation;
3. compare it with already implemented semantics on the consolidated branch;
4. reject stale or already-supported frontiers;
5. choose the smallest semantically correct behavior that crosses that exact blocker;
6. define RED tests reproducing the observed boundary synthetically;
7. implement only that behavior;
8. rerun Linux + Windows CI;
9. produce a new Windows artifact;
10. rerun the commercial checkpoint and verify a strictly later frontier or first-frame landmark.

If the refreshed run already reaches a first-frame landmark beyond the historical GP0/GP1/CD state, that later strict frontier becomes the C1 target immediately.

## 8. C1 subsystem priority

The default Frame-First priority remains GPU/GP0/DMA first, but strict commercial evidence overrides the default ordering.

Examples:

- a new unsupported GP0 command becomes the C1 target;
- DMA2 activation becomes the target if it is the first strict blocker;
- IRQ/timer behavior becomes the target if it blocks before DMA/VRAM progress;
- CD-ROM remains the target if a new command, response, IRQ, or data-flow boundary is first;
- GTE, BIOS/kernel, SIO, or SPU become targets only if they are the actual next pre-frame blocker.

C1 must not implement unrelated subsystem breadth merely because it is likely to be needed later.

## 9. Exit criteria

C0 is complete when all of the following are true:

- the checkpoint was produced using the exact CI #1688 artifact from SHA `be2645471daff4f43d6cae91f15b0b755b04b419`;
- the report comes from the user's locally prepared JoJo installation;
- the report is supplied for analysis;
- the first strict blocker still present on the consolidated build is identified unambiguously;
- no new hardware semantics were implemented during C0;
- a separate C1 design can be written from that evidence without guessing.

C0 does not claim first-frame progress by itself. Its output is authoritative scope selection for C1.

## 10. Non-goals

C0 does not:

- add GPU, DMA, IRQ, timer, CD-ROM, GTE, BIOS, SIO, or SPU behavior;
- increase OMEGA budgets;
- change strict/speculative ranking;
- change frame gating;
- infer the next blocker from historical checkpoints alone;
- claim commercial rendering success.

## 11. Handoff to C1

Once the fresh checkpoint is available, Phase C1 becomes a new architectural subproject with its own evidence-backed design, spec, implementation plan, RED-GREEN cycle, and Linux/Windows validation.

The C1 design must cite the refreshed checkpoint fields that justify the exact behavior selected for implementation.