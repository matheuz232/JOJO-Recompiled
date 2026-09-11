# PS1 OMEGA Infinity + Total Evidence Design

## Status

Approved 2026-09-11.

## Baseline

- Repository: `matheuz232/JOJO-Recompiled`
- Baseline SHA: `b3f716b68cab835b8dc65230278078fef7b39b89`
- Baseline branch: `feature/ps1-omega-frame-first-phase-c1-cdrom-hclrctl`
- Baseline CI: run #1692, Linux + Windows/MSVC green
- Last strict commercial checkpoint: 816565 retired instructions
- Current strict frontier: byte write `0x07` to physical `0x1F801802` at PC `0x8004B4A4`
- C2/HINTMSK is intentionally paused until OMEGA Infinity produces a broader evidence bundle.

## Goal

Replace the one-frontier-at-a-time commercial checkpoint loop with a resumable, continuously exploring OMEGA session that:

1. runs the existing strict-first MAX3/OMEGA search in repeated epochs;
2. uses 3,000,000,000 retired instructions as the per-epoch ceiling, not the lifetime session ceiling;
3. persists enough structured evidence to analyze many future blockers from one user run;
4. preserves strict/speculative provenance at every level;
5. remains crash-safe and resumable;
6. produces one portable session bundle for later analysis;
7. never converts speculative progress into a strict commercial boot/frame claim.

## Non-goals

This project does not implement new PlayStation hardware semantics merely to keep exploration moving. In particular this design does not itself add HINTMSK, new CD-ROM commands, DMA modes, GPU commands, GTE instructions, BIOS services, timers, SIO, SPU or other hardware behavior.

It also does not distribute or embed proprietary game data, BIOS ROMs or game assets.

OMEGA Infinity is a diagnostic/search infrastructure project. Hardware unlocks remain separate evidence-driven phases.

## Core invariants

### Strict remains authoritative

The Phase B strict-first rules remain unchanged:

- strict evidence outranks speculative evidence before numeric progress;
- `best_node`, `best_path` and `best_report` remain strict-authoritative;
- speculative nodes may expose future blockers but may not become commercial success evidence;
- `commercial_frame_presented` is authoritative only when reached by strict execution.

### No silent success

Unknown instructions, MMIO writes, device commands, GPU commands and BIOS services must never become successful production behavior merely because Infinity wants to continue.

Diagnostic continuations are explicit assumptions and always carry speculative provenance.

### Epochs, not one unbounded allocation

The existing OMEGA profile ceiling of 3,000,000,000 retired instructions becomes the maximum retired-work budget for one epoch.

An epoch ending because a search budget is reached does not necessarily end the session. The session persists its state, compacts completed work and may begin the next epoch from resumable pending work.

The session lifetime retired count is a saturating 64-bit aggregate. No `UINT64_MAX` single-run allocation or trace buffer is required.

### Bounded memory, durable disk

Long-running evidence must spill to disk in bounded chunks. Ring buffers are used for hot recent context; durable journals preserve compact event records and frontier snapshots.

No design requires retaining billions of instruction records in RAM.

## Architecture

### 1. `Ps1OmegaInfinitySession`

A new top-level session object owns identity, epoch number, cumulative counters, session status, output directory and resumable scheduler state.

The existing `explore_ps1_max3()` remains available for ordinary bounded reports. Infinity orchestrates bounded OMEGA epochs around it or around an extracted resumable explorer core; it does not change the semantics of ordinary MAX3 callers.

Proposed public data model:

```cpp
struct Ps1OmegaInfinityOptions {
    std::uint64_t epoch_retired_limit{3000000000ull};
    std::uint64_t chunk_target_bytes{8ull * 1024ull * 1024ull};
    std::uint64_t max_session_disk_bytes{16ull * 1024ull * 1024ull * 1024ull};
    std::size_t hot_trace_capacity{262144u};
    bool stop_on_strict_commercial_frame{true};
};

enum class Ps1OmegaInfinityStopReason : std::uint8_t {
    none,
    user_requested,
    strict_commercial_frame,
    fatal_no_safe_continuation,
    disk_budget_exhausted,
    invalid_resume_state,
};

struct Ps1OmegaInfinitySummary {
    std::uint64_t epoch_count{};
    std::uint64_t total_retired{};
    std::uint64_t strict_frontier_count{};
    std::uint64_t speculative_frontier_count{};
    std::uint64_t unique_state_count{};
    std::uint64_t presented_frames{};
    Ps1OmegaInfinityStopReason stop_reason{};
};
```

### 2. Persistent frontier scheduler

`Ps1OmegaFrontierScheduler` persists the work queue required to resume exploration. Each queued item must preserve:

- evidence class;
- insertion sequence;
- parent frontier;
- assumption chain;
- path metrics;
- cumulative retired count;
- state hash;
- enough replay information to reconstruct the runtime deterministically.

Raw in-memory runtime objects are not serialized as ABI-dependent C++ object dumps.

Resume is replay-based: reconstruct from the validated installed executable plus deterministic decision chain, then verify the recorded state hash before accepting the item.

A mismatch produces `invalid_resume_state`; it must not silently continue.

### 3. Total evidence recorder

`Ps1OmegaEvidenceRecorder` receives normalized events and writes append-only chunks.

The recorder is schema-versioned and category-based. Required event categories:

- instruction/control-flow landmarks;
- MMIO;
- BIOS/HLE;
- IRQ;
- CD-ROM;
- DMA;
- GPU GP0/GP1;
- timer;
- CPU exception/boundary;
- MAX3 decisions;
- frontier creation;
- pruning/deduplication/dominance;
- frame-first landmarks;
- stagnation/loop observations;
- fatal errors.

The recorder may summarize repetitive instruction execution instead of storing every retired instruction forever. It must preserve full local trace context around every frontier and every frame-first landmark.

### 4. State snapshots

Every strict frontier and selected speculative frontier records a versioned snapshot descriptor containing all state currently modeled by the runtime.

Required CPU snapshot fields:

- 32 GPRs;
- HI and LO;
- PC and next PC;
- pending delayed load;
- delay-slot state;
- COP0 target address, BadVAddr, Status, Cause and EPC;
- COP2/GTE state currently present in `R3000aState`;
- external interrupt pending state.

Required system/device snapshot fields include every modeled field exposed by the active runtime at implementation time. At minimum:

- I_STAT and I_MASK;
- CD-ROM index, drive status, interrupt enable/status, IRQ line, command count, response/result state and unsupported command state;
- DMA2 MADR/BCR/CHCR, DPCR and DICR/visible DMA interrupt state;
- Timer1 counter/mode and any modeled timer state;
- GPU state required to reconstruct GPUSTAT/display/draw status and command counters;
- BIOS HLE heap/hook/pad/root-counter/ISO9660 state;
- diagnostic MMIO read override state;
- current frame-first counters.

If a modeled field cannot be safely serialized in the first implementation, the manifest must declare it as `not_serialized` rather than pretending the snapshot is complete.

### 5. Memory evidence

Infinity must not copy all RAM after every instruction.

Use page-granular hashing and dirty tracking:

- main RAM pages receive stable hashes;
- scratchpad receives stable hashes;
- changed pages may be stored as compressed deltas at frontier snapshots;
- snapshot descriptors reference parent snapshots where possible;
- proprietary bytes are kept only in the user's local bundle and are never committed to the repository.

VRAM follows the same principle: hashes and dirty regions are always recorded; raw VRAM deltas are optional/local and must be bounded.

### 6. Coverage evidence

Persist compact coverage structures for:

- unique PCs;
- unique opcodes/instruction classes;
- observed branch/jump edges;
- BIOS selectors;
- MMIO addresses/width/direction combinations;
- CD-ROM commands;
- GP0/GP1 command classes;
- DMA channels/modes reached;
- frontier callsites;
- subsystem coverage.

Coverage is cumulative across epochs.

### 7. Frame-first evidence

Persist first-seen and best-seen records for:

1. first strict GP0 command;
2. first strict DMA-to-GPU transfer;
3. first strict VRAM mutation;
4. first strict valid display configuration;
5. first strict presentable framebuffer state;
6. first strict `commercial_frame_presented`.

Speculative equivalents may be recorded separately but never replace strict records.

### 8. Stagnation and loop evidence

The session must diagnose long loops without writing every repetition:

- hot PC histogram;
- recent edge histogram;
- repeated state-hash detection;
- loop period estimate where deterministic;
- instructions since last new coverage/frontier/progress landmark;
- reason for any scheduler compaction or branch termination.

### 9. Crash-safe persistence

Each durable chunk is written to a temporary path, flushed, checksummed, then atomically renamed.

`manifest.json` records only committed chunks. A crash may leave an orphan `.tmp`; resume ignores uncommitted temporary files.

Every chunk has:

- schema version;
- category;
- monotonically increasing sequence;
- epoch number;
- payload length;
- checksum.

### 10. Bundle layout

A completed or paused session is exportable as one directory/ZIP with this logical layout:

```text
OMEGA-Infinity-Session/
  manifest.json
  summary.txt
  resume.json
  scheduler/
    queue.jsonl
    completed.jsonl
  coverage/
    pc.bin
    edges.bin
    devices.bin
  events/
    cpu-*.bin
    mmio-*.bin
    bios-*.bin
    irq-*.bin
    cdrom-*.bin
    dma-*.bin
    gpu-*.bin
    timer-*.bin
    search-*.bin
  snapshots/
    snapshot-*.bin
  frame-first/
    landmarks.json
  errors/
    fatal-*.json
```

The first implementation may use deterministic binary records plus JSON metadata; it does not require adding an external database dependency.

### 11. Human-readable summary

`summary.txt` must stay small and include:

- build/version identity;
- session schema version;
- installation/executable identity without proprietary payload;
- start/resume/end timestamps;
- epoch count;
- total retired count;
- strict/speculative frontier counts;
- strict best report summary;
- frame-first landmarks;
- top frontier clusters;
- top unresolved strict blockers;
- top speculative future blockers;
- coverage totals;
- disk usage;
- stop reason;
- exact path to the portable bundle.

### 12. Identity and compatibility

The manifest records enough identity to reject incompatible resumes:

- diagnostic schema version;
- JOJO-Recompiled build identifier when available;
- executable metadata/FNV identity already verified by the installation manifest;
- OMEGA option set;
- epoch budget;
- strict/speculative policy version.

Resume against a different executable identity or incompatible schema must fail closed.

## Budget policy

### OMEGA epoch defaults

Use the existing OMEGA profile as the basis:

- `max_nodes = 16383`;
- `max_branch_depth = 32`;
- `max_total_retired = 3000000000` per epoch;
- `max_unique_frontiers = 96`;
- `max_speculative_depth = 24`;
- `max_candidates_per_read = 8`.

Infinity may roll to another epoch when a resumable budget is exhausted.

### Session disk budget

Default maximum durable session size: 16 GiB.

Hitting the disk ceiling stops safely with `disk_budget_exhausted`; it must not delete authoritative evidence behind the user's back.

### Hot memory limits

Hot trace/event buffers remain bounded. Durable chunks are the source of long-lived evidence.

## UI/workflow

Keep the existing short checkpoint action intact.

Add a separate action labeled in Portuguese:

`EXECUTAR OMEGA INFINITY`

Required controls/status:

- start new session;
- resume compatible paused session;
- request graceful stop;
- show current epoch;
- show cumulative retired count;
- show strict/speculative frontier counts;
- show current disk usage;
- show latest strict frontier;
- show whether a strict frame-first landmark was reached.

The diagnostic run must not freeze the UI message pump. Long execution therefore requires a worker/background execution mechanism owned by the process, with explicit cancellation and deterministic shutdown/join. This is local application concurrency, not asynchronous ChatGPT work.

## API separation

Keep these existing APIs working unchanged:

- `bootstrap_runtime_checkpoint*`
- `bootstrap_runtime_max3_local_evidence_to_file`
- `bootstrap_runtime_local_evidence_to_file`
- `explore_ps1_max3`

Add a separate Infinity API, conceptually:

```cpp
Result<Ps1OmegaInfinitySummary> run_ps1_omega_infinity(
    const std::filesystem::path& install_root,
    const std::filesystem::path& session_root,
    const Ps1OmegaInfinityOptions& options,
    const Ps1OmegaInfinityControl& control);
```

The exact cancellation/control type may be refined during implementation, but it must be explicit, testable and not rely on global mutable state.

## Determinism requirements

Given identical executable identity, options and decision sequence:

- normalized event payloads are deterministic apart from explicitly excluded wall-clock metadata;
- state hashes match across resume/replay;
- frontier ordering remains deterministic;
- strict-first ordering remains deterministic;
- summary counters are reproducible;
- chunk sequence/order is deterministic for a single-threaded exploration core.

UI/background execution must not make search ordering concurrent or nondeterministic. The exploration core remains single-threaded unless a future independently approved design changes that.

## Testing strategy

### Unit tests

- option defaults use 3,000,000,000 retired instructions per Infinity epoch;
- chunk writer commit/rename/checksum behavior;
- manifest ignores orphan temporary chunks;
- disk ceiling fails closed;
- strict/speculative provenance survives serialization;
- CPU snapshot round-trip;
- scheduler queue serialization/replay metadata round-trip;
- identity mismatch rejects resume;
- state-hash mismatch rejects resume;
- coverage merge across epochs;
- frame-first strict and speculative records stay separate;
- summary generation is deterministic for deterministic inputs.

### Integration tests

- tiny synthetic executable runs two deliberately small epochs and resumes into the second;
- a forced stop creates a resumable session;
- resume continues without duplicating committed events;
- ordinary bounded MAX3 behavior remains unchanged;
- existing local checkpoint still writes the legacy report successfully.

### Commercial gate

After Linux and Windows/MSVC CI are green, create a Windows artifact and run `EXECUTAR OMEGA INFINITY` against the user's owned JoJo installation.

Success for the infrastructure phase means a portable Infinity bundle is produced and can be parsed/reviewed, not that JoJo is declared playable.

The current strict frontier `0x1F801802 = 0x07` may remain unresolved because C2 is paused. Infinity is expected to preserve it as strict and, where safe speculative continuations exist, expose later speculative blockers as additional evidence.

## Scope gate

Before final CI, compare against baseline `b3f716b68cab835b8dc65230278078fef7b39b89`.

Unexpected semantic edits to GPU, DMA, CD-ROM commands, GTE, BIOS services, timers or unrelated runtime features fail the scope audit.

Infrastructure changes to runtime orchestration, diagnostics, serializers, UI and tests are expected.

## Completion criteria

OMEGA Infinity + Total Evidence is complete when:

1. spec and implementation plan are committed;
2. legacy checkpoint APIs remain compatible;
3. Infinity uses 3,000,000,000 retired instructions per epoch by default;
4. session continuation can cross an artificial epoch boundary;
5. durable evidence chunks are crash-safe and checksummed;
6. strict/speculative provenance is preserved end-to-end;
7. CPU/system snapshots and cumulative coverage are persisted;
8. resume validates executable/schema/state identity;
9. UI can start, display, stop and resume Infinity without blocking;
10. Linux and Windows/MSVC CI are green on the exact final SHA;
11. a Windows artifact is produced for the commercial OMEGA Infinity run;
12. no commercial boot/frame claim is made without strict evidence.
