# Next milestones

The reusable architecture roadmap is [`architecture/PRODUCTION-ROADMAP.md`](architecture/PRODUCTION-ROADMAP.md). Production-completion status is machine-checked from [`architecture/PRODUCTION-READINESS.tsv`](architecture/PRODUCTION-READINESS.tsv).

**R2.1 — Repository truth and release gates** is `verified`. Its readiness contract remains the authority for production-completion claims.

**R2.2 — Commercial revision enablement** is `blocked-external-evidence` until legally supplied supported commercial media is available for independently verified fingerprints. No fingerprint or commercial-game data may be guessed to bypass that blocker.

**R2.3 — Game-specific execution and device integration** remains `implemented-unverified`. The Maple runtime package has generic Linux/Windows evidence for chained DMA, controller Device Request/Get Condition, input bridging, completion signaling and boot-harness integration. That evidence does not establish compatibility with the commercial game and therefore does not promote R2.3 to `verified`.

**R2.4 — Real gameplay integration** is `blocked-external-evidence` by the same legally supplied supported commercial image required by R2.2. Real gameplay adapters may not be fabricated from synthetic fixtures.

**R2.5 — Online product modes/M9** remains `implemented-unverified` and is the active non-external track. The direct-session layer now has real nonblocking IPv4 UDP transport plus caller-driven heartbeat/liveness, pinned-peer reconnect, reconnect timeout, gameplay suppression outside `connected`, spoof-resistant liveness accounting and RTT/jitter/loss telemetry. Linux and Windows generic contract evidence exists, but direct transport/reconnect is not production matchmaking and does not complete M9.

Continue R2.5 through independently testable user-facing direct-session/online increments. Casual/ranked service claims, public rooms, invitations, accounts, relay/NAT traversal and related production-service capabilities remain unavailable until their actual infrastructure is explicitly scoped and integrated. Preserve the external-evidence blockers and the single-executable product contract.
