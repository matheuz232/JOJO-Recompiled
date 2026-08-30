# M8 Rollback Networking Core Verification

## Scope closed

M8 implements the reusable portable rollback/networking-core contract defined by `docs/superpowers/specs/2026-08-30-rollback-networking-core-design.md`:

- deterministic packed local/remote frame inputs;
- snapshot capture/restore around simulated frames;
- latest-known remote-input prediction;
- late-input rollback and deterministic re-simulation;
- suppression of side effects during every replayed frame;
- bounded rollback window with rejection of too-old corrections before mutation;
- SHA-256 state hashes and earliest-frame desync reporting;
- fixed integer-only deterministic RNG with restorable state;
- versioned deterministic datagram serialization suitable for UDP;
- unreliable input/ping/pong traffic and reliable session/control traffic only;
- acknowledgement and caller-time-driven control retransmission bookkeeping;
- RTT, jitter, packet loss, prediction, rollback-depth and connection-state telemetry.

## TDD evidence

- RED run `33316000172` at `4b1faa32cd90848d950bef0d12ca64c04e6ba552`: the Linux `g++ -fsyntax-only` check of the permanent `tests/test_rollback.cpp` contract failed exactly with `fatal error: core/rollback.h: No such file or directory`. This was the intended missing-feature failure, not an environment or syntax failure.
- GREEN build run `33316203649` at `0c156c64712b998e26852cb33a1f151a5b436011`: Portable core / Linux configured, built and passed CTest; Windows x64 / MSVC 2022 configured, built Release, passed CTest and uploaded the single executable artifact.
- GREEN Windows artifact: `JOJO-Recompiled-Windows-x64`, artifact `9733546296`, digest `sha256:24fe40f45044ee95e35495a7f6264800713e53d0c599f056981bdb85d1d875ea`.
- The temporary `.github/workflows/m8-red.yml` used only for RED proof was removed at commit `8378ab054b825ccdcd00ac35cbde268dbeff113c`; the normal build workflow remains the permanent CI path.

## Permanent test coverage

`tests/test_rollback.cpp` proves:

1. identical RNG seed produces identical output and saved RNG state restores the exact sequence;
2. missing remote input is predicted and counted;
3. a differing late remote input restores the correct frame snapshot and reaches the same state/hash as an authoritative run;
4. rollback replay calls the simulation with side effects disabled;
5. multi-frame rollback depth is measured and corrections older than the configured window are rejected without state/frame mutation;
6. local state hashes are deterministic and a mismatching remote hash reports desync;
7. every packet kind serializes deterministically and round-trips;
8. bad magic/version/kind/reserved fields, truncation and oversized payloads are rejected;
9. only session hello/accept/disconnect may enter the reliable retransmission queue, with duplicate protection, retry timing and acknowledgement removal;
10. RTT, jitter, sent/received/lost counters, packet-loss percentage, prediction count, rollback depth and connection lifecycle are represented deterministically.

## Readiness boundary

M8 completion closes the reusable **rollback/networking core**. The packet layer is intentionally UDP-oriented and platform-socket independent: a host can send/receive the serialized datagrams through UDP without coupling socket APIs to gameplay simulation. M8 does not claim that public matchmaking, account services, relay/NAT traversal, encryption/key exchange, production socket threading or a commercial revision's complete gameplay-state adapter are already live. Those are product/integration responsibilities beyond this core milestone.

A final branch CI and a fresh post-merge `main` CI are still mandatory gates before M8 may be reported complete.