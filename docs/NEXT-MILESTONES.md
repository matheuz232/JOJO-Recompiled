# Next milestones

The reusable architecture roadmap is [`architecture/PRODUCTION-ROADMAP.md`](architecture/PRODUCTION-ROADMAP.md). Production-completion status is machine-checked from [`architecture/PRODUCTION-READINESS.tsv`](architecture/PRODUCTION-READINESS.tsv).

**R2.1 — Repository truth and release gates** is `verified`. Its readiness contract remains the authority for production-completion claims.

**R2.2 — Commercial revision enablement** is `blocked-external-evidence` until legally supplied supported commercial media is available for independently verified fingerprints. No fingerprint or commercial-game data may be guessed to bypass that blocker.

**R2.3 — Game-specific execution and device integration** remains `implemented-unverified`. The Maple runtime package now has generic Linux/Windows evidence for chained DMA, controller Device Request/Get Condition, input bridging, completion signaling and boot-harness integration. That evidence does not establish compatibility with the commercial game and therefore does not promote R2.3 to `verified`.

**R2.4 — Real gameplay integration** is `blocked-external-evidence` by the same legally supplied supported commercial image required by R2.2. Real gameplay adapters may not be fabricated from synthetic fixtures.

**R2.5 — Online product modes/M9** is now `implemented-unverified` and is the active non-external track. The first production-oriented increment is a real nonblocking UDP/direct-peer transport around the existing rollback packet protocol. Direct transport is not matchmaking: casual/ranked service claims remain unavailable until actual account/matchmaking/relay infrastructure is explicitly scoped and integrated.

Continue R2.5 through independently testable direct-session, reconnect/telemetry and user-facing online increments while preserving the external-evidence blockers and the single-executable product contract.
