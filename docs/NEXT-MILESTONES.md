# Next milestones

The authoritative active roadmap is [`architecture/PRODUCTION-ROADMAP.md`](architecture/PRODUCTION-ROADMAP.md).

The project is now an offline-only native Windows port. Online/networking, Mods and Training are removed from active product scope. Local two-player play, resolution/aspect improvements and a verified real 60 FPS commercial-runtime patch remain required.

Implementation order:

1. Finish the offline-scope cleanup and verify the exact branch head on Linux + Windows CI.
2. **R1 — Media/revision evidence:** validate a legally supplied supported commercial game image and record exact revision evidence without guessed fingerprints.
3. **R2 — Commercial boot/runtime:** drive the real executable through the production SH-4/memory/device/native-backend path and fix each real blocker explicitly.
4. **R3 — Real video + graphics patches:** render the commercial game, wire real resolution/aspect output, then implement and verify the true 60 FPS timing patch. A 60 Hz swap chain or frame duplication does not count.
5. **R4 — Audio + local two-player:** real game audio, Player 1/Player 2 input, two simultaneous controllers, and a real local versus match.
6. **R5-R7 — Original content, persistence and release hardening:** complete offline content, saves/configuration, clean-machine conversion/relaunch and final Windows release evidence.

No later item is marked complete from model-only or mock-only tests.
