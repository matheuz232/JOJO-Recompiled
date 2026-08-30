# PS1 R1 Media and Revision Intake Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Make the shipping conversion path recognize the supported PlayStation 1 USA `SLUS_010.60` image from `SYSTEM.CNF` + PS-X EXE structure + exact registered fingerprints, with no proprietary game bytes in Git.

**Architecture:** Keep the existing BIN/CUE and ISO9660 layers. Add focused PS1 parsers above them, then register the verified commercial revision in production code. The default `convert_image` path uses the built-in profile and validates the PS1 boot executable; an R1 development probe reuses exactly those production functions so the user's supplied image can be checked outside CI without committing it.

**Tech Stack:** C++20, existing `Result<T>`, ISO9660/media core, CMake/CTest, GitHub Actions Linux + Windows/MSVC.

**Spec:** `docs/superpowers/specs/2026-08-30-ps1-platform-pivot-design.md`

## Global Constraints

- First supported commercial target: PlayStation 1 USA boot executable `SLUS_010.60;1`.
- No proprietary game payload bytes may be committed.
- Unknown or modified revisions fail explicitly; filename-only acceptance is forbidden.
- Resolution, 4:3/16:9/16:10/21:9/32:9 presentation infrastructure and the required real 60 FPS product gate remain preserved.
- `JOJO-Recompiled.exe` must not require caller-supplied test profiles.
- Linux and Windows CI must remain green at the exact verified head.

---

### Task 1: Strict SYSTEM.CNF parser

**Files:**
- Create: `src/core/psx_system_cnf.h`
- Create: `src/core/psx_system_cnf.cpp`
- Create: `tests/test_psx_system_cnf.cpp`
- Modify: `CMakeLists.txt`

**Interfaces:**
- Produces: `struct PsxSystemCnf { std::string boot_iso_path; uint32_t tcb; uint32_t event; uint32_t stack; }`
- Produces: `Result<PsxSystemCnf> parse_psx_system_cnf(std::string_view text)`
- `boot_iso_path` is normalized to an ISO9660 virtual path such as `/SLUS_010.60`; `cdrom:` prefix, backslashes and `;1` are removed safely.

- [ ] **Step 1: Write the failing parser test**

Test the exact structural shape used by the supported disc without copying proprietary payload data:

```cpp
const auto parsed = jojo::parse_psx_system_cnf(
    "BOOT = cdrom:\\SLUS_010.60;1\r\n"
    "TCB = 4\r\n"
    "EVENT = 16\r\n"
    "STACK = 801fff00\r\n");
CHECK(parsed);
CHECK(parsed.value.boot_iso_path == "/SLUS_010.60");
CHECK(parsed.value.tcb == 0x4u);
CHECK(parsed.value.event == 0x16u);
CHECK(parsed.value.stack == 0x801fff00u);
```

Also reject missing `BOOT`, non-`cdrom:` boot targets, parent traversal, duplicate `BOOT`, invalid hex fields and embedded NUL input.

- [ ] **Step 2: Run CI and verify RED**

Expected failure: missing `core/psx_system_cnf.h` / missing parser target implementation.

- [ ] **Step 3: Implement the minimal strict parser**

Parse line-oriented `key=value` text, case-insensitive keys, trim whitespace, accept CRLF/LF, parse TCB/EVENT/STACK as hexadecimal, require exactly one `BOOT`, normalize `cdrom:\\...;1` to a safe `/...` virtual path, reject `..` path components and NUL bytes.

- [ ] **Step 4: Run CI and verify GREEN**

Expected: new parser test passes on Linux and Windows with no new compiler warnings.

- [ ] **Step 5: Commit**

Commit message: `feat: parse PlayStation SYSTEM.CNF`

---

### Task 2: Strict PS-X EXE header parser

**Files:**
- Create: `src/core/psx_exe.h`
- Create: `src/core/psx_exe.cpp`
- Create: `tests/test_psx_exe.cpp`
- Modify: `CMakeLists.txt`

**Interfaces:**
- Produces: `struct PsxExeHeader` with initial PC/GP, load address, payload size, data/BSS fields, stack base and stack offset.
- Produces: `Result<PsxExeHeader> parse_psx_exe(std::span<const std::uint8_t> file)`

- [ ] **Step 1: Write the failing EXE parser test**

Construct a synthetic 0x1000-byte vector with an 0x800-byte header, `PS-X EXE` magic, little-endian fields, load address `0x80010000`, PC `0x8001000c`, payload size `0x800`, and stack base `0x801ffff0`.

Assert the parsed fields. Add separate negative cases for short file, bad magic, non-0x800-aligned payload size, header payload size not matching actual bytes, PC outside the loaded payload, and address overflow.

- [ ] **Step 2: Run CI and verify RED**

Expected failure: missing `core/psx_exe.h` / parser implementation.

- [ ] **Step 3: Implement the minimal parser**

Read all 32-bit header fields little-endian. Require file size at least 0x800, exact `PS-X EXE` magic, payload multiple of 0x800, exact payload-size/file-size agreement, non-overflowing load range, and initial PC inside the loaded payload range.

- [ ] **Step 4: Run CI and verify GREEN**

Expected: parser tests pass on Linux and Windows with no new warnings.

- [ ] **Step 5: Commit**

Commit message: `feat: parse PlayStation PS-X EXE`

---

### Task 3: PS1 boot image analysis through ISO9660

**Files:**
- Create: `src/core/psx_boot.h`
- Create: `src/core/psx_boot.cpp`
- Create: `tests/test_psx_boot.cpp`
- Modify: `tests/iso_fixture.h`
- Modify: `CMakeLists.txt`

**Interfaces:**
- Consumes: `open_iso9660`, `read_iso9660_file`, `parse_psx_system_cnf`, `parse_psx_exe`.
- Produces: `struct PsxBootImage { PsxSystemCnf system; PsxExeHeader executable; std::string executable_path; }`
- Produces: `Result<PsxBootImage> analyze_psx_boot(const Iso9660Image& image)`

- [ ] **Step 1: Extend the synthetic ISO fixture**

Add `write_psx_image(path, boot_name = "SLUS_010.60")` that creates a synthetic ISO9660 image containing `SYSTEM.CNF;1` and a synthetic `PS-X EXE` file. This fixture contains no commercial bytes.

- [ ] **Step 2: Write the failing boot-analysis test**

Open the synthetic image with production `open_iso9660`, call `analyze_psx_boot`, and assert `/SLUS_010.60`, PC/load/payload fields and SYSTEM.CNF values. Negative cases: missing `SYSTEM.CNF`, boot path missing from ISO, malformed SYSTEM.CNF and malformed PS-X EXE.

- [ ] **Step 3: Run CI and verify RED**

Expected failure: missing `core/psx_boot.h` / implementation.

- [ ] **Step 4: Implement boot analysis**

Read `/SYSTEM.CNF`, parse it, read the normalized boot file from ISO9660, parse the PS-X EXE and return the structured result. Propagate exact parser/filesystem errors; do not fall back to guessed executable names.

- [ ] **Step 5: Run CI and verify GREEN**

Expected: the synthetic PS1 boot test passes on Linux and Windows.

- [ ] **Step 6: Commit**

Commit message: `feat: analyze PlayStation boot image`

---

### Task 4: Register SLUS-01060 and wire production conversion

**Files:**
- Create: `src/core/psx_revision.h`
- Create: `src/core/psx_revision.cpp`
- Create: `tests/test_psx_revision.cpp`
- Modify: `src/core/conversion.cpp`
- Modify: `src/core/conversion.h`
- Modify: `tests/test_main.cpp`
- Modify: `CMakeLists.txt`

**Interfaces:**
- Produces: `const std::vector<GameRevisionProfile>& supported_psx_game_revision_profiles()`
- Registered profile ID: `ps1-usa-slus-01060`
- Registered signatures:
  - `/SYSTEM.CNF`, 68 bytes, FNV-1a64 `0x1eb36f6335bbf54a`
  - `/SLUS_010.60`, 565248 bytes, FNV-1a64 `0xb84be235e572adcc`
- Default `convert_image(source, install_dir, callback)` uses this profile automatically.

- [ ] **Step 1: Write failing profile tests**

Assert the built-in profile ID, both paths, sizes and FNV values exactly. Build a filename-correct synthetic PS1 image with different bytes and assert default production conversion rejects it as `unknown_revision` without creating a manifest.

- [ ] **Step 2: Run CI and verify RED**

Expected failure: built-in profile API is missing; default conversion still has no registered revision profiles.

- [ ] **Step 3: Implement built-in profile and conversion wiring**

Before revision matching, call `analyze_psx_boot` so a fingerprint match cannot succeed on a structurally invalid/non-PSX executable. Keep the explicit-options overload for synthetic tests. Change only the no-options overload to populate `supported_psx_game_revision_profiles()`.

After successful match, record `revision_id=ps1-usa-slus-01060`; keep backend explicitly pending until R2.

- [ ] **Step 4: Run CI and verify GREEN**

Expected: all retained tests plus new R1 tests pass on Linux and Windows.

- [ ] **Step 5: Commit**

Commit message: `feat: register supported PS1 US revision`

---

### Task 5: Production-code R1 media probe and real-image evidence

**Files:**
- Create: `tools/r1_media_probe.cpp`
- Modify: `CMakeLists.txt`
- Modify: `.github/workflows/build.yml`
- Create after verification: `docs/superpowers/plans/2026-08-30-ps1-r1-verification.md`

**Interfaces:**
- `jojo_r1_media_probe <cue|bin|iso>` opens the medium with `open_iso9660`, calls `analyze_psx_boot`, calls `identify_game_revision(...supported_psx_game_revision_profiles())`, and prints only non-proprietary metadata: revision ID, boot path, executable size and PS-X EXE header fields.
- Exit 0 only on a supported registered revision.

- [ ] **Step 1: Add the probe target**

The probe is a development utility, not linked into or exposed by the shipping Windows UI.

- [ ] **Step 2: Add Linux CI artifact upload**

Upload only `build/jojo_r1_media_probe` as `JOJO-R1-Media-Probe-Linux`; do not upload any game media.

- [ ] **Step 3: Run exact-head CI**

Require Linux and Windows jobs green. Record test counts and both artifact IDs/digests.

- [ ] **Step 4: Download/materialize the Linux probe artifact**

Run the exact CI-built probe against the extracted CUE/BIN from the user's supplied `JoJos Bizarre Adventure (USA).zip` in the working container.

Expected production-code result:

```text
revision=ps1-usa-slus-01060
boot=/SLUS_010.60
pc=0x8001000c
load=0x80010000
payload=0x89800
```

The probe may print additional non-proprietary header metadata, but must never dump commercial payload bytes.

- [ ] **Step 5: Record R1 verification**

Record exact commit, workflow run/jobs, test counts, artifact digest, local probe exit code and observed metadata. Explicitly state that R1 proves intake/revision only and does not prove game boot/playability.

- [ ] **Step 6: Commit verification evidence**

Commit message: `docs: record PS1 R1 verification`
