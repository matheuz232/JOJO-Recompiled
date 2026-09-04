# Next milestones

The reusable architecture roadmap is [`architecture/PRODUCTION-ROADMAP.md`](architecture/PRODUCTION-ROADMAP.md). Production-completion status is machine-checked from [`architecture/PRODUCTION-READINESS.tsv`](architecture/PRODUCTION-READINESS.tsv).

**R2.1 — Repository truth and release gates** is `verified`. Its readiness contract remains the authority for production-completion claims.

**R2.2 — Commercial revision enablement** is `blocked-external-evidence` until legally supplied supported commercial media is available for independently verified fingerprints. No fingerprint or commercial-game data may be guessed to bypass that blocker.

**R2.3 — Game-specific execution and device integration** remains `implemented-unverified`. The Maple runtime package has generic Linux/Windows evidence for chained DMA, controller Device Request/Get Condition, input bridging, completion signaling and boot-harness integration. That evidence does not establish compatibility with the commercial game and therefore does not promote R2.3 to `verified`.

**R2.4 — Real gameplay integration** is `blocked-external-evidence` by the same legally supplied supported commercial image required by R2.2. Real gameplay adapters may not be fabricated from synthetic fixtures.

**R2.5 — Online product modes/M9** remains `implemented-unverified` and is the active non-external track. The direct-session transport provides real nonblocking IPv4 UDP, caller-driven heartbeat/liveness, pinned-peer reconnect, reconnect timeout, gameplay suppression outside `connected`, spoof-resistant liveness accounting and RTT/jitter/loss telemetry. The product-facing `OnlineSessionController` now adds strict direct-endpoint parsing, Host/Join orchestration, stable waiting/connecting/connected/reconnecting/disconnected/faulted view state, local/remote endpoint presentation, gameplay gating, telemetry exposure, explicit disconnect/reset and operational-fault persistence. Generic Linux/Windows CI evidence exists, but this does not establish commercial-game online compatibility and does not complete M9.

The next R2.5 increment is **in-game Online presentation/integration around the direct-session controller**: expose direct Host/Join, connection/reconnect/disconnect status and telemetry through the product flow, then bridge the established session to the real gameplay/rollback loop when game-specific integration evidence is available. Do not place production-online claims on the launcher merely to make the feature visible.

Casual/ranked service claims, matchmaking, public rooms, invitations, accounts, relay/NAT traversal and related production-service capabilities remain unavailable until their actual infrastructure is explicitly scoped and integrated. Preserve the external-evidence blockers and the single-executable product contract.
