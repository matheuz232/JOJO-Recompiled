# Offline Scope Cleanup Verification

## Verified implementation/documentation head

- Branch: `feature/offline-product-rebuild`
- Exact verified head: `da62cc546a219272f168f8ec2c9a73b773d9db14`
- Workflow run: `33324514615` — PASS
- Linux job: `99292174997` — PASS
- Windows job: `99292175008` — PASS
- Linux CTest: 48/48 passed
- Windows CTest: 50/50 passed
- Windows artifact: `JOJO-Recompiled-Windows-x64`
- Artifact ID: `9735857119`
- Artifact ZIP SHA-256: `f772d6a729a7c94becb924af1f4f0ec0d1fd8fa8e4a61525e66571510b59f291`

## Scope result

The active build no longer contains or links the removed Online/networking, rollback/replay, Mods/plugin, or Training subsystems.

The permanent `jojo_offline_scope_policy` passes on Linux and Windows, preventing those removed product subsystems/targets from silently returning to the active build.

The retained offline product contract also passes. It protects exactly two logical local players and the retained resolution/aspect presentation infrastructure, including 4:3, 16:9, 16:10, 21:9 and 32:9 presentation policies.

Windows CI builds `JOJO-Recompiled.exe` without the removed WinSock/network host target. `jojo_win32_input_tests` and `jojo_win32_presentation_tests` pass on the same verified Windows job.

## Explicitly not proven by this cleanup

This verification closes only the **scope cleanup**. It does not prove:

- supported commercial game identification/end-to-end media intake;
- commercial game boot;
- real gameplay rendering;
- real game audio;
- real Player 1/Player 2 input wired into the commercial game;
- complete original offline content;
- saves/persistence through the commercial runtime;
- the required real 60 FPS gameplay patch;
- overall `100%` or production readiness.

The 60 FPS requirement remains explicitly incomplete until the commercial runtime demonstrates correct gameplay/update cadence, speed, input timing and audio synchronization. A 60 Hz presentation surface, frame duplication or interpolation alone is not accepted as proof.

## Known compiler warnings

The verified cleanup build is green but is not warning-clean. Retained SH-4/Dreamcast-era code still reports compiler warnings, including unused helpers and narrowing/tautological checks. These warnings are not hidden or counted as fixed by this verification.

## Next gate

The next product work begins with R1 media/platform/revision evidence from the user's legally supplied supported game image. Any retained architecture that does not match the actual supplied platform must be treated as historical/incorrect for that target rather than cosmetically reused.
