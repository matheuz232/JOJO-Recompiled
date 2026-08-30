# JOJO Recompiled — Offline Product Rebuild Design

## Status

Approved product direction for `feature/offline-product-rebuild`.

## Goal

Reduce JOJO Recompiled to one clear, testable product promise: a native Windows port of the original offline game experience, using a legally obtained user-supplied copy, with local two-player input and host-side compatibility/graphics improvements.

The project must not claim a feature complete unless the shipping `JOJO-Recompiled.exe` reaches and exercises that feature through the real end-user path.

## Product contract

The end user receives one Windows executable: `JOJO-Recompiled.exe`.

On first run the executable accepts a supported image of the user's own copy, validates the revision, converts/prepares only the local data required by the port, and records real progress from actual work stages. On subsequent runs it launches the converted game directly.

The final product must preserve the original offline game content and local two-player experience. Player 1 and Player 2 may each use independently selected local input devices. Keyboard, XInput controllers and supported generic HID gamepads remain valid host input sources. USB cable, Bluetooth and wireless dongles are transport details and must not change the logical binding model.

Host-side graphics and compatibility improvements may be exposed when they are real and wired to the runtime: resolution, aspect/presentation policy, filtering, anti-aliasing, window mode, V-Sync, UI scale and related presentation options.

## Explicitly out of scope

The following subsystems are removed from the product and from completion criteria:

- Online multiplayer in every form.
- Casual, Ranked, Direct and Custom online modes.
- Matchmaking, accounts, rooms, invite codes, relay/NAT traversal and network services.
- Rollback netcode, network packet/session protocol and production socket transport.
- Online profile/history/connection-quality UI and network replays.
- Mods, mod discovery, dependency resolution, overlays, mod hashes/policies and native mod plugins.
- Training laboratory, frame meter, training snapshots and training-only diagnostics.

These features must not remain linked into `JOJO-Recompiled.exe`, exposed in the shipping UI, or counted as future release blockers.

Historical documentation may mention removed milestones only when clearly labeled historical/removed and never as current product scope.

## What is preserved

The rebuild preserves reusable work that serves the offline commercial runtime and has independent value:

- Disc/media validation and ISO/BIN/CUE/GDI handling.
- ISO9660 filesystem parsing.
- Revision identification infrastructure.
- Dreamcast executable analysis and memory-map work.
- SH-4 decoder, CFG, IR, reference executor and native x64 backend work that is required by the commercial game.
- Dreamcast bus, interrupt, ASIC and PVR2 device work that is required for real boot/runtime behavior.
- Deterministic runtime infrastructure when used for correctness, save-state integrity or reproducible debugging independent of Online.
- Presentation/renderer host work.
- Two-player input model and Windows controller host.
- Core settings that are actually wired to the shipping runtime.
- General-purpose hashing/semantic-version utilities only when still used by retained components.
- Concrete CPU/device bug fixes and warning fixes unrelated to removed features.

Preservation does not upgrade a component to product-complete. Every retained component remains subject to end-to-end proof.

## Local two-player requirement

Local multiplayer is mandatory because it belongs to the original game experience.

The retained input contract must support exactly two logical players in the normal game path. Each player must be able to select a connected local device independently. Two physical controllers must work simultaneously. Mixed-device configurations such as keyboard + controller and two different controllers must work when the host APIs expose them.

Bindings must persist without tying identity to the physical transport type. Disconnect/reconnect behavior must not corrupt the other player's bindings.

The final acceptance test must include a real local versus match with two simultaneously active player inputs through the production executable.

## Removal strategy

Removal is dependency-driven rather than file-name driven.

1. Remove Online product-model code and tests.
2. Remove WinSock/network-host code and all network-only linkage.
3. Remove rollback and packet/session protocol code if no retained offline subsystem requires them.
4. Remove replay code when its only remaining purpose is Online/network replay. If a future offline replay feature is desired, it must be redesigned and approved separately rather than preserved implicitly.
5. Remove mod runtime, resolver, content/policy and native plugin loader code and tests.
6. Remove training code and tests.
7. Remove now-unused utility code only after dependency checks prove it is no longer required.
8. Update CMake, README, architecture docs and CI so removed features are neither built nor advertised.
9. Do not delete shared runtime/CPU/device code merely because a removed subsystem once consumed it.

## Rebuilt milestone structure

The previous M1-M9 numbering is no longer the product truth. The rebuilt roadmap uses end-user evidence gates:

### R1 — Media and revision intake

A supported user image is accepted, parsed and identified without guessed commercial fingerprints or silent fallback.

### R2 — Commercial boot/runtime

The real supported game executable reaches a verified boot path through the production runtime. Unsupported SH-4 operations, MMIO accesses or devices fail explicitly during development rather than being cosmetically ignored.

### R3 — Real video output

The commercial game produces real visible frames through the shipping renderer/presentation path. Resolution/aspect/filtering/MSAA options count only when changing real output.

### R4 — Real audio and local input

The commercial game produces real game audio and accepts Player 1 and Player 2 input through the production executable. Two-controller local play is mandatory.

### R5 — Original offline content completion

All normal offline menus, characters, stages, matches and progression/content expected from the supported original game revision are reachable and behave correctly enough for complete play.

### R6 — Persistence and host settings

Required game saves/configuration plus retained host settings survive restart and apply to the real runtime.

### R7 — Release hardening

A clean Windows environment can perform first-run conversion, relaunch the game and complete representative single-player and local two-player sessions through the same artifact that will be shipped.

Only R1-R7 together may justify an overall `100%` or `production-ready` claim.

## Testing policy

Tests are evidence, not substitutes for product execution.

Portable unit tests remain appropriate for parsers, CPU semantics, state transitions and validation. Windows integration tests remain appropriate for D3D11, controller enumeration/input and filesystem/runtime integration.

Every rebuilt release gate must add at least one production-path test or captured execution result that exercises the same component wiring used by `JOJO-Recompiled.exe`.

Mocks may be used for unit isolation but cannot satisfy a release gate by themselves. Placeholder backends, fake progress, no-op menu items and synthetic success returns are forbidden in shipping code.

## Failure policy

Unknown game revisions are rejected explicitly.

Unsupported CPU instructions, device operations or media layouts must produce actionable diagnostics during development. The runtime must not silently pretend successful execution after skipping behavior required by the commercial game.

Feature flags may temporarily isolate incomplete retained work during development, but hidden incomplete behavior may not be presented as finished functionality.

## CI and merge policy

Linux and Windows CI must stay green for retained portable/platform-specific components.

Removed Online/Mods/Training targets and tests disappear from CI rather than being left disabled indefinitely.

New warnings introduced by retained production code are treated as defects. Warning cleanup must fix the source where practical rather than hide diagnostics with broad pragmas.

A branch may merge only after exact-head CI verification. Overall product readiness still requires real commercial-media end-to-end evidence beyond CI.

## Repository history

The existing M6/M7/M8/M9 implementation history is not rewritten. Git history remains as engineering record. Current documentation, build targets and shipping code are what define active product scope.

The old `feature/m9-online-product-modes` branch is superseded by `feature/offline-product-rebuild`. Useful CPU/device/warning fixes already present at its head may be retained; Online/Mods/Training-specific code is removed in this branch.

## Acceptance definition

This scope reduction is complete only when:

- the shipping executable contains no Online/Mods/Training UI or production linkage;
- network-only libraries are not linked into the product;
- local Player 1/Player 2 input remains built and tested;
- documentation no longer advertises removed features as product goals;
- CI is green after removal; and
- the roadmap's only final `100%` gate is full offline commercial-game functionality plus local two-player play through the real Windows executable.
