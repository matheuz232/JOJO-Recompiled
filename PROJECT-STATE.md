# JOJO Recompiled — Project State

## Active development line

- Repository: `matheuz232/JOJO-Recompiled`
- Active branch: `feature/r2-5-online-transport`
- `main`: unchanged; do not update without explicit user authorization.
- Shipping artifact policy: keep a single `JOJO-Recompiled.exe`.
- Mods: deferred until the base game reaches 100%.

## Roadmap status

- R2.2: `blocked-external-evidence` — legally supplied supported commercial image required.
- R2.3: `implemented-unverified` — generic device/execution implementation exists; commercial compatibility is not proven.
- R2.4: `blocked-external-evidence` — real gameplay integration requires the same legal commercial evidence.
- R2.5: `implemented-unverified` — direct UDP session transport plus reconnect/liveness/telemetry increments exist; M9 is not complete.
- R2.6: `not-started`.
- Priority: `R2.5 -> R2.6 -> 100% -> mods`, with R2.2/R2.4 resumed immediately when legal commercial evidence is available.

No internal test, synthetic fixture or CI artifact is treated as proof that the commercial game is playable. No commercial fingerprint, disc identity or proprietary game data is fabricated.

## R2.3 Maple Runtime checkpoint

- Implementation commit: `146c75c21f2f4e008c25e924f3abcdd3d5f1aa99`
- Package checkpoint: `d0f6532b2ee176d133b7700fc676b92b0f48600b`
- RED workflow: `33712171601`
- Targeted GREEN workflow: `33712381545`
- Full Linux/Windows/readiness/artifact workflow: `33712464917`
- Windows artifact: one `JOJO-Recompiled-Windows-x64` artifact containing the shipping `JOJO-Recompiled.exe` output.

Implemented generic scope includes chained Maple DMA, full-chain validation/atomicity, controller Device Request/Get Condition, two controller ports plus explicit no-device ports, existing-input bridging, completion IRQ/event behavior, boot-harness integration and essential malformed-table negatives. VMU, VBlank-triggered DMA, cycle-accurate Maple timing and invented commercial behavior remain outside that package.

## R2.5 direct UDP transport checkpoint

- Branch base: R2.3 checkpoint `d0f6532b2ee176d133b7700fc676b92b0f48600b`.
- RED commit: `8d9b978f9336ce0a9545db438a31111d3d8352ad`.
- RED workflow: `33712848574` — standalone contract compiled and failed because `core/network_transport.h` was absent.
- Implementation commit: `7ca8dff3fb0a24f13d6be12b6051ee3a7c77bd0d`.
- Targeted GREEN workflow: `33712996387`.
- Windows CI environment fix: `921e77108adeaea931b3620e9c82a4e161c5b1b7`.
- Full Linux/Windows/readiness/artifact workflow after CI fix: `33835799076`.

Implemented increment:

- real IPv4 UDP sockets on loopback/host endpoints;
- nonblocking receive semantics;
- bounded 1200-byte transport datagrams, sufficient for the existing <=1066-byte rollback packet contract;
- exact datagram source preservation;
- `NetworkPacket` serialization/parsing over UDP;
- malformed packet rejection;
- caller-driven direct host/client handshake using existing `session_hello`/`session_accept` control packets;
- existing `ControlReliabilityQueue` used for hello/accept retransmission;
- connected-peer pinning so datagrams from another endpoint cannot force disconnect or enter gameplay delivery;
- input/gameplay packet delivery remains data-only and does not own gameplay state;
- caller-driven ping/pong acknowledgement and RTT telemetry;
- explicit disconnect state propagation;
- RAII socket lifecycle with POSIX sockets and Winsock implementation selected at compile time.

## R2.5 reconnect/liveness/telemetry checkpoint

- Design: `docs/superpowers/specs/2026-09-04-r2-5-reconnect-telemetry-design.md`.
- Plan: `docs/superpowers/plans/2026-09-04-r2-5-reconnect-telemetry.md`.
- RED commit: `bb1614c52114b37e3181cf5e805b3ff840c49978`.
- RED workflow: `33837212025` — repository build/readiness/CTest remained green while the standalone R2.5 contract failed on the intentionally missing reconnect API/state.
- Implementation commit: `4e7460fa04c7c40b44bc88e87cfd6b1eff6e01cc`.
- Full Linux/Windows/readiness/artifact GREEN workflow: `33837379074`.
- Windows artifact: `JOJO-Recompiled-Windows-x64`, digest `sha256:df3c8a7b66e894aa7566a02b2fc3ba2d2dd4ea5f152aea7c7ba8b801ed148704`.

Implemented increment:

- explicit `DirectSessionTiming` with defaults of 100 ms control retry, 500 ms heartbeat, 2000 ms liveness timeout and 5000 ms reconnect timeout;
- invalid zero heartbeat/liveness/reconnect timings rejected, and liveness shorter than heartbeat rejected;
- caller-time-driven heartbeat ping/pong and RTT/jitter telemetry;
- valid traffic from the pinned peer refreshes liveness while traffic from other endpoints is ignored before liveness accounting;
- silence transitions `connected -> reconnecting` and records heartbeat-loss telemetry;
- gameplay/data send and delivery remain gated to `connected`;
- client reconnect reuses `session_hello`/`session_accept` only with the existing pinned endpoint;
- host reconnect acceptance remains pinned to the already selected peer;
- successful same-peer handshake restores `connected` without changing gameplay ownership;
- reconnect deadline transitions terminally to `disconnected` and late queued traffic cannot auto-revive the session;
- explicit peer/local disconnect remains terminal.

These R2.5 increments do **not** claim production matchmaking, ranked services, NAT traversal, relay service, accounts, encryption, public rooms, invitations, profiles/history/replays, game-specific online integration or complete M9 UI.

## Working protocol

- Prefer large coherent packages over microbranches/microcommits.
- Use targeted TDD while developing.
- Run full Linux/Windows/readiness/artifact CI only at package checkpoints.
- Fix P0/P1 first; defer P2/P3 and cosmetic refactors.
- Do not infer commercial verification from synthetic or CI-only evidence.
- Do not start mod development until the base game reaches 100%.
