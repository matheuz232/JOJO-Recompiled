# JOJO Recompiled

JOJO Recompiled is an experimental native-Windows recompilation project for a **user-supplied, legally obtained** copy of *JoJo's Bizarre Adventure: Heritage for the Future*.

This repository contains **no game image, original executable, artwork, music, ROM data, or extracted copyrighted game assets**.

## Product direction

The end-user product is intentionally one executable:

```text
JOJO-Recompiled.exe
```

On first launch it asks for the user's own supported game image and prepares local converted data under `%LOCALAPPDATA%\JOJO Recompiled\game`. Later launches should go directly into the game only after the production-readiness gates are actually satisfied. Graphics, controls, audio, mods, training tools and Online belong to the in-game menus rather than an external launcher.

## Current production program

The reusable architecture milestones M1–M8 are complete within their scoped contracts. The active program is **R2 — Production completion**, which requires evidence for real commercial-game integration before any global playability claim.

Canonical machine-checkable status: [`docs/architecture/PRODUCTION-READINESS.tsv`](docs/architecture/PRODUCTION-READINESS.tsv).

Commercial-game integration is not yet verified. In particular, a supported commercial revision still requires independently verified fingerprints from a legally supplied user image, and real game boot/render/audio/input/gameplay evidence remains outside the completed reusable contracts.

Implemented in the portable core includes the C++20/CMake/CTest foundation, disc-media parsing and revision infrastructure, deterministic SH-4/runtime contracts, presentation/settings/input contracts, mod runtime, training laboratory and rollback/networking core.

Implemented for Windows includes the single shipping target `JOJO-Recompiled.exe`, first-run image selection/conversion UI, `%LOCALAPPDATA%\JOJO Recompiled` storage, platform presentation/input adapters and Windows/MSVC CI coverage.

The generic native backend architecture is implemented, but that is not equivalent to verified commercial-game execution. Conversion/runtime readiness must remain truthful until R2.2–R2.6 evidence exists.

## Build on Windows

See [`docs/BUILD-WINDOWS.md`](docs/BUILD-WINDOWS.md).

## Architecture / roadmap

See [`docs/architecture/PRODUCTION-ROADMAP.md`](docs/architecture/PRODUCTION-ROADMAP.md) for the reusable architecture milestones and [`docs/superpowers/specs/2026-09-01-production-completion-design.md`](docs/superpowers/specs/2026-09-01-production-completion-design.md) for the evidence-based R2 production-completion program.

## CI

GitHub Actions builds/tests the portable core on Linux and the complete x64 application on `windows-2022`. Both jobs run the production-readiness gate, and the Windows job uploads only `JOJO-Recompiled.exe` as its application artifact.
