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
- R2.5: `implemented-unverified` — direct UDP/session transport increment exists; M9 is not complete.
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

This increment does **not** claim production matchmaking, ranked services, NAT traversal, relay service, accounts, encryption, public rooms, invitations, profiles/history/replays or complete M9 UI.

## Working protocol

- Prefer large coherent packages over microbranches/microcommits.
- Use targeted TDD while developing.
- Run full Linux/Windows/readiness/artifact CI only at package checkpoints.
- Fix P0/P1 first; defer P2/P3 and cosmetic refactors.
- Do not infer commercial verification from synthetic or CI-only evidence.
- Do not start mod development until the base game reaches 100%.
