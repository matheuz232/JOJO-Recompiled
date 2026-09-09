# USA Native Backend Promotion v0 Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Promote the recognized USA disc revision to `backend=native-ready` only after the local Dreamcast boot program is analyzed, a host-specific native backend cache is generated/reused and reloaded successfully, and the promoted manifest can be revalidated by `bootstrap_runtime()`.

**Architecture:** Add one focused `game_backend` component between revision identification and the existing generic SH-4/native backend. Conversion remains the owner of progress and manifest state, while `game_backend` owns revision eligibility plus boot-analysis/cache-verification policy. Runtime independently reloads and verifies the persisted native plan instead of trusting the `backend=native-ready` string.

**Tech Stack:** C++20, CMake 3.20+, existing Dreamcast ISO9660/IP.BIN pipeline, SH-4 CFG/IR/reference executor, existing x64 native backend/cache, MSVC 2022 x64, GitHub Actions Linux + Windows.

**Spec:** `docs/superpowers/specs/2026-09-08-usa-native-backend-promotion-v0-design.md`

## Global Constraints

- The only commercial revision eligible for this v0 game-specific path is `jojo-usa-observed-b8b5dbf79cdb9fcf`.
- `native-ready` means a validated game-specific backend package exists; it does **not** claim complete commercial gameplay, rendering, audio, input, timing, or end-to-end boot validation.
- The repository, tests, CI artifacts, and release package must contain no commercial game image, boot executable, assets, music, or extracted proprietary files.
- Tests use only synthetic Dreamcast/ISO9660 fixtures and synthetic SH-4 programs.
- The user's selected image remains read-only; persistent outputs are derived metadata/cache under the per-user installation directory.
- Unknown/unverified revisions retain the existing successful base-conversion behavior and remain `pending-game-specific-recompiler`.
- A supported revision must be written to disk as **pending** before game-backend preparation begins, so a failed re-prepare cannot preserve stale readiness.
- Final `native-ready` promotion is an atomic manifest replacement only after cache reload verification succeeds.
- CMake 3.20+, C++20, MSVC v143/Visual Studio 2022 x64; Windows GitHub Actions is authoritative for the x64 native backend ABI.

---

### Task 1: Add the game-specific backend preparation boundary

**Files:**
- Create: `src/core/game_backend.h`
- Create: `src/core/game_backend.cpp`
- Modify: `tests/iso_fixture.h`
- Create: `tests/test_game_backend.cpp`
- Modify: `CMakeLists.txt`

**Interfaces:**
- Consumes: `Iso9660Image`, `read_dreamcast_boot_program(...)`, `analyze_dreamcast_boot_program(...)`, `ensure_native_backend_cache(...)`, `load_native_backend_cache(...)`, `native_backend_abi_version()`.
- Produces:

```cpp
inline constexpr std::string_view kJojoUsaObservedRevisionId =
    "jojo-usa-observed-b8b5dbf79cdb9fcf";

enum class GameBackendStage {
    boot_analyzed,
    cache_ready,
    cache_verified,
};

using GameBackendProgressCallback = std::function<void(GameBackendStage)>;

struct GameNativeBackendSummary {
    std::string boot_program_hash_hex;
    std::uint32_t abi_version{};
    std::string program_hash;
    std::uint64_t block_count{};
    std::uint64_t native_block_count{};
    std::uint64_t fallback_block_count{};
    std::uint64_t native_code_bytes{};
};

[[nodiscard]] bool supports_game_native_backend(
    std::string_view revision_id) noexcept;

[[nodiscard]] Result<GameNativeBackendSummary> prepare_game_native_backend(
    std::string_view revision_id,
    const Iso9660Image& image,
    const std::filesystem::path& install_dir,
    const GameBackendProgressCallback& on_progress = {});
```

- `prepare_game_native_backend(...)` assumes revision identification has already happened. It still rejects every revision except `kJojoUsaObservedRevisionId`.
- `GameBackendStage` is semantic only; conversion owns percentages and user-facing text.

- [ ] **Step 1: Extend the synthetic ISO fixture with Dreamcast helpers**

Add to `tests/iso_fixture.h` reusable helpers that write only synthetic bytes:

```cpp
inline void write_ascii_field(const std::filesystem::path& path,
                              std::streamoff offset,
                              std::size_t width,
                              std::string_view text) {
    std::fstream io(path, std::ios::in | std::ios::out | std::ios::binary);
    std::string field(width, ' ');
    std::copy_n(text.begin(), std::min(width, text.size()), field.begin());
    io.seekp(offset);
    io.write(field.data(), static_cast<std::streamsize>(field.size()));
}

inline void install_dreamcast_ip_metadata(
    const std::filesystem::path& path,
    std::string_view device_info = "GD-ROM1/1",
    std::string_view boot_filename = "1ST_READ.BIN") {
    write_ascii_field(path, 0x000, 16, "SEGA SEGAKATANA ");
    write_ascii_field(path, 0x010, 16, "SEGA ENTERPRISES");
    write_ascii_field(path, 0x020, 16, device_info);
    write_ascii_field(path, 0x030, 8, "JUE");
    write_ascii_field(path, 0x038, 8, "E000F10");
    write_ascii_field(path, 0x040, 10, "T-TEST0001");
    write_ascii_field(path, 0x04A, 6, "V1.001");
    write_ascii_field(path, 0x050, 16, "20000101");
    write_ascii_field(path, 0x060, 16, boot_filename);
    write_ascii_field(path, 0x070, 16, "OPENAI TEST");
    write_ascii_field(path, 0x080, 128, "JOJO RECOMPILED SYNTHETIC");
}

inline void overwrite_boot_program_12(
    const std::filesystem::path& path,
    const std::array<std::uint8_t, 12>& bytes) {
    std::fstream io(path, std::ios::in | std::ios::out | std::ios::binary);
    io.seekp(static_cast<std::streamoff>(21 * sector));
    io.write(reinterpret_cast<const char*>(bytes.data()),
             static_cast<std::streamsize>(bytes.size()));
}
```

Also add the missing `<array>` include. Do not alter the existing `write_image(...)` behavior used by older tests.

- [ ] **Step 2: Write the failing game-backend tests**

Create `tests/test_game_backend.cpp` with these three RED cases:

```cpp
static constexpr std::array<std::uint8_t, 12> kValidBoot{{
    0x01, 0xE0, // MOV #1,R0
    0x02, 0x70, // ADD #2,R0
    0x09, 0x00, // NOP
    0x09, 0x00, // NOP
    0x09, 0x00, // NOP
    0x09, 0x00, // NOP
}};

static void test_supported_usa_revision_builds_and_reloads_native_cache() {
    const auto root = temp_install("supported");
    const auto image_path = root / "synthetic.iso";
    test_iso::write_image(image_path);
    test_iso::install_dreamcast_ip_metadata(image_path);
    test_iso::overwrite_boot_program_12(image_path, kValidBoot);
    const auto image = jojo::open_iso9660(image_path);
    CHECK(image);

    std::vector<jojo::GameBackendStage> stages;
    const auto prepared = jojo::prepare_game_native_backend(
        jojo::kJojoUsaObservedRevisionId, image.value, root,
        [&](jojo::GameBackendStage stage) { stages.push_back(stage); });
    CHECK(prepared);
    if (prepared) {
        CHECK(prepared.value.boot_program_hash_hex.size() == 16u);
        CHECK(prepared.value.abi_version == jojo::native_backend_abi_version());
        CHECK(!prepared.value.program_hash.empty());
        CHECK(prepared.value.block_count > 0u);
        CHECK(prepared.value.native_block_count + prepared.value.fallback_block_count ==
              prepared.value.block_count);
        CHECK(std::filesystem::is_regular_file(root / "cache/native/compiled_plan.bin"));
        CHECK(stages == std::vector<jojo::GameBackendStage>{
            jojo::GameBackendStage::boot_analyzed,
            jojo::GameBackendStage::cache_ready,
            jojo::GameBackendStage::cache_verified});
    }
}

static void test_unknown_revision_cannot_prepare_backend() {
    // Same synthetic valid image, but revision_id="other-revision".
    // Expect !prepared and ErrorCode::backend_unavailable.
}

static void test_boot_encoding_and_reachable_unsupported_opcode_are_hard_failures() {
    // Case A: install metadata with device_info="CD-ROM1/1" and expect unsupported_format.
    // Case B: use GD-ROM metadata but boot bytes start with 0xFFFF and expect backend failure.
}
```

The test must delete its temporary tree at the end of each case.

- [ ] **Step 3: Register the RED target**

In `CMakeLists.txt` add `src/core/game_backend.cpp` to `jojo_core`, then add:

```cmake
add_executable(jojo_game_backend_tests tests/test_game_backend.cpp)
target_link_libraries(jojo_game_backend_tests PRIVATE jojo_core)
add_test(NAME jojo_game_backend_tests COMMAND jojo_game_backend_tests)
```

- [ ] **Step 4: Run the test and confirm RED**

Run:

```bash
cmake -S . -B build -DCMAKE_BUILD_TYPE=Release
cmake --build build --target jojo_game_backend_tests --parallel 2
ctest --test-dir build -R jojo_game_backend_tests --output-on-failure
```

Expected: build/test fails because `core/game_backend.h` and the new API do not exist yet. Do not weaken the test.

- [ ] **Step 5: Implement the minimal game-backend component**

`src/core/game_backend.cpp` must follow this order:

```cpp
if (!supports_game_native_backend(revision_id))
    return failure(ErrorCode::backend_unavailable, ...);

auto boot = read_dreamcast_boot_program(image);
if (!boot) return failure(boot.error, boot.detail);

auto analysis = analyze_dreamcast_boot_program(boot.value);
if (!analysis) return failure(analysis.error, analysis.detail);
if (on_progress) on_progress(GameBackendStage::boot_analyzed);

auto cache = ensure_native_backend_cache(boot.value, install_dir);
if (!cache) return failure(cache.error, cache.detail);
if (on_progress) on_progress(GameBackendStage::cache_ready);

auto loaded = load_native_backend_cache(cache.value.plan_path);
if (!loaded) return failure(loaded.error, loaded.detail);

if (loaded.value.abi_version != native_backend_abi_version() ||
    loaded.value.abi_version != cache.value.abi_version ||
    loaded.value.program_hash != cache.value.program_hash ||
    loaded.value.ir.blocks.size() != cache.value.block_count) {
    return failure(ErrorCode::invalid_installation,
                   "native backend cache reload verification failed");
}
```

Compute `native_code_bytes` from the loaded blocks and require it to equal `cache.value.native_code_bytes`. Populate the summary from `boot.value.hash_hex`, loaded backend counts, current ABI, and `cache.value.program_hash`. Emit `cache_verified` only after every comparison succeeds.

Do not write `game_manifest.ini` here.

- [ ] **Step 6: Re-run focused and native-backend tests**

Run:

```bash
cmake --build build --target jojo_game_backend_tests jojo_native_backend_tests --parallel 2
ctest --test-dir build -R "jojo_(game_backend|native_backend)_tests" --output-on-failure
```

Expected: PASS.

- [ ] **Step 7: Commit Task 1**

```bash
git add CMakeLists.txt src/core/game_backend.h src/core/game_backend.cpp tests/iso_fixture.h tests/test_game_backend.cpp
git commit -m "feat: add USA game backend preparation boundary"
```

---

### Task 2: Extend the manifest with verifiable native-backend metadata

**Files:**
- Modify: `src/core/conversion.h`
- Modify: `src/core/conversion.cpp`
- Modify: `tests/test_main.cpp`

**Interfaces:**
- Consumes: `GameNativeBackendSummary` values from Task 1.
- Produces these additional `ConversionManifest` fields:

```cpp
std::string boot_program_hash_hex;
std::optional<std::uint32_t> backend_abi_version;
std::string backend_program_hash;
std::optional<std::uint64_t> backend_block_count;
std::optional<std::uint64_t> backend_native_block_count;
std::optional<std::uint64_t> backend_fallback_block_count;
std::optional<std::uint64_t> backend_native_code_bytes;
```

- Produces:

```cpp
[[nodiscard]] bool has_complete_native_backend_metadata(
    const ConversionManifest& manifest) noexcept;
```

- Persistent key names are exactly:
  - `boot_program_hash_fnv1a64`
  - `backend_abi_version`
  - `backend_program_hash`
  - `backend_block_count`
  - `backend_native_block_count`
  - `backend_fallback_block_count`
  - `backend_native_code_bytes`

- [ ] **Step 1: Write RED manifest compatibility/integrity tests**

Add to `tests/test_main.cpp`:

```cpp
static void test_pending_manifest_remains_backward_compatible() {
    // Save/load an ordinary pending manifest with no backend metadata.
    // Expect load success and has_complete_native_backend_metadata(...) == false.
}

static void test_native_ready_manifest_round_trips_complete_backend_metadata() {
    jojo::ConversionManifest m{};
    // Fill existing required source fields.
    m.revision_id = std::string(jojo::kJojoUsaObservedRevisionId);
    m.backend = "native-ready";
    m.boot_program_hash_hex = "1111111111111111";
    m.backend_abi_version = 0x20001u;
    m.backend_program_hash = "2222222222222222";
    m.backend_block_count = 4u;
    m.backend_native_block_count = 3u;
    m.backend_fallback_block_count = 1u;
    m.backend_native_code_bytes = 64u;
    CHECK(jojo::save_conversion_manifest_atomic(path, m));
    const auto loaded = jojo::load_conversion_manifest(path);
    CHECK(loaded);
    if (loaded) CHECK(jojo::has_complete_native_backend_metadata(loaded.value));
}

static void test_native_ready_manifest_rejects_missing_or_malformed_backend_metadata() {
    // Hand-write a native-ready manifest missing backend_program_hash -> invalid_installation.
    // Hand-write backend_abi_version=not-a-number -> invalid_installation.
}
```

Include `core/game_backend.h` so the revision ID is not duplicated.

- [ ] **Step 2: Run RED**

Run:

```bash
cmake --build build --target jojo_tests --parallel 2
ctest --test-dir build -R jojo_tests --output-on-failure
```

Expected: compile failure because the manifest fields/helper do not exist.

- [ ] **Step 3: Implement manifest fields and parser/writer rules**

In `conversion.h`, include `<optional>` and add the exact fields above.

In `conversion.cpp`:

1. Extend `save_conversion_manifest_atomic(...)` to reject `backend == "native-ready"` when `has_complete_native_backend_metadata(m)` is false.
2. Write optional backend keys only when they are populated; pending manifests therefore retain compatibility.
3. Extend `load_conversion_manifest(...)` to parse the seven backend keys.
4. Reuse `parse_u64(...)` for counts. For `backend_abi_version`, reject values greater than `std::numeric_limits<std::uint32_t>::max()`.
5. After normal required-field validation, if `m.backend == "native-ready"` and metadata is incomplete, return `ErrorCode::invalid_installation`.

`has_complete_native_backend_metadata(...)` must require:

```cpp
return !m.boot_program_hash_hex.empty() &&
       m.backend_abi_version.has_value() &&
       !m.backend_program_hash.empty() &&
       m.backend_block_count.has_value() &&
       m.backend_native_block_count.has_value() &&
       m.backend_fallback_block_count.has_value() &&
       m.backend_native_code_bytes.has_value() &&
       *m.backend_native_block_count + *m.backend_fallback_block_count ==
           *m.backend_block_count;
```

- [ ] **Step 4: Run focused tests**

Run:

```bash
cmake --build build --target jojo_tests --parallel 2
ctest --test-dir build -R jojo_tests --output-on-failure
```

Expected: PASS, including all old pending-manifest tests.

- [ ] **Step 5: Commit Task 2**

```bash
git add src/core/conversion.h src/core/conversion.cpp tests/test_main.cpp
git commit -m "feat: persist native backend manifest metadata"
```

---

### Task 3: Make runtime bootstrap independently verify the native cache

**Files:**
- Create: `tests/test_runtime_native_backend.cpp`
- Modify: `src/core/runtime.cpp`
- Modify: `CMakeLists.txt`

**Interfaces:**
- Consumes: `ConversionManifest` backend metadata from Task 2, `kJojoUsaObservedRevisionId`, `load_native_backend_cache(...)`, `native_backend_abi_version()`.
- Produces no new public runtime API; `bootstrap_runtime(const std::filesystem::path&)` becomes strict.

- [ ] **Step 1: Write RED forged-manifest/runtime tests**

Create `tests/test_runtime_native_backend.cpp`. Build a synthetic valid Dreamcast image with `kValidBoot`, call `prepare_game_native_backend(...)` directly to create the local cache, then create a matching `native-ready` manifest.

The success case must assert:

```cpp
CHECK(jojo::bootstrap_runtime(install));
```

Then create independent negative cases, restoring the valid installation between cases:

```cpp
// wrong revision_id -> backend_unavailable
// remove cache/native/compiled_plan.bin -> invalid_installation or file_not_found
// manifest.backend_program_hash = "ffffffffffffffff" -> invalid_installation
// manifest.backend_abi_version = 0 -> invalid_installation
// manifest.backend_block_count += 1 -> invalid_installation
// manifest.backend_native_block_count += 1 while keeping total inconsistent -> manifest save/load rejection
```

For tamper tests that intentionally need an invalid on-disk manifest, hand-write the `.ini` instead of using `save_conversion_manifest_atomic(...)`, because Task 2 correctly refuses malformed ready manifests.

- [ ] **Step 2: Register and run RED**

Add:

```cmake
add_executable(jojo_runtime_native_backend_tests tests/test_runtime_native_backend.cpp)
target_link_libraries(jojo_runtime_native_backend_tests PRIVATE jojo_core)
add_test(NAME jojo_runtime_native_backend_tests COMMAND jojo_runtime_native_backend_tests)
```

Run:

```bash
cmake --build build --target jojo_runtime_native_backend_tests --parallel 2
ctest --test-dir build -R jojo_runtime_native_backend_tests --output-on-failure
```

Expected: at least the forged/missing-cache cases fail because current `bootstrap_runtime()` trusts only the string `native-ready`.

- [ ] **Step 3: Harden `bootstrap_runtime()`**

In `src/core/runtime.cpp`, after installation validation:

```cpp
if (manifest.backend != "native-ready")
    return backend_unavailable(...);

if (manifest.revision_id != kJojoUsaObservedRevisionId)
    return backend_unavailable(...);

if (!has_complete_native_backend_metadata(manifest))
    return invalid_installation(...);

auto loaded = load_native_backend_cache(
    install_dir / "cache" / "native" / "compiled_plan.bin");
if (!loaded)
    return Result<void>::failure(loaded.error, loaded.detail);
```

Then require all of:

```cpp
loaded.value.abi_version == native_backend_abi_version();
loaded.value.abi_version == *manifest.backend_abi_version;
loaded.value.program_hash == manifest.backend_program_hash;
loaded.value.blocks.size() == *manifest.backend_block_count;
loaded.value.native_block_count == *manifest.backend_native_block_count;
loaded.value.fallback_block_count == *manifest.backend_fallback_block_count;
```

Sum `compiled.native_code.size()` over `loaded.value.blocks` and compare with `backend_native_code_bytes`.

Any mismatch returns `ErrorCode::invalid_installation`; do not rebuild the cache from runtime bootstrap because bootstrap has no access to the source image.

- [ ] **Step 4: Run runtime + existing tests**

Run:

```bash
cmake --build build --target jojo_runtime_native_backend_tests jojo_tests jojo_native_backend_tests --parallel 2
ctest --test-dir build -R "jojo_(runtime_native_backend|tests|native_backend_tests)" --output-on-failure
```

Expected: PASS.

- [ ] **Step 5: Commit Task 3**

```bash
git add CMakeLists.txt src/core/runtime.cpp tests/test_runtime_native_backend.cpp
git commit -m "feat: verify native backend cache during bootstrap"
```

---

### Task 4: Promote the supported revision during conversion with atomic pending-first semantics

**Files:**
- Modify: `src/core/conversion.h`
- Modify: `src/core/conversion.cpp`
- Modify: `tests/test_main.cpp`

**Interfaces:**
- Consumes: `supports_game_native_backend(...)`, `prepare_game_native_backend(...)`, `GameNativeBackendSummary`.
- Adds `ConversionStage` values:

```cpp
preparing_game_backend,
building_native_backend,
verifying_native_backend,
promoting_native_backend,
```

- The existing explicit `convert_image(..., ConversionOptions, ...)` remains strict about revision identification; the default UI overload still permits unverified **base** conversion only.

- [ ] **Step 1: Add test helpers for synthetic revision profiles**

In `tests/test_main.cpp`, add a local FNV-1a helper and allow the synthetic revision profile to be built from arbitrary 12-byte boot content and an arbitrary revision ID:

```cpp
static std::uint64_t fnv1a64(std::span<const std::uint8_t> bytes) {
    std::uint64_t hash = 14695981039346656037ull;
    for (auto byte : bytes) { hash ^= byte; hash *= 1099511628211ull; }
    return hash;
}

static jojo::GameRevisionProfile profile_for_boot(
    std::string revision_id,
    const std::array<std::uint8_t, 12>& boot) {
    return {
        std::move(revision_id),
        {
            {"/1ST_READ.BIN", 12, fnv1a64(boot)},
            {"/DATA/ASSET.DAT", 5, 0x65f9a54a4f1d65c8ull},
        }
    };
}
```

Add `<array>` and `<span>` includes.

This is the legal test seam: a synthetic ISO is identified by an explicit synthetic profile whose **revision ID string** is the supported USA ID. Production default conversion still obtains that ID only from the observed commercial disc fingerprint.

- [ ] **Step 2: Write the RED successful-promotion test**

Add:

```cpp
static void test_supported_revision_promotes_only_after_native_backend_verification() {
    // Build synthetic ISO, install Dreamcast metadata, overwrite boot with kValidBoot.
    // ConversionOptions contains profile_for_boot(string(kJojoUsaObservedRevisionId), kValidBoot).
    // Capture progress events.
    const auto converted = jojo::convert_image(source, install, options, callback);
    CHECK(converted);
    if (converted) {
        CHECK(converted.value.backend == "native-ready");
        CHECK(jojo::has_complete_native_backend_metadata(converted.value));
        CHECK(converted.value.revision_id == jojo::kJojoUsaObservedRevisionId);
    }
    CHECK(jojo::bootstrap_runtime(install));
    // Assert progress is monotonic and includes all four new backend stages.
}
```

Expected current behavior: converted manifest remains pending, so RED.

- [ ] **Step 3: Write the RED atomic re-prepare test**

Add:

```cpp
static void test_failed_reprepare_cannot_preserve_stale_native_ready_state() {
    // 1. Convert kValidBoot with supported USA revision profile -> expect native-ready.
    // 2. Rewrite the same synthetic source with kUnsupportedBoot whose first opcode is 0xFFFF.
    // 3. Use a new explicit revision profile matching kUnsupportedBoot but with the same supported USA revision ID.
    // 4. Run conversion again -> expect failure from backend analysis/compile.
    // 5. Load game_manifest.ini and assert backend == "pending-game-specific-recompiler".
    // 6. bootstrap_runtime(install) must return backend_unavailable.
}
```

Also retain/add a control assertion that ordinary `synthetic-test-revision` conversion still succeeds as pending and never emits a backend-preparation stage.

- [ ] **Step 4: Run RED**

Run:

```bash
cmake --build build --target jojo_tests --parallel 2
ctest --test-dir build -R jojo_tests --output-on-failure
```

Expected: successful-promotion and stale-readiness tests fail.

- [ ] **Step 5: Implement pending-first conversion orchestration**

In `conversion.cpp`, after revision identification and directory creation:

1. Build the normal `ConversionManifest` with source + revision data and `backend="pending-game-specific-recompiler"`.
2. Report installation preparation around 55%.
3. Atomically write this pending manifest **before** any game-backend call.
4. If `!supports_game_native_backend(manifest.revision_id)`, report base completion and return the pending manifest exactly as today.
5. For the supported revision, call `prepare_game_native_backend(...)` and map semantic callbacks to conversion progress:

```cpp
GameBackendProgressCallback backend_progress = [&](GameBackendStage stage) {
    switch (stage) {
        case GameBackendStage::boot_analyzed:
            report(ConversionStage::preparing_game_backend, 65,
                   "analyze_game_boot",
                   "Programa de boot Dreamcast analisado para a revisão reconhecida.");
            break;
        case GameBackendStage::cache_ready:
            report(ConversionStage::building_native_backend, 80,
                   "build_native_backend",
                   "Backend nativo gerado ou reutilizado para o programa identificado.");
            break;
        case GameBackendStage::cache_verified:
            report(ConversionStage::verifying_native_backend, 92,
                   "verify_native_backend",
                   "Cache do backend nativo recarregado e verificado.");
            break;
    }
};
```

6. If preparation fails, return that error immediately. Do **not** restore or rewrite an older ready manifest.
7. Copy the summary into the manifest fields:

```cpp
manifest.boot_program_hash_hex = summary.boot_program_hash_hex;
manifest.backend_abi_version = summary.abi_version;
manifest.backend_program_hash = summary.program_hash;
manifest.backend_block_count = summary.block_count;
manifest.backend_native_block_count = summary.native_block_count;
manifest.backend_fallback_block_count = summary.fallback_block_count;
manifest.backend_native_code_bytes = summary.native_code_bytes;
manifest.backend = "native-ready";
```

8. Report `promoting_native_backend` at 97%, atomically save the final manifest, then report 100% completion.

Do not persist the boot-program bytes themselves.

- [ ] **Step 6: Run conversion/runtime regression suite**

Run:

```bash
cmake --build build --target jojo_tests jojo_game_backend_tests jojo_runtime_native_backend_tests --parallel 2
ctest --test-dir build -R "jojo_(tests|game_backend|runtime_native_backend)_tests" --output-on-failure
```

Expected: PASS.

- [ ] **Step 7: Commit Task 4**

```bash
git add src/core/conversion.h src/core/conversion.cpp tests/test_main.cpp
git commit -m "feat: promote verified USA backend during conversion"
```

---

### Task 5: Align Windows UI semantics and make the promotion contract explicit in CI

**Files:**
- Modify: `src/app_win32/main.cpp`
- Modify: `.github/workflows/build.yml`

**Interfaces:**
- Consumes strict `bootstrap_runtime(...)` behavior and final conversion manifest from Tasks 2-4.
- No new public C++ interfaces.

- [ ] **Step 1: Update UI wording without claiming gameplay readiness**

In `refresh_install()` replace the current ready copy with wording equivalent to:

```cpp
status = L"Backend nativo da revisão USA preparado. Validação fim a fim é o próximo marco.";
add_log(L"Backend nativo verificado e cache carregável detectado.");
```

In `WM_FINISHED`, when `bootstrap_runtime(game_dir)` succeeds, use the same bounded statement rather than `Instalação nativa pronta.`.

Keep the pending message unchanged for `backend_unavailable`.

- [ ] **Step 2: Add explicit CI promotion-contract steps**

After normal CTest in both jobs, add a named contract step.

Linux:

```yaml
- name: USA native backend promotion contract
  run: ./build/jojo_game_backend_tests && ./build/jojo_runtime_native_backend_tests
```

Windows:

```yaml
- name: USA native backend promotion contract
  shell: cmd
  run: |
    build\Release\jojo_game_backend_tests.exe
    build\Release\jojo_runtime_native_backend_tests.exe
```

Do not compile or upload any commercial fixture. These executables use only synthetic data created at runtime in the runner temp directory.

- [ ] **Step 3: Run the complete local portable suite**

Run:

```bash
cmake -S . -B build -DCMAKE_BUILD_TYPE=Release
cmake --build build --parallel 2
ctest --test-dir build --output-on-failure
./build/jojo_game_backend_tests
./build/jojo_runtime_native_backend_tests
```

Expected: all tests PASS.

- [ ] **Step 4: Run production-readiness scripts**

Run:

```bash
cmake -DJOJO_SOURCE_DIR="$PWD" -P cmake/CheckProductionReadiness.cmake
cmake -DJOJO_SOURCE_DIR="$PWD" -P cmake/CheckProductionReadinessNegative.cmake
```

Expected: PASS. The readiness scan must not detect any proprietary game file or newly forbidden binary fixture.

- [ ] **Step 5: Commit Task 5**

```bash
git add src/app_win32/main.cpp .github/workflows/build.yml
git commit -m "ci: verify USA native backend promotion contract"
```

---

### Task 6: Final branch verification, review, PR, and Windows artifact handoff

**Files:**
- Review only unless a defect is found.
- PR target: `main`.

**Interfaces:**
- Consumes every task above.
- Produces one reviewed PR and a Windows x64 artifact from post-merge `main` if all gates remain green.

- [ ] **Step 1: Run full verification from a clean build**

Run:

```bash
rm -rf build
cmake -S . -B build -DCMAKE_BUILD_TYPE=Release
cmake --build build --parallel 2
ctest --test-dir build --output-on-failure
cmake -DJOJO_SOURCE_DIR="$PWD" -P cmake/CheckProductionReadiness.cmake
cmake -DJOJO_SOURCE_DIR="$PWD" -P cmake/CheckProductionReadinessNegative.cmake
```

Expected: zero failures.

- [ ] **Step 2: Review the complete diff against the design base**

Run:

```bash
git diff --check main...HEAD
git diff --stat main...HEAD
git diff main...HEAD -- \
  CMakeLists.txt \
  .github/workflows/build.yml \
  src/core/game_backend.h \
  src/core/game_backend.cpp \
  src/core/conversion.h \
  src/core/conversion.cpp \
  src/core/runtime.cpp \
  src/app_win32/main.cpp \
  tests/iso_fixture.h \
  tests/test_game_backend.cpp \
  tests/test_runtime_native_backend.cpp \
  tests/test_main.cpp
```

Review specifically for:

- no game bytes/assets added;
- exact USA eligibility gate preserved;
- pending manifest written before backend work;
- no path sets `native-ready` before cache reload verification;
- runtime does not trust the manifest string alone;
- old unverified/pending conversion behavior remains intact;
- UI does not claim complete gameplay readiness.

- [ ] **Step 3: Push/open PR and require CI**

PR summary must state:

```text
- recognizes the already-observed USA revision as the only v0 game-backend candidate
- analyzes the local Dreamcast boot program and builds/reloads the existing native backend cache
- promotes to native-ready only after derived cache metadata is verified
- hardens bootstrap against forged/stale ready manifests
- contains no commercial game content; tests are synthetic only
```

Do not merge until both `Portable core / Linux` and `Windows x64 / MSVC 2022` are green, including the explicit `USA native backend promotion contract` step.

- [ ] **Step 4: Merge with expected head SHA and validate post-merge `main`**

Use the exact PR head SHA as the merge precondition. After merge, require the `main` workflow to pass the same Linux + Windows gates.

- [ ] **Step 5: Download and verify the post-merge Windows artifact**

Download `JOJO-Recompiled-Windows-x64` from the post-merge `main` workflow. Confirm the ZIP contains only `JOJO-Recompiled.exe` and verify the local SHA-256 equals the GitHub artifact digest.

The user-local commercial validation after this task is:

1. run that exact `main` executable;
2. select the same recognized USA BIN;
3. if conversion reaches `native-ready`, inspect the new derived backend metadata and then test the next runtime/game-boot milestone;
4. if conversion fails in boot analysis/backend generation, use `%LOCALAPPDATA%\JOJO Recompiled\game\logs\conversion.log` to identify the first real unsupported SH-4/backend blocker.
