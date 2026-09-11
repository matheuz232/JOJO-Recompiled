# PS1 OMEGA Frame-First Phase C0 Evidence Refresh Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Produce a fresh strict commercial checkpoint from the exact consolidated Windows artifact and use it to select the first evidence-backed Phase C1 blocker without changing hardware semantics.

**Architecture:** C0 is an evidence-only gate. It pins the CI/artifact identity, executes the existing `EXECUTAR CHECKPOINT` path against the user's local legal JoJo `ps1_m1` installation, validates strict provenance and required derived diagnostics, rejects stale/already-supported frontiers, and hands one exact blocker to a separate C1 design. No production code changes are permitted in C0.

**Tech Stack:** GitHub Actions metadata, existing Win32 GUI checkpoint path, MAX3/OMEGA diagnostic report, text diagnostics under `%LOCALAPPDATA%\\JOJO Recompiled\\diagnostics`.

**Spec:** `docs/superpowers/specs/2026-09-11-ps1-omega-frame-first-phase-c0-design.md`

## Global Constraints

- Baseline SHA: `be2645471daff4f43d6cae91f15b0b755b04b419`.
- Baseline CI run: `#1688` / run ID `34591205792`.
- Baseline Windows artifact: `JOJO-Recompiled-Windows-x64`, artifact ID `10195773587`.
- C0 performs no GPU, DMA, IRQ, timer, CD-ROM, GTE, BIOS, SIO, or SPU implementation.
- Only strict commercial evidence may select the C1 implementation scope.
- Speculative descendants may be used only as secondary hints.
- Proprietary game/BIOS payloads must never be committed or copied into CI.
- If the fresh report lacks enough bounded derived diagnostics to classify the strict blocker, C1 begins with diagnostic instrumentation rather than guessed hardware semantics.

---

### Task 1: Pin the exact validated binary baseline

**Files:**
- Read only: GitHub Actions run `34591205792`
- Read only: artifact `10195773587`
- No repository file modifications

**Interfaces:**
- Consumes: consolidated SHA `be2645471daff4f43d6cae91f15b0b755b04b419`
- Produces: one verified Windows artifact identity for the user's local run

- [ ] **Step 1: Verify the consolidation branch ref**

Confirm `feature/ps1-omega-frame-first-consolidation` resolves to exactly:

```text
be2645471daff4f43d6cae91f15b0b755b04b419
```

Expected: exact match. If the branch moved, stop C0 and re-evaluate the baseline instead of silently using a different build.

- [ ] **Step 2: Verify CI run identity**

Confirm run `34591205792` is `completed/success`, branch `feature/ps1-omega-frame-first-consolidation`, and `head_sha` is the exact baseline SHA.

Expected: Linux and Windows x64 jobs both `success`.

- [ ] **Step 3: Verify artifact identity**

Confirm artifact:

```text
name: JOJO-Recompiled-Windows-x64
id: 10195773587
head_sha: be2645471daff4f43d6cae91f15b0b755b04b419
```

Record its current digest for provenance. Do not substitute an older artifact with the same name.

- [ ] **Step 4: Gate Task 1**

Task 1 passes only if branch SHA, run SHA, and artifact SHA all agree exactly.

---

### Task 2: Generate the fresh commercial checkpoint locally

**Files:**
- User-local input: legal prepared JoJo `ps1_m1` installation
- User-local output: `%LOCALAPPDATA%\\JOJO Recompiled\\diagnostics\\m3a-checkpoint.txt`
- No repository file modifications

**Interfaces:**
- Consumes: verified artifact from Task 1 and the user's local `ps1_m1` installation
- Produces: one fresh `m3a-checkpoint.txt`

- [ ] **Step 1: Run the exact artifact**

Launch the executable from artifact ID `10195773587`. Do not use another local build for authoritative C0 evidence.

- [ ] **Step 2: Select the prepared JoJo installation**

Use the existing GUI to select the already prepared/validated `ps1_m1` installation if it is not already selected.

Expected: `EXECUTAR CHECKPOINT` is enabled for the validated installation.

- [ ] **Step 3: Execute the checkpoint**

Click:

```text
EXECUTAR CHECKPOINT
```

Wait for the existing synchronous checkpoint operation to complete.

- [ ] **Step 4: Collect the bounded report**

Retrieve:

```text
%LOCALAPPDATA%\JOJO Recompiled\diagnostics\m3a-checkpoint.txt
```

Do not provide the game image, BIOS, raw sectors, unrestricted RAM dumps, textures, or audio.

- [ ] **Step 5: Supply the report for analysis**

Attach only `m3a-checkpoint.txt` to the ChatGPT conversation.

Task 2 cannot be completed by CI or by synthetic fixtures because it depends on the user's local commercial JoJo installation.

---

### Task 3: Validate that the report is authoritative strict evidence

**Files:**
- Read only: fresh `m3a-checkpoint.txt`
- Read only as needed: current consolidated source files for the implicated subsystem

**Interfaces:**
- Consumes: report from Task 2
- Produces: validated strict blocker record, or a diagnostic-gap decision

- [ ] **Step 1: Parse the report header and terminal state**

Extract at minimum:

```text
format
termination_reason / stop_reason
instructions_retired
last_pc
last_opcode
best_node
best_path_count
```

Expected: the report is syntactically complete enough to identify the root/best strict run.

- [ ] **Step 2: Verify strict authority**

Confirm the authoritative selected result has no speculative ancestor and does not depend on a diagnostic MMIO candidate as commercial success.

If the report exposes evidence class/provenance fields, require `strict`. If it does not expose them explicitly, validate them using the Phase B `best_*` strict-authoritative contract and the node/path structure.

- [ ] **Step 3: Extract first-frame counters**

Record:

```text
gpu_gp0_command_count
gpu_gp1_command_count
dma_transfer_count
vram_write_count
presented_frames
cdrom_command_count
interrupts_accepted
```

These counters are progress evidence only; none substitutes for `commercial_frame_presented`.

- [ ] **Step 4: Extract the exact terminal dependency/frontier**

For MMIO, capture:

```text
address
width
direction (read/write)
derived value if reported
PC/opcode
```

For BIOS/HLE, capture:

```text
table
selector
PC/opcode
```

For CPU/GTE/device-command boundaries, capture the corresponding exact opcode/command and state fields already present in the bounded report.

- [ ] **Step 5: Reject stale or already-supported boundaries**

Compare the captured blocker against the current consolidated source tree. A frontier that the current code already handles is not eligible for C1; investigate whether the report came from the wrong binary or whether the diagnostic classification itself is stale.

- [ ] **Step 6: Gate diagnostic sufficiency**

Task 3 passes only if one exact strict blocker can be named without guessing. If not, classify C1 as a bounded-diagnostics batch first; do not infer missing side effects.

---

### Task 4: Select and hand off exactly one Phase C1 target

**Files:**
- Create later, only after C0 evidence exists: `docs/superpowers/specs/2026-09-11-ps1-omega-frame-first-phase-c1-<blocker>-design.md`
- No C1 file is created during C0 until the refreshed report is analyzed

**Interfaces:**
- Consumes: validated strict blocker from Task 3
- Produces: one evidence-backed architectural scope for C1

- [ ] **Step 1: Classify the blocker subsystem**

Choose exactly one primary class:

```text
GPU/GP0
DMA
IRQ/timer
CD-ROM
GTE/COP2
BIOS/kernel
SIO/controller
SPU
CPU/memory
bounded diagnostics
```

- [ ] **Step 2: Define the minimal semantic unit**

State the smallest behavior that can cross the exact observed boundary correctly. Examples include one GP0 command family, one DMA2 transfer mode, one IRQ acknowledgement rule, one CD command/response/data transition, or one GTE command.

Do not bundle adjacent features that are not required by the strict frontier.

- [ ] **Step 3: Establish the C1 RED contract**

Before implementation, the C1 design must identify a synthetic test that reproduces the observed boundary and fails on the current consolidated code for the expected reason.

- [ ] **Step 4: Define the C1 exit gate**

C1 is successful only when both are true:

```text
1. Linux + Windows CI remain green.
2. A new commercial checkpoint on the resulting artifact reaches a strictly later frontier or a new strict first-frame landmark.
```

- [ ] **Step 5: Close C0**

Mark C0 complete only after the exact C1 target is identified from the refreshed strict report. No hardware change is part of C0 itself.

---

## C0 Completion Evidence

The execution record for C0 should contain exactly these provenance facts:

```text
baseline_sha=be2645471daff4f43d6cae91f15b0b755b04b419
baseline_ci_run=34591205792
baseline_artifact_id=10195773587
checkpoint_source=local legal ps1_m1 installation
checkpoint_provenance=strict
selected_c1_blocker=<exact subsystem + operation>
```

C0 is not complete until `<exact subsystem + operation>` is derived from the fresh report rather than historical checkpoints.