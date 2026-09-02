# Next milestones

The reusable architecture roadmap is [`architecture/PRODUCTION-ROADMAP.md`](architecture/PRODUCTION-ROADMAP.md). Production-completion status is machine-checked from [`architecture/PRODUCTION-READINESS.tsv`](architecture/PRODUCTION-READINESS.tsv).

**R2.1 — Repository truth and release gates** is `verified`. Its readiness contract remains the authority for production-completion claims.

**R2.2 — Commercial revision enablement** is `blocked-external-evidence` until legally supplied supported commercial media is available for independently verified fingerprints. No fingerprint or commercial-game data may be guessed to bypass that blocker.

The active non-external integration track is **R2.3 — Game-specific execution and device integration**, currently `implemented-unverified`. Generic CI evidence covers the SH-4 sleep boundary and a minimal Maple MMIO configuration boundary, but does not establish commercial-game compatibility, Maple DMA/controller functionality, or completion of R2.3.

Continue R2.3 through independently testable device/execution increments while preserving the R2.2 external-evidence blocker and the single-executable product contract.
