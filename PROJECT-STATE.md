# JOJO Recompiled — Project State

## Active development line

- Repository: `matheuz232/JOJO-Recompiled`
- Active branch: `feature/r2-3-sleep-boundary`
- `main`: unchanged; do not update without explicit user authorization.
- Shipping artifact policy: keep a single `JOJO-Recompiled.exe`.
- Mods: deferred until the base game reaches 100%.

## Roadmap status

- R2.2: `blocked-external-evidence`
- R2.3 Maple Runtime: `implemented-unverified`
- Priority after R2.3: `R2.4 -> R2.5 -> R2.6 -> 100% -> mods`

`implemented-unverified` is intentional. Internal tests and CI do not substitute for legal commercial-runtime evidence, and no commercial fingerprint, disc identity, or game-data claim is fabricated by this repository checkpoint.

## R2.3 Maple Runtime checkpoint

Implementation commit: `146c75c21f2f4e008c25e924f3abcdd3d5f1aa99`

Targeted TDD evidence:

- RED commit: `0bd4bb78afd0dd382db2a548b98273ed3ec33dfd`
- RED workflow run: `33712171601`
- GREEN workflow run: `33712381545`
- Targeted targets: `jojo_dreamcast_maple_tests`, `jojo_dreamcast_boot_runner_tests`

Implemented scope:

- consecutive multi-entry/chained Maple DMA tables;
- full-chain validation before receive-buffer mutation;
- atomic failure behavior with staged writes and defensive rollback;
- checked command-table address arithmetic and mapped-span validation;
- 256-entry runtime safety bound for unterminated/hostile tables;
- controller endpoint on Maple ports A/B; ports C/D report no device;
- Device Request (`0x01`) -> Device Info (`0x05`);
- Get Condition (`0x09`) for controller function (`0x00000001`) -> Data Transfer (`0x08`);
- standard controller capability word and protocol-valid 28-word device-info payload using neutral JOJO Recompiled identity strings;
- active-low Dreamcast button encoding;
- JoJo action bridge: Light->X, Medium->Y, Heavy->B, Stand->A, Start/Pause->Start, directional actions->D-pad and digitalized left analog axes;
- Maple DMA completion event raised only after every validated response is committed;
- boot reference runner accepts `ResolvedInputFrame` and passes it to Maple;
- negative coverage for disabled DMA, hardware-trigger mode, malformed frame length, misaligned receive buffer, mapped-span overflow, invalid chained entry, and unterminated table behavior.

Explicitly not part of R2.3:

- VMU;
- VBlank-triggered Maple DMA;
- cycle-accurate Maple timing;
- invented commercial-game behavior or fingerprints.

## Working protocol

- Prefer large coherent packages over microbranches/microcommits.
- Use targeted TDD while developing.
- Run full Linux/Windows/readiness/artifact CI only at package checkpoints.
- Fix P0/P1 first; defer P2/P3 and cosmetic refactors.
- Do not infer commercial verification from synthetic or CI-only evidence.
