# PS1 MAX³ OMEGA Commercial Activation and CI Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Expose fully validated OMEGA diagnostics through the Windows checkpoint flow, strengthen Linux/MSVC CI and artifact integrity, and produce one exact Windows x64 build for the next commercial checkpoint.

**Architecture:** Runtime APIs accept an explicit MAX³ profile and build identity. The Win32 `EXECUTAR CHECKPOINT` path requests `omega`; normal runtime/boot remains strict. CI adds focused OMEGA determinism/checkpoint validation and a portable sanitizer lane while retaining existing readiness, active-architecture, observed-disc, UDP, and artifact gates.

**Tech Stack:** C++20, Win32, CMake/CTest, GitHub Actions, GCC/Clang sanitizer support where available, MSVC 2022, SHA-256 tooling already in repo.

**Spec:** `docs/superpowers/specs/2026-09-10-ps1-max3-omega-deep-consolidation-design.md`

## Global Constraints

- Do not switch the commercial checkpoint button to OMEGA until Plans A/B/C promotion gates are green.
- `bootstrap_runtime()` normal runtime behavior remains strict and must not inherit diagnostic assumptions.
- Windows/MSVC user-facing artifact must come from the exact final SHA that passes all required jobs.
- GitHub Actions artifacts must contain no proprietary data; expected user artifact remains the executable ZIP only.
- Sanitizer CI is additive and portable-core-only; sanitizer unavailability must not weaken Windows authority or silently skip required CTest suites.
- Checkpoint output must identify schema/profile/build SHA and validate before atomic replacement.

---

### Task D1: Add explicit runtime checkpoint profile API

**Files:**
- Modify: `src/core/runtime.h`
- Modify: `src/core/runtime.cpp`
- Modify: `tests/test_ps1_local_evidence.cpp`

**Interfaces:**
- Produces:

```cpp
[[nodiscard]] Result<Ps1Max3Report> bootstrap_runtime_max3_checkpoint_to_file(
    const std::filesystem::path& install_root,
    const std::filesystem::path& report_path,
    Ps1Max3Profile profile,
    std::string build_sha = {});
```

- Preserves `bootstrap_runtime_max3_local_evidence_to_file(...)` temporarily as a compatibility wrapper.
- `bootstrap_runtime_local_evidence_to_file(...)` becomes a wrapper that explicitly selects `Ps1Max3Profile::omega` only after this plan is promoted.

- [ ] **Step 1: Write RED runtime-profile tests**

Synthetic/temporary installation fixture calls strict/deep/omega entrypoints and validates report.options.profile plus v2 profile line after save. Also call `bootstrap_runtime()` and prove it does not invoke MAX³ fallback exploration.

- [ ] **Step 2: Run RED**

```bash
cmake -S . -B build -DCMAKE_BUILD_TYPE=Release
cmake --build build --parallel 2 --target jojo_ps1_local_evidence_tests
ctest --test-dir build -R jojo_ps1_local_evidence_tests --output-on-failure
```

- [ ] **Step 3: Implement profile-aware wrapper**

Pseudo-flow:

```cpp
auto options = ps1_max3_options(profile);
auto report = explore_ps1_max3(executable.value, options);
Ps1Max3CheckpointIdentity identity{};
identity.build_sha = std::move(build_sha);
auto saved = save_ps1_max3_report_atomic(report_path, report.value, identity);
```

If `save_ps1_max3_report_atomic` currently lacks identity, extend its signature with a defaulted identity parameter in Plan B-compatible fashion.

- [ ] **Step 4: Run GREEN and commit**

```bash
ctest --test-dir build -R jojo_ps1_local_evidence_tests --output-on-failure
git add src/core/runtime.* tests/test_ps1_local_evidence.cpp
git commit -m "feat: expose MAX3 diagnostic profiles at runtime"
```

### Task D2: Wire Win32 `EXECUTAR CHECKPOINT` to OMEGA and v2 filename

**Files:**
- Modify: `src/app_win32/main.cpp`
- Modify: `tests/test_win32_image_selection.cpp` or create `tests/test_win32_checkpoint_contract.cpp` if a focused contract test is cleaner.
- Modify: `CMakeLists.txt` if a new test target is created.

**Interfaces:**
- Consumes: `bootstrap_runtime_max3_checkpoint_to_file(..., Ps1Max3Profile::omega, build_sha)`.
- Produces user checkpoint path `diagnostics/m3a-omega-checkpoint.txt` or the exact v2 filename selected by the test contract.

- [ ] **Step 1: Write RED source/contract test**

Require that the checkpoint handler selects `Ps1Max3Profile::omega`, saves under the OMEGA/v2 diagnostic filename, and reports a MAX³ termination/health result rather than only `best_report.stop_reason` when available.

- [ ] **Step 2: Update `run_checkpoint()`**

Use explicit omega API. Keep conversion/install selection logic unchanged.

- [ ] **Step 3: Update Portuguese status text**

Success status must identify an OMEGA checkpoint and output path; failure remains actionable but must not claim the game is playable/rendering.

- [ ] **Step 4: Run GREEN and commit**

```bash
cmake --build build --parallel 2
ctest --test-dir build -R "jojo_(win32_checkpoint_contract|win32_image_selection)_tests" --output-on-failure

git add src/app_win32/main.cpp CMakeLists.txt tests/test_win32_checkpoint_contract.cpp tests/test_win32_image_selection.cpp
git commit -m "feat: enable OMEGA commercial checkpoint"
```

Only add files that actually exist after implementation.

### Task D3: Register all OMEGA CTest targets and warning-clean build

**Files:**
- Modify: `CMakeLists.txt`
- Modify: source files only when compiler warnings expose a real issue introduced by OMEGA.

**Interfaces:**
- Produces named CTest coverage for all new Plan A/B/C tests.

- [ ] **Step 1: Audit CMake target registration**

Require test targets for candidate engine, coverage, search policy, budget, frontier priority, OMEGA determinism, checkpoint v2, checkpoint parser, diagnostic state hash, synthetic corpus, property matrix, and Win32 checkpoint contract where applicable.

- [ ] **Step 2: Configure/build GCC Release with warnings visible**

```bash
cmake -S . -B build -DCMAKE_BUILD_TYPE=Release
cmake --build build --parallel 2
```

Expected: no new OMEGA narrowing/unhandled-enum warnings. Existing unrelated warnings are recorded but not opportunistically refactored.

- [ ] **Step 3: Run full CTest**

```bash
ctest --test-dir build --output-on-failure
```

- [ ] **Step 4: Commit registration/warning fixes**

```bash
git add CMakeLists.txt src/core tests
git commit -m "test: register MAX3 OMEGA verification suite"
```

Review staged files before commit; do not include unrelated paths.

### Task D4: Add portable sanitizer OMEGA lane

**Files:**
- Modify: `.github/workflows/build.yml`
- Optionally create: `cmake/EnablePortableSanitizers.cmake`

**Interfaces:**
- Produces a Linux sanitizer job covering portable OMEGA core tests; Windows remains separate authority.

- [ ] **Step 1: Write/configure sanitizer build locally when compiler supports it**

Use compiler flags equivalent to:

```text
-fsanitize=address,undefined -fno-omit-frame-pointer
```

Apply only to portable core/test targets, not Windows-only code.

- [ ] **Step 2: Add workflow job**

Job configures a dedicated `build-sanitize`, builds selected OMEGA tests, and runs:

```bash
ctest --test-dir build-sanitize -R "jojo_ps1_(max3|diagnostic)_.*tests" --output-on-failure
```

- [ ] **Step 3: Keep existing Linux/MSVC jobs unchanged in authority**

Do not move artifact upload away from MSVC. Existing production-readiness, active-architecture, observed-disc and UDP steps remain required.

- [ ] **Step 4: Commit**

```bash
git add .github/workflows/build.yml cmake/EnablePortableSanitizers.cmake CMakeLists.txt
git commit -m "ci: add portable MAX3 sanitizer coverage"
```

Only add optional helper if created.

### Task D5: Add checkpoint/artifact integrity contract

**Files:**
- Create: `tests/test_ps1_omega_artifact_contract.cpp`
- Modify: `CMakeLists.txt`
- Modify: `.github/workflows/build.yml`
- Modify: `src/core/version.cpp` / `src/core/version.h` only if needed to expose a deterministic build identifier already supplied by CMake.

**Interfaces:**
- Produces final checks that report identity and artifact contain no proprietary payload.

- [ ] **Step 1: Add build identity test**

Require checkpoint identity field to be non-empty in CI-built OMEGA reports when the build SHA definition is supplied; local developer builds may use explicit `unknown` rather than an absent structural field.

- [ ] **Step 2: Add forbidden-artifact-name/content-path checks**

Workflow artifact must remain the single Windows executable archive. Reject packaging of common game image/dump extensions and diagnostic reports into the public CI artifact.

- [ ] **Step 3: Add checkpoint validator CI smoke test**

Generate a synthetic OMEGA checkpoint during tests, validate it with `validate_ps1_max3_checkpoint_text`, and assert `format=jojo-max3-checkpoint-v2` + `profile=omega`.

- [ ] **Step 4: Run GREEN and commit**

```bash
ctest --test-dir build -R jojo_ps1_omega_artifact_contract_tests --output-on-failure
git add CMakeLists.txt .github/workflows/build.yml tests/test_ps1_omega_artifact_contract.cpp src/core/version.cpp src/core/version.h
git commit -m "test: verify OMEGA checkpoint artifact identity"
```

Only stage source-version files if they were actually required.

### Task D6: Final CI, deterministic replay, and commercial artifact

**Files:**
- Modify: `PROJECT-STATE.md`
- Modify: `docs/NEXT-MILESTONES.md`
- Modify: `docs/BUILD-WINDOWS.md` only if user checkpoint instructions changed.

**Interfaces:**
- Produces final OMEGA SHA, CI run, Windows ZIP and user checkpoint instructions.

- [ ] **Step 1: Run full local Linux verification fresh**

```bash
rm -rf build
cmake -S . -B build -DCMAKE_BUILD_TYPE=Release
cmake --build build --parallel 2
ctest --test-dir build --output-on-failure
cmake -P cmake/CheckProductionReadiness.cmake
cmake -P cmake/CheckPs1ActiveArchitecture.cmake
```

Also run repository's existing observed-disc and UDP contract commands exactly as defined in `.github/workflows/build.yml`.

- [ ] **Step 2: Run deterministic OMEGA fixture repeatedly**

Use the dedicated determinism test target at least three executions. Output hashes/counts must match.

- [ ] **Step 3: Update project status/docs without claiming gameplay**

Document OMEGA diagnostic capability, strict production isolation, checkpoint v2, and that rendering/gameplay remains unverified until actual strict commercial evidence shows VRAM/frame progress.

- [ ] **Step 4: Commit docs/status**

```bash
git add PROJECT-STATE.md docs/NEXT-MILESTONES.md docs/BUILD-WINDOWS.md
git commit -m "docs: record MAX3 OMEGA checkpoint milestone"
```

Only stage docs actually changed.

- [ ] **Step 5: Require exact final SHA GitHub Actions success**

All required jobs must be `completed/success`: Linux, Windows/MSVC, sanitizer OMEGA, readiness, architecture, observed-disc, UDP, test suites, artifact upload.

- [ ] **Step 6: Download Windows artifact and verify**

Verify GitHub artifact digest, local ZIP SHA-256, contained file list, EXE SHA-256, and absence of extra files.

- [ ] **Step 7: Deliver build and request one commercial OMEGA checkpoint**

User instruction: run `EXECUTAR CHECKPOINT` once and upload the generated `m3a-omega-checkpoint.txt`. Do not ask them to supply proprietary game bytes to the repository.
