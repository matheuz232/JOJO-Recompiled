# JOJO Recompiled — Offline-Only Product Scope Design

Date: 2026-08-30
Status: Approved design direction, pending implementation plan
Branch: `feature/offline-only-product-scope`

## 1. Product decision

JOJO Recompiled is an offline native-Windows port/recompilation project for a user-supplied, legally obtained copy of *JoJo's Bizarre Adventure: Heritage for the Future*.

The product scope is intentionally reduced to the original game's normal offline content plus host-side compatibility and presentation improvements needed for a correct modern Windows experience.

The project will not ship Online, networking, matchmaking, rollback netcode, mod support, project-added training-laboratory features, or any UI that implies those removed systems exist.

Local two-player play remains mandatory because it is part of the original game experience. The runtime must support two independently configurable local player input routes and must not depend on networking code to do so.

All content and modes that belong to the supported original game revision remain in scope. If the original game contains a built-in training/practice mode, that original mode remains; only the separate project-added Training Laboratory subsystem is removed.

## 2. Product contract

The end user receives one executable:

`JOJO-Recompiled.exe`

On first launch, the executable asks for the user's own supported game image, validates it, prepares/recompiles the required local data, and records real progress from actual work stages. On later launches it opens the game directly once the installation is truly runnable.

A product-level completion claim requires end-to-end proof using legally supplied supported media. CI models, mocks, placeholders, fake progress, disconnected settings models, or host UI that does not affect the commercial runtime do not count as product completion.

## 3. Required final features

The final product must preserve and run the original game's normal offline content, including local versus play for two players.

Required host-side functionality:

- supported user-supplied game media identification and conversion;
- complete commercial boot/runtime path required by the supported revision;
- native execution or an explicitly documented development fallback only while the product is not yet release-ready;
- correct Dreamcast CPU/device behavior required by the game;
- real frame rendering from the commercial game;
- real game audio;
- real local player input;
- two independent local player input configurations;
- keyboard and supported Windows game controllers through cable, Bluetooth, or wireless dongle where Windows exposes them through the supported input APIs;
- save/configuration persistence required for normal play;
- resolution and presentation improvements that do not falsify gameplay state;
- supported aspect-ratio handling without non-uniform stretching;
- texture filtering, anti-aliasing, display mode, V-Sync, and UI-scale controls only where they are connected to the actual renderer/runtime;
- one shipping Windows executable and a reproducible Windows build/test path.

## 4. Systems to remove completely

The following are outside product scope and must be removed from production code, build targets, tests, documentation, menus, settings, release claims, and milestone accounting unless a small shared primitive is independently required by the offline runtime.

### 4.1 Online and networking

Remove:

- Online product modes;
- Casual matchmaking;
- Ranked/competitive matchmaking;
- Direct online rooms/invites;
- Custom online sessions;
- online profile/history abstractions;
- online connection indicators and lifecycle state;
- network telemetry shown as a product feature;
- WinSock UDP transport added for Online;
- network session hello/accept/disconnect machinery when it has no offline consumer;
- packet retransmission/acknowledgement machinery when it has no offline consumer;
- public/backend service abstractions created only for Online;
- network-specific settings and UI;
- Online replay plumbing created specifically for M9.

### 4.2 Rollback/networking core

Remove the M8 rollback/networking product subsystem when it is used only for Online. This includes rollback-session orchestration, network protocol framing, packet telemetry, network reliability, and deterministic networking RNG ownership that has no independent offline consumer.

Do not remove general deterministic execution, save-state primitives, hashing utilities, frame indexing, or other reusable offline-runtime components merely because they were also useful to rollback. Shared primitives are retained only if they have a concrete offline consumer and tests after the removal.

### 4.3 Mods

Remove:

- mod discovery and manifest loading;
- dependency resolution;
- overlay routing;
- mod content hashes used only for mod identity;
- ranked/custom mod policies;
- native mod plugin loading and the public mod ABI;
- mod menus/settings;
- native-mod test fixtures and build targets;
- roadmap/release claims about mod support.

General utilities such as SHA-256 remain when used by installation validation, runtime state verification, cache identity, or other offline product requirements.

### 4.4 Project-added Training Laboratory

Remove:

- frame-data training timeline;
- startup/active/recovery training diagnostics;
- hitbox/hurtbox/pushbox visualization abstractions created for Training Laboratory;
- combo/scaling training display added by the project;
- project-added training save-state slots;
- exact frame-step UI created specifically for Training Laboratory;
- Training Laboratory menu/settings and product claims.

Do not remove or alter a training/practice mode that is part of the original supported game. Original game behavior remains product content and must run correctly like every other original mode.

Low-level deterministic state capture/restore may remain only where required by the runtime, validation, debugging tests, or normal game save behavior. It must not remain presented as a project-added Training feature.

## 5. Local multiplayer preservation

Local two-player input is explicitly retained and is independent from Online removal.

The input architecture must preserve:

- `input_player_count == 2` or an equivalent explicit two-player contract;
- independent selected devices per player;
- independent per-action bindings per player;
- simultaneous action resolution for both players in the same local frame;
- hot-plug handling without silently reassigning the other player's bindings;
- keyboard/controller coexistence where supported;
- two connected controllers where supported;
- controller transport transparency: cable, Bluetooth, and wireless dongle are treated according to the Windows input device exposed to the application, not as separate gameplay modes.

The final product gate requires an end-to-end local two-player test through the production input path, not only unit tests of binding objects.

## 6. Milestone reset

The previous M1-M9 numbering is no longer a product-completion ladder because M6-M9 included systems that are now removed from scope and several earlier milestones represented reusable contracts rather than complete commercial integration.

Implementation planning must replace the old completion language with an offline product sequence focused on proof:

- Foundation and media ingestion;
- Commercial revision identification;
- Commercial boot and CPU/device compatibility;
- Native runtime/recompiler completion for the supported game path;
- PVR2 rendering and modern presentation;
- AICA/audio path;
- Maple/local input and two-player production integration;
- normal save/configuration persistence;
- complete original-content playthrough validation;
- Windows release hardening.

Existing code may be reused, rewritten, or deleted according to evidence. Historical roadmap documents can remain only if clearly marked historical; no obsolete `Complete` label may be allowed to imply current product readiness.

## 7. Implementation strategy

Do not restart the repository from zero by default. Preserve code that has a real offline consumer and verified behavior, because deleting verified CPU/media/runtime work would increase risk without improving correctness.

For every existing subsystem, classify it during implementation as one of:

- `PROVEN-OFFLINE`: used by the production offline path and verified;
- `PARTIAL`: real implementation but missing product integration or coverage;
- `DEV-ONLY`: useful development/reference implementation not acceptable as the release path;
- `REMOVE`: belongs only to Online, Mods, Training Laboratory, or otherwise removed scope;
- `BROKEN`: concrete defect requiring a regression test and fix before reuse.

Only `PROVEN-OFFLINE` behavior may support a product completion claim.

## 8. Removal safety rules

Removal work must be dependency-driven, not filename-driven.

Before deleting a shared file or symbol:

1. identify all consumers;
2. distinguish removed-feature consumers from offline-runtime consumers;
3. migrate any legitimate offline dependency to a smaller neutral primitive if needed;
4. add or retain regression tests for the offline behavior;
5. remove the obsolete subsystem;
6. rebuild and run the full Linux and Windows suites.

The shipping executable must not link WinSock or removed feature libraries after the cleanup unless an independently justified offline requirement is discovered and documented.

## 9. Documentation and UI truthfulness

README, roadmap, build docs, user-facing text, menus, and settings must describe only functionality that exists in the production path.

Remove wording that advertises Online, Mods, Training Laboratory, matchmaking, ranked play, network connection quality, mod APIs, or similar removed features. References to a training/practice mode that genuinely belongs to the original game are not removed.

Do not leave disabled menu entries, "coming soon" buttons, hidden fake panels, placeholder services, or visual remnants that suggest removed project-added features will ship.

Historical engineering documents may be retained in a clearly marked archive/history location if useful, but they must not be linked as current product capabilities.

## 10. Verification requirements

Every destructive cleanup batch must pass:

- configure + build on Linux;
- full portable CTest suite on Linux;
- configure + Release build with MSVC x64;
- full Windows CTest suite;
- creation of `JOJO-Recompiled.exe`;
- inspection that removed production targets/files are no longer linked into the executable;
- regression verification of two-player local input;
- documentation scan for current claims about removed features.

The final offline product must additionally pass real-media end-to-end verification on Windows using a legally supplied supported game copy.

## 11. Definition of 100%

JOJO Recompiled may be called 100% only when the supported original game content can be used through the final Windows product path without fake or disconnected functionality.

At minimum, evidence must prove:

- first-run media selection and real preparation;
- supported revision identification;
- commercial boot;
- playable original game content through the required original game modes;
- correct real rendering and audio;
- responsive local controls;
- local two-player play with two independently routed player inputs;
- persistent settings/saves required for normal use;
- graphics options actually changing the production renderer where applicable;
- close/relaunch behavior from the prepared installation;
- clean Windows build and release artifact;
- no production dependency on removed Online, Mods, or project-added Training Laboratory systems;
- no fake progress, placeholder backend, decorative-only control, or unverified `100%` claim.

Any unmet item keeps overall product status below 100%.