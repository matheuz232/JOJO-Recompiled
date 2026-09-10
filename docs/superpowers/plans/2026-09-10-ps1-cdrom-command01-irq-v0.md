# PS1 CD-ROM Command 01 + IRQ Bridge v0 Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Replace the first observed speculative PS1 CD-ROM MMIO sequence with deterministic command `0x01` device semantics, latch CD-ROM IRQ into I_STAT, bridge enabled interrupt-controller state to R3000A IP2, and report the command in MAX3 diagnostics.

**Architecture:** Add a focused `Ps1CdromState` component that owns bank selection, one-byte RESULT state, HINTSTS/HINTMSK state, command accounting, and IRQ edge detection. `Ps1MemoryBus` routes only approved 8-bit ports and owns I_STAT/I_MASK latching; `Ps1BootRuntime` seeds post-BIOS state, synchronizes `(I_STAT & I_MASK)` to R3000A external IP2, and records real device commands in `Ps1BootReport`.

**Tech Stack:** C++20, CMake 3.20+, existing R3000A reference executor and PS1 memory bus, synthetic fixtures only, GitHub Actions Linux + Windows/MSVC 2022.

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

- Create `src/core/ps1_cdrom_state.h`: public CD-ROM device API and compact I/O/event types.
- Create `src/core/ps1_cdrom_state.cpp`: banked 8-bit port semantics, command `0x01`, one-byte RESULT, IRQ edge logic, deterministic hash.
- Create `tests/test_ps1_cdrom_state.cpp`: pure device tests.
- Modify `src/core/ps1_memory_bus.h/.cpp`: own/expose `Ps1CdromState`, route approved ports, latch I_STAT bit 2, support `read16(I_STAT)`, preserve strict widths.
- Modify `tests/test_ps1_memory_bus.cpp`: reproduce the six commercial MMIO events and negative strictness cases.
- Modify `src/core/ps1_boot_runtime.cpp`: seed post-BIOS CD-ROM state, synchronize IP2, record commands, classify unsupported commands.
- Modify `tests/test_ps1_boot_runtime.cpp`: synthetic bootstrap, IRQ, reporting, and stop-reason tests.
- Modify `CMakeLists.txt`: compile/register the new component and test.

---

### Task 1: Isolated `Ps1CdromState`

**Files:**
- Create: `src/core/ps1_cdrom_state.h`
- Create: `src/core/ps1_cdrom_state.cpp`
- Create: `tests/test_ps1_cdrom_state.cpp`
- Modify: `CMakeLists.txt`

**Interfaces:**

```cpp
enum class Ps1CdromIoStatus : std::uint8_t {
    ok,
    unsupported_register,
    unsupported_command,
};

struct Ps1CdromIoResult {
    Ps1CdromIoStatus status{Ps1CdromIoStatus::unsupported_register};
    std::uint8_t value{};
};

struct Ps1CdromCommandEvent {
    std::uint64_t sequence{};
    std::uint8_t command{};
    std::uint8_t index{};
    std::uint8_t status{};
};

class Ps1CdromState {
public:
    void seed_post_bios(std::uint8_t drive_status, std::uint8_t interrupt_enable) noexcept;
    Ps1CdromIoResult read8(std::uint32_t physical) noexcept;
    Ps1CdromIoResult write8(std::uint32_t physical, std::uint8_t value) noexcept;
    bool take_irq_rising_edge() noexcept;
    [[nodiscard]] bool irq_line() const noexcept;
    [[nodiscard]] std::uint64_t command_count() const noexcept;
    [[nodiscard]] const std::optional<Ps1CdromCommandEvent>& last_command_event() const noexcept;
    [[nodiscard]] std::uint8_t index() const noexcept;
    [[nodiscard]] std::uint8_t drive_status() const noexcept;
    [[nodiscard]] std::uint8_t interrupt_enable() const noexcept;
    [[nodiscard]] std::uint8_t interrupt_status() const noexcept;
    [[nodiscard]] std::uint64_t diagnostic_state_hash() const noexcept;
};
```

- [ ] **Step 1: Write the RED device test and register the target**

Create `tests/test_ps1_cdrom_state.cpp` with the existing `CHECK` harness. Exercise this exact sequence on two devices so deterministic hashing compares equivalent state:

```cpp
static void run_observed_sequence(jojo::Ps1CdromState& cdrom) {
    cdrom.seed_post_bios(0x02u, 0x1Fu);
    CHECK(cdrom.write8(0x1F801800u, 0x01u).status == jojo::Ps1CdromIoStatus::ok);
    const auto hintsts = cdrom.read8(0x1F801803u);
    CHECK(hintsts.status == jojo::Ps1CdromIoStatus::ok);
    CHECK(hintsts.value == 0xE0u);
    CHECK(cdrom.write8(0x1F801800u, 0x00u).status == jojo::Ps1CdromIoStatus::ok);
    CHECK(cdrom.write8(0x1F801803u, 0x00u).status == jojo::Ps1CdromIoStatus::ok);
    CHECK(cdrom.write8(0x1F801800u, 0x00u).status == jojo::Ps1CdromIoStatus::ok);
    CHECK(cdrom.write8(0x1F801801u, 0x01u).status == jojo::Ps1CdromIoStatus::ok);
}

int main() {
    jojo::Ps1CdromState first;
    CHECK(first.index() == 0u);
    CHECK(first.drive_status() == 0u);
    CHECK(first.interrupt_enable() == 0u);
    CHECK(first.interrupt_status() == 0u);
    CHECK(first.command_count() == 0u);
    CHECK(!first.irq_line());

    run_observed_sequence(first);
    CHECK(first.drive_status() == 0x02u);
    CHECK(first.interrupt_enable() == 0x1Fu);
    CHECK(first.command_count() == 1u);
    CHECK(first.interrupt_status() == 3u);
    CHECK(first.irq_line());
    CHECK(first.take_irq_rising_edge());
    CHECK(!first.take_irq_rising_edge());

    const auto& event = first.last_command_event();
    CHECK(event.has_value());
    if (event) {
        CHECK(event->sequence == 1u);
        CHECK(event->command == 0x01u);
        CHECK(event->index == 0u);
        CHECK(event->status == 0x02u);
    }

    const auto result = first.read8(0x1F801801u);
    CHECK(result.status == jojo::Ps1CdromIoStatus::ok);
    CHECK(result.value == 0x02u);
    CHECK(first.read8(0x1F801801u).status == jojo::Ps1CdromIoStatus::unsupported_register);

    jojo::Ps1CdromState left;
    jojo::Ps1CdromState right;
    run_observed_sequence(left);
    run_observed_sequence(right);
    CHECK(left.diagnostic_state_hash() == right.diagnostic_state_hash());

    CHECK(left.write8(0x1F801801u, 0x02u).status == jojo::Ps1CdromIoStatus::unsupported_command);
    CHECK(left.write8(0x1F801803u, 0x01u).status == jojo::Ps1CdromIoStatus::unsupported_register);
    CHECK(left.write8(0x1F801800u, 0x03u).status == jojo::Ps1CdromIoStatus::ok);
    CHECK(left.read8(0x1F801803u).status == jojo::Ps1CdromIoStatus::unsupported_register);
    return failures ? 1 : 0;
}
```

Modify CMake exactly by adding `src/core/ps1_cdrom_state.cpp` to `jojo_core` and adding:

```cmake
add_jojo_test(jojo_ps1_cdrom_state_tests tests/test_ps1_cdrom_state.cpp)
```

- [ ] **Step 2: Verify RED**

```bash
cmake -S . -B build
cmake --build build --target jojo_ps1_cdrom_state_tests -j2
```

Expected: compilation fails because `core/ps1_cdrom_state.h`/`Ps1CdromState` does not exist.

- [ ] **Step 3: Implement minimal device semantics**

Private state:

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

Implement exactly:

```text
write8 0x1F801800: index_ = value & 3; ok.
read8  0x1F801803: only index 1; return 0xE0 | (interrupt_status_ & 0x1F).
write8 0x1F801803: only index 0 and value 0; ok/no effect.
write8 0x1F801801: only index 0; command 0x01 supported, all other bytes unsupported_command.
command 0x01: response_=drive_status_; interrupt_status_=3; ++command_count_;
              last_command_event_={command_count_,0x01,index_,drive_status_}; recompute IRQ.
read8 0x1F801801: only index 0 with response present; return response and clear slot.
all other reads/writes: unsupported_register.
```

IRQ recomputation:

```cpp
const bool next = (interrupt_enable_ & interrupt_status_ & 0x1Fu) != 0u;
if (!irq_line_ && next) irq_rising_edge_pending_ = true;
irq_line_ = next;
```

`seed_post_bios` sets provided drive status/mask, resets index/status/response/counter/event/IRQ fields, then recomputes from a false line. FNV-1a hash includes every private field above, including optional-presence bits and event fields.

- [ ] **Step 4: Verify GREEN**

```bash
cmake --build build --target jojo_ps1_cdrom_state_tests -j2
ctest --test-dir build -R jojo_ps1_cdrom_state_tests --output-on-failure
```

Expected: PASS.

- [ ] **Step 5: Regression smoke and commit**

```bash
ctest --test-dir build -R "jojo_ps1_(memory_bus|boot_runtime|diagnostic_frontier|max3_explorer)_tests" --output-on-failure
git add CMakeLists.txt src/core/ps1_cdrom_state.h src/core/ps1_cdrom_state.cpp tests/test_ps1_cdrom_state.cpp
git commit -m "feat: add minimal PS1 CD-ROM state"
```

Expected: selected tests PASS before commit.

---

### Task 2: Route observed CD-ROM MMIO and latch I_STAT IRQ2

**Files:**
- Modify: `src/core/ps1_memory_bus.h`
- Modify: `src/core/ps1_memory_bus.cpp`
- Modify: `tests/test_ps1_memory_bus.cpp`

**Interfaces produced:**

```cpp
[[nodiscard]] std::uint16_t interrupt_status() const noexcept;
[[nodiscard]] Ps1CdromState& cdrom() noexcept;
[[nodiscard]] const Ps1CdromState& cdrom() const noexcept;
[[nodiscard]] const std::optional<std::uint8_t>& last_unsupported_cdrom_command() const noexcept;
void clear_last_unsupported_cdrom_command() noexcept;
```

- [ ] **Step 1: Add RED assertions for the exact commercial sequence**

Add an isolated scope to `tests/test_ps1_memory_bus.cpp`:

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
    CHECK(!cd_bus.last_diagnostic_mmio_probe());

    const auto istat16 = cd_bus.read16(0x1F801070u);
    CHECK(istat16.status == jojo::R3000aBusStatus::ok);
    CHECK(istat16.value == 0x0004u);
    CHECK(cd_bus.read32(0x1F801070u).status == jojo::R3000aBusStatus::unsupported);

    CHECK(cd_bus.write16(0x1F801070u, 0x0000u).status == jojo::R3000aBusStatus::ok);
    CHECK(cd_bus.interrupt_status() == 0u);

    cd_bus.clear_last_unsupported_cdrom_command();
    CHECK(cd_bus.write8(0x1F801801u, 0x02u).status == jojo::R3000aBusStatus::unsupported);
    CHECK(cd_bus.last_unsupported_cdrom_command().has_value());
    CHECK(cd_bus.read16(0x1F801800u).status == jojo::R3000aBusStatus::unsupported);
    CHECK(cd_bus.read32(0x1F801800u).status == jojo::R3000aBusStatus::unsupported);
}
```

Add two fresh buses that run the same approved sequence and assert equal `diagnostic_state_hash()` at the same final point.

- [ ] **Step 2: Verify RED**

```bash
cmake --build build --target jojo_ps1_memory_bus_tests -j2
ctest --test-dir build -R jojo_ps1_memory_bus_tests --output-on-failure
```

Expected: missing bus accessors and/or speculative MMIO assertions fail.

- [ ] **Step 3: Implement bus ownership/routing**

In the header include `core/ps1_cdrom_state.h`, add:

```cpp
Ps1CdromState cdrom_{};
std::optional<std::uint8_t> last_unsupported_cdrom_command_{};
```

In `ps1_memory_bus.cpp` add:

```cpp
constexpr std::uint32_t kCdromIndexStatus = 0x1F801800u;
constexpr std::uint32_t kCdromResponseCommand = 0x1F801801u;
constexpr std::uint32_t kCdromRequestInterrupt = 0x1F801803u;
constexpr std::uint16_t kInterruptCdrom = 1u << 2;
```

Route only `read8`/`write8` for those three addresses through `cdrom_`. Map `ok` to bus `ok`; map both unsupported statuses to bus `unsupported` and fill `last_unsupported_`. If device status is `unsupported_command`, also store the byte in `last_unsupported_cdrom_command_`.

After each successful CD-ROM write:

```cpp
if (cdrom_.take_irq_rising_edge()) interrupt_status_ |= kInterruptCdrom;
```

Add `read16(I_STAT)` returning `interrupt_status_`; keep `read32(I_STAT)` untouched/strict. Hash `cdrom_.diagnostic_state_hash()` as a u64 in `Ps1MemoryBus::diagnostic_state_hash()`.

- [ ] **Step 4: Verify GREEN and strict widths**

```bash
cmake --build build --target jojo_ps1_memory_bus_tests -j2
ctest --test-dir build -R jojo_ps1_memory_bus_tests --output-on-failure
```

Expected: PASS.

- [ ] **Step 5: Commit**

```bash
git add src/core/ps1_memory_bus.h src/core/ps1_memory_bus.cpp tests/test_ps1_memory_bus.cpp
git commit -m "feat: route PS1 CD-ROM command MMIO"
```

---

### Task 3: Seed post-BIOS state and bridge I_STAT/I_MASK to R3000A IP2

**Files:**
- Modify: `src/core/ps1_boot_runtime.cpp`
- Modify: `tests/test_ps1_boot_runtime.cpp`

- [ ] **Step 1: Add RED bootstrap test**

```cpp
static void test_runtime_seeds_post_bios_cdrom_state() {
    auto runtime = make_runtime({test_mips::j(0x02u, 0x80010000u >> 2), 0u});
    CHECK(runtime.bus().cdrom().drive_status() == 0x02u);
    CHECK(runtime.bus().cdrom().interrupt_enable() == 0x1Fu);
}
```

- [ ] **Step 2: Add RED enabled/masked IP2 programs**

At test-file scope add the exact test-only COP0 encoder:

```cpp
constexpr std::uint32_t mtc0(std::uint8_t rt, std::uint8_t rd) noexcept {
    return (0x10u << 26) | (0x04u << 21) |
           (std::uint32_t(rt) << 16) | (std::uint32_t(rd) << 11);
}
```

Enabled program (Status IE + IM2 = `0x0401`, I_MASK bit 2 = `0x0004`):

```cpp
const std::vector<std::uint32_t> words{
    test_mips::i(0x0Fu, 0u, 8u, 0x1F80u),
    test_mips::i(0x0Du, 8u, 8u, 0x1074u),
    test_mips::i(0x09u, 0u, 9u, 0x0004u),
    test_mips::i(0x2Bu, 8u, 9u, 0x0000u),
    test_mips::i(0x09u, 0u, 13u, 0x0401u),
    mtc0(13u, 12u),
    test_mips::i(0x0Fu, 0u, 10u, 0x1F80u),
    test_mips::i(0x0Du, 10u, 10u, 0x1800u),
    test_mips::i(0x09u, 0u, 11u, 0x0000u),
    test_mips::i(0x28u, 10u, 11u, 0x0000u),
    test_mips::i(0x09u, 0u, 12u, 0x0001u),
    test_mips::i(0x28u, 10u, 12u, 0x0001u),
    0x00000000u,
};
```

Run with budget 32 and assert:

```cpp
CHECK(report.stop_reason == jojo::Ps1BootStopReason::cpu_boundary);
CHECK(report.interrupts_accepted == 1u);
CHECK(runtime.bus().interrupt_status() == 0x0004u);
CHECK((runtime.cpu_state().cop0.cause & 0x00000400u) != 0u);
```

Masked program is identical except instruction 3 loads `0x0000` into `t1`; append:

```cpp
test_mips::j(0x02u, 0x80010030u >> 2),
0x00000000u,
```

after the command, run with budget 32, and assert `interrupts_accepted == 0u` while `interrupt_status() == 0x0004u`.

- [ ] **Step 3: Verify RED**

```bash
cmake --build build --target jojo_ps1_boot_runtime_tests -j2
ctest --test-dir build -R jojo_ps1_boot_runtime_tests --output-on-failure
```

Expected: bootstrap and/or IP2 assertions fail.

- [ ] **Step 4: Implement seed + synchronization**

In `Ps1BootRuntime::create`, after loading the executable and before return:

```cpp
runtime.bus_.cdrom().seed_post_bios(0x02u, 0x1Fu);
```

Add file-local helper:

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

Call it as the first operation in every `while` iteration, before BIOS dispatch, syscall fast-path logic, opcode fetch, and `step_r3000a`.

- [ ] **Step 5: Verify GREEN + COP0 regressions**

```bash
cmake --build build --target jojo_ps1_boot_runtime_tests jojo_r3000a_exception_tests jojo_r3000a_cop0_tests -j2
ctest --test-dir build -R "jojo_ps1_boot_runtime_tests|jojo_r3000a_exception_tests|jojo_r3000a_cop0_tests" --output-on-failure
```

Expected: PASS.

- [ ] **Step 6: Commit**

```bash
git add src/core/ps1_boot_runtime.cpp tests/test_ps1_boot_runtime.cpp
git commit -m "feat: bridge PS1 interrupt controller to IP2"
```

---

### Task 4: Real CD-ROM command reporting and strict stop reason

**Files:**
- Modify: `src/core/ps1_boot_runtime.cpp`
- Modify: `tests/test_ps1_boot_runtime.cpp`

- [ ] **Step 1: Add RED command-report test**

Use this synthetic program with I_MASK left at zero:

```cpp
const std::vector<std::uint32_t> words{
    test_mips::i(0x0Fu, 0u, 8u, 0x1F80u),
    test_mips::i(0x0Du, 8u, 8u, 0x1800u),
    test_mips::i(0x09u, 0u, 9u, 0x0000u),
    test_mips::i(0x28u, 8u, 9u, 0x0000u),
    test_mips::i(0x09u, 0u, 10u, 0x0001u),
    test_mips::i(0x28u, 8u, 10u, 0x0001u),
    test_mips::j(0x02u, 0x80010018u >> 2),
    0x00000000u,
};
```

Set `instruction_budget=32`, `diagnostic_mmio_probe=true`, `mmio_event_capacity=4`, then assert:

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

For unsupported command, replace `0x0001u` with `0x0002u` and assert:

```cpp
CHECK(report.stop_reason == jojo::Ps1BootStopReason::device_command_unimplemented);
CHECK(report.cdrom_command_count == 0u);
CHECK(report.unsupported_access.has_value());
```

- [ ] **Step 2: Verify RED**

```bash
cmake --build build --target jojo_ps1_boot_runtime_tests -j2
ctest --test-dir build -R jojo_ps1_boot_runtime_tests --output-on-failure
```

Expected: reporting remains empty and/or unsupported command is still generic MMIO.

- [ ] **Step 3: Implement reporting/progress/classification**

At the beginning of `run()`, before defining `finish`:

```cpp
const auto cdrom_before = bus_.cdrom().command_count();
```

Inside `finish`:

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

Immediately before `step_r3000a`:

```cpp
const auto command_count_before_step = bus_.cdrom().command_count();
bus_.clear_last_unsupported_cdrom_command();
```

After a retired step:

```cpp
if (bus_.cdrom().command_count() != command_count_before_step) {
    const auto& event = bus_.cdrom().last_command_event();
    if (event) {
        record_recent_cdrom(report,
            Ps1CdromCommandSummary{event->command, event->index, event->status},
            options.mmio_event_capacity);
    }
    instructions_since_progress = 0u;
}
```

For a failed step, after capturing `report.unsupported_access`, classify a present `last_unsupported_cdrom_command()` as `Ps1BootStopReason::device_command_unimplemented` before generic MMIO classification. Preserve `report.unsupported_access`.

- [ ] **Step 4: Verify GREEN + MAX3/report regressions**

```bash
cmake --build build --target jojo_ps1_boot_runtime_tests jojo_ps1_max3_explorer_tests jojo_ps1_boot_report_io_tests -j2
ctest --test-dir build -R "jojo_ps1_boot_runtime_tests|jojo_ps1_max3_explorer_tests|jojo_ps1_boot_report_io_tests" --output-on-failure
```

Expected: PASS; identical runs produce identical command summaries and runtime diagnostic hashes.

- [ ] **Step 5: Commit**

```bash
git add src/core/ps1_boot_runtime.cpp tests/test_ps1_boot_runtime.cpp
git commit -m "feat: report PS1 CD-ROM commands"
```

---

### Task 5: Strictness audit and cross-platform verification

**Files:** Modify only if a regression test exposes a defect in Tasks 1-4. Do not add new CD-ROM commands, HCLRCTL, DMA3, GPU, or presentation behavior.

- [ ] **Step 1: Verify explicit negative coverage**

Tests must assert these remain unsupported:

```text
read16/read32 at CD-ROM ports 0x1F801800..0x1F801803
read32(I_STAT)
CD-ROM command byte other than 0x01
bank-1 write 0x1F801803 (HCLRCTL)
non-zero bank-0 write 0x1F801803 (HCHPCTL)
bank-3 read 0x1F801803
parameter FIFO and RDDATA operations not approved by the spec
DMA3 registers/transfers unchanged
```

If a production path is too permissive, add the failing direct assertion first, run it to RED, make the smallest strictness fix, then rerun the affected test.

- [ ] **Step 2: Run complete Linux suite and gates**

```bash
cmake -S . -B build
cmake --build build -j2
ctest --test-dir build --output-on-failure
cmake -DJOJO_SOURCE_DIR="$PWD" -P cmake/CheckProductionReadiness.cmake
cmake -DJOJO_SOURCE_DIR="$PWD" -P cmake/CheckProductionReadinessNegative.cmake
cmake -DJOJO_SOURCE_DIR="$PWD" -P cmake/CheckPs1ActiveArchitecture.cmake
```

Expected: 100% PASS.

- [ ] **Step 3: Review exact diff from approved spec commit**

```bash
git diff fbe14615ceab3bd50f261270e5b91dddee42b2ce...HEAD -- \
  CMakeLists.txt \
  src/core/ps1_cdrom_state.h src/core/ps1_cdrom_state.cpp \
  src/core/ps1_memory_bus.h src/core/ps1_memory_bus.cpp \
  src/core/ps1_boot_runtime.cpp \
  tests/test_ps1_cdrom_state.cpp tests/test_ps1_memory_bus.cpp tests/test_ps1_boot_runtime.cpp
```

Reject unrelated refactors, new commands, DMA3, graphics work, or proprietary fixtures.

- [ ] **Step 4: Require exact-SHA GitHub Actions success**

Push the branch and verify the `build` workflow `head_sha` equals branch HEAD. Require Linux and Windows/MSVC jobs to end `success`, including production readiness, PS1 architecture, observed-disc revision, UDP checks, and complete CTest suites.

- [ ] **Step 5: Verify Windows artifact**

Download `JOJO-Recompiled-Windows-x64` from that exact successful run. Verify the GitHub artifact SHA-256 equals the downloaded ZIP SHA-256, inspect that the ZIP contains only `JOJO-Recompiled.exe`, and compute the EXE SHA-256.

- [ ] **Step 6: Evidence handoff**

Deliver the verified Windows ZIP and request exactly one `EXECUTAR CHECKPOINT` run. The next checkpoint alone decides whether the following milestone is HCLRCTL, RESULT/HSTS polling, another CD-ROM command, interrupt-handler behavior, or a different subsystem.
