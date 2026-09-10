# PS1 CD-ROM Command 01 + IRQ Bridge v0 Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Replace the four remaining speculative CD-ROM MMIO dependencies from `m3a-checkpoint(20260910-093208).txt` with deterministic command `0x01` device semantics and a real CD-ROM IRQ -> I_STAT -> R3000A IP2 path.

**Architecture:** Add a focused `Ps1CdromState` component that owns bank selection, drive status, HINTMSK/HINTSTS, a one-byte RESULT slot, command accounting, and IRQ-edge state. `Ps1MemoryBus` routes only the approved 8-bit CD-ROM accesses and latches IRQ2 into I_STAT; `Ps1BootRuntime` seeds the synthesized post-BIOS state, synchronizes aggregate I_STAT/I_MASK into external IP2 at the guest-execution boundary, records command progress, and classifies unsupported command bytes.

**Tech Stack:** C++20, CMake >= 3.20, existing `R3000aBus`/`R3000aState`, CHECK-based C++ tests, GitHub Actions Linux + Windows x64/MSVC 2022.

**Spec:** `docs/superpowers/specs/2026-09-10-ps1-cdrom-command01-irq-v0-design.md`

## Global Constraints

- Work on `feature/ps1-gp0-dma2-bios-frontier-v0`; keep unrelated history and behavior unchanged.
- Every production behavior follows TDD RED -> GREEN with an independently reviewable commit.
- Windows x64/MSVC 2022 CI is authoritative; Linux must also be green on the same exact SHA.
- Use synthetic fixtures only. Do not add commercial disc data, BIOS bytes, RAM dumps, proprietary assets, or derived payloads to source, tests, CI artifacts, or releases.
- Implement only CD-ROM command `0x01` in this milestone.
- Approved guest-visible CD-ROM accesses are: 8-bit write `0x1F801800`, bank-1 8-bit read `0x1F801803`, bank-0 8-bit write `0x1F801803` with value zero, bank-0 8-bit write `0x1F801801` with command `0x01`, and bank-0 8-bit read `0x1F801801` only as the one-byte RESULT for command `0x01`.
- Keep all other CD-ROM commands, 16/32-bit CD-ROM accesses, bank-1 HCLRCTL writes, bank-3 HINTSTS reads, parameter FIFO, RDDATA, DMA3, and CD audio behavior strict.
- Standalone `Ps1CdromState` and `Ps1MemoryBus` keep neutral reset state. Only `Ps1BootRuntime::create` seeds `drive_status=0x02` and `interrupt_enable=0x1F` as synthesized post-BIOS state.
- A CD-ROM device-line false->true transition latches I_STAT bit 2. Clearing I_STAT while the device line remains high must not relatch it without another device-line rising edge.
- R3000A external pending bit `0x04` mirrors `(I_STAT & I_MASK) != 0` while preserving all other external pending bits.
- Add `read16(I_STAT)` only. `read32(I_STAT)` remains strict.
- Real supported CD-ROM commands count as diagnostic progress and appear in the existing `Ps1BootReport::recent_cdrom_commands` using `Ps1BootOptions::mmio_event_capacity` as the retention cap.
- Unsupported command bytes surface as `Ps1BootStopReason::device_command_unimplemented`; unsupported register directions/widths remain `mmio_unimplemented`.
- MAX3 state identity must include guest-observable CD-ROM state and the approved sequence must no longer appear as speculative MMIO dependencies.
- Do not implement HCLRCTL acknowledgement, Setloc/ReadN/ReadS, sector I/O, DMA3, XA/CD-DA, GPU drawing, DMA2 linked-list execution, or native x64 lowering here.

---

## File Structure

**Create**
- `src/core/ps1_cdrom_state.h` — CD-ROM v0 public state-machine contract.
- `src/core/ps1_cdrom_state.cpp` — banked 8-bit ports, command `0x01`, RESULT, IRQ edge, deterministic hash.
- `tests/test_ps1_cdrom_state.cpp` — pure device tests.

**Modify**
- `CMakeLists.txt` — compile/register the new component and test.
- `src/core/ps1_memory_bus.h` — own/expose `Ps1CdromState`, interrupt status, and unsupported-command marker.
- `src/core/ps1_memory_bus.cpp` — route exact ports, latch IRQ2, add `read16(I_STAT)`, preserve strictness, hash device state.
- `src/core/ps1_boot_runtime.cpp` — seed post-BIOS state, synchronize IP2, record commands/progress, classify unsupported commands.
- `tests/test_ps1_memory_bus.cpp` — reproduce the six observed MMIO events and edge/strictness behavior.
- `tests/test_ps1_boot_runtime.cpp` — bootstrap, IRQ delivery/masking, command reporting/capacity, stop reason.
- `tests/test_ps1_max3_explorer.cpp` — verify path identity and removal of the four speculative dependencies.

No new public boot-report fields are required; `Ps1CdromCommandSummary`, `cdrom_command_count`, and `recent_cdrom_commands` already exist.

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
    [[nodiscard]] Ps1CdromIoResult read8(std::uint32_t physical) noexcept;
    [[nodiscard]] Ps1CdromIoResult write8(std::uint32_t physical, std::uint8_t value) noexcept;
    [[nodiscard]] bool take_irq_rising_edge() noexcept;
    [[nodiscard]] bool irq_line() const noexcept;
    [[nodiscard]] std::uint64_t command_count() const noexcept;
    [[nodiscard]] const std::optional<Ps1CdromCommandEvent>& last_command_event() const noexcept;
    [[nodiscard]] const std::optional<std::uint8_t>& last_unsupported_command() const noexcept;
    void clear_last_unsupported_command() noexcept;
    [[nodiscard]] std::uint8_t index() const noexcept;
    [[nodiscard]] std::uint8_t drive_status() const noexcept;
    [[nodiscard]] std::uint8_t interrupt_enable() const noexcept;
    [[nodiscard]] std::uint8_t interrupt_status() const noexcept;
    [[nodiscard]] std::uint64_t diagnostic_state_hash() const noexcept;
};
```

- [ ] **Step 1: Register the RED target and write the failing device test**

Add `src/core/ps1_cdrom_state.cpp` to `jojo_core` and:

```cmake
add_jojo_test(jojo_ps1_cdrom_state_tests tests/test_ps1_cdrom_state.cpp)
```

Create `tests/test_ps1_cdrom_state.cpp` with the repository's existing `CHECK` harness and this core sequence:

```cpp
static void observed_sequence(jojo::Ps1CdromState& cdrom) {
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
```

In `main`, assert neutral reset values, capture `neutral_hash`, call `observed_sequence`, then assert:

```cpp
CHECK(cdrom.drive_status() == 0x02u);
CHECK(cdrom.interrupt_enable() == 0x1Fu);
CHECK(cdrom.interrupt_status() == 3u);
CHECK(cdrom.command_count() == 1u);
CHECK(cdrom.irq_line());
CHECK(cdrom.take_irq_rising_edge());
CHECK(!cdrom.take_irq_rising_edge());
CHECK(cdrom.diagnostic_state_hash() != neutral_hash);

const auto event = cdrom.last_command_event();
CHECK(event.has_value());
if (event) {
    CHECK(event->sequence == 1u);
    CHECK(event->command == 0x01u);
    CHECK(event->index == 0u);
    CHECK(event->status == 0x02u);
}

const auto before_result_hash = cdrom.diagnostic_state_hash();
const auto result = cdrom.read8(0x1F801801u);
CHECK(result.status == jojo::Ps1CdromIoStatus::ok);
CHECK(result.value == 0x02u);
CHECK(cdrom.diagnostic_state_hash() != before_result_hash);
CHECK(cdrom.read8(0x1F801801u).status == jojo::Ps1CdromIoStatus::unsupported_register);
```

Use two additional fresh devices, run the same sequence on both, and assert equal hashes. Assert command `0x02` returns `unsupported_command`, non-zero bank-0 HCHPCTL returns `unsupported_register`, and bank-3 HINTSTS read returns `unsupported_register`.

- [ ] **Step 2: Run RED**

```bash
cmake -S . -B build -DCMAKE_BUILD_TYPE=Release
cmake --build build --target jojo_ps1_cdrom_state_tests --parallel 2
```

Expected: compile failure because `core/ps1_cdrom_state.h` does not exist.

- [ ] **Step 3: Implement the minimal device**

Private state is exactly:

```cpp
std::uint8_t index_{};
std::uint8_t drive_status_{};
std::uint8_t interrupt_enable_{};
std::uint8_t interrupt_status_{};
std::optional<std::uint8_t> response_{};
std::uint64_t command_count_{};
std::optional<Ps1CdromCommandEvent> last_command_event_{};
std::optional<std::uint8_t> last_unsupported_command_{};
bool irq_line_{};
bool irq_rising_edge_pending_{};
```

Implement only these rules:

```text
write8 0x1F801800: index_ = value & 3; ok.
read8  0x1F801803: only index 1; return 0xE0 | (interrupt_status_ & 0x1F).
write8 0x1F801803: only index 0 and value 0; ok/no effect.
write8 0x1F801801: only index 0; command 0x01 supported; other command bytes unsupported_command.
command 0x01: require response_ empty; response_=drive_status_; interrupt_status_=3;
              ++command_count_; last event={count,0x01,index_,drive_status_}; recompute IRQ.
read8  0x1F801801: only index 0 with response_ present; return value and consume response_.
all other register/direction cases: unsupported_register.
```

A second command `0x01` while `response_` is occupied returns `unsupported_command`; v0 does not invent multi-entry FIFO behavior.

IRQ recomputation is:

```cpp
const bool next = (interrupt_enable_ & interrupt_status_ & 0x1Fu) != 0u;
if (!irq_line_ && next) irq_rising_edge_pending_ = true;
irq_line_ = next;
```

`seed_post_bios` sets the supplied drive status/mask and resets index, interrupt status, response, counters/events, unsupported-command marker, and IRQ fields. FNV-1a hashing includes every private field, including optional-presence bits and optional values.

- [ ] **Step 4: Run GREEN and regressions**

```bash
cmake --build build --target jojo_ps1_cdrom_state_tests --parallel 2
ctest --test-dir build -R jojo_ps1_cdrom_state_tests --output-on-failure
ctest --test-dir build -R "jojo_ps1_(memory_bus|boot_runtime|diagnostic_frontier|max3_explorer)_tests" --output-on-failure
```

Expected: all selected tests pass.

- [ ] **Step 5: Commit**

```bash
git add CMakeLists.txt src/core/ps1_cdrom_state.h src/core/ps1_cdrom_state.cpp tests/test_ps1_cdrom_state.cpp
git commit -m "feat: add minimal PS1 CD-ROM state"
```

---

### Task 2: Route observed CD-ROM MMIO and latch IRQ2 in I_STAT

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

- [ ] **Step 1: Add the failing bus test for the six exact checkpoint events**

In an isolated test scope:

```cpp
jojo::Ps1MemoryBus bus;
bus.cdrom().seed_post_bios(0x02u, 0x1Fu);
bus.set_diagnostic_mmio_probe_enabled(true);

bus.clear_last_diagnostic_mmio_probe();
CHECK(bus.write8(0x1F801800u, 0x01u).status == jojo::R3000aBusStatus::ok);
CHECK(!bus.last_diagnostic_mmio_probe());
CHECK(bus.read8(0x1F801803u).value == 0xE0u);
CHECK(!bus.last_diagnostic_mmio_probe());
CHECK(bus.write8(0x1F801800u, 0x00u).status == jojo::R3000aBusStatus::ok);
CHECK(bus.write8(0x1F801803u, 0x00u).status == jojo::R3000aBusStatus::ok);
CHECK(bus.write8(0x1F801800u, 0x00u).status == jojo::R3000aBusStatus::ok);
CHECK(bus.write8(0x1F801801u, 0x01u).status == jojo::R3000aBusStatus::ok);
CHECK(!bus.last_diagnostic_mmio_probe());
CHECK(bus.interrupt_status() == 0x0004u);

const auto istat = bus.read16(0x1F801070u);
CHECK(istat.status == jojo::R3000aBusStatus::ok);
CHECK(istat.value == 0x0004u);
CHECK(bus.read32(0x1F801070u).status == jojo::R3000aBusStatus::unsupported);

CHECK(bus.write16(0x1F801070u, 0x0000u).status == jojo::R3000aBusStatus::ok);
CHECK(bus.interrupt_status() == 0u);
// Device IRQ is still high, so a harmless successful index write must not create a second edge.
CHECK(bus.write8(0x1F801800u, 0x00u).status == jojo::R3000aBusStatus::ok);
CHECK(bus.interrupt_status() == 0u);
```

Add negative assertions for 16/32-bit CD-ROM accesses, bank-3 HINTSTS, bank-1 HCLRCTL, non-zero bank-0 HCHPCTL, and command `0x02`. For command `0x02`, require `last_unsupported_cdrom_command()==0x02`.

- [ ] **Step 2: Run RED**

```bash
cmake --build build --target jojo_ps1_memory_bus_tests --parallel 2
ctest --test-dir build -R jojo_ps1_memory_bus_tests --output-on-failure
```

Expected: missing accessors and/or current speculative-shadow behavior fail the new assertions.

- [ ] **Step 3: Wire `Ps1CdromState` into the bus**

Include `core/ps1_cdrom_state.h`, add `Ps1CdromState cdrom_{};` and `std::optional<std::uint8_t> last_unsupported_cdrom_command_{};`.

Define:

```cpp
constexpr std::uint32_t kCdromIndexStatus = 0x1F801800u;
constexpr std::uint32_t kCdromResponseCommand = 0x1F801801u;
constexpr std::uint32_t kCdromRequestInterrupt = 0x1F801803u;
constexpr std::uint16_t kInterruptCdrom = 1u << 2;
```

In `read8`/`write8`, route only those three physical addresses to `cdrom_` before diagnostic-shadow fallback. Device `ok` maps to bus `ok`. `unsupported_register` and `unsupported_command` map to bus `unsupported` and populate `last_unsupported_`; `unsupported_command` also copies `cdrom_.last_unsupported_command()` into `last_unsupported_cdrom_command_`.

After every successful CD-ROM write:

```cpp
if (cdrom_.take_irq_rising_edge()) interrupt_status_ |= kInterruptCdrom;
```

Add `read16(I_STAT)` returning `interrupt_status_ & 0x07FFu`; leave `read32(I_STAT)` unchanged. Add the public accessors above. Include `cdrom_.diagnostic_state_hash()` in the existing bus FNV state hash.

- [ ] **Step 4: Run GREEN and strictness regression**

```bash
cmake --build build --target jojo_ps1_cdrom_state_tests jojo_ps1_memory_bus_tests --parallel 2
ctest --test-dir build -R "jojo_ps1_(cdrom_state|memory_bus)_tests" --output-on-failure
```

Expected: both pass; no diagnostic probe is emitted for the six approved accesses.

- [ ] **Step 5: Commit**

```bash
git add src/core/ps1_memory_bus.h src/core/ps1_memory_bus.cpp tests/test_ps1_memory_bus.cpp
git commit -m "feat: route observed PS1 CD-ROM MMIO"
```

---

### Task 3: Seed post-BIOS state and bridge I_STAT/I_MASK to R3000A IP2

**Files:**
- Modify: `src/core/ps1_boot_runtime.cpp`
- Modify: `tests/test_ps1_boot_runtime.cpp`

**Consumes:** `Ps1MemoryBus::interrupt_status()`, `interrupt_mask()`, and `cdrom()` from Task 2. Existing `R3000aState::external_interrupt_pending` bit `0x04` maps to COP0 IP2 in `step_r3000a`.

- [ ] **Step 1: Add RED bootstrap and interrupt-delivery tests**

Bootstrap:

```cpp
auto runtime = make_runtime({test_mips::j(0x02u, 0x80010000u >> 2), 0u});
CHECK(runtime.bus().cdrom().drive_status() == 0x02u);
CHECK(runtime.bus().cdrom().interrupt_enable() == 0x1Fu);
```

Add this test-only COP0 encoder:

```cpp
constexpr std::uint32_t mtc0(std::uint8_t rt, std::uint8_t rd) noexcept {
    return (0x10u << 26) | (0x04u << 21) |
           (std::uint32_t(rt) << 16) | (std::uint32_t(rd) << 11);
}
```

Enabled program:

```cpp
const std::vector<std::uint32_t> words{
    test_mips::i(0x0Fu, 0u, 8u, 0x1F80u),
    test_mips::i(0x0Du, 8u, 8u, 0x1074u),
    test_mips::i(0x09u, 0u, 9u, 0x0004u),
    test_mips::i(0x2Bu, 8u, 9u, 0x0000u), // I_MASK = CDROM
    test_mips::i(0x09u, 0u, 13u, 0x0401u),
    mtc0(13u, 12u),                         // COP0 Status IE + IM2
    test_mips::i(0x0Fu, 0u, 10u, 0x1F80u),
    test_mips::i(0x0Du, 10u, 10u, 0x1800u),
    test_mips::i(0x09u, 0u, 11u, 0x0000u),
    test_mips::i(0x28u, 10u, 11u, 0x0000u),
    test_mips::i(0x09u, 0u, 12u, 0x0001u),
    test_mips::i(0x28u, 10u, 12u, 0x0001u), // command 01
    0x00000000u,
};
```

Run with budget 32 and assert `interrupts_accepted==1`, `interrupt_status()==0x0004`, and COP0 Cause contains `0x00000400`.

For the masked case, make instruction 3 store zero to I_MASK and append a self-loop after command `0x01`; assert `interrupts_accepted==0` while I_STAT remains `0x0004`.

- [ ] **Step 2: Run RED**

```bash
cmake --build build --target jojo_ps1_boot_runtime_tests --parallel 2
ctest --test-dir build -R jojo_ps1_boot_runtime_tests --output-on-failure
```

Expected: bootstrap values and/or IP2 delivery assertions fail.

- [ ] **Step 3: Implement bootstrap and synchronization at the correct execution boundary**

In `Ps1BootRuntime::create`, after executable load succeeds:

```cpp
runtime.bus_.cdrom().seed_post_bios(0x02u, 0x1Fu);
```

Add:

```cpp
void sync_interrupt_controller_to_cpu(R3000aState& cpu, const Ps1MemoryBus& bus) noexcept {
    constexpr std::uint8_t kExternalIp2 = 0x04u;
    const bool active = (bus.interrupt_status() & bus.interrupt_mask()) != 0u;
    cpu.external_interrupt_pending = static_cast<std::uint8_t>(
        (cpu.external_interrupt_pending & static_cast<std::uint8_t>(~kExternalIp2)) |
        (active ? kExternalIp2 : 0u));
}
```

In `run()`, do **not** synchronize before BIOS-table HLE dispatch. Preserve the existing BIOS interception first. Once the loop has determined that the PC is not being handled as a BIOS table call, call `sync_interrupt_controller_to_cpu(cpu_, bus_)` immediately before opcode observation/syscall-preemption logic. This ensures `interrupt_would_preempt_syscall()` sees the current IP2 state and the same state reaches the subsequent `step_r3000a`.

- [ ] **Step 4: Run GREEN and COP0 regressions**

```bash
cmake --build build --target jojo_ps1_boot_runtime_tests jojo_r3000a_exception_tests jojo_r3000a_cop0_tests --parallel 2
ctest --test-dir build -R "jojo_ps1_boot_runtime_tests|jojo_r3000a_exception_tests|jojo_r3000a_cop0_tests" --output-on-failure
```

Expected: all pass.

- [ ] **Step 5: Commit**

```bash
git add src/core/ps1_boot_runtime.cpp tests/test_ps1_boot_runtime.cpp
git commit -m "feat: bridge PS1 interrupt controller to IP2"
```

---

### Task 4: Command reporting, diagnostic progress, and strict stop reason

**Files:**
- Modify: `src/core/ps1_boot_runtime.cpp`
- Modify: `tests/test_ps1_boot_runtime.cpp`

- [ ] **Step 1: Add RED reporting/capacity/classification tests**

Use a synthetic program that selects bank 0, writes command `0x01`, then self-loops. With `instruction_budget=32`, `diagnostic_mmio_probe=true`, and `mmio_event_capacity=4`, assert:

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

Run the same program with `mmio_event_capacity=0`; assert:

```cpp
CHECK(report.cdrom_command_count == 1u);
CHECK(report.recent_cdrom_commands.empty());
```

Replace command `0x01` with `0x02` and assert:

```cpp
CHECK(report.stop_reason == jojo::Ps1BootStopReason::device_command_unimplemented);
CHECK(report.cdrom_command_count == 0u);
CHECK(report.unsupported_access.has_value());
if (report.unsupported_access) {
    CHECK(report.unsupported_access->guest_address == 0x1F801801u);
    CHECK(report.unsupported_access->width == 1u);
    CHECK(report.unsupported_access->write);
    CHECK(report.unsupported_access->value == 0x02u);
}
```

- [ ] **Step 2: Run RED**

```bash
cmake --build build --target jojo_ps1_boot_runtime_tests --parallel 2
ctest --test-dir build -R jojo_ps1_boot_runtime_tests --output-on-failure
```

Expected: command reporting is empty/zero and unsupported command is generic MMIO until production changes are added.

- [ ] **Step 3: Implement per-instruction command observation without unbounded history**

At the start of `run()`:

```cpp
const auto cdrom_commands_before = bus_.cdrom().command_count();
```

In `finish`:

```cpp
report.cdrom_command_count = bus_.cdrom().command_count() - cdrom_commands_before;
```

Add a `record_recent_cdrom` helper matching the existing recent-event pattern and cap it with `options.mmio_event_capacity`.

Immediately before each `step_r3000a` call:

```cpp
const auto command_count_before_step = bus_.cdrom().command_count();
bus_.clear_last_unsupported_cdrom_command();
```

After a retired step:

```cpp
if (bus_.cdrom().command_count() != command_count_before_step) {
    if (const auto& event = bus_.cdrom().last_command_event(); event) {
        record_recent_cdrom(report,
            Ps1CdromCommandSummary{event->command, event->index, event->status},
            options.mmio_event_capacity);
    }
    instructions_since_progress = 0u;
}
```

For a failed step, after assigning `report.unsupported_access`, check `bus_.last_unsupported_cdrom_command()` before generic MMIO classification. If present, preserve the unsupported access/MMIO evidence and return `finish(Ps1BootStopReason::device_command_unimplemented)`.

- [ ] **Step 4: Run GREEN and report regressions**

```bash
cmake --build build --target jojo_ps1_boot_runtime_tests jojo_ps1_boot_report_io_tests --parallel 2
ctest --test-dir build -R "jojo_ps1_boot_runtime_tests|jojo_ps1_boot_report_io_tests" --output-on-failure
```

Expected: all pass.

- [ ] **Step 5: Commit**

```bash
git add src/core/ps1_boot_runtime.cpp tests/test_ps1_boot_runtime.cpp
git commit -m "feat: report PS1 CD-ROM command progress"
```

---

### Task 5: MAX3 dependency-removal contract, strictness audit, and cross-platform closure

**Files:**
- Modify: `tests/test_ps1_max3_explorer.cpp`
- Modify only when a failing approved assertion requires it: `src/core/ps1_cdrom_state.cpp`, `src/core/ps1_memory_bus.cpp`, `src/core/ps1_boot_runtime.cpp`

- [ ] **Step 1: Add the explicit MAX3 integration test**

Build a synthetic PS-X EXE with existing MIPS encoders that performs the six commercial events in order and then self-loops:

```text
write 0x01 -> 0x1F801800
read          0x1F801803
write 0x00 -> 0x1F801800
write 0x00 -> 0x1F801803
write 0x00 -> 0x1F801800
write 0x01 -> 0x1F801801
```

Explore with `max_branch_depth=0`, diagnostic probe enabled, and a bounded stagnation limit. Assert:

```cpp
CHECK(report.nodes.size() == 1u);
CHECK(report.nodes[0].path_cdrom_command_count == 1u);
CHECK(report.best_report.cdrom_command_count == 1u);
CHECK(std::none_of(report.dependencies.begin(), report.dependencies.end(), [](const auto& dep) {
    if (dep.kind != jojo::Ps1Max3DependencyKind::speculative_mmio) return false;
    return dep.address == 0x1F801800u || dep.address == 0x1F801801u || dep.address == 0x1F801803u;
}));
```

Also create two identical runtimes/buses, verify equal hashes after identical sequences, then consume RESULT on only one and verify the hashes differ. This locks MAX3 identity to guest-observable response state.

- [ ] **Step 2: Run the MAX3 integration test**

```bash
cmake --build build --target jojo_ps1_max3_explorer_tests --parallel 2
ctest --test-dir build -R jojo_ps1_max3_explorer_tests --output-on-failure
```

Expected after Tasks 1-4: PASS. If the integration assertion exposes a gap, add the failing assertion first, make only the smallest approved production fix, and rerun this command. Do not weaken production code merely to manufacture a RED at this integration layer; lower-layer tasks already established RED for each new behavior.

- [ ] **Step 3: Run explicit strictness regression**

Ensure tests assert these remain unsupported:

```text
16/32-bit CD-ROM ports 0x1F801800..0x1F801803
read32(I_STAT)
CD-ROM command byte != 0x01
second command 0x01 while RESULT is still occupied
bank-1 write 0x1F801803 (HCLRCTL)
non-zero bank-0 write 0x1F801803 (HCHPCTL)
bank-3 read 0x1F801803
parameter FIFO / RDDATA operations not approved by the spec
DMA3 registers/transfers unchanged
```

Run:

```bash
ctest --test-dir build -R "jojo_ps1_(cdrom_state|memory_bus|boot_runtime|max3_explorer|diagnostic_frontier|local_evidence)_tests" --output-on-failure
```

Expected: PASS.

- [ ] **Step 4: Commit the MAX3 contract and any strictly necessary fixes**

```bash
git add tests/test_ps1_max3_explorer.cpp
git add -u src/core/ps1_cdrom_state.cpp src/core/ps1_memory_bus.cpp src/core/ps1_boot_runtime.cpp
git commit -m "test: lock PS1 CD-ROM MAX3 frontier contract"
```

Before committing, inspect staged diff and unstage any unchanged or unrelated production file.

- [ ] **Step 5: Run complete Linux suite and production gates**

```bash
cmake -S . -B build -DCMAKE_BUILD_TYPE=Release
cmake --build build --parallel 2
cmake -DJOJO_SOURCE_DIR="$PWD" -P cmake/CheckProductionReadiness.cmake
cmake -DJOJO_SOURCE_DIR="$PWD" -P cmake/CheckProductionReadinessNegative.cmake
cmake -DJOJO_SOURCE_DIR="$PWD" -P cmake/CheckPs1ActiveArchitecture.cmake
ctest --test-dir build --output-on-failure
c++ -std=c++20 -Wall -Wextra -Wpedantic -Isrc tests/test_observed_disc_revision.cpp build/libjojo_core.a -ldl -pthread -o observed_disc_revision_tests
./observed_disc_revision_tests
c++ -std=c++20 -Wall -Wextra -Wpedantic -Isrc tests/test_network_transport.cpp src/core/network_protocol.cpp -o network_transport_tests
./network_transport_tests
```

Expected: every command exits zero.

- [ ] **Step 6: Review the exact implementation diff**

```bash
git diff fbe14615ceab3bd50f261270e5b91dddee42b2ce...HEAD -- \
  CMakeLists.txt \
  src/core/ps1_cdrom_state.h src/core/ps1_cdrom_state.cpp \
  src/core/ps1_memory_bus.h src/core/ps1_memory_bus.cpp \
  src/core/ps1_boot_runtime.cpp \
  tests/test_ps1_cdrom_state.cpp tests/test_ps1_memory_bus.cpp \
  tests/test_ps1_boot_runtime.cpp tests/test_ps1_max3_explorer.cpp
```

Reject unrelated refactors, extra CD-ROM commands, HCLRCTL, DMA3, graphics work, or proprietary fixtures.

- [ ] **Step 7: Require exact-SHA GitHub Actions success**

Push the branch and verify workflow `head_sha` equals the final branch HEAD. Require both jobs to complete successfully, including:

```text
Portable core / Linux
Windows x64 / MSVC 2022
Production readiness gate
PS1 active architecture gate
complete CTest suite
Observed disc revision contract
R2.5 direct UDP transport contract
Upload single executable
```

Do not claim completion from local Linux alone.

- [ ] **Step 8: Validate the exact Windows artifact and hand off evidence**

Download `JOJO-Recompiled-Windows-x64` from the successful final-SHA workflow and verify:

```text
ZIP contains exactly JOJO-Recompiled.exe
local ZIP SHA-256 equals GitHub Actions artifact digest
EXE is non-empty and its SHA-256 is recorded
```

Deliver that ZIP and request exactly one `EXECUTAR CHECKPOINT` run. The next checkpoint alone decides whether the next milestone is HCLRCTL, RESULT/HSTS polling, another CD-ROM command, interrupt-handler behavior, or a different subsystem.