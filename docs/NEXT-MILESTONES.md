# Next milestones

The active guest architecture is PlayStation 1 and the project scope is this JoJo title/revision family only. Current machine-checkable readiness remains in [`architecture/PRODUCTION-READINESS.tsv`](architecture/PRODUCTION-READINESS.tsv).

**R2.1 — Repository truth and release gates** is `verified` for the PS1 architecture-clean baseline. The active build/CTest/workflow contains no Dreamcast/SH-4 guest/backend targets.

**R2.2 — Commercial revision enablement** remains `blocked-external-evidence`. The known whole-image fingerprint is recognized, but the corrected PS1 path still needs one local run against the user's same legally obtained image to record commercial `SYSTEM.CNF` / `PS-X EXE` discovery evidence. This external-evidence gate does not authorize commercial game bytes or a proprietary PlayStation BIOS in Git, CI, artifacts or releases.

**R2.3 — Game-specific execution and device integration** is now `implemented-unverified` at the R3000A reference-CPU layer. Synthetic Linux/Windows contracts cover MIPS decoding, integer semantics and `$zero`, precise exceptions, HI/LO, branch/jump delay slots, aligned memory operations, the R3000A one-instruction load delay, `LWL/LWR/SWL/SWR`, COP0/RFE/interrupt admission, an explicit COP2/GTE boundary, PS-X EXE CPU-state initialization and deterministic replay. The evidence run is `34323411695`.

The immediate engineering milestone is **JoJo-specific PS1 memory/bus + BIOS/HLE**. It must provide the memory surfaces and BIOS/HLE calls needed to place the validated PS-X EXE payload in guest memory and begin reference execution from its entry point. Unsupported addresses and BIOS/HLE calls must remain explicit diagnostic boundaries rather than fabricated success.

GTE execution is not implemented by R2.3; with CU2 disabled COP2 correctly raises CpU/CE=2, and with CU2 enabled it stops at the explicit `cop2_unimplemented` boundary. GPU, DMA, timers/interrupt devices, SPU, CD-ROM streaming and original-game controller integration remain evidence-driven follow-on work.

MIPS CFG/IR and Windows x64 native lowering/cache remain downstream of the reference executor. The reference core is the semantic oracle for those later compiler stages; native x64 code generation is not a current capability and must not be claimed early.

**R2.4 — Real gameplay integration** remains `not-started`. Commercial boot, rendering, audio, input and gameplay each require independent evidence before promotion.

**R2.5 — Online product modes/M9** retains host-side rollback/networking infrastructure only. It is not proof of commercial-game online integration and is lower priority than getting the base PS1 game executing correctly.

**R2.6 — Production validation/release** remains `not-started`. A passing synthetic CPU contract does not make the commercial game bootable or playable.
