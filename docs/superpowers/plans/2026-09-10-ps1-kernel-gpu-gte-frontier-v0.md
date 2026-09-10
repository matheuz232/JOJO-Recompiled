# PS1 Kernel BIOS + GP1 + GTE Transfer Frontier v0 Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Advance the commercial JoJo PS1 boot path past B0/18, GP1 `0x1F801814`, B0/56, A0/44, and the observed `CTC2 $t0,$29` frontier without speculative production behavior.

**Architecture:** Keep BIOS semantics in `Ps1HleBios`, introduce a focused `Ps1GpuState` owned by `Ps1MemoryBus`, and extend `R3000aState` with the 32 GTE control registers. Implement only CTC2/CFC2 transfers; MFC2/MTC2 and GTE math commands remain explicit boundaries. All new guest-visible state participates in MAX3 fingerprints.

**Tech Stack:** C++20, CMake 3.20+, portable reference R3000A interpreter, GitHub Actions Linux + Windows x64/MSVC 2022.

**Spec:** `docs/superpowers/specs/2026-09-10-ps1-kernel-gpu-gte-frontier-v0-design.md`

## Global Constraints

- Do not add Sony BIOS ROM bytes, game data, or other proprietary payloads.
- Preserve strict production behavior for unsupported BIOS, GP1, and COP2 operations.
- MAX3 diagnostic MMIO speculation must never absorb a known-but-unsupported GP1 command.
- A0/B0/C0 return through `$ra`; SYS semantics remain separate.
- CU2-disabled COP2 operations continue to raise Coprocessor Unusable with CE=2.
- MFC2/MTC2 and GTE math remain `cop2_unimplemented` after this milestone.
- Completion requires fresh Linux and Windows/MSVC CI on the exact final SHA and a verified Windows artifact.

---

### Task 1: Kernel BIOS guest-memory services

**Files:**
- Modify: `src/core/ps1_hle_bios.h`
- Modify: `src/core/ps1_hle_bios.cpp`
- Modify: `src/core/ps1_boot_runtime.cpp`
- Test: `tests/test_ps1_hle_bios.cpp`

**Interfaces:**
- Consumes: `Ps1MemoryBus::read32/write32`, `R3000aState`, existing `Ps1HleBiosCall`.
- Produces: `Ps1HleBios::dispatch(const Ps1HleBiosCall&, R3000aState&, Ps1MemoryBus&) noexcept`, guest-visible C0 table state, default EntryInt state.

- [ ] **Step 1: Write failing kernel HLE tests**

Add direct tests for A0/44, B0/18, and B0/56. Representative assertions:

```cpp
jojo::Ps1MemoryBus bus;
jojo::Ps1HleBios bios;
jojo::R3000aState cpu{};
cpu.gpr[2] = 0x12345678u;
cpu.gpr[31] = 0x80011000u;

const jojo::Ps1HleBiosCall flush{
    jojo::Ps1HleBiosDomain::a0, 0x44u, 0xA0u,
    0u, 0u, 0u, 0u, cpu.gpr[31]};
CHECK(bios.dispatch(flush, cpu, bus).disposition == jojo::Ps1HleBiosDisposition::handled);
CHECK(cpu.gpr[2] == 0x12345678u);
CHECK(cpu.pc == 0x80011000u);

cpu.gpr[31] = 0x80012000u;
const jojo::Ps1HleBiosCall reset_entry{
    jojo::Ps1HleBiosDomain::b0, 0x18u, 0xB0u,
    0u, 0u, 0u, 0u, cpu.gpr[31]};
CHECK(bios.dispatch(reset_entry, cpu, bus).disposition == jojo::Ps1HleBiosDisposition::handled);
CHECK(cpu.gpr[2] == 0x00006CF4u);
CHECK(bus.read32(0x00006CF4u).value == 0x00000F40u);
CHECK(bus.read32(0x00006CF8u).value == 0x000085D4u);
CHECK(bios.interrupt_hook_address().value_or(0u) == 0x00006CF4u);

CHECK(bus.write32(0x00006CF4u, 0xDEADBEEFu).status == jojo::R3000aBusStatus::ok);
CHECK(bios.dispatch(reset_entry, cpu, bus).disposition == jojo::Ps1HleBiosDisposition::handled);
CHECK(bus.read32(0x00006CF4u).value == 0x00000F40u);

const jojo::Ps1HleBiosCall get_c0{
    jojo::Ps1HleBiosDomain::b0, 0x56u, 0xB0u,
    0u, 0u, 0u, 0u, cpu.gpr[31]};
CHECK(bios.dispatch(get_c0, cpu, bus).disposition == jojo::Ps1HleBiosDisposition::handled);
CHECK(cpu.gpr[2] == 0x00000674u);
CHECK(bus.read32(0x00000674u + 6u * 4u).value == 0x00000C80u);
CHECK(bus.write32(0x00000674u + 6u * 4u, 0x80012340u).status == jojo::R3000aBusStatus::ok);
CHECK(bios.dispatch(get_c0, cpu, bus).disposition == jojo::Ps1HleBiosDisposition::handled);
CHECK(bus.read32(0x00000674u + 6u * 4u).value == 0x80012340u);
```

- [ ] **Step 2: Run RED**

Run through CI target `jojo_ps1_hle_bios_tests` after committing only the test/API expectation. Expected: compile failure because the three-argument `dispatch(..., Ps1MemoryBus&)` does not exist, or behavioral failure for A0/44/B0/18/B0/56.

- [ ] **Step 3: Implement the bus-aware HLE contract and services**

In `ps1_hle_bios.h`:

```cpp
class Ps1MemoryBus;

[[nodiscard]] Ps1HleBiosResult dispatch(
    const Ps1HleBiosCall& call,
    R3000aState& cpu,
    Ps1MemoryBus& bus) noexcept;
```

Add:

```cpp
bool c0_table_materialized_{};
```

In `ps1_hle_bios.cpp`, use these constants:

```cpp
constexpr std::uint32_t kC0Table = 0x00000674u;
constexpr std::uint32_t kC0ExceptionEntry = 0x00000C80u;
constexpr std::uint32_t kDefaultEntryInt = 0x00006CF4u;
constexpr std::uint32_t kReturnFromException = 0x00000F40u;
constexpr std::uint32_t kKernelSavedSp = 0x000085D4u;
```

Implement A0/44 as `return_from_bios_vector(cpu)` preserving `$v0`. For B0/18, write twelve 32-bit words starting at `kDefaultEntryInt`, with word 0 `kReturnFromException`, word 1 `kKernelSavedSp`, and words 2..11 zero; set `interrupt_hook_address_`, set `$v0`, and return through `$ra`. For B0/56, initialize 0x1E words at `kC0Table` only the first time, set entry 6 to `kC0ExceptionEntry`, remember `c0_table_materialized_`, then return `kC0Table` in `$v0`.

If any bus write fails, return `Ps1HleBiosDisposition::terminal` without returning through `$ra`.

Update every runtime dispatch call to pass `bus_`.

- [ ] **Step 4: Extend the HLE hash**

Include `c0_table_materialized_` in `diagnostic_state_hash()`:

```cpp
hash_bool(hash, c0_table_materialized_);
```

Guest RAM bytes are already covered by `Ps1MemoryBus::diagnostic_state_hash()`.

- [ ] **Step 5: Run GREEN and commit**

Run the full Linux test suite. Expected: all existing tests plus `jojo_ps1_hle_bios_tests` pass. Commit:

```text
feat: implement PS1 kernel HLE frontier services
```

---

### Task 2: GP1 state component and strict MMIO routing

**Files:**
- Create: `src/core/ps1_gpu_state.h`
- Create: `src/core/ps1_gpu_state.cpp`
- Modify: `src/core/ps1_memory_bus.h`
- Modify: `src/core/ps1_memory_bus.cpp`
- Modify: `CMakeLists.txt`
- Test: `tests/test_ps1_gpu_state.cpp`
- Test: `tests/test_ps1_memory_bus.cpp`

**Interfaces:**
- Produces: `Ps1GpuState::write_gp1(std::uint32_t)`, `gpu_stat()`, `gp1_command_count()`, `diagnostic_state_hash()`.
- `Ps1MemoryBus` exposes `gpu()` accessors for reporting/tests.

- [ ] **Step 1: Write RED tests for GP1 reset and command routing**

Create `tests/test_ps1_gpu_state.cpp` with assertions equivalent to:

```cpp
jojo::Ps1GpuState gpu;
CHECK(gpu.gpu_stat() == 0x14802000u);
CHECK(gpu.gp1_command_count() == 0u);
CHECK(gpu.write_gp1(0x03000000u));
CHECK(!gpu.display_disabled());
CHECK(gpu.gp1_command_count() == 1u);
CHECK(gpu.write_gp1(0x04000002u));
CHECK(gpu.dma_direction() == 2u);
CHECK(gpu.write_gp1(0x05012345u));
CHECK(gpu.write_gp1(0x060C0200u));
CHECK(gpu.write_gp1(0x0703C010u));
CHECK(gpu.write_gp1(0x08000000u));
CHECK(!gpu.write_gp1(0x09000000u));
```

Extend memory-bus tests:

```cpp
jojo::Ps1MemoryBus bus;
bus.set_diagnostic_mmio_probe_enabled(true);
bus.clear_last_diagnostic_mmio_probe();
CHECK(bus.write32(0x1F801814u, 0x00000000u).status == jojo::R3000aBusStatus::ok);
CHECK(!bus.last_diagnostic_mmio_probe().has_value());
CHECK(bus.read32(0x1F801814u).status == jojo::R3000aBusStatus::ok);
CHECK(bus.read32(0x1F801814u).value == 0x14802000u);

bus.clear_last_diagnostic_mmio_probe();
CHECK(bus.write32(0x1F801814u, 0x09000000u).status == jojo::R3000aBusStatus::unsupported);
CHECK(!bus.last_diagnostic_mmio_probe().has_value());
```

- [ ] **Step 2: Run RED**

Expected: missing `Ps1GpuState`/`gpu()` symbols.

- [ ] **Step 3: Implement `Ps1GpuState`**

The header owns explicit fields:

```cpp
class Ps1GpuState {
public:
    [[nodiscard]] bool write_gp1(std::uint32_t value) noexcept;
    [[nodiscard]] std::uint32_t gpu_stat() const noexcept;
    [[nodiscard]] std::uint64_t diagnostic_state_hash() const noexcept;
    [[nodiscard]] std::uint64_t gp1_command_count() const noexcept;
    [[nodiscard]] bool display_disabled() const noexcept;
    [[nodiscard]] std::uint8_t dma_direction() const noexcept;
private:
    bool display_disabled_{true};
    std::uint8_t dma_direction_{};
    std::uint16_t display_vram_x_{};
    std::uint16_t display_vram_y_{};
    std::uint16_t horizontal_start_{0x200u};
    std::uint16_t horizontal_end_{0xC00u};
    std::uint16_t vertical_start_{0x010u};
    std::uint16_t vertical_end_{0x100u};
    std::uint8_t display_mode_{};
    bool irq1_{};
    std::uint64_t gp1_command_count_{};
    std::uint64_t command_buffer_reset_count_{};
};
```

Implement only GP1 00..08. GP1(00) restores reset state. GP1(01) increments command-buffer reset count. GP1(02) clears IRQ. GP1(03..08) update the documented fields. Unsupported opcodes return `false` and do not mutate state or increment the supported-command counter.

- [ ] **Step 4: Route `0x1F801814` before the diagnostic shadow**

In `Ps1MemoryBus::read32`, return `gpu_.gpu_stat()` for the GP1 address. In `write32`, call `gpu_.write_gp1(value)` and return `unsupported` for a rejected command without falling through to diagnostic shadow. Add:

```cpp
[[nodiscard]] Ps1GpuState& gpu() noexcept;
[[nodiscard]] const Ps1GpuState& gpu() const noexcept;
```

Include `gpu_.diagnostic_state_hash()` in the bus hash.

- [ ] **Step 5: Wire CMake, run GREEN, commit**

Add `src/core/ps1_gpu_state.cpp` and `jojo_ps1_gpu_state_tests`. Run full Linux tests. Commit:

```text
feat: implement PS1 GP1 control state
```

---

### Task 3: Real GP1 accounting in boot reports

**Files:**
- Modify: `src/core/ps1_boot_runtime.cpp`
- Test: `tests/test_ps1_boot_runtime.cpp`
- Test: `tests/test_ps1_max3_explorer.cpp`

**Interfaces:**
- Consumes: `Ps1MemoryBus::gpu().gp1_command_count()`.
- Produces: `Ps1BootReport::gpu_gp1_command_count` as per-run delta.

- [ ] **Step 1: Write RED tests**

Use a synthetic program that stores two supported commands to `0x1F801814` and loops. Assert:

```cpp
CHECK(report.gpu_gp1_command_count == 2u);
CHECK(report.presented_frames == 0u);
CHECK(report.vram_write_count == 0u);
CHECK(report.speculative_mmio_count == 0u);
```

Add a MAX3 ranking regression where one path reaches one extra GP1 command and therefore outranks an otherwise equal path.

- [ ] **Step 2: Run RED**

Expected: report counter remains zero.

- [ ] **Step 3: Implement per-run delta accounting**

At the start of `Ps1BootRuntime::run` capture:

```cpp
const auto gp1_before = bus_.gpu().gp1_command_count();
```

Before every return from `run`, set:

```cpp
report.gpu_gp1_command_count = bus_.gpu().gp1_command_count() - gp1_before;
```

Use a small local finalizer helper/lambda to avoid missing return paths.

- [ ] **Step 4: Run GREEN and commit**

Commit:

```text
feat: report real PS1 GP1 command progress
```

---

### Task 4: GTE control-register state and CTC2

**Files:**
- Modify: `src/core/r3000a_state.h`
- Modify: `src/core/r3000a_reference_executor.cpp`
- Test: `tests/test_r3000a_boundary.cpp`
- Test: `tests/test_r3000a_cop2_control.cpp`
- Modify: `CMakeLists.txt`

**Interfaces:**
- Produces: `R3000aCop2Gte::control[32]` and architectural CTC2 execution.

- [ ] **Step 1: Write RED CTC2 tests**

Create helpers:

```cpp
constexpr std::uint32_t ctc2(std::uint8_t rt, std::uint8_t rd) noexcept {
    return (0x12u << 26) | (0x06u << 21) |
           (std::uint32_t(rt) << 16) | (std::uint32_t(rd) << 11);
}
```

Test CU2 clear still raises CpU. With CU2 set, test:

```cpp
s.gpr[8] = 0x00000155u;
bus.store32(0x1000u, ctc2(8u, 29u));
CHECK(jojo::step_r3000a(s, bus).status == jojo::R3000aStepStatus::retired);
CHECK(s.cop2_gte.control[29] == 0x00000155u);
CHECK(s.pc == 0x1004u);
```

Also test canonical sign extension for rd 4/12/20/27/29/30, low-16 storage for H(26), raw 32-bit storage classes, and FLAG write masking.

- [ ] **Step 2: Run RED**

Expected: missing `cop2_gte` and CTC2 still returns `cop2_unimplemented`.

- [ ] **Step 3: Add GTE control state**

In `r3000a_state.h`:

```cpp
struct R3000aCop2Gte {
    std::array<std::uint32_t, 32> control{};
};

struct R3000aState {
    // existing fields...
    R3000aCop2Gte cop2_gte{};
};
```

- [ ] **Step 4: Implement CTC2 normalization**

Add helpers:

```cpp
std::uint32_t sign_extend_low16(std::uint32_t v) noexcept {
    const auto lo = static_cast<std::uint16_t>(v);
    return (lo & 0x8000u) ? (0xFFFF0000u | lo) : lo;
}

std::uint32_t normalize_gte_control_write(std::uint8_t rd, std::uint32_t value) noexcept;
```

Rules must match the spec exactly. FLAG(31) stores only bits 30..12 and derives bit31 from bits 30..23 or 18..13.

Split the existing grouped COP2 switch: CTC2 executes when CU2 is enabled; MFC2/MTC2/COP2 command remain boundary.

- [ ] **Step 5: Run GREEN and commit**

Commit:

```text
feat: implement PS1 GTE CTC2 control transfers
```

---

### Task 5: CFC2 delayed loads and control-register read rules

**Files:**
- Modify: `src/core/r3000a_reference_executor.cpp`
- Test: `tests/test_r3000a_cop2_control.cpp`

**Interfaces:**
- Consumes: `R3000aState::cop2_gte.control`.
- Produces: architectural CFC2 delayed GPR load.

- [ ] **Step 1: Write RED CFC2 tests**

Helper:

```cpp
constexpr std::uint32_t cfc2(std::uint8_t rt, std::uint8_t rd) noexcept {
    return (0x12u << 26) | (0x02u << 21) |
           (std::uint32_t(rt) << 16) | (std::uint32_t(rd) << 11);
}
```

Assert one-instruction delay:

```cpp
s.cop0.status |= 1u << 30;
s.cop2_gte.control[29] = 0xFFFF8001u;
bus.store32(0x1000u, cfc2(8u, 29u));
bus.store32(0x1004u, 0u);
CHECK(jojo::step_r3000a(s, bus).status == jojo::R3000aStepStatus::retired);
CHECK(s.gpr[8] != 0xFFFF8001u);
CHECK(s.pending_load.valid && s.pending_load.reg == 8u);
CHECK(jojo::step_r3000a(s, bus).status == jojo::R3000aStepStatus::retired);
CHECK(s.gpr[8] == 0xFFFF8001u);
```

Test H(26) read sign-extension bug and FLAG(31) derived bit31/bits11..0 zero.

- [ ] **Step 2: Run RED**

Expected: CFC2 remains boundary.

- [ ] **Step 3: Implement normalized CFC2 reads**

Add:

```cpp
std::uint32_t read_gte_control(const R3000aState& state, std::uint8_t rd) noexcept;
```

Use the existing `queue_load()` path so delayed-load collision behavior remains shared with ordinary loads. Do not direct-write the GPR.

- [ ] **Step 4: Update boundary regression**

Change `tests/test_r3000a_boundary.cpp` so with CU2 enabled, only MFC2, MTC2, and COP2 command are expected to remain `cop2_unimplemented`; CTC2/CFC2 must retire.

- [ ] **Step 5: Run GREEN and commit**

Commit:

```text
feat: implement PS1 GTE CFC2 delayed loads
```

---

### Task 6: MAX3 fingerprint and observed commercial regression

**Files:**
- Modify: `src/core/ps1_boot_runtime.cpp`
- Test: `tests/test_ps1_diagnostic_frontier.cpp`
- Test: `tests/test_ps1_max3_explorer.cpp`

**Interfaces:**
- Consumes: GPU bus hash, GTE control state.
- Produces: distinct MAX3 hashes for distinct GTE control-register states and a regression that passes the observed `CTC2 cnt29` sequence.

- [ ] **Step 1: Write RED fingerprint test**

Clone two identical runtimes. Mutate GTE control register 29 through real guest execution in only one and assert:

```cpp
CHECK(mutated.diagnostic_state_hash() != baseline.diagnostic_state_hash());
```

- [ ] **Step 2: Write observed-trace regression**

Synthetic program:

```cpp
// Status.CU2 already enabled in the fixture/runtime state.
addiu $t0,$zero,0x155
ctc2  $t0,$29
addiu $s0,$zero,0x1234
```

Assert execution retires beyond the former COP2 boundary and ZSF3 equals `0x155`.

- [ ] **Step 3: Run RED**

If the fingerprint test already passes because the whole CPU struct is explicitly hashed after Task 4, document it as a characterization pass and do not introduce fake production changes. The observed-trace regression must pass after Tasks 4/5.

- [ ] **Step 4: Extend runtime hash if required**

If needed, append all 32 control registers explicitly:

```cpp
for (const auto value : cpu_.cop2_gte.control) hash_u32(hash, value);
```

- [ ] **Step 5: Full GREEN and commit only if production/test changes remain**

Commit if needed:

```text
feat: include PS1 GTE state in MAX3 fingerprints
```

---

### Task 7: Final audit, CI, and Windows artifact

**Files:**
- No production files unless verification exposes a defect.

**Interfaces:**
- Produces: exact final SHA, green Linux/Windows evidence, verified ZIP.

- [ ] **Step 1: Audit diff against baseline `c83c20b823c6e54e0db6102c8f9fdd57329c6e99`**

Verify changed files are limited to the approved BIOS/GP1/GTE/test/docs scope and that no `.bin`, `.iso`, BIOS dump, game asset, or proprietary payload was introduced.

- [ ] **Step 2: Run full Linux gate**

Require configure, build, production readiness, PS1 architecture gate, all tests, observed-disc contract, and UDP contract to complete successfully on the exact candidate SHA.

- [ ] **Step 3: Run full Windows/MSVC gate**

Require Release build, readiness gate, PS1 architecture gate, all tests, observed-disc contract, UDP contract, and executable upload to complete successfully on the same SHA.

- [ ] **Step 4: Inspect final test count and failures**

Read CI logs/jobs. Completion requires zero failing tests on both platforms.

- [ ] **Step 5: Download and verify artifact**

Fetch the `JOJO-Recompiled-Windows-x64` artifact for the final run. Download it as `JOJO-Recompiled-Kernel-GP1-GTE-v0-Windows-x64.zip`, compute SHA-256 locally, compare to GitHub artifact digest, and inspect the ZIP for `JOJO-Recompiled.exe`.

- [ ] **Step 6: Deliver next checkpoint build**

Report branch, final SHA, workflow/run, test status, artifact digest, and a sandbox download link. Ask for one new MAX3 report generated by that exact executable.
