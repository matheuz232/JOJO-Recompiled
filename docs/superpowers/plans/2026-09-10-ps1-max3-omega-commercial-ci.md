# PS1 MAX³ OMEGA Commercial Activation and CI Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans task-by-task.

**Goal:** Wire fully validated OMEGA diagnostics into the Windows checkpoint workflow, add reproducible build identity/sanitizer/determinism gates, and produce one exact Windows x64 artifact for the next commercial checkpoint.

**Spec:** `docs/superpowers/specs/2026-09-10-ps1-max3-omega-deep-consolidation-design.md`

## Fixed decisions

- Plans A/B/C must pass promotion gates first.
- Normal `bootstrap_runtime()` stays strict.
- Exact commercial output filename: `diagnostics/m3a-omega-checkpoint.txt`.
- Create dedicated tests `tests/test_version.cpp`, `tests/test_win32_checkpoint_contract.cpp`, and `tests/test_ps1_omega_artifact_contract.cpp`.
- Build identity is supplied by CMake through `JOJO_BUILD_GIT_SHA`, exposed by `jojo::build_git_sha()` in `version.{h,cpp}`. CMake runs `git rev-parse HEAD` at configure time; fallback is literal `unknown`.
- Sanitizers use one CMake option `JOJO_ENABLE_ASAN_UBSAN` in `CMakeLists.txt`; no helper module.
- GitHub artifact remains `JOJO-Recompiled-Windows-x64` and upload path remains exactly `build/Release/JOJO-Recompiled.exe`.

---

### Task D1 — Build SHA identity

**Modify:** `CMakeLists.txt`, `src/core/version.{h,cpp}`; **create:** `tests/test_version.cpp`.

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

- [ ] RED `jojo_version_tests`: API missing; test accepts exactly 40 lowercase/uppercase hex characters or literal `unknown`.
- [ ] GREEN macro fallback implementation; no runtime Git dependency.
- [ ] Register test with `add_jojo_test(jojo_version_tests tests/test_version.cpp)`.
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

If `build_sha` is empty, use `build_git_sha()`. Call Plan B's exact `save_ps1_max3_report_atomic(path, report, identity)` API.

- [ ] RED strict/deep/omega temporary-installation tests validate report profile and saved v2 identity.
- [ ] RED `bootstrap_runtime()` remains independent of MAX³ fallback exploration.
- [ ] GREEN wrapper; keep old local-evidence API as explicit compatibility delegation.
- [ ] Commit: `feat: expose MAX3 diagnostic profiles at runtime`.

### Task D3 — Windows `EXECUTAR CHECKPOINT` OMEGA contract

**Modify:** `src/app_win32/main.cpp`, `CMakeLists.txt`; **create:** `tests/test_win32_checkpoint_contract.cpp`.

- [ ] RED dedicated Windows test requires `Ps1Max3Profile::omega` and exact filename `m3a-omega-checkpoint.txt` using a synthetic/local installation fixture and shipping checkpoint helper seam.
- [ ] Keep button text exactly `EXECUTAR CHECKPOINT`.
- [ ] Update status text to identify OMEGA/v2 path and MAX³ termination/health without claiming gameplay/rendering.
- [ ] Register `jojo_win32_checkpoint_contract_tests` under `if(WIN32)`.
- [ ] GREEN new contract + existing `jojo_win32_image_selection_tests`.
- [ ] Commit: `feat: enable OMEGA commercial checkpoint`.

### Task D4 — Register all OMEGA tests and warning-clean build

**Modify:** `CMakeLists.txt` and OMEGA-touched source files only when they emit a new warning.

Register exact new targets from A/B/C/D: candidate engine, coverage, search policy, budget, frontier priority, omega determinism, checkpoint v2, checkpoint parser, diagnostic state hash, synthetic corpus, property matrix, version, omega artifact contract, and Windows checkpoint contract.

- [ ] GCC Release build with existing `-Wall -Wextra -Wpedantic`; zero new OMEGA narrowing/unhandled-enum/overflow warnings.
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

- [ ] RED before implementation:

```bash
rm -rf build-sanitize
cmake -S . -B build-sanitize -DCMAKE_BUILD_TYPE=Debug -DCMAKE_EXPORT_COMPILE_COMMANDS=ON -DJOJO_ENABLE_ASAN_UBSAN=ON
grep -q -- "-fsanitize=address,undefined" build-sanitize/compile_commands.json
```

Expected before GREEN: grep fails because flags are absent.

- [ ] GREEN configure/build and focused run:

```bash
cmake --build build-sanitize --parallel 2
ctest --test-dir build-sanitize -R "jojo_ps1_(max3|diagnostic).*tests" --output-on-failure
```

- [ ] Add GitHub Actions job `omega-sanitizers` using those exact commands.
- [ ] Existing Linux/MSVC jobs remain unchanged in authority; Windows shipping EXE never uses sanitizer option.
- [ ] Commit: `ci: add portable MAX3 sanitizer coverage`.

### Task D6 — Checkpoint/artifact integrity contract

**Create:** `tests/test_ps1_omega_artifact_contract.cpp`; **modify:** `CMakeLists.txt`.

- [ ] Generate synthetic OMEGA report using current `build_git_sha()` identity; validator requires `format=jojo-max3-checkpoint-v2`, `profile=omega`, `build_sha`, explorer version, and complete configuration fingerprint.
- [ ] Test forbidden public-package suffix list (`.iso`, `.bin`, `.cue`, BIOS/RAM/VRAM dump names, checkpoint `.txt`) against the explicitly permitted artifact filename `JOJO-Recompiled.exe`; the contract documents that diagnostics are not public CI artifacts.
- [ ] Audit `.github/workflows/build.yml` during review: upload step must still contain exactly `path: build/Release/JOJO-Recompiled.exe`; no implementation change to upload path is needed.
- [ ] Commit: `test: verify OMEGA checkpoint artifact identity`.

### Task D7 — Exact final verification and artifact

Fresh Linux commands:

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
ctest --test-dir build -R jojo_ps1_max3_omega_determinism_tests --repeat until-fail:3 --output-on-failure
```

- [ ] Update `PROJECT-STATE.md`, `docs/NEXT-MILESTONES.md`, `docs/BUILD-WINDOWS.md` with OMEGA capability/instructions and explicit “rendering/gameplay unverified until strict commercial evidence” language.
- [ ] Push exact final SHA; require GitHub Actions Linux, Windows/MSVC, `omega-sanitizers`, readiness, architecture, observed-disc, UDP, all tests, artifact upload = `completed/success`.
- [ ] Windows required commands remain those in workflow: VS2022 x64 configure/build, `ctest --test-dir build -C Release --output-on-failure`, readiness/architecture scripts, MSVC observed-disc/UDP contracts, single EXE upload.
- [ ] Download artifact; record GitHub digest, local ZIP SHA-256, contained file list, EXE SHA-256. ZIP must contain only `JOJO-Recompiled.exe`.
- [ ] Deliver ZIP; user runs `EXECUTAR CHECKPOINT` once and uploads `m3a-omega-checkpoint.txt`.
- [ ] Commit docs: `docs: record MAX3 OMEGA checkpoint milestone`.

## Plan D promotion gate

One exact SHA passes Linux + Windows/MSVC + sanitizer OMEGA + legacy gates; checkpoint self-validates and identifies build/profile/schema; Win32 uses OMEGA; artifact is single EXE with verified digests and no proprietary payload.
