# JOJO Recompiled — Project State

## Integrated line

- Repository: `matheuz232/JOJO-Recompiled`
- Current integrated branch: `main`
- Current `main` HEAD: `0c76a7c089e9d4a0a09c4f98c850cedc20180b30`
- R2.5 merge commit: `e13f7fc541315b7ab0ae1dc3d0e1c388db368700`
- Post-merge `main` CI: `33838410540` — Linux and Windows passed build, readiness, tests and the R2.5 UDP contract.
- Shipping policy: one `JOJO-Recompiled.exe`.
- Mods remain deferred until the base game reaches 100%.

## Production workstreams

- R2.1: `verified`
- R2.2: `blocked-external-evidence` — requires a legally supplied supported commercial image.
- R2.3: `implemented-unverified` — generic execution/device integration exists; commercial compatibility is not proven.
- R2.4: `blocked-external-evidence` — real gameplay integration requires the same legal commercial evidence.
- R2.5: `implemented-unverified` — direct UDP transport plus reconnect/liveness/telemetry are integrated into `main`; the product-facing direct-session controller is implemented on its feature branch; M9 is not complete.
- R2.6: `not-started`

## R2.5 reconnect checkpoint

- RED commit: `bb1614c52114b37e3181cf5e805b3ff840c49978`
- RED workflow: `33837212025`
- Implementation commit: `4e7460fa04c7c40b44bc88e87cfd6b1eff6e01cc`
- Feature GREEN workflow: `33837379074`
- Merge-validation workflow: `33838201969`
- Post-merge `main` workflow: `33838410540`

Implemented scope includes configurable heartbeat/liveness/reconnect timing, pinned-peer liveness accounting, `connected -> reconnecting -> connected` recovery, terminal reconnect timeout, gameplay suppression while reconnecting, spoof rejection before liveness refresh, and RTT/jitter/loss telemetry integration.

## R2.5 online controller checkpoint

- Branch: `feature/r2-5-online-controller`
- Design: `docs/superpowers/specs/2026-09-04-r2-5-online-controller-design.md`
- Plan: `docs/superpowers/plans/2026-09-04-r2-5-online-controller.md`
- Functional/test checkpoint: `2a39d9cd51e62b7a53ee9fef6754df30b25510c8`
- GREEN workflow: `33842642660`
- Windows artifact: `JOJO-Recompiled-Windows-x64`
- Artifact ID: `9925476699`
- Artifact digest: `sha256:06ccafe6611df7bb55cb1c7094e0304f724537fa95b511ae08f816cc60ab346e`

Implemented controller scope includes strict IPv4 `A.B.C.D:PORT` parsing, host/join lifecycle, waiting/connecting/connected/reconnecting/disconnected/faulted product states, local/remote endpoint presentation, gameplay gating, RTT/jitter/loss/counter telemetry, reconnect recovery/timeout mapping, explicit disconnect, reset, spoof-resistant liveness behavior, and operational-fault persistence.

## Truth boundary

No internal test, synthetic fixture or CI artifact is treated as proof that the commercial game is playable or that commercial online behavior is compatible. These R2.5 increments do not claim production matchmaking, ranked services, NAT traversal, relay service, accounts, encryption, public rooms, invitations, profiles/history/replays, game-specific online integration or complete M9 UI.

## Next priority

Finish the R2.5 product path by presenting and integrating the direct Host/Join session flow in-game around the controller, without inventing service infrastructure. Then continue to R2.6 production validation. Resume R2.2/R2.4 immediately when legally supplied supported commercial evidence is available.
