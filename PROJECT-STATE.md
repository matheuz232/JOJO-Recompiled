# JOJO Recompiled — Project State

## Active development line

- Repository: `matheuz232/JOJO-Recompiled`
- Active branch: `feature/ps1-visible-boot-m3a`
- Active platform: **Sony PlayStation 1**
- Product scope: **JoJo PS1 only**
- M3A code-head commit: `814c956eefd608080320aff270d59568fbb2b28a`
- M3A verification workflow: `34334677057` (run #1339)
- Linux job: passed configure, build, production-readiness gate, PS1 active-architecture gate, CTest, observed-disc revision contract, and UDP transport contract.
- Windows job: passed configure, Release build, production-readiness gate, PS1 active-architecture gate, Release CTest, observed-disc revision contract, UDP transport contract, and artifact upload.
- Windows artifact: `JOJO-Recompiled-Windows-x64`
- Artifact ID: `10097301228`
- Artifact digest: `sha256:ee121eefcdb9876256306641bfc44f6f69050630d2e7240bebc12f40e4965ce6`
- Shipping policy: one `JOJO-Recompiled.exe`.

## PS1 foundation evidence

Verified by repository tests/CI:

- `.iso`, `.bin`, and `.cue` are the active PS1 source formats; `.gdi` is rejected.
- raw MODE1/2352 and MODE2/2352 logical-sector extraction remains supported.
- ISO9660 access is retained.
- strict `SYSTEM.CNF` boot-path parsing is implemented.
- `PS-X EXE` validation/metadata/hash handling is implemented.
- user-selectable installation root and transactional manifest-v2 installation are implemented.
- the observed USA whole-image fingerprint `bin / 666806112 / b8b5dbf79cdb9fcf` is recognized as `jojo-usa-observed-b8b5dbf79cdb9fcf`.
- Dreamcast/SH-4 guest architecture is absent from the active build/test path.

## R3000A reference core — M2

R3000A reference CPU semantics are implemented and verified by synthetic Linux/Windows contracts.
Commercial JoJo boot, rendering, audio, input and gameplay are not verified.

The synthetic M2 contract covers MIPS decoding, integer and HI/LO semantics, precise exceptions, delay-slot control flow, aligned and unaligned memory operations, one-instruction GPR load delay, COP0/RFE/interrupt admission, explicit COP2/GTE boundaries, deterministic PS-X EXE CPU-state initialization and deterministic replay.

The R3000A reference executor is the semantic oracle for later compiler stages; it is not a Windows x64 native recompilation backend.

## JoJo PS1 visible-boot path — M3A

JoJo PS1 M3A memory/bus, PS-X EXE payload loading, and bounded R3000A boot checkpoints are implemented and verified by synthetic Linux/Windows contracts.
Commercial JoJo boot, BIOS/HLE progress, device progress, rendering, audio, input and gameplay are not verified by M3A.

The synthetic M3A contract covers:

- heap-backed 2 MiB PS1 main RAM;
- 1 KiB scratchpad;
- explicit JoJo-required KUSEG/KSEG0/KSEG1 RAM aliases without adding generic PS1 mirrors;
- little-endian 8/16/32-bit bus operations and bounded unsupported-access evidence;
- transactional PS-X EXE payload placement from offset `0x800` into the validated load address;
- exact R3000A `PC`, `next_pc`, `$gp` and `$sp` initialization through the M2 initializer;
- installation-backed checkpoint execution from the validated local `boot.psxexe`;
- explicit instruction-budget exhaustion;
- explicit A0/B0/C0 BIOS-call boundaries;
- explicit unsupported MMIO boundaries;
- bounded derived `Ps1BootReport` diagnostics;
- deterministic replay of two independent M3A runtime instances.

The immutable implementation evidence is GitHub Actions run `34334677057` on code-head `814c956eefd608080320aff270d59568fbb2b28a`, with both Linux and Windows jobs successful.

## Truth boundary

The following are still **not implemented or not verified** at this point:

- commercial JoJo execution from the user's supported local installation;
- PlayStation BIOS/HLE services required by the commercial game;
- GTE execution semantics beyond the explicit COP2 boundary;
- IRQ/timer device behavior required by JoJo;
- DMA device behavior required by JoJo;
- CD-ROM runtime/streaming behavior required by JoJo;
- GPU command processing, VRAM rendering and frame presentation from commercial JoJo execution;
- SPU audio;
- original-game controller integration;
- MIPS CFG/IR production execution;
- Windows x64 native code generation for R3000A;
- commercial boot;
- rendering, audio, input, gameplay or playability.

Synthetic fixtures and CI prove only the repository contracts they exercise. They are not proof that the commercial game boots or is playable.

## Commercial-image evidence still required

The user's previously observed whole-image fingerprint is known, but the corrected PS1 pipeline still needs one local run against the same legally obtained image for commercial `SYSTEM.CNF` / `PS-X EXE` discovery evidence and then an M3A checkpoint run against the resulting supported local installation.

No commercial game bytes, extracted PS-X EXE, proprietary BIOS, raw sectors, or unrestricted guest-memory dumps are to be committed to the repository or CI. Commercial checkpoint evidence must remain bounded and derived.

## Production workstreams

Canonical status remains in `docs/architecture/PRODUCTION-READINESS.tsv`.

- R2.1: repository truth/release gates — verified.
- R2.2: commercial revision enablement — blocked on local commercial-image discovery evidence.
- R2.3: JoJo-specific reference execution checkpoint — `implemented-unverified`, evidenced by GitHub Actions run `34334677057`; blocker is `jojo-bios-hle-and-device-runtime-not-implemented`.
- R2.4: real gameplay integration — not started.
- R2.5: host-side online/rollback infrastructure exists but is not integrated with the commercial JoJo PS1 game.
- R2.6: production validation/release — not started.

## Next priority

M3B — JoJo-observed BIOS/HLE: run the supported local JoJo installation through the M3A checkpoint, capture only bounded derived diagnostics at the first A0/B0/C0 or kernel boundary, reproduce the required contract synthetically, and implement only the JoJo-required service.

After each JoJo-observed boundary, add the smallest synthetic RED→GREEN contract needed to progress toward the first visible commercial frame. IRQ/timers, DMA, CD-ROM, GPU and GTE subsets are added only when the JoJo execution path proves they are required.

Keep MIPS CFG/IR and Windows x64 lowering downstream of visible boot. This project has no compatibility target for unrelated games.

Historical Dreamcast/SH-4 plans/specs under `docs/superpowers/` remain project history only.
