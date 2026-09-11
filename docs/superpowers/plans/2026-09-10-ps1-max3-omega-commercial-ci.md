# PS1 MAX³ OMEGA Commercial Activation and CI Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans task-by-task.

**Goal:** Wire fully validated OMEGA diagnostics into the Windows checkpoint workflow, add reproducible build identity/sanitizer/determinism gates, and produce one exact Windows x64 artifact for the next commercial checkpoint.

**Spec:** `docs/superpowers/specs/2026-09-10-ps1-max3-omega-deep-consolidation-design.md`

## Fixed decisions

- Plans A/B/C must pass promotion gates first.
- Normal `bootstrap_runtime()` stays strict.
- Exact commercial output filename: `diagnostics/m3a-omega-checkpoint.txt`.
- Create a dedicated Windows-only `tests/test_win32_checkpoint_contract.cpp`; do not overload `test_win32_image_selection.cpp`.
- Build identity is supplied by CMake through `JOJO_BUILD_GIT_SHA`, exposed by `jojo::build_git_sha()` in `version.{h,cpp}`. CMake runs `git rev-parse HEAD` at configure time; fallback is literal `unknown`.
- Sanitizers use one CMake option `JOJO_ENABLE_ASAN_UBSAN` in `CMakeLists.txt`; no optional helper file.
- GitHub artifact stays `JOJO-Recompiled-Windows-x64` containing only `build/Release/JOJO-Recompiled.exe`.

---

### Task D1 — Build SHA identity

**Modify:** `CMakeLists.txt`, `src/core/version.{h,cpp}`, `tests/test_main.cpp` or a focused version test.

CMake contract:

```cmake
execute_process(COMMAND git rev-parse HEAD
  WORKING_DIRECTORY ${CMAKE_SOURCE_DIR}
  OUTPUT_VARIABLE JOJO_BUILD_GIT_SHA
  OUTPUT_STRIP_TRAILING_WHITESPACE
  ERROR_QUIET)
if(NOT JOJO_BUILD_GIT_SHA)
  set(JOJO_BUILD_GIT_SHA "unknown")
endif()
target_compile_definitions(jojo_core PRIVATE JOJO_BUILD_GIT_SHA="${JOJO_BUILD_GIT_SHA}")
```

Version API:

```cpp
const char* build_git_sha() noexcept;
```

- [ ] RED: API missing / configured build expects 40-hex SHA or `unknown`.
- [ ] GREEN macro fallback implementation; no runtime Git dependency.
- [ ] Commit: `feat: expose deterministic build SHA`.

### Task D2 — Explicit runtime checkpoint profile API

**Modify:** `src/core/runtime.{h,cpp}`, `tests/test_ps1_local_evidence.cpp`.

Exact interface:

```cpp
Result<Ps1Max3Report> bootstrap_runtime_max3_checkpoint_to_file(
    const std::filesystem::path& install_root,
    const std::filesystem::path& report_path,
    Ps1Max3Profile profile,
    std::string build_sha = {});
```

If `build_sha` is empty, use `build_git_sha()`. Call the exact Plan B save API with `Ps1Max3CheckpointIdentity`.

- [ ] RED strict/deep/omega temporary-installation tests validate report profile and saved v2 identity.
- [ ] RED `bootstrap_runtime()` proves it does not invoke diagnostic fallback exploration.
- [ ] GREEN wrapper implementation; keep old local-evidence function as compatibility wrapper delegating explicitly.
- [ ] Commit: `feat: expose MAX3 diagnostic profiles at runtime`.

### Task D3 — Windows `EXECUTAR CHECKPOINT` OMEGA contract

**Modify:** `src/app_win32/main.cpp`, `CMakeLists.txt`; **create:** `tests/test_win32_checkpoint_contract.cpp`.

- [ ] RED source/runtime contract requires `Ps1Max3Profile::omega` and exact filename `m3a-omega-checkpoint.txt`.
- [ ] Windows-only test uses synthetic/local installation fixture and the shipping checkpoint helper seam; it must not require user's commercial data.
- [ ] Keep existing button text `EXECUTAR CHECKPOINT`; update status to identify OMEGA/v2 path and MAX³ termination/health without claiming gameplay/rendering.
- [ ] Register `jojo_win32_checkpoint_contract_tests` under `if(WIN32)`.
- [ ] GREEN Windows test + existing `jojo_win32_image_selection_tests`.
- [ ] Commit: `feat: enable OMEGA commercial checkpoint`.

### Task D4 — Register full OMEGA tests and warning-clean build

**Modify:** `CMakeLists.txt` and only OMEGA-touched source files that emit new warnings.

Register exact new targets from Plans A/B/C:

- candidate engine;
- coverage;
- search policy;
- budget;
- frontier priority;
- omega determinism;
- checkpoint v2;
- checkpoint parser;
- diagnostic state hash;
- synthetic corpus;
- property matrix;
- omega artifact contract;
- Win32 checkpoint contract under Windows.

- [ ] GCC Release build with `-Wall -Wextra -Wpedantic` shows no new OMEGA narrowing/unhandled-enum/overflow warnings.
- [ ] Full `ctest --test-dir build --output-on-failure`.
- [ ] Commit: `test: register MAX3 OMEGA verification suite`.

### Task D5 — Portable ASan/UBSan lane

**Modify:** `CMakeLists.txt`, `.github/workflows/build.yml`.

Add:

```cmake
option(JOJO_ENABLE_ASAN_UBSAN "Enable ASan/UBSan for portable tests" OFF)
if(JOJO_ENABLE_ASAN_UBSAN AND NOT MSVC)
  add_compile_options(-fsanitize=address,undefined -fno-omit-frame-pointer)
  add_link_options(-fsanitize=address,undefined -fno-omit-frame-pointer)
endif()
```

CI job `omega-sanitizers` on Ubuntu:

```bash
cmake -S . -B build-sanitize -DCMAKE_BUILD_TYPE=Debug -DJOJO_ENABLE_ASAN_UBSAN=ON
cmake --build build-sanitize --parallel 2
ctest --test-dir build-sanitize -R "jojo_ps1_(max3|diagnostic).*tests" --output-on-failure
```

- [ ] RED/CI configuration test where practical.
- [ ] Existing Linux/MSVC jobs remain unchanged in authority.
- [ ] Sanitizer runtime is never linked into Windows shipping EXE.
- [ ] Commit: `ci: add portable MAX3 sanitizer coverage`.

### Task D6 — Checkpoint/artifact integrity contract

**Create:** `tests/test_ps1_omega_artifact_contract.cpp`; modify `CMakeLists.txt`, `.github/workflows/build.yml` only if needed for the contract.

- [ ] Generate a synthetic omega report with default build identity; validator requires `format=jojo-max3-checkpoint-v2`, `profile=omega`, `build_sha` structural field, complete configuration fingerprint.
- [ ] Reject/report forbidden artifact packaging patterns: `.iso`, `.bin`, `.cue`, BIOS/RAM/VRAM dumps, checkpoint files in public artifact.
- [ ] Workflow upload remains exactly `build/Release/JOJO-Recompiled.exe`.
- [ ] Commit: `test: verify OMEGA checkpoint artifact identity`.

### Task D7 — Exact final verification commands

Fresh Linux build:

```bash
rm -rf build
cmake -S . -B build -DCMAKE_BUILD_TYPE=Release
cmake --build build --parallel 2
ctest --test-dir build --output-on-failure
cmake -DJOJO_SOURCE_DIR=$PWD -P cmake/CheckProductionReadiness.cmake
cmake -DJOJO_SOURCE_DIR=$PWD -P cmake/CheckProductionReadinessNegative.cmake
cmake -DJOJO_SOURCE_DIR=$PWD -P cmake/CheckPs1ActiveArchitecture.cmake
c++ -std=c++20 -Wall -Wextra -Wpedantic -Isrc tests/test_observed_disc_revision.cpp build/libjojo_core.a -ldl -pthread -o observed_disc_revision_tests
./observed_disc_revision_tests
c++ -std=c++20 -Wall -Wextra -Wpedantic -Isrc tests/test_network_transport.cpp src/core/network_protocol.cpp -o network_transport_tests
./network_transport_tests
```

Determinism stress: execute the dedicated OMEGA determinism CTest at least 3 separate times and compare its canonical output/hash fixture.

- [ ] Update `PROJECT-STATE.md`, `docs/NEXT-MILESTONES.md`, and `docs/BUILD-WINDOWS.md` with OMEGA status/instructions only; no gameplay claim.
- [ ] Push exact final SHA; require GitHub Actions Linux, Windows/MSVC, OMEGA sanitizer, readiness, architecture, observed-disc, UDP, tests, artifact upload all `completed/success`.
- [ ] Windows job continues exact commands already in workflow: VS2022 x64 configure/build, full `ctest -C Release`, readiness/architecture scripts, MSVC observed-disc/UDP contracts, single EXE upload.
- [ ] Download GitHub artifact; record GitHub digest, local ZIP SHA-256, file list, EXE SHA-256; ZIP must contain only `JOJO-Recompiled.exe`.
- [ ] Deliver artifact and ask user to run `EXECUTAR CHECKPOINT` once, then upload `m3a-omega-checkpoint.txt`.
- [ ] Commit docs: `docs: record MAX3 OMEGA checkpoint milestone`.

## Plan D promotion gate

One exact SHA passes Linux + Windows/MSVC + sanitizer OMEGA + all legacy gates; checkpoint self-validates and identifies build/profile/schema; Win32 uses OMEGA; artifact is single EXE with verified digests and no proprietary payload.
