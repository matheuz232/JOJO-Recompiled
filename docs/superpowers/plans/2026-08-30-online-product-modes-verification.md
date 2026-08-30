# M9 Online Product Modes Verification

## Scope closed

M9 implements the reusable portable Online product/orchestration contract over the existing M6 mod policy and M8 rollback/network telemetry:

- stable Casual, Ranked, Direct 1v1 and Custom match modes;
- bounded match rules and rollback/input-delay settings;
- Ranked legality and Custom/Direct compatibility delegated to M6 `validate_mod_session()`;
- backend-independent matchmaking/direct-room orchestration through `IOnlineBackend`;
- multi-metric connection quality from RTT, jitter, packet loss, prediction rate and rollback depth;
- accessible signal token + text indicators with connected, reconnecting, disconnected and voluntary-left states kept distinct;
- profile and bounded match-history data;
- deterministic versioned replay serialization containing only inputs, hashes and portable metadata.

## TDD evidence

- Initial RED commit `d54972c4a329bf49b85e9502448cd69410a08405`, workflow run `33318472989`: checkout/setup succeeded and `g++ -std=c++20 -Isrc -fsyntax-only tests/test_online.cpp` failed exactly with `tests/test_online.cpp:1:10: fatal error: core/online.h: No such file or directory`. This is the intended missing-feature RED, not an environment failure.
- First integrated GREEN commit `6b6d6789341153af3e68198123f3b1bcd9dfd9d7`, normal build run `33318708845`: Portable core / Linux configured, built and passed CTest; Windows x64 / MSVC 2022 configured, built Release, passed CTest and uploaded the single executable.
- First GREEN Windows artifact: `JOJO-Recompiled-Windows-x64`, artifact `9734276042`, digest `sha256:9f6fa6ce90667877cad9283983cadba2907c745521d55900953ba6f9bec1ed4d`.
- Temporary `.github/workflows/m9-red.yml` was removed after the RED proof at commit `85c305f1b58edca937a9265b8e779dd9b5127b81`; the normal build workflow is the permanent verification path.

## Permanent test coverage

`tests/test_online.cpp` proves:

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
11. malformed replay magic/version/mode/reserved bytes, truncation and trailing bytes are rejected.

## Readiness boundary

M9 closes the portable Online product model and service-adapter contract. It does **not** claim that a public account service, production matchmaking fleet, NAT traversal/relay, encryption/key exchange, production socket threading or commercial-game Online UI rendering are already live. Those are host/backend and real-game integration responsibilities outside this portable milestone.

The final exact branch-head Linux + Windows/MSVC CI, protected PR merge, and fresh post-merge `main` CI/artifact remain mandatory integration gates. Their final IDs are recorded in the PR/integration evidence after those gates execute.