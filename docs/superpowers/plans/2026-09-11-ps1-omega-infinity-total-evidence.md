# PS1 OMEGA Infinity + Total Evidence Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Build a resumable, crash-safe OMEGA Infinity diagnostic session that runs 3,000,000,000-retired-instruction epochs, preserves strict/speculative provenance and broad runtime evidence, and produces a portable local evidence bundle without changing PlayStation hardware semantics.

**Architecture:** Keep bounded MAX3 APIs intact and add a separate Infinity orchestration layer. Split responsibilities into option/control types, deterministic snapshots, append-only evidence chunks, persistent session/scheduler state, runtime orchestration, and Win32 UI control. Search execution remains single-threaded and strict-first; the Win32 process runs the long session on one owned worker thread so the UI remains responsive.

**Tech Stack:** C++20, existing JOJO core/runtime/MAX3 code, `std::filesystem`, deterministic binary/JSON-like text metadata, existing FNV/SHA utilities where appropriate, CMake/CTest, Win32 UI/thread messaging.

**Spec:** `docs/superpowers/specs/2026-09-11-ps1-omega-infinity-total-evidence-design.md`

## Global Constraints

- Baseline SHA is `b3f716b68cab835b8dc65230278078fef7b39b89`.
- C2/HINTMSK remains paused; no new PS1 hardware semantics in this project.
- Strict evidence always outranks speculative evidence.
- `commercial_frame_presented` is authoritative only on a strict path.
- Infinity default epoch retired ceiling is exactly `3000000000ull`.
- Long-lived evidence spills to disk; RAM usage stays bounded.
- Resume must reject incompatible executable/schema/state identity instead of continuing silently.
- Existing `bootstrap_runtime_checkpoint*`, `bootstrap_runtime_max3_local_evidence_to_file`, `bootstrap_runtime_local_evidence_to_file`, and `explore_ps1_max3` remain compatible.
- Search ordering remains single-threaded and deterministic.
- Default durable session disk ceiling is 16 GiB.
- Linux and Windows/MSVC CI must be green at the final exact SHA.

---

### Task 1: Infinity option/control types and 3B epoch contract

**Files:**
- Create: `src/core/ps1_omega_infinity.h`
- Create: `src/core/ps1_omega_infinity.cpp`
- Create: `tests/test_ps1_omega_infinity.cpp`
- Modify: `CMakeLists.txt`

**Interfaces:**
- Produces: `Ps1OmegaInfinityOptions`, `Ps1OmegaInfinityStopReason`, `Ps1OmegaInfinitySummary`, `Ps1OmegaInfinityControl`, `ps1_omega_infinity_options()`.
- `Ps1OmegaInfinityControl` owns an atomic stop-request flag through an implementation that is safe to query from the exploration worker.

- [ ] **Step 1: Write the RED option/control test**

Test exact defaults:

```cpp
const auto options = jojo::ps1_omega_infinity_options();
CHECK(options.epoch_retired_limit == 3000000000ull);
CHECK(options.chunk_target_bytes == 8ull * 1024ull * 1024ull);
CHECK(options.max_session_disk_bytes == 16ull * 1024ull * 1024ull * 1024ull);
CHECK(options.hot_trace_capacity == 262144u);
CHECK(options.stop_on_strict_commercial_frame);

jojo::Ps1OmegaInfinityControl control;
CHECK(!control.stop_requested());
control.request_stop();
CHECK(control.stop_requested());
```

Also assert ordinary `ps1_max3_local_evidence_options().max_total_retired` remains `1000000000ull` and `ps1_max3_options(Ps1Max3Profile::omega).max_total_retired` remains `3000000000ull`.

- [ ] **Step 2: Run the new target and verify RED**

Run the new CTest target. Expected failure: Infinity symbols do not exist.

- [ ] **Step 3: Implement only option/control data types**

Do not start a session yet. `ps1_omega_infinity_options()` returns the exact defaults from the spec.

- [ ] **Step 4: Run tests GREEN**

Run the focused target, then the existing MAX3 profile/budget tests.

- [ ] **Step 5: Commit**

Commit message: `feat: define PS1 OMEGA Infinity session options`.

---

### Task 2: Versioned CPU/system snapshot schema

**Files:**
- Create: `src/core/ps1_omega_snapshot.h`
- Create: `src/core/ps1_omega_snapshot.cpp`
- Create: `tests/test_ps1_omega_snapshot.cpp`
- Modify: `src/core/ps1_memory_bus.h`
- Modify: `src/core/ps1_gpu_state.h`
- Modify: `src/core/ps1_cdrom_state.h`
- Modify: `CMakeLists.txt`

**Interfaces:**
- Consumes: `R3000aState`, `Ps1BootRuntime`, `Ps1MemoryBus` public diagnostic accessors.
- Produces: `Ps1OmegaCpuSnapshot`, `Ps1OmegaSystemSnapshot`, `capture_ps1_omega_snapshot(const Ps1BootRuntime&)`, deterministic encode/decode helpers.

- [ ] **Step 1: Write RED CPU snapshot round-trip tests**

Populate every `R3000aState` field: all GPRs, HI/LO, PC/next-PC, delayed load, delay slot, all COP0 fields, all 32 current COP2/GTE control slots, external interrupt pending. Capture, encode, decode, and assert every field matches exactly.

- [ ] **Step 2: Write RED system snapshot visibility tests**

Use existing modeled states to assert the snapshot exposes at least I_STAT/I_MASK, CD-ROM index/status/enable/IRQ/command count, DMA2 registers + DPCR/DICR visible state, Timer1 state, GPU diagnostic state and HLE state. If a private state has no accessor, add a diagnostic read-only accessor rather than duplicating semantics.

- [ ] **Step 3: Run RED**

Expected failure: snapshot types/functions/accessors are absent.

- [ ] **Step 4: Implement deterministic snapshot structs and accessors**

No mutating restore is required in this task. The serialized representation starts with `schema_version = 1` and uses explicit fixed-width fields; never serialize raw object memory or C++ ABI layout.

- [ ] **Step 5: GREEN + determinism**

Encode the same state twice and assert identical bytes/hash.

- [ ] **Step 6: Commit**

Commit message: `feat: capture deterministic OMEGA runtime snapshots`.

---

### Task 3: Crash-safe evidence chunk writer and manifest

**Files:**
- Create: `src/core/ps1_omega_evidence.h`
- Create: `src/core/ps1_omega_evidence.cpp`
- Create: `src/core/ps1_omega_session_io.h`
- Create: `src/core/ps1_omega_session_io.cpp`
- Create: `tests/test_ps1_omega_session_io.cpp`
- Modify: `CMakeLists.txt`

**Interfaces:**
- Produces: `Ps1OmegaEvidenceCategory`, `Ps1OmegaChunkHeader`, `Ps1OmegaSessionManifest`, `Ps1OmegaEvidenceRecorder`.
- Chunk commit API writes `<name>.tmp`, flushes/closes, computes/records checksum, atomically renames to final name, then updates the manifest atomically.

- [ ] **Step 1: RED atomic chunk test**

Create a temporary session root. Append one MMIO event chunk. Assert final chunk exists, `.tmp` does not, manifest contains sequence/category/epoch/payload length/checksum, and checksum verification succeeds.

- [ ] **Step 2: RED orphan recovery test**

Place an unreferenced `.tmp` file beside a valid manifest. Load the session and assert the temporary file is ignored and is never treated as committed evidence.

- [ ] **Step 3: RED disk-ceiling test**

Use an intentionally tiny `max_session_disk_bytes`; an append that would exceed it must return `disk_budget_exhausted` without partially committing a chunk.

- [ ] **Step 4: Implement manifest/chunk writer**

Use only deterministic core facilities and existing hash/checksum code where adequate. No external DB dependency. Chunk headers include schema version, category, sequence, epoch, payload length and checksum.

- [ ] **Step 5: GREEN + cross-platform path tests**

Run on Linux CI and later Windows CI; no hard-coded separators.

- [ ] **Step 6: Commit**

Commit message: `feat: persist crash-safe OMEGA evidence chunks`.

---

### Task 4: Coverage, frontier and frame-first evidence model

**Files:**
- Create: `src/core/ps1_omega_coverage.h`
- Create: `src/core/ps1_omega_coverage.cpp`
- Create: `tests/test_ps1_omega_coverage.cpp`
- Modify: `src/core/ps1_omega_evidence.h`
- Modify: `CMakeLists.txt`

**Interfaces:**
- Produces: cumulative `Ps1OmegaCoverage`, strict/speculative `Ps1OmegaFrameFirstEvidence`, frontier/event aggregation helpers.

- [ ] **Step 1: RED merge tests**

Create two epoch coverage objects with overlapping PCs/edges/MMIO signatures and unique entries. Merge and assert union counts, deterministic ordering/encoding and no double-counting.

- [ ] **Step 2: RED strict/speculative landmark separation**

Record speculative `presented_frame` first, then strict GP0 and strict VRAM landmarks. Assert speculative frame never populates strict commercial-frame fields.

- [ ] **Step 3: RED repetitive loop summarization**

Feed repeated identical PCs/edges/state hashes and assert the aggregate retains counts/hotness/period evidence without one durable record per repetition.

- [ ] **Step 4: Implement compact coverage/landmark structures**

Keep raw frontier-local traces elsewhere; this task only provides cumulative compact data.

- [ ] **Step 5: GREEN and commit**

Commit message: `feat: aggregate OMEGA coverage and frame-first evidence`.

---

### Task 5: Persistent scheduler/replay descriptors

**Files:**
- Create: `src/core/ps1_omega_scheduler.h`
- Create: `src/core/ps1_omega_scheduler.cpp`
- Create: `tests/test_ps1_omega_scheduler.cpp`
- Modify: `src/core/ps1_max3_explorer.h`
- Modify: `src/core/ps1_max3_explorer.cpp`
- Modify: `CMakeLists.txt`

**Interfaces:**
- Produces: `Ps1OmegaReplayDescriptor`, `Ps1OmegaPendingWork`, `Ps1OmegaFrontierScheduler`.
- Uses existing `Ps1Max3Decision`, evidence class, assumption chain and strict-first search score semantics.

- [ ] **Step 1: RED descriptor round-trip**

Create pending work containing strict/speculative evidence, insertion sequence, decisions, assumption chain, metrics, cumulative retired count and expected state hash. Encode/decode and assert exact equality.

- [ ] **Step 2: RED ranking compatibility**

Provide equivalent strict and speculative pending work where speculative has larger progress counters. Assert scheduler chooses strict first, matching `ps1_max3_search_outranks` policy.

- [ ] **Step 3: RED replay validation contract**

A descriptor with an expected state hash different from replayed state must return `invalid_resume_state` and never enter the runnable queue.

- [ ] **Step 4: Extract only the minimal MAX3 internals needed for replay descriptors**

Do not fork a second ranking policy. Reuse `Ps1Max3SearchScore`/strict-first comparator directly.

- [ ] **Step 5: GREEN + existing MAX3 regression suite**

All Phase B ranking/dominance/determinism tests remain green.

- [ ] **Step 6: Commit**

Commit message: `feat: persist OMEGA frontier scheduler state`.

---

### Task 6: Multi-epoch Infinity session orchestration

**Files:**
- Modify: `src/core/ps1_omega_infinity.h`
- Modify: `src/core/ps1_omega_infinity.cpp`
- Modify: `src/core/runtime.h`
- Modify: `src/core/runtime.cpp`
- Create: `tests/test_ps1_omega_infinity_session.cpp`
- Modify: `CMakeLists.txt`

**Interfaces:**
- Produces: `run_ps1_omega_infinity(install_root, session_root, options, control)` and resumable session load/run behavior.
- Consumes Tasks 2-5.

- [ ] **Step 1: RED artificial two-epoch test**

Use a tiny synthetic executable/options with a deliberately tiny per-epoch retired limit. Verify epoch 1 commits session state, epoch 2 resumes, cumulative retired count increases monotonically, and committed events are not duplicated.

- [ ] **Step 2: RED graceful-stop/resume test**

Request stop at a deterministic boundary. Assert stop reason `user_requested`, session remains resumable, and a second invocation resumes from compatible state.

- [ ] **Step 3: RED identity mismatch test**

Change executable identity/schema option and assert resume rejects it with `invalid_resume_state` or an explicit compatibility error.

- [ ] **Step 4: Implement Infinity orchestration**

Use `ps1_max3_options(Ps1Max3Profile::omega)` as the epoch basis, force `max_total_retired` to `options.epoch_retired_limit`, persist evidence at epoch/frontier boundaries, compact/spill queue state, and start another epoch only when resumable work remains.

- [ ] **Step 5: Preserve legacy APIs**

Run tests proving `bootstrap_runtime_local_evidence_to_file()` still uses the existing deep/1B path and writes the legacy report.

- [ ] **Step 6: GREEN and commit**

Commit message: `feat: run resumable multi-epoch OMEGA Infinity sessions`.

---

### Task 7: Total evidence summary and portable session bundle

**Files:**
- Create: `src/core/ps1_omega_summary.h`
- Create: `src/core/ps1_omega_summary.cpp`
- Create: `tests/test_ps1_omega_summary.cpp`
- Modify: `src/core/ps1_omega_session_io.h`
- Modify: `src/core/ps1_omega_session_io.cpp`
- Modify: `CMakeLists.txt`

**Interfaces:**
- Produces deterministic `summary.txt`, `manifest.json` and the logical `OMEGA-Infinity-Session` directory suitable for upload after ZIP packaging.

- [ ] **Step 1: RED summary determinism test**

Given a fixed manifest/coverage/frontier set, render twice and assert byte-identical summary excluding no fields. Wall-clock values must come from manifest input, not direct calls inside renderer.

- [ ] **Step 2: RED required-field test**

Assert summary contains build/schema identity, epoch/retired totals, strict/speculative frontier counts, strict best report, frame-first landmarks, unresolved strict blockers, speculative future blockers, coverage totals, disk usage and stop reason.

- [ ] **Step 3: Implement summary and bundle finalization**

Finalization flushes all committed chunks before writing summary/manifest. The session directory is the authoritative bundle content. Add a deterministic store-only ZIP packager if it can be implemented without external dependencies; otherwise expose a single explicit packaging step in the Win32 layer using a tested project-owned packager before commercial handoff. Do not silently omit bundle packaging from the final gate.

- [ ] **Step 4: GREEN and commit**

Commit message: `feat: finalize portable OMEGA Infinity evidence bundle`.

---

### Task 8: Win32 OMEGA Infinity controls without UI blocking

**Files:**
- Modify: `src/app_win32/main.cpp`
- Create: `tests/test_ps1_omega_ui_state.cpp` only if UI state is extracted into a portable helper; otherwise validate core worker/control state through Task 6 and Windows CI.

**Interfaces:**
- Adds `EXECUTAR OMEGA INFINITY`, graceful stop/resume behavior, status polling/messages and final bundle path.

- [ ] **Step 1: Extract/define UI session state transitions**

States: idle, running, stop_requested, completed, failed, resumable. Invalid transitions must be rejected deterministically.

- [ ] **Step 2: Add controls and worker ownership**

Add a second button separate from `EXECUTAR CHECKPOINT`. Infinity starts one owned `std::thread`; do not use `detach()`. Keep `Ps1OmegaInfinityControl` alive until the worker exits.

- [ ] **Step 3: Add progress messages**

Post lightweight Win32 messages containing epoch, cumulative retired, strict/speculative frontier counts, disk bytes and latest strict frontier. UI rendering never touches explorer state directly.

- [ ] **Step 4: Add graceful stop and shutdown**

Clicking the running Infinity button requests stop. Window shutdown requests stop and joins the worker before destroying process-owned session state.

- [ ] **Step 5: Add resume detection**

If a compatible paused session exists under `%LOCALAPPDATA%/JOJO Recompiled/diagnostics/omega-infinity/`, label/action offers resume instead of overwriting it.

- [ ] **Step 6: Windows build validation**

MSVC must compile with no use-after-free/thread-lifetime warnings introduced by this feature.

- [ ] **Step 7: Commit**

Commit message: `feat: add OMEGA Infinity Windows diagnostic workflow`.

---

### Task 9: Regression, scope audit, CI and commercial artifact

**Files:**
- Modify only tests/docs needed for final verification.

**Interfaces:**
- Final head must be commercial-run ready but must not include C2/HINTMSK semantics.

- [ ] **Step 1: Run full Linux test suite**

Expected: all tests green, including legacy MAX3/checkpoint contracts.

- [ ] **Step 2: Run strict-first regression audit**

Explicitly run Phase B search-policy/frontier-priority/local-evidence tests.

- [ ] **Step 3: Scope diff against baseline**

Compare final SHA to `b3f716b68cab835b8dc65230278078fef7b39b89`. Reject unexpected hardware semantic changes.

- [ ] **Step 4: Authoritative GitHub CI**

Require Linux and Windows/MSVC jobs green on the exact final SHA, including production-readiness and PS1 architecture gates.

- [ ] **Step 5: Download exact Windows artifact**

Record artifact ID, SHA/digest, exact head SHA and CI run number.

- [ ] **Step 6: Commercial OMEGA Infinity gate**

User runs `EXECUTAR OMEGA INFINITY` against the owned JoJo installation. Required result is a parseable portable Infinity bundle. Analyze that bundle before resuming C2/C3 hardware unlocks.

- [ ] **Step 7: Do not claim playability**

Only a strict `commercial_frame_presented` may support a first-real-frame claim.

- [ ] **Step 8: Commit any verification-only docs if needed**

Commit message: `test: gate OMEGA Infinity evidence workflow`.
