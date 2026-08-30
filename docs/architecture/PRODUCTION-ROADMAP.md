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

### M3 — Native recompiler backend — Complete (100%)

Completion evidence is tracked in [`../superpowers/plans/2026-08-29-playable-backend-path.md`](../superpowers/plans/2026-08-29-playable-backend-path.md).

#### M3.1 — Dreamcast executable loader + deterministic memory map — Complete (100%)

- [x] Analyze the supported plain GD-ROM boot executable before loading it.
- [x] Load executable bytes deterministically at `0x8C010000` into a zero-initialized 16 MiB main-RAM backing store.
- [x] Model the complete 64 MiB Area-3 address window as four mirrors of the same 16 MiB RAM for physical, P1 cached and P2 uncached aliases.
- [x] Keep Dreamcast bus classification consistent with those RAM mirrors.
- [x] Reject unsupported MIL-CD/unknown encodings before memory preparation instead of guessing normalization.
- [x] Linux and Windows/MSVC build/test coverage for the complete M3.1 contract.

#### M3.2 — Deterministic native runtime + versioned compiled-code cache — Complete (100%)

- [x] Compile eligible straight-line SH-4 CFG/IR blocks into executable x86-64 machine code; blocks without a dedicated machine-code lowering remain an explicit reference fallback.
- [x] Execute generated code from RX memory using the correct Windows x64 or SysV x64 calling convention.
- [x] Deterministic `step_native_frame()` state transition with frame index and CPU+RAM state hashing.
- [x] Versioned, host-ABI-specific native-backend ABI exposed independently from the application/core version.
- [x] Reloadable compiled code/data cache stored under `<converted-game>/cache/native/compiled_plan.bin` with metadata in `backend_cache.ini`; the binary cache persists the generated machine-code bytes themselves.
- [x] Cache reuse when ABI/core/program identity matches.
- [x] Automatic rebuild independently verified for ABI changes, core-version changes and program-content changes.
- [x] Linux x86-64 and Windows x64/MSVC build/test coverage that executes generated machine code on both host ABIs.

M3 criteria:

- [x] Parse/analyze the supported executable and establish its deterministic memory map (M3.1).
- [x] Lift supported CPU instructions into an explicit intermediate representation.
- [x] Deterministic runtime state and frame-step API.
- [x] Generated native code/data cache stored in the converted game directory.
- [x] Recompiler versioning and automatic rebuild when ABI/version changes.

M3 completion closes the **backend architecture contract defined by this roadmap**. It does **not** claim that every SH-4 IR operation already has a dedicated x86-64 machine-code lowering: blocks without one remain explicit and observable reference fallbacks. It also does **not** mark a commercial installation `native-ready`, prove real-game boot, render a real frame/menu, or provide functional game input. Those require the following device/integration milestones and end-to-end evidence with legally supplied game data.

### M4 — Renderer, presentation and aspect correction — Complete (100%)

Completion evidence is tracked in [`../superpowers/plans/2026-08-30-presentation-renderer.md`](../superpowers/plans/2026-08-30-presentation-renderer.md).

- [x] Separate simulation resolution from presentation resolution.
- [x] 4:3, 16:9, 16:10, 21:9 and 32:9 camera/presentation policies.
- [x] No non-uniform stretching; output uses a centered aspect-fitted viewport with one uniform scale.
- [x] UI logical-coordinate system with safe areas and DPI-aware scaling from 480p through 8K.
- [x] Windowed, exclusive fullscreen (when available) and borderless fullscreen host plans, with deterministic borderless fallback when exclusive capability is unavailable.
- [x] Texture filtering Off/2x/4x/8x/16x and MSAA Off/2x/4x/8x negotiated against renderer/device capabilities; Windows CI probes a real D3D11 hardware-or-WARP device for multisample support.
- [x] Linux portable-core and Windows x64/MSVC build/test coverage for the complete M4 presentation contract.

M4 completion closes the **host presentation/aspect/quality contract defined above**. It does **not** claim that Dreamcast PVR2 scene rendering is complete, that a commercial revision boots, that a real frame/menu is visible, or that game input/audio are functional. Those remain device/integration work and are still required before conversion output can become `native-ready`.

### M5 — In-game settings + input — Complete (100%)

Completion evidence is tracked in [`../superpowers/plans/2026-08-30-ingame-settings-input.md`](../superpowers/plans/2026-08-30-ingame-settings-input.md) and [`../superpowers/plans/2026-08-30-ingame-settings-input-verification.md`](../superpowers/plans/2026-08-30-ingame-settings-input-verification.md).

- [x] Runtime graphics, audio and controls settings modeled for the in-game menu while remaining absent from the first-run conversion shell.
- [x] Two-player per-device bindings, deterministic action resolution and interactive binding capture.
- [x] Backward-compatible player-1 input settings loading with the new two-player persisted schema.
- [x] Keyboard, dynamically loaded XInput and generic joystick/gamepad HID support on Windows.
- [x] USB cable, Bluetooth and wireless dongle remain transport-transparent to persisted bindings.
- [x] Hot-plug catalog refresh with explicit connected/disconnected changes while preserving bindings for devices that later reconnect.
- [x] Real Windows Raw Input HID decoding plus XInput HID-shadow filtering to avoid duplicate physical controllers.
- [x] Linux portable-core and Windows x64/MSVC build/test coverage, including Windows executable artifact upload.

M5 completion closes the reusable **settings/menu/input runtime contract**. It does **not** claim that a commercial revision currently reaches a rendered native settings screen, that Dreamcast Maple input has been wired into original game code, that AICA audio playback is complete, or that a converted commercial installation is `native-ready`. Those remain real-game/device integration work requiring legally supplied media for end-to-end evidence.

### M6 — Mod runtime — Complete (100%)

Completion evidence is tracked in [`../superpowers/plans/2026-08-30-mod-runtime.md`](../superpowers/plans/2026-08-30-mod-runtime.md).

Two compatibility levels:

1. Data/script mods for assets, localization, UI, stages, character data and gameplay definitions.
2. Explicitly opt-in native plugins through a versioned C ABI for invasive extensions.

The completed portable contract includes:

- [x] Strict manifests, semantic API compatibility and deterministic discovery.
- [x] Dependency closure, version requirements, cycle/conflict diagnostics and stable topological load order.
- [x] Data overlays with normalized logical paths, deterministic collision reporting and symlink/traversal rejection.
- [x] SHA-256 content, full mod-set and gameplay-only identities independent of host path separators and file creation order.
- [x] Ranked rejection of gameplay-changing mods and optional exact mod-set matching for custom sessions.
- [x] Native plugins disabled by default and enabled only through explicit opt-in to the versioned C ABI.
- [x] Real dynamic-library lifecycle coverage on Linux and Windows/MSVC, including descriptor validation, partial-failure cleanup and reverse-order unload.

M6 completion closes the reusable mod-runtime contract. It does **not** claim that commercial game assets are already routed through overlays, that in-game mod controls exist, that native plugins are sandboxed, or that the converted installation is `native-ready`.

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
