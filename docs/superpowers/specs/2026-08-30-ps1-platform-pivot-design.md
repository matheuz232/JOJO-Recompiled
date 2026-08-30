# JOJO Recompiled — PlayStation 1 Platform Pivot Design

## Status

Approved pivot for `feature/offline-product-rebuild` after inspecting the user's supplied `JoJos Bizarre Adventure (USA).zip`.

## Why the pivot is mandatory

The supplied commercial image is a PlayStation 1 BIN/CUE image, not a Dreamcast image.

Observed media facts from the user-supplied copy:

- CUE: one `MODE2/2352` data track.
- ISO9660 system identifier: `PLAYSTATION`.
- `SYSTEM.CNF` boot target: `cdrom:\\SLUS_010.60;1`.
- Boot executable ISO name: `SLUS_010.60;1`.
- Boot executable magic: `PS-X EXE`.
- PS-X EXE initial PC: `0x8001000c`.
- PS-X EXE load address: `0x80010000`.
- PS-X EXE payload size: `0x89800` (563200 bytes), matching the bytes after the 0x800-byte executable header.
- PS-X EXE stack base from the executable header: `0x801ffff0`.
- `SYSTEM.CNF` size: 68 bytes.
- `SYSTEM.CNF` SHA-256: `6865085b75d283ffde7a70957d935b3e1383e66c9d5e8835eeb57c85bcf8a232`.
- `SYSTEM.CNF` FNV-1a64: `0x1eb36f6335bbf54a`.
- `SLUS_010.60;1` size: 565248 bytes.
- `SLUS_010.60;1` SHA-256: `b8b94b23b0d40c25d710d181566d04602be8fbc1bf503560ab9a14513308927d`.
- `SLUS_010.60;1` FNV-1a64: `0xb84be235e572adcc`.

No proprietary game payload bytes may be committed to this repository. Only parser tests using synthetic fixtures and metadata/fingerprints derived from the user's supplied copy may be committed.

## Target revision

The first officially supported product revision is the supplied US PlayStation release identified by boot executable `SLUS_010.60;1`.

A revision is not accepted solely by filename. The production intake must validate the exact registered fingerprint profile. Unknown or modified images must fail explicitly rather than silently falling back.

## Reusable retained components

The following existing components remain relevant and should be preserved unless a later dependency audit proves otherwise:

- BIN/CUE/ISO logical-sector intake;
- ISO9660 filesystem parsing;
- generic revision matching infrastructure;
- conversion manifest and installation plumbing;
- Windows shell/executable;
- D3D11 presentation host;
- resolution/aspect-ratio presentation model;
- local two-player logical input model and Windows controller host;
- settings/persistence utilities that are platform-independent;
- general hashing/version utilities that remain used.

The required real resolution/aspect/60 FPS product gates remain unchanged.

## Superseded active architecture

Dreamcast/SH-4/PVR2 code is historical engineering work for the wrong commercial target and must not be treated as active product architecture for `SLUS_010.60`.

It may remain temporarily in the branch only while the PS1 replacement lands behind explicit build boundaries. It must eventually leave the shipping `JOJO-Recompiled.exe` dependency graph and active release gates.

Specifically superseded for the active target:

- SH-4 decoder/CFG/IR/reference executor;
- SH-4 native x64 backend;
- Dreamcast boot/bootstrap code;
- Dreamcast memory map, bus, ASIC, interrupts and PVR2 model.

## R1 architecture — media/revision intake

R1 is complete only when the normal production conversion path can consume a supported BIN/CUE image, read `SYSTEM.CNF`, resolve the boot path, validate the PS-X EXE header, and match the registered `SLUS_010.60` revision fingerprint without the caller injecting a test-only profile.

Focused components:

- `psx_system_cnf`: strict parser for `BOOT`, `TCB`, `EVENT`, `STACK`, with normalized ISO9660 boot path output.
- `psx_exe`: strict parser for the 0x800-byte PS-X EXE header and payload-size/address validation.
- `psx_revision`: built-in supported profile for `SLUS_010.60` using committed metadata/fingerprints, not game payload bytes.
- `conversion`: default production conversion uses the built-in supported profiles and verifies the PS1 boot executable before writing a successful manifest.

The existing overload that accepts explicit revision profiles may remain for unit-test isolation, but the shipping Windows path must not depend on caller-supplied profiles.

## R2 architecture — commercial boot/runtime

R2 replaces the current SH-4 runtime path with a PlayStation 1 runtime for the supported executable.

The CPU target is the PlayStation MIPS/R3000A-compatible instruction set and its required COP0 behavior. The eventual runtime also requires the PS1 memory map and the devices exercised by the game, including DMA, GPU/VRAM, CD-ROM, SPU, timers/interrupts and GTE/COP2 as execution reaches them.

Unsupported instructions, COP operations and MMIO accesses must fail with actionable diagnostics during development. They may not be skipped as no-ops to create fake boot progress.

R2 will be planned and implemented in independent TDD slices after R1 is green.

## Graphics and 60 FPS preservation

The pivot must not regress the retained host-facing requirements:

- actual selectable output resolution;
- 4:3, 16:9, 16:10, 21:9 and 32:9 presentation without non-uniform stretch;
- real renderer integration for filtering/AA/window/V-Sync where supported;
- real 60 FPS gameplay patch.

The 60 FPS requirement stays explicitly incomplete until the PS1 commercial runtime demonstrates correct simulation/update cadence, gameplay speed, input timing and audio synchronization. A 60 Hz swap chain, duplicated frames or interpolation alone do not satisfy it.

## R1 acceptance

R1 passes only when all of the following are true:

1. synthetic permanent tests prove strict `SYSTEM.CNF` parsing and malformed-input rejection;
2. synthetic permanent tests prove PS-X EXE header parsing, exact payload sizing and malformed-input rejection;
3. the built-in `SLUS_010.60` fingerprint profile is registered in production code;
4. the production `convert_image(source, install_dir, callback)` path uses the built-in profile automatically;
5. the conversion path rejects a filename-correct but fingerprint-wrong image;
6. Linux and Windows CI pass at the exact head;
7. separate local evidence against the user's supplied commercial image confirms the observed boot target and registered fingerprints; and
8. no proprietary bytes are added to Git.

R1 still does not mean the game boots or is playable. Those claims belong to R2+.
