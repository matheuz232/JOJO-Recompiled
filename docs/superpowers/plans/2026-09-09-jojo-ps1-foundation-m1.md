# JoJo PS1 M0+M1 Foundation Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Replace the incorrect Dreamcast/SH-4 commercial path with a truthful PlayStation 1 foundation that recognizes the supported JoJo image, resolves `SYSTEM.CNF`, validates the real PS-X EXE, installs required user-local data into a user-selected root, and records only verified M1 evidence.

**Architecture:** Keep the existing console-independent media/ISO9660/revision infrastructure where it is correct, add focused PS1 `SYSTEM.CNF`, PS-X EXE, and generation-installation components, then rewrite conversion/runtime around manifest v2. Only after the replacement path is green remove Dreamcast/SH-4/native-backend sources and tests from the active repository. This plan deliberately stops before R3000A execution; a successful M1 installation must report that MIPS/runtime work is still pending.

**Tech Stack:** C++20, CMake 3.20+, CTest, Windows x64 / Visual Studio 2022, Win32/COM shell dialogs, portable Linux CI for core tests.

**Spec:** `docs/superpowers/specs/2026-09-09-jojo-ps1-architecture-migration-design.md`

## Global Constraints

- Active commercial platform is Sony PlayStation 1 only.
- The product is exclusive to this JoJo title/revision family, not a generic PS1 emulator.
- No proprietary PlayStation BIOS is required or distributed.
- Source BIN/CUE is opened read-only and is never modified.
- Git repository, tests, CI, artifacts, and releases contain no commercial game bytes or extracted proprietary assets.
- Synthetic fixtures are the only game-like bytes committed to the repository.
- User-local installed data derived from the user's own image may live only under the user-selected install root.
- `%LOCALAPPDATA%\JOJO Recompiled\game` is a suggested default, not a fixed destination.
- Dreamcast/SH-4 is absent from the final active build, test suite, workflow contracts, and current product documentation for this milestone.
- A synthetic test can prove parser/conversion contracts but cannot prove commercial boot/render/audio/input/gameplay.
- `JOJO-Recompiled.exe` remains the single downloadable application artifact.
- M1 must not claim `mips-analysis-ready`, `reference-execution-ready`, `native-codegen-ready`, `boot-reached`, `rendering-verified`, `audio-verified`, `input-verified`, or `gameplay-verified`.

---

## File Structure for This Plan

### New production files

- `src/core/ps1_system_cnf.h` — strict `SYSTEM.CNF` parsing and ISO9660 boot-target discovery.
- `src/core/ps1_system_cnf.cpp` — parser implementation and path normalization.
- `src/core/ps1_exe.h` — PS-X EXE metadata and parser interfaces.
- `src/core/ps1_exe.cpp` — PS-X EXE header validation, full-file hash, and ISO9660 executable loading.
- `src/core/ps1_installation.h` — generation/staging/activation interfaces.
- `src/core/ps1_installation.cpp` — generation allocation, atomic active pointer, and active-install resolution.

### New tests/fixtures

- `tests/ps1_fixture.h` — fully synthetic PS1 ISO/PS-X EXE fixture builder.
- `tests/test_ps1_system_cnf.cpp`
- `tests/test_ps1_exe.cpp`
- `tests/test_ps1_installation.cpp`
- `tests/test_ps1_conversion.cpp`
- `tests/test_ps1_runtime_installation.cpp`
- `cmake/CheckPs1ActiveArchitecture.cmake` — active-source hygiene gate after Dreamcast cleanup.

### Existing files modified

- `src/core/disc_image.cpp`
- `src/core/disc_media.cpp`
- `src/core/conversion.h`
- `src/core/conversion.cpp`
- `src/core/runtime.h`
- `src/core/runtime.cpp`
- `src/core/settings.h`
- `src/core/settings.cpp`
- `src/app_win32/main.cpp`
- `tests/iso_fixture.h`
- `tests/test_disc_media.cpp`
- `tests/test_main.cpp`
- `tests/test_win32_image_selection.cpp`
- `CMakeLists.txt`
- `.github/workflows/build.yml`
- `README.md`
- `PROJECT-STATE.md`
- `docs/NEXT-MILESTONES.md`
- `docs/architecture/PRODUCTION-ROADMAP.md`
- `docs/architecture/PRODUCTION-READINESS.tsv`

### Dreamcast/SH-4 guest-backend files deleted in Task 9

- `src/core/dreamcast_analysis.cpp`
- `src/core/dreamcast_analysis.h`
- `src/core/dreamcast_boot.cpp`
- `src/core/dreamcast_boot.h`
- `src/core/dreamcast_boot_runner.cpp`
- `src/core/dreamcast_boot_runner.h`
- `src/core/dreamcast_bus.cpp`
- `src/core/dreamcast_bus.h`
- `src/core/dreamcast_interrupts.cpp`
- `src/core/dreamcast_interrupts.h`
- `src/core/dreamcast_maple.h`
- `src/core/dreamcast_memory.cpp`
- `src/core/dreamcast_memory.h`
- `src/core/dreamcast_pvr2.cpp`
- `src/core/dreamcast_pvr2.h`
- `src/core/dreamcast_system_asic.cpp`
- `src/core/dreamcast_system_asic.h`
- `src/core/sh4_cfg.cpp`
- `src/core/sh4_cfg.h`
- `src/core/sh4_decoder.cpp`
- `src/core/sh4_decoder.h`
- `src/core/sh4_interrupt_entry.cpp`
- `src/core/sh4_ir.cpp`
- `src/core/sh4_ir.h`
- `src/core/sh4_opcode_census.cpp`
- `src/core/sh4_opcode_census.h`
- `src/core/sh4_reference_boundary.cpp`
- `src/core/sh4_reference_bus.cpp`
- `src/core/sh4_reference_executor.cpp`
- `src/core/sh4_reference_executor.h`
- `src/core/game_backend.cpp`
- `src/core/game_backend.h`
- `src/core/native_backend.cpp`
- `src/core/native_backend.h`
- `src/core/native_x64.cpp`
- `src/core/native_x64.h`

The host-side `native_mod_loader.*` is not part of the guest SH-4 backend and remains in this milestone.

---

### Task 1: Make the Accepted Media Contract PS1-Only

**Files:**
- Modify: `src/core/disc_image.cpp`
- Modify: `src/core/disc_media.cpp`
- Modify: `tests/test_main.cpp`
- Modify: `tests/test_disc_media.cpp`
- Modify: `CMakeLists.txt`

**Interfaces:**
- Consumes: existing `bool supported_disc_extension(std::string_view)` and `Result<LogicalSectorSource> open_logical_sector_source(const std::filesystem::path&)`.
- Produces: active acceptance of `.iso`, `.bin`, `.cue`; explicit rejection of `.gdi`; existing raw 2352 MODE1/MODE2 and CUE behavior unchanged.

- [ ] **Step 1: Change the extension contract test to RED**

In `tests/test_main.cpp`, replace the GDI-positive assertion with:

```cpp
static void test_disc_extension_detection() {
    CHECK(jojo::supported_disc_extension("game.ISO"));
    CHECK(jojo::supported_disc_extension("game.bin"));
    CHECK(jojo::supported_disc_extension("game.cue"));
    CHECK(!jojo::supported_disc_extension("game.gdi"));
    CHECK(!jojo::supported_disc_extension("game.zip"));
}
```

In `tests/test_disc_media.cpp`, add a test that creates a text `.gdi` file and checks:

```cpp
const auto opened = jojo::open_logical_sector_source(gdi_path);
CHECK(!opened);
CHECK(opened.error == jojo::ErrorCode::unsupported_format);
CHECK(opened.detail.find(".gdi") != std::string::npos);
```

- [ ] **Step 2: Run the targeted tests and verify RED**

Run:

```bash
cmake -S . -B build -DCMAKE_BUILD_TYPE=Debug
cmake --build build --target jojo_tests jojo_media_tests --parallel 2
ctest --test-dir build -R "jojo_tests|jojo_media_tests" --output-on-failure
```

Expected: FAIL because `.gdi` is still accepted.

- [ ] **Step 3: Remove GDI from the active media path**

Change `supported_disc_extension` in `src/core/disc_image.cpp` to:

```cpp
bool supported_disc_extension(std::string_view filename) {
    const auto ext = lower_ext(filename);
    return ext == "iso" || ext == "bin" || ext == "cue";
}
```

Change its error text to exactly:

```cpp
"supported PS1 formats: .iso, .bin, .cue"
```

In `src/core/disc_media.cpp`, remove the `open_gdi_source` branch from `open_logical_sector_source` and return the existing unsupported-format error for `.gdi`. Do not change `.bin` raw offsets: MODE1/2352 remains user offset 16 and MODE2/2352 remains user offset 24.

- [ ] **Step 4: Run media tests GREEN**

Run the same build/CTest commands. Expected: PASS.

- [ ] **Step 5: Commit**

```bash
git add src/core/disc_image.cpp src/core/disc_media.cpp tests/test_main.cpp tests/test_disc_media.cpp CMakeLists.txt
git commit -m "fix: make commercial media contract PS1-only"
```

---

### Task 2: Add Strict `SYSTEM.CNF` Boot Discovery

**Files:**
- Create: `src/core/ps1_system_cnf.h`
- Create: `src/core/ps1_system_cnf.cpp`
- Create: `tests/test_ps1_system_cnf.cpp`
- Modify: `CMakeLists.txt`

**Interfaces:**
- Consumes: `Iso9660Image`, `read_iso9660_file`.
- Produces:

```cpp
struct Ps1SystemCnf {
    std::string boot_iso_path;
};

Result<Ps1SystemCnf> parse_ps1_system_cnf(std::string_view text);
Result<Ps1SystemCnf> read_ps1_system_cnf(const Iso9660Image& image);
```

`boot_iso_path` is normalized to an ISO9660 virtual path beginning with `/` and without `;1`, for example `/SLUS_TEST.00`.

- [ ] **Step 1: Write RED parser tests**

Create `tests/test_ps1_system_cnf.cpp` with checks covering at least these exact inputs:

```cpp
CHECK(parse("BOOT = cdrom:\\SLUS_TEST.00;1\r\n").boot_iso_path == "/SLUS_TEST.00");
CHECK(parse("  boot=CDROM:\\DIR\\GAME.EXE;1  \n").boot_iso_path == "/DIR/GAME.EXE");
CHECK_FAILS("TCB = 4\n");
CHECK_FAILS("BOOT = cdrom:\\..\\GAME.EXE;1\n");
CHECK_FAILS("BOOT = cdrom:\\A.EXE;1\nBOOT = cdrom:\\B.EXE;1\n");
CHECK_FAILS("BOOT = host0:GAME.EXE\n");
```

Use the repository's simple `CHECK` pattern; do not introduce a new test framework.

- [ ] **Step 2: Register and run RED**

Add:

```cmake
add_executable(jojo_ps1_system_cnf_tests tests/test_ps1_system_cnf.cpp)
target_link_libraries(jojo_ps1_system_cnf_tests PRIVATE jojo_core)
add_test(NAME jojo_ps1_system_cnf_tests COMMAND jojo_ps1_system_cnf_tests)
```

and add `src/core/ps1_system_cnf.cpp` to `jojo_core`.

Run:

```bash
cmake --build build --target jojo_ps1_system_cnf_tests --parallel 2
ctest --test-dir build -R jojo_ps1_system_cnf_tests --output-on-failure
```

Expected: compile/link FAIL before interfaces exist.

- [ ] **Step 3: Implement the parser minimally and strictly**

Create `src/core/ps1_system_cnf.h` with the interface above. In `ps1_system_cnf.cpp`:

- split by lines;
- trim ASCII whitespace;
- compare key `BOOT` case-insensitively;
- require exactly one `BOOT` declaration;
- require a `cdrom:` prefix case-insensitively;
- accept one or more `\` or `/` separators after `cdrom:`;
- replace backslashes with `/`;
- remove terminal `;1` case-insensitively;
- reject absolute host paths, drive letters, empty components, `.` and `..` traversal;
- prepend `/` to the normalized ISO path;
- return `ErrorCode::unsupported_format` for malformed/ambiguous content;
- `read_ps1_system_cnf` reads `/SYSTEM.CNF` through `read_iso9660_file`, converts bytes to a string, and invokes the parser.

- [ ] **Step 4: Run GREEN**

Expected: all `jojo_ps1_system_cnf_tests` pass.

- [ ] **Step 5: Commit**

```bash
git add src/core/ps1_system_cnf.h src/core/ps1_system_cnf.cpp tests/test_ps1_system_cnf.cpp CMakeLists.txt
git commit -m "feat: parse PS1 SYSTEM.CNF boot target"
```

---

### Task 3: Add a PS-X EXE Parser and Synthetic PS1 Fixture

**Files:**
- Create: `src/core/ps1_exe.h`
- Create: `src/core/ps1_exe.cpp`
- Create: `tests/ps1_fixture.h`
- Create: `tests/test_ps1_exe.cpp`
- Modify: `CMakeLists.txt`

**Interfaces:**
- Consumes: `Iso9660Image`, `read_iso9660_file`, normalized boot path from Task 2.
- Produces:

```cpp
struct Ps1ExeMetadata {
    std::uint32_t entry_pc{};
    std::uint32_t initial_gp{};
    std::uint32_t text_load_address{};
    std::uint32_t text_size{};
    std::uint32_t stack_base{};
    std::uint32_t stack_size{};
    std::string fnv1a64_hex;
};

struct Ps1Executable {
    Ps1ExeMetadata metadata;
    std::vector<std::uint8_t> file_bytes;
};

Result<Ps1Executable> parse_ps1_executable(std::span<const std::uint8_t> bytes);
Result<Ps1Executable> read_ps1_executable(const Iso9660Image& image, std::string_view iso_path);
```

- [ ] **Step 1: Build a fully synthetic PS-X EXE fixture**

In `tests/ps1_fixture.h`, implement a helper that creates a byte vector of `0x800 + text_size` bytes and writes:

```text
0x000: "PS-X EXE" (8 bytes)
0x010: entry PC (LE32)
0x014: initial GP (LE32)
0x018: text load address (LE32)
0x01C: text size (LE32)
0x030: stack base (LE32)
0x034: stack size (LE32)
0x800: synthetic MIPS-looking payload bytes chosen by the test only
```

Use synthetic values:

```cpp
entry_pc         = 0x80010000u;
initial_gp       = 0x80018000u;
text_load_address= 0x80010000u;
text_size        = 16u;
stack_base       = 0x801FFF00u;
stack_size       = 0x00000100u;
```

No commercial executable bytes may appear in the fixture.

- [ ] **Step 2: Write RED PS-X EXE tests**

Verify:

```cpp
CHECK(exe.metadata.entry_pc == 0x80010000u);
CHECK(exe.metadata.initial_gp == 0x80018000u);
CHECK(exe.metadata.text_load_address == 0x80010000u);
CHECK(exe.metadata.text_size == 16u);
CHECK(exe.metadata.stack_base == 0x801FFF00u);
CHECK(exe.metadata.stack_size == 0x100u);
CHECK(exe.metadata.fnv1a64_hex.size() == 16u);
CHECK(exe.file_bytes == fixture_bytes);
```

Also mutate fixtures so tests reject:

- magic not equal to `PS-X EXE`;
- file shorter than `0x800` bytes;
- text size larger than available payload;
- entry PC not 4-byte aligned;
- text load address not 4-byte aligned;
- text size not 4-byte aligned.

- [ ] **Step 3: Register and run RED**

Add `ps1_exe.cpp` to core and `jojo_ps1_exe_tests` to CMake. Run the target and expect compile/link failure before implementation.

- [ ] **Step 4: Implement parser and full-file hash**

Use explicit little-endian reads; do not cast the header to a packed host struct. Validate exact 8-byte magic and all bounds before reading. Compute FNV-1a 64 over the **entire PS-X EXE file**, render lowercase 16-digit hex, and preserve the exact input bytes in `Ps1Executable::file_bytes`.

Do not validate guest RAM ranges in this task; the M1 contract validates file structure, while PS1 memory-map validity belongs to the later runtime/memory milestone.

- [ ] **Step 5: Run GREEN and commit**

```bash
ctest --test-dir build -R jojo_ps1_exe_tests --output-on-failure
git add src/core/ps1_exe.h src/core/ps1_exe.cpp tests/ps1_fixture.h tests/test_ps1_exe.cpp CMakeLists.txt
git commit -m "feat: validate PS-X EXE metadata"
```

---

### Task 4: Replace Manifest v1 Backend Claims with Manifest v2 Evidence

**Files:**
- Modify: `src/core/conversion.h`
- Modify: `src/core/conversion.cpp`
- Create: `tests/test_ps1_manifest.cpp`
- Modify: `CMakeLists.txt`

**Interfaces:**
- Consumes: source fingerprint and PS-X EXE metadata.
- Produces the v2 `ConversionManifest` contract:

```cpp
struct ConversionManifest {
    std::string manifest_version{"2"};
    std::string converter_version;
    std::string platform{"playstation"};
    std::string game_id{"jojo-ps1"};
    std::string source_name;
    std::string source_format;
    std::uint64_t source_size{};
    std::string source_hash_fnv1a64;
    std::string revision_id;
    std::string system_cnf_path;
    std::string boot_executable;
    std::string psx_exe_hash_fnv1a64;
    std::optional<std::uint32_t> psx_exe_entry;
    std::optional<std::uint32_t> psx_exe_load_address;
    std::optional<std::uint32_t> psx_exe_initial_gp;
    std::optional<std::uint32_t> psx_exe_text_size;
    std::optional<std::uint32_t> psx_exe_stack_base;
    std::optional<std::uint32_t> psx_exe_stack_size;
    std::string media_status{"pending"};
    std::string executable_status{"pending"};
    std::string mips_analysis_status{"pending"};
    std::string reference_runtime_status{"pending"};
    std::string native_codegen_status{"pending"};
    std::string hardware_runtime_status{"pending"};
    std::string boot_status{"pending"};
    std::string rendering_status{"pending"};
    std::string audio_status{"pending"};
    std::string input_status{"pending"};
    std::string gameplay_status{"pending"};
};
```

Delete `backend`, `boot_program_hash_hex`, and all SH-4/native-backend metadata fields from this struct.

- [ ] **Step 1: Write RED truth-contract tests**

Create a complete synthetic manifest with `media_status=verified` and `executable_status=verified`; save/load and compare every field.

Add negative assertions that `save_conversion_manifest_atomic` fails with `ErrorCode::invalid_installation` when:

```text
manifest_version != 2
platform != playstation
executable_status == verified but media_status != verified
executable_status == verified but boot_executable is empty
executable_status == verified but psx_exe_hash_fnv1a64 is empty
executable_status == verified but any mandatory PS-X numeric field is absent
any M1+ status is set to verified while executable_status is not verified
```

For this milestone, mandatory numeric fields are entry, load address, initial GP, text size, stack base, stack size; zero values are valid if the PS-X EXE header explicitly contains zero.

- [ ] **Step 2: Run RED**

Expected: tests fail against the v1/native-backend schema.

- [ ] **Step 3: Implement strict v2 serialization**

Use stable keys exactly matching the field names above. Serialize addresses as `0x` plus eight lowercase hex digits and sizes as decimal. Parsing must reject overflow, malformed hex, duplicate keys that affect truth-sensitive fields, and a missing mandatory v2 key.

Remove `has_complete_native_backend_metadata` entirely.

- [ ] **Step 4: Run GREEN and commit**

```bash
ctest --test-dir build -R jojo_ps1_manifest_tests --output-on-failure
git add src/core/conversion.h src/core/conversion.cpp tests/test_ps1_manifest.cpp CMakeLists.txt
git commit -m "feat: define truthful PS1 manifest v2"
```

---

### Task 5: Add Transactional Install Generations and Atomic Activation

**Files:**
- Create: `src/core/ps1_installation.h`
- Create: `src/core/ps1_installation.cpp`
- Create: `tests/test_ps1_installation.cpp`
- Modify: `CMakeLists.txt`

**Interfaces:**
- Consumes: user-selected install root and manifest writer from Task 4.
- Produces:

```cpp
struct PendingInstallGeneration {
    std::string generation_id;
    std::filesystem::path staging_dir;
    std::filesystem::path final_dir;
};

struct ActiveInstallGeneration {
    std::filesystem::path install_root;
    std::string generation_id;
    std::filesystem::path generation_dir;
    std::filesystem::path manifest_path;
};

Result<PendingInstallGeneration> begin_install_generation(const std::filesystem::path& install_root);
Result<void> commit_install_generation(const std::filesystem::path& install_root,
                                       const PendingInstallGeneration& generation);
Result<ActiveInstallGeneration> resolve_active_install_generation(const std::filesystem::path& install_root);
```

- [ ] **Step 1: Write RED generation tests**

Use a temporary root. Assert the first pending generation is `generation-000001`, the second allocated after a committed first generation is `generation-000002`, and staging lives under `<root>/.staging/<generation-id>` while committed data lives under `<root>/generations/<generation-id>`.

Create a valid dummy `game_manifest.ini` inside generation A, commit A, then begin B without committing it. Assert:

```cpp
const auto active = resolve_active_install_generation(root);
CHECK(active);
CHECK(active.value.generation_id == "generation-000001");
```

Also corrupt `active_install.ini` with `../escape` and assert resolution rejects traversal.

- [ ] **Step 2: Run RED**

Expected: interfaces do not exist.

- [ ] **Step 3: Implement staging and active pointer**

`begin_install_generation` must create only the selected install root plus `.staging/<id>` and required parent directories. It must not create or overwrite `active_install.ini`.

`commit_install_generation` must:

1. require a regular staging `game_manifest.ini`;
2. create `generations/`;
3. rename the staging directory to `generations/<id>` on the same volume;
4. write `active_install.ini.tmp` containing exactly:

```ini
format=1
generation_id=generation-000001
manifest=generations/generation-000001/game_manifest.ini
```

5. flush/close it;
6. atomically replace `active_install.ini` using the repository's Windows `MoveFileExW(... MOVEFILE_REPLACE_EXISTING | MOVEFILE_WRITE_THROUGH)` pattern and `std::filesystem::rename` on non-Windows;
7. leave an older valid pointer untouched if any earlier step fails.

`resolve_active_install_generation` must reject absolute paths, `..`, mismatched generation IDs, missing generation directories, and missing manifests.

- [ ] **Step 4: Run GREEN and commit**

```bash
ctest --test-dir build -R jojo_ps1_installation_tests --output-on-failure
git add src/core/ps1_installation.h src/core/ps1_installation.cpp tests/test_ps1_installation.cpp CMakeLists.txt
git commit -m "feat: add transactional PS1 install generations"
```

---

### Task 6: Rewrite Conversion as PS1 Foundation + Executable Discovery

**Files:**
- Modify: `src/core/conversion.h`
- Modify: `src/core/conversion.cpp`
- Create: `tests/test_ps1_conversion.cpp`
- Modify: `tests/ps1_fixture.h`
- Modify: `tests/test_observed_disc_revision.cpp`
- Modify: `CMakeLists.txt`

**Interfaces:**
- Consumes: Tasks 1–5 plus existing revision profiles and exact observed whole-image fingerprint recognition.
- Produces:

```cpp
enum class ConversionStage {
    validating_source,
    fingerprinting_source,
    discovering_filesystem,
    identifying_revision,
    reading_system_cnf,
    reading_psx_exe,
    preparing_installation,
    copying_local_data,
    writing_manifest,
    activating_installation,
    completed
};

struct ConversionOptions {
    std::vector<GameRevisionProfile> revision_profiles;
};
```

`convert_image(source, install_root, options, callback)` and the production overload remain the entry points. Remove `allow_unverified_base_conversion`.

- [ ] **Step 1: Extend `tests/ps1_fixture.h` to emit a complete synthetic PS1 ISO**

The fixture must contain at minimum:

```text
/SYSTEM.CNF
/SLUS_TEST.00
/DATA/ASSET.DAT
```

`SYSTEM.CNF` content:

```text
BOOT = cdrom:\SLUS_TEST.00;1
TCB = 4
EVENT = 10
STACK = 801FFF00
```

`SLUS_TEST.00` is the synthetic PS-X EXE from Task 3. `ASSET.DAT` contains a small ASCII synthetic payload.

Provide helpers for cooked ISO and raw MODE2/2352 conversion so the same M1 conversion contract can run against both layouts.

- [ ] **Step 2: Write RED end-to-end conversion tests**

Use an injected `GameRevisionProfile` containing synthetic file hashes for `/SYSTEM.CNF`, `/SLUS_TEST.00`, and `/DATA/ASSET.DAT`.

Assert a successful conversion produces:

```text
<root>/active_install.ini
<root>/generations/generation-000001/game_manifest.ini
<root>/generations/generation-000001/data/SYSTEM.CNF
<root>/generations/generation-000001/data/boot.psxexe
```

Assert the manifest has:

```cpp
manifest_version == "2"
platform == "playstation"
revision_id == "synthetic-ps1-jojo"
system_cnf_path == "/SYSTEM.CNF"
boot_executable == "/SLUS_TEST.00"
media_status == "verified"
executable_status == "verified"
mips_analysis_status == "pending"
native_codegen_status == "pending"
boot_status == "pending"
gameplay_status == "pending"
```

Delete the source image after conversion and assert the installed `data/boot.psxexe` remains readable and matches the fixture executable bytes.

Run the same conversion once with cooked ISO and once with MODE2/2352 BIN.

Add failure tests for missing `SYSTEM.CNF`, malformed `BOOT`, invalid PS-X EXE magic, unknown revision, and an unwritable/invalid destination. None may create or change `active_install.ini`.

- [ ] **Step 3: Run RED**

Expected: old conversion attempts Dreamcast backend preparation or writes v1 layout.

- [ ] **Step 4: Implement the M1 conversion sequence**

The production overload must retain the exact observed source fingerprint contract already established for the user's image:

```text
format=bin
size=666806112
fnv1a64=b8b5dbf79cdb9fcf
revision_id=jojo-usa-observed-b8b5dbf79cdb9fcf
```

Do not embed any other commercial metadata or bytes.

Sequence:

```text
validate source extension/file
fingerprint source
open PS1 logical sectors + ISO9660
identify revision
read + parse /SYSTEM.CNF
read + validate resolved PS-X EXE
begin install generation
create staging data/ and logs/
write exact SYSTEM.CNF bytes to data/SYSTEM.CNF
write exact PS-X EXE bytes to data/boot.psxexe
construct manifest v2 with M1 evidence only
save staging game_manifest.ini last
commit generation + active pointer
return manifest
```

If writing any local file fails, return an error and never activate the new generation.

Do not call `prepare_game_native_backend`, Dreamcast analysis, SH-4 analysis, or native backend cache code.

- [ ] **Step 5: Run GREEN on cooked and MODE2 tests**

```bash
ctest --test-dir build -R "jojo_ps1_conversion_tests|jojo_media_tests|jojo_iso_tests|observed" --output-on-failure
```

Expected: PASS.

- [ ] **Step 6: Commit**

```bash
git add src/core/conversion.h src/core/conversion.cpp tests/ps1_fixture.h tests/test_ps1_conversion.cpp tests/test_observed_disc_revision.cpp CMakeLists.txt
git commit -m "feat: convert JoJo media through PS1 executable discovery"
```

---

### Task 7: Make Runtime Validation Truthful for M1 and Reject Legacy v1 Installs

**Files:**
- Modify: `src/core/runtime.h`
- Modify: `src/core/runtime.cpp`
- Create: `tests/test_ps1_runtime_installation.cpp`
- Modify: `CMakeLists.txt`

**Interfaces:**
- Consumes: `resolve_active_install_generation`, manifest v2, local `data/boot.psxexe`, PS-X EXE parser.
- Produces:

```cpp
struct InstallationInfo {
    std::filesystem::path install_root;
    std::filesystem::path generation_dir;
    ConversionManifest manifest;
};

Result<InstallationInfo> validate_installation(const std::filesystem::path& install_root);
Result<void> bootstrap_runtime(const std::filesystem::path& install_root);
```

- [ ] **Step 1: Write RED runtime-install tests**

From a synthetic successful M1 conversion, assert `validate_installation(root)` succeeds even after deleting the source image.

Then mutate copies and assert rejection for:

- `manifest_version=1` with detail containing `legacy` and `Dreamcast/SH-4`;
- `platform` not equal to `playstation`;
- missing `data/boot.psxexe`;
- changed byte in `data/boot.psxexe` causing full-file hash mismatch;
- manifest `boot_executable` empty while executable status is verified;
- active pointer referencing a missing generation.

Assert `bootstrap_runtime` on a valid M1 installation returns:

```cpp
CHECK(!boot);
CHECK(boot.error == jojo::ErrorCode::backend_unavailable);
CHECK(boot.detail.find("R3000A") != std::string::npos);
CHECK(boot.detail.find("not implemented") != std::string::npos);
```

This is deliberate: M1 is installable/inspectable, not bootable.

- [ ] **Step 2: Run RED**

Expected: current runtime requires `native-ready` SH-4 cache and fails the new contract.

- [ ] **Step 3: Rewrite runtime validation**

Remove `game_backend.h` and `native_backend.h` dependencies. Resolve active generation, load manifest v2, require `platform=playstation`, require `media_status=verified` and `executable_status=verified`, parse `data/boot.psxexe`, and compare all stored PS-X metadata/hash against the local file.

For a root that lacks `active_install.ini` but contains an old root-level `game_manifest.ini`, inspect enough of that manifest to identify v1 and return the explicit legacy error rather than a generic missing-installation error.

`bootstrap_runtime` must call `validate_installation` first, then return the explicit R3000A-not-implemented `backend_unavailable` error. It must not create caches or promote status.

- [ ] **Step 4: Run GREEN and commit**

```bash
ctest --test-dir build -R jojo_ps1_runtime_installation_tests --output-on-failure
git add src/core/runtime.h src/core/runtime.cpp tests/test_ps1_runtime_installation.cpp CMakeLists.txt
git commit -m "fix: validate PS1 M1 installs without false boot claims"
```

---

### Task 8: Persist a User-Selectable Install Root and Add Win32 Folder Selection

**Files:**
- Modify: `src/core/settings.h`
- Modify: `src/core/settings.cpp`
- Modify: `tests/test_main.cpp`
- Modify: `src/app_win32/main.cpp`
- Modify: `tests/test_win32_image_selection.cpp`
- Modify: `CMakeLists.txt`

**Interfaces:**
- Consumes: existing atomic settings file, M1 conversion and runtime validation.
- Produces: `AppSettings::install_root`, settings key `install_root=...`, Win32 folder picker, remembered install root.

- [ ] **Step 1: Write RED settings migration tests**

Change the settings round-trip test to:

```cpp
jojo::AppSettings in{};
in.install_root = "C:/Games/JOJO-Recompiled";
CHECK(jojo::save_settings_atomic(path, in));
const auto loaded = jojo::load_settings(path);
CHECK(loaded);
CHECK(loaded.value.install_root == in.install_root);
```

Add a legacy-input test that writes:

```ini
install_dir=D:/Old/JoJo
```

and asserts `load_settings` maps it to `install_root == "D:/Old/JoJo"`. Saving that result must emit `install_root=` and must not emit `install_dir=`.

- [ ] **Step 2: Run settings test RED**

Expected: `AppSettings::install_root` does not exist.

- [ ] **Step 3: Implement settings key migration**

In `AppSettings`, replace `install_dir` with:

```cpp
std::string install_root{};
```

Parser behavior:

```cpp
if (key == "install_root") result.install_root = value;
else if (key == "install_dir" && result.install_root.empty()) result.install_root = value;
```

Serializer writes only `install_root=`.

- [ ] **Step 4: Write RED Win32 UX assertions**

Assign stable IDs:

```cpp
ID_SOURCE_PATH = 1001
ID_SELECT_SOURCE = 1002
ID_PREPARE = 1003
ID_INSTALL_PATH = 1004
ID_SELECT_INSTALL = 1005
```

Update `tests/test_win32_image_selection.cpp` to assert both path fields and both selection buttons are visible/usable. Click `ID_SELECT_INSTALL`, assert a native `#32770` dialog opens, cancel it, and assert the install path remains unchanged.

Update source-drop test extensions to `{L".iso", L".cue", L".BIN"}` and add a `.gdi` drop assertion that selection is unchanged.

- [ ] **Step 5: Run Win32 test RED in Windows CI or local VS2022 environment**

Expected: install controls are absent and GDI is still listed in UI filters.

- [ ] **Step 6: Implement folder selection and persistence**

Use `IFileOpenDialog` with `FOS_PICKFOLDERS | FOS_FORCEFILESYSTEM`. The application root remains `%LOCALAPPDATA%\JOJO Recompiled`; the settings path is `<app_root>/settings.ini`.

Startup sequence:

```text
load settings.ini if valid
if install_root is empty: propose <app_root>/game
set install-path edit control
call refresh_install() against selected/proposed root
```

Do **not** create the selected game install root merely by starting the application. It is created only when conversion starts.

When the user chooses another folder:

```text
update game_dir
update install edit box
save settings atomically
refresh installation status
```

Update the source picker filter and messages to `.iso;*.bin;*.cue` only.

Replace fixed text `Dados convertidos: %LOCALAPPDATA%...` with a visible install-root label derived from `game_dir`.

After successful M1 conversion, UI text must be equivalent to:

```text
Executável PS1 identificado e instalação validada. Análise R3000A/MIPS é o próximo marco.
```

It must not say `backend nativo preparado`, `native-ready`, or imply bootability.

- [ ] **Step 7: Run settings + Windows tests GREEN**

Linux/portable:

```bash
ctest --test-dir build -R jojo_tests --output-on-failure
```

Windows:

```powershell
ctest --test-dir build -C Release -R jojo_win32_image_selection_tests --output-on-failure
```

- [ ] **Step 8: Commit**

```bash
git add src/core/settings.h src/core/settings.cpp tests/test_main.cpp src/app_win32/main.cpp tests/test_win32_image_selection.cpp CMakeLists.txt
git commit -m "feat: let users choose JoJo install root"
```

---

### Task 9: Remove Dreamcast/SH-4 Guest Architecture and Old Backend Contracts

**Files:**
- Create: `cmake/CheckPs1ActiveArchitecture.cmake`
- Modify: `CMakeLists.txt`
- Modify: `.github/workflows/build.yml`
- Modify: `tests/iso_fixture.h`
- Modify: `tests/test_main.cpp`
- Delete: all guest/backend source files listed in the File Structure section.
- Delete old Dreamcast/SH-4/backend tests listed below.

**Interfaces:**
- Consumes: complete green PS1 M1 path from Tasks 1–8.
- Produces: an active repository whose compiled/tested commercial path contains no Dreamcast, SH-4, GDI, `game_backend`, or old guest `native_backend` dependency.

**Delete these old tests:**

- `tests/test_dreamcast_analysis.cpp`
- `tests/test_dreamcast_boot.cpp`
- `tests/test_dreamcast_boot_runner.cpp`
- `tests/test_dreamcast_bus_diagnostics.cpp`
- `tests/test_dreamcast_executable_memory.cpp`
- `tests/test_dreamcast_irq_delivery.cpp`
- `tests/test_dreamcast_maple.cpp`
- `tests/test_dreamcast_pvr2_vblank.cpp`
- `tests/test_dreamcast_system_asic.cpp`
- `tests/test_game_backend.cpp`
- `tests/test_native_backend.cpp`
- `tests/test_manifest_backend_metadata.cpp`
- `tests/test_runtime_native_backend.cpp`
- `tests/test_unverified_conversion.cpp`
- `tests/test_sh4_addressing_pipeline.cpp`
- `tests/test_sh4_block_boundary.cpp`
- `tests/test_sh4_carry_overflow.cpp`
- `tests/test_sh4_cfg.cpp`
- `tests/test_sh4_data_manipulation.cpp`
- `tests/test_sh4_decoder.cpp`
- `tests/test_sh4_flds_fsts.cpp`
- `tests/test_sh4_float_ftrc.cpp`
- `tests/test_sh4_fmov_memory.cpp`
- `tests/test_sh4_fmov_registers.cpp`
- `tests/test_sh4_fpscr_banks.cpp`
- `tests/test_sh4_fpu_arithmetic.cpp`
- `tests/test_sh4_fpu_compare.cpp`
- `tests/test_sh4_fpu_completion.cpp`
- `tests/test_sh4_fpu_constants.cpp`
- `tests/test_sh4_fpu_disable.cpp`
- `tests/test_sh4_fpu_mode_switches.cpp`
- `tests/test_sh4_fpu_unary.cpp`
- `tests/test_sh4_fpul_transfers.cpp`
- `tests/test_sh4_gbr_memory.cpp`
- `tests/test_sh4_gbr_register_transfer.cpp`
- `tests/test_sh4_integer_pipeline.cpp`
- `tests/test_sh4_interrupt_entry.cpp`
- `tests/test_sh4_ir.cpp`
- `tests/test_sh4_memory_bus.cpp`
- `tests/test_sh4_memory_pipeline.cpp`
- `tests/test_sh4_multiply.cpp`
- `tests/test_sh4_opcode_census.cpp`
- `tests/test_sh4_pr_mac_register_transfer.cpp`
- `tests/test_sh4_reference_executor.cpp`

- [ ] **Step 1: Add a RED architecture hygiene gate**

Create `cmake/CheckPs1ActiveArchitecture.cmake` that reads active production/test/config files only:

```cmake
set(_roots
  "${JOJO_SOURCE_DIR}/src"
  "${JOJO_SOURCE_DIR}/tests"
)
file(GLOB_RECURSE _active CONFIGURE_DEPENDS
  "${JOJO_SOURCE_DIR}/src/*.h" "${JOJO_SOURCE_DIR}/src/*.cpp"
  "${JOJO_SOURCE_DIR}/tests/*.h" "${JOJO_SOURCE_DIR}/tests/*.cpp")
list(APPEND _active
  "${JOJO_SOURCE_DIR}/CMakeLists.txt"
  "${JOJO_SOURCE_DIR}/.github/workflows/build.yml")
foreach(_file IN LISTS _active)
  file(READ "${_file}" _text)
  string(TOLOWER "${_text}" _lower)
  if(_lower MATCHES "dreamcast|sh4|segakatana|maple|pvr2|\\.gdi")
    message(FATAL_ERROR "PS1 active-architecture gate failed: ${_file}")
  endif()
endforeach()
```

Do not scan `docs/superpowers/` because historical specs/plans intentionally preserve design history.

Run:

```bash
cmake -DJOJO_SOURCE_DIR=$PWD -P cmake/CheckPs1ActiveArchitecture.cmake
```

Expected: FAIL on current Dreamcast/SH-4 files.

- [ ] **Step 2: Remove old guest/backend source and CMake targets**

Delete every production source listed under `Dreamcast/SH-4 guest-backend files deleted in Task 9` and every old test listed above.

Remove their entries/targets from `CMakeLists.txt`. Keep generic core, revision, ISO/media, PS1 M1, host input/presentation, networking/mod/training components.

In `tests/test_main.cpp`, remove includes, constants, helpers, and test calls that reference `game_backend`, Dreamcast boot data, SH-4, native backend, or old v1 conversion layout. Preserve generic settings/disc/hash/device tests.

In `tests/iso_fixture.h`, remove `install_dreamcast_ip_metadata`, Dreamcast ASCII fields, SH-4 named payloads, and other Dreamcast-only helpers. Keep only neutral ISO helpers still used by `test_iso9660.cpp`/`test_disc_media.cpp`, or migrate those tests to `ps1_fixture.h` and delete `iso_fixture.h` if no references remain.

- [ ] **Step 3: Replace workflow contracts**

Delete workflow steps named:

```text
Native backend manifest contract
Runtime native backend validation contract
USA native backend promotion contract
Unverified base conversion contract
```

Add explicit portable and Windows commands for:

```text
PS1 SYSTEM.CNF contract
PS-X EXE contract
PS1 manifest v2 contract
PS1 transactional install contract
PS1 conversion M1 contract
PS1 runtime installation contract
PS1 active architecture gate
Observed disc revision contract
```

The standard CTest run may already execute the compiled tests; the explicit architecture gate must still run with:

```bash
cmake -DJOJO_SOURCE_DIR=${{ github.workspace }} -P cmake/CheckPs1ActiveArchitecture.cmake
```

- [ ] **Step 4: Run hygiene gate GREEN**

Run the gate. Expected: PASS.

- [ ] **Step 5: Run complete Linux build/test GREEN**

```bash
rm -rf build
cmake -S . -B build -DCMAKE_BUILD_TYPE=Release
cmake --build build --parallel 2
cmake -DJOJO_SOURCE_DIR=$PWD -P cmake/CheckProductionReadiness.cmake
cmake -DJOJO_SOURCE_DIR=$PWD -P cmake/CheckProductionReadinessNegative.cmake
cmake -DJOJO_SOURCE_DIR=$PWD -P cmake/CheckPs1ActiveArchitecture.cmake
ctest --test-dir build --output-on-failure
```

Expected: PASS with no Dreamcast/SH-4 target compiled.

- [ ] **Step 6: Commit cleanup**

```bash
git add -A
git commit -m "refactor: remove Dreamcast SH-4 commercial architecture"
```

---

### Task 10: Correct Current Documentation and Readiness Truth

**Files:**
- Modify: `README.md`
- Modify: `PROJECT-STATE.md`
- Modify: `docs/NEXT-MILESTONES.md`
- Modify: `docs/architecture/PRODUCTION-ROADMAP.md`
- Modify: `docs/architecture/PRODUCTION-READINESS.tsv`
- Modify: `docs/BUILD-WINDOWS.md` only if build/test target names changed materially.

**Interfaces:**
- Consumes: final M0+M1 implementation state.
- Produces: current docs that describe PS1 M1 accurately and do not claim commercial boot/gameplay.

- [ ] **Step 1: Write the exact current-state wording before editing**

The current-state documents must state these facts:

```text
Platform: PlayStation 1
Game scope: JoJo-only
Observed USA whole-image fingerprint: recognized
PS1 media + SYSTEM.CNF + PS-X EXE pipeline: implemented and synthetic-test verified
User-selectable installation root: implemented
Transactional generation activation: implemented
R3000A execution: not implemented in M1
Native x64 codegen: not implemented in M1
Commercial PS-X EXE discovery on the user's real image: awaiting post-build user evidence until a new build is run locally
Boot/render/audio/input/gameplay: not verified
```

- [ ] **Step 2: Update machine-readable readiness**

Replace Dreamcast-era R2 wording with PS1-specific M1 entries. Use evidence fields that point only to commits/CI runs actually produced during execution. Before a real-image M1 run, the commercial-evidence line must remain `blocked-external-evidence` or equivalent; do not mark it verified because synthetic CI passed.

- [ ] **Step 3: Verify no current docs misstate the active platform**

Run:

```bash
grep -RniE "Dreamcast|SH-4|Maple|PVR2|native-ready" README.md PROJECT-STATE.md docs/NEXT-MILESTONES.md docs/architecture/PRODUCTION-ROADMAP.md docs/architecture/PRODUCTION-READINESS.tsv
```

Expected: no active-platform claims remain. Historical notes may mention the migration only if explicitly labeled as superseded history.

- [ ] **Step 4: Commit docs**

```bash
git add README.md PROJECT-STATE.md docs/NEXT-MILESTONES.md docs/architecture/PRODUCTION-ROADMAP.md docs/architecture/PRODUCTION-READINESS.tsv docs/BUILD-WINDOWS.md
git commit -m "docs: mark PS1 foundation truthfully"
```

---

### Task 11: Final Verification, Windows CI, and Commercial-Evidence Handoff

**Files:**
- No production source change unless verification exposes a defect; any defect requires a new RED test before a fix.
- Update: `PROJECT-STATE.md` and `docs/architecture/PRODUCTION-READINESS.tsv` only with actual CI identifiers after successful runs.

**Interfaces:**
- Consumes: Tasks 1–10.
- Produces: CI-backed M0+M1 build and a precise local test request for the user's legally obtained image.

- [ ] **Step 1: Run local/branch verification from a clean build**

```bash
rm -rf build
cmake -S . -B build -DCMAKE_BUILD_TYPE=Release
cmake --build build --parallel 2
cmake -DJOJO_SOURCE_DIR=$PWD -P cmake/CheckProductionReadiness.cmake
cmake -DJOJO_SOURCE_DIR=$PWD -P cmake/CheckProductionReadinessNegative.cmake
cmake -DJOJO_SOURCE_DIR=$PWD -P cmake/CheckPs1ActiveArchitecture.cmake
ctest --test-dir build --output-on-failure
```

Expected: all portable tests pass.

- [ ] **Step 2: Push branch and require GitHub Actions Linux + Windows success**

Windows authority must show:

```text
Configure: success
Build Release: success
Production readiness gate: success
PS1 active architecture gate: success
CTest Release: success
JOJO-Recompiled-Windows-x64 artifact: exactly JOJO-Recompiled.exe
```

- [ ] **Step 3: Record CI evidence without overstating commercial status**

Update current-state docs with the actual workflow run ID and artifact digest. Keep commercial M1 discovery pending until the user runs the Windows artifact against their image.

- [ ] **Step 4: Give the user the M1 real-image test contract**

Ask the user to select the same legally obtained PS1 BIN/CUE and any chosen installation directory. The expected successful progress boundary is:

```text
source recognized
PS1 filesystem opened
SYSTEM.CNF resolved
PS-X EXE validated
local generation installed
manifest v2 activated
R3000A/MIPS analysis pending
```

The user should return only the application log/manifest metadata needed for diagnosis; do not ask them to upload the commercial BIN, executable, BIOS, or extracted proprietary assets.

- [ ] **Step 5: Commercial evidence promotion rule**

Only after a real run demonstrates the M1 sequence may `PROJECT-STATE.md` mark commercial PS-X EXE discovery verified. Even then the following remain unverified and must stay so:

```text
R3000A execution
native x64 codegen
hardware runtime
boot
rendering
audio
input
gameplay
```

- [ ] **Step 6: Commit evidence-only status update if commercial M1 succeeds**

```bash
git add PROJECT-STATE.md docs/architecture/PRODUCTION-READINESS.tsv
git commit -m "docs: record verified JoJo PS1 M1 evidence"
```

If the user's real run fails, do not make this commit; instead use the failure as the next systematic-debugging input.

---

## Milestone Exit Criteria

This M0+M1 plan is complete only when all of the following are true:

1. Linux and Windows CI are green.
2. Active source/build/tests contain no Dreamcast/SH-4/GDI commercial path.
3. `.iso`, `.bin`, `.cue`, raw MODE1/2352, raw MODE2/2352 and CUE data-track tests are green where applicable.
4. Synthetic `SYSTEM.CNF` discovery is green.
5. Synthetic PS-X EXE parsing and full-file hash are green.
6. Manifest v2 truth validation is green.
7. Generation activation preserves the old active install on incomplete conversion.
8. Win32 UI allows choosing and persisting an install root.
9. M1 conversion installs `SYSTEM.CNF` and the validated boot executable locally without requiring the source afterward.
10. Runtime validation verifies the installed executable but explicitly reports R3000A runtime as unavailable.
11. Current docs contain no false Dreamcast/native-ready claims.
12. Commercial-image PS-X EXE discovery remains explicitly pending until the user supplies real execution evidence from their own local copy.

## Follow-On Plan Boundaries

After M1 commercial evidence is obtained, create separate implementation plans from the same architecture spec in this order, each after inspecting evidence produced by the preceding milestone:

1. R3000A reference executor + PS1 memory foundation.
2. MIPS CFG + PS1 IR.
3. Windows x64 native codegen/cache.
4. BIOS/HLE JoJo-only services.
5. GTE/GPU and real rendering checkpoint.
6. CD-ROM/DMA/timers/interrupts.
7. SPU/audio.
8. input + first real boot checkpoint.
9. gameplay completion for this JoJo only.

This sequencing is intentional: hardware work is evidence-driven from the real JoJo execution path rather than generic PS1 compatibility work.