# JOJO Recompiled — Production Architecture

## Product contract

The end user receives and launches a single executable: `JOJO-Recompiled.exe`.

On first launch the executable asks for a legally obtained image of the user's own game copy, validates it, converts/recompiles the required content into `%LOCALAPPDATA%/JOJO Recompiled/game`, and reports real stage progress. On subsequent launches it goes directly into the game.

Graphics, controls, audio, mods, training tools and online options belong to the in-game UI. The first-run conversion UI is not a launcher/settings application.

The repository must never contain game images, extracted copyrighted assets, generated game data, or user save/configuration files.

## Milestones

### M1 — Production foundation — Complete (100%)

Completion evidence is tracked in [`../superpowers/plans/2026-08-28-production-foundation.md`](../superpowers/plans/2026-08-28-production-foundation.md).

- [x] One Windows end-user executable.
- [x] First-run conversion flow with progress events and a persistent conversion log.
- [x] Modern custom-drawn JoJo-inspired first-run visual language.
- [x] `%LOCALAPPDATA%` application/game-data layout.
- [x] Graphics model extended to MSAA 8x, windowed/fullscreen/borderless and UI scaling.
- [x] No external graphics/control tabs in the first-run shell.
- [x] Windows CI build + tests.

M1 completion does **not** imply that the commercial game is playable or that the native backend is ready; those remain later milestones.

### M2 — Disc filesystem + game revision identification — Complete (100%)

Completion evidence is tracked in [`../superpowers/plans/2026-08-28-disc-filesystem.md`](../superpowers/plans/2026-08-28-disc-filesystem.md) and [`../superpowers/plans/2026-08-28-track-media.md`](../superpowers/plans/2026-08-28-track-media.md).

- [x] Read-only disc filesystem abstraction.
- [x] ISO9660 parser with track-aware ISO/BIN/CUE/GDI readers behind the same logical-sector interface.
- [x] Locate executable/data files through the disc filesystem without hard-coded host paths.
- [x] Identify a supported revision from multiple declarative file fingerprints.
- [x] Reject unknown revisions explicitly with profile/file-level diagnostics instead of guessing offsets.
- [x] Linux and Windows/MSVC CI coverage for the complete media/filesystem/revision pipeline.

M2 completion covers the safe media, filesystem and revision-profile infrastructure. Commercial revision fingerprints are intentionally **not** guessed or copied from game data: until signatures are independently verified from legally supplied media, that media remains an explicit `unknown_revision`. M2 completion does **not** imply that a commercial revision is enabled, that the native backend is ready, or that the game boots.

### M3 — Native recompiler backend

- Parse executable format and memory map.
- Lift supported CPU instructions into an explicit intermediate representation.
- Deterministic runtime state and frame-step API.
- Generated native code/data cache stored in the converted game directory.
- Recompiler versioning and automatic rebuild when ABI/version changes.

### M4 — Renderer, presentation and aspect correction

- Separate simulation resolution from presentation resolution.
- 4:3, 16:9, 16:10, 21:9 and 32:9 camera/presentation policies.
- No non-uniform stretching.
- UI logical-coordinate system with safe areas and DPI-aware scaling from 480p through 8K.
- Windowed, exclusive fullscreen (when available) and borderless fullscreen.
- Texture filtering Off/2x/4x/8x/16x and MSAA Off/2x/4x/8x where the active renderer/device supports it.

### M5 — In-game settings + input

- Graphics, audio and controls inside the game menus.
- Keyboard, XInput and generic HID devices.
- USB, Bluetooth and dongle transport are transparent to the binding model.
- Per-player/per-device bindings and hot-plug refresh.

### M6 — Mod runtime

Two compatibility levels:

1. Data/script mods for assets, localization, UI, stages, character data and gameplay definitions.
2. Explicitly opt-in native plugins through a versioned C ABI for invasive extensions.

The loader owns manifests, semantic API version, dependency graph, load order, conflict diagnostics and gameplay-mod hashing. Ranked online rejects gameplay-changing mods; custom rooms can require an exact mod set.

### M7 — Training laboratory

A deterministic frame timeline powers:

- startup / active / recovery bars;
- hitstop, hitstun, blockstun, advantage and cancel windows;
- hitbox/hurtbox visualization;
- input history, damage/scaling and combo information;
- pause and frame-step;
- save/load training state.

The visual meter uses icon/labels in addition to color so it remains readable with color-vision deficiencies.

### M8 — Rollback networking core

The simulation contract is intentionally designed before matchmaking:

- deterministic `step_frame(local, remote)`;
- snapshot save/restore;
- state hashing/desync detection;
- deterministic RNG ownership;
- side-effect suppression during rollback re-simulation.

Transport targets low-latency UDP input exchange with reliability only for control/session messages. Network telemetry tracks RTT, jitter, packet loss, predicted frames, rollback depth and disconnect state.

No implementation may promise zero latency: physical RTT, jitter and packet loss are external constraints. The product target is minimum avoidable local latency and graceful behavior under real-world network conditions.

### M9 — Online product modes

In-game Online menu:

- Casual matchmaking;
- Competitive/ranked matchmaking;
- Direct 1v1 rooms/invites;
- Custom matches with rules/mod policy;
- profile/history/replays;
- network settings and telemetry.

Every player connection exposes a compact signal icon plus text state. Quality is computed from RTT, jitter, packet loss and rollback behavior, not ping alone. Disconnect, reconnecting and voluntary leave are distinct states.

## Architectural rules

- Game simulation never depends on wall-clock rendering cadence.
- Rendering never owns authoritative gameplay state.
- Networking never directly mutates gameplay objects; it supplies timestamped/frame-numbered inputs and session commands.
- Mods consume versioned public interfaces rather than private offsets whenever possible.
- UI layout uses logical coordinates + anchors/safe areas, never resolution-specific pixel patches.
- Conversion progress is event-driven from actual work stages, never timer-faked.
- User-facing distribution contains one executable; developer test/build tools are not part of the release package.
