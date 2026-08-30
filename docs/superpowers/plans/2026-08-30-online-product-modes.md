# M9 Online Product Modes Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Implement and verify the portable M9 in-game Online product contract over the existing M6 mod policy and M8 rollback/network telemetry.

**Architecture:** Keep online product state/orchestration in `online.*`, deterministic replay persistence in `replay.*`, and backend/network services behind `IOnlineBackend`. Reuse `validate_mod_session()` and `NetworkTelemetry`; do not duplicate mod legality, rollback simulation, or socket transport.

**Tech Stack:** C++20, existing `Result<T>` API, M6 mod runtime/policy, M8 network/rollback types, CMake/CTest, GitHub Actions Linux + Windows x64/MSVC.

**Spec:** `docs/superpowers/specs/2026-08-30-online-product-modes-design.md`

## Global Constraints

- M9 does not implement or claim a public account service, matchmaking fleet, NAT traversal/relay, encryption/key exchange, production socket threading, or commercial-game UI rendering.
- M6 `validate_mod_session()` is the single mod-legality authority.
- M8 `NetworkTelemetry` is the network-quality source; M9 does not create a competing transport telemetry system.
- Connection quality must use RTT, jitter, packet loss, prediction and rollback behavior, not ping alone.
- `connected`, `reconnecting`, `disconnected`, and voluntary `left` are distinct product states.
- Replay payloads contain only portable metadata, inputs and hashes; no proprietary assets or raw commercial snapshots.
- Full Linux + Windows/MSVC build/CTest and Windows executable artifact are mandatory before completion.

---

### Task 1: Permanent M9 contract test and RED proof

**Files:**
- Create: `tests/test_online.cpp`
- Temporary create/delete: `.github/workflows/m9-red.yml`

**Interfaces:**
- Consumes: existing `core/mod_runtime.h`, `core/network_protocol.h`, `core/rollback.h`.
- Produces: compile-time and behavioral contract for `core/online.h` and `core/replay.h`.

- [ ] **Step 1: Write the permanent test first**

The test includes both missing headers immediately:

```cpp
#include "core/online.h"
#include "core/replay.h"

#include <iostream>

namespace {
int failures = 0;
#define CHECK(expr) do { if (!(expr)) { std::cerr << __FILE__ << ':' << __LINE__ << " CHECK failed: " #expr "\n"; ++failures; } } while (false)
}

int main() {
    // Stable menu order.
    const auto& menu = jojo::default_online_menu_entries();
    CHECK(menu.size() == 4);
    CHECK(menu[0].mode == jojo::OnlineMode::casual);
    CHECK(menu[1].mode == jojo::OnlineMode::ranked);
    CHECK(menu[2].mode == jojo::OnlineMode::direct);
    CHECK(menu[3].mode == jojo::OnlineMode::custom);

    // Additional sections below cover validation, M6 policy delegation,
    // backend orchestration, connection quality/lifecycle, history and replay.
    return failures == 0 ? 0 : 1;
}
```

The completed test must include explicit fixtures for one gameplay mod and one cosmetic mod, a fake `IOnlineBackend`, metric samples that degrade one dimension at a time, all lifecycle states, bounded history, deterministic replay bytes, malformed replay cases, and trailing-data rejection.

- [ ] **Step 2: Add a temporary Linux-only RED workflow**

```yaml
name: m9-red
on:
  push:
    branches: [feature/m9-online-product-modes]
jobs:
  red:
    runs-on: ubuntu-latest
    steps:
      - uses: actions/checkout@v4
      - run: g++ -std=c++20 -Isrc -fsyntax-only tests/test_online.cpp
```

- [ ] **Step 3: Run RED and verify intended failure**

Expected: `fatal error: core/online.h: No such file or directory` (or `core/replay.h` only after `online.h` exists). Environment/setup failures do not count as RED evidence.

- [ ] **Step 4: Commit**

Commit messages:

```text
test: define M9 online product contract
test: add temporary M9 red proof
```

---

### Task 2: Online product model, M6 policy integration and backend orchestration

**Files:**
- Create: `src/core/online.h`
- Create: `src/core/online.cpp`
- Test: `tests/test_online.cpp`

**Interfaces:**
- Consumes: `ModSetHashes`, `ResolvedModSet`, `ModSessionPolicy`, `validate_mod_session()`, `NetworkTelemetry`.
- Produces: `OnlineMode`, menu entries, rules/settings validation, `OnlineModPolicy`, connection indicator, profile/history, `IOnlineBackend`, `OnlineProductController`.

- [ ] **Step 1: Declare the public contract**

`online.h` must expose the exact spec types plus:

```cpp
class OnlineProductController {
public:
    OnlineProductController(
        IOnlineBackend& backend,
        const ResolvedModSet& mods,
        ModSetHashes hashes) noexcept;

    [[nodiscard]] Result<void> begin_matchmaking(const OnlineMatchRequest& request);
    [[nodiscard]] Result<DirectRoomDescriptor> create_direct_room(const OnlineMatchRequest& request);
    [[nodiscard]] Result<void> join_direct_room(std::string_view invite_code);
    [[nodiscard]] Result<void> validate_room(const DirectRoomDescriptor& room) const;

private:
    [[nodiscard]] Result<void> validate_request(const OnlineMatchRequest& request) const;

    IOnlineBackend& backend_;
    const ResolvedModSet& mods_;
    ModSetHashes hashes_;
};
```

- [ ] **Step 2: Implement deterministic validation and menu order**

Required rules:

```cpp
if (rules.rounds_to_win < 1 || rules.rounds_to_win > 5)
    return Result<void>::failure(ErrorCode::invalid_argument, "rounds_to_win must be 1..5");
if (rules.timer_enabled && (rules.timer_seconds < 30 || rules.timer_seconds > 999))
    return Result<void>::failure(ErrorCode::invalid_argument, "enabled timer must be 30..999 seconds");

if (settings.max_rollback_frames < 1 || settings.max_rollback_frames > 20)
    return Result<void>::failure(ErrorCode::invalid_argument, "max rollback frames must be 1..20");
if (settings.input_delay_frames > 8)
    return Result<void>::failure(ErrorCode::invalid_argument, "input delay frames must be 0..8");
```

- [ ] **Step 3: Map Online policy to M6 without duplicating gameplay-mod inspection**

Ranked:

```cpp
if (mode == OnlineMode::ranked) {
    if (online_policy.kind != OnlineModPolicyKind::ranked_legal_only)
        return Result<ModSessionPolicy>::failure(ErrorCode::invalid_argument, "ranked requires ranked_legal_only mod policy");
    return Result<ModSessionPolicy>::success(ModSessionPolicy{ModSessionMode::ranked, {}});
}
```

Casual exact compatibility:

```cpp
if (mode == OnlineMode::casual && online_policy.kind != OnlineModPolicyKind::exact_mod_set)
    return Result<ModSessionPolicy>::failure(ErrorCode::invalid_argument, "casual requires exact mod-set compatibility");
```

For `exact_mod_set`, use `required_mod_set_hash` if non-empty; otherwise use `local_hashes.mod_set_hash`. For `unrestricted`, return custom mode with empty required hash. For `ranked_legal_only` outside Ranked, map to M6 ranked mode.

Every request validation then calls:

```cpp
auto policy = make_mod_session_policy(request.mode, request.mod_policy, hashes_);
if (!policy) return Result<void>::failure(policy.error, policy.detail);
return validate_mod_session(mods_, hashes_, policy.value);
```

- [ ] **Step 4: Implement backend orchestration**

`begin_matchmaking()` rejects Direct, validates first, then forwards Casual/Ranked/Custom. `create_direct_room()` accepts only Direct, validates first, calls backend, and validates the returned descriptor before returning it. `join_direct_room()` rejects empty/whitespace-only invite codes before backend invocation. `validate_room()` requires non-empty room/invite IDs and validates contained rules/network/mod policy as a Direct request.

- [ ] **Step 5: Implement quality using all metrics**

Use helper bucket functions implementing the exact thresholds in the spec. Prediction percent is:

```cpp
const double predicted_percent = metrics.observed_frames == 0
    ? 0.0
    : 100.0 * static_cast<double>(metrics.network.predicted_frames)
        / static_cast<double>(metrics.observed_frames);
```

Rollback impairment uses `std::max(last_rollback_depth, max_rollback_depth)`. Compute the five impairment integers, `max_impairment`, and a ceiling average `(sum + 4) / 5` in integer arithmetic. Bucket exactly as specified.

- [ ] **Step 6: Implement accessible lifecycle indicator**

Connected mapping: excellent/good/fair/poor/unusable => 4/3/2/1/0 bars and `signal-4`..`signal-0`, text `Excellent`, `Good`, `Fair`, `Poor`, `Unusable`.

Reconnecting preserves the computed bars/quality but text is `Reconnecting`. Disconnected forces unusable/0/`signal-0`/`Disconnected`. Left forces unusable/0/`signal-0`/`Left match`.

- [ ] **Step 7: Implement bounded history**

Constructor throws no exception; retain capacity and let `append()` return invalid-argument if capacity is zero. `append()` rejects empty IDs, duplicates and rounds >9, evicts oldest when at capacity, then appends.

- [ ] **Step 8: Run M9 test and commit**

Expected at this stage: online-product sections pass; replay sections still fail to link/compile until Task 3.

Commit:

```text
feat: add M9 online product model
```

---

### Task 3: Deterministic replay format

**Files:**
- Create: `src/core/replay.h`
- Create: `src/core/replay.cpp`
- Test: `tests/test_online.cpp`

**Interfaces:**
- Consumes: `OnlineMode`, `RollbackInput`, `Result<T>`.
- Produces: `OnlineReplay`, validation, deterministic serializer/parser.

- [ ] **Step 1: Define binary layout**

Magic: four bytes `J`, `R`, `P`, `L`; version little-endian `uint16_t`; mode `uint8_t`; reserved `uint8_t` zero; RNG seed `uint64_t`; then bounded strings and frame vector.

Each string is `uint16_t length` + UTF-8 bytes. Maximum replay ID 128, state hash 64 exactly, mod-set hash 128, frame count 1,000,000. Each frame writes frame index `uint64_t`, local and remote inputs as `uint32_t buttons + uint16_t axis_x bits + uint16_t axis_y bits`, and 64-byte ASCII state hash.

- [ ] **Step 2: Implement validation**

Reject empty replay ID, replay IDs >128, invalid mode enum, initial/frame hashes not exactly 64 hex chars, mod hash >128, more than 1,000,000 frames, and duplicate/out-of-order frame indexes.

- [ ] **Step 3: Implement serializer**

Call validation first. Append all integers explicitly little-endian; do not `memcpy` host structs. Axis values serialize via `static_cast<std::uint16_t>(axis)` and parse back through the corresponding signed 16-bit conversion.

- [ ] **Step 4: Implement parser**

Use offset-checked readers for u8/u16/u32/u64 and bounded strings. Reject bad magic, unsupported version, nonzero reserved byte, invalid mode, any truncated field, oversized strings/count, semantic validation failure, and `offset != bytes.size()` trailing data.

- [ ] **Step 5: Run replay tests**

Required assertions include:

```cpp
auto bytes1 = serialize_online_replay(replay);
auto bytes2 = serialize_online_replay(replay);
CHECK(bytes1 && bytes2);
CHECK(bytes1.value == bytes2.value);
auto parsed = parse_online_replay(bytes1.value);
CHECK(parsed);
CHECK(parsed.value == replay);
```

Then mutate magic/version/hash/order, truncate one byte, and append one trailing byte; each parse/validation must fail.

- [ ] **Step 6: Commit**

```text
feat: add deterministic online replay format
```

---

### Task 4: Build integration, full GREEN, roadmap and verification

**Files:**
- Modify: `CMakeLists.txt`
- Delete: `.github/workflows/m9-red.yml`
- Modify: `docs/architecture/PRODUCTION-ROADMAP.md`
- Create: `docs/superpowers/plans/2026-08-30-online-product-modes-verification.md`

**Interfaces:**
- Consumes: all M9 sources/tests.
- Produces: permanent CI integration and completion evidence.

- [ ] **Step 1: Integrate sources/tests into CMake**

Add to `jojo_core`:

```cmake
  src/core/online.cpp
  src/core/replay.cpp
```

Add permanent test target:

```cmake
add_executable(jojo_online_tests tests/test_online.cpp)
target_link_libraries(jojo_online_tests PRIVATE jojo_core)
add_test(NAME jojo_online_tests COMMAND jojo_online_tests)
```

- [ ] **Step 2: Run normal branch CI on production implementation**

Required: Linux configure/build/CTest success; Windows x64/MSVC configure/build Release/CTest success; Windows executable upload success.

- [ ] **Step 3: Remove temporary RED workflow**

Delete `.github/workflows/m9-red.yml`; confirm it is absent from final diff.

- [ ] **Step 4: Mark roadmap M9 complete only after requirements are covered**

Change heading to:

```markdown
### M9 — Online product modes — Complete (100%)
```

Checklist must explicitly cover four modes, M6 mod integration, direct rooms/invites, profile/history/replays, network settings/telemetry, multi-metric connection quality, distinct lifecycle states, and Linux+Windows verification. Readiness boundary must state backend/service/UI-rendering exclusions.

- [ ] **Step 5: Record verification evidence**

Verification doc must include RED run/SHA and exact missing-header failure, GREEN run/SHA, Windows artifact ID/digest, permanent test coverage, final branch run, PR/merge SHA and post-merge run/artifact once available.

- [ ] **Step 6: Run final CI on the exact final branch head**

Any documentation commit after a prior green run creates a new head and therefore requires a new full Linux+Windows run.

- [ ] **Step 7: Review final diff**

Compare against `main`; require 0 behind, only intended M9 permanent files, and no temporary workflow.

- [ ] **Step 8: Open and merge PR safely**

Open M9 PR. Re-fetch PR, require `mergeable=true` and unchanged head SHA, then merge using `expected_head_sha`.

- [ ] **Step 9: Post-merge verification**

Require a fresh `main` workflow associated with the merge SHA: Linux configure/build/CTest success, Windows/MSVC configure/build Release/CTest/upload success, plus the Windows artifact ID/digest. Only then report M9 complete.