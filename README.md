# JOJO Recompiled

JOJO Recompiled is an experimental native-Windows recompilation project for a **user-supplied, legally obtained** copy of *JoJo's Bizarre Adventure: Heritage for the Future*.

This repository contains **no game image, original executable, artwork, music, ROM data, or extracted copyrighted game assets**.

## Product contract

The end-user product is intentionally one executable:

```text
JOJO-Recompiled.exe
```

On first launch it asks for the user's supported game image and prepares local converted data under `%LOCALAPPDATA%\JOJO Recompiled\game`. The final product target is the original **offline** game experience running through the native Windows runtime, including local two-player play.

The active product does **not** include Online multiplayer, rollback/netcode, Mods or Training tools. Their previous implementation milestones remain only as repository history and are not shipping goals.

## Required retained improvements

The offline port keeps these host-side improvements as release requirements:

- selectable output resolution, including high-resolution output up to 8K where supported;
- 4:3, 16:9, 16:10, 21:9 and 32:9 presentation without non-uniform stretching;
- texture filtering, MSAA, window mode, V-Sync and presentation/UI scaling when wired to real rendered gameplay;
- local Player 1 + Player 2 input, including two simultaneously connected controllers and mixed keyboard/controller configurations;
- a **real 60 FPS commercial-runtime patch**.

The repository already contains resolution/aspect presentation infrastructure. **60 FPS is not currently claimed complete.** A 60 Hz swap chain, duplicated frames, interpolation by itself, or a decorative menu toggle does not satisfy the requirement. It must be demonstrated on the real commercial runtime with correct gameplay speed, update/input timing and audio synchronization.

## Current status

**Overall product status: IN PROGRESS.**

Reusable infrastructure currently includes C++20/CMake/CTest, ISO/BIN/CUE/GDI media handling, ISO9660 parsing, revision/profile infrastructure, Dreamcast executable analysis/memory work, substantial SH-4 decoder/IR/reference/native-backend work, presentation models, and a two-player input model with Windows controller support.

Those components are engineering foundations, not proof that the commercial game is already fully playable. `JOJO-Recompiled.exe` may only be described as production-ready after the end-to-end R1-R7 gates in [`docs/architecture/PRODUCTION-ROADMAP.md`](docs/architecture/PRODUCTION-ROADMAP.md) are proven with a legally supplied supported game copy.

## Build on Windows

See [`docs/BUILD-WINDOWS.md`](docs/BUILD-WINDOWS.md).

## Architecture / roadmap

See [`docs/architecture/PRODUCTION-ROADMAP.md`](docs/architecture/PRODUCTION-ROADMAP.md).

## CI

GitHub Actions builds/tests the retained portable core on Linux and the x64 application on Windows/MSVC. CI proves the tested engineering contracts; it does not substitute for commercial-game end-to-end evidence.
