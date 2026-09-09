# JOJO Recompiled — Project State

## Active development line

- Repository: `matheuz232/JOJO-Recompiled`
- Active branch: `feature/r3000a-reference-core-m2`
- Active platform: **Sony PlayStation 1**
- Product scope: **this JoJo title/revision family only**
- R3000A M2 code-head commit: `24f08c2e364be13a16ff756ea69d3d42e734cfa7`
- Verification workflow: `34323411695` (run #1325)
- Linux job: passed configure, build, production-readiness gate, PS1 active-architecture gate, CTest, observed-disc revision contract, and UDP transport contract.
- Windows job: passed configure, Release build, production-readiness gate, PS1 active-architecture gate, Release CTest, observed-disc revision contract, UDP transport contract, and artifact upload.
- Windows artifact: `JOJO-Recompiled-Windows-x64`
- Artifact ID: `10092837844`
- Artifact digest: `sha256:7a2d111b1ecf090208eb0aced051f948cf7663a608af6c809cd9da3f22b91a6a`
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

The synthetic M2 contract covers:

- MIPS decoder coverage used by the reference executor;
- integer/shift/immediate semantics and the `$zero` invariant;
- trapping ADD/ADDI/SUB and precise exception entry;
- SYSCALL, BREAK, RI, AdEL/AdES, IBE/DBE and BEV vector selection;
- HI/LO multiply/divide edge behavior;
- J/JAL/JR/JALR and conditional branches with one architectural delay slot;
- delay-slot exception BD/BT/EPC/TAR context;
- LB/LBU/LH/LHU/LW and SB/SH/SW;
- the R3000A one-instruction GPR load delay;
- `LWL/LWR/SWL/SWR` lane semantics and merge-load forwarding;
- COP0 MFC0/MTC0 masks, RFE and interrupt admission;
- COP2/GTE gating: CU2 clear -> CpU with CE=2; CU2 set -> explicit `cop2_unimplemented` boundary;
- deterministic PS-X EXE CPU-state initialization for PC/next-PC/GP/SP;
- deterministic replay from cloned synthetic CPU/bus state.

The R3000A reference executor is a semantic oracle. It is not yet a complete PS1 runtime and is not a native x64 recompilation backend.

## Truth boundary

The following are still **not implemented or not verified** at this point:

- JoJo-specific PS1 RAM/memory-map/bus integration sufficient to run the commercial executable;
- PlayStation BIOS/HLE services required by the commercial game;
- GTE execution semantics beyond the explicit COP2 boundary;
- GPU rendering;
- DMA/timer/device integration required by the game;
- SPU audio;
- CD-ROM runtime/streaming behavior;
- original-game controller integration;
- MIPS CFG/IR production execution;
- Windows x64 native code generation for R3000A;
- commercial PS-X EXE execution from the user's real image;
- commercial boot;
- rendering, audio, input, gameplay or playability.

Synthetic fixtures and CI prove only the repository contracts they exercise. They are not proof that the commercial game boots or is playable.

## Commercial-image evidence still required

The user's previously observed whole-image fingerprint is known, but the corrected PS1 pipeline still needs one new local run against the same legally obtained image for commercial `SYSTEM.CNF` / `PS-X EXE` discovery evidence. The success boundary for that run remains:

```text
source recognized
PS1 filesystem opened
SYSTEM.CNF resolved
PS-X EXE validated
local generation installed
manifest v2 activated
```

No commercial game bytes, extracted PS-X EXE, or proprietary BIOS are to be committed to the repository or CI.

## Production workstreams

Canonical status remains in `docs/architecture/PRODUCTION-READINESS.tsv`.

- R2.1: repository truth/release gates — verified.
- R2.2: commercial revision enablement — blocked on local commercial-image discovery evidence.
- R2.3: R3000A reference CPU layer — `implemented-unverified`, evidenced by GitHub Actions run `34323411695`; blocker is `ps1-memory-bus-bios-hle-not-implemented`.
- R2.4: real gameplay integration — not started.
- R2.5: host-side online/rollback infrastructure exists but is not integrated with the commercial PS1 game.
- R2.6: production validation/release — not started.

## Next priority

1. Implement the **JoJo-specific PS1 memory/bus** required to place the validated PS-X EXE payload and service R3000A accesses truthfully.
2. Add only the **BIOS/HLE services observed to be required by JoJo**, with explicit boundaries for unknown calls.
3. Reach the first reference-execution checkpoint from the commercial PS-X EXE entry point using local user-owned data only.
4. Keep MIPS CFG/IR and Windows x64 lowering downstream of the verified reference executor.

Historical Dreamcast/SH-4 plans/specs under `docs/superpowers/` remain project history only.
