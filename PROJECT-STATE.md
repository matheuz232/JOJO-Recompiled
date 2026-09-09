# JOJO Recompiled — Project State

## Active development line

- Repository: `matheuz232/JOJO-Recompiled`
- Active branch: `feature/ps1-foundation-m1`
- Active platform: **Sony PlayStation 1**
- Product scope: **this JoJo title/revision family only**
- PS1 architecture-clean commit: `c4a358b26755a7a32bdf4b2cf28d8aa941dc2568`
- Verification workflow: `34314593101` (run #1287)
- Linux job: passed configure, build, production-readiness gate, PS1 active-architecture gate, CTest, observed-disc revision contract, and UDP transport contract.
- Windows job: passed configure, Release build, production-readiness gate, PS1 active-architecture gate, Release CTest, observed-disc revision contract, UDP transport contract, and artifact upload.
- Windows artifact: `JOJO-Recompiled-Windows-x64`
- Artifact ID: `10089639003`
- Artifact digest: `sha256:5e93b51a671bcbccdbfc7e32baca3ff9ac0c3c09192447ca5666753b15b05721`
- Shipping policy: one `JOJO-Recompiled.exe`.

## PS1 M1 evidence

Verified by repository tests/CI:

- `.iso`, `.bin`, and `.cue` are the active PS1 source formats; `.gdi` is rejected.
- raw MODE1/2352 and MODE2/2352 logical-sector extraction remains supported.
- ISO9660 access is retained.
- strict `SYSTEM.CNF` boot-path parsing is implemented.
- `PS-X EXE` validation/metadata/hash handling is implemented.
- user-selectable installation root is implemented.
- transactional generation staging/activation and manifest v2 handling are implemented.
- the observed USA whole-image fingerprint `bin / 666806112 / b8b5dbf79cdb9fcf` is recognized as `jojo-usa-observed-b8b5dbf79cdb9fcf`.
- Dreamcast/SH-4 guest sources, old guest backend files, old guest tests, and old native-backend workflow contracts are absent from the active architecture.

## Truth boundary

The following are **not** verified or implemented by M1:

- R3000A/MIPS instruction execution;
- MIPS CFG/IR execution;
- native x64 code generation for R3000A;
- PlayStation BIOS/HLE service coverage for the commercial game;
- commercial PS-X EXE discovery on the user's real image after the architecture migration;
- commercial boot;
- GPU rendering;
- SPU audio;
- original-game controller integration;
- gameplay.

Synthetic fixtures and CI prove only the repository contracts they exercise. They are not proof that the commercial game is playable.

## Commercial-image evidence still required

The user's previously observed whole-image fingerprint is known, but the corrected PS1 M1 pipeline still needs one new local run against the same legally obtained image. The success boundary for that run is:

```text
source recognized
PS1 filesystem opened
SYSTEM.CNF resolved
PS-X EXE validated
local generation installed
manifest v2 activated
R3000A/MIPS analysis pending
```

No commercial game bytes, extracted PS-X EXE, or proprietary BIOS are to be committed to the repository or CI.

## Production workstreams

Canonical status remains in `docs/architecture/PRODUCTION-READINESS.tsv`.

- R2.1: repository truth/release gates — verified by the PS1 architecture-clean CI baseline.
- R2.2: commercial revision enablement — blocked on a new local commercial-image M1 run for PS-X EXE discovery evidence.
- R2.3: game-specific execution/device integration — not started for PS1 because R3000A execution is not implemented yet.
- R2.4: real gameplay integration — not started.
- R2.5: host-side online/rollback infrastructure exists but is not integrated with the commercial PS1 game.
- R2.6: production validation/release — not started.

## Next priority

1. Run the new Windows M1 artifact locally with the user's same legally obtained PS1 BIN/CUE and capture only derived logs/metadata.
2. If `SYSTEM.CNF` and the commercial `PS-X EXE` validate, use that evidence to define the exact R3000A/MIPS instruction and runtime requirements.
3. Begin the R3000A reference-execution milestone with synthetic TDD before any native x64 codegen claim.

Historical Dreamcast/SH-4 plans/specs under `docs/superpowers/` remain only as project history.
