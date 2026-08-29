# JOJO-Recompiled: path to first playable backend

## Goal
Reach the first genuinely playable Windows build without falsely marking the installation `native-ready` before the game-specific backend can boot and run the user-provided game data.

## Execution order
1. Expand SH-4 integer/control coverage with test-first increments.
2. Add unsupported-opcode census/reporting so future instruction work is prioritized by actual executable coverage rather than guesswork.
3. Model the remaining architectural SH-4 state needed by real code: status/control registers, exception return, banked registers, dynamic shifts/rotates, MAC operations and FPU.
4. Build a Dreamcast executable loader and deterministic memory map for the analyzed boot executable.
5. Add MMIO/device boundaries and deterministic timing surfaces required to get from reset/bootstrap into game code.
6. Implement the minimum PVR2/video, Maple/input, GD-ROM/data access and AICA/audio behavior needed for first interactive gameplay, keeping rendering/audio adapters outside deterministic gameplay state.
7. Replace the current reference-only execution path with a production backend while retaining the reference executor as an oracle for differential tests.
8. Mark conversion output `native-ready` only after an end-to-end test proves that the generated installation launches through `JOJO-Recompiled.exe` into an interactive game state using user-provided legally obtained data.
9. After first playable, harden performance, graphics/aspect handling, controls, mods, training instrumentation and rollback networking against the approved design.

## Global constraints
- TDD: no production behavior without a failing test first.
- No copyrighted game data in the repository or CI.
- One end-user executable remains the packaging target.
- Do not fake conversion progress, backend readiness, boot success or playability.
- Keep gameplay simulation deterministic and renderer-independent so rollback and training tooling remain possible.
- Every feature branch must pass Linux portable-core and Windows x64/MSVC CI before merge, followed by a post-merge `main` verification.
