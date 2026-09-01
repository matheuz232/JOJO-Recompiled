# Next milestones

The authoritative active roadmap is [`architecture/PRODUCTION-ROADMAP.md`](architecture/PRODUCTION-ROADMAP.md).

The project is now an offline-only native Windows port of the PlayStation version. Online/networking, Mods and Training are removed from active product scope. Local two-player play, resolution/aspect improvements and a verified real 60 FPS commercial-runtime patch remain required.

Implementation order:

1. Keep the active branch truthful and green on Linux + Windows CI while removing stale assumptions from the previous Dreamcast/SH-4 direction.
2. **R1 — Media/revision evidence:** validate a legally supplied supported commercial PS1 game image, record exact revision evidence without guessed fingerprints, and preserve the validated runtime data locally (`SYSTEM.CNF`, `PSX.EXE`, normalized `DISC.ISO`).
3. **R2 — Commercial boot/runtime:** drive the real PS-X executable through the R3000A/GTE/BIOS/MMIO runtime and fix each observed CPU, GPU, CD-ROM, SIO, SPU, DMA, timer or interrupt blocker explicitly.
4. **R3 — Real video + graphics patches:** render the commercial PS1 game, wire real resolution/aspect output, then implement and verify the true 60 FPS timing patch. A 60 Hz swap chain or frame duplication does not count.
5. **R4 — Audio + local two-player:** connect the existing two-player host input to PS1 SIO/controllers, implement real SPU/audio output, support two simultaneous controllers/mixed keyboard-controller configurations, and prove a real local versus match.
6. **R5-R7 — Original content, persistence and release hardening:** complete offline content, memory-card/save behavior, host configuration, clean-machine conversion/relaunch and final Windows release evidence.

No later item is marked complete from model-only, synthetic-only or CI-only tests.
