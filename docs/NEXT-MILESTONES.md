# Next milestones

The active guest architecture is PlayStation 1 and the project scope is this JoJo title/revision family only. Current machine-checkable readiness remains in [`architecture/PRODUCTION-READINESS.tsv`](architecture/PRODUCTION-READINESS.tsv).

**R2.1 — Repository truth and release gates** is `verified` for the PS1 architecture-clean baseline. The active build/CTest/workflow no longer contains Dreamcast/SH-4 guest/backend targets.

The immediate evidence milestone is a **new local PS1 M1 run** with the user's same legally obtained image. The expected success boundary is: source fingerprint recognized; PS1 ISO9660 opened; `SYSTEM.CNF` resolved; commercial `PS-X EXE` validated; local generation installed; manifest v2 activated; then stop truthfully at `R3000A/MIPS analysis pending`.

**R2.2 — Commercial revision enablement** remains blocked on that local commercial-image evidence. The known whole-image fingerprint is recognized, but the corrected PS1 path has not yet observed the commercial `PS-X EXE` on the user's real image.

**R2.3 — Game-specific execution and device integration** is not started for the active PS1 architecture. The next engineering milestone after successful M1 evidence is an R3000A/MIPS I reference core built with synthetic TDD: register invariants, little-endian memory semantics, branch delay slots, load delay behavior, HI/LO, and only the exception/COP0/GTE behavior demonstrated to be required by this JoJo.

After the reference semantics are stable, the next compiler milestone is MIPS CFG/IR plus host x64 lowering/cache verification. Native x64 code generation is not an M1 capability and must not be claimed early.

**R2.4 — Real gameplay integration** remains not started. PS1 memory/bus, BIOS HLE, DMA, timers/interrupts, GPU, GTE, SPU, CD-ROM and controller work will be added evidence-first according to accesses actually made by the game. Unsupported operations must produce explicit diagnostics rather than fabricated success.

**R2.5 — Online product modes/M9** retains host-side rollback/networking infrastructure only. It is not proof of commercial-game online integration and is lower priority than getting the base PS1 game executing correctly.

**R2.6 — Production validation/release** remains not started. Boot, rendering, audio, input and gameplay require independent evidence before any playable/release claim.

No milestone may add commercial game bytes or a proprietary PlayStation BIOS to Git, CI, artifacts or releases.
