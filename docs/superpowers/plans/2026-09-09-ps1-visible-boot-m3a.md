# JOJO PS1 Visible Boot M3A Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Build the JoJo-only PS1 memory bus, verified PS-X EXE loader, and deterministic reference-CPU boot checkpoint loop that executes installed JoJo code until the first explicit BIOS/MMIO/CPU boundary or instruction budget.

**Architecture:** M3A plugs a concrete `Ps1MemoryBus` into the verified M2 `R3000a` reference executor, loads only the already-validated installed `boot.psxexe`, and runs it through a `Ps1BootRuntime`. The runtime emits a derived `Ps1BootReport`; it does not implement BIOS services or devices yet and must stop explicitly when JoJo reaches them.

**Tech Stack:** C++20, CMake 3.20+, CTest, GCC/Clang-compatible portable core on Linux, MSVC 2022 on Windows, existing `jojo_core` static library and GitHub Actions workflow.

**Spec:** `docs/superpowers/specs/2026-09-09-ps1-visible-boot-m3-design.md`

## Global Constraints

- Product scope is **JoJo PS1 only**. Do not add support, compatibility logic, tests, profiles, or fallback behavior for unrelated games.
- M3A uses the M2 R3000A reference executor. Do not add CFG/IR or Windows x64 native R3000A code generation.
- Main RAM is exactly 2 MiB; scratchpad is exactly 1 KiB.
- Initial accepted RAM aliases are `0x00000000..0x001FFFFF`, `0x80000000..0x801FFFFF`, and `0xA0000000..0xA01FFFFF`. Do not silently add additional RAM mirrors.
- Scratchpad physical range is `0x1F800000..0x1F8003FF`, with KSEG0/KSEG1 aliases resolving through the same architectural physical mask.
- Unknown/not-yet-implemented addresses return explicit unsupported results. Reads must not fabricate zero and writes must not fabricate success.
- The PS-X EXE payload source is exactly offset `0x800`, length `metadata.text_size`, destination `metadata.text_load_address` after supported address translation.
- No proprietary PlayStation BIOS bytes, commercial JoJo bytes, raw sectors, or unrestricted guest-memory dumps may enter Git or CI.
- Commercial execution is local-only evidence. CI uses synthetic fixtures only.
- Instruction-budget exhaustion is a stop reason, never a boot-success claim.
- M3A does not claim commercial boot, rendering, audio, input, gameplay, or native recompilation.
- Do not mutate the active installation manifest during a boot checkpoint.
- Every task follows TDD RED→GREEN and ends with a dedicated commit. Linux and Windows CI remain final verification authorities.

---

## File Structure

Create these focused production files:

- `src/core/ps1_memory_bus.h/.cpp` — 2 MiB RAM, 1 KiB scratchpad, JoJo-required initial address translation, little-endian bus operations, unsupported-access evidence, loader block copy.
- `src/core/ps1_executable_loader.h/.cpp` — validates and copies the PS-X EXE payload to main RAM and returns the initialized M2 `R3000aState`.
- `src/core/ps1_boot_report.h` — stable M3 boot stop reasons and derived report structures.
- `src/core/ps1_boot_runtime.h/.cpp` — owns `Ps1MemoryBus`, R3000A state and instruction-budget loop; classifies BIOS-entry and MMIO boundaries.

Modify:

- `src/core/runtime.h/.cpp` — add installation-backed checkpoint entry point while preserving the existing product `bootstrap_runtime` wrapper and non-mutation contract.
- `CMakeLists.txt` — compile new core sources and register new synthetic tests.
- `PROJECT-STATE.md`, `docs/NEXT-MILESTONES.md`, `docs/architecture/PRODUCTION-READINESS.tsv` — update truth only after code-head Linux+Windows CI succeeds.

Create tests:

- `tests/test_ps1_memory_bus.cpp`
- `tests/test_ps1_executable_loader.cpp`
- `tests/test_ps1_boot_runtime.cpp`

Modify test support only where needed:

- `tests/test_ps1_runtime_installation.cpp`
- `tests/ps1_fixture.h`

---

### Task 1: JoJo PS1 Main RAM, Scratchpad, and Address Translation

**Files:**
- Create: `src/core/ps1_memory_bus.h`
- Create: `src/core/ps1_memory_bus.cpp`
- Create: `tests/test_ps1_memory_bus.cpp`
- Modify: `CMakeLists.txt`

**Interfaces:**
- Consumes: `jojo::R3000aBus`, `jojo::R3000aBusResult`, `jojo::R3000aBusStatus`, `jojo::Result<void>`.
- Produces:

```cpp
namespace jojo {

struct Ps1UnsupportedAccess {
    std::uint32_t guest_address{};
    std::uint32_t physical_address{};
    std::uint8_t width{};
    bool write{};
    std::uint32_t value{};
};

class Ps1MemoryBus final : public R3000aBus {
public:
    static constexpr std::uint32_t main_ram_size = 2u * 1024u * 1024u;
    static constexpr std::uint32_t scratchpad_base = 0x1F800000u;
    static constexpr std::uint32_t scratchpad_size = 1024u;

    static std::optional<std::uint32_t> guest_to_physical(std::uint32_t guest) noexcept;

    R3000aBusResult read8(std::uint32_t address) noexcept override;
    R3000aBusResult read16(std::uint32_t address) noexcept override;
    R3000aBusResult read32(std::uint32_t address) noexcept override;
    R3000aBusResult write8(std::uint32_t address, std::uint8_t value) noexcept override;
    R3000aBusResult write16(std::uint32_t address, std::uint16_t value) noexcept override;
    R3000aBusResult write32(std::uint32_t address, std::uint32_t value) noexcept override;

    Result<void> load_main_ram(std::uint32_t guest_address,
                               std::span<const std::uint8_t> bytes);
    const std::optional<Ps1UnsupportedAccess>& last_unsupported_access() const noexcept;
    void clear_last_unsupported_access() noexcept;

private:
    std::array<std::uint8_t, main_ram_size> main_ram_{};
    std::array<std::uint8_t, scratchpad_size> scratchpad_{};
    std::optional<Ps1UnsupportedAccess> last_unsupported_{};
};

}
```

- [ ] **Step 1: Write the failing memory-bus contract**

Add `tests/test_ps1_memory_bus.cpp` with explicit checks for alias coherence, little-endian access, scratchpad isolation, KSEG scratchpad aliases, and unsupported addresses:

```cpp
#include "core/ps1_memory_bus.h"
#include <iostream>

static int failures = 0;
#define CHECK(x) do { if (!(x)) { std::cerr << __FILE__ << ':' << __LINE__ << " CHECK failed: " #x "\n"; ++failures; } } while (0)

int main() {
    jojo::Ps1MemoryBus bus;

    CHECK(bus.write32(0x00000100u, 0x44332211u).status == jojo::R3000aBusStatus::ok);
    CHECK(bus.read8(0x80000100u).value == 0x11u);
    CHECK(bus.read8(0xA0000101u).value == 0x22u);
    CHECK(bus.read16(0x00000102u).value == 0x4433u);
    CHECK(bus.read32(0x80000100u).value == 0x44332211u);

    CHECK(bus.write16(0x1F800010u, 0xBBAAu).status == jojo::R3000aBusStatus::ok);
    CHECK(bus.read16(0x9F800010u).value == 0xBBAAu);
    CHECK(bus.read16(0xBF800010u).value == 0xBBAAu);
    CHECK(bus.read16(0x00000010u).value != 0xBBAAu);

    const auto unsupported = bus.read32(0x1F801070u);
    CHECK(unsupported.status == jojo::R3000aBusStatus::unsupported);
    CHECK(bus.last_unsupported_access().has_value());
    if (bus.last_unsupported_access()) {
        CHECK(bus.last_unsupported_access()->guest_address == 0x1F801070u);
        CHECK(bus.last_unsupported_access()->physical_address == 0x1F801070u);
        CHECK(bus.last_unsupported_access()->width == 4u);
        CHECK(!bus.last_unsupported_access()->write);
    }

    CHECK(bus.read32(0x00200000u).status == jojo::R3000aBusStatus::unsupported);
    CHECK(bus.read32(0xC0000000u).status == jojo::R3000aBusStatus::unsupported);

    return failures ? 1 : 0;
}
```

Add to `CMakeLists.txt`:

```cmake
add_jojo_test(jojo_ps1_memory_bus_tests tests/test_ps1_memory_bus.cpp)
```

- [ ] **Step 2: Run RED**

Run:

```bash
cmake -S . -B build -DCMAKE_BUILD_TYPE=Release
cmake --build build --target jojo_ps1_memory_bus_tests --config Release
```

Expected: build fails because `core/ps1_memory_bus.h` does not exist yet.

- [ ] **Step 3: Implement address normalization and storage**

Implement `guest_to_physical` with these exact rules:

```cpp
std::optional<std::uint32_t> Ps1MemoryBus::guest_to_physical(std::uint32_t guest) noexcept {
    if (guest < 0x80000000u) return guest;
    if (guest < 0xC0000000u) return guest & 0x1FFFFFFFu;
    return std::nullopt;
}
```

For each read/write, resolve the guest address, accept only a range wholly inside main RAM or scratchpad, and encode/decode little-endian bytes explicitly. On failure set `last_unsupported_` using the original guest address, translated physical address when translation exists, requested width, write flag and write value; return `{R3000aBusStatus::unsupported, 0}`.

Implement loader block copy so the entire translated range must fit main RAM before any byte changes:

```cpp
Result<void> Ps1MemoryBus::load_main_ram(
    std::uint32_t guest_address,
    std::span<const std::uint8_t> bytes) {
    const auto physical = guest_to_physical(guest_address);
    if (!physical || *physical >= main_ram_size ||
        bytes.size() > static_cast<std::size_t>(main_ram_size - *physical)) {
        return Result<void>::failure(ErrorCode::invalid_argument,
                                     "PS-X EXE payload destination is outside JoJo main RAM");
    }
    std::copy(bytes.begin(), bytes.end(), main_ram_.begin() + *physical);
    return Result<void>::success();
}
```

Add `src/core/ps1_memory_bus.cpp` to `jojo_core`.

- [ ] **Step 4: Run GREEN and regression suite**

Run:

```bash
cmake --build build --target jojo_ps1_memory_bus_tests --config Release
ctest --test-dir build -C Release --output-on-failure -R "jojo_ps1_memory_bus_tests|jojo_r3000a_memory_tests|jojo_r3000a_unaligned_tests"
```

Expected: all selected tests pass.

- [ ] **Step 5: Commit**

```bash
git add CMakeLists.txt src/core/ps1_memory_bus.h src/core/ps1_memory_bus.cpp tests/test_ps1_memory_bus.cpp
git commit -m "feat: add JoJo PS1 memory bus"
```

---

### Task 2: Verified PS-X EXE Payload Loader

**Files:**
- Create: `src/core/ps1_executable_loader.h`
- Create: `src/core/ps1_executable_loader.cpp`
- Create: `tests/test_ps1_executable_loader.cpp`
- Modify: `CMakeLists.txt`

**Interfaces:**
- Consumes: `Ps1Executable`, `Ps1MemoryBus`, `initialize_r3000a_for_psx_exe(const Ps1ExeMetadata&)`.
- Produces:

```cpp
[[nodiscard]] Result<R3000aState> load_ps1_executable_into_bus(
    Ps1MemoryBus& bus,
    const Ps1Executable& executable);
```

- [ ] **Step 1: Write failing loader tests**

Create tests that parse `test_ps1::make_psx_exe()`, load it, then assert payload bytes and CPU initialization:

```cpp
auto parsed = jojo::parse_ps1_executable(test_ps1::make_psx_exe());
CHECK(parsed);
jojo::Ps1MemoryBus bus;
auto loaded = jojo::load_ps1_executable_into_bus(bus, parsed.value);
CHECK(loaded);
if (loaded) {
    CHECK(loaded.value.pc == 0x80010000u);
    CHECK(loaded.value.next_pc == 0x80010004u);
    CHECK(loaded.value.gpr[28] == 0x80018000u);
    CHECK(loaded.value.gpr[29] == 0x80200000u);
    CHECK(bus.read32(0x80010000u).value == 0x00000000u);
    CHECK(bus.read32(0x80010004u).value == 0x24080001u);
    CHECK(bus.read32(0x00010008u).value == 0x24090002u);
}
```

Add explicit rejection cases by copying a valid `Ps1Executable` and modifying only metadata/file size:

```cpp
auto truncated = parsed.value;
truncated.file_bytes.resize(0x800u + truncated.metadata.text_size - 1u);
CHECK(!jojo::load_ps1_executable_into_bus(bus, truncated));

auto outside = parsed.value;
outside.metadata.text_load_address = 0x801FFFFCu;
outside.metadata.text_size = 8u;
outside.file_bytes.resize(0x808u, 0u);
CHECK(!jojo::load_ps1_executable_into_bus(bus, outside));

auto wraps = parsed.value;
wraps.metadata.text_load_address = 0xFFFFFFFCu;
wraps.metadata.text_size = 8u;
wraps.file_bytes.resize(0x808u, 0u);
CHECK(!jojo::load_ps1_executable_into_bus(bus, wraps));
```

Register:

```cmake
add_jojo_test(jojo_ps1_executable_loader_tests tests/test_ps1_executable_loader.cpp)
```

- [ ] **Step 2: Run RED**

Run:

```bash
cmake --build build --target jojo_ps1_executable_loader_tests --config Release
```

Expected: build fails because `load_ps1_executable_into_bus` is absent.

- [ ] **Step 3: Implement exact, transactional payload placement**

Implement the pre-copy checks before calling `load_main_ram`:

```cpp
Result<R3000aState> load_ps1_executable_into_bus(
    Ps1MemoryBus& bus,
    const Ps1Executable& executable) {
    constexpr std::size_t header_size = 0x800u;
    const auto text_size = static_cast<std::size_t>(executable.metadata.text_size);
    if (executable.file_bytes.size() < header_size ||
        text_size > executable.file_bytes.size() - header_size) {
        return Result<R3000aState>::failure(
            ErrorCode::invalid_installation,
            "validated PS-X EXE payload is truncated");
    }
    if (executable.metadata.text_size != 0u &&
        executable.metadata.text_load_address >
            0xFFFFFFFFu - (executable.metadata.text_size - 1u)) {
        return Result<R3000aState>::failure(
            ErrorCode::invalid_installation,
            "PS-X EXE guest payload range wraps 32-bit address space");
    }
    const std::span<const std::uint8_t> payload{
        executable.file_bytes.data() + header_size, text_size};
    auto copied = bus.load_main_ram(executable.metadata.text_load_address, payload);
    if (!copied) {
        return Result<R3000aState>::failure(copied.error, copied.detail);
    }
    return Result<R3000aState>::success(
        initialize_r3000a_for_psx_exe(executable.metadata));
}
```

Add the source to `jojo_core`.

- [ ] **Step 4: Run GREEN**

Run:

```bash
cmake --build build --target jojo_ps1_executable_loader_tests --config Release
ctest --test-dir build -C Release --output-on-failure -R "jojo_ps1_executable_loader_tests|jojo_r3000a_init_tests"
```

Expected: both tests pass.

- [ ] **Step 5: Commit**

```bash
git add CMakeLists.txt src/core/ps1_executable_loader.h src/core/ps1_executable_loader.cpp tests/test_ps1_executable_loader.cpp
git commit -m "feat: load JoJo PS-X EXE into PS1 RAM"
```

---

### Task 3: Structured Boot Report and Deterministic Reference-CPU Loop

**Files:**
- Create: `src/core/ps1_boot_report.h`
- Create: `src/core/ps1_boot_runtime.h`
- Create: `src/core/ps1_boot_runtime.cpp`
- Create: `tests/test_ps1_boot_runtime.cpp`
- Modify: `CMakeLists.txt`

**Interfaces:**
- Consumes: `Ps1MemoryBus`, `load_ps1_executable_into_bus`, `step_r3000a`.
- Produces:

```cpp
enum class Ps1BootStopReason : std::uint8_t {
    none,
    execution_budget_exhausted,
    cpu_boundary,
    bios_call_unimplemented,
    bios_call_unknown,
    mmio_unimplemented,
    installed_media_missing,
    device_command_unimplemented,
    gpu_command_unimplemented,
    commercial_frame_presented,
    fatal_runtime_error,
};

struct Ps1BiosCallSummary {
    std::uint32_t pc{};
    std::uint32_t table_physical{};
    std::uint32_t selector{};
};

struct Ps1MmioSummary {
    std::uint32_t pc{};
    std::uint32_t address{};
    std::uint8_t width{};
    bool write{};
    std::uint32_t value{};
};

struct Ps1CdromCommandSummary {
    std::uint8_t command{};
    std::uint8_t index{};
    std::uint8_t status{};
};

struct Ps1BootReport {
    std::uint64_t instructions_retired{};
    std::uint32_t last_pc{};
    std::optional<std::uint32_t> last_opcode{};
    Ps1BootStopReason stop_reason{Ps1BootStopReason::none};
    std::uint64_t bios_call_count{};
    std::vector<Ps1BiosCallSummary> recent_bios_calls;
    std::vector<Ps1MmioSummary> recent_mmio;
    std::uint64_t interrupts_accepted{};
    std::uint64_t dma_transfer_count{};
    std::vector<Ps1CdromCommandSummary> recent_cdrom_commands;
    std::uint64_t gpu_gp0_command_count{};
    std::uint64_t gpu_gp1_command_count{};
    std::uint64_t vram_write_count{};
    std::uint64_t presented_frames{};
    std::optional<R3000aDiagnostic> cpu_diagnostic;
    std::optional<Ps1UnsupportedAccess> unsupported_access;
};

struct Ps1BootOptions {
    std::uint64_t instruction_budget{10000u};
};

class Ps1BootRuntime {
public:
    static Result<Ps1BootRuntime> create(const Ps1Executable& executable);
    Ps1BootReport run(const Ps1BootOptions& options) noexcept;
    const R3000aState& cpu_state() const noexcept { return cpu_; }
    const Ps1MemoryBus& bus() const noexcept { return bus_; }

private:
    Ps1MemoryBus bus_{};
    R3000aState cpu_{};
};
```

- [ ] **Step 1: Add a test helper for synthetic instruction payloads**

Extend `tests/ps1_fixture.h` with a JoJo-labelled synthetic helper that writes instruction words little-endian into an otherwise valid PS-X EXE:

```cpp
inline std::vector<std::uint8_t> make_psx_exe_from_words(
    const std::vector<std::uint32_t>& words,
    std::uint32_t entry_pc = 0x80010000u) {
    auto bytes = make_psx_exe();
    bytes.resize(0x800u + words.size() * 4u, 0u);
    write_le32(bytes, 0x010, entry_pc);
    write_le32(bytes, 0x018, entry_pc);
    write_le32(bytes, 0x01C, static_cast<std::uint32_t>(words.size() * 4u));
    for (std::size_t i = 0; i < words.size(); ++i) {
        write_le32(bytes, 0x800u + i * 4u, words[i]);
    }
    return bytes;
}
```

- [ ] **Step 2: Write RED contracts for budget, BIOS stop, and MMIO stop**

In `tests/test_ps1_boot_runtime.cpp`, build three synthetic programs:

```cpp
const auto loop_exe = jojo::parse_ps1_executable(test_ps1::make_psx_exe_from_words({
    test_mips::j(0x02u, 0x80010000u >> 2),
    0x00000000u,
}));
CHECK(loop_exe);
auto loop_runtime = jojo::Ps1BootRuntime::create(loop_exe.value);
CHECK(loop_runtime);
if (loop_runtime) {
    const auto report = loop_runtime.value.run({10u});
    CHECK(report.stop_reason == jojo::Ps1BootStopReason::execution_budget_exhausted);
    CHECK(report.instructions_retired == 10u);
}

const auto bios_exe = jojo::parse_ps1_executable(test_ps1::make_psx_exe_from_words({
    test_mips::j(0x02u, 0x800000A0u >> 2),
    0x00000000u,
}));
auto bios_runtime = jojo::Ps1BootRuntime::create(bios_exe.value);
CHECK(bios_runtime);
if (bios_runtime) {
    const auto report = bios_runtime.value.run({16u});
    CHECK(report.stop_reason == jojo::Ps1BootStopReason::bios_call_unimplemented);
    CHECK(report.instructions_retired == 2u);
    CHECK(report.last_pc == 0x800000A0u);
    CHECK(report.bios_call_count == 1u);
}

const auto mmio_exe = jojo::parse_ps1_executable(test_ps1::make_psx_exe_from_words({
    test_mips::i(0x0Fu, 0u, 8u, 0x1F80u),
    test_mips::i(0x0Du, 8u, 8u, 0x1070u),
    test_mips::i(0x23u, 8u, 9u, 0u),
}));
auto mmio_runtime = jojo::Ps1BootRuntime::create(mmio_exe.value);
CHECK(mmio_runtime);
if (mmio_runtime) {
    const auto report = mmio_runtime.value.run({16u});
    CHECK(report.stop_reason == jojo::Ps1BootStopReason::mmio_unimplemented);
    CHECK(report.instructions_retired == 2u);
    CHECK(report.unsupported_access.has_value());
    if (report.unsupported_access) {
        CHECK(report.unsupported_access->guest_address == 0x1F801070u);
        CHECK(report.unsupported_access->width == 4u);
    }
}
```

Register:

```cmake
add_jojo_test(jojo_ps1_boot_runtime_tests tests/test_ps1_boot_runtime.cpp)
```

- [ ] **Step 3: Run RED**

Run:

```bash
cmake --build build --target jojo_ps1_boot_runtime_tests --config Release
```

Expected: build fails because the boot report/runtime types do not exist.

- [ ] **Step 4: Implement runtime creation and stop classification**

`Ps1BootRuntime::create` must call the loader and retain its state:

```cpp
Result<Ps1BootRuntime> Ps1BootRuntime::create(const Ps1Executable& executable) {
    Ps1BootRuntime runtime{};
    auto loaded = load_ps1_executable_into_bus(runtime.bus_, executable);
    if (!loaded) {
        return Result<Ps1BootRuntime>::failure(loaded.error, loaded.detail);
    }
    runtime.cpu_ = loaded.value;
    return Result<Ps1BootRuntime>::success(std::move(runtime));
}
```

Before each CPU step, normalize `cpu_.pc`. If physical PC equals `0xA0`, `0xB0`, or `0xC0`, stop as `bios_call_unimplemented`, increment `bios_call_count`, and append exactly one `Ps1BiosCallSummary` using selector `cpu_.gpr[9]`.

For an ordinary step:

```cpp
const auto pc_before = cpu_.pc;
const auto opcode = bus_.read32(pc_before);
report.last_pc = pc_before;
if (opcode.status == R3000aBusStatus::ok) report.last_opcode = opcode.value;

bus_.clear_last_unsupported_access();
const auto step = step_r3000a(cpu_, bus_);
if (step.status == R3000aStepStatus::retired) {
    ++report.instructions_retired;
    continue;
}
report.cpu_diagnostic = step.diagnostic;
report.unsupported_access = bus_.last_unsupported_access();
if (report.unsupported_access &&
    report.unsupported_access->physical_address >= 0x1F801000u &&
    report.unsupported_access->physical_address <= 0x1F802FFFu) {
    report.stop_reason = Ps1BootStopReason::mmio_unimplemented;
    report.recent_mmio.push_back(Ps1MmioSummary{
        pc_before,
        report.unsupported_access->guest_address,
        report.unsupported_access->width,
        report.unsupported_access->write,
        report.unsupported_access->value,
    });
} else {
    report.stop_reason = Ps1BootStopReason::cpu_boundary;
}
return report;
```

If the loop consumes exactly `instruction_budget` retired instructions without another stop, set `execution_budget_exhausted` and return. No success state is inferred.

Add `src/core/ps1_boot_runtime.cpp` to `jojo_core`.

- [ ] **Step 5: Run GREEN**

Run:

```bash
cmake --build build --target jojo_ps1_boot_runtime_tests --config Release
ctest --test-dir build -C Release --output-on-failure -R "jojo_ps1_boot_runtime_tests|jojo_ps1_memory_bus_tests|jojo_ps1_executable_loader_tests|jojo_r3000a_control_flow_tests"
```

Expected: all selected tests pass.

- [ ] **Step 6: Commit**

```bash
git add CMakeLists.txt tests/ps1_fixture.h tests/test_ps1_boot_runtime.cpp src/core/ps1_boot_report.h src/core/ps1_boot_runtime.h src/core/ps1_boot_runtime.cpp
git commit -m "feat: add deterministic JoJo PS1 boot checkpoint"
```

---

### Task 4: Installation-Backed JoJo Boot Checkpoint

**Files:**
- Modify: `src/core/runtime.h`
- Modify: `src/core/runtime.cpp`
- Modify: `tests/test_ps1_runtime_installation.cpp`

**Interfaces:**
- Consumes: existing `validate_installation`, installed `data/boot.psxexe`, `Ps1BootRuntime`.
- Produces:

```cpp
[[nodiscard]] Result<Ps1BootReport> bootstrap_runtime_checkpoint(
    const std::filesystem::path& install_root,
    const Ps1BootOptions& options = {});
```

Existing interface remains:

```cpp
[[nodiscard]] Result<void> bootstrap_runtime(
    const std::filesystem::path& install_root);
```

- [ ] **Step 1: Replace the obsolete runtime-installation RED expectation**

Replace `test_bootstrap_reports_r3000a_not_implemented_without_mutation` with two tests.

Checkpoint test:

```cpp
static void test_bootstrap_checkpoint_executes_installed_psx_exe_without_mutation() {
    auto fixture = make_converted("bootstrap-checkpoint");
    const auto generation = generation_dir(fixture);
    const auto manifest_path = generation / "game_manifest.ini";
    const auto before = read_text(manifest_path);

    const auto boot = jojo::bootstrap_runtime_checkpoint(fixture.install, {4u});
    CHECK(boot);
    if (boot) {
        CHECK(boot.value.stop_reason == jojo::Ps1BootStopReason::execution_budget_exhausted);
        CHECK(boot.value.instructions_retired == 4u);
        CHECK(boot.value.last_pc == 0x8001000Cu);
    }
    CHECK(read_text(manifest_path) == before);
    CHECK(!fs::exists(generation / "cache"));
    cleanup(fixture);
}
```

Product-wrapper truth test:

```cpp
static void test_bootstrap_runtime_does_not_claim_boot_from_checkpoint() {
    auto fixture = make_converted("bootstrap-wrapper");
    const auto result = jojo::bootstrap_runtime(fixture.install);
    CHECK(!result);
    if (!result) {
        CHECK(result.error == jojo::ErrorCode::backend_unavailable);
        CHECK(result.detail.find("checkpoint") != std::string::npos);
        CHECK(result.detail.find("not verified") != std::string::npos);
    }
    cleanup(fixture);
}
```

- [ ] **Step 2: Run RED**

Run:

```bash
cmake --build build --target jojo_ps1_runtime_installation_tests --config Release
ctest --test-dir build -C Release --output-on-failure -R jojo_ps1_runtime_installation_tests
```

Expected: build fails because `bootstrap_runtime_checkpoint` is not declared.

- [ ] **Step 3: Implement installation-backed checkpoint**

Move/reuse the existing private `read_local_file` helper; do not add a second parser. The implementation sequence is exact:

```cpp
Result<Ps1BootReport> bootstrap_runtime_checkpoint(
    const std::filesystem::path& install_root,
    const Ps1BootOptions& options) {
    auto install = validate_installation(install_root);
    if (!install) {
        return Result<Ps1BootReport>::failure(install.error, install.detail);
    }
    auto bytes = read_local_file(install.value.generation_dir / "data" / "boot.psxexe");
    if (!bytes) {
        return Result<Ps1BootReport>::failure(bytes.error, bytes.detail);
    }
    auto executable = parse_ps1_executable(bytes.value);
    if (!executable) {
        return Result<Ps1BootReport>::failure(ErrorCode::invalid_installation,
                                              "installed PS-X EXE is invalid: " + executable.detail);
    }
    if (!executable_metadata_matches(install.value.manifest, executable.value.metadata)) {
        return Result<Ps1BootReport>::failure(
            ErrorCode::invalid_installation,
            "installed PS-X EXE no longer matches JoJo manifest evidence");
    }
    auto runtime = Ps1BootRuntime::create(executable.value);
    if (!runtime) {
        return Result<Ps1BootReport>::failure(runtime.error, runtime.detail);
    }
    return Result<Ps1BootReport>::success(runtime.value.run(options));
}
```

Keep `bootstrap_runtime` conservative. Run a finite checkpoint and return `Result<void>::failure(ErrorCode::backend_unavailable, ...)` unless the report stop reason is `commercial_frame_presented`. M3A cannot produce that reason, so no M3A path claims boot success.

Use a fixed wrapper budget of `10000` retired instructions so the GUI cannot hang indefinitely during this milestone.

- [ ] **Step 4: Run GREEN and full portable core suite**

Run:

```bash
cmake --build build --config Release
ctest --test-dir build -C Release --output-on-failure
```

Expected: zero failed tests.

- [ ] **Step 5: Commit**

```bash
git add src/core/runtime.h src/core/runtime.cpp tests/test_ps1_runtime_installation.cpp
git commit -m "feat: execute installed JoJo boot checkpoint"
```

---

### Task 5: Deterministic Replay and Exact M3A Boundary Evidence

**Files:**
- Modify: `tests/test_ps1_boot_runtime.cpp`
- Modify: `tests/test_ps1_memory_bus.cpp`

**Interfaces:**
- Consumes: finalized M3A public interfaces from Tasks 1–4.
- Produces: regression evidence that identical synthetic JoJo-labelled inputs produce identical CPU state, memory observations and boot report; confirms no extra RAM mirrors are accepted.

- [ ] **Step 1: Add deterministic replay RED/GREEN regression**

Add a helper in `tests/test_ps1_boot_runtime.cpp` that creates two independent runtimes from the same synthetic loop executable, runs both for 32 retired instructions, and compares every M3A-relevant state field:

```cpp
auto a = jojo::Ps1BootRuntime::create(loop_exe.value);
auto b = jojo::Ps1BootRuntime::create(loop_exe.value);
CHECK(a && b);
if (a && b) {
    const auto ra = a.value.run({32u});
    const auto rb = b.value.run({32u});
    CHECK(ra.instructions_retired == rb.instructions_retired);
    CHECK(ra.last_pc == rb.last_pc);
    CHECK(ra.last_opcode == rb.last_opcode);
    CHECK(ra.stop_reason == rb.stop_reason);

    const auto& sa = a.value.cpu_state();
    const auto& sb = b.value.cpu_state();
    CHECK(sa.gpr == sb.gpr);
    CHECK(sa.hi == sb.hi);
    CHECK(sa.lo == sb.lo);
    CHECK(sa.pc == sb.pc);
    CHECK(sa.next_pc == sb.next_pc);
    CHECK(sa.pending_load.valid == sb.pending_load.valid);
    CHECK(sa.pending_load.reg == sb.pending_load.reg);
    CHECK(sa.pending_load.value == sb.pending_load.value);
    CHECK(sa.delay_slot.active == sb.delay_slot.active);
    CHECK(sa.delay_slot.branch_pc == sb.delay_slot.branch_pc);
    CHECK(sa.delay_slot.taken == sb.delay_slot.taken);
    CHECK(sa.delay_slot.target == sb.delay_slot.target);
    CHECK(sa.cop0.status == sb.cop0.status);
    CHECK(sa.cop0.cause == sb.cop0.cause);
    CHECK(sa.cop0.epc == sb.cop0.epc);
    CHECK(sa.cop0.bad_vaddr == sb.cop0.bad_vaddr);
    CHECK(sa.cop0.target_address == sb.cop0.target_address);
    CHECK(a.value.bus().read32(0x80010000u).value == b.value.bus().read32(0x80010000u).value);
}
```

In `tests/test_ps1_memory_bus.cpp`, lock the JoJo-only initial alias policy:

```cpp
CHECK(bus.read8(0x00200000u).status == jojo::R3000aBusStatus::unsupported);
CHECK(bus.read8(0x80200000u).status == jojo::R3000aBusStatus::unsupported);
CHECK(bus.read8(0xA0200000u).status == jojo::R3000aBusStatus::unsupported);
```

- [ ] **Step 2: Run focused suite**

Run:

```bash
cmake --build build --target jojo_ps1_boot_runtime_tests jojo_ps1_memory_bus_tests --config Release
ctest --test-dir build -C Release --output-on-failure -R "jojo_ps1_boot_runtime_tests|jojo_ps1_memory_bus_tests"
```

Expected: all tests pass. If the replay test exposes nondeterministic state, stop and use `superpowers:systematic-debugging` before changing semantics.

- [ ] **Step 3: Run full suite before code-head CI**

Run:

```bash
cmake --build build --config Release
ctest --test-dir build -C Release --output-on-failure
```

Expected: zero failed tests.

- [ ] **Step 4: Commit**

```bash
git add tests/test_ps1_boot_runtime.cpp tests/test_ps1_memory_bus.cpp
git commit -m "test: verify deterministic JoJo M3A boot replay"
```

- [ ] **Step 5: Push code head and require GitHub Actions Linux + Windows success**

Push the implementation branch and record the exact numeric GitHub Actions run ID whose `head_sha` equals the Task 5 commit. Both `Portable core / Linux` and `Windows x64 / MSVC 2022` must complete with `conclusion=success`. Do not update readiness evidence from a run attached to any other SHA.

---

### Task 6: Truthful M3A Project-State Update

**Files:**
- Modify: `PROJECT-STATE.md`
- Modify: `docs/NEXT-MILESTONES.md`
- Modify: `docs/architecture/PRODUCTION-READINESS.tsv`

**Interfaces:**
- Consumes: exact successful code-head GitHub Actions run ID from Task 5.
- Produces: repository truth that M3A is synthetically verified while JoJo commercial boot remains unverified and M3B BIOS/HLE is next.

- [ ] **Step 1: Update R2.3 evidence without promoting R2.4**

Change only the R2.3 line to:

```text
R2.3	implemented-unverified	github-actions:run-<CODE_HEAD_RUN_ID>	jojo-bios-hle-and-device-runtime-not-implemented
```

Replace `<CODE_HEAD_RUN_ID>` with the numeric run ID verified in Task 5 before committing. Leave R2.4 exactly `not-started` unless separate commercial evidence exists; M3A alone is not such evidence.

- [ ] **Step 2: Update `PROJECT-STATE.md` with exact M3A truth**

Add these statements verbatim to the active PS1 status section:

```text
JoJo PS1 M3A memory/bus, PS-X EXE payload loading, and bounded R3000A boot checkpoints are implemented and verified by synthetic Linux/Windows contracts.
Commercial JoJo boot, BIOS/HLE progress, device progress, rendering, audio, input and gameplay are not verified by M3A.
```

Remove any stale statement claiming R3000A execution itself is not implemented. Do not add a claim that the commercial JoJo executable has booted.

- [ ] **Step 3: Make M3B the next milestone**

Update `docs/NEXT-MILESTONES.md` so the immediate engineering target is:

```text
M3B — JoJo-observed BIOS/HLE: run the supported local JoJo installation through the M3A checkpoint, capture only bounded derived diagnostics at the first A0/B0/C0 or kernel boundary, reproduce the required contract synthetically, and implement only the JoJo-required service.
```

Keep CFG/IR/x64 downstream of visible boot and explicitly retain the JoJo-only product boundary.

- [ ] **Step 4: Commit documentation**

```bash
git add PROJECT-STATE.md docs/NEXT-MILESTONES.md docs/architecture/PRODUCTION-READINESS.tsv
git commit -m "docs: record JoJo PS1 M3A checkpoint readiness"
```

- [ ] **Step 5: Verify the final docs head independently**

Push the docs commit. Require a fresh GitHub Actions run whose `head_sha` equals the docs commit, with both Linux and Windows jobs successful. The R2.3 evidence field remains the Task 5 code-head run ID because that run is the immutable implementation evidence; the final docs-head run verifies repository consistency after the status update.

- [ ] **Step 6: Completion boundary**

Only after Step 5 succeeds may M3A be called complete. The maximum allowed claim is:

```text
M3A synthetic reference-execution checkpoint ready for a local JoJo boundary run.
```

Do not claim `commercial boot verified`, `rendering verified`, `playable`, or `native recompilation verified`.

---

## Self-Review Result

- Spec coverage: M3A sections 5, 6, 11, 12, 13, 14, 15 and the M3A portion of section 16 are covered by Tasks 1–6.
- M3B–M3E are intentionally not implemented by this plan; each receives a separate plan after the preceding JoJo checkpoint provides evidence.
- JoJo-only scope is locked by Global Constraints and Task 5 alias tests; no other-game compatibility work is authorized.
- No proprietary or commercial bytes are introduced into tests or CI.
- Public names are consistent across tasks: `Ps1MemoryBus`, `Ps1UnsupportedAccess`, `load_ps1_executable_into_bus`, `Ps1BootStopReason`, `Ps1BootReport`, `Ps1BootOptions`, `Ps1BootRuntime`, and `bootstrap_runtime_checkpoint`.
- No M3A task promotes commercial boot or rendering status.
