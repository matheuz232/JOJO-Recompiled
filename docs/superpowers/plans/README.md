# Implementation plans

These plans record implementation sequencing and verification evidence for JOJO Recompiled. Each active production increment follows test-first development where applicable and must pass Linux portable-core and Windows x64/MSVC CI before integration.

## Active product direction

The authoritative active product is the offline rebuild defined by:

- `../specs/2026-08-30-offline-product-rebuild-design.md`
- `2026-08-30-offline-scope-cleanup.md`
- `../../architecture/PRODUCTION-ROADMAP.md`

The current release gates are R1-R7. Product completion means the original offline commercial game works end-to-end through `JOJO-Recompiled.exe`, including local two-player input, retained resolution/aspect improvements and a verified real 60 FPS runtime patch.

## Historical plans

Plans and verification records for former M6 Mods, M7 Training, M8 rollback/networking and M9 Online remain in Git history/documentation as engineering records only. Those subsystems are removed from active product scope and are not release blockers or shipping features.

Older M1-M5 plans may describe reusable infrastructure that is still retained, but their historical milestone-complete wording does not prove current R1-R7 commercial-game readiness.

Mocks, isolated models and CI-only contracts never replace production-path end-to-end evidence.
