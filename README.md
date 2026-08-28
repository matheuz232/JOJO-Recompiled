# JOJO Recompiled

JOJO Recompiled is an experimental native-Windows recompilation project for a **user-supplied, legally obtained** copy of *JoJo's Bizarre Adventure: Heritage for the Future*.

This repository contains **no game image, original executable, artwork, music, ROM data, or extracted copyrighted game assets**.

## Product direction

The end-user product is intentionally one executable:

```text
JOJO-Recompiled.exe
```

On first launch it asks for the user's own supported game image and prepares local converted data under `%LOCALAPPDATA%\JOJO Recompiled\game`. Once the native backend is complete, later launches go directly into the game. Graphics, controls, audio, mods, training tools and Online belong to the in-game menus rather than an external launcher.

## Current production milestone

Implemented in the portable core:

- C++20 + CMake/CTest foundation.
- `.iso`, `.bin`, `.cue`, `.gdi` source validation/fingerprinting.
- Source-independent conversion manifest.
- Event-driven conversion progress API (no fake timer progress).
- Persistent graphics model prepared for the in-game renderer:
  - 640×480 through 7680×4320 (8K)
  - 4:3 / 16:9 / 16:10 / 21:9 / 32:9
  - texture filtering Off / 2× / 4× / 8× / 16×
  - MSAA Off / 2× / 4× / 8×
  - windowed / fullscreen / borderless fullscreen
  - V-Sync
  - automatic or explicit UI scale (75–150%)
  - 16:9-safe or expanded HUD area
- Per-action input binding model.

Implemented for Windows:

- one shipping target, `JOJO-Recompiled.exe`;
- JoJo-inspired dark/magenta/purple/gold first-run preparation screen;
- user image picker;
- real conversion progress percentage + stage log;
- converted data stored in `%LOCALAPPDATA%\JOJO Recompiled`;
- UTF-8 source compilation under MSVC.

The game-specific native recompiler is **not finished yet**. A converted foundation manifest therefore still reports `pending-game-specific-recompiler`; the application does not pretend the commercial game is already playable.

## Build on Windows

See [`docs/BUILD-WINDOWS.md`](docs/BUILD-WINDOWS.md).

## Architecture / roadmap

See [`docs/architecture/PRODUCTION-ROADMAP.md`](docs/architecture/PRODUCTION-ROADMAP.md) for the planned disc parser, native recompiler, widescreen/HiDPI renderer, in-game settings, mod API, training frame meter, deterministic rollback core and Online modes.

## CI

GitHub Actions builds/tests the portable core on Linux and the complete x64 application on `windows-latest`. The Windows job uploads only `JOJO-Recompiled.exe` as its application artifact.
