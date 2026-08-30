# Offline Scope Cleanup Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Remove Online/networking, Mods and Training from the active JOJO Recompiled product without regressing local two-player input, resolution/aspect presentation infrastructure, or the mandatory future real-60-FPS requirement.

**Architecture:** Shrink `jojo_core` and the Windows shipping target to the offline commercial-runtime dependency graph. Add a permanent configure/test policy that fails if removed product subsystems are reintroduced into the active build, while keeping historical design documents as history. This cleanup does not invent a 60 FPS toggle: the real 60 FPS patch is implemented and proven later in R3 against the commercial runtime.

**Tech Stack:** C++20, CMake 3.20+, CTest, Win32, D3D11/DXGI, XInput/Raw Input/HID, GitHub Actions Linux + Windows/MSVC.

**Spec:** `docs/superpowers/specs/2026-08-30-offline-product-rebuild-design.md`

## Global Constraints

- Shipping product remains one executable: `JOJO-Recompiled.exe`.
- Online multiplayer, rollback/netcode, network protocol/socket transport, Mods and Training are outside active product scope.
- Local multiplayer remains exactly two logical players through the normal production input path.
- Resolution and aspect-ratio presentation infrastructure must remain built and tested.
- Supported aspect policies remain 4:3, 16:9, 16:10, 21:9 and 32:9 without non-uniform stretching.
- 60 FPS remains a mandatory product requirement but must not be represented as complete until the commercial runtime proves correct gameplay/update cadence, speed and audio synchronization.
- Do not add a decorative 60 FPS setting during this cleanup.
- Historical M6/M7/M8/M9 documents remain repository history; current README/roadmap must identify them as removed historical scope rather than active goals.
- Linux and Windows CI must pass at the exact branch head before merge.

---

### Task 1: Add a permanent offline-scope regression gate

**Files:**
- Create: `cmake/verify_offline_scope.cmake`
- Create: `tests/test_offline_product_contract.cpp`
- Modify: `CMakeLists.txt`

**Interfaces:**
- Consumes: `jojo::input_player_count`, `jojo::GraphicsSettings`, `jojo::PresentationInputs`, `jojo::RendererCapabilities`, `jojo::build_presentation_plan`.
- Produces: CTest `jojo_offline_scope_policy` and executable test `jojo_offline_product_contract_tests`.

- [ ] **Step 1: Create the failing scope-policy script**

Create `cmake/verify_offline_scope.cmake` with this exact policy surface:

```cmake
if(NOT DEFINED ROOT)
  message(FATAL_ERROR "ROOT is required")
endif()

set(FORBIDDEN_ACTIVE_FILES
  "src/core/online.cpp"
  "src/core/online.h"
  "src/core/rollback.cpp"
  "src/core/rollback.h"
  "src/core/network_protocol.cpp"
  "src/core/network_protocol.h"
  "src/core/replay.cpp"
  "src/core/replay.h"
  "src/platform/windows/udp_transport_win32.cpp"
  "src/platform/windows/udp_transport_win32.h"
  "src/core/mod_runtime.cpp"
  "src/core/mod_runtime.h"
  "src/core/mod_resolver.cpp"
  "src/core/mod_content.cpp"
  "src/core/mod_policy.cpp"
  "src/core/native_mod_loader.cpp"
  "src/core/native_mod_loader.h"
  "src/mod_api/jojo_mod_api.h"
  "src/core/training.cpp"
  "src/core/training.h"
)

foreach(path IN LISTS FORBIDDEN_ACTIVE_FILES)
  if(EXISTS "${ROOT}/${path}")
    message(FATAL_ERROR "Removed product subsystem still present: ${path}")
  endif()
endforeach()

file(READ "${ROOT}/CMakeLists.txt" CMAKE_TEXT)
set(FORBIDDEN_BUILD_TOKENS
  "jojo_win32_network_host"
  "jojo_win32_udp_transport_tests"
  "jojo_online_tests"
  "jojo_online_hardening_tests"
  "jojo_rollback_tests"
  "jojo_mod_runtime_tests"
  "jojo_mod_policy_tests"
  "jojo_native_mod_loader_tests"
  "jojo_training_tests"
)

foreach(token IN LISTS FORBIDDEN_BUILD_TOKENS)
  string(FIND "${CMAKE_TEXT}" "${token}" found_at)
  if(NOT found_at EQUAL -1)
    message(FATAL_ERROR "Removed build target/token still active: ${token}")
  endif()
endforeach()
```

- [ ] **Step 2: Add the retained-product contract test**

Create `tests/test_offline_product_contract.cpp`:

```cpp
#include "core/input.h"
#include "core/presentation.h"

#include <array>
#include <cassert>

int main() {
    static_assert(jojo::input_player_count == 2);

    const jojo::RendererCapabilities caps{
        .exclusive_fullscreen = true,
        .texture_filters = {jojo::TextureFilter::off, jojo::TextureFilter::x16},
        .msaa_modes = {jojo::Msaa::off, jojo::Msaa::x4},
    };
    const jojo::PresentationInputs inputs{
        .simulation_resolution = {640u, 480u},
        .desktop_resolution = {3840u, 2160u},
        .dpi = 96u,
    };

    constexpr std::array aspects{
        jojo::AspectRatio::ratio_4_3,
        jojo::AspectRatio::ratio_16_9,
        jojo::AspectRatio::ratio_16_10,
        jojo::AspectRatio::ratio_21_9,
        jojo::AspectRatio::ratio_32_9,
    };

    for (const auto aspect : aspects) {
        jojo::GraphicsSettings graphics{};
        graphics.width = 3840;
        graphics.height = 2160;
        graphics.aspect_ratio = aspect;
        const auto plan = jojo::build_presentation_plan(graphics, inputs, caps);
        assert(plan);
        assert(plan.value.presentation_resolution.width == 3840u);
        assert(plan.value.presentation_resolution.height == 2160u);
        assert(plan.value.viewport.width > 0u);
        assert(plan.value.viewport.height > 0u);
        assert(plan.value.uniform_presentation_scale);
    }
}
```

- [ ] **Step 3: Register both tests in CMake**

Add after `enable_testing()`:

```cmake
add_test(
  NAME jojo_offline_scope_policy
  COMMAND ${CMAKE_COMMAND}
    -DROOT=${CMAKE_SOURCE_DIR}
    -P ${CMAKE_SOURCE_DIR}/cmake/verify_offline_scope.cmake)
```

Add near the other portable tests:

```cmake
add_executable(jojo_offline_product_contract_tests tests/test_offline_product_contract.cpp)
target_link_libraries(jojo_offline_product_contract_tests PRIVATE jojo_core)
add_test(NAME jojo_offline_product_contract_tests COMMAND jojo_offline_product_contract_tests)
```

- [ ] **Step 4: Run the policy test and confirm RED before deleting features**

Run:

```bash
cmake -S . -B build -DCMAKE_BUILD_TYPE=Release
ctest --test-dir build -R jojo_offline_scope_policy --output-on-failure
```

Expected: FAIL with the first remaining forbidden active file, initially `src/core/online.cpp`.

- [ ] **Step 5: Commit the RED guard**

```bash
git add cmake/verify_offline_scope.cmake tests/test_offline_product_contract.cpp CMakeLists.txt
git commit -m "test: lock offline-only product scope"
```

---

### Task 2: Remove Online, rollback, replay and Windows networking from production

**Files:**
- Delete: `src/core/online.cpp`
- Delete: `src/core/online.h`
- Delete: `src/core/rollback.cpp`
- Delete: `src/core/rollback.h`
- Delete: `src/core/network_protocol.cpp`
- Delete: `src/core/network_protocol.h`
- Delete: `src/core/replay.cpp`
- Delete: `src/core/replay.h`
- Delete: `src/platform/windows/udp_transport_win32.cpp`
- Delete: `src/platform/windows/udp_transport_win32.h`
- Delete: `tests/test_online.cpp`
- Delete: `tests/test_online_hardening.cpp`
- Delete: `tests/test_rollback.cpp`
- Delete: `tests/test_win32_udp_transport.cpp`
- Modify: `CMakeLists.txt`

**Interfaces:**
- Consumes: none; these are removed product-only subsystems.
- Produces: `jojo_recompiled` linked only to `jojo_core`, `jojo_win32_input_host`, `shell32`, `ole32`, `uuid`, `d3d11`, `dxgi`; no `jojo_win32_network_host` or `ws2_32` product dependency.

- [ ] **Step 1: Remove network-only sources from `jojo_core`**

Delete these entries from `add_library(jojo_core STATIC ...)`:

```cmake
  src/core/rollback.cpp
  src/core/network_protocol.cpp
  src/core/online.cpp
  src/core/replay.cpp
```

- [ ] **Step 2: Remove the WinSock host target and product linkage**

Delete the complete `jojo_win32_network_host` block and change the shipping link line to:

```cmake
target_link_libraries(jojo_recompiled PRIVATE jojo_core jojo_win32_input_host shell32 ole32 uuid d3d11 dxgi)
```

Delete the `jojo_win32_udp_transport_tests` target/test block.

- [ ] **Step 3: Remove Online/rollback test targets from CMake**

Delete the target/test blocks for:

```text
jojo_rollback_tests
jojo_online_tests
jojo_online_hardening_tests
```

- [ ] **Step 4: Delete the listed Online/network/replay files**

Delete every source/header/test path listed in this task. Do not delete `src/core/input.*`, `src/core/presentation.*`, `src/core/settings.*`, `src/platform/windows/controller_win32.*`, deterministic CPU/runtime files, or SHA-256 utilities.

- [ ] **Step 5: Build retained product tests**

Run:

```bash
cmake -S . -B build -DCMAKE_BUILD_TYPE=Release
cmake --build build --config Release
ctest --test-dir build --output-on-failure
```

Expected: build succeeds; `jojo_offline_scope_policy` still FAILS only because Mods/Training files remain.

- [ ] **Step 6: Commit networking removal**

```bash
git add -A
git commit -m "refactor: remove online and networking product scope"
```

---

### Task 3: Remove Mods and Training from the active build

**Files:**
- Delete: `src/core/mod_runtime.cpp`
- Delete: `src/core/mod_runtime.h`
- Delete: `src/core/mod_resolver.cpp`
- Delete: `src/core/mod_content.cpp`
- Delete: `src/core/mod_policy.cpp`
- Delete: `src/core/native_mod_loader.cpp`
- Delete: `src/core/native_mod_loader.h`
- Delete: `src/mod_api/jojo_mod_api.h`
- Delete: `src/core/training.cpp`
- Delete: `src/core/training.h`
- Delete: `tests/test_mod_runtime.cpp`
- Delete: `tests/test_mod_policy.cpp`
- Delete: `tests/test_native_mod_loader.cpp`
- Delete: `tests/fixtures/test_native_mod.cpp`
- Delete: `tests/test_training.cpp`
- Modify: `CMakeLists.txt`

**Interfaces:**
- Consumes: retained core runtime does not require a mod/plugin or training API after this task.
- Produces: active `jojo_core` with no mod/training objects or plugin-fixture targets.

- [ ] **Step 1: Remove Mod/Training sources from `jojo_core`**

Delete these CMake entries:

```cmake
  src/core/training.cpp
  src/core/mod_runtime.cpp
  src/core/mod_resolver.cpp
  src/core/mod_content.cpp
  src/core/mod_policy.cpp
  src/core/native_mod_loader.cpp
```

- [ ] **Step 2: Remove Mod/Training tests and native-plugin fixtures**

Delete the complete CMake blocks for:

```text
jojo_mod_runtime_tests
jojo_training_tests
jojo_mod_policy_tests
jojo_native_mod_fixture_first
jojo_native_mod_fixture_second
jojo_native_mod_fixture_failure
jojo_native_mod_fixture_bad_abi
jojo_native_mod_fixture_no_export
jojo_native_mod_loader_tests
```

Delete the helper function `add_jojo_native_mod_fixture` because it has no retained caller.

- [ ] **Step 3: Delete the listed Mod/Training files**

Delete every source/header/API/test/fixture path listed in this task.

Do **not** delete `sha256.*`: it remains useful to retained deterministic/runtime integrity work. Do not delete `semver.*` in this cleanup commit; remove it only in a later dead-code cleanup after an exact repository dependency audit proves there is no retained consumer.

- [ ] **Step 4: Re-run all retained tests**

Run:

```bash
cmake -S . -B build -DCMAKE_BUILD_TYPE=Release
cmake --build build --config Release
ctest --test-dir build --output-on-failure
```

Expected: `jojo_offline_scope_policy` PASS and `jojo_offline_product_contract_tests` PASS; all existing retained tests PASS.

- [ ] **Step 5: Commit feature removal**

```bash
git add -A
git commit -m "refactor: remove mods and training product scope"
```

---

### Task 4: Replace the active roadmap with offline R1-R7 truth

**Files:**
- Modify: `README.md`
- Modify: `docs/architecture/PRODUCTION-ROADMAP.md`
- Modify: `docs/NEXT-MILESTONES.md`
- Modify: `docs/superpowers/plans/README.md`

**Interfaces:**
- Consumes: approved offline rebuild spec and the actual retained build graph after Tasks 2-3.
- Produces: public documentation whose active product scope matches shipping code.

- [ ] **Step 1: Rewrite README current scope**

The README must say all of the following explicitly:

```text
- Native Windows offline port using a user-supplied legal game copy.
- Original local two-player play remains required.
- Online, Mods and Training are not active product features.
- Resolution/aspect improvements remain required.
- Real 60 FPS is a required future runtime patch and is NOT currently claimed complete.
- Current commercial game boot/playability status remains IN PROGRESS until end-to-end evidence exists.
```

Remove active-product wording that advertises rollback, Online, mod API or training features.

- [ ] **Step 2: Replace active milestone truth in `PRODUCTION-ROADMAP.md`**

Make R1-R7 from the approved spec the only active readiness gates. Move M1-M9 to a compact `Historical engineering milestones` section that states their reusable contracts/history do not define current product completeness.

R3 must explicitly include:

```text
Resolution/aspect output and a real commercial-runtime 60 FPS patch.
60 Hz swap-chain presentation alone does not satisfy 60 FPS.
```

- [ ] **Step 3: Rewrite `NEXT-MILESTONES.md`**

Set the immediate order to:

```text
1. Finish offline-scope cleanup and exact-head CI.
2. R1 media/revision evidence with a legally supplied supported image.
3. R2 commercial boot/runtime blockers.
4. R3 real rendered output, resolution/aspect integration and real 60 FPS timing patch.
5. R4 audio + two-player local input.
6. R5-R7 content, persistence and release hardening.
```

- [ ] **Step 4: Label historical plans**

Update `docs/superpowers/plans/README.md` to state that M6/M7/M8/M9 Mod/Training/Online/rollback plans are historical records and are not current product scope.

- [ ] **Step 5: Commit documentation truth update**

```bash
git add README.md docs/architecture/PRODUCTION-ROADMAP.md docs/NEXT-MILESTONES.md docs/superpowers/plans/README.md
git commit -m "docs: make offline rebuild the active roadmap"
```

---

### Task 5: Verify exact-head Linux/Windows cleanup and record evidence

**Files:**
- Create: `docs/superpowers/plans/2026-08-30-offline-scope-cleanup-verification.md`

**Interfaces:**
- Consumes: exact branch-head GitHub Actions run, Linux test results, Windows test results and Windows executable artifact metadata.
- Produces: auditable cleanup evidence; does **not** claim R1-R7 or overall product completion.

- [ ] **Step 1: Push the exact branch head and run normal CI**

Required workflow: `.github/workflows/build.yml` on `feature/offline-product-rebuild`.

- [ ] **Step 2: Inspect both job logs rather than relying only on the green run badge**

Confirm:

```text
Linux build/test: success
Windows x64/MSVC build/test: success
jojo_offline_scope_policy: PASS
jojo_offline_product_contract_tests: PASS
jojo_win32_input_tests: PASS
jojo_win32_presentation_tests: PASS
No jojo_win32_network_host build step
No jojo_win32_udp_transport_tests
No Online/rollback/Mod/Training test targets
```

- [ ] **Step 3: Confirm the Windows shipping artifact**

Verify the workflow uploads `JOJO-Recompiled-Windows-x64` (or the workflow's current canonical executable artifact), record artifact ID and digest, and confirm the artifact was produced from the exact verified head.

- [ ] **Step 4: Write the verification record**

Create `docs/superpowers/plans/2026-08-30-offline-scope-cleanup-verification.md` containing:

```markdown
# Offline Scope Cleanup Verification

- Branch: `feature/offline-product-rebuild`
- Exact verified head: `<commit sha>`
- Linux job: `<job id>` — PASS
- Windows job: `<job id>` — PASS
- Workflow run: `<run id>` — PASS
- Windows artifact: `<artifact name>`
- Artifact ID: `<artifact id>`
- Artifact digest: `<digest>`

## Scope result

Online/networking, Mods and Training are absent from the active build and shipping linkage. Local two-player input and resolution/aspect presentation contracts remain built/tested. This verifies the scope cleanup only.

## Explicitly not proven here

Commercial game boot, real gameplay rendering/audio, full original-content playability and the real 60 FPS runtime patch remain R1-R7 work and are not claimed complete by this cleanup.
```

Replace angle-bracket fields with the actual CI evidence; do not commit placeholders.

- [ ] **Step 5: Commit verification evidence and verify that final documentation-only head again**

After committing the verification file, run CI once more on that exact documentation commit if the repository policy requires every merged head to be verified. Merge only the exact green head.
