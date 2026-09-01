# JOJO Recompiled — Production Roadmap

## Product contract

The active product is a native Windows port of the original **offline** *JoJo's Bizarre Adventure: Heritage for the Future* experience built from a supported, legally obtained user-supplied game copy.

The end user receives one executable: `JOJO-Recompiled.exe`. First run performs real media validation/conversion work; subsequent runs must launch the converted game directly once the commercial runtime is actually ready.

Local two-player play is mandatory. Online multiplayer, rollback/netcode/network services, Mods and Training tools are removed from active product scope.

## Product readiness rule

**Overall product status: IN PROGRESS.**

No percentage, milestone label, unit-test count or CI badge may substitute for end-to-end product evidence. The project may claim overall `100%` / `production-ready` only after **R1 through R7** below are all proven through the same production path used by `JOJO-Recompiled.exe`.

Mocks, placeholder backends, fake progress, no-op UI, synthetic success returns, settings that are not wired to the commercial runtime, duplicated frames presented as gameplay 60 FPS, or isolated model tests do not satisfy a release gate.

## Active release gates

### R1 — Media and revision intake — IN PROGRESS

Required evidence:

- accept the supported ISO/BIN/CUE/GDI layout from the user's own copy;
- parse the media and disc filesystem read-only;
- identify a supported commercial revision from independently verified fingerprints;
- reject unknown revisions explicitly rather than guessing offsets or silently continuing;
- feed the exact validated commercial executable/data into the production conversion/runtime path.

Portable parser/revision infrastructure exists. The preparation path now materializes validated PS1 boot files and a normalized 2048-byte-sector data track (`SYSTEM.CNF`, `PSX.EXE`, `DISC.ISO`) so later runtime execution does not depend on reopening the user's original image. This is enabling infrastructure only: R1 is not complete until supported commercial-media evidence is recorded.

### R2 — Commercial boot/runtime — NOT COMPLETE

Required evidence:

- boot the real supported commercial PS-X executable through the production runtime;
- execute every R3000A/GTE operation required to reach normal game operation correctly;
- implement required PS1 memory/MMIO/device behavior instead of ignoring unsupported accesses, including the GPU, CD-ROM, SIO/controllers, SPU/audio, DMA, timers and interrupts used by the game;
- fail explicitly and diagnostically during development on unsupported behavior;
- keep the prepared/local runtime path honest: a successfully parsed or loaded executable must never be cosmetically reported as a game-ready installation before commercial boot is actually proven.

Existing PS1 R3000A/GTE/bus/BIOS/MMIO contracts, prepared-runtime support and historical Dreamcast/SH-4 work are engineering foundations/history only; none of them by themselves proves commercial boot.

### R3 — Real video output + resolution/aspect/60 FPS — NOT COMPLETE

Required evidence:

- render real commercial-game frames through the shipping Windows presentation path;
- make selected output resolution affect actual rendered output;
- support 4:3, 16:9, 16:10, 21:9 and 32:9 without non-uniform horizontal stretching;
- apply filtering/MSAA/window-mode/V-Sync/presentation options only where they affect the real renderer;
- validate gameplay scenes and UI at representative resolutions/aspects;
- implement and prove a **real 60 FPS commercial-runtime patch**.

The 60 FPS gate requires correct game/update cadence, gameplay speed, input timing, animation/timing behavior and audio synchronization. A 60 Hz swap chain alone does not pass. Frame duplication alone does not pass. Interpolation may be an optional presentation feature but cannot be labeled as the gameplay 60 FPS patch unless the underlying runtime update behavior meets the requirement.

Resolution/aspect presentation infrastructure exists. **The real 60 FPS patch is currently unproven and must remain marked incomplete until runtime evidence exists.**

### R4 — Real audio + local two-player input — NOT COMPLETE

Required evidence:

- produce real commercial-game audio through the shipping runtime;
- keep audio synchronized in normal and verified 60 FPS modes;
- connect Player 1 and Player 2 host input to the real game's PS1 controller/SIO path;
- support two simultaneously active physical controllers;
- support mixed local configurations such as keyboard + controller where exposed by host APIs;
- preserve independent bindings across disconnect/reconnect;
- complete a real local versus match through `JOJO-Recompiled.exe` with both players providing simultaneous input.

The reusable input model defines exactly two logical players and Windows controller support. Host actions are also translated to the original PS1 digital-pad button layout, but R4 still requires SIO/runtime integration, real audio and commercial-game evidence.

### R5 — Original offline content completion — NOT COMPLETE

Required evidence:

- normal menus and game flow operate correctly;
- original playable characters and expected selectable content for the supported revision are reachable;
- stages and match flow work;
- single-player/offline progression and normal game modes expected from the supported original revision are playable;
- no required original content is blocked by missing CPU/device/render/audio/input behavior.

Removed Online/Mods/Training features are not part of this gate.

### R6 — Persistence + real host settings — NOT COMPLETE

Required evidence:

- required game save data persists correctly across restarts;
- host configuration persists correctly;
- Player 1/Player 2 bindings persist;
- selected resolution/aspect/presentation settings apply to the real runtime after restart;
- a 60 FPS setting/control is exposed only after the real patch exists and must persist/apply correctly when enabled.

Do not add a decorative 60 FPS toggle before the runtime patch exists.

### R7 — Release hardening — NOT COMPLETE

Required evidence from the same Windows artifact intended for users:

- clean-environment first launch;
- select and validate a supported legal game image;
- complete conversion/preparation through real event-driven stages;
- close/relaunch and enter the game directly;
- complete representative single-player gameplay;
- complete representative local two-player gameplay with two active inputs;
- validate representative native resolutions and 4:3/widescreen/ultrawide modes;
- validate real 60 FPS gameplay timing and audio synchronization when 60 FPS is enabled;
- preserve saves/configuration after restart;
- no Online/Mods/Training shipping linkage or UI.

Only R1-R7 together may justify overall `100%` or `production-ready`.

## Active architecture rules

- Game simulation never depends on wall-clock rendering cadence.
- Rendering never owns authoritative gameplay state.
- UI/settings are counted only when wired to real runtime behavior.
- Local input supports exactly two logical players for the original two-player experience.
- UI/presentation uses logical coordinates/anchors/safe areas rather than resolution-specific stretching patches.
- Conversion progress comes from actual work events, never a fake timer.
- Unknown media/revisions and unsupported CPU/device behavior fail explicitly during development.
- The repository/distribution contains no copyrighted game image or extracted proprietary assets.
- The shipping product has no Online/networking, Mod/plugin, or Training-laboratory dependency.

## Historical engineering milestones

The former M1-M9 milestones remain useful Git history and documentation for engineering work that was attempted or verified in isolation. They are **not the current product-readiness model**.

- M1-M5 produced reusable foundation, media/recompiler/presentation/settings/input contracts that may still serve the offline port and remain subject to R1-R7 end-to-end proof.
- M6 (Mods), M7 (Training), M8 (rollback/networking) and M9 (Online product modes) are removed from active product scope. Their historical commits/docs may remain for audit/history, but their production sources/targets are not part of the shipping product.

The active truth is R1-R7 above.
