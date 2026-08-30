# PS1 R1 Media / Revision Verification

## Exact verified head

- Branch: `feature/offline-product-rebuild`
- Commit: `9808bc94b6b33d9e89380c83b9b8f92b98cfeb55`
- GitHub Actions run: `33329982124`
- Linux job `99306699783`: PASS
- Windows job `99306699911`: PASS
- Linux CTest: 49/49 passed
- Windows CTest: 51/51 passed

## CI artifacts

### R1 media probe

- Artifact: `JOJO-R1-Media-Probe-Linux`
- Artifact ID: `9737355255`
- ZIP SHA-256: `d64dcf2ad0171c376562dfda2ce4d612d1ae1b564eb7cacc228b53fd948a53b2`

The probe is built from the same core functions used by production intake: ISO9660 open/read, PS1 boot analysis, PS-X EXE parsing and registered revision matching. It does not dump or package game payload bytes.

### Windows application

- Artifact: `JOJO-Recompiled-Windows-x64`
- Artifact ID: `9737375525`
- ZIP SHA-256: `e018cd9485a5aca878839ea49338559f3253a7a5367ec4fc2d8ef92b58040687`

## User-supplied commercial-media proof

The exact Linux probe artifact from run `33329982124` was executed locally against the user's legally supplied `JoJos Bizarre Adventure (USA).zip`, using its BIN/CUE media without committing any commercial payload to Git.

Probe exit code: `0`

Observed non-proprietary metadata:

```text
revision=ps1-usa-slus-01060
boot=/SLUS_010.60
pc=0x8001000c
load=0x80010000
payload=0x89800
stack=0x801ffff0
```

This matches the registered production revision profile and the previously measured executable/header metadata.

## What R1 proves

R1 now has end-to-end evidence that the active production intake can:

1. open the supported PS1 BIN/CUE media;
2. parse the ISO9660 filesystem;
3. parse `SYSTEM.CNF` and follow its boot path rather than guessing a filename;
4. validate the `PS-X EXE` header and payload contract;
5. match the exact registered `ps1-usa-slus-01060` fingerprints;
6. reject filename-correct but fingerprint-wrong synthetic media; and
7. do all of this without storing proprietary game payload bytes in the repository.

## Explicitly not proven by R1

R1 does **not** prove that the commercial game executes or is playable. It does not prove:

- R3000A/MIPS instruction execution;
- COP0/exceptions/interrupts;
- PS1 RAM/MMIO/DMA/timers;
- GPU/VRAM rendering;
- CD-ROM runtime commands;
- SPU/audio;
- GTE/COP2;
- real local Player 1/Player 2 integration into commercial gameplay;
- saves/memory card;
- the required real 60 FPS gameplay patch;
- overall product readiness or `100%` completion.

Those remain R2+ gates and must be proven through the commercial runtime.

## Known legacy warnings

The exact verified build still compiles temporarily retained superseded Dreamcast/SH-4 code and therefore still reports known compiler warnings in that historical path. R1 does not classify those warnings as fixed. The superseded code must leave the active shipping dependency graph as the PS1 replacement becomes complete.
