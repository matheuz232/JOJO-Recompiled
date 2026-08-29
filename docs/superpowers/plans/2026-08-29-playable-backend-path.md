# JOJO-Recompiled: path to first playable backend

## Goal
Reach the first genuinely playable Windows build without falsely marking the installation `native-ready` before the game-specific backend can boot and run the user-provided game data.

## Execution order
1. [x] Expand SH-4 integer/control coverage with test-first increments.
2. [x] Add unsupported-opcode census/reporting so future instruction work is prioritized by actual executable coverage rather than guesswork.
3. [x] Model the remaining architectural SH-4 state needed by real code: status/control registers, exception return, banked registers, dynamic shifts/rotates, MAC operations and FPU.
4. [x] Build a Dreamcast executable loader and deterministic memory map for the analyzed boot executable. **M3.1 complete (100%).**
5. [ ] Add MMIO/device boundaries and deterministic timing surfaces required to get from reset/bootstrap into game code.
6. [ ] Implement the minimum PVR2/video, Maple/input, GD-ROM/data access and AICA/audio behavior needed for first interactive gameplay, keeping rendering/audio adapters outside deterministic gameplay state.
7. [ ] Replace the current reference-only execution path with a production backend while retaining the reference executor as an oracle for differential tests.
8. [ ] Mark conversion output `native-ready` only after an end-to-end test proves that the generated installation launches through `JOJO-Recompiled.exe` into an interactive game state using user-provided legally obtained data.
9. [ ] After first playable, harden performance, graphics/aspect handling, controls, mods, training instrumentation and rollback networking against the approved design.

## M3.1 completion evidence

- RED `eca5b75accb90ec98c0a7bc40ecd8fbb9345493d`: CI #533 failed at compile time because executable preparation did not yet exist.
- Additional RED `305c1d110578f4322138bf4dd743df9a3f26443d` requires the Dreamcast bus to recognize all Area-3 main-RAM mirrors and aliases.
- GREEN head `8b840070fabcbe6132ee2466f33e57b9b47ebd3b`: CI #537 passed the full suite on Linux and Windows/MSVC, including the Windows Release executable artifact.
- The loader now analyzes supported plain GD-ROM boot media before loading, rejects unsupported MIL-CD/unknown encoding, zero-initializes the 16 MiB RAM backing, and exposes all four 16 MiB mirrors across the 64 MiB Area-3 window consistently through physical/P1/P2 aliases.
- No copyrighted game bytes or commercial fingerprints were added; all tests use synthetic program data.

M3.1 completion does **not** mark the installation `native-ready` and does not prove commercial game boot or playability. Steps 5–8 remain required.

## Global constraints
- TDD: no production behavior without a failing test first.
- No copyrighted game data in the repository or CI.
- One end-user executable remains the packaging target.
- Do not fake conversion progress, backend readiness, boot success or playability.
- Keep gameplay simulation deterministic and renderer-independent so rollback and training tooling remain possible.
- Every feature branch must pass Linux portable-core and Windows x64/MSVC CI before merge, followed by a post-merge `main` verification.
