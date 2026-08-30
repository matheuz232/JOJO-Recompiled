# M9 Online Product Modes Verification

## Current status

**M9 overall status: IN PROGRESS.** The reusable portable Online core is implemented and verified, but that is not equivalent to a live end-user Online product.

The portable core currently provides:

- stable Casual, Ranked, Direct 1v1 and Custom match models;
- bounded match rules and rollback/input-delay settings;
- Ranked legality and Custom/Direct compatibility delegated to M6 `validate_mod_session()`;
- backend-independent matchmaking/direct-room orchestration through `IOnlineBackend`;
- multi-metric connection quality from RTT, jitter, packet loss, prediction rate and rollback depth;
- accessible signal token + text indicators with connected, reconnecting, disconnected and voluntary-left states kept distinct;
- profile and bounded match-history data;
- deterministic versioned replay serialization containing only inputs, hashes and portable metadata.

This is production-oriented infrastructure, not a claim that matchmaking, rooms or Internet play are live.

## TDD evidence for the portable core

- Initial RED commit `d54972c4a329bf49b85e9502448cd69410a08405`, workflow run `33318472989`: checkout/setup succeeded and `g++ -std=c++20 -Isrc -fsyntax-only tests/test_online.cpp` failed exactly with `tests/test_online.cpp:1:10: fatal error: core/online.h: No such file or directory`. This is the intended missing-feature RED, not an environment failure.
- First integrated GREEN commit `6b6d6789341153af3e68198123f3b1bcd9dfd9d7`, normal build run `33318708845`: Portable core / Linux configured, built and passed CTest; Windows x64 / MSVC 2022 configured, built Release, passed CTest and uploaded the single executable.
- First GREEN Windows artifact: `JOJO-Recompiled-Windows-x64`, artifact `9734276042`, digest `sha256:9f6fa6ce90667877cad9283983cadba2907c745521d55900953ba6f9bec1ed4d`.
- Temporary `.github/workflows/m9-red.yml` was removed after the RED proof at commit `85c305f1b58edca937a9265b8e779dd9b5127b81`; the normal build workflow is the permanent verification path.

## Hardening and cross-milestone regression fixes

Full-suite hardening intentionally looked for failures beyond the happy-path M9 contract. Two real defects were found and retained as permanent regression coverage.

### Invalid OnlineMode boundary

- Hardening RED at `26e64ef5e982e1d34b2a97a439ecdcf5bffae027`, workflow run `33319041858`: the repository built, then CTest passed 52/53 on Linux and failed only `jojo_online_hardening_tests` because `make_mod_session_policy()` accepted `static_cast<OnlineMode>(255)`.
- Root cause: the function special-cased Ranked/Casual and then switched on mod-policy kind, allowing an invalid mode to fall into a valid policy path.
- Fix commit `5e64311d726bded84bdfed31a6dde1eca124ed4a` rejects any value outside Casual/Ranked/Direct/Custom before policy mapping.
- GREEN run `33319232937` passed complete Linux and Windows/MSVC configure/build/CTest and Windows executable upload.

### Dreamcast boot SLEEP propagation

- Full-build review exposed an existing `-Wswitch` warning: `Sh4ReferenceStopReason::sleep` existed in the SH-4 executor but the Dreamcast boot result had no corresponding stop reason.
- Compile RED run `33319305205` at `20385670d1018281d7fe902642287799ebdfee5d`: the new permanent boot-runner regression test failed to compile exactly because `DreamcastBootStopReason::sleep` did not exist.
- Commit `86d90fdf1f225ad7f6a804a39dacc20101c84424` exposed the public boot `sleep` stop reason.
- Commit `3f9fc964e74e50812b9be2e10513ec6f09b60114` added the explicit `Sh4ReferenceStopReason::sleep -> DreamcastBootStopReason::sleep` mapping.
- The regression test was then corrected to execute the privileged SH-4 `SLEEP` instruction with `SR.MD=1`, preserving the executor's privilege rule rather than weakening production semantics.
- Full hardening GREEN run `33319575723` at `fce647cfc96061336d618c3a6638430cd50d1264`: Linux configured, built and passed all 53 CTests; Windows x64/MSVC configured, built Release, passed all 55 CTests and uploaded the executable.
- Hardening GREEN Windows artifact: `JOJO-Recompiled-Windows-x64`, artifact `9734527244`, digest `sha256:b8c9653cb7277a75ffbca4c979dc7213dc113b67db349fe4c5d23b793a1e6ea5`.

## Permanent portable-core test coverage

`tests/test_online.cpp` and `tests/test_online_hardening.cpp` prove:

1. stable four-mode Online menu order and non-empty product labels;
2. match-rule and network-setting boundary validation;
3. Casual exact mod-set compatibility and Ranked `ranked_legal_only` mapping;
4. Direct/Custom exact, unrestricted and ranked-legal policy mapping without duplicating M6 gameplay-mod inspection;
5. gameplay-changing mods are rejected before Ranked backend calls while cosmetic-only Ranked requests may proceed;
6. matchmaking rejects Direct mode, Direct room creation validates requests and returned descriptors, and blank invites are rejected before backend calls;
7. connection quality degrades from RTT, jitter, packet loss, prediction and rollback independently rather than from ping alone;
8. accessible signal tokens/text and connected/reconnecting/disconnected/voluntary-left lifecycle distinctions;
9. bounded match history, duplicate-ID rejection, invalid-round rejection and oldest-entry eviction;
10. deterministic replay bytes, round-trip parsing, 64-hex state hashes and strictly increasing frame indexes;
11. malformed replay magic/version/mode/reserved bytes, truncation and trailing bytes are rejected;
12. invalid `OnlineMode` enum values are rejected before any valid mod-policy path can be selected.

The existing `tests/test_dreamcast_boot_runner.cpp` additionally retains the cross-milestone regression proving that a privileged SH-4 `SLEEP` stops the boot runner as `DreamcastBootStopReason::sleep`, preserves `state.sleeping`/the sleep system event, and does not execute the following instruction as normal continuation.

## Product-level gates still open

M9 may only be called complete after all of the following have real evidence:

- real Windows socket transport for M8 datagrams, proven through two OS sockets/processes rather than a fake backend;
- real Direct/Custom room creation and joining through a deployed service or equivalent production path;
- real Casual/Ranked matchmaking integration with honest service-unavailable/error states and no fabricated opponents;
- all Online modes/settings/telemetry/profile/history/replay controls connected to the actual in-game runtime UI;
- end-user persistence for profile/history/network settings/replays;
- live M8 rollback sessions connected to the commercial game's deterministic state adapter once the commercial runtime can boot;
- an end-to-end Windows test using the same networking/runtime path as `JOJO-Recompiled.exe`.

The final branch CI, PR merge and post-merge CI are necessary gates, but **they are not sufficient to make M9 100% while the product-level items above remain unimplemented**.
