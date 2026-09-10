# PS1 CD-ROM Command 01 + IRQ Bridge v0 Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Replace the first observed speculative PS1 CD-ROM MMIO sequence with deterministic command `0x01` device semantics, latch CD-ROM IRQ into I_STAT, bridge enabled interrupt-controller state to R3000A IP2, and report the command in MAX3 diagnostics.

**Architecture:** Add a focused `Ps1CdromState` device component that owns bank selection, one-byte RESULT state, HINTSTS/HINTMSK state, command accounting, and IRQ edge detection. `Ps1MemoryBus` routes only the approved 8-bit ports and owns I_STAT/I_MASK latching; `Ps1BootRuntime` seeds post-BIOS state, synchronizes `(I_STAT & I_MASK)` to R3000A external IP2, and records device commands in `Ps1BootReport`.

**Tech Stack:** C++20, CMake 3.20+, existing R3000A reference executor and PS1 memory bus, synthetic test fixtures only, GitHub Actions Linux + Windows/MSVC 2022.

**Spec:** `docs/superpowers/specs/2026-09-10-ps1-cdrom-command01-irq-v0-design.md`

## Global Constraints

- Implement only CD-ROM command `0x01` (`Nop/GetStatus`) in this milestone.
- The observed MMIO contract is 8-bit write `0x1F801800`, 8-bit read/write `0x1F801803`, and 8-bit write `0x1F801801`; RESULT read at `0x1F801801` is allowed only as part of command `0x01` response semantics.
- Keep all other CD-ROM commands, 16/32-bit CD-ROM accesses, bank-1 HCLRCTL writes, parameter FIFO, RDDATA, DMA3, and CD audio behavior strict.
- Standalone `Ps1CdromState`/`Ps1MemoryBus` use neutral reset state; only `Ps1BootRuntime::create` seeds post-BIOS `drive_status=0x02` and `interrupt_enable=0x1F`.
- CD-ROM device IRQ is `(interrupt_enable & hintsts_low5) != 0`; its rising edge latches I_STAT bit 2.
- R3000A external pending bit `0x04` mirrors whether `(I_STAT & I_MASK) != 0`, preserving all other external pending bits.
- `read16(I_STAT)` becomes supported; `read32(I_STAT)` stays strict.
- MAX3 hashes all guest-observable CD-ROM state that can change future execution.
- No proprietary game/BIOS/RAM payloads or assets may enter source, tests, CI artifacts, or releases.
- Every production change follows RED -> GREEN; Windows/MSVC CI and Linux CI must both pass on the final exact SHA.

---

## File Structure

**Create** `src/core/ps1_cdrom_state.h` — public device-state API and compact result/event types.

**Create** `src/core/ps1_cdrom_state.cpp` — banked 8-bit port semantics, command `0x01`, one-byte RESULT state, IRQ-edge logic, deterministic hash.

**Create** `tests/test_ps1_cdrom_state.cpp` — pure device tests with no CPU/runtime dependency.

**Modify** `src/core/ps1_memory_bus.h` — own/expose `Ps1CdromState`, expose `interrupt_status()`, and expose last unsupported CD-ROM command for structured classification.

**Modify** `src/core/ps1_memory_bus.cpp` — route approved ports, latch I_STAT bit 2 on CD-ROM IRQ rising edge, add `read16(I_STAT)`, hash CD-ROM state, keep non-approved widths strict.

**Modify** `tests/test_ps1_memory_bus.cpp` — reproduce the six commercial MMIO events and strict negative cases.

**Modify** `src/core/ps1_boot_runtime.cpp` — seed post-BIOS CD-ROM state, synchronize IP2 before each execution iteration, count/report real commands, classify unsupported commands.

**Modify** `tests/test_ps1_boot_runtime.cpp` — synthetic R3000A tests for bootstrap, IRQ masking/preemption, reporting, and unsupported-command stop reason.

**Modify** `CMakeLists.txt` — compile the new component and register its unit-test executable.

---

### Task 1: Isolated `Ps1CdromState` for command `0x01`

**Files:**
- Create: `src/core/ps1_cdrom_state.h`
- Create: `src/core/ps1_cdrom_state.cpp`
- Create: `tests/test_ps1_cdrom_state.cpp`
- Modify: `CMakeLists.txt`

**Interfaces:**
- Consumes: no PS1 bus/runtime state.
- Produces:
  - `enum class Ps1CdromIoStatus : std::uint8_t { ok, unsupported_register, unsupported_command };`
  - `struct Ps1CdromIoResult { Ps1CdromIoStatus status; std::uint8_t value; };`
  - `struct Ps1CdromCommandEvent { std::uint64_t sequence; std::uint8_t command; std::uint8_t index; std::uint8_t status; };`
  - `void Ps1CdromState::seed_post_bios(std::uint8_t drive_status, std::uint8_t interrupt_enable) noexcept;`
  - `Ps1CdromIoResult Ps1CdromState::read8(std::uint32_t physical) noexcept;`
  - `Ps1CdromIoResult Ps1CdromState::write8(std::uint32_t physical, std::uint8_t value) noexcept;`
  - `bool Ps1CdromState::take_irq_rising_edge() noexcept;`
  - `bool Ps1CdromState::irq_line() const noexcept;`
  - `std::uint64_t Ps1CdromState::command_count() const noexcept;`
  - `const std::optional<Ps1CdromCommandEvent>& Ps1CdromState::last_command_event() const noexcept;`
  - `std::uint8_t Ps1CdromState::index() const noexcept;`
  - `std::uint8_t Ps1CdromState::drive_status() const noexcept;`
  - `std::uint8_t Ps1CdromState::interrupt_enable() const noexcept;`
  - `std::uint8_t Ps1CdromState::interrupt_status() const noexcept;`
  - `std::uint64_t Ps1CdromState::diagnostic_state_hash() const noexcept;`

- [ ] **Step 1: Write the failing device test and register it**

Add `tests/test_ps1_cdrom_state.cpp` with the existing `CHECK` harness style. The core assertions must be:

```cpp
#include "core/ps1_cdrom_state.h"
#include <iostream>

static int failures = 0;
#define CHECK(x) do { if (!(x)) { std::cerr << __FILE__ << ':' << __LINE__ << " CHECK failed: " #x "\n"; ++failures; } } while (0)

int main() {
    jojo::Ps1CdromState cdrom;
    CHECK(cdrom.index() == 0u);
    CHECK(cdrom.drive_status() == 0u);
    CHECK(cdrom.interrupt_enable() == 0u);
    CHECK(cdrom.interrupt_status() == 0u);
    CHECK(cdrom.command_count() == 0u);
    CHECK(!cdrom.irq_line());

    cdrom.seed_post_bios(0x02u, 0x1Fu);
    CHECK(cdrom.drive_status() == 0x02u);
    CHECK(cdrom.interrupt_enable() == 0x1Fu);

    CHECK(cdrom.write8(0x1F801800u, 0x01u).status == jojo::Ps1CdromIoStatus::ok);
    const auto hintsts_before = cdrom.read8(0x1F801803u);
    CHECK(hintsts_before.status == jojo::Ps1CdromIoStatus::ok);
    CHECK(hintsts_before.value == 0xE0u);

    CHECK(cdrom.write8(0x1F801800u, 0x00u).status == jojo::Ps1CdromIoStatus::ok);
    CHECK(cdrom.write8(0x1F801803u, 0x00u).status == jojo::Ps1CdromIoStatus::ok);
    const auto hash_before = cdrom.diagnostic_state_hash();
    CHECK(cdrom.write8(0x1F801801u, 0x01u).status == jojo::Ps1CdromIoStatus::ok);
    CHECK(cdrom.command_count() == 1u);
    CHECK(cdrom.interrupt_status() == 3u);
    CHECK(cdrom.irq_line());
    CHECK(cdrom.take_irq_rising_edge());
    CHECK(!cdrom.take_irq_rising_edge());
    CHECK(cdrom.diagnostic_state_hash() != hash_before);

    const auto event = cdrom.last_command_event();
    CHECK(event.has_value());
    if (event) {
        CHECK(event->sequence == 1u);
        CHECK(event->command == 0x01u);
        CHECK(event->index == 0u);
        CHECK(event->status == 0x02u);
    }

    const auto result = cdrom.read8(0x1F801801u);
    CHECK(result.status == jojo::Ps1CdromIoStatus::ok);
    CHECK(result.value == 0x02u);
    CHECK(cdrom.read8(0x1F801801u).status == jojo::Ps1CdromIoStatus::unsupported_register);

    CHECK(cdrom.write8(0x1F801801u, 0x02u).status == jojo::Ps1CdromIoStatus::unsupported_command);
    CHECK(cdrom.write8(0x1F801803u, 0x01u).status == jojo::Ps1CdromIoStatus::unsupported_register);

    cdrom.write8(0x1F801800u, 0x03u);
    CHECK(cdrom.read8(0x1F801803u).status == jojo::Ps1CdromIoStatus::unsupported_register);

    jojo::Ps1CdromState replay;
    replay.seed_post_bios(0x02u, 0x1Fu);
    replay.write8(0x1F801800u, 0x01u);
    replay.read8(0x1F801803u);
    replay.write8(0x1F801800u, 0x00u);
    replay.write8(0x1F801803u, 0x00u);
    replay.write8(0x1F801801u, 0x01u);
    CHECK(replay.diagnostic_state_hash() == cdrom.diagnostic_state_hash() || replay.command_count() == cdrom.command_count());

    return failures ? 1 : 0;
}
```

Register the future source/test in CMake:

```cmake
add_library(jojo_core STATIC
  # existing sources...
  src/core/ps1_cdrom_state.cpp
  src/core/ps1_memory_bus.cpp
  # existing sources...
)

add_jojo_test(jojo_ps1_cdrom_state_tests tests/test_ps1_cdrom_state.cpp)
```

Before GREEN, tighten the replay check to compare hashes at the same logical point: construct both devices through the identical sequence and do not consume the IRQ edge or RESULT on only one side.

- [ ] **Step 2: Run the targeted build to verify RED**

Run on Linux CI/local equivalent:

```bash
cmake -S . -B build
cmake --build build --target jojo_ps1_cdrom_state_tests -j2
```

Expected RED: compilation fails because `core/ps1_cdrom_state.h` / `Ps1CdromState` does not exist yet. No existing production file is changed to satisfy the test before this observation.

- [ ] **Step 3: Implement the minimal device component**

Create `src/core/ps1_cdrom_state.h` using exactly the interfaces above. Private state is:

```cpp
std::uint8_t index_{};
std::uint8_t drive_status_{};
std::uint8_t interrupt_enable_{};
std::uint8_t interrupt_status_{};
std::optional<std::uint8_t> response_{};
std::uint64_t command_count_{};
std::optional<Ps1CdromCommandEvent> last_command_event_{};
bool irq_line_{};
bool irq_rising_edge_pending_{};
```

Create `src/core/ps1_cdrom_state.cpp` with these exact v0 rules:

```cpp
// 0x1F801800 write: index_ = value & 3, always ok.
// 0x1F801803 read: only index_ == 1; return 0xE0 | (interrupt_status_ & 0x1F).
// 0x1F801803 write: only index_ == 0 && value == 0; return ok/no effect.
// 0x1F801801 write: only index_ == 0. command 0x01 is supported; any other byte => unsupported_command.
// command 0x01: response_ = drive_status_; interrupt_status_ = 3; ++command_count_;
// last_command_event_ = {command_count_, 0x01, index_, drive_status_}; then recompute IRQ line.
// 0x1F801801 read: only index_ == 0 && response_.has_value(); return and clear response_; otherwise unsupported_register.
```

IRQ recomputation is edge-based:

```cpp
const bool next = (interrupt_enable_ & interrupt_status_ & 0x1Fu) != 0u;
if (!irq_line_ && next) irq_rising_edge_pending_ = true;
irq_line_ = next;
```

`seed_post_bios` sets status/mask, clears response/event/counter/interrupt status, and recomputes from a false prior line. The diagnostic FNV-1a hash includes index, drive status, interrupt enable/status, response presence/value, command count, last-event presence/fields, current IRQ line, and pending rising edge. Do not hash unused capacity or addresses.

- [ ] **Step 4: Run the targeted test to verify GREEN**

```bash
cmake -S . -B build
cmake --build build --target jojo_ps1_cdrom_state_tests -j2
ctest --test-dir build -R jojo_ps1_cdrom_state_tests --output-on-failure
```

Expected: PASS.

- [ ] **Step 5: Run existing PS1 bus/runtime smoke tests**

```bash
ctest --test-dir build -R "jojo_ps1_(memory_bus|boot_runtime|diagnostic_frontier|max3_explorer)_tests" --output-on-failure
```

Expected: all selected tests PASS; Task 1 is isolated and must not change bus behavior yet.

- [ ] **Step 6: Commit Task 1 GREEN**

```bash
git add CMakeLists.txt src/core/ps1_cdrom_state.h src/core/ps1_cdrom_state.cpp tests/test_ps1_cdrom_state.cpp
git commit -m "feat: add minimal PS1 CD-ROM state"
```

---

### Task 2: Route observed CD-ROM MMIO and latch I_STAT IRQ2

**Files:**
- Modify: `src/core/ps1_memory_bus.h`
- Modify: `src/core/ps1_memory_bus.cpp`
- Modify: `tests/test_ps1_memory_bus.cpp`

**Interfaces:**
- Consumes: `Ps1CdromState::{read8,write8,take_irq_rising_edge,diagnostic_state_hash}` from Task 1.
- Produces:
  - `std::uint16_t Ps1MemoryBus::interrupt_status() const noexcept;`
  - `Ps1CdromState& Ps1MemoryBus::cdrom() noexcept;`
  - `const Ps1CdromState& Ps1MemoryBus::cdrom() const noexcept;`
  - `const std::optional<std::uint8_t>& Ps1MemoryBus::last_unsupported_cdrom_command() const noexcept;`
  - `void Ps1MemoryBus::clear_last_unsupported_cdrom_command() noexcept;`

- [ ] **Step 1: Add RED assertions for the exact commercial MMIO sequence**

Append a fresh scoped block in `tests/test_ps1_memory_bus.cpp` so existing shared bus state cannot contaminate the device fixture:

```cpp
{
    jojo::Ps1MemoryBus cd_bus;
    cd_bus.cdrom().seed_post_bios(0x02u, 0x1Fu);
    cd_bus.set_diagnostic_mmio_probe_enabled(true);

    cd_bus.clear_last_diagnostic_mmio_probe();
    CHECK(cd_bus.write8(0x1F801800u, 0x01u).status == jojo::R3000aBusStatus::ok);
    CHECK(!cd_bus.last_diagnostic_mmio_probe());

    const auto hintsts = cd_bus.read8(0x1F801803u);
    CHECK(hintsts.status == jojo::R3000aBusStatus::ok);
    CHECK(hintsts.value == 0xE0u);
    CHECK(!cd_bus.last_diagnostic_mmio_probe());

    CHECK(cd_bus.write8(0x1F801800u, 0x00u).status == jojo::R3000aBusStatus::ok);
    CHECK(cd_bus.write8(0x1F801803u, 0x00u).status == jojo::R3000aBusStatus::ok);
    CHECK(cd_bus.write8(0x1F801800u, 0x00u).status == jojo::R3000aBusStatus::ok);
    CHECK(cd_bus.write8(0x1F801801u, 0x01u).status == jojo::R3000aBusStatus::ok);
    CHECK(cd_bus.interrupt_status() == 0x0004u);

    const auto istat16 = cd_bus.read16(0x1F801070u);
    CHECK(istat16.status == jojo::R3000aBusStatus::ok);
    CHECK(istat16.value == 0x0004u);
    CHECK(cd_bus.read32(0x1F801070u).status == jojo::R3000aBusStatus::unsupported);

    CHECK(cd_bus.write16(0x1F801070u, 0x0000u).status == jojo::R3000aBusStatus::ok);
    CHECK(cd_bus.interrupt_status() == 0u);

    cd_bus.clear_last_unsupported_cdrom_command();
    CHECK(cd_bus.write8(0x1F801801u, 0x02u).status == jojo::R3000aBusStatus::unsupported);
    CHECK(cd_bus.last_unsupported_cdrom_command().has_value());
    if (cd_bus.last_unsupported_cdrom_command()) CHECK(*cd_bus.last_unsupported_cdrom_command() == 0x02u);

    CHECK(cd_bus.read16(0x1F801800u).status == jojo::R3000aBusStatus::unsupported);
    CHECK(cd_bus.read32(0x1F801800u).status == jojo::R3000aBusStatus::unsupported);
}
```

Also compare `diagnostic_state_hash()` before/after command `0x01` and across two identical fresh bus sequences.

- [ ] **Step 2: Run the bus test and verify RED**

```bash
cmake --build build --target jojo_ps1_memory_bus_tests -j2
ctest --test-dir build -R jojo_ps1_memory_bus_tests --output-on-failure
```

Expected RED: compile failure for missing bus CD-ROM accessors, or failing MMIO assertions if the accessors were introduced in the test harness first. Existing diagnostic shadow behavior must not make the new assertions falsely pass because `last_diagnostic_mmio_probe` is required to remain empty.

- [ ] **Step 3: Add bus ownership and routing**

In `ps1_memory_bus.h`, include `core/ps1_cdrom_state.h`, add `Ps1CdromState cdrom_{};` and `std::optional<std::uint8_t> last_unsupported_cdrom_command_{};`, plus the interfaces above.

In `ps1_memory_bus.cpp`, define:

```cpp
constexpr std::uint32_t kCdromIndexStatus = 0x1F801800u;
constexpr std::uint32_t kCdromResponseCommand = 0x1F801801u;
constexpr std::uint32_t kCdromRequestInterrupt = 0x1F801803u;
constexpr std::uint16_t kInterruptCdrom = 1u << 2;
```

Route only `read8`/`write8` at these addresses. Convert `Ps1CdromIoStatus::ok` to `R3000aBusStatus::ok`; convert unsupported register/command to `R3000aBusStatus::unsupported` and populate `last_unsupported_` exactly as other MMIO failures do. For `unsupported_command`, also set `last_unsupported_cdrom_command_ = value`.

Immediately after a successful CD-ROM write:

```cpp
if (cdrom_.take_irq_rising_edge()) interrupt_status_ |= kInterruptCdrom;
```

Add `read16(I_STAT)` returning `interrupt_status_`. Do not add `read32(I_STAT)`.

Hash `cdrom_.diagnostic_state_hash()` as a u64 in `Ps1MemoryBus::diagnostic_state_hash()`; I_STAT/I_MASK remain hashed through their existing fields.

- [ ] **Step 4: Run bus GREEN + strict negative tests**

```bash
cmake --build build --target jojo_ps1_memory_bus_tests -j2
ctest --test-dir build -R jojo_ps1_memory_bus_tests --output-on-failure
```

Expected: PASS, including no diagnostic probe for the six approved events, strict 16/32-bit CD-ROM ports, strict `read32(I_STAT)`, and structured unsupported command byte.

- [ ] **Step 5: Commit Task 2**

```bash
git add src/core/ps1_memory_bus.h src/core/ps1_memory_bus.cpp tests/test_ps1_memory_bus.cpp
git commit -m "feat: route PS1 CD-ROM command MMIO"
```

---

### Task 3: Seed post-BIOS CD-ROM state and bridge I_STAT/I_MASK to R3000A IP2

**Files:**
- Modify: `src/core/ps1_boot_runtime.cpp`
- Modify: `tests/test_ps1_boot_runtime.cpp`

**Interfaces:**
- Consumes: `Ps1MemoryBus::{interrupt_status,interrupt_mask,cdrom}` from Task 2 and existing `R3000aState::external_interrupt_pending`.
- Produces: no new public runtime API; behavior is observable through `cpu_state()`, `bus()`, and `Ps1BootReport`.

- [ ] **Step 1: Add RED bootstrap and IRQ tests**

Add tests using existing `make_runtime()`.

First, verify bootstrap is isolated to runtime creation:

```cpp
static void test_runtime_seeds_post_bios_cdrom_state() {
    auto runtime = make_runtime({test_mips::j(0x02u, 0x80010000u >> 2), 0u});
    CHECK(runtime.bus().cdrom().drive_status() == 0x02u);
    CHECK(runtime.bus().cdrom().interrupt_enable() == 0x1Fu);
}
```

Then add a synthetic program that writes `I_MASK=0x0004`, writes CD-ROM index 0 and command `0x01`, and has COP0 IE+IM2 enabled before the command. Use existing MIPS encoders already present in the test suite; set `runtime.cpu_state()` only through an added test-local non-const path if one already exists. If runtime exposes only const CPU state, encode `mtc0 Status` in the synthetic program using the existing COP0 test encoding pattern rather than adding a production mutator.

The assertions are:

```cpp
CHECK(report.interrupts_accepted == 1u);
CHECK(runtime.bus().interrupt_status() == 0x0004u);
CHECK((runtime.cpu_state().cop0.cause & 0x00000400u) != 0u); // IP2
```

Add a second program with I_MASK=0 and the same command; it must not accept the interrupt within the same instruction budget.

- [ ] **Step 2: Run runtime tests and verify RED**

```bash
cmake --build build --target jojo_ps1_boot_runtime_tests -j2
ctest --test-dir build -R jojo_ps1_boot_runtime_tests --output-on-failure
```

Expected RED: runtime bootstrap remains neutral and/or no external IP2 is synchronized from I_STAT/I_MASK.

- [ ] **Step 3: Implement runtime seed and IP2 synchronization**

In `Ps1BootRuntime::create`, after loading the executable and before returning success:

```cpp
runtime.bus_.cdrom().seed_post_bios(0x02u, 0x1Fu);
```

Add a private-file helper in `ps1_boot_runtime.cpp`:

```cpp
void sync_interrupt_controller_to_cpu(R3000aState& cpu, const Ps1MemoryBus& bus) noexcept {
    constexpr std::uint8_t kExternalIp2 = 0x04u;
    if ((bus.interrupt_status() & bus.interrupt_mask()) != 0u) {
        cpu.external_interrupt_pending |= kExternalIp2;
    } else {
        cpu.external_interrupt_pending &= static_cast<std::uint8_t>(~kExternalIp2);
    }
}
```

Call it at the top of every `while` iteration, before BIOS-table handling, syscall fast-path checks, opcode fetch, or `step_r3000a`. This keeps `interrupt_would_preempt_syscall()` and the reference executor consistent with the same interrupt-controller state.

- [ ] **Step 4: Run runtime GREEN and CPU interrupt regression tests**

```bash
cmake --build build --target jojo_ps1_boot_runtime_tests jojo_r3000a_exception_tests jojo_r3000a_cop0_tests -j2
ctest --test-dir build -R "jojo_ps1_boot_runtime_tests|jojo_r3000a_exception_tests|jojo_r3000a_cop0_tests" --output-on-failure
```

Expected: PASS. Masked device interrupt must not preempt; enabled I_STAT/I_MASK + COP0 IE/IM2 must enter the existing interrupt exception path.

- [ ] **Step 5: Commit Task 3**

```bash
git add src/core/ps1_boot_runtime.cpp tests/test_ps1_boot_runtime.cpp
git commit -m "feat: bridge PS1 interrupt controller to IP2"
```

---

### Task 4: Real CD-ROM command reporting, progress, and unsupported-command stop reason

**Files:**
- Modify: `src/core/ps1_boot_runtime.cpp`
- Modify: `tests/test_ps1_boot_runtime.cpp`

**Interfaces:**
- Consumes: `Ps1CdromState::command_count()`, `last_command_event()`, and `Ps1MemoryBus::last_unsupported_cdrom_command()`.
- Produces: existing `Ps1BootReport::{cdrom_command_count,recent_cdrom_commands}` becomes populated by real device commands; existing `Ps1BootStopReason::device_command_unimplemented` becomes reachable for unsupported bank-0 command writes.

- [ ] **Step 1: Add RED reporting and strict-command tests**

Add a synthetic runtime program whose only supported device action is bank-0 command `0x01`, followed by a finite loop. Configure options with `mmio_event_capacity = 4` and enough budget to retire beyond the command. Assert:

```cpp
CHECK(report.cdrom_command_count == 1u);
CHECK(report.recent_cdrom_commands.size() == 1u);
if (!report.recent_cdrom_commands.empty()) {
    const auto& event = report.recent_cdrom_commands.back();
    CHECK(event.command == 0x01u);
    CHECK(event.index == 0u);
    CHECK(event.status == 0x02u);
}
CHECK(report.speculative_mmio_count == 0u);
```

Add a second synthetic program that selects bank 0 and writes command `0x02` to `0x1F801801`; assert:

```cpp
CHECK(report.stop_reason == jojo::Ps1BootStopReason::device_command_unimplemented);
CHECK(report.cdrom_command_count == 0u);
CHECK(report.unsupported_access.has_value());
```

- [ ] **Step 2: Run the tests and verify RED**

```bash
cmake --build build --target jojo_ps1_boot_runtime_tests -j2
ctest --test-dir build -R jojo_ps1_boot_runtime_tests --output-on-failure
```

Expected RED: command count/history remain zero and unsupported command is still classified as generic MMIO boundary.

- [ ] **Step 3: Record real command deltas and classify unsupported commands**

At `run()` entry capture:

```cpp
const auto cdrom_before = bus_.cdrom().command_count();
```

Extend `finish`:

```cpp
report.cdrom_command_count = bus_.cdrom().command_count() - cdrom_before;
```

Add:

```cpp
void record_recent_cdrom(Ps1BootReport& report,
                         const Ps1CdromCommandSummary& event,
                         std::size_t capacity) {
    if (capacity == 0u) return;
    if (report.recent_cdrom_commands.size() == capacity) {
        report.recent_cdrom_commands.erase(report.recent_cdrom_commands.begin());
    }
    report.recent_cdrom_commands.push_back(event);
}
```

Immediately before `step_r3000a`, capture `command_count_before_step`. After a retired step, if the count increased, read `last_command_event()` and append:

```cpp
record_recent_cdrom(report,
    Ps1CdromCommandSummary{event.command, event.index, event.status},
    options.mmio_event_capacity);
instructions_since_progress = 0u;
```

Before each step call `bus_.clear_last_unsupported_cdrom_command()`. If a failed step has an unsupported access and `last_unsupported_cdrom_command()` is set, return `device_command_unimplemented` instead of `mmio_unimplemented`. Preserve `report.unsupported_access`.

- [ ] **Step 4: Run GREEN and deterministic replay tests**

```bash
cmake --build build --target jojo_ps1_boot_runtime_tests jojo_ps1_max3_explorer_tests jojo_ps1_boot_report_io_tests -j2
ctest --test-dir build -R "jojo_ps1_boot_runtime_tests|jojo_ps1_max3_explorer_tests|jojo_ps1_boot_report_io_tests" --output-on-failure
```

Expected: PASS. Two identical synthetic runs must produce identical `cdrom_command_count`, command summaries, and `diagnostic_state_hash()`.

- [ ] **Step 5: Commit Task 4**

```bash
git add src/core/ps1_boot_runtime.cpp tests/test_ps1_boot_runtime.cpp
git commit -m "feat: report PS1 CD-ROM commands"
```

---

### Task 5: Full strictness, MAX3, and cross-platform verification

**Files:**
- Modify only if a regression test exposes a real defect in Tasks 1-4.
- No scope expansion to new CD-ROM commands or HCLRCTL is allowed in this task.

**Interfaces:**
- Consumes: all Task 1-4 behavior.
- Produces: exact validated branch SHA and Windows artifact for the next commercial checkpoint.

- [ ] **Step 1: Run all Linux tests and project gates**

```bash
cmake -S . -B build
cmake --build build -j2
ctest --test-dir build --output-on-failure
cmake -DJOJO_SOURCE_DIR="$PWD" -P cmake/CheckProductionReadiness.cmake
cmake -DJOJO_SOURCE_DIR="$PWD" -P cmake/CheckProductionReadinessNegative.cmake
cmake -DJOJO_SOURCE_DIR="$PWD" -P cmake/CheckPs1ActiveArchitecture.cmake
```

Expected: 100% tests pass and all gates pass.

- [ ] **Step 2: Verify strict negative coverage explicitly**

Confirm the tests still assert all of these as unsupported:

```text
read16/read32 0x1F801800..0x1F801803
read32(I_STAT)
CD-ROM command != 0x01
bank-1 write 0x1F801803 (HCLRCTL)
non-zero bank-0 write 0x1F801803 (HCHPCTL)
bank-3 read 0x1F801803
parameter/data ports not approved by the spec
DMA3 untouched
```

If any item is missing, add a direct negative assertion to `tests/test_ps1_cdrom_state.cpp` or `tests/test_ps1_memory_bus.cpp`, observe RED if production is too permissive, then make the smallest correction and rerun the affected suite.

- [ ] **Step 3: Review the diff against the spec**

```bash
git diff <spec-approved-base>...HEAD -- \
  CMakeLists.txt \
  src/core/ps1_cdrom_state.h src/core/ps1_cdrom_state.cpp \
  src/core/ps1_memory_bus.h src/core/ps1_memory_bus.cpp \
  src/core/ps1_boot_runtime.cpp \
  tests/test_ps1_cdrom_state.cpp tests/test_ps1_memory_bus.cpp tests/test_ps1_boot_runtime.cpp
```

Use the spec-approved base commit `fbe14615ceab3bd50f261270e5b91dddee42b2ce`. Reject unrelated refactors, new device commands, DMA3, graphics work, or proprietary fixtures.

- [ ] **Step 4: Push exact HEAD and require GitHub Actions Linux + Windows success**

Use the repository's existing push-driven `build` workflow. Verify the workflow `head_sha` exactly equals the branch HEAD. Require Linux and Windows/MSVC jobs to finish `success`, including production readiness, PS1 architecture gate, observed-disc revision, UDP checks, and the complete CTest suites.

- [ ] **Step 5: Download and verify the Windows artifact**

Download the `JOJO-Recompiled-Windows-x64` artifact from the successful exact-SHA workflow. Verify the GitHub-reported SHA-256 matches the downloaded ZIP, inspect that it contains only `JOJO-Recompiled.exe`, and compute the EXE SHA-256 locally.

- [ ] **Step 6: Run the next evidence handoff**

Deliver the verified ZIP and ask for exactly one `EXECUTAR CHECKPOINT` run. The next checkpoint is the authority for whether to implement HCLRCTL, RESULT/HSTS polling, another CD-ROM command, interrupt-handler behavior, or a different subsystem. Do not implement any of those preemptively.
