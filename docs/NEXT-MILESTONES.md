# Next milestones

The active guest architecture is PlayStation 1 and the product scope is **JoJo PS1 only**. Current machine-checkable readiness remains in [`architecture/PRODUCTION-READINESS.tsv`](architecture/PRODUCTION-READINESS.tsv).

**R2.1 — Repository truth and release gates** is `verified` for the PS1 architecture-clean baseline. The active build/CTest/workflow contains no Dreamcast/SH-4 guest/backend targets.

**R2.2 — Commercial revision enablement** remains `blocked-external-evidence`. The known whole-image fingerprint is recognized, but the corrected PS1 path still needs one local run against the user's same legally obtained image to record commercial `SYSTEM.CNF` / `PS-X EXE` discovery evidence. This external-evidence gate does not authorize commercial game bytes or a proprietary PlayStation BIOS in Git, CI, artifacts or releases.

**R2.3 — Game-specific execution and device integration** is `implemented-unverified` at the M3A checkpoint layer. Synthetic Linux/Windows contracts now cover the M2 R3000A semantics plus JoJo-specific 2 MiB PS1 RAM, 1 KiB scratchpad, explicit supported aliases, transactional PS-X EXE payload placement, installation-backed bounded reference execution, deterministic replay, A0/B0/C0 boundary recognition, MMIO-boundary reporting and instruction-budget termination. The immutable M3A code-head evidence run is `34334677057`.

M3B — JoJo-observed BIOS/HLE: run the supported local JoJo installation through the M3A checkpoint, capture only bounded derived diagnostics at the first A0/B0/C0 or kernel boundary, reproduce the required contract synthetically, and implement only the JoJo-required service.

Unsupported BIOS/HLE calls and not-yet-implemented PS1 device accesses must remain explicit diagnostic boundaries rather than fabricated success. The implementation remains evidence-driven and JoJo-only; there is no compatibility target for unrelated games.

GTE execution beyond the explicit COP2 boundary, IRQ/timers, DMA, CD-ROM runtime behavior, GPU rendering, SPU audio and original-game controller integration remain follow-on work only when the JoJo boot path demonstrates that they are required.

MIPS CFG/IR and Windows x64 native lowering/cache remain downstream of visible boot. The R3000A reference executor remains the semantic oracle; native x64 code generation is not a current capability and must not be claimed early.

**R2.4 — Real gameplay integration** remains `not-started`. Commercial boot, BIOS/HLE progress, device progress, rendering, audio, input and gameplay each require independent evidence before promotion.

**R2.5 — Online product modes/M9** retains host-side rollback/networking infrastructure only. It is not proof of commercial-game online integration and is lower priority than getting the base JoJo PS1 game executing correctly.

**R2.6 — Production validation/release** remains `not-started`. Passing synthetic M3A contracts does not make the commercial game bootable or playable.
